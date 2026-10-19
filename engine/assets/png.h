#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ucn::assets {

struct Image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba; // width * height * 4
    mutable int8_t opaque = -1; // every alpha 255? worked out on first use (render_world's occlusion), -1 = not yet
};

// Decodes an 8-bit (or 16-bit, high byte) non-interlaced PNG into RGBA8.
bool png_decode(const uint8_t* data, size_t size, Image* out, std::string* error);
bool png_decode_file(const std::string& path, Image* out, std::string* error);

// Writes an 8-bit RGBA PNG (filter 0, single IDAT).
bool png_write_file(const std::string& path, const Image& image, std::string* error);

} // namespace ucn::assets
