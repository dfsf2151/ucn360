#include "assets/png.h"
// asset_pack BUNDLE.bin out/images.pak [--root assets_raw/out] [--scale NUM/DEN] [--masks out/masks.pak]
// Builds the console's texture pack: every bundle image cropped from its atlas, scaled (default 2/3: the game's
// 1920x1080 logical space drawn at 1280x720), padded to whole 4x4 blocks and DXT-compressed: DXT1 when alpha is
// only ever 0 or 255, DXT5 otherwise. Layout (little-endian):
//   "UCNI", u32 version 1, u32 count, u32 scale_num, u32 scale_den
//   count x { i32 handle, u16 tex_w, u16 tex_h, u16 used_w, u16 used_h, u8 format (0 DXT1, 1 DXT5), u8 pad[3],
//             u32 offset, u32 size }       (used_w/h: the scaled image inside the padded texture)
//   then the block data, each image 128-byte aligned.
// --masks also writes the collision masks at full size (the console has only the scaled images), the runtime's
// 1-bit-per-pixel rows (game::Mask): "UCNM", u32 version 1, u32 count, count x { i32 handle, u16 w, u16 h,
// u16 line_words, u16 pad, u32 offset }, then each mask's u16 words.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "assets/dxt.h"
#include "game/mask.h"
#include "assets/image_bank.h"
#include "data/bundle.h"
#include "data/reader.h"

namespace {
void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back(uint8_t(x >> (8 * i))); }
void put16(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); }

// Area-average downscale in premultiplied alpha (no dark fringes), into a w4 x h4 canvas padded with transparency.
std::vector<uint8_t> scale(const ucn::assets::Image& src, double s, uint32_t* ow, uint32_t* oh, uint32_t* w4, uint32_t* h4) {
    const uint32_t w = std::max(1u, uint32_t(std::lround(src.width * s)));
    const uint32_t h = std::max(1u, uint32_t(std::lround(src.height * s)));
    *ow = w; *oh = h; *w4 = (w + 3) & ~3u; *h4 = (h + 3) & ~3u;
    std::vector<uint8_t> out(size_t(*w4) * *h4 * 4, 0);
    const double fx = double(src.width) / w, fy = double(src.height) / h;
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            const double x0 = x * fx, x1 = (x + 1) * fx, y0 = y * fy, y1 = (y + 1) * fy;
            double acc[4] = {0, 0, 0, 0}, area = 0;
            for (uint32_t sy = uint32_t(y0); sy < std::min<double>(std::ceil(y1), src.height); sy++)
                for (uint32_t sx = uint32_t(x0); sx < std::min<double>(std::ceil(x1), src.width); sx++) {
                    const double wx = std::min<double>(x1, sx + 1) - std::max<double>(x0, sx);
                    const double wy = std::min<double>(y1, sy + 1) - std::max<double>(y0, sy);
                    const double wt = wx * wy;
                    if (wt <= 0) continue;
                    const uint8_t* p = src.rgba.data() + (size_t(sy) * src.width + sx) * 4;
                    const double a = p[3] / 255.0;
                    for (int c = 0; c < 3; c++) acc[c] += p[c] * a * wt;
                    acc[3] += p[3] * wt;
                    area += wt;
                }
            uint8_t* d = out.data() + (size_t(y) * *w4 + x) * 4;
            if (area <= 0) continue;
            const double A = acc[3] / area;
            d[3] = uint8_t(std::lround(std::min(255.0, A)));
            for (int c = 0; c < 3; c++) d[c] = A > 0 ? uint8_t(std::lround(std::min(255.0, acc[c] / area / (A / 255.0)))) : 0;
        }
    return out;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: asset_pack BUNDLE.bin out.pak [--root DIR] [--scale N/D]\n"); return 2; }
    std::string root = "assets_raw/out";
    uint32_t num = 2, den = 3;
    std::string masks_path, override_dir;
    std::vector<std::pair<uint32_t, std::string>> exports; // --export H:file.png writes image H as decoded, to edit
    for (int i = 3; i + 1 < argc; i++) {
        if (!std::strcmp(argv[i], "--override")) override_dir = argv[i + 1]; // DIR/H.png replaces image H
        if (!std::strcmp(argv[i], "--export")) exports.push_back({uint32_t(std::atoi(argv[i + 1])), std::strchr(argv[i + 1], ':') + 1});
        if (!std::strcmp(argv[i], "--masks")) masks_path = argv[i + 1];
        if (!std::strcmp(argv[i], "--root")) root = argv[i + 1];
        if (!std::strcmp(argv[i], "--scale")) std::sscanf(argv[i + 1], "%u/%u", &num, &den);
    }
    ucn::data::Bundle bundle;
    std::string error;
    if (!bundle.open(argv[1], &error)) { std::fprintf(stderr, "bundle: %s\n", error.c_str()); return 1; }
    ucn::assets::ImageBank bank;
    if (!bank.load(bundle, root, &error)) { std::fprintf(stderr, "images: %s\n", error.c_str()); return 1; }
    for (const auto& e : exports) {
        const ucn::assets::Image* im = bank.crop(e.first, &error);
        if (!im || !ucn::assets::png_write_file(e.second, *im, &error)) { std::fprintf(stderr, "export %u: %s\n", e.first, error.c_str()); return 1; }
    }
    if (!exports.empty()) return 0;
    const uint32_t n = bundle.count(ucn::data::kImages);
    std::vector<uint32_t> handles;
    for (uint32_t i = 0; i < n; i++) {
        ucn::data::Reader r{bundle.bytes(), bundle.record_offset(ucn::data::kImages, i)};
        handles.push_back(r.u32());
    }
    // Group by atlas file so each atlas decodes once.
    std::sort(handles.begin(), handles.end(), [&](uint32_t a, uint32_t b) {
        const auto *ra = bank.record(a), *rb = bank.record(b);
        return ra->file_idx != rb->file_idx ? ra->file_idx < rb->file_idx : a < b;
    });
    const double s = double(num) / den;
    std::vector<uint8_t> table, data, mtable, mdata;
    size_t dxt1 = 0, dxt5 = 0, missing = 0;
    for (uint32_t h : handles) {
        const ucn::assets::Image* im = bank.crop(h, &error);
        ucn::assets::Image replaced;
        if (im && !override_dir.empty() && ucn::assets::png_decode_file(override_dir + "/" + std::to_string(h) + ".png", &replaced, &error)) {
            im = &replaced; // a new size is fine for drawing; collision masks keep the original (built without --override)
        }
        uint32_t w = 0, hh = 0, w4 = 4, h4 = 4;
        std::vector<uint8_t> px;
        if (!im) { missing++; px.assign(64, 0); }
        else px = scale(*im, s, &w, &hh, &w4, &h4);
        if (im && !masks_path.empty()) {
            ucn::game::Mask m;
            m.build(im->rgba.data(), int32_t(im->width), int32_t(im->height), 0, 0);
            put32(mtable, h); put16(mtable, uint32_t(m.width)); put16(mtable, uint32_t(m.height));
            put16(mtable, uint32_t(m.line_width)); put16(mtable, 0); put32(mtable, uint32_t(mdata.size()));
            for (uint16_t word : m.bits) put16(mdata, word);
        }
        bool binary = true;
        for (size_t i = 3; i < px.size(); i += 4) if (px[i] != 0 && px[i] != 255) { binary = false; break; }
        const std::vector<uint8_t> blocks = ucn::assets::dxt_encode(px.data(), w4, h4, !binary);
        (binary ? dxt1 : dxt5)++;
        while (data.size() % 128) data.push_back(0);
        put32(table, h); put16(table, w4); put16(table, h4); put16(table, w); put16(table, hh);
        table.push_back(binary ? 0 : 1); table.push_back(0); table.push_back(0); table.push_back(0);
        put32(table, uint32_t(data.size())); put32(table, uint32_t(blocks.size()));
        data.insert(data.end(), blocks.begin(), blocks.end());
    }
    std::vector<uint8_t> head = {'U', 'C', 'N', 'I'};
    put32(head, 1); put32(head, uint32_t(handles.size())); put32(head, num); put32(head, den);
    const uint32_t base = uint32_t(head.size() + table.size());
    // offsets are relative to the data start; store absolute
    for (size_t i = 0; i < handles.size(); i++) {
        uint8_t* p = table.data() + i * 24 + 16;
        uint32_t off = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
        off += base;
        for (int k = 0; k < 4; k++) p[k] = uint8_t(off >> (8 * k));
    }
    FILE* f = std::fopen(argv[2], "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
    std::fwrite(head.data(), 1, head.size(), f);
    std::fwrite(table.data(), 1, table.size(), f);
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
    std::printf("%zu images (%zu DXT1, %zu DXT5, %zu missing) scale %u/%u -> %s, %.1f MB\n", handles.size(), dxt1, dxt5,
                missing, num, den, argv[2], (head.size() + table.size() + data.size()) / 1e6);
    if (!masks_path.empty()) {
        std::vector<uint8_t> mh = {'U', 'C', 'N', 'M'};
        put32(mh, 1); put32(mh, uint32_t(mtable.size() / 16));
        const uint32_t mbase = uint32_t(mh.size() + mtable.size());
        for (size_t i = 0; i < mtable.size() / 16; i++) { // offsets absolute
            uint8_t* p = mtable.data() + i * 16 + 12;
            const uint32_t off = (uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24) + mbase;
            for (int k = 0; k < 4; k++) p[k] = uint8_t(off >> (8 * k));
        }
        FILE* m = std::fopen(masks_path.c_str(), "wb");
        if (!m) { std::fprintf(stderr, "cannot write %s\n", masks_path.c_str()); return 1; }
        std::fwrite(mh.data(), 1, mh.size(), m);
        std::fwrite(mtable.data(), 1, mtable.size(), m);
        std::fwrite(mdata.data(), 1, mdata.size(), m);
        std::fclose(m);
        std::printf("masks -> %s, %.1f MB\n", masks_path.c_str(), (mh.size() + mtable.size() + mdata.size()) / 1e6);
    }
    return 0;
}
