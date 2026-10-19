// The event program: per-frame preparation, the selection list, group evaluation and action execution, the
// expression evaluator, and the conditions and actions this program uses. Ported from build/runtime_pretty.js
// (N.prototype from line 8003; the condition/action/expression classes per docs/runtime-map.md).
#include <algorithm>
#include <cctype>
#include <cmath>

#include "game/run.h"

namespace ucn::game {

namespace {
constexpr int32_t kSign = INT32_MIN;
// group flags G.*
constexpr int32_t kGOnce = 1, kGNotAlways = 2, kGRepeat = 4, kGRestrict = 8, kGShuffle = 16, kGOr = 1024,
                  kGCx = 2048, kGOrLogical = 4096, kGInactive = 16384, kGAx = 2 + 4 + 8 + 16, kGJp = 256 + 8192;
// item flags
constexpr int32_t kQTs = 32, kQzx = 128, kQip = 8, kQjl = 16, kRji = 1;
constexpr int32_t kQkE = 32 + 1 + 4 + 8 + 16; // Q.kE
// sprite flags
constexpr int32_t kRHidden = 1, kRtl = 16, kREg = 32, kRTj = 64, kRNs = 4096, kRmx = 4095;
constexpr int32_t kIQd = 256, kILp = 4096, kIJp = 512, kIKp = 1024;
constexpr int32_t kZSprite = 512, kZAt = 4096, kZMovements = 16, kZAnimations = 32, kZQj = 16384, kZHy = 32768;
constexpr int32_t kTbNx = 32;
constexpr int32_t kSysTypes = 7, kSysListSize = 64, kObjListSize = 81 + 64; // event-list widths (any >= used nums)
constexpr int32_t kMaxStack = 128;     // h.jt

// Token codes (ba.*)
constexpr int32_t kTokEnd = 0, kTokInt = 65535, kTokDouble = 1572863, kTokString = 262143, kTokOpen = -1,
                  kTokClose = -65537, kTokRandom = 131071, kTokStr = 327679, kTokPlus = 131072, kTokMinus = 262144,
                  kTokMul = 393216, kTokDiv = 524288, kTokAltValue = 1048578, kTokXScale = 5308418,
                  kTokCounter = 5242887, kTokIniValue = 5373984;

bool compare(double a, double b, int32_t op) { // h.iq / h.hq
    switch (op) {
    case 0: return a == b;
    case 1: return a != b;
    case 2: return a <= b;
    case 3: return a < b;
    case 4: return a >= b;
    case 5: return a > b;
    }
    return false;
}
bool compare(const Value& a, const Value& b, int32_t op) {
    if (a.is_str && b.is_str) {
        switch (op) {
        case 0: return a.str == b.str;
        case 1: return a.str != b.str;
        case 2: return a.str <= b.str;
        case 3: return a.str < b.str;
        case 4: return a.str >= b.str;
        case 5: return a.str > b.str;
        }
        return false;
    }
    return compare(a.as_num(), b.as_num(), op);
}
double trunc0(double a) { return a < 0 ? std::ceil(a) : std::floor(a); } // n.Dd
std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower(uint8_t(c))); return s; }
double parse_int(const std::string& s) { // parseInt(s, 10), NaN -> 0 as the Ini expression does
    size_t i = s.find_first_not_of(" \t\r\n");
    if (i == std::string::npos) return 0;
    bool neg = false;
    if (s[i] == '+' || s[i] == '-') { neg = s[i] == '-'; i++; }
    if (i >= s.size() || !std::isdigit(uint8_t(s[i]))) return 0;
    double v = 0;
    while (i < s.size() && std::isdigit(uint8_t(s[i]))) v = v * 10 + (s[i++] - '0');
    return neg ? -v : v;
}
} // namespace

// ---------------------------------------------------------------- preparation (jw 8673, $p 8707)

void Run::prepare_program() {
    const data::ProgramFrame& pf = program_.frames[size_t(frame_)];
    groups_.clear(); items_.clear(); params_.clear();
    values_ = program_.values;
    token_oil_.assign(program_.tokens.size(), -1);
    for (uint32_t g = 0; g < pf.group_count; g++) {
        const data::ProgramGroup& src = program_.groups[pf.first_group + g];
        Group grp;
        grp.conditions = src.conditions; grp.actions = src.actions;
        grp.flags = src.flags; grp.hi = src.hi; grp.hl = src.hl;
        grp.first_item = uint32_t(items_.size());
        for (uint32_t k = 0; k < uint32_t(src.conditions + src.actions); k++) {
            const data::ProgramItem& si = program_.items[src.first_item + k];
            Item it;
            it.code = si.code; it.oi = si.oi; it.oil = si.oil; it.ma = si.ma; it.qe = si.qe;
            it.first_param = uint32_t(params_.size());
            it.param_count = si.param_count;
            for (uint32_t p = 0; p < si.param_count; p++) {
                const data::ProgramParam& sp = program_.params[si.first_param + p];
                Param pr;
                pr.code = sp.code; pr.first_value = sp.first_value; pr.value_count = sp.value_count;
                pr.first_token = sp.first_token; pr.token_count = sp.token_count;
                params_.push_back(pr);
            }
            items_.push_back(it);
        }
        groups_.push_back(grp);
    }

    // jw: group and parameter state at frame start
    for (auto& g : groups_) {
        g.flags &= kGJp;
        g.hi = 0; g.hl = 0;
        for (uint32_t e = 0; e < uint32_t(g.conditions + g.actions); e++) {
            Item& b = items_[g.first_item + e];
            b.ma = b.code < 0 ? (b.ma & kQkE) : (b.ma & ~(kRji | kQjl));
            for (int f = 0; f < b.param_count; f++) {
                Param& c = params_[b.first_param + uint32_t(f)];
                if (c.code == 25) values_[c.first_value + 1] = 0; // Ds
            }
        }
    }

    // $p
    mk_ = 0;
    for (auto& o : oil_) { o.act_gen = -1; o.col_flags = 0; }
    for (auto& b : groups_) {
        for (uint32_t k = 0; k < uint32_t(b.conditions + b.actions); k++) {
            Item& c = items_[b.first_item + k];
            c.ma &= ~kQzx;
            if (c.type() >= 0) c.oil = oil_of(c.oi);
            for (int m = 0; m < c.param_count; m++) {
                Param& d = params_[c.first_param + uint32_t(m)];
                switch (d.code) {
                case 25: values_[d.first_value] = 0; break;
                case 9: case 16: case 18: {
                    const int32_t f = int32_t(values_[d.first_value]); // qo
                    if (f != -1) d.oil = oil_of(f);
                    break;
                }
                case 1: d.oil = oil_of(int32_t(values_[d.first_value])); break; // Qh
                case 15: case 27: case 28: case 45: case 46: case 22: case 23: case 52: case 59: case 53: case 62:
                case 54:
                    for (uint32_t t = 0; t < d.token_count; t++) {
                        const data::ProgramToken& tk = program_.tokens[d.first_token + t];
                        if (int16_t(uint32_t(tk.code) & 0xFFFF) > 0) token_oil_[d.first_token + t] = oil_of(tk.oi);
                    }
                    break;
                }
            }
        }
        // group flags and the collision flags of the OIs (Jd)
        int32_t m = 0, q = kGOnce | kGAx | kGCx;
        for (uint32_t k = 0; k < uint32_t(b.conditions + b.actions); k++) {
            Item& c = items_[b.first_item + k];
            const int32_t e = c.type();
            int32_t l2 = 0, h2 = 0, g = 0, d_oil = -1;
            auto sprites_only = [&](int32_t type) { // Sq
                for (auto& o : oil_)
                    if (o.oi != -1 && o.type == type) {
                        if ((o.oc_flags & kZSprite) && !(o.oc_flags & kZAt)) break;
                        return false;
                    }
                return true;
            };
            if (e >= 2) {
                switch (int32_t(uint32_t(c.code) & 0xFFFF0000u)) {
                case 262144: case 589824: m |= kGCx; break;  // collision-stop actions (none here)
                case 1638400: m |= kGShuffle; break;
                case -262144: { // overlap condition
                    const Param& p0 = param(c, 0);
                    l2 = sprites_only(e) ? kIQd : kIQd | kILp;
                    const int32_t gt = int32_t(values_[p0.first_value + 1]);
                    h2 = sprites_only(gt) ? kIQd : kIQd | kILp;
                    d_oil = p0.oil;
                    g = 3;
                    break;
                }
                case -720896: case -786432: h2 = kIKp; g = 1; break;
                case -851968: h2 = kIJp; g = 1; break;
                }
            } else {
                switch (c.code) {
                case -327681: q &= ~kGOnce; break;
                case -393217: q |= kGRestrict; break;
                case -262145: q |= kGRestrict; break;
                case -196609: q |= kGNotAlways + kGRepeat; break;
                case -196614: l2 = kIQd; d_oil = param(c, 0).oil; g = 2; break;
                case -393222: l2 = kIQd; d_oil = param(c, 1).oil; g = 2; break;
                }
            }
            if ((g & 1) && c.oil >= 0 && c.oil < int32_t(oil_.size())) oil_[size_t(c.oil)].col_flags |= h2;
            if ((g & 2) && d_oil >= 0 && d_oil < int32_t(oil_.size())) oil_[size_t(d_oil)].col_flags |= l2;
        }
        b.flags &= ~q;
        b.flags |= m;
    }

    // event list bases: system types first (index 0..6 for types -7..-1), then one block per OI
    const int32_t n_oil = int32_t(oil_.size());
    std::vector<int32_t> base(size_t(kSysTypes + n_oil));
    int32_t k = 0;
    for (int32_t b = 0; b < kSysTypes; b++) { base[size_t(b)] = k; k += kSysListSize; }
    for (int32_t r = 0; r < n_oil; r++) { base[size_t(kSysTypes + r)] = k; k += kObjListSize; }
    const int32_t h2 = k;
    ec_.assign(size_t(h2), 0);
    int32_t q = 0;
    auto list_index = [&](const Item& c, int32_t oil_index) {
        const int32_t e = c.type();
        const int32_t g = -int32_t(c.num());
        return (e < 0 ? base[size_t(kSysTypes + e)] : base[size_t(kSysTypes + oil_index)]) + g;
    };
    for (auto& b : groups_) {
        b.flags &= ~kGOr;
        bool d = true;
        int32_t l2 = 0;
        for (uint32_t kk = 0; kk < b.conditions; kk++) {
            Item& c = items_[b.first_item + kk];
            if (d) {
                if (c.ma & kQTs) q++;
                if (c.type() < 0) ec_[size_t(list_index(c, 0))]++;
                else if (c.oil >= 0 && c.oil < n_oil) ec_[size_t(list_index(c, c.oil))]++;
            }
            d = false;
            if (c.code == -1507329 || c.code == -1572865) {
                d = true;
                b.flags |= kGOr;
                if (l2 == 0) l2 = c.code; else c.code = l2;
                if (l2 == -1572865) b.flags |= kGOrLogical;
            }
        }
    }
    int32_t c = q + 1;
    for (int32_t b = 0; b < h2; b++)
        if (ec_[size_t(b)] != 0) { const int32_t n = ec_[size_t(b)]; ec_[size_t(b)] = c; c += n + 1; }
    rc_.assign(size_t(c), -1);
    kg_.assign(size_t(c), 0);
    std::vector<int32_t> fill(ec_);
    int32_t h3 = 0;
    for (int32_t gi = 0; gi < int32_t(groups_.size()); gi++) {
        Group& b = groups_[size_t(gi)];
        bool d = true;
        for (uint32_t kk = 0; kk < b.conditions; kk++) {
            Item& it = items_[b.first_item + kk];
            if (d) {
                if (it.ma & kQTs) { rc_[size_t(h3)] = gi; kg_[size_t(h3)] = int32_t(kk); h3++; }
                int32_t t2 = -1;
                if (it.type() < 0) t2 = list_index(it, 0);
                else if (it.oil >= 0 && it.oil < n_oil) t2 = list_index(it, it.oil);
                if (t2 >= 0) { rc_[size_t(fill[size_t(t2)])] = gi; kg_[size_t(fill[size_t(t2)])] = int32_t(kk); fill[size_t(t2)]++; }
            }
            d = it.code == -1507329 || it.code == -1572865;
        }
    }
    for (int32_t r = 0; r < n_oil; r++) { oil_[size_t(r)].event_base = base[size_t(kSysTypes + r)]; oil_[size_t(r)].wv = 0; }
    ge_[0] = 0;
    for (int32_t g = 1; g <= kSysTypes; g++) ge_[g] = base[size_t(kSysTypes - g)];
    for (auto& v2 : oil_) {
        for (int32_t e = v2.first; !none(e);) {
            Instance* a = slots_[size_t(e)];
            a->event_base = v2.event_base;
            a->col_flags = v2.col_flags;
            if (a->has_move) a->mv.pw = v2.wv;
            if ((a->oc_flags & kZSprite) && !(a->col_flags & kIQd)) a->sp.flags &= ~kRTj;
            if (!(a->oc_flags & kZHy)) {
                a->oc_flags &= ~kZQj;
                if ((a->col_flags & kIJp) && (fd_->flags & kTbNx)) a->oc_flags |= kZQj;
                if (a->col_flags & (kIQd | kIKp)) a->oc_flags |= kZQj;
            }
            e = a->next_same;
        }
    }
    has_always_ = q != 0;
}

// ---------------------------------------------------------------- selection (8004..8228)

Instance* Run::sel_first(int32_t a) { // vf
    hc_ = 0;
    if (a < 0 || a >= int32_t(oil_.size())) return nullptr; // 32767 qualifier "none" / unresolved
    OiEntry& c = oil_[size_t(a)];
    Instance* b;
    if (c.gen == wb_) {
        if (none(c.sel_first)) return nullptr;
        b = slots_[size_t(c.sel_first)];
        bi_ = -1; ok_ = a; ff_ = b->slot; co_ = a;
    } else {
        c.gen = wb_;
        if (kg_false_) { c.sel_first = -1; c.sel_count = 0; return nullptr; }
        c.sel_first = c.first;
        if (none(c.first)) { c.sel_count = 0; return nullptr; }
        int32_t d = c.first;
        do { b = slots_[size_t(d)]; d = b->next_same; b->sel_next = d; } while (!none(d));
        b = slots_[size_t(c.first)];
        bi_ = -1; ok_ = a; ff_ = b->slot; co_ = a;
        c.sel_count = c.count;
    }
    hc_ = c.sel_count;
    return b;
}

Instance* Run::sel_next() { // Re
    if (ff_ < 0) {
        if (co_ >= 0) {
            OiEntry& b = oil_[size_t(co_)];
            if (!none(b.sel_first)) { bi_ = -1; ok_ = co_; ff_ = b.sel_first; return slots_[size_t(ff_)]; }
        }
        return nullptr;
    }
    Instance* a = slots_[size_t(ff_)];
    if (!none(a->sel_next)) { bi_ = a->slot; ok_ = -1; ff_ = a->sel_next; return slots_[size_t(ff_)]; }
    return nullptr; // no qualifier walk
}

void Run::sel_drop() { // Bd
    Instance* f = slots_[size_t(ff_)];
    --oil_[size_t(f->oil)].sel_count;
    if (bi_ >= 0) { slots_[size_t(bi_)]->sel_next = f->sel_next; ff_ = bi_; }
    else { oil_[size_t(ok_)].sel_first = f->sel_next; ff_ = -1; }
}

void Run::sel_add(Instance* a) { // Il
    OiEntry& b = oil_[size_t(a->oil)];
    if (b.gen != wb_) { b.gen = wb_; b.sel_first = a->slot; b.sel_count = 1; a->sel_next = -1; return; }
    int32_t c = b.sel_first;
    if (none(c)) { b.sel_first = a->slot; b.sel_count += 1; a->sel_next = -1; return; }
    Instance* last = nullptr;
    do {
        if (a->slot == c) return;
        last = slots_[size_t(c)];
        c = last->sel_next;
    } while (!none(c));
    last->sel_next = a->slot;
    a->sel_next = -1;
    b.sel_count += 1;
}

void Run::sel_pick(int32_t a, Instance* b) { // sH (one OI, no qualifiers)
    OiEntry& o = oil_[size_t(a)];
    o.gen = wb_; o.sel_first = -1; o.sel_count = 0;
    b->sel_next = -1;
    OiEntry& f = oil_[size_t(b->oil)];
    f.sel_first = b->slot; f.sel_count = 1; f.gen = wb_;
}

Instance* Run::sel_nth(int32_t a, int32_t n) { // dz + cz: the n-th live instance (1-based) of the selection
    n++;
    hc_ = 0;
    if (a < 0 || a >= int32_t(oil_.size())) return nullptr;
    OiEntry& c = oil_[size_t(a)];
    const bool walk_sel = c.gen == wb_;
    if (!walk_sel && kg_false_) { hc_ = 0; return nullptr; }
    for (int32_t s = walk_sel ? c.sel_first : c.first; !none(s);) {
        Instance* i = slots_[size_t(s)];
        if (!i) break;
        if (!(i->flags & 1)) { hc_++; if (hc_ == n) return i; } // F.Me
        s = walk_sel ? i->sel_next : i->next_same;
    }
    return nullptr;
}

Instance* Run::expr_object(int32_t a) { // pk
    if (nk_) { pm_ = false; return current_object(a); }
    if (a < 0 || a >= int32_t(oil_.size())) return nullptr;
    OiEntry& o = oil_[size_t(a)];
    if (o.gen == wb_ && !none(o.sel_first)) return slots_[size_t(o.sel_first)];
    return none(o.first) ? nullptr : slots_[size_t(o.first)];
}

Instance* Run::action_object(Item& it) { // ab
    it.ma &= ~kQjl;
    pm_ = true;
    Instance* b = current_object(it.oil);
    if (b) { if (ac_) { it.ma |= kRji; kj_ = true; } return b; }
    it.ma |= kQjl;
    return nullptr;
}

Instance* Run::current_object(int32_t a) { // Jq -> MH 8166
    if (a < 0 || a >= int32_t(oil_.size())) return nullptr;
    OiEntry& b = oil_[size_t(a)];
    if (b.act_gen != mk_) {
        b.act_gen = mk_;
        b.act_loop = ai_;
        if (b.gen == wb_ && !none(b.sel_first)) {
            b.act_cur = b.sel_first;
            Instance* i = slots_[size_t(b.sel_first)];
            b.act_next = i->sel_next;
            if (none(i->sel_next)) { b.act_more = false; b.act_state = 1; ac_ = false; return i; }
            b.act_more = true; b.act_state = 2; ac_ = true;
            return i;
        }
        if (pm_ && b.gen == wb_) { b.act_state = 0; b.act_cur = -1; return nullptr; }
        if (!none(b.first)) {
            b.act_cur = b.first;
            Instance* i = slots_[size_t(b.first)];
            if (!i) { b.act_state = 0; b.act_cur = -1; return nullptr; }
            if (!none(i->next_same)) { b.act_next = i->next_same; b.act_more = true; b.act_state = 3; ac_ = true; return i; }
            b.act_more = false; b.act_state = 1; ac_ = false;
            return i;
        }
        b.act_state = 0; b.act_cur = -1;
        return nullptr;
    }
    if (b.act_loop != ai_) {
        b.act_loop = ai_;
        switch (b.act_state) {
        case 0: ac_ = b.act_more; return nullptr;
        case 1: ac_ = b.act_more; return slots_[size_t(b.act_cur)];
        case 2: {
            b.act_cur = b.act_next;
            if (none(b.act_next)) return nullptr;
            Instance* i = slots_[size_t(b.act_next)];
            if (!i) return nullptr;
            int32_t c = i->sel_next;
            if (none(c)) { b.act_more = false; c = b.sel_first; }
            b.act_next = c;
            ac_ = b.act_more;
            return i;
        }
        case 3: {
            b.act_cur = b.act_next;
            if (none(b.act_next)) return nullptr;
            Instance* i = slots_[size_t(b.act_next)];
            if (!i) return nullptr;
            int32_t c = i->next_same;
            if (none(c)) { b.act_more = false; c = b.first; }
            b.act_next = c;
            ac_ = b.act_more;
            return i;
        }
        }
    }
    if (b.act_cur < 0) return nullptr;
    ac_ = b.act_more;
    return slots_[size_t(b.act_cur)];
}

// ---------------------------------------------------------------- group evaluation

void Run::run_list(int32_t a, Instance* trigger) { // sf 8410
    pc_flag_ = false;
    if (a < 0 || a >= int32_t(rc_.size()) || rc_[size_t(a)] < 0) return;
    do {
        const int32_t gi = rc_[size_t(a)];
        Group& d = groups_[size_t(gi)];
        if (!(d.flags & kGInactive)) {
            lg_ = gi;
            if (!(d.flags & kGOr)) {
                wb_ += 1;
                kg_false_ = false;
                uint32_t e = 0;
                if (eval_condition(items_[d.first_item], trigger, true))
                    for (e++; e < d.conditions && eval_condition(items_[d.first_item + e], nullptr, false); e++) {}
                if (e == d.conditions) {
                    if (pc_flag_) { /* wG: trigger-only actions; no condition in this program sets pC */ }
                    else run_actions(gi);
                }
                a++;
            } else {
                // OR groups (none in this program; kept as the runtime's normal-path fallthrough)
                a++;
            }
        } else {
            a++;
            while (a < int32_t(rc_.size()) && rc_[size_t(a)] == gi) a++;
        }
    } while (a < int32_t(rc_.size()) && rc_[size_t(a)] >= 0);
}

void Run::run_actions(int32_t gi) { // My 8470
    Group& a = groups_[size_t(gi)];
    if (a.flags & kGAx) {
        if (a.flags & kGNotAlways) {
            const int32_t b = lb_, c = a.hi;
            a.hi = b;
            if (b == c || b - 1 == c) return;
        }
        if (a.flags & kGRepeat) {
            if (a.hl != 0) a.hl--;
            else return;
        }
        if (a.flags & kGRestrict) {
            const double b = rf_ / 10;
            const int32_t c = a.hl;
            if (c != 0 && b < c) return;
            a.hl = int32_t(b + a.hi);
        }
    }
    mk_++;
    kj_ = false;
    ai_ = 0;
    nk_ = true;
    for (uint32_t b = 0; b < a.actions; b++) {
        Item& c = items_[a.first_item + a.conditions + b];
        if (!(c.ma & (kQzx | kQip))) { c.ma &= ~kRji; exec_action(c); }
    }
    while (kj_) {
        kj_ = false;
        ai_++;
        for (uint32_t b = 0; b < a.actions; b++) {
            Item& c = items_[a.first_item + a.conditions + b];
            if (c.ma & kRji) { c.ma &= ~kRji; exec_action(c); }
        }
    }
    nk_ = false;
}

void Run::fire(int32_t a) { // ye
    int32_t b = a & 65535;
    if (b & 32768) b = 65536 - b;
    if (b < 1 || b > kSysTypes) return;
    const int32_t idx = ge_[b] + -(a >> 16);
    if (idx < 0 || idx >= int32_t(ec_.size())) return;
    const int32_t c = ec_[size_t(idx)];
    if (c != 0) run_list(c, nullptr);
}

bool Run::fire_for(Instance* a, int32_t b) { // nd
    fs_ = b;
    const int32_t idx = a->event_base + -(b >> 16);
    if (idx < 0 || idx >= int32_t(ec_.size())) return false;
    const int32_t c = ec_[size_t(idx)];
    if (c == 0) return false;
    run_list(c, a);
    return true;
}

void Run::special_lists() { // LG 8373 (not paused)
    int32_t a = ec_[size_t(ge_[4] + 3)];
    if (a != 0) run_list(a, nullptr);
    a = ec_[size_t(ge_[3] + 1)];
    if (a != 0) { run_list(a, nullptr); ec_[size_t(ge_[3] + 1)] = 0; }
    a = ec_[size_t(ge_[4] + 2)];
    if (a != 0) run_list(a, nullptr);
    a = ec_[size_t(ge_[4] + 1)];
    if (a != 0) run_list(a, nullptr);
}

void Run::run_queue() { // WH
    auto q = std::move(queue_);
    queue_.clear();
    for (auto& e : q) { pc_ = e.second; fire(e.first); }
}

// ---------------------------------------------------------------- expressions (6944.., 4432..)

// Db / Eb (2885): a colour constant (0xRRGGBB), or an expression (code 22) given as 0xBBGGRR (n.YC swaps it)
int32_t Run::counter_colour(const Param& p) {
    if (p.code != 22) return int32_t(values_[p.first_value]);
    const uint32_t a = uint32_t(int32_t(eval_param(p).as_num()));
    return int32_t((a & 255) << 16 | (a >> 8 & 255) << 8 | (a >> 16 & 255));
}

double& Run::pval(const Item& it, int p, int field) {
    return values_[params_[it.first_param + uint32_t(p)].first_value + uint32_t(field)];
}

Value Run::eval_param(const Param& p) { // Ta / ng / Iu / OH
    rj_ = p.first_token;
    cd_ = 0;
    wf_ = false;
    return get_expression();
}

Value Run::get_expression() { // getExpression 6973
    if (ba_.size() < size_t(kMaxStack)) { ba_.resize(size_t(kMaxStack)); bm_.resize(size_t(kMaxStack)); }
    const int32_t b = fa_;
    bm_[size_t(fa_)] = -1; // `as`: an end token (code 0)
    auto code_of = [&](int32_t tok) { return tok < 0 ? 0 : program_.tokens[size_t(tok)].code; };
    do {
        fa_++;
        hn_ = true;
        eval_token(rj_ + uint32_t(cd_));
        hn_ = false;
        cd_++;
        for (;;) {
            const int32_t tok = int32_t(rj_) + cd_;
            const int32_t code = program_.tokens[size_t(tok)].code;
            if (0 < code && code < 1310720) {
                if (code > code_of(bm_[size_t(fa_ - 1)])) {
                    bm_[size_t(fa_)] = tok;
                    cd_++;
                    fa_++;
                    hn_ = true;
                    eval_token(rj_ + uint32_t(cd_));
                    hn_ = false;
                    cd_++;
                } else {
                    fa_--;
                    eval_token(uint32_t(bm_[size_t(fa_)]));
                }
            } else {
                fa_--;
                if (fa_ == b) break;
                eval_token(uint32_t(bm_[size_t(fa_)]));
            }
        }
    } while (fa_ > b + 1);
    return ba_[size_t(b + 1)];
}

void Run::eval_token(uint32_t t) {
    const data::ProgramToken& tk = program_.tokens[t];
    Value& top = ba_[size_t(fa_)];
    switch (tk.code) {
    case kTokEnd: top = Value(0.0); break;                              // va
    case kTokInt: top = Value(tk.value); break;                         // bb
    case kTokDouble: top = Value(tk.value); wf_ = true; break;          // Ic
    case kTokString: top = Value(defs_.str(int32_t(tk.value))); break;  // Nc
    case kTokOpen: cd_++; { Value v = get_expression(); ba_[size_t(fa_)] = v; } break; // Wc
    case kTokClose: break;                                              // Xc
    case kTokRandom: {                                                  // Uc
        cd_++;
        wf_ = false;
        const double r = get_expression().as_num();
        const int32_t n = int32_t(r < 0 ? std::ceil(r) : std::floor(r));
        ba_[size_t(fa_)] = Value(double(random(n)));
        break;
    }
    case kTokStr: {                                                     // Vc
        cd_++;
        Value v = get_expression();
        ba_[size_t(fa_)] = Value(v.as_str());
        break;
    }
    case kTokPlus: ba_[size_t(fa_)] += ba_[size_t(fa_ + 1)]; break;     // Qc
    case kTokMinus:                                                     // Rc
        if (hn_) {
            cd_++;
            eval_token(rj_ + uint32_t(cd_));
            ba_[size_t(fa_)] = Value(-ba_[size_t(fa_)].as_num());
        } else ba_[size_t(fa_)] = Value(ba_[size_t(fa_)].as_num() - ba_[size_t(fa_ + 1)].as_num());
        break;
    case kTokMul: ba_[size_t(fa_)] = Value(ba_[size_t(fa_)].as_num() * ba_[size_t(fa_ + 1)].as_num()); break;
    case kTokDiv: {                                                     // Tc
        const double b = ba_[size_t(fa_)].as_num(), c = ba_[size_t(fa_ + 1)].as_num();
        ba_[size_t(fa_)] = Value(c != 0 ? (!wf_ ? trunc0(b / c) : b / c) : 0.0);
        break;
    }
    case kTokAltValue: {                                                // Jc
        Instance* b = expr_object(token_oil_[t]);
        if (!b) { top = Value(0.0); break; }
        const double v = b->has_values ? (tk.aux < int32_t(b->values.size()) ? b->values[size_t(tk.aux)] : 0) : 0;
        if (std::ceil(v) != v) wf_ = true;
        ba_[size_t(fa_)] = Value(v);
        break;
    }
    case kTokXScale: {                                                  // Yc
        Instance* b = expr_object(token_oil_[t]);
        if (!b) { top = Value(0.0); break; }
        ba_[size_t(fa_)] = Value(b->b.sx);
        wf_ = true;
        break;
    }
    case kTokCounter: {                                                 // Zc
        Instance* b = expr_object(token_oil_[t]);
        if (!b) { top = Value(0.0); break; }
        ba_[size_t(fa_)] = Value(b->counter);
        if (b->counter_float) wf_ = true;
        break;
    }
    case kTokIniValue: {                                                // ee -> Ka.KE: value of item <expr>
        Instance* b = expr_object(token_oil_[t]);
        if (!b) { top = Value(0.0); break; }
        const int32_t slot = fa_;
        cd_++;
        const std::string item = get_expression().as_str();
        const std::string& v = ini[lower(b->ini_file)][lower(b->ini_group)][lower(item)];
        ba_[size_t(slot)] = Value(parse_int(v));
        break;
    }
    default:
        top = Value(0.0); // an expression the program does not use
        break;
    }
}

// ---------------------------------------------------------------- conditions

bool Run::obj_filter_flag(Item& it, int which) { // B.uf with the jk of each class
    int32_t d;
    Instance* c = sel_first(it.oil);
    d = hc_;
    for (; c; c = sel_next()) {
        bool keep = true;
        switch (which) {
        case 0: keep = neg_result(it, !(c->sp.flags & kRHidden)); break;       // wc: visible
        case 1: keep = (c->sp.flags & kRHidden) != 0; break;                  // xc: invisible
        case 2: keep = neg_result(it, c->b.speed == 0); break;                // Dc: stopped
        case 3: keep = c->b.move_type == 5 && (c->path_end_tick == lb_ || c->path_end_tick == lb_ - 1) && c->path_end_tick != 0; break; // Ac
        }
        if (!keep) { d--; sel_drop(); }
    }
    return d != 0;
}

bool Run::eval_condition(Item& it, Instance* trigger, bool first) {
    const int32_t code = it.code;
    switch (code) {
    case -1: return true;                                                       // (-1,-1) always
    case -393217: {                                                             // (-1,-7) not always
        Group& a = groups_[size_t(lg_)];
        if (a.flags & kGNotAlways) return true;
        if (a.flags & kGRestrict) return false;
        a.hi = -2;
        a.flags |= kGNotAlways;
        return true;
    }
    case -327681: {                                                             // (-1,-6) once
        Group& a = groups_[size_t(lg_)];
        if (a.flags & kGOnce) return false;
        a.flags |= kGOnce;
        return true;
    }
    case -131073: {                                                             // (-1,-3) compare two values
        const Value b = eval_param(param(it, 0));
        const Value a = eval_param(param(it, 1));
        return compare(b, a, int32_t(pval(it, 1, 0)));
    }
    case -3: return !(2 < lb_);                                                 // (-3,-1) start of frame
    case -458756: {                                                             // (-4,-8) every
        const Param& p0 = param(it, 0);
        double& value = pval(it, 1, 0);
        double& ds = pval(it, 1, 1);
        auto period = [&] { return p0.code == 22 ? eval_param(p0).as_num() : values_[p0.first_value]; };
        if (ds == 0) { value = period(); ds = -1; }
        else if (value -= go_, value <= 0) { value += period(); return true; }
        return false;
    }
    case -393220: {                                                             // (-4,-7) timer reaches
        const Param& p0 = param(it, 0);
        const double b = p0.code == 22 ? eval_param(p0).as_num() : values_[p0.first_value];
        double& c = pval(it, 1, 0);
        if (rf_ >= b) {
            if (c == lb_) { c = lb_ + 1; return false; }
            c = lb_ + 1;
            return true;
        }
        return false;
    }
    case -65540: {                                                              // (-4,-2) timer <=
        const Param& p0 = param(it, 0);
        const double b = p0.code == 22 ? eval_param(p0).as_num() : values_[p0.first_value];
        return !(rf_ > b);
    }
    case -4: {                                                                  // (-4,-1) timer >
        const Param& p0 = param(it, 0);
        const double b = p0.code == 22 ? eval_param(p0).as_num() : values_[p0.first_value];
        return rf_ > b;
    }
    case -6: {                                                                  // (-6,-1) upon pressing key
        const int32_t key = int32_t(pval(it, 0, 0));
        if (!keys_[key & 255]) return negated(it);
        // B.KG: newly true this tick
        Group& g = groups_[size_t(lg_)];
        const int32_t c = g.hi;
        g.hi = lb_;
        const bool fresh = !(lb_ == c || lb_ - 1 == c);
        return fresh ? !negated(it) : negated(it);
    }
    case -65542: return neg_result(it, keys_[int32_t(pval(it, 0, 0)) & 255]);   // (-6,-2) key held
    case -262142: case -262137: return cond_overlap(it);                        // (2,-4) (7,-4) overlap
    case -196614: return cond_mouse_over(param(it, 0).oil, negated(it));        // (-6,-4) mouse over
    case -458758: {                                                             // (-6,-8) mouse button held (kc)
        const int32_t k = int32_t(pval(it, 0, 0));
        const int32_t key = k == 1 ? 200 : k == 2 ? 202 : k == 4 ? 201 : 0;    // l.Ne / l.cn / l.Tp
        return !keys_[key] ? negated(it) : !negated(it);
    }
    case -393222: {                                                             // (-6,-7) click on object (lc)
        const Param& p1 = param(it, 1);
        if (first) { // Ja: only ever first through po's fire(-393222) (Ma has no Q.Ts)
            if (pc_ != int32_t(pval(it, 0, 0))) return false;
            if (jw_ == int32_t(values_[p1.first_value])) { if (slots_[size_t(hw_)]) sel_add(slots_[size_t(hw_)]); return true; }
            return false;
        }
        return tr_ != int32_t(pval(it, 0, 0)) ? false : cond_mouse_over(p1.oil, false);
    }
    case -262150:                                                               // (-6,-5) click (mc)
        return first ? int32_t(pval(it, 0, 0)) == pc_ : int32_t(pval(it, 0, 0)) == tr_;
    case -2: return sample_playing(int32_t(pval(it, 0, 0))) ? negated(it) : !negated(it); // (-2,-1) not playing
    case -131074: return any_playing() ? negated(it) : !negated(it);            // (-2,-3) no sample playing
    case -2752510: {                                                            // (2,-42) alterable value vs constant
        Instance* b = sel_first(it.oil);
        if (!b) return false;
        int32_t c = hc_;
        const int32_t d = int32_t(pval(it, 0, 0));
        const Param& e = param(it, 1);
        const double f = program_.tokens[e.first_token].value;
        const int32_t op = int32_t(values_[e.first_value]);
        do {
            if (d >= 0 && b->has_values) {
                const double v = d < int32_t(b->values.size()) ? b->values[size_t(d)] : 0;
                if (!compare(v, f, op)) { c--; sel_drop(); }
            } else { c--; sel_drop(); }
            b = sel_next();
        } while (b);
        return c != 0;
    }
    case -1769470: {                                                            // (2,-27) alterable value #expr vs expr
        Instance* b = sel_first(it.oil);
        if (!b) return false;
        int32_t c = hc_;
        const Param& p0 = param(it, 0);
        const Param& e = param(it, 1);
        do {
            const int32_t d = p0.code == 53 ? int32_t(eval_param(p0).as_num()) : int32_t(values_[p0.first_value]);
            if (d >= 0 && b->has_values) {
                const double v = d < int32_t(b->values.size()) ? b->values[size_t(d)] : 0;
                const Value rhs = eval_param(e);
                if (!compare(Value(v), rhs, int32_t(values_[e.first_value]))) { c--; sel_drop(); }
            } else { c--; sel_drop(); }
            b = sel_next();
        } while (b);
        return c != 0;
    }
    case -5308409: {                                                            // (7,-81) counter vs expr
        int32_t c = 0;
        Instance* b = sel_first(it.oil);
        c = hc_;
        const Param& p0 = param(it, 0);
        while (b) {
            const double v = b->counter;
            const Value d = eval_param(p0);
            if (!compare(Value(v), d, int32_t(values_[p0.first_value]))) { c--; sel_drop(); }
            b = sel_next();
        }
        return c != 0;
    }
    case -2228222: {                                                            // (2,-34) pick one at random
        sel_nth(it.oil, -1);
        if (hc_ == 0) return false;
        const int32_t r = random(hc_);
        Instance* b = sel_nth(it.oil, r);
        if (b) sel_pick(it.oil, b);
        return true;
    }
    case -2097150: {                                                            // (2,-32) number of objects
        const int32_t b = it.oil >= 0 && it.oil < int32_t(oil_.size()) ? oil_[size_t(it.oil)].count : 0;
        const Param& p0 = param(it, 0);
        const double a = eval_param(p0).as_num();
        return compare(double(b), a, int32_t(values_[p0.first_value]));
    }
    case -1900542: case -1900541: return obj_filter_flag(it, 0);                // (2/3,-29) visible
    case -1835006: return obj_filter_flag(it, 1);                               // (2,-28) invisible
    case -458750: return obj_filter_flag(it, 2);                                // (2,-7) stopped
    case -1376254:                                                              // (2,-21) end of path
        if (first && trigger) return true;                                      // Ac.Ja: true, no selection
        return obj_filter_flag(it, 3);
    case -1114110: case -1048574: case -65534: {                                // (2,-17) X, (2,-16) Y, (2,-1) frame: Gi
        Instance* c = sel_first(it.oil);
        int32_t d = hc_;
        const Param& e = param(it, 0);
        const int32_t op = int32_t(values_[e.first_value]);
        const data::ProgramToken& f0 = program_.tokens[e.first_token];
        const bool constant = (f0.code == kTokInt || f0.code == kTokDouble) && program_.tokens[e.first_token + 1].code == 0;
        for (; c; c = sel_next()) {
            const double f = constant ? f0.value : eval_param(e).as_num();
            const double v = code == -1114110 ? c->x : code == -1048574 ? c->y : double(c->an.frame);
            if (!compare(v, f, op)) { d--; sel_drop(); }
        }
        return d != 0;
    }
    case -196606: case -131070: {                                               // (2,-3) playing, (2,-2) over
        const Param& p0 = param(it, 0);
        const bool over = code == -131070;
        if (over && first && trigger) {
            const double v = p0.code == 10 ? values_[p0.first_value] : eval_param(p0).as_num();
            if (v != pc_) return false;
            sel_add(trigger);
            return true;
        }
        Instance* c = sel_first(it.oil);
        int32_t d = hc_;
        for (; c; c = sel_next()) {
            const double v = p0.code == 10 ? values_[p0.first_value] : eval_param(p0).as_num();
            bool keep;
            if (over) keep = v == c->an.anim && c->an.frame_count == 0;
            else keep = v != c->an.anim ? negated(it) : (c->an.frame_count != 0 ? !negated(it) : negated(it));
            if (!keep) { d--; sel_drop(); }
        }
        return d != 0;
    }
    default:
        unported[code]++; // a condition with no port: false
        return false;
    }
}

// ---------------------------------------------------------------- actions

void Run::exec_action(Item& it) {
    const int32_t code = it.code;
    if (it.type() < 0) {
        switch (code) {
        case 65533: zb_ = 1; break;                                             // (-3,0) next frame
        case 196605: {                                                          // (-3,2) jump to frame
            const Param& p0 = param(it, 0);
            int32_t b;
            if (p0.code == 26) b = int32_t(values_[p0.first_value]);
            else { b = int32_t(eval_param(p0).as_num()) - 1; if (b < 0 || b >= 4096) return; b |= 32768; }
            zb_ = 3;
            dm_ = b;
            break;
        }
        case 327677: break;                                                     // (-3,4) restart: zb only when yA
        case 524285: {                                                          // (-3,7) center display at position
            const Param& p0 = param(it, 0);
            const double* v = &values_[p0.first_value];
            double x, y;
            int32_t layer = -1;
            if (int32_t(v[0]) == -1) {
                x = v[2]; y = v[3];
                layer = int32_t(v[9]);
                if (layer > fd_->layer_count - 1) layer = fd_->layer_count - 1;
            } else {
                pm_ = false;
                Instance* d = current_object(p0.oil);
                if (!d) break;
                x = d->x + v[2]; y = d->y + v[3]; // zw (Hk flags OD/ND unused by these calls)
                layer = d->layer;
            }
            center_display(x, y, layer, 3);
            break;
        }
        case 131070: sound_stop_all(); break;                                   // (-2,1) stop all samples
        case 786430:                                                            // (-2,11) play sample on channel
            sound_play(int32_t(pval(it, 0, 0)), 1, int32_t(eval_param(param(it, 1)).as_num()) - 1);
            break;
        case 851966: {                                                          // (-2,12) ... with loops
            const int32_t ch = int32_t(eval_param(param(it, 1)).as_num());
            const int32_t loops = int32_t(eval_param(param(it, 2)).as_num());
            sound_play(int32_t(pval(it, 0, 0)), loops, ch - 1);
            break;
        }
        case 1179646: {                                                         // (-2,17) channel volume
            const int32_t ch = int32_t(eval_param(param(it, 0)).as_num()) - 1;
            double v = eval_param(param(it, 1)).as_num();
            if (v >= 0 && v <= 100 && ch >= 0 && ch < 32) { // rb: 0 <= c <= 100, then mK clamps
                channel_volume_[ch] = v;
                if (channel_[ch] >= 0) samples_[channel_[ch]].volume = v;
            }
            break;
        }
        case 1245182: break;                                                    // (-2,18) no-op (da)
        default: if (code != 65531 && code != 524285) unported[code]++; break;
        case 65531: {                                                           // (-5,0) create object
            const Param& p0 = param(it, 0);
            const double* v = &values_[p0.first_value];
            // zw(run, 17, c): position from the parameter (qo = -1: absolute)
            double x, y;
            int32_t dir = -1, layer = -1;
            const int32_t qo = int32_t(v[0]), hk = int32_t(v[1]);
            if (qo == -1) {
                if (!(hk & 8)) dir = random_dir(int32_t(v[6]));
                x = v[2]; y = v[3];
                layer = int32_t(v[9]);
                if (layer > fd_->layer_count - 1) layer = fd_->layer_count - 1;
                it.ma &= ~kRji;
            } else {
                pm_ = false;
                Instance* d = current_object(p0.oil);
                if (ac_) { it.ma |= kRji; kj_ = true; } else it.ma &= ~kRji;
                if (!d) return;
                x = d->x + v[2]; y = d->y + v[3];
                layer = d->layer;
                dir = (hk & 8) ? -1 : (hk & 4) ? d->b.dir : random_dir(int32_t(v[6]));
            }
            const int32_t slot = create(int32_t(v[10]), int32_t(v[11]), x, y, dir, 0, layer, -1);
            if (slot >= 0) sel_add(slots_[size_t(slot)]);
            break;
        }
        }
        return;
    }
    Instance* b = action_object(it);
    if (!b) return;
    switch (it.num()) {
    case 1: {                                                                   // set position
        const double* v = &values_[param(it, 0).first_value];
        const int32_t qo = int32_t(v[0]);
        double x, y;
        if (qo == -1) { x = v[2]; y = v[3]; }
        else {
            pm_ = false;
            Instance* d = current_object(param(it, 0).oil);
            if (!d) return;
            x = d->x + v[2]; y = d->y + v[3];
        }
        set_x(b, x); set_y(b, y);
        break;
    }
    case 2: set_x(b, std::floor(eval_param(param(it, 0)).as_num())); break;    // set X
    case 6: if (b->has_move && b->mv.type == 5) {                              // set speed (path Je)
            int32_t a = int32_t(eval_param(param(it, 0)).as_num());
            if (a < 0) a = 0;
            if (a > 250) a = 250;
            b->mv.speed = a; b->b.speed = a; b->mv.stop_flag = true;
        }
        break;
    case 13: {                                                                  // select movement
        const Param& p0 = param(it, 0);
        const int32_t a = p0.code == 22 ? int32_t(eval_param(p0).as_num()) : int32_t(values_[p0.first_value]);
        if (b->has_move && a >= 0 && a < int32_t(b->def->moves.size())) movement_init(b, a, b->def, -2, -1);
        break;
    }
    case 15: if (b->has_anim) b->an.stopped = true; break;
    case 16: if (b->has_anim) b->an.stopped = false; break;
    case 17: {                                                                  // change animation
        const Param& p0 = param(it, 0);
        const int32_t a = p0.code == 10 ? int32_t(values_[p0.first_value]) : int32_t(eval_param(p0).as_num());
        if (b->has_anim) anim_force(b, a);
        b->b.changed = true;
        break;
    }
    case 19: if (b->has_anim) anim_force_speed(b, int32_t(eval_param(param(it, 0)).as_num())); break;
    case 23: {                                                                  // set direction
        const Param& p0 = param(it, 0);
        const int32_t c = p0.code == 29 ? random_dir(int32_t(values_[p0.first_value])) : int32_t(eval_param(p0).as_num());
        set_dir(b, c);
        break;
    }
    case 24:                                                                    // destroy
        if (b->type == 3) {
            if (b->text_flags & 4) { sprite_hide(b); b->sp.flags &= ~kREg; b->flags |= 8192; }
            else { b->flags |= 1; request_destroy(b->slot); }
        } else if (!(b->flags & 1)) {
            b->flags |= 1;
            if ((b->oc_flags & kZAnimations) && anim_exists(b, 4)) { // uA: play the disappearing animation
                b->sp.flags &= ~kRTj; b->flags |= 8192;
                if (b->has_move) { b->mv.type = 0; b->b.move_type = 11; b->b.speed = 0; }
                anim_force(b, 4);
                if (b->an.loops == 0) b->an.loops = 1;
            } else if ((b->oc_flags & kZAnimations) || (b->oc_flags & kZSprite)) {
                b->handled = false; request_destroy(b->slot);
            } else { b->handled = false; request_destroy(b->slot); }
        }
        break;
    case 26: if (b->has_sprite) { sprite_hide(b); b->sp.flags &= ~kREg; b->sp.flash_interval = 0; } break; // hide
    case 27: if (b->has_sprite) { sprite_show(b); b->sp.flags |= kREg; b->sp.flash_interval = 0; } break;  // show
    case 28: if (b->has_sprite) {                                               // flash
            sprite_hide(b); b->sp.flags &= ~kREg;
            const Param& p0 = param(it, 0);
            b->sp.flash_interval = b->sp.flash_left = p0.code == 2 ? values_[p0.first_value] : eval_param(p0).as_num();
        }
        break;
    case 31: case 32: case 33: {                                                // alterable value set/add/sub
        const Param& p0 = param(it, 0);
        const int32_t c = p0.code == 53 ? int32_t(eval_param(p0).as_num()) : int32_t(values_[p0.first_value]);
        if (c >= 0 && b->has_values) {
            if (c >= int32_t(b->values.size())) b->values.resize(size_t(c + 10), 0.0);
            const double a = eval_param(param(it, 1)).as_num();
            double& v = b->values[size_t(c)];
            if (it.num() == 31) v = a; else if (it.num() == 32) v += a; else v -= a;
        }
        break;
    }
    case 40: if (b->has_anim) { anim_force_frame(b, int32_t(eval_param(param(it, 0)).as_num())); b->b.changed = true; } break;
    case 58: if (b->has_sprite) display_move(b, 0); break;                      // be(0)
    case 61: {                                                                  // set layer
        const int32_t c = int32_t(eval_param(param(it, 0)).as_num());
        if (b->has_sprite && c > 0 && c <= fd_->layer_count && b->layer != c - 1) {
            display_remove(b);
            b->layer = c - 1; b->sp.layer = c - 1;
            display_add(b, -1); // D.hz(false)
        }
        break;
    }
    case 63: break;                                                             // ink effect by name: render only
    case 65: if (b->has_sprite) {                                               // alpha coefficient
            double a = 255 - eval_param(param(it, 0)).as_num();
            a = std::min(std::max(a, 0.0), 255.0);
            const bool c = !(b->sp.ink & kRNs);
            b->sp.ink = (b->sp.ink & kRmx) | kRNs;
            int32_t d = 16777215;
            if (!c) d = b->sp.ink_param;
            b->sp.ink_param = int32_t(uint32_t(int32_t(a)) << 24) | (d & 16777215);
        }
        break;
    case 85: {                                                                  // set scale
        if (b->type == 7) { b->color1 = counter_colour(param(it, 0)); counter_layout(b); break; } // (7,85) Db: colour 1
        double c = eval_param(param(it, 0)).as_num();
        if (c < 0) c = 0;
        const bool d = eval_param(param(it, 1)).as_num() != 0;
        b->sp.flags &= ~kRtl;
        if (d) b->sp.flags |= kRtl;
        if (b->b.sx != c || b->b.sy != c) { // F.Rw
            if (c >= 0) b->b.sx = c, b->b.sy = c;
            b->b.changed = true;
            set_image_metrics(b, b->b.image);
        }
        break;
    }
    case 88:
        if (b->type == 3) {                                                     // (3,88) set text
            const std::string a = eval_param(param(it, 0)).as_str();
            if (!b->has_text || a != b->text) { b->has_text = true; b->text = a; }
        } else {                                                                // (2,88) set angle
            double c = std::fmod(eval_param(param(it, 0)).as_num(), 360);
            if (c < 0) c += 360;
            const bool d = eval_param(param(it, 1)).as_num() != 0;
            const bool a = (b->sp.flags & kRtl) != 0;
            if (b->b.angle != c || a != d) {
                b->b.angle = c;
                b->sp.flags &= ~kRtl;
                if (d) b->sp.flags |= kRtl;
                b->b.changed = true;
                set_image_metrics(b, b->b.image);
            }
        }
        break;
    case 80:
        if (b->type == 7) { const double a = eval_param(param(it, 0)).as_num(); counter_note(b, a); counter_set(b, a); } // set counter
        else if (b->type >= 32) b->ini_group = eval_param(param(it, 0)).as_str(); // Ini: set group
        break;
    case 81: if (b->type == 7) { const double a = eval_param(param(it, 0)).as_num(); counter_note(b, a); counter_set(b, b->counter + a); } break;
    case 82: if (b->type == 7) { const double a = eval_param(param(it, 0)).as_num(); counter_note(b, a); counter_set(b, b->counter - a); } break;
    case 86:
        if (b->type >= 32) {                                                    // Ini: set file (n.QB strips the path)
            std::string f = eval_param(param(it, 0)).as_str();
            const size_t p = f.rfind('\\');
            if (p != std::string::npos && p > 0) f = f.substr(p + 1);
            b->ini_file = f;
        } else if (b->type == 7) { b->color2 = counter_colour(param(it, 0)); counter_layout(b); } // Eb: colour 2
        break;
    case 87: if (b->type >= 32) {                                               // Ini: set value of item
            const std::string item = eval_param(param(it, 0)).as_str();
            const std::string v = eval_param(param(it, 1)).as_str();
            ini[lower(b->ini_file)][lower(b->ini_group)][lower(item)] = v;
            b->ini_save = 10;
            ini_dirty = true;
        }
        break;
    case 93: if (b->type >= 32) {                                               // Ini: delete group
            ini[lower(b->ini_file)].erase(lower(eval_param(param(it, 0)).as_str()));
            b->ini_save = 10;
            ini_dirty = true;
        }
        break;
    default:
        unported[code]++;
        break;
    }
}

} // namespace ucn::game
