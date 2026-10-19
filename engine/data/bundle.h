#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ucn::data {

enum SectionType : uint32_t {
    kStrings = 1, kFrames = 2, kObjects = 3, kImages = 4,
    kSounds = 5, kProgram = 6, kResources = 7, kObjRt = 8, kFrameRt = 9, kSoundRt = 10,
};

struct Section { uint32_t type = 0, offset = 0, size = 0; };

class Bundle {
public:
    bool open(const std::string& path, std::string* error);

    const std::vector<uint8_t>& bytes() const { return bytes_; }
    const Section* find(uint32_t type) const;
    const std::vector<std::string>& strings() const { return strings_; }
    const std::string* string_at(uint32_t index) const;

    uint32_t frame_count() const;
    std::string frame_name(uint32_t i) const;
    uint32_t frame_instance_count(uint32_t i) const;
    uint32_t object_count() const;
    uint32_t image_count() const;
    uint32_t resource_count() const;

    // Generic helpers for record sections (count u32 + offset table + records).
    uint32_t count(uint32_t section_type) const;
    uint32_t record_offset(uint32_t section_type, uint32_t index) const;

private:
    template <typename T> T read(uint32_t at) const;
    bool parse_strings(std::string* error);
    uint32_t frame_record(uint32_t i) const;
    uint32_t section_count_field(uint32_t type) const;

    std::vector<uint8_t> bytes_;
    std::vector<Section> sections_;
    std::vector<std::string> strings_;
};

} // namespace ucn::data
