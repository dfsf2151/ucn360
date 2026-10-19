#include "data/program.h"

#include <cstring>

#include "data/bundle.h"
#include "data/reader.h"

namespace ucn::data {

bool Program::load(const Bundle& bundle, std::string* error) {
    auto fail = [&](const char* what) {
        if (error) *error = what;
        return false;
    };
    const Section* s = bundle.find(kProgram);
    if (!s) return fail("no PROGRAM section");
    Reader r{bundle.bytes(), s->offset};
    if (r.u32() != 2) return fail("PROGRAM is not v2");
    const uint32_t nf = r.u32(), ng = r.u32(), ni = r.u32(), np = r.u32(), nv = r.u32(), nt = r.u32();
    if (!r.ok) return fail("short PROGRAM header");

    frames.resize(nf);
    for (auto& f : frames) { f.first_group = r.u32(); f.group_count = r.u32(); }
    groups.resize(ng);
    for (auto& g : groups) {
        g.conditions = r.u8(); g.actions = r.u8(); g.flags = r.u16();
        g.hi = r.i32(); g.hl = r.i32(); g.first_item = r.u32();
    }
    items.resize(ni);
    for (auto& it : items) {
        it.code = r.i32(); it.oi = r.u16(); it.oil = r.u16();
        it.ma = r.u8(); it.qe = r.u8(); it.param_count = r.u8(); it.tn = r.u8();
        it.identifier = r.u16(); r.skip(2); it.first_param = r.u32();
    }
    params.resize(np);
    for (auto& p : params) {
        p.code = r.u16(); p.value_count = r.u8(); r.skip(1);
        p.first_value = r.u32(); p.first_token = r.u32(); p.token_count = r.u32();
    }
    auto f64 = [&] {
        uint64_t bits = uint64_t(r.u32());
        bits |= uint64_t(r.u32()) << 32;
        double d;
        std::memcpy(&d, &bits, sizeof d);
        return d;
    };
    values.resize(nv);
    for (auto& v : values) v = f64();
    tokens.resize(nt);
    for (auto& t : tokens) { t.code = r.i32(); t.oi = r.i32(); t.aux = r.i32(); t.oil = r.i32(); t.value = f64(); }
    if (!r.ok) return fail("short PROGRAM tables");
    if (r.p != s->offset + s->size) return fail("PROGRAM size mismatch");

    for (const auto& f : frames)
        if (f.first_group + f.group_count > ng) return fail("frame group run out of range");
    for (size_t i = 0; i < ng; i++) {
        const auto& g = groups[i];
        if (g.first_item + g.conditions + g.actions > ni) return fail("group item run out of range");
    }
    for (const auto& it : items)
        if (it.first_param + it.param_count > np) return fail("item param run out of range");
    for (const auto& p : params)
        if (p.first_value + p.value_count > nv || p.first_token + p.token_count > nt)
            return fail("param run out of range");
    return true;
}

} // namespace ucn::data
