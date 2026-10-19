#include "data/bundle.h"

#include <cstring>
#include <fstream>

namespace ucn::data {

namespace {
constexpr uint32_t kHeaderSize = 16;
constexpr uint32_t kSectionEntrySize = 12;
// Frame record fixed parts (see the bundle format section in the M1 plan).
constexpr uint32_t kFramePrologue = 12;   // name_idx, width, height
constexpr uint32_t kFrameLayerSize = 16;  // name_idx, x_coeff, y_coeff, flags
constexpr uint32_t kFrameBackground = 20; // mode, image, x, y, color
} // namespace

// Little-endian whatever the host is (the console is big-endian PowerPC).
template <typename T>
T Bundle::read(uint32_t at) const {
    if (at > bytes_.size() || bytes_.size() - at < sizeof(T)) return T{};
    uint64_t v = 0;
    for (size_t i = 0; i < sizeof(T); i++) v |= uint64_t(bytes_[at + i]) << (8 * i);
    if constexpr (sizeof(T) == 1) return T(uint8_t(v));
    else if constexpr (sizeof(T) == 2) return T(uint16_t(v));
    else if constexpr (sizeof(T) == 4) return T(uint32_t(v));
    else return T(v);
}

bool Bundle::open(const std::string& path, std::string* error) {
    auto fail = [&](const char* what) {
        if (error) *error = what;
        bytes_.clear();
        sections_.clear();
        strings_.clear();
        return false;
    };

    std::ifstream f(path, std::ios::binary);
    if (!f) return fail("cannot open file");
    f.seekg(0, std::ios::end);
    const std::streamoff file_size = f.tellg();
    if (file_size < static_cast<std::streamoff>(kHeaderSize)) return fail("file too small");
    f.seekg(0, std::ios::beg);
    bytes_.resize(static_cast<size_t>(file_size));
    f.read(reinterpret_cast<char*>(bytes_.data()), file_size);
    if (!f) return fail("short read");

    if (std::memcmp(bytes_.data(), "UCNB", 4) != 0) return fail("bad magic");
    if (read<uint16_t>(4) != 2) return fail("unsupported version");
    const uint32_t section_count = read<uint16_t>(6);
    if (read<uint32_t>(8) != bytes_.size()) return fail("total size mismatch");
    if (read<uint32_t>(12) != 0) return fail("reserved field is not zero");
    if (section_count == 0 ||
        kHeaderSize + section_count * kSectionEntrySize > bytes_.size())
        return fail("bad section table");

    for (uint32_t i = 0; i < section_count; ++i) {
        const uint32_t at = kHeaderSize + i * kSectionEntrySize;
        Section s;
        s.type = read<uint32_t>(at);
        s.offset = read<uint32_t>(at + 4);
        s.size = read<uint32_t>(at + 8);
        if (s.offset > bytes_.size() || bytes_.size() - s.offset < s.size)
            return fail("section out of bounds");
        sections_.push_back(s);
    }

    if (!parse_strings(error)) return false;
    return true;
}

bool Bundle::parse_strings(std::string* error) {
    strings_.clear();
    const Section* s = find(kStrings);
    if (!s) {
        if (error) *error = "missing strings section";
        return false;
    }
    const uint32_t count = read<uint32_t>(s->offset);
    uint32_t p = s->offset + 4;
    const uint32_t end = s->offset + s->size;
    for (uint32_t i = 0; i < count; ++i) {
        if (p > end || end - p < 4) {
            if (error) *error = "string table truncated";
            return false;
        }
        const uint32_t len = read<uint32_t>(p);
        p += 4;
        if (end - p < len) {
            if (error) *error = "string data truncated";
            return false;
        }
        strings_.emplace_back(reinterpret_cast<const char*>(bytes_.data() + p), len);
        p += len;
    }
    if (p != end) {
        if (error) *error = "string table size mismatch";
        return false;
    }
    return true;
}

const Section* Bundle::find(uint32_t type) const {
    for (const Section& s : sections_)
        if (s.type == type) return &s;
    return nullptr;
}

const std::string* Bundle::string_at(uint32_t index) const {
    if (index >= strings_.size()) return nullptr;
    return &strings_[index];
}

uint32_t Bundle::section_count_field(uint32_t type) const {
    const Section* s = find(type);
    if (!s || s->size < 4) return 0;
    return read<uint32_t>(s->offset);
}

uint32_t Bundle::count(uint32_t section_type) const { return section_count_field(section_type); }

uint32_t Bundle::record_offset(uint32_t section_type, uint32_t index) const {
    const Section* s = find(section_type);
    if (!s) return 0;
    const uint32_t n = read<uint32_t>(s->offset);
    if (index >= n) return 0;
    const uint32_t off = read<uint32_t>(s->offset + 4 + 4 * index);
    if (off == 0 || off >= s->size) return 0;
    return s->offset + off;
}

uint32_t Bundle::frame_count() const { return section_count_field(kFrames); }

uint32_t Bundle::frame_record(uint32_t i) const { return record_offset(kFrames, i); }

std::string Bundle::frame_name(uint32_t i) const {
    const uint32_t r = frame_record(i);
    if (r == 0) return {};
    const std::string* name = string_at(read<uint32_t>(r));
    return name ? *name : std::string();
}

uint32_t Bundle::frame_instance_count(uint32_t i) const {
    const uint32_t r = frame_record(i);
    if (r == 0) return 0;
    const uint32_t layers = read<uint32_t>(r + kFramePrologue);
    const uint32_t p = r + kFramePrologue + 4 + layers * kFrameLayerSize + kFrameBackground;
    return read<uint32_t>(p);
}

uint32_t Bundle::object_count() const { return section_count_field(kObjects); }
uint32_t Bundle::image_count() const { return section_count_field(kImages); }
uint32_t Bundle::resource_count() const { return section_count_field(kResources); }

} // namespace ucn::data
