#pragma once
#include <cstdint>
#include <vector>

namespace ucn::assets {

// DXT1 / DXT5 (BC1 / BC3) block compression for the console's texture pack, and decoding for host checks. Blocks
// are 4x4 texels in .dds order, little-endian 16-bit words (xsdk's DXT upload takes them byte-swapped, as the CPU's).
// `rgba` is width x height RGBA8 with width and height multiples of 4.
std::vector<uint8_t> dxt_encode(const uint8_t* rgba, uint32_t width, uint32_t height, bool with_alpha);
void dxt_decode(const uint8_t* blocks, uint32_t width, uint32_t height, bool with_alpha, uint8_t* rgba);

} // namespace ucn::assets
