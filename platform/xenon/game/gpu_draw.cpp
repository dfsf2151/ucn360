#include "gpu_draw.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#include "assets/dxt.h"

extern "C" {
#include "xsdk/log.h"
#include "xsdk/present.h"
#include "xsdk/xsc.h"
}

using ucn::assets::Image;
using ucn::render::Draw;
using ucn::render::Font;
using ucn::render::Glyph;

// The sprite shaders are xost's own test inputs that do exactly this (xenos-toolchain/xsc/testdata): a vertex shader
// taking a clip-space position, texture coordinates and a colour, and a pixel shader giving the texel times the colour.
// Their SPIR-V goes in as it is and xsc compiles it on the console at start, as vulkan4xenon does.
asm(".section .rodata\n.balign 4\n"
    "ucn_vs_spv: .incbin \"vk_three_inputs.vert.spv\"\nucn_vs_spv_end:\n"
    ".balign 4\nucn_ps_spv: .incbin \"modulate.frag.spv\"\nucn_ps_spv_end:\n.previous");
extern "C" const uint32_t ucn_vs_spv[], ucn_vs_spv_end[], ucn_ps_spv[], ucn_ps_spv_end[];

namespace {

constexpr float kW = 1280, kH = 720;

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint32_t le16(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8; }

struct Compiled { XsdkShader shader{}; std::vector<std::pair<uint32_t, std::vector<float>>> literals; uint32_t sampler = 0; };

// Kept for good: the loaded shader's fetch patches point into its container.
bool compile(const uint32_t* spirv, const uint32_t* end, Compiled* out, std::string* error) {
    XsdkXscOptions options;
    xsdk_xsc_default_options(&options);
    options.optimize = true;
    auto* xs = new XsdkXscShader;
    if (xsdk_xsc_compile(spirv, size_t(end - spirv), &options, xs) != XSDK_XSC_OK) {
        *error = std::string("xsc: ") + xs->message;
        return false;
    }
    if (!NT_SUCCESS(xsdk_shader_load(&out->shader, reinterpret_cast<const uint32_t*>(xs->container), xs->container_bytes))) {
        *error = "shader load";
        return false;
    }
    for (size_t i = 0; i < xs->need_count; i++)
        if (xs->needs[i].kind == XSDK_XSC_LITERAL) {
            const double* v = xs->needs[i].value;
            out->literals.push_back({xs->needs[i].reg, {float(v[0]), float(v[1]), float(v[2]), float(v[3])}});
        }
    for (size_t i = 0; i < xs->resource_count; i++)
        if (xs->resources[i].kind == XSDK_XSC_SAMPLER) out->sampler = xs->resources[i].reg;
    return true;
}

Compiled g_vs, g_ps;

} // namespace

bool GpuDraw::place(Tex* t, std::string* error) {
    const uint32_t size = (t->t.size_bytes + 4095) & ~4095u;
    if (!chunk_ || chunk_used_ + size > chunk_size_) {
        chunk_size_ = std::max<uint32_t>(16u << 20, size);
        chunk_ = xsdk_gpu_alloc(chunk_size_);
        chunk_used_ = 0;
        allocated_ += chunk_size_;
        if (!chunk_) { *error = "out of GPU memory"; return false; }
    }
    xsdk_texture_attach(&t->t, chunk_ + chunk_used_ / 4);
    chunk_used_ += size;
    return true;
}

bool GpuDraw::open(const char* pak, std::string* error) {
    pak_ = pak;
    std::ifstream f(pak, std::ios::binary);
    uint8_t head[20];
    if (!f.read(reinterpret_cast<char*>(head), 20) || std::memcmp(head, "UCNI", 4) || le32(head + 4) != 1) {
        *error = std::string("bad image pack ") + pak;
        return false;
    }
    std::vector<uint8_t> table(size_t(le32(head + 8)) * 24);
    f.read(reinterpret_cast<char*>(table.data()), std::streamsize(table.size()));
    struct Entry { int32_t handle; uint32_t tw, th, uw, uh, offset, size; bool dxt5; };
    std::vector<Entry> entries;
    for (size_t o = 0; o + 24 <= table.size(); o += 24) {
        const uint8_t* p = &table[o];
        entries.push_back({int32_t(le32(p)), le16(p + 4), le16(p + 6), le16(p + 8), le16(p + 10), le32(p + 16), le32(p + 20), p[12] == 1});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.offset < b.offset; });
    std::vector<uint8_t> window; // the pack read in 8 MB pieces: a read per image took 5 ms each in Xenia
    uint32_t window_at = 0;
    for (const Entry& e : entries) {
        if (e.uw == 0) continue;
        Tex t;
        if (!NT_SUCCESS(xsdk_texture_init(&t.t, e.dxt5 ? XSDK_FORMAT_DXT5 : XSDK_FORMAT_DXT1, e.tw, e.th, 1, true))) {
            *error = "texture " + std::to_string(e.handle);
            return false;
        }
        if (!place(&t, error)) return false;
        if (e.offset < window_at || e.offset + e.size > window_at + window.size()) {
            window_at = e.offset;
            window.resize(std::max<uint32_t>(8u << 20, e.size));
            f.clear();
            f.seekg(e.offset);
            f.read(reinterpret_cast<char*>(window.data()), std::streamsize(window.size()));
            window.resize(size_t(f.gcount()));
            if (e.size > window.size()) { *error = "short image pack"; return false; }
        }
        uint8_t* blocks = &window[e.offset - window_at];
        // The pack's blocks are .dds's little-endian 16-bit words; the upload takes them as the CPU's (big-endian) ones
        for (uint32_t i = 0; i + 1 < e.size; i += 2) std::swap(blocks[i], blocks[i + 1]);
        xsdk_texture_upload(&t.t, 0, blocks, e.tw / 4 * (e.dxt5 ? 16 : 8));
        xsdk_gpu_invalidate(t.t.address, t.t.size_bytes);
        t.size.width = e.uw;
        t.size.height = e.uh;
        t.u1 = float(e.uw) / float(e.tw);
        t.v1 = float(e.uh) / float(e.th);
        t.offset = e.offset;
        t.bytes = e.size;
        t.dxt5 = e.dxt5;
        tex_[e.handle] = t;
    }

    if (!compile(ucn_vs_spv, ucn_vs_spv_end, &g_vs, error) || !compile(ucn_ps_spv, ucn_ps_spv_end, &g_ps, error)) return false;
    // vk_three_inputs.vert's inputs by location, as xsc names them: 0 POSITION, 1 COLOR, 2 TEXCOORD (docs/xsc.md)
    XsdkVertexElement el[3] = {};
    el[0].usage = XSDK_USAGE_POSITION; el[0].format = XSDK_VERTEX_FLOAT2; el[0].offset_bytes = 0;
    el[1].usage = XSDK_USAGE_COLOR; el[1].format = XSDK_VERTEX_FLOAT2; el[1].offset_bytes = 8;     // a_uv
    el[2].usage = XSDK_USAGE_TEXCOORD; el[2].format = XSDK_VERTEX_FLOAT4; el[2].offset_bytes = 16; // a_color
    const uint32_t stride = sizeof(Vertex);
    if (!NT_SUCCESS(xsdk_shader_set_vertex_layout(&g_vs.shader, el, 3, &stride))) { *error = "vertex layout"; return false; }

    XsdkSampler s = {};
    s.mag_filter = s.min_filter = s.mip_filter = XSDK_FILTER_POINT;
    s.address_u = s.address_v = s.address_w = XSDK_ADDRESS_CLAMP;
    xsdk_texture_sampler_state(&point_, &s);
    s.mag_filter = s.min_filter = XSDK_FILTER_LINEAR;
    xsdk_texture_sampler_state(&linear_, &s);
    XsdkBlend b = {};
    b.color_source = XSDK_BLEND_SRC_ALPHA; b.color_dest = XSDK_BLEND_ONE_MINUS_SRC_ALPHA; b.color_op = XSDK_BLEND_OP_ADD;
    b.alpha_source = XSDK_BLEND_ONE; b.alpha_dest = XSDK_BLEND_ONE_MINUS_SRC_ALPHA; b.alpha_op = XSDK_BLEND_OP_ADD;
    b.write_mask = XSDK_WRITE_ALL;
    xsdk_gpu_blend_state(&normal_, &b);
    b.color_dest = XSDK_BLEND_ONE; // the canvas's "lighter"
    xsdk_gpu_blend_state(&add_, &b);

    Image white;
    white.width = white.height = 4;
    white.rgba.assign(64, 255);
    white_ = tex_[add(white)];
    for (auto& v : vertices_)
        if (!(v = xsdk_gpu_alloc(kMaxVertices * sizeof(Vertex)))) { *error = "out of GPU memory"; return false; }
    xsdk_log_write("gpu: %u images in %u MB of textures", unsigned(tex_.size()), unsigned(allocated_ >> 20));
    return true;
}

const Image* GpuDraw::image(int32_t handle) const {
    auto it = tex_.find(handle);
    return it == tex_.end() ? nullptr : &it->second.size;
}

bool GpuDraw::decode(int32_t handle, Image* out) const {
    auto it = tex_.find(handle);
    if (it == tex_.end() || it->second.bytes == 0) return false;
    const Tex& t = it->second;
    std::vector<uint8_t> blocks(t.bytes), full(size_t(t.t.width) * t.t.height * 4);
    std::ifstream f(pak_, std::ios::binary);
    f.seekg(t.offset);
    if (!f.read(reinterpret_cast<char*>(blocks.data()), std::streamsize(blocks.size()))) return false;
    ucn::assets::dxt_decode(blocks.data(), t.t.width, t.t.height, t.dxt5, full.data());
    out->width = t.size.width;
    out->height = t.size.height;
    out->rgba.resize(size_t(out->width) * out->height * 4);
    for (uint32_t y = 0; y < out->height; y++)
        std::memcpy(&out->rgba[size_t(y) * out->width * 4], &full[size_t(y) * t.t.width * 4], size_t(out->width) * 4);
    return true;
}

int32_t GpuDraw::add(const Image& img) {
    Tex t;
    std::string error;
    if (!NT_SUCCESS(xsdk_texture_init(&t.t, XSDK_FORMAT_R8G8B8A8, img.width, img.height, 1, true)) || !place(&t, &error)) {
        xsdk_log_write("gpu: no texture for a %ux%u overlay", unsigned(img.width), unsigned(img.height));
        return 0;
    }
    xsdk_texture_upload(&t.t, 0, img.rgba.data(), img.width * 4);
    xsdk_gpu_invalidate(t.t.address, t.t.size_bytes);
    t.size.width = img.width;
    t.size.height = img.height;
    tex_[next_extra_] = t;
    return next_extra_--;
}

// A font's glyphs side by side in rows of an 8-bit texture read as white with the glyph's coverage as alpha.
const GpuDraw::Tex& GpuDraw::atlas(const Font* font) {
    auto it = atlases_.find(font);
    if (it != atlases_.end()) return it->second.tex;
    Atlas& a = atlases_[font];
    constexpr uint32_t kAtlasW = 512;
    uint32_t x = 0, y = 0, row = 0;
    std::vector<uint32_t> at(95 * 2);
    for (int i = 0; i < 95; i++) {
        const Glyph& g = font->glyphs[i];
        if (x + g.w + 1 > kAtlasW) { x = 0; y += row + 1; row = 0; }
        at[size_t(i) * 2] = x; at[size_t(i) * 2 + 1] = y;
        x += g.w + 1u;
        row = std::max<uint32_t>(row, g.h);
    }
    const uint32_t h = std::max<uint32_t>(4, (y + row + 4) & ~3u);
    std::vector<uint8_t> px(size_t(kAtlasW) * h, 0);
    a.uv.resize(95 * 4);
    for (int i = 0; i < 95; i++) {
        const Glyph& g = font->glyphs[i];
        const uint32_t gx = at[size_t(i) * 2], gy = at[size_t(i) * 2 + 1];
        for (uint32_t r = 0; r < g.h && g.alpha; r++) std::memcpy(&px[size_t(gy + r) * kAtlasW + gx], g.alpha + size_t(r) * g.w, g.w);
        float* uv = &a.uv[size_t(i) * 4];
        uv[0] = float(gx) / kAtlasW; uv[1] = float(gy) / float(h);
        uv[2] = float(gx + g.w) / kAtlasW; uv[3] = float(gy + g.h) / float(h);
    }
    std::string error;
    if (NT_SUCCESS(xsdk_texture_init(&a.tex.t, XSDK_FORMAT_R8, kAtlasW, h, 1, true)) && place(&a.tex, &error)) {
        const uint32_t ones[4] = {XSDK_CHANNEL_ONE, XSDK_CHANNEL_ONE, XSDK_CHANNEL_ONE, XSDK_CHANNEL_RED};
        xsdk_texture_swizzle(&a.tex.t, ones);
        xsdk_texture_upload(&a.tex.t, 0, px.data(), kAtlasW);
        xsdk_gpu_invalidate(a.tex.t.address, a.tex.t.size_bytes);
    } else {
        a.tex = white_;
    }
    return a.tex;
}

// Two triangles, corners top-left, top-right, bottom-right, bottom-left in window pixels.
void GpuDraw::quad(const Tex& tex, bool linear, bool additive, const float xy[4][2], float u0, float v0, float u1,
                   float v1, const float rgba[4][4]) {
    if (verts_.size() + 6 > kMaxVertices) return;
    const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    if (batches_.empty() || batches_.back().tex != &tex || batches_.back().linear != linear ||
        batches_.back().additive != additive)
        batches_.push_back({&tex, linear, additive, uint32_t(verts_.size()), 0});
    for (int k : {0, 1, 2, 0, 2, 3})
        verts_.push_back({xy[k][0] / (kW / 2) - 1, 1 - xy[k][1] / (kH / 2), uv[k][0], uv[k][1], rgba[k][0], rgba[k][1],
                          rgba[k][2], rgba[k][3]});
    batches_.back().count += 6;
}

void GpuDraw::present(const std::vector<Draw>& draws, uint32_t background) {
    verts_.clear();
    batches_.clear();
    const auto rgb = [](int32_t c, double a, float* out) {
        out[0] = float((c >> 16) & 255) / 255; out[1] = float((c >> 8) & 255) / 255; out[2] = float(c & 255) / 255;
        out[3] = float(std::min(1.0, std::max(0.0, a)));
    };
    const auto rect = [&](double x0, double y0, double x1, double y1, int32_t c1, int32_t c2, bool down, double alpha) {
        const float X0 = float(std::lround(x0)), X1 = float(std::lround(x1)), Y0 = float(std::lround(y0)), Y1 = float(std::lround(y1));
        const float xy[4][2] = {{X0, Y0}, {X1, Y0}, {X1, Y1}, {X0, Y1}};
        float c[4][4];
        rgb(c1, alpha, c[0]);
        rgb(down ? c1 : c2, alpha, c[1]);
        rgb(c2, alpha, c[2]);
        rgb(down ? c2 : c1, alpha, c[3]);
        quad(white_, false, false, xy, 0, 0, 1, 1, c);
    };
    rect(0, 0, kW, kH, int32_t(background), int32_t(background), false, 1);
    for (const Draw& d : draws) {
        if (d.kind == Draw::kBar) {
            rect(d.x, d.y, d.x + d.w, d.y + d.h, d.color, d.color2, d.flags != 0, d.alpha);
            continue;
        }
        if (d.kind == Draw::kText) {
            const Tex& t = atlas(d.font);
            const auto& uv = atlases_[d.font].uv;
            float c[4][4];
            for (auto& k : c) rgb(d.color, std::floor(std::min(1.0, std::max(0.0, d.alpha)) * 255 + 0.5) / 255, k);
            ucn::render::layout_text(*d.font, d.text, d.x, d.y, d.w, d.h, d.flags, [&](const Glyph& g, int32_t gx, int32_t gy) {
                if (!g.w || !g.h) return;
                const float* q = &uv[size_t(&g - d.font->glyphs) * 4];
                const float X0 = float(gx), Y0 = float(gy), X1 = X0 + g.w, Y1 = Y0 + g.h;
                const float xy[4][2] = {{X0, Y0}, {X1, Y0}, {X1, Y1}, {X0, Y1}};
                quad(t, false, false, xy, q[0], q[1], q[2], q[3], c);
            });
            continue;
        }
        const ucn::render::Sprite& s = d.s;
        if (s.alpha <= 0) continue; // globalAlpha 0 draws nothing
        auto it = tex_.find(s.handle);
        if (it == tex_.end()) continue;
        const Tex& t = it->second;
        const float w = float(t.size.width), h = float(t.size.height);
        float c[4][4];
        for (auto& k : c) { k[0] = k[1] = k[2] = 1; k[3] = float(std::min(1.0, s.alpha)); }
        if (s.angle == 0 && s.sx == 1 && s.sy == 1) { // whole pixels, as the CPU's exact path
            const float X0 = float(std::floor(s.x - s.hot_x)), Y0 = float(std::floor(s.y - s.hot_y));
            const float xy[4][2] = {{X0, Y0}, {X0 + w, Y0}, {X0 + w, Y0 + h}, {X0, Y0 + h}};
            quad(t, false, s.additive, xy, 0, 0, t.u1, t.v1, c);
            continue;
        }
        // translate(x, y) rotate(-angle) scale(sx, sy) translate(-hot): each corner of the image
        const double r = s.angle * M_PI / 180, cs = std::cos(r), sn = std::sin(r);
        float xy[4][2];
        for (int k = 0; k < 4; k++) {
            const double px = ((k == 1 || k == 2) ? w : 0) - s.hot_x, py = (k >= 2 ? h : 0) - s.hot_y;
            const double qx = px * s.sx, qy = py * s.sy;
            xy[k][0] = float(s.x + qx * cs + qy * sn);
            xy[k][1] = float(s.y - qx * sn + qy * cs);
        }
        quad(t, s.smooth, s.additive, xy, 0, 0, t.u1, t.v1, c);
    }

    const uint32_t k = frame_++ % kBuffers;
    if (fences_[k] && !NT_SUCCESS(xsdk_gpu_wait(fences_[k], 1000))) xsdk_log_write("gpu: a frame's draws never finished");
    volatile uint32_t* dst = vertices_[k];
    const uint32_t words = uint32_t(verts_.size() * sizeof(Vertex) / 4);
    const uint32_t* src = reinterpret_cast<const uint32_t*>(verts_.data());
    for (uint32_t i = 0; i < words; i++) dst[i] = src[i];
    __asm__ volatile("sync" ::: "memory");
    const uint32_t base = xsdk_gpu_address(dst);
    xsdk_gpu_invalidate(base, words * 4);

    uint32_t address = 0;
    if (!NT_SUCCESS(xsdk_present_begin(&address, 1000))) return;
    xsdk_gpu_set_target(uint32_t(kW), uint32_t(kH));
    xsdk_gpu_set_viewport(-0.5f, -0.5f, kW, kH, 0, 1); // pixel centres on halves, as the canvas has them
    xsdk_gpu_set_shaders(&g_vs.shader, &g_ps.shader);
    for (const auto& l : g_vs.literals) xsdk_gpu_set_vertex_constants(l.first, l.second.data(), 1);
    int blend = -1;
    for (const Batch& b : batches_) {
        if (blend != int(b.additive)) { blend = int(b.additive); xsdk_gpu_set_blend(b.additive ? &add_ : &normal_); }
        xsdk_gpu_set_texture(g_ps.sampler, &b.tex->t, b.linear ? &linear_ : &point_);
        xsdk_gpu_set_vertices(0, base + b.first * uint32_t(sizeof(Vertex)), b.count * uint32_t(sizeof(Vertex)));
        xsdk_gpu_draw(XSDK_PRIMITIVE_TRIANGLE_LIST, b.count);
    }
    xsdk_gpu_resolve(address, uint32_t(kW), uint32_t(kH), 0xFF000000);
    fences_[k] = xsdk_gpu_fence();
    last_address_ = address;
    xsdk_present_end();
}

void GpuDraw::read_back(std::vector<uint8_t>* rgba) {
    for (uint32_t f : fences_) if (f) xsdk_gpu_wait(f, 1000);
    volatile uint32_t* px = xsdk_present_pixels(last_address_);
    rgba->resize(size_t(kW) * size_t(kH) * 4);
    for (uint32_t y = 0; y < uint32_t(kH); y++)
        for (uint32_t x = 0; x < uint32_t(kW); x++) {
            const uint32_t v = px[tiled_index(x, y, uint32_t(kW))]; // B, G, R, A from the top byte
            uint8_t* d = &(*rgba)[(size_t(y) * size_t(kW) + x) * 4];
            d[0] = uint8_t(v >> 8); d[1] = uint8_t(v >> 16); d[2] = uint8_t(v >> 24); d[3] = 255;
        }
}
