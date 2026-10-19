#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ucn::data {

class Bundle;

// The typed PROGRAM section (bundle v2; layout in tools/compile_bundle.py w_program). Plain tables indexed by
// position; a frame owns a run of groups, a group a run of items (conditions first, then actions), an item a
// run of parameters, a parameter a run of values and, for expression parameters, a run of tokens. Field names
// in comments are the runtime's (docs/runtime-map.md).
struct ProgramGroup {
    uint8_t conditions = 0;  // Yb
    uint8_t actions = 0;     // xe
    uint16_t flags = 0;      // Ka as dumped
    int32_t hi = 0, hl = 0;  // Hi, Hl
    uint32_t first_item = 0;
};

struct ProgramItem {
    int32_t code = 0;        // ya: (num << 16) | (object type & 0xFFFF)
    uint16_t oi = 0;         // ke: object handle
    uint16_t oil = 0;        // ub: the live runtime's resolved OI-list index (cross-check only)
    uint8_t ma = 0, qe = 0;  // Ma, Qe flags
    uint8_t param_count = 0; // Ad
    uint8_t tn = 0;
    uint16_t identifier = 0; // mP (conditions)
    uint32_t first_param = 0;

    int16_t type() const { return int16_t(uint32_t(code) & 0xFFFF); }
    int16_t num() const { return int16_t(uint32_t(code) >> 16); }
};

struct ProgramParam {
    uint16_t code = 0;
    uint8_t value_count = 0;
    uint32_t first_value = 0;
    uint32_t first_token = 0, token_count = 0;
};

struct ProgramToken {
    int32_t code = 0;
    int32_t oi = -1;   // Qh
    int32_t aux = 0;   // Lh (alterable value index)
    int32_t oil = -1;  // Ub as the live runtime resolved it (cross-check only)
    double value = 0;  // integer / double constant, or string-table index for string constants
};

struct ProgramFrame { uint32_t first_group = 0, group_count = 0; };

struct Program {
    std::vector<ProgramFrame> frames;
    std::vector<ProgramGroup> groups;
    std::vector<ProgramItem> items;
    std::vector<ProgramParam> params;
    std::vector<double> values;
    std::vector<ProgramToken> tokens;

    // Reads the bundle's PROGRAM section and checks every run stays inside its table.
    bool load(const Bundle& bundle, std::string* error);
};

} // namespace ucn::data
