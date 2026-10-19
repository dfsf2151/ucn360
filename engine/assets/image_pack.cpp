#include "assets/image_pack.h"

#include <cstring>
#include <fstream>

#include "assets/dxt.h"

namespace ucn::assets {

namespace {
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint32_t le16(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8; }
} // namespace

bool ImagePack::open(const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    f.seekg(0, std::ios::end);
    file_.resize(size_t(f.tellg()));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(file_.data()), std::streamsize(file_.size()));
    if (!f || file_.size() < 20 || std::memcmp(file_.data(), "UCNI", 4) != 0 || le32(&file_[4]) != 1) {
        if (error) *error = "bad image pack " + path;
        return false;
    }
    const uint32_t n = le32(&file_[8]);
    num_ = le32(&file_[12]);
    den_ = le32(&file_[16]);
    if (file_.size() < 20 + size_t(n) * 24) { if (error) *error = "short image pack"; return false; }
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t* p = &file_[20 + size_t(i) * 24];
        Entry e;
        e.tex_w = le16(p + 4); e.tex_h = le16(p + 6); e.used_w = le16(p + 8); e.used_h = le16(p + 10);
        e.format = p[12]; e.offset = le32(p + 16); e.size = le32(p + 20);
        if (size_t(e.offset) + e.size > file_.size()) { if (error) *error = "image pack entry out of range"; return false; }
        entries_[int32_t(le32(p))] = e;
    }
    return true;
}

const Image* ImagePack::get(int32_t handle) {
    auto w = where_.find(handle);
    if (w != where_.end()) {
        cache_.splice(cache_.begin(), cache_, w->second);
        return &cache_.front().second;
    }
    auto it = entries_.find(handle);
    if (it == entries_.end() || it->second.used_w == 0) return nullptr;
    const Entry& e = it->second;
    std::vector<uint8_t> full(size_t(e.tex_w) * e.tex_h * 4);
    dxt_decode(&file_[e.offset], e.tex_w, e.tex_h, e.format == 1, full.data());
    Image img;
    img.width = e.used_w;
    img.height = e.used_h;
    img.rgba.resize(size_t(e.used_w) * e.used_h * 4);
    for (uint32_t y = 0; y < e.used_h; y++)
        std::memcpy(&img.rgba[size_t(y) * e.used_w * 4], &full[size_t(y) * e.tex_w * 4], size_t(e.used_w) * 4);
    cached_bytes_ += img.rgba.size();
    cache_.emplace_front(handle, std::move(img));
    where_[handle] = cache_.begin();
    while (cached_bytes_ > budget_bytes && cache_.size() > 1) { // never the one just made
        cached_bytes_ -= cache_.back().second.rgba.size();
        where_.erase(cache_.back().first);
        cache_.pop_back();
    }
    return &cache_.front().second;
}

} // namespace ucn::assets
