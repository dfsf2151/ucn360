#pragma once
#include <cstdint>
#include <vector>

namespace ucn::data {

// Little-endian cursor over a byte buffer. `ok` goes false as soon as a read runs
// past the end; callers check it once at the end of a record.
struct Reader {
    const std::vector<uint8_t>& b;
    size_t p = 0;
    bool ok = true;

    bool has(size_t n) const { return ok && p + n <= b.size(); }
    uint8_t u8() { if (!has(1)) { ok = false; return 0; } return b[p++]; }
    uint16_t u16() {
        if (!has(2)) { ok = false; return 0; }
        const uint16_t v = uint16_t(b[p]) | uint16_t(b[p + 1]) << 8;
        p += 2;
        return v;
    }
    int16_t i16() { return int16_t(u16()); }
    uint32_t u32() {
        if (!has(4)) { ok = false; return 0; }
        const uint32_t v = uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 |
                           uint32_t(b[p + 3]) << 24;
        p += 4;
        return v;
    }
    int32_t i32() { return int32_t(u32()); }
    void skip(size_t n) { if (!has(n)) { ok = false; return; } p += n; }
};

} // namespace ucn::data
