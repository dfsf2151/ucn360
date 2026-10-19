#include "render/world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include "data/reader.h"

namespace ucn::render {

namespace {

// Canvas 2D "source-over" with globalAlpha; additive is "lighter". Truncating float blending is what matches
// Chrome closest (frame 1: 0.149 mean, the rest +-1 rounding; three 8-bit premultiplied variants measured worse).
// `ga` is globalAlpha in 0..255, worked out once per sprite (a float to int per pixel stalls a PPC core).
void put(uint8_t* d, const uint8_t* s, uint32_t ga, bool additive) {
    // the float blend, truncated, in integers: A = alpha8 * sa / 255 in 1/65025 units
    const uint32_t A = s[3] * ga;
    if (A == 0) return;
    for (int c = 0; c < 3; c++) {
        const uint32_t v = additive ? (d[c] * 65025u + s[c] * A) / 65025u : (s[c] * A + d[c] * (65025u - A)) / 65025u;
        d[c] = uint8_t(v > 255 ? 255 : v);
    }
    d[3] = 255;
}

uint32_t alpha8(double alpha) { return uint32_t(std::min(1.0, std::max(0.0, alpha)) * 255 + 0.5); }

// Rows [y0, y1) of the output, drawn by one worker (band `index` of kBands).
constexpr int kBands = 6;
struct Band { assets::Image* out; int32_t y0, y1, index; };

// Straight-alpha "source over", as the M2 compositor (exact integer path when nothing is transformed).
void draw(const Band& band, const Sprite& s) {
    if (s.alpha <= 0) return; // globalAlpha 0 draws nothing (the office keeps full-screen overlays faded out)
    const assets::Image& src = *s.img;
    assets::Image* out = band.out;
    const int32_t W = int32_t(out->width), Y0 = band.y0, Y1 = band.y1;
    const uint32_t ga = alpha8(s.alpha);
    if (s.angle == 0 && s.sx == 1 && s.sy == 1) {
        const int32_t dx = int32_t(std::floor(s.x - s.hot_x)), dy = int32_t(std::floor(s.y - s.hot_y));
        const int32_t x0 = std::max(0, dx), y0 = std::max(Y0, dy);
        const int32_t x1 = std::min(W, dx + int32_t(src.width)), y1 = std::min(Y1, dy + int32_t(src.height));
        const bool plain = ga == 255 && !s.additive;
        for (int32_t y = y0; y < y1; ++y) {
            const uint8_t* sr = src.rgba.data() + (size_t(y - dy) * src.width + size_t(x0 - dx)) * 4;
            uint8_t* dr = out->rgba.data() + (size_t(y) * size_t(W) + size_t(x0)) * 4;
            for (int32_t x = x0; x < x1; ++x, sr += 4, dr += 4) {
                if (plain && sr[3] == 255) std::memcpy(dr, sr, 4);
                else put(dr, sr, ga, s.additive);
            }
        }
        return;
    }
    if (s.angle == 0) { // scaled, not rotated: u depends on x only and v on y only, so table both
        const double w = src.width, h = src.height;
        const double ax = s.x - s.hot_x * s.sx, bx = s.x + (w - s.hot_x) * s.sx;
        const double ay = s.y - s.hot_y * s.sy, by = s.y + (h - s.hot_y) * s.sy;
        const int32_t x0 = std::max(0, int32_t(std::floor(std::min(ax, bx)))), x1 = std::min(W, int32_t(std::ceil(std::max(ax, bx))));
        const int32_t y0 = std::max(Y0, int32_t(std::floor(std::min(ay, by)))), y1 = std::min(Y1, int32_t(std::ceil(std::max(ay, by))));
        if (x0 >= x1 || y0 >= y1) return;
        struct Tap { int32_t q0, q1, w; bool in; }; // texels q0, q1 (clamped), weight of q1 in 1/256, inside the image
        auto taps = [&](int32_t n0, int32_t n1, double pos, double scale, double hot, double size) {
            std::vector<Tap> t(size_t(n1 - n0));
            for (int32_t i = n0; i < n1; i++) {
                const double u = (i + 0.5 - pos) / scale + hot;
                Tap& k = t[size_t(i - n0)];
                k.in = u >= 0 && u < size;
                const double f = s.smooth ? u - 0.5 : u, fl = std::floor(f);
                k.q0 = std::min(std::max(int32_t(fl), 0), int32_t(size) - 1);
                k.q1 = s.smooth ? std::min(std::max(int32_t(fl) + 1, 0), int32_t(size) - 1) : k.q0;
                k.w = s.smooth ? int32_t((f - fl) * 256) : 0;
            }
            return t;
        };
        const std::vector<Tap> tx = taps(x0, x1, s.x, s.sx, s.hot_x, w), ty = taps(y0, y1, s.y, s.sy, s.hot_y, h);
        const size_t stride = size_t(src.width) * 4, n = size_t(x1 - x0);
        if (!s.smooth) {
            for (int32_t y = y0; y < y1; ++y) {
                const Tap& ry = ty[size_t(y - y0)];
                if (!ry.in) continue;
                const uint8_t* r0 = src.rgba.data() + size_t(ry.q0) * stride;
                uint8_t* dr = out->rgba.data() + (size_t(y) * size_t(W) + size_t(x0)) * 4;
                for (size_t i = 0; i < n; i++, dr += 4)
                    if (tx[i].in) put(dr, r0 + size_t(tx[i].q0) * 4, ga, s.additive);
            }
            return;
        }
        // Bilinear, alpha-weighted (the canvas's smoothing of straight-alpha texels), in two passes: a source row
        // filtered across to premultiplied {c * a * 256 x3, a * 256} once, then two such rows mixed down per pixel.
        std::vector<uint32_t> rows[2] = {std::vector<uint32_t>(n * 4), std::vector<uint32_t>(n * 4)};
        int32_t have[2] = {-1, -1};
        auto across = [&](int32_t q, int32_t keep) -> const uint32_t* { // keep: the other row this pixel row needs
            for (int k = 0; k < 2; k++) if (have[k] == q) return rows[k].data();
            const int k = have[0] == keep ? 1 : 0;
            have[k] = q;
            const uint8_t* r = src.rgba.data() + size_t(q) * stride;
            uint32_t* o = rows[k].data();
            for (size_t i = 0; i < n; i++, o += 4) {
                const uint8_t *p0 = r + size_t(tx[i].q0) * 4, *p1 = r + size_t(tx[i].q1) * 4;
                const uint32_t a0 = p0[3] * uint32_t(256 - tx[i].w), a1 = p1[3] * uint32_t(tx[i].w);
                o[0] = p0[0] * a0 + p1[0] * a1; o[1] = p0[1] * a0 + p1[1] * a1; o[2] = p0[2] * a0 + p1[2] * a1;
                o[3] = a0 + a1;
            }
            return rows[k].data();
        };
        size_t i0 = 0, i1 = n; // the columns inside the image: one run, as u moves one way along x
        while (i0 < n && !tx[i0].in) i0++;
        while (i1 > i0 && !tx[i1 - 1].in) i1--;
        for (int32_t y = y0; y < y1; ++y) {
            const Tap& ry = ty[size_t(y - y0)];
            if (!ry.in) continue;
            const uint32_t* h0 = across(ry.q0, ry.q1);
            const uint32_t* h1 = across(ry.q1, ry.q0);
            const uint32_t v1 = uint32_t(ry.w), v0 = 256 - v1;
            uint8_t* dr = out->rgba.data() + (size_t(y) * size_t(W) + size_t(x0 + i0)) * 4;
            h0 += i0 * 4;
            h1 += i0 * 4;
            for (size_t i = i0; i < i1; i++, dr += 4, h0 += 4, h1 += 4) {
                const uint32_t A = (((h0[3] * v0 + h1[3] * v1) >> 8) * ga) >> 8; // alpha8 * ga, as put()'s A
                if (A == 0) continue;
                const uint32_t keep = 65025u - A;
                for (int c = 0; c < 3; c++) {
                    const uint32_t sc = (((h0[c] >> 8) * v0 + (h1[c] >> 8) * v1) >> 8) * ga; // colour * alpha8 * ga
                    if (s.additive) {
                        const uint32_t v = (dr[c] * 65025u + sc) / 65025u;
                        dr[c] = uint8_t(v > 255 ? 255 : v);
                    } else {
                        const uint32_t v = (sc + dr[c] * keep) / 65025u;
                        dr[c] = uint8_t(v > 255 ? 255 : v);
                    }
                }
                dr[3] = 255;
            }
        }
        return;
    }
    // translate(x, y) rotate(-angle) scale(sx, sy) translate(-Ga, -Ca): map each destination pixel back.
    const double r = s.angle * M_PI / 180, c = std::cos(r), sn = std::sin(r);
    const double w = src.width, h = src.height;
    // destination bounds of the transformed rectangle
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    for (int k = 0; k < 4; k++) {
        const double u = ((k & 1) ? w : 0) - s.hot_x, v = ((k & 2) ? h : 0) - s.hot_y;
        const double px = u * s.sx, py = v * s.sy;
        const double X = s.x + px * c + py * sn, Y = s.y - px * sn + py * c;
        minx = std::min(minx, X); maxx = std::max(maxx, X); miny = std::min(miny, Y); maxy = std::max(maxy, Y);
    }
    const int32_t x0 = std::max(0, int32_t(std::floor(minx))), x1 = std::min(W, int32_t(std::ceil(maxx)));
    const int32_t y0 = std::max(Y0, int32_t(std::floor(miny))), y1 = std::min(Y1, int32_t(std::ceil(maxy)));
    for (int32_t y = y0; y < y1; ++y)
        for (int32_t x = x0; x < x1; ++x) {
            const double X = x + 0.5 - s.x, Y = y + 0.5 - s.y;
            const double px = X * c - Y * sn, py = X * sn + Y * c; // undo rotate(-angle)
            const double u = px / s.sx + s.hot_x, v = py / s.sy + s.hot_y;
            if (u < 0 || v < 0 || u >= w || v >= h) continue;
            uint8_t px4[4];
            if (s.smooth) {
                const double fu = u - 0.5, fv = v - 0.5;
                const int32_t iu = int32_t(std::floor(fu)), iv = int32_t(std::floor(fv));
                const double au = fu - iu, av = fv - iv;
                double acc[4] = {0, 0, 0, 0};
                for (int dyy = 0; dyy < 2; dyy++)
                    for (int dxx = 0; dxx < 2; dxx++) {
                        const int32_t qx = std::min(std::max(iu + dxx, 0), int32_t(w) - 1);
                        const int32_t qy = std::min(std::max(iv + dyy, 0), int32_t(h) - 1);
                        const double wt = (dxx ? au : 1 - au) * (dyy ? av : 1 - av);
                        const uint8_t* p = src.rgba.data() + (size_t(qy) * src.width + size_t(qx)) * 4;
                        const double pa = p[3] * wt;
                        for (int k = 0; k < 3; k++) acc[k] += p[k] * pa;
                        acc[3] += pa;
                    }
                if (acc[3] <= 0) continue;
                for (int k = 0; k < 3; k++) px4[k] = uint8_t(std::min(255.0, acc[k] / acc[3]));
                px4[3] = uint8_t(std::min(255.0, acc[3]));
            } else {
                const uint8_t* p = src.rgba.data() + (size_t(v) * src.width + size_t(u)) * 4;
                std::copy(p, p + 4, px4);
            }
            put(out->rgba.data() + (size_t(y) * size_t(W) + size_t(x)) * 4, px4, ga, s.additive);
        }
}

double ink_alpha(int32_t xb, int32_t nb) { // Ie
    const int32_t d = xb & 4095;
    if (xb & 4096) return double((uint32_t(nb) >> 24) & 255) / 255;
    if (d == 1) return (128.0 - nb) / 128;
    return 1;
}

// aa.hb + aa.Bb (10150, 10201): type 1 lays the value's digit images out right-aligned on (x, y) (hotspot =
// total width, max height); type 4 draws frame floor((value - min) * count / (max - min)); types 2/3 fill a bar.
struct View { double scale = 1, ox = 0, oy = 0; }; // window = (frame - scroll + centring) * scale

bool draw_counter(const std::function<void(const Sprite&)>& emit, const game::Instance& a, const game::Defs& defs,
                  const ImageGetter& image, const View& v) {
    const game::ObjectDef* d = a.def;
    if (!d || !d->has_display) return false;
    if (d->disp_type == 1) {
        std::string txt = game::Value::number_string(a.counter); // n.Li (pu for float counters: same digits here)
        const int32_t w = d->disp_flags & 15;
        if (!a.counter_float && w) {
            if (int32_t(txt.size()) > w) txt = txt.substr(txt.size() - size_t(w));
            else while (int32_t(txt.size()) < w) txt = "0" + txt;
        }
        auto frame_of = [&](char ch) -> int32_t {
            int32_t i = -1;
            if (ch == '-') i = 10; else if (ch == '.' || ch == ',') i = 12; else if (ch == '+') i = 11;
            else if (ch == 'e' || ch == 'E') i = 13; else if (ch >= '0' && ch <= '9') i = ch - '0';
            return i >= 0 && i < int32_t(d->disp_frames.size()) ? d->disp_frames[size_t(i)] : -1;
        };
        double total = 0, height = 0;
        for (char ch : txt) if (const game::ImageInfo* im = defs.image(frame_of(ch))) { total += im->width; height = std::max<double>(height, im->height); }
        double bx = a.x + v.ox - total, by = a.y + v.oy - height;
        const double alpha = ink_alpha(a.sp.ink, a.sp.ink_param);
        for (char ch : txt) {
            const int32_t img = frame_of(ch);
            const game::ImageInfo* info = defs.image(img);
            const assets::Image* im = info ? image(img) : nullptr;
            if (!im) continue;
            Sprite s;
            s.img = im; s.handle = img; s.x = bx * v.scale; s.y = by * v.scale; s.hot_x = 0; s.hot_y = 0;
            s.alpha = alpha;
            emit(s);
            bx += info->width;
        }
        return true;
    }
    if (d->disp_type == 4 && !d->disp_frames.empty()) {
        const int32_t n = d->disp_frame_count;
        int32_t og = a.cmax <= a.cmin ? 0 : int32_t(std::floor((a.counter - a.cmin) * n / (a.cmax - a.cmin)));
        og = std::min(og, n - 1);
        const int32_t img = d->disp_frames[size_t(std::max(og, 0))];
        const game::ImageInfo* info = defs.image(img);
        const assets::Image* im = info ? image(img) : nullptr;
        if (!im) return false;
        Sprite s;
        s.img = im; s.handle = img; s.x = (a.x + v.ox) * v.scale; s.y = (a.y + v.oy) * v.scale;
        s.hot_x = info->hot_x * v.scale; s.hot_y = info->hot_y * v.scale;
        s.alpha = ink_alpha(a.sp.ink, a.sp.ink_param);
        emit(s);
        return true;
    }
    return false; // bars and font counters: not drawn yet
}

// One band's share of a text draw (layout_text), each glyph's coverage as the source alpha.
void draw_text(const Band& band, const Font& f, const std::string& text, double left, double top, double bw, double bh,
               int32_t flags, int32_t color, double alpha) {
    assets::Image* out = band.out;
    const uint8_t r = uint8_t(color >> 16), g = uint8_t(color >> 8), b = uint8_t(color);
    const uint32_t ga = alpha8(alpha);
    layout_text(f, text, left, top, bw, bh, flags, [&](const Glyph& gl, int32_t gx, int32_t gy) {
        for (int32_t yy = 0; yy < gl.h; yy++) {
            const int32_t py = gy + yy;
            if (py < band.y0 || py >= band.y1) continue;
            for (int32_t xx = 0; xx < gl.w; xx++) {
                const int32_t px = gx + xx;
                if (px < 0 || px >= int32_t(out->width)) continue;
                const uint8_t src[4] = {r, g, b, gl.alpha[size_t(yy) * gl.w + size_t(xx)]};
                put(out->rgba.data() + (size_t(py) * out->width + size_t(px)) * 4, src, ga, false);
            }
        }
    });
}

} // namespace

bool FontPack::open(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { if (error) *error = "cannot open " + path; return false; }
    file_.assign(std::istreambuf_iterator<char>(in), {});
    auto u16 = [&](size_t o) { return uint32_t(file_[o]) | uint32_t(file_[o + 1]) << 8; };
    auto u32 = [&](size_t o) { return u16(o) | u16(o + 2) << 16; };
    auto f32 = [&](size_t o) { const uint32_t v = u32(o); float f; std::memcpy(&f, &v, 4); return f; };
    if (file_.size() < 12 || std::memcmp(file_.data(), "UCNF", 4) != 0) { if (error) *error = "bad font pack"; return false; }
    const uint32_t n = u32(8);
    size_t o = 12;
    for (uint32_t i = 0; i < n && o + 16 <= file_.size(); i++) {
        Font f;
        const int32_t handle = int32_t(u32(o));
        f.size = f32(o + 4); f.line = f32(o + 8);
        const uint32_t ng = u32(o + 12);
        o += 16;
        for (uint32_t k = 0; k < ng && o + 14 <= file_.size(); k++) {
            const uint32_t c = u16(o);
            Glyph g;
            g.advance = f32(o + 2); g.x = int16_t(u16(o + 6)); g.y = int16_t(u16(o + 8));
            g.w = uint16_t(u16(o + 10)); g.h = uint16_t(u16(o + 12));
            o += 14;
            g.alpha = &file_[o];
            o += size_t(g.w) * g.h;
            if (c >= 32 && c < 127) f.glyphs[c - 32] = g;
        }
        fonts_.push_back({handle, f});
    }
    return true;
}

const Font* FontPack::font(int32_t handle) const {
    for (const auto& f : fonts_) if (f.first == handle) return &f.second;
    return nullptr;
}

void layout_text(const Font& f, const std::string& text, double left, double top, double bw, double bh, int32_t flags,
                 const std::function<void(const Glyph&, int32_t, int32_t)>& glyph) {
    auto width_of = [&](const std::string& s) {
        double w = 0;
        for (unsigned char c : s) if (c >= 32 && c < 127) w += f.glyphs[c - 32].advance;
        return w;
    };
    std::vector<std::string> lines;
    size_t p = 0;
    while (p <= text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string para = text.substr(p, e - p);
        para.erase(std::remove(para.begin(), para.end(), '\r'), para.end());
        std::string line;
        size_t q = 0;
        while (q <= para.size()) {
            size_t sp = para.find(' ', q);
            if (sp == std::string::npos) sp = para.size();
            const std::string word = para.substr(q, sp - q);
            const std::string cand = line.empty() ? word : line + " " + word;
            if (!line.empty() && width_of(cand) > bw) { lines.push_back(line); line = word; }
            else line = cand;
            q = sp + 1;
        }
        lines.push_back(line);
        p = e + 1;
    }
    const double m = lines.size() * f.line;
    double y = top + ((flags & 8) ? bh - m : (flags & 4) ? bh / 2 - m / 2 : 0);
    for (const std::string& line : lines) {
        const double lw = width_of(line);
        double x = left + ((flags & 1) ? bw / 2 - lw / 2 : (flags & 2) ? bw - lw : 0);
        for (unsigned char c : line) {
            if (c < 32 || c >= 127) continue;
            const Glyph& gl = f.glyphs[c - 32];
            glyph(gl, int32_t(std::floor(x)) + gl.x, int32_t(std::floor(y)) + gl.y);
            x += gl.advance;
        }
        y += f.line;
    }
}

bool collect_world(const game::Run& run, const game::Defs& defs, const data::Bundle& bundle, const ImageGetter& image,
                   double scale, std::vector<Draw>* out, uint32_t* background, WorldStats* stats, std::string* error,
                   const FontPack* fonts) {
    WorldStats st;
    const int32_t fi = run.frame();
    // FRAMES record: name, width, height, layers..., background (mode, image, x, y, color), instances
    data::Reader r{bundle.bytes(), bundle.record_offset(data::kFrames, uint32_t(fi))};
    r.u32();
    const int32_t width = r.i32(), height = r.i32();
    const uint32_t layer_count = r.u32();
    r.skip(size_t(layer_count) * 16);
    r.u32(); r.u32(); r.i32(); r.i32();
    const uint32_t bg = r.u32();
    if (!r.ok || width <= 0 || height <= 0) { if (error) *error = "bad frame record"; return false; }
    *background = bg;
    const game::FrameDef* fd = run.frame_def();
    // The canvas is the application window; the frame's display area sits centred in it (tK: Vf, Wf).
    const double vf = 1920 / 2.0 - fd->display_w / 2.0, wf = 1080 / 2.0 - fd->display_h / 2.0;
    std::vector<Draw>& cmds = *out;
    cmds.clear();
    const auto sprite = [&](const Sprite& s) {
        Draw d;
        d.s = s;
        cmds.push_back(std::move(d));
    };
    const auto text = [&](const Font* f, std::string txt, double x, double y, double w, double h, int32_t flags,
                          int32_t color, double alpha) {
        Draw d;
        d.kind = Draw::kText;
        d.font = f; d.text = std::move(txt); d.x = x; d.y = y; d.w = w; d.h = h; d.flags = flags; d.color = color;
        d.alpha = alpha;
        cmds.push_back(std::move(d));
    };

    // Backdrop images per object (OBJECTS record field after blend).
    auto backdrop_image = [&](int32_t oi) -> int32_t {
        data::Reader o{bundle.bytes(), bundle.record_offset(data::kObjects, uint32_t(oi))};
        o.u32(); o.u8(); o.u8(); o.u32(); o.u8(); o.u8(); o.u8();
        return int32_t(o.u32());
    };
    const auto& slots = run.slots();
    const View view{scale, vf - run.scroll_x(), wf - run.scroll_y()};
    for (size_t L = 0; L < run.display_main().size(); L++) {
        // gc: the layer's backdrops, in frame instance order
        for (const auto& in : fd->instances) {
            if (in.layer != int32_t(L)) continue;
            const game::ObjectDef* d = defs.object(in.object);
            if (!d || d->type >= 2) continue;
            const int32_t img = backdrop_image(in.object);
            if (img <= 0) continue;
            const assets::Image* im = image(img);
            const game::ImageInfo* info = defs.image(img);
            if (!im || !info) { st.missing_image++; continue; }
            // gc sits at the layer's scroll (ea * coefficient), not the frame's
            const double cx = L < fd->layers.size() ? fd->layers[L].x_coeff : 1, cy = L < fd->layers.size() ? fd->layers[L].y_coeff : 1;
            Sprite s;
            s.img = im; s.handle = img;
            s.x = (in.x + vf - run.scroll_x() * cx) * scale; s.y = (in.y + wf - run.scroll_y() * cy) * scale;
            s.hot_x = info->hot_x * scale; s.hot_y = info->hot_y * scale;
            sprite(s);
            st.drawn++;
        }
        for (const auto* list : {&run.display_back()[L], &run.display_main()[L]})
            for (int32_t slot : *list) {
                const game::Instance* a = slots[size_t(slot)];
                if (!a) continue;
                if (!a->visible_list) { st.hidden++; continue; }
                if (a->type == 3) {
                    const game::ObjectDef* d = a->def;
                    const Font* f = fonts && d && !d->texts.empty() ? fonts->font(d->texts[0].font) : nullptr;
                    if (!f) { st.texts_skipped++; continue; }
                    text(f, a->has_text ? a->text : std::string(), (a->x + view.ox) * scale, (a->y + view.oy) * scale,
                         d->text_w * scale, d->text_h * scale, d->texts[0].flags & 47, // n.gp|Hj|Ss|hp|fp|li|cE
                         a->color1, ink_alpha(a->sp.ink, a->sp.ink_param));
                    st.drawn++;
                    continue;
                }
                if (a->type == 7 && a->def && a->def->has_display && a->def->disp_type == 5) { // aa.hb type 5
                    const Font* f = fonts ? fonts->font(a->def->disp_font) : nullptr;
                    if (!f) { st.counters_skipped++; continue; }
                    std::string txt = game::Value::number_string(a->counter);
                    double w = 0;
                    for (unsigned char c : txt) if (c >= 32 && c < 127) w += f->glyphs[c - 32].advance;
                    const double top = (a->y + view.oy) * scale - (a->def->disp_h * scale / 2 + f->line / 2);
                    text(f, std::move(txt), (a->x + view.ox) * scale - w, top, w + 1, f->line, 0, a->def->disp_color1,
                         ink_alpha(a->sp.ink, a->sp.ink_param));
                    st.drawn++;
                    continue;
                }
                if (a->type == 7 && a->def && a->def->has_display && (a->def->disp_type == 2 || a->def->disp_type == 3)) {
                    // aa.Bb bars: fillRect (Ui 1) or a linear gradient (Ui 2, colour 1 to 2, swapped by flag 256;
                    // across, or down when jo) over the box hb sized from the value
                    const game::ObjectDef* d = a->def;
                    Draw b;
                    b.kind = Draw::kBar;
                    b.x = (a->x - a->hx + view.ox) * scale; b.y = (a->y - a->hy + view.oy) * scale;
                    b.w = a->w * scale; b.h = a->h * scale;
                    b.color = a->color1; b.color2 = d->disp_fill == 2 ? a->color2 : a->color1;
                    if (d->disp_fill == 2 && (d->disp_flags & 256)) std::swap(b.color, b.color2);
                    b.flags = d->disp_vertical != 0;
                    b.alpha = ink_alpha(a->sp.ink, a->sp.ink_param);
                    if (b.w <= 0 || b.h <= 0 || d->disp_fill < 1 || d->disp_fill > 2) { st.counters_skipped++; continue; }
                    cmds.push_back(std::move(b));
                    st.drawn++;
                    continue;
                }
                if (a->type == 7) {
                    if (draw_counter(sprite, *a, defs, image, view)) st.drawn++; else st.counters_skipped++;
                    continue;
                }
                if (!a->has_pos || a->b.image < 0) continue;
                const assets::Image* im = image(a->b.image);
                const game::ImageInfo* info = defs.image(a->b.image);
                if (!im || !info) { st.missing_image++; continue; }
                Sprite s;
                s.img = im; s.handle = a->b.image; s.x = (a->x + view.ox) * scale; s.y = (a->y + view.oy) * scale;
                s.hot_x = info->hot_x * scale; s.hot_y = info->hot_y * scale;
                s.angle = a->b.angle; s.sx = a->b.sx; s.sy = a->b.sy;
                s.alpha = ink_alpha(a->sp.ink, a->sp.ink_param);
                s.additive = (a->sp.ink & 4095) == 9;
                s.smooth = (a->sp.flags & 16) != 0;
                sprite(s);
                st.drawn++;
            }
    }
    // The port's credit line: it is not part of the original game's data, so it is drawn here instead of being
    // fabricated into the bundle. On the menu (frame 1) it follows the version string in the bottom-left corner.
    if (fi == 1 && fonts) {
        if (const Font* f = fonts->font(4)) {
            const std::string version = "1.031", credit = "Developed by mnmuxk";
            const auto width_of = [&](const std::string& s) {
                double w = 0;
                for (unsigned char c : s) if (c >= 32 && c < 127) w += f->glyphs[c - 32].advance;
                return w;
            };
            // The version instance (object 125) sits at (7, 1053) on the 1920x1080 frame; the credit trails it.
            text(f, credit, (7.0 + view.ox) * scale + width_of(version) + 16.0, (1053.0 + view.oy) * scale, 400.0, f->line,
                 0, 0xFFFFFF, 1.0);
        }
    }
    if (stats) *stats = st;
    return true;
}

bool render_world(const game::Run& run, const game::Defs& defs, const data::Bundle& bundle, const ImageGetter& image,
                  double scale, assets::Image* out, WorldStats* stats, std::string* error, const FontPack* fonts,
                  const Parallel* parallel) {
    // Everything to draw, in order, found here on the calling thread (image lookups decode and evict, so they stay on
    // one thread); the bands then replay it.
    std::vector<Draw> cmds;
    uint32_t bg = 0;
    if (!collect_world(run, defs, bundle, image, scale, &cmds, &bg, stats, error, fonts)) return false;
    out->width = uint32_t(std::lround(1920 * scale));
    out->height = uint32_t(std::lround(1080 * scale));
    out->rgba.resize(size_t(out->width) * out->height * 4);
    size_t first = 0; // commands before an opaque sprite over the whole output can't show: skipped
    const double W = out->width, H = out->height;
    for (size_t i = 0; i < cmds.size(); i++) {
        if (cmds[i].kind != Draw::kSprite) continue;
        const Sprite& s = cmds[i].s;
        const assets::Image& im = *s.img;
        const double ax = s.x - s.hot_x * s.sx, bx = s.x + (im.width - s.hot_x) * s.sx;
        const double ay = s.y - s.hot_y * s.sy, by = s.y + (im.height - s.hot_y) * s.sy;
        if (s.alpha >= 1 && !s.additive && s.angle == 0 && std::min(ax, bx) <= 0 && std::max(ax, bx) >= W &&
            std::min(ay, by) <= 0 && std::max(ay, by) >= H) {
            if (im.opaque < 0) {
                im.opaque = 1;
                for (size_t k = 3; k < im.rgba.size(); k += 4) if (im.rgba[k] != 255) { im.opaque = 0; break; }
            }
            if (im.opaque) first = i;
        }
    }
    const auto bar = [](const Band& b, const Draw& d) {
        const double x0 = d.x, y0 = d.y, bw = d.w, bh = d.h;
        const int32_t c1 = d.color, c2 = d.color2;
        const uint32_t ga = alpha8(d.alpha);
        const int32_t X0 = std::max(0, int32_t(std::lround(x0))), X1 = std::min(int32_t(b.out->width), int32_t(std::lround(x0 + bw)));
        const int32_t Y0 = std::max(b.y0, int32_t(std::lround(y0))), Y1 = std::min(b.y1, int32_t(std::lround(y0 + bh)));
        for (int32_t y = Y0; y < Y1; y++)
            for (int32_t x = X0; x < X1; x++) {
                const double t = d.flags ? (y + 0.5 - y0) / bh : (x + 0.5 - x0) / bw;
                uint8_t px[4] = {0, 0, 0, 255};
                for (int k = 0; k < 3; k++) {
                    const int sh = 16 - 8 * k;
                    px[k] = uint8_t(std::lround(((c1 >> sh) & 255) * (1 - t) + ((c2 >> sh) & 255) * t));
                }
                put(b.out->rgba.data() + (size_t(y) * b.out->width + size_t(x)) * 4, px, ga, false);
            }
    };
    const std::function<void(int)> band = [&](int i) {
        const int32_t rows = int32_t(out->height);
        const Band b{out, rows * i / kBands, rows * (i + 1) / kBands, i};
        uint8_t* p = out->rgba.data() + size_t(b.y0) * out->width * 4;
        for (uint8_t* e = out->rgba.data() + size_t(b.y1) * out->width * 4; p != e; p += 4) {
            p[0] = uint8_t(bg >> 16); p[1] = uint8_t(bg >> 8); p[2] = uint8_t(bg); p[3] = 255;
        }
        for (size_t c = first; c < cmds.size(); c++) {
            const Draw& d = cmds[c];
            if (d.kind == Draw::kSprite) draw(b, d.s);
            else if (d.kind == Draw::kText) draw_text(b, *d.font, d.text, d.x, d.y, d.w, d.h, d.flags, d.color, d.alpha);
            else bar(b, d);
        }
    };
    if (parallel) (*parallel)(kBands, band);
    else for (int i = 0; i < kBands; i++) band(i);
    return true;
}

} // namespace ucn::render
