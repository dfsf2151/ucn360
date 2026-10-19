#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "assets/png.h"
#include "data/bundle.h"

namespace ucn::assets {

struct ImageRecord {
    uint32_t handle = 0;
    uint32_t file_idx = 0;
    int32_t crop_x = 0, crop_y = 0, crop_w = 0, crop_h = 0;
    int32_t src_w = 0, src_h = 0; // atlas size
    int16_t hotspot_x = 0, hotspot_y = 0;
    int16_t off_x = 0, off_y = 0;
    uint16_t flags = 0;
    std::string file;
};

// IMAGES section -> decoded crops, with the atlas files loaded on demand from a root
// directory (host: the recovered assets; console: the XEX directory).
class ImageBank {
public:
    bool load(const data::Bundle& bundle, const std::string& root, std::string* error);

    size_t image_count() const { return records_.size(); }
    const ImageRecord* record(uint32_t handle) const;
    const Image* crop(uint32_t handle, std::string* error);
    const Image* atlas(uint32_t file_idx, std::string* error);

private:
    const data::Bundle* bundle_ = nullptr;
    std::string root_;
    std::map<uint32_t, ImageRecord> records_;
    std::map<uint32_t, Image> atlases_;  // by file_idx
    std::map<uint32_t, Image> crops_;    // by image handle
};

} // namespace ucn::assets
