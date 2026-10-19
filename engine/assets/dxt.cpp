#include "assets/dxt.h"

#include <algorithm>
#include <cmath>

namespace ucn::assets {

namespace {

uint16_t to565(const float c[3]) {
    const int r = std::clamp(int(std::lround(c[0] * 31 / 255)), 0, 31);
    const int g = std::clamp(int(std::lround(c[1] * 63 / 255)), 0, 63);
    const int b = std::clamp(int(std::lround(c[2] * 31 / 255)), 0, 31);
    return uint16_t(r << 11 | g << 5 | b);
}
void from565(uint16_t v, float out[3]) {
    const int r = v >> 11 & 31, g = v >> 5 & 63, b = v & 31;
    out[0] = float((r << 3) | (r >> 2));
    out[1] = float((g << 2) | (g >> 4));
    out[2] = float((b << 3) | (b >> 2));
}
void put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }

// Colour block: endpoints along the principal axis of the 16 colours (a few power iterations), then the best of
// the 4 palette entries per texel. Transparent texels (alpha 0) do not steer the fit.
void color_block(const uint8_t px[16][4], bool dxt1_alpha, uint8_t* out) {
    float mean[3] = {0, 0, 0};
    int n = 0;
    for (int i = 0; i < 16; i++) {
        if (dxt1_alpha && px[i][3] < 128) continue;
        for (int c = 0; c < 3; c++) mean[c] += px[i][c];
        n++;
    }
    if (n == 0) { // fully transparent DXT1 block: colour 0 < colour 1 mode, index 3 = transparent
        put16(out, 0); put16(out + 2, 0xFFFF);
        out[4] = out[5] = out[6] = out[7] = 0xFF;
        return;
    }
    for (int c = 0; c < 3; c++) mean[c] /= n;
    float cov[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 16; i++) {
        if (dxt1_alpha && px[i][3] < 128) continue;
        const float d[3] = {px[i][0] - mean[0], px[i][1] - mean[1], px[i][2] - mean[2]};
        cov[0] += d[0] * d[0]; cov[1] += d[0] * d[1]; cov[2] += d[0] * d[2];
        cov[3] += d[1] * d[1]; cov[4] += d[1] * d[2]; cov[5] += d[2] * d[2];
    }
    float axis[3] = {1, 1, 1};
    for (int it = 0; it < 6; it++) {
        const float x = cov[0] * axis[0] + cov[1] * axis[1] + cov[2] * axis[2];
        const float y = cov[1] * axis[0] + cov[3] * axis[1] + cov[4] * axis[2];
        const float z = cov[2] * axis[0] + cov[4] * axis[1] + cov[5] * axis[2];
        const float m = std::max(std::fabs(x), std::max(std::fabs(y), std::fabs(z)));
        if (m <= 0) break;
        axis[0] = x / m; axis[1] = y / m; axis[2] = z / m;
    }
    float lo = 1e30f, hi = -1e30f;
    for (int i = 0; i < 16; i++) {
        if (dxt1_alpha && px[i][3] < 128) continue;
        const float t = (px[i][0] - mean[0]) * axis[0] + (px[i][1] - mean[1]) * axis[1] + (px[i][2] - mean[2]) * axis[2];
        lo = std::min(lo, t); hi = std::max(hi, t);
    }
    const float len2 = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
    float c0[3], c1[3];
    for (int c = 0; c < 3; c++) {
        c0[c] = std::clamp(mean[c] + axis[c] * hi / (len2 > 0 ? len2 : 1), 0.f, 255.f);
        c1[c] = std::clamp(mean[c] + axis[c] * lo / (len2 > 0 ? len2 : 1), 0.f, 255.f);
    }
    uint16_t e0 = to565(c0), e1 = to565(c1);
    const bool three = dxt1_alpha; // keep a transparent index available when the block has holes
    bool has_holes = false;
    if (dxt1_alpha) for (int i = 0; i < 16; i++) if (px[i][3] < 128) has_holes = true;
    // 4-colour mode needs e0 > e1; 3-colour + transparent needs e0 <= e1.
    if (three && has_holes) { if (e0 > e1) std::swap(e0, e1); }
    else { if (e0 < e1) std::swap(e0, e1); if (e0 == e1) { if (e1 > 0) e1--; else e0++; } }
    float p[4][3];
    from565(e0, p[0]); from565(e1, p[1]);
    const bool four = e0 > e1;
    for (int c = 0; c < 3; c++) {
        if (four) { p[2][c] = (2 * p[0][c] + p[1][c]) / 3; p[3][c] = (p[0][c] + 2 * p[1][c]) / 3; }
        else { p[2][c] = (p[0][c] + p[1][c]) / 2; p[3][c] = 0; }
    }
    uint32_t idx = 0;
    for (int i = 0; i < 16; i++) {
        uint32_t best = 0;
        if (!four && dxt1_alpha && px[i][3] < 128) best = 3;
        else {
            float bd = 1e30f;
            for (uint32_t k = 0; k < (four ? 4u : 3u); k++) {
                float d = 0;
                for (int c = 0; c < 3; c++) { const float e = px[i][c] - p[k][c]; d += e * e; }
                if (d < bd) { bd = d; best = k; }
            }
        }
        idx |= best << (2 * i);
    }
    put16(out, e0); put16(out + 2, e1);
    out[4] = uint8_t(idx); out[5] = uint8_t(idx >> 8); out[6] = uint8_t(idx >> 16); out[7] = uint8_t(idx >> 24);
}

void alpha_block(const uint8_t px[16][4], uint8_t* out) { // 8-value mode (a0 > a1)
    int lo = 255, hi = 0;
    for (int i = 0; i < 16; i++) { lo = std::min<int>(lo, px[i][3]); hi = std::max<int>(hi, px[i][3]); }
    if (hi == lo) { out[0] = uint8_t(hi); out[1] = uint8_t(lo); for (int i = 2; i < 8; i++) out[i] = 0; return; }
    const int a0 = hi, a1 = lo;
    int pal[8];
    pal[0] = a0; pal[1] = a1;
    for (int k = 1; k < 7; k++) pal[k + 1] = ((7 - k) * a0 + k * a1) / 7;
    uint64_t bits = 0;
    for (int i = 0; i < 16; i++) {
        int best = 0, bd = 1 << 30;
        for (int k = 0; k < 8; k++) { const int d = std::abs(px[i][3] - pal[k]); if (d < bd) { bd = d; best = k; } }
        bits |= uint64_t(best) << (3 * i);
    }
    out[0] = uint8_t(a0); out[1] = uint8_t(a1);
    for (int i = 0; i < 6; i++) out[2 + i] = uint8_t(bits >> (8 * i));
}

} // namespace

std::vector<uint8_t> dxt_encode(const uint8_t* rgba, uint32_t w, uint32_t h, bool with_alpha) {
    const uint32_t bw = w / 4, bh = h / 4, bs = with_alpha ? 16 : 8;
    std::vector<uint8_t> out(size_t(bw) * bh * bs);
    uint8_t px[16][4];
    for (uint32_t by = 0; by < bh; by++)
        for (uint32_t bx = 0; bx < bw; bx++) {
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++) {
                    const uint8_t* s = rgba + ((size_t(by) * 4 + y) * w + bx * 4 + x) * 4;
                    std::copy(s, s + 4, px[y * 4 + x]);
                }
            uint8_t* o = out.data() + (size_t(by) * bw + bx) * bs;
            if (with_alpha) { alpha_block(px, o); color_block(px, false, o + 8); }
            else color_block(px, true, o);
        }
    return out;
}

void dxt_decode(const uint8_t* blocks, uint32_t w, uint32_t h, bool with_alpha, uint8_t* rgba) {
    const uint32_t bw = w / 4, bh = h / 4, bs = with_alpha ? 16 : 8;
    for (uint32_t by = 0; by < bh; by++)
        for (uint32_t bx = 0; bx < bw; bx++) {
            const uint8_t* b = blocks + (size_t(by) * bw + bx) * bs;
            int alpha[16];
            for (int i = 0; i < 16; i++) alpha[i] = 255;
            if (with_alpha) {
                const int a0 = b[0], a1 = b[1];
                int pal[8] = {a0, a1};
                if (a0 > a1) for (int k = 1; k < 7; k++) pal[k + 1] = ((7 - k) * a0 + k * a1) / 7;
                else { for (int k = 1; k < 5; k++) pal[k + 1] = ((5 - k) * a0 + k * a1) / 5; pal[6] = 0; pal[7] = 255; }
                uint64_t bits = 0;
                for (int i = 0; i < 6; i++) bits |= uint64_t(b[2 + i]) << (8 * i);
                for (int i = 0; i < 16; i++) alpha[i] = pal[(bits >> (3 * i)) & 7];
                b += 8;
            }
            const uint16_t e0 = uint16_t(b[0] | b[1] << 8), e1 = uint16_t(b[2] | b[3] << 8);
            float p[4][3];
            from565(e0, p[0]); from565(e1, p[1]);
            const bool four = e0 > e1 || with_alpha;
            for (int c = 0; c < 3; c++) {
                if (four) { p[2][c] = (2 * p[0][c] + p[1][c]) / 3; p[3][c] = (p[0][c] + 2 * p[1][c]) / 3; }
                else { p[2][c] = (p[0][c] + p[1][c]) / 2; p[3][c] = 0; }
            }
            const uint32_t idx = uint32_t(b[4]) | uint32_t(b[5]) << 8 | uint32_t(b[6]) << 16 | uint32_t(b[7]) << 24;
            for (int i = 0; i < 16; i++) {
                const uint32_t k = idx >> (2 * i) & 3;
                uint8_t* d = rgba + ((size_t(by) * 4 + i / 4) * w + bx * 4 + i % 4) * 4;
                for (int c = 0; c < 3; c++) d[c] = uint8_t(p[k][c]);
                d[3] = uint8_t(!with_alpha && !four && k == 3 ? 0 : alpha[i]);
            }
        }
}

} // namespace ucn::assets
