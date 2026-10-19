#include "game/mask.h"

#include <algorithm>
#include <cmath>

namespace ucn::game {

void Mask::build(const uint8_t* rgba, int32_t w, int32_t h, int32_t hx, int32_t hy) { // iu
    width = w; height = h; hot_x = hx; hot_y = hy;
    const int32_t f = int32_t(std::floor(double((width + 15) & int32_t(0xFFFFFFF0)) / 16));
    line_width = f;
    bits.assign(size_t(f * height + 1), 0);
    for (int32_t c = 0; c < height; c++) {
        size_t e = size_t(c) * size_t(w) * 4 + 3;
        int32_t g = c * f;
        uint32_t m = 32768;
        for (int32_t d = 0; d < width; d++) {
            if (rgba[e] != 0) bits[size_t(g)] |= uint16_t(m);
            e += 4;
            m >>= 1;
            if (m == 0) { m = 32768; g++; }
        }
    }
}

bool Mask::transform(const Mask& a, double b, double c, double d) { // UG
    const double g0 = a.width, e0 = a.height;
    // m: the transformed size, h2: the transformed hotspot (TJ)
    double right = std::floor(a.width * c), bottom = std::floor(a.height * d);
    double hx = std::floor(a.hot_x * c), hy = std::floor(a.hot_y * d);
    {
        double cc, f;
        if (b == 90) { cc = 0; f = 1; }
        else if (b == 180) { cc = -1; f = 0; }
        else if (b == 270) { cc = 0; f = -1; }
        else { f = b * M_PI / 180; cc = std::cos(f); f = std::sin(f); }
        const double g = -hx * cc, dd0 = -hx * f, e = -hy * cc, m = -hy * f;
        const double ugx = std::floor(g + m), ugy = std::floor(e - dd0);
        double dd = right - hx;
        const double gg = dd * cc;
        dd *= f;
        const double xjx = std::floor(gg + m), xjy = std::floor(e - dd);
        const double ee = bottom - hy;
        const double dix = std::floor(gg + ee * f), diy = std::floor(ee * cc - dd);
        const double zlx = ugx + dix - xjx, zly = ugy + diy - xjy;
        const double minx = std::min(ugx, std::min(xjx, std::min(dix, zlx)));
        const double miny = std::min(ugy, std::min(xjy, std::min(diy, zly)));
        const double maxx = std::max(ugx, std::max(xjx, std::max(dix, zlx)));
        const double maxy = std::max(ugy, std::max(xjy, std::max(diy, zly)));
        hx = -minx; hy = -miny;
        right = maxx - minx; bottom = maxy - miny;
    }
    const int32_t l2 = int32_t(right), m = int32_t(bottom);
    if (l2 <= 0 || m <= 0) return false;
    const int32_t n2 = a.line_width, p2 = int32_t(double((l2 + 15) & 2147483632) / 16);
    bits.assign(size_t(p2 * m + 1), 0);
    line_width = p2;
    width = l2;
    height = m;
    hot_x = int32_t(hx);
    hot_y = int32_t(hy);
    b *= 0.017453292;
    const double fcos = std::cos(b), r2s = std::sin(b);
    int32_t row = 0;
    double h2 = std::floor(65536 * (g0 / 2 - (l2 / 2.0 * fcos - m / 2.0 * r2s) / c));
    double k = std::floor(65536 * (e0 / 2 - (l2 / 2.0 * r2s + m / 2.0 * fcos) / d));
    const double u2 = std::floor(65536 * fcos / c), v2 = std::floor(65536 * r2s / d);
    const double z2 = l2 / 16.0;
    const int32_t rem = l2 % 16;
    const double dstep = std::floor(65536 * fcos / d), cstep = std::floor(65536 * r2s / c);
    const double gmax = 65536 * g0, rmax = 65536 * e0;
    auto sample = [&](double t2, double C) {
        if (0 <= t2 && t2 < gmax && 0 <= C && C < rmax) {
            const double B2 = std::floor(t2 / 65536), A = std::floor(C / 65536);
            const uint32_t w2 = 32768u >> (int64_t(B2) % 16);
            const uint16_t v = a.bits[size_t(std::floor(A * n2 + B2 / 16))];
            return (v & w2) != 0;
        }
        return false;
    };
    for (int32_t f = 0; f < m; f++) {
        double t2 = h2, C = k;
        int32_t E2 = row;
        for (int32_t e = 0; e < z2; e++) { // the runtime's `e < z2` with z2 = l2 / 16 (fractional)
            uint32_t D2 = 0;
            for (int bit = 15; bit >= 0; bit--) {
                if (sample(t2, C)) D2 |= 1u << bit;
                t2 += u2;
                C += v2;
            }
            bits[size_t(E2++)] = uint16_t(D2);
        }
        if (rem != 0) {
            uint32_t B2 = 0, D2 = 32768;
            for (int32_t e = 0; e < rem; e++, D2 = (D2 >> 1) & 32767) {
                if (sample(t2, C)) B2 |= D2;
                t2 += u2;
                C += v2;
            }
            bits[size_t(E2)] = uint16_t(B2);
        }
        row += p2;
        h2 -= cstep;
        k += dstep;
    }
    return true;
}

bool Mask::overlaps(double a, double b, int32_t c, const Mask& d0, double e, double f, int32_t g) const { // wj
    const Mask* h2;
    const Mask* d;
    int32_t n2, cc, l2, gg, aa, bb;
    if (a <= e) {
        h2 = this; d = &d0;
        n2 = int32_t(std::floor(double(c))); cc = int32_t(std::floor(double(g)));
        l2 = int32_t(std::floor(a)); gg = int32_t(std::floor(b)); aa = int32_t(std::floor(e)); bb = int32_t(std::floor(f));
    } else {
        h2 = &d0; d = this;
        n2 = int32_t(std::floor(double(g))); cc = int32_t(std::floor(double(c)));
        l2 = int32_t(std::floor(e)); gg = int32_t(std::floor(f)); aa = int32_t(std::floor(a)); bb = int32_t(std::floor(b));
    }
    int32_t ff = h2->height, p2 = 0;
    if (n2 != 0) { ff = n2; gg += h2->height - n2; p2 = h2->height - n2; }
    int32_t ee = d->height, r2 = 0;
    if (cc != 0) { ee = cc; bb += d->height - cc; r2 = d->height - cc; }
    if (l2 >= aa + d->width || l2 + h2->width <= aa || gg >= bb + ee || gg + ff < bb) return false;
    int32_t c2 = aa - l2;
    const int32_t nw = c2 / 16;
    c2 %= 16;
    int32_t lw = std::min(l2 + h2->width - aa, d->width);
    lw = (lw + 15) / 16;
    int32_t a2, rows;
    if (gg <= bb) { a2 = bb - gg + p2; p2 = r2; rows = std::min(gg + ff, bb + ee) - bb; }
    else { a2 = p2; p2 = gg - bb + r2; rows = std::min(gg + ff, bb + ee) - gg; }
    int32_t bi = a2 * h2->line_width, ei = p2 * d->line_width;
    auto H = [&](int32_t i) -> uint32_t { return h2->bits[size_t(i)]; };
    auto Dw = [&](int32_t i) -> uint32_t { return d->bits[size_t(i)]; };
    if (c2 != 0) {
        switch (lw) {
        case 1:
            for (int32_t r = 0; r < rows; r++) {
                uint32_t v = H(bi + nw) << c2;
                if ((v & Dw(ei)) != 0) return true;
                if (nw + 1 < h2->line_width) { v = (H(bi + nw + 1) << c2) >> 16; if ((v & Dw(ei)) != 0) return true; }
                bi += h2->line_width; ei += d->line_width;
            }
            break;
        case 2:
            for (int32_t r = 0; r < rows; r++) {
                uint32_t v = H(bi + nw) << c2;
                if ((v & Dw(ei)) != 0) return true;
                v = H(bi + nw + 1) << c2;
                const uint32_t hi = v >> 16;
                if ((hi & Dw(ei)) != 0 || (v & Dw(ei + 1)) != 0) return true;
                if (nw + 2 < h2->line_width) { v = (H(bi + nw + 2) << c2) >> 16; if ((v & Dw(ei + 1)) != 0) return true; }
                bi += h2->line_width; ei += d->line_width;
            }
            break;
        default:
            for (int32_t r = 0; r < rows; r++) {
                uint32_t v = H(bi + nw) << c2;
                if ((v & Dw(ei)) != 0) return true;
                int32_t k = 0;
                for (; k < lw - 1; k++) {
                    v = H(bi + nw + k + 1) << c2;
                    const uint32_t hi = v >> 16;
                    if ((hi & Dw(ei + k)) != 0 || (v & Dw(ei + k + 1)) != 0) return true;
                }
                if (nw + k + 1 < h2->line_width) { v = (H(bi + nw + k + 1) << c2) >> 16; if ((v & Dw(ei + k)) != 0) return true; }
                bi += h2->line_width; ei += d->line_width;
            }
        }
    } else {
        for (int32_t r = 0; r < rows; r++) {
            for (int32_t k = 0; k < lw; k++) if ((Dw(ei + k) & H(bi + nw + k)) != 0) return true;
            bi += h2->line_width; ei += d->line_width;
        }
    }
    return false;
}

bool MaskPack::open(const std::string& path) {
    file_.open(path, std::ios::binary);
    uint8_t h[12];
    if (!file_.read(reinterpret_cast<char*>(h), 12) || h[0] != 'U' || h[1] != 'C' || h[2] != 'N' || h[3] != 'M') return false;
    const uint32_t n = uint32_t(h[8]) | uint32_t(h[9]) << 8 | uint32_t(h[10]) << 16 | uint32_t(h[11]) << 24;
    std::vector<uint8_t> t(size_t(n) * 16);
    if (!file_.read(reinterpret_cast<char*>(t.data()), std::streamsize(t.size()))) return false;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t* p = &t[size_t(i) * 16];
        auto u16 = [&](int o) { return uint32_t(p[o]) | uint32_t(p[o + 1]) << 8; };
        auto u32 = [&](int o) { return u16(o) | u16(o + 2) << 16; };
        entries_[int32_t(u32(0))] = Entry{u16(4), u16(6), u16(8), u32(12)};
    }
    return true;
}

bool MaskPack::load(int32_t image, Mask* out) {
    auto it = entries_.find(image);
    if (it == entries_.end()) return false;
    const Entry& e = it->second;
    std::vector<uint8_t> raw(size_t(e.line) * e.h * 2 + 2);
    file_.clear();
    file_.seekg(std::streamoff(e.offset));
    if (!file_.read(reinterpret_cast<char*>(raw.data()), std::streamsize(raw.size()))) return false;
    out->width = int32_t(e.w);
    out->height = int32_t(e.h);
    out->line_width = int32_t(e.line);
    out->bits.resize(raw.size() / 2);
    for (size_t i = 0; i < out->bits.size(); i++) out->bits[i] = uint16_t(raw[2 * i] | raw[2 * i + 1] << 8);
    return true;
}

bool Mask::test_point(double a, double b, double c, double d, double e) const { // $C
    a = std::floor(a);
    b = std::floor(b);
    double f = a, g = b;
    if (c == 0) {
        if (d != 1 || e != 1) { f = std::floor(f / d); g = std::floor(g / e); }
    } else {
        const double r = 3.141592653589 * c / 180, cc = std::cos(r), s = std::sin(r);
        f = a * cc - b * s;
        g = b * cc + a * s;
        if (d != 1 || e != 1) { f /= d; g /= e; }
    }
    f += hot_x;
    g += hot_y;
    const int32_t x = int32_t(std::floor(f)), y = int32_t(std::floor(g));
    if (x < 0 || x >= width || y < 0 || y >= height) return false;
    return (bits[size_t(y * line_width + x / 16)] & (32768u >> (x & 15))) != 0;
}

} // namespace ucn::game
