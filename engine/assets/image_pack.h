#pragma once
#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "assets/png.h"

namespace ucn::assets {

// The console's images (images.pak, platform/host/asset_pack): DXT blocks kept in memory, decoded to RGBA on demand
// into a cache of at most `budget_bytes`, least recently used out first.
class ImagePack {
public:
    bool open(const std::string& path, std::string* error);
    // RGBA at the pack's scale (used_w x used_h); valid until the next get(). nullptr: no such image.
    const Image* get(int32_t handle);
    double scale() const { return den_ ? double(num_) / den_ : 1; }
    size_t budget_bytes = 96u << 20;

private:
    struct Entry { uint32_t tex_w = 0, tex_h = 0, used_w = 0, used_h = 0, format = 0, offset = 0, size = 0; };
    std::vector<uint8_t> file_;
    std::map<int32_t, Entry> entries_;
    uint32_t num_ = 1, den_ = 1;
    std::list<std::pair<int32_t, Image>> cache_; // front = most recent
    std::map<int32_t, std::list<std::pair<int32_t, Image>>::iterator> where_;
    size_t cached_bytes_ = 0;
};

} // namespace ucn::assets
