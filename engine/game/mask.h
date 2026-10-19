#pragma once
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace ucn::game {

// Collision mask (the runtime's D, pretty line ~13640): 1 bit per pixel, 16 pixels per u16 word, MSB first,
// set where the image's alpha is not 0. `hot_x/hot_y` is the hotspot (Ga/Ca) inside the mask.
struct Mask {
    int32_t width = 0, height = 0, line_width = 0;
    int32_t hot_x = 0, hot_y = 0;
    std::vector<uint16_t> bits;

    // iu: from an RGBA8 crop.
    void build(const uint8_t* rgba, int32_t w, int32_t h, int32_t hot_x, int32_t hot_y);
    // UG: `src` rotated by `angle` degrees and scaled by sx, sy. False when the result is empty.
    bool transform(const Mask& src, double angle, double sx, double sy);
    // wj: does this mask at (x, y) overlap `other` at (ox, oy)? Positions are the masks' top-left corners.
    bool overlaps(double x, double y, int32_t height_limit, const Mask& other, double ox, double oy,
                  int32_t other_height_limit) const;
    // $C: is the point (x, y), relative to the hotspot, solid once undone through angle and scale?
    bool test_point(double x, double y, double angle, double sx, double sy) const;
};

// masks.pak (platform/host/asset_pack --masks): full-size masks read on demand, for a console that only has the
// scaled images. load() fills width/height/bits; the caller sets the hotspot.
class MaskPack {
public:
    bool open(const std::string& path);
    bool load(int32_t image, Mask* out);

private:
    struct Entry { uint32_t w = 0, h = 0, line = 0, offset = 0; };
    std::ifstream file_;
    std::map<int32_t, Entry> entries_;
};

} // namespace ucn::game
