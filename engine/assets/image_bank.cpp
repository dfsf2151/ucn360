#include "assets/image_bank.h"

#include <cstring>

namespace ucn::assets {

namespace {

struct Reader {
    const std::vector<uint8_t>& b;
    size_t p;
    bool ok = true;

    bool has(size_t n) const { return ok && p + n <= b.size(); }
    uint8_t u8() { if (!has(1)) { ok = false; return 0; } return b[p++]; }
    uint16_t u16() { if (!has(2)) { ok = false; return 0; } uint16_t v = uint16_t(b[p]) | uint16_t(b[p + 1]) << 8; p += 2; return v; }
    int16_t i16() { return int16_t(u16()); }
    uint32_t u32() {
        if (!has(4)) { ok = false; return 0; }
        uint32_t v = uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 | uint32_t(b[p + 3]) << 24;
        p += 4;
        return v;
    }
    int32_t i32() { return int32_t(u32()); }
};

} // namespace

bool ImageBank::load(const data::Bundle& bundle, const std::string& root, std::string* error) {
    bundle_ = &bundle;
    root_ = root;
    records_.clear();
    atlases_.clear();
    crops_.clear();

    const uint32_t count = bundle.count(data::kImages);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t at = bundle.record_offset(data::kImages, i);
        if (at == 0) {
            if (error) *error = "image record table is truncated";
            return false;
        }
        Reader r{bundle.bytes(), at};
        ImageRecord rec;
        rec.handle = r.u32();
        rec.file_idx = r.u32();
        rec.crop_x = r.i32(); rec.crop_y = r.i32();
        rec.crop_w = r.i32(); rec.crop_h = r.i32();
        rec.src_w = r.i32(); rec.src_h = r.i32();
        rec.hotspot_x = r.i16(); rec.hotspot_y = r.i16();
        rec.off_x = r.i16(); rec.off_y = r.i16();
        rec.flags = r.u16();
        if (!r.ok) {
            if (error) *error = "image record runs past the section";
            return false;
        }
        const std::string* name = bundle.string_at(rec.file_idx);
        rec.file = name ? *name : std::string();
        records_[rec.handle] = rec;
    }
    return true;
}

const ImageRecord* ImageBank::record(uint32_t handle) const {
    auto it = records_.find(handle);
    return it == records_.end() ? nullptr : &it->second;
}

const Image* ImageBank::atlas(uint32_t file_idx, std::string* error) {
    auto it = atlases_.find(file_idx);
    if (it != atlases_.end()) return &it->second;
    const std::string* name = bundle_ ? bundle_->string_at(file_idx) : nullptr;
    if (!name || name->empty()) {
        if (error) *error = "atlas has no file name";
        return nullptr;
    }
    Image img;
    std::string err;
    if (!png_decode_file(root_ + "/" + *name, &img, &err)) {
        if (error) *error = err + " (" + *name + ")";
        return nullptr;
    }
    it = atlases_.emplace(file_idx, std::move(img)).first;
    return &it->second;
}

const Image* ImageBank::crop(uint32_t handle, std::string* error) {
    auto cached = crops_.find(handle);
    if (cached != crops_.end()) return &cached->second;
    const ImageRecord* rec = record(handle);
    if (!rec) {
        if (error) *error = "no image record for handle " + std::to_string(handle);
        return nullptr;
    }
    const Image* src = atlas(rec->file_idx, error);
    if (!src) return nullptr;
    if (rec->crop_x < 0 || rec->crop_y < 0 || rec->crop_w <= 0 || rec->crop_h <= 0 ||
        size_t(rec->crop_x) + rec->crop_w > src->width ||
        size_t(rec->crop_y) + rec->crop_h > src->height) {
        if (error) *error = "crop out of atlas bounds for handle " + std::to_string(handle);
        return nullptr;
    }
    Image out;
    out.width = uint32_t(rec->crop_w);
    out.height = uint32_t(rec->crop_h);
    out.rgba.resize(size_t(out.width) * out.height * 4);
    for (int32_t y = 0; y < rec->crop_h; ++y) {
        const uint8_t* srow = src->rgba.data() + (size_t(rec->crop_y + y) * src->width + rec->crop_x) * 4;
        std::memcpy(out.rgba.data() + size_t(y) * out.width * 4, srow, size_t(out.width) * 4);
    }
    auto it = crops_.emplace(handle, std::move(out)).first;
    return &it->second;
}

} // namespace ucn::assets
