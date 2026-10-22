// Frame start and the per-tick loop (CRun: cI 6186, Mt 6140, iw 6293, hu 6334, tu 6724, vH 6762), plus the state
// record that tools/compare_state.py diffs against the live oracle (tools/oracle_capture.js).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

#include "data/bundle.h"
#include "data/reader.h"
#include "game/run.h"

namespace ucn::game {

namespace {
constexpr int32_t kText = 3, kQuestion = 4;
constexpr int32_t kViVisibleAtStart = 8;
constexpr int32_t kZJy = 8, kZGy = 131072;
constexpr int32_t kHText = 4, kHHidden = 2, kHvx = 8;
constexpr int32_t kLcCreating = 512;

void json_number(std::string& o, double d) {
    if (std::isnan(d) || std::isinf(d)) { o += "null"; return; }
    o += Value::number_string(d);
}
void json_string(std::string& o, const std::string& s) {
    o += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
            else o += char(c);
        }
    }
    o += '"';
}
} // namespace

Run::~Run() {
    for (Instance* i : slots_) delete i;
}

void Run::init_frame() { // Mt + Uu + iw
    for (Instance* i : slots_) delete i;
    slots_.assign(size_t(std::max(fd_->max_objects, 1)), nullptr);
    live_ = 0;
    dw_ = forced_seed_ >= 0 ? forced_seed_ : (fd_->seed != -1 ? fd_->seed : int32_t(clock_ms_ & 65535));
    tk_.assign(slots_.size() / 32 + 2, 0);
    nd_ = fd_->virtual_right == -1 ? 2147479552.0 : fd_->virtual_right;
    od_ = fd_->virtual_bottom == -1 ? 2147479552.0 : fd_->virtual_bottom;
    ea_ = ga_ = 0;
    oj_ = pj_ = frame_ni_ = frame_oi_ = 0;
    scroll_pending_ = false;
    layer_x_.assign(size_t(std::max(fd_->layer_count, 1)), 0);
    layer_y_.assign(layer_x_.size(), 0);
    zb_ = dm_ = 0;
    lc_ = 0;
    layer_counts_.assign(size_t(std::max(fd_->layer_count, 1)), 1); // iw: pb[c].Hv = 1
    disp_back_.assign(layer_counts_.size(), {});
    disp_main_.assign(layer_counts_.size(), {});

    // iw: the OI list. The runtime walks the app's loaded-object table, which for a frame holds every object the
    // frame instantiates or its events name, in handle order (checked against the dumped resolutions below).
    std::set<int32_t> ois;
    for (const auto& fi : fd_->instances) ois.insert(fi.object);
    const data::ProgramFrame& pf = program_.frames[size_t(frame_)];
    for (uint32_t g = 0; g < pf.group_count; g++) {
        const data::ProgramGroup& grp = program_.groups[pf.first_group + g];
        for (uint32_t k = 0; k < uint32_t(grp.conditions + grp.actions); k++) {
            const data::ProgramItem& it = program_.items[grp.first_item + k];
            if (it.type() >= 2) ois.insert(it.oi);
            for (uint32_t p = 0; p < it.param_count; p++) {
                const data::ProgramParam& pr = program_.params[it.first_param + p];
                if (pr.code == 1) ois.insert(int32_t(program_.values[pr.first_value]));
                if (pr.code == 9) ois.insert(int32_t(program_.values[pr.first_value + 11]));
                for (uint32_t t = 0; t < pr.token_count; t++) {
                    const data::ProgramToken& tk = program_.tokens[pr.first_token + t];
                    if (int16_t(uint32_t(tk.code) & 0xFFFF) > 0 && tk.oi >= 0) ois.insert(tk.oi);
                }
            }
        }
    }
    oil_.clear();
    for (int32_t oi : ois) {
        const ObjectDef* d = defs_.object(oi);
        if (!d || d->type < 2) continue;
        OiEntry e;
        e.oi = oi; e.type = d->type; e.vflags = d->oc_vflags; e.ink = d->ink; e.ink_param = d->ink_param;
        e.oc_flags = d->oc_flags; e.gen = 0; e.first = kNone; e.col_flags = 65535; e.name = d->name; e.oo = -1;
        if (d->type == kText || d->type == kQuestion)
            for (const auto& fi : fd_->instances) if (fi.object == oi) { e.oo = fi.handle; break; }
        oil_.push_back(e);
    }
    uk_ = 0;
}

void Run::create_start_objects() { // hu
    lc_ |= kLcCreating;
    for (const auto& f : fd_->instances) {
        const ObjectDef* b = defs_.object(f.object);
        if (!b) continue;
        int32_t e = kHvx;
        if (f.create != 0) continue; // ya.my: created at frame start
        if (b->type == kText) e |= kHText;
        if (!(b->oc_vflags & kViVisibleAtStart)) {
            if (b->type == kQuestion) continue;
            e |= kHHidden;
        }
        if (!(b->oc_flags & kZGy)) create(f.handle, f.object, NAN, NAN, -1, e, -1, -1);
    }
    lc_ &= ~kLcCreating;
}

void Run::reset_timers() { // tu
    vk_ = 0;
    eo_ = zb_ = lb_ = 0;
    rf_ = 0;
    std::fill(tk_.begin(), tk_.end(), 0u);
    const double qm = fd_->display_w, rm = fd_->display_h; // A.Pn, A.On
    vm_ = -480; zm_ = -300; tm_ = nd_ + 480; xm_ = od_ + 300;
    double a = ea_ - 64;
    if (a < 0) a = vm_;
    um_ = a;
    a = ga_ - 16;
    if (a < 0) a = zm_;
    ym_ = a;
    a = ea_ + qm + 64;
    if (a > nd_) a = tm_;
    sm_ = a;
    a = ga_ + rm + 16;
    if (a > od_) a = xm_;
    wm_ = a;
    cm_ = 0;
    for (int i = 0; i < 4; i++) { mc_[i] = 0; wr_[i] = 0; ur_[i] = 255; }
    queue_.clear();
}

void Run::save_globals() { // VJ: objects flagged global (Rh & p.ky) keep their state into the next frame
    for (const OiEntry& b : oil_) {
        const ObjectDef* d = defs_.object(b.oi);
        if (!d || !(d->flags & 4) || none(b.first)) continue;
        const Instance* first = slots_[size_t(b.first)];
        if (!(b.type == 3 || b.type == 7 || first->has_values)) continue;
        std::vector<Saved>& list = globals_[b.name + std::to_string(b.type)];
        list.clear();
        for (int32_t f = b.first; !none(f);) {
            const Instance* a = slots_[size_t(f)];
            Saved s;
            if (b.type == 3) { s.text = a->text; s.has_text = a->has_text; }
            else if (b.type == 7) { s.counter = a->counter; s.cmin = a->cmin; s.cmax = a->cmax; }
            else { s.values = a->values; s.strings = a->strings; s.values_flags = a->values_flags; }
            list.push_back(s);
            f = a->next_same;
        }
    }
}

void Run::restore_globals() { // iv
    for (const OiEntry& b : oil_) {
        const ObjectDef* d = defs_.object(b.oi);
        if (!d || !(d->flags & 4) || none(b.first)) continue;
        auto it = globals_.find(b.name + std::to_string(b.type));
        if (it == globals_.end()) continue;
        size_t k = 0;
        for (int32_t f = b.first; !none(f) && k < it->second.size(); k++) {
            Instance* a = slots_[size_t(f)];
            const Saved& s = it->second[k];
            if (b.type == 3) { a->text = s.text; a->has_text = s.has_text; }
            else if (b.type == 7) { a->counter = s.counter; a->cmin = s.cmin; a->cmax = s.cmax; counter_layout(a); }
            else if (a->has_values) { a->values = s.values; a->strings = s.strings; a->values_flags = s.values_flags; }
            f = a->next_same;
        }
    }
}

void Run::start_frame(int32_t index) { // cI
    if (frame_ >= 0 && fd_) save_globals(); // tI at the end of the previous frame
    frame_ = index;
    fd_ = &defs_.frames[size_t(index)];
    frame_w_ = frame_h_ = 0;
    if (defs_.bundle) { // FRAMES record: name, width, height
        const uint32_t r = defs_.bundle->record_offset(data::kFrames, uint32_t(index));
        data::Reader rd{defs_.bundle->bytes(), r};
        rd.u32();
        frame_w_ = rd.u32();
        frame_h_ = rd.u32();
    }
    init_frame();
    sound_reset();
    mouse_to_frame();
    lb_ = 0;
    create_start_objects();
    restore_globals();
    prepare_program();
    reset_timers();
    // Cross-check: the items' OI-list indices against the ones the live runtime resolved for this frame.
    oil_mismatches_ = 0;
    const data::ProgramFrame& pf = program_.frames[size_t(frame_)];
    const uint32_t base = pf.group_count ? program_.groups[pf.first_group].first_item : 0;
    for (size_t k = 0; k < items_.size(); k++)
        if (items_[k].type() >= 0 && uint16_t(items_[k].oil) != program_.items[base + k].oil) oil_mismatches_++;
}

void Run::sound_reset() {
    for (int i = 0; i < 32; i++) { channel_[i] = -1; channel_volume_[i] = defs_.sound_volume; }
    samples_.clear();
}

void Run::sound_update() { // AG -> Qa.zG
    for (int i = 0; i < 32; i++) {
        if (channel_[i] < 0) continue;
        Sample& s = samples_[channel_[i]];
        if (s.active && !s.paused && s.started && sound_now_ >= s.ends_at) {
            if (s.loops > 0 && (s.loops--, s.loops > 0)) { s.ends_at = sound_now_ + s.ms; channel_gen_[i]++; continue; } // Gk again
            s.active = false;
            channel_[i] = -1;
        }
    }
}

void Run::sound_play(int32_t a, int32_t b, int32_t c) { // U.play 5699 -> Qa.play / Gk
    if (!defs_.sound_on) return;
    if (a < 0 || size_t(a) >= defs_.sample_ms.size() || defs_.sample_ms[size_t(a)] < 0) return; // LH: not in the bank
    if (!defs_.sound_multiple) c = 0;
    if (c < 0) {
        for (c = 0; c < 32 && channel_[c] >= 0; c++) {}
        if (c == 32) for (c = 0; c < 32 && channel_[c] < 0; c++) {}
        if (c >= 0 && c < 32) channel_volume_[c] = defs_.sound_volume;
    }
    if (c < 0 || c >= 32) return;
    if (channel_[c] >= 0 && channel_[c] != a) { samples_[channel_[c]].active = false; channel_[c] = -1; }
    for (int i = 0; i < 32; i++) if (channel_[i] == a) { samples_[a].active = false; channel_[i] = -1; }
    channel_[c] = a;
    channel_gen_[c]++;
    Sample& s = samples_[a];
    s.ms = defs_.sample_ms[size_t(a)];
    s.loops = b == 0 ? 10000000 : b;
    s.volume = channel_volume_[c];
    s.started = true;
    s.ends_at = sound_now_ + s.ms;
    s.paused = false;
    s.active = true;
}

void Run::sound_stop_all() { // us
    for (int i = 0; i < 32; i++) if (channel_[i] >= 0) { samples_[channel_[i]].active = false; channel_[i] = -1; }
}

bool Run::sample_playing(int32_t a) { // nI -> $u
    for (int i = 0; i < 32; i++)
        if (channel_[i] == a) { const Sample& s = samples_[a]; return s.started && s.active && !s.paused; }
    return false;
}

bool Run::any_playing() { // oI
    for (int i = 0; i < 32; i++)
        if (channel_[i] >= 0) { const Sample& s = samples_[channel_[i]]; if (s.started && s.active && !s.paused) return true; }
    return false;
}

int32_t Run::tick(double now) { // vH
    sound_now_ = now;
    sound_update();
    const double b = rf_;
    rf_ = now - vk_;
    go_ = rf_ - b;
    cm_ += go_;
    lb_ += 1;
    bc_ = go_ * fd_->frame_rate / 1000;
    for (int a = 0; a < 4; a++) wr_[a] = mc_[a];
    for (int a = 0; a < 4; a++) mc_[a] = 0; // qI: no pads bound on the host
    mouse_to_frame();
    for (int a = 0; a < 4; a++) {
        int32_t v = mc_[a] & ur_[a];
        mc_[a] = v;
        v ^= wr_[a];
        vr_[a] = v;
    }
    if (live_ != 0) {
        int32_t a = live_, s = 0;
        do {
            for (fw_ = 0; s < int32_t(slots_.size()) && !slots_[size_t(s)]; s++) {}
            if (s >= int32_t(slots_.size())) break;
            Instance* c = slots_[size_t(s)];
            c->picked_prev = std::move(c->picked_now);
            c->picked_now.clear();
            if (c->handled) { sc_ = s; handle_object(c); }
            a += fw_;
            s++;
            a--;
        } while (a != 0);
    }
    qe_++;
    special_lists();
    if (has_always_) run_list(0, nullptr);
    run_queue();
    flush_destroys();
    do_scroll();
    tr_ = -1;
    return zb_;
}

void Run::center_display(double a, double b, int32_t c, int32_t d) { // eK 6??? (CRun)
    const double qm = fd_->display_w, rm = fd_->display_h;
    a -= std::floor(qm / 2);
    b -= std::floor(rm / 2);
    if (c != -1 && c < int32_t(fd_->layers.size())) {
        const FrameLayer& L = fd_->layers[size_t(c)];
        auto trunc0 = [](double v) { return v < 0 ? std::ceil(v) : std::floor(v); };
        if (L.x_coeff > 1) { a -= ea_; a /= L.x_coeff; a = trunc0(ea_ + a); }
        if (L.y_coeff > 1) { b -= ga_; b /= L.y_coeff; b = trunc0(ga_ + b); }
    }
    if (a < 0) a = 0;
    if (b < 0) b = 0;
    double cc = a + qm, e = b + rm;
    if (cc > nd_) { cc = nd_ - qm; if (cc < 0) cc = 0; a = cc; }
    if (e > od_) { e = od_ - rm; if (e < 0) e = 0; b = e; }
    if ((d & 1) && a != ea_) { oj_ = a; scroll_pending_ = true; }
    if ((d & 2) && b != ga_) { pj_ = b; scroll_pending_ = true; }
}

void Run::do_scroll() { // doScroll -> bK + EK
    if (!scroll_pending_) return;
    scroll_pending_ = false;
    if (frame_ni_ != oj_ || frame_oi_ != pj_) {
        for (size_t i = 0; i < layer_x_.size() && i < fd_->layers.size(); i++) { // bK (no wrapping layers here)
            layer_x_[i] = oj_ * fd_->layers[i].x_coeff;
            layer_y_[i] = pj_ * fd_->layers[i].y_coeff;
        }
        frame_ni_ = oj_; frame_oi_ = pj_;
        // EK
        const double a = frame_ni_, b = frame_oi_;
        const double ds = a - ea_, es = b - ga_;
        int c = (ds != 0) + (es != 0);
        const double e = ea_, f = ga_;
        ea_ = a;
        um_ = a - 64; if (um_ < 0) um_ = vm_;
        ga_ = b;
        ym_ = b - 16; if (ym_ < 0) ym_ = zm_;
        sm_ = a + fd_->display_w + 64; if (sm_ > nd_) sm_ = tm_;
        wm_ = b + fd_->display_h + 16; if (wm_ > od_) wm_ = xm_;
        if (c != 0)
            for (Instance* n2 : slots_) {
                if (!n2) continue;
                if (n2->oc_flags & 2048) { // z.FF: moves with the display
                    if (!n2->has_move) { n2->x += ds; n2->y += es; }
                    else { set_x(n2, ds + n2->x); set_y(n2, es + n2->y); }
                } else if (n2->layer < int32_t(fd_->layers.size())) {
                    const FrameLayer& d = fd_->layers[size_t(n2->layer)];
                    double k = e, p2 = f, u2 = a, v2 = b;
                    if (d.flags & 1) { k *= d.x_coeff; u2 *= d.x_coeff; }
                    if (d.flags & 2) { p2 *= d.y_coeff; v2 *= d.y_coeff; }
                    k = n2->x + k - u2 + ds;
                    const double y = n2->y + p2 - v2 + es;
                    if (!n2->has_move) { n2->x = k; n2->y = y; } else { set_x(n2, k); set_y(n2, y); }
                }
            }
    }
    oj_ = ea_;
    pj_ = ga_;
}

void Run::mouse_button(int32_t button, bool down, int32_t clicks) { // zv / Av, then po 8550
    static const int32_t kJc[3] = {200, 201, 202}; // l.Ne, l.Tp, l.cn
    if (button < 0 || button > 2) return;
    keys_[kJc[button]] = down;
    if (!down || uk_ != 0) return; // po: frames with a mouse-controlled object take no clicks
    mouse_to_frame();
    const int32_t c = button + (clicks == 2 ? 256 : 0);
    tr_ = pc_ = c;
    fire(-262150);
    fire(-327686);
    std::vector<Instance*> hit;
    for (Instance* e : slots_) {
        if (!e) continue;
        const double f = e->x - e->hx, g = e->y - e->hy;
        if (!(mouse_x_ >= f && mouse_x_ < f + e->w && mouse_y_ >= g && mouse_y_ < g + e->h)) continue;
        if (!(e->col_flags & 256) || (e->flags & 1)) continue; // I.Qd, F.Me
        if (e->type == 2 && !(e->sp.flags & 128)) {
            const Mask* m = mask_for(e->b.image, 0, 1, 1);
            if (!m || !m->test_point(mouse_x_ - e->x, mouse_y_ - e->y, e->b.angle, e->b.sx, e->b.sy)) continue;
        }
        hit.push_back(e);
    }
    for (Instance* e : hit) { jw_ = e->oi; hw_ = e->slot; fire(-393222); }
}

bool Run::sprite_covers(const Instance* a, double x, double y) {
    if (!a || a->type != 2 || a->b.image < 0) return false;
    const double f = a->x - a->hx, g = a->y - a->hy;
    if (!(x >= f && x < f + a->w && y >= g && y < g + a->h)) return false;
    const Mask* m = mask_for(a->b.image, 0, 1, 1);
    return m && m->test_point(x - a->x, y - a->y, a->b.angle, a->b.sx, a->b.sy);
}

int32_t Run::next_frame(int32_t zb) const { // sn 5051
    static const int32_t kXn[16] = {1, 2, 3, 4, 5, 6, 7, 9, 0, 8, 11, 12, 10, 13, 14, 15}; // app.xn (oracle/app.json)
    const int32_t nf = int32_t(defs_.frames.size()), we = frame_;
    int32_t db;
    switch (zb) {
    case 1: db = we + 1; return db >= nf ? -1 : db;
    case 2: return std::max(0, we - 1);
    case 3:
        if (dm_ & 32768) db = std::min(std::max(dm_ & 32767, 0), nf - 1);
        else if (dm_ < 16) { db = kXn[dm_]; if (db == -1) db = we + 1; }
        else db = we + 1;
        return db < 0 || db >= nf ? we : db;
    default: return -1;
    }
}

std::string Run::ini_save_text() const {
    std::string o;
    for (const auto& f : ini)
        for (const auto& g : f.second)
            for (const auto& i : g.second)
                if (!i.second.empty()) o += f.first + '\x1f' + g.first + '\x1f' + i.first + '\x1f' + i.second + '\n';
    return o;
}

void Run::ini_load_text(const std::string& text) {
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(p, e - p);
        p = e + 1;
        size_t a = line.find('\x1f'), b = a == std::string::npos ? a : line.find('\x1f', a + 1),
               c = b == std::string::npos ? b : line.find('\x1f', b + 1);
        if (c == std::string::npos) continue;
        ini[line.substr(0, a)][line.substr(a + 1, b - a - 1)][line.substr(b + 1, c - b - 1)] = line.substr(c + 1);
    }
}

std::string Run::capture_json() const {
    std::string o;
    o.reserve(4096);
    o += "{\"t\":"; json_number(o, lb_);
    o += ",\"f\":"; json_number(o, frame_);
    o += ",\"rf\":"; json_number(o, rf_);
    o += ",\"seed\":"; json_number(o, dw_);
    o += ",\"o\":[";
    bool first = true;
    for (const Instance* h : slots_) {
        if (!h) continue;
        if (!first) o += ',';
        first = false;
        o += '[';
        json_number(o, h->slot); o += ',';
        json_number(o, h->oi); o += ',';
        json_number(o, h->x); o += ',';
        json_number(o, h->y); o += ',';
        json_number(o, h->has_pos ? h->b.image : -1); o += ',';
        json_number(o, h->has_anim ? h->an.anim : -1); o += ',';
        json_number(o, h->has_anim ? h->an.frame : -1); o += ',';
        json_number(o, h->has_pos ? h->b.dir : -1); o += ',';
        json_number(o, h->has_sprite ? h->sp.flags : -1); o += ',';
        bool any = false;
        if (h->has_values)
            for (size_t k = 0; k < h->values.size(); k++) {
                if (h->values[k] == 0) continue;
                o += any ? "," : "{";
                any = true;
                o += '"'; o += std::to_string(k); o += "\":";
                json_number(o, h->values[k]);
            }
        o += any ? "}" : "null";
        o += ',';
        if (h->type == 7) json_number(o, h->counter); else o += "null";
        o += ',';
        if (h->type == 3 && h->has_text) json_string(o, h->text); else o += "null";
        o += ']';
    }
    o += "]}";
    return o;
}

} // namespace ucn::game
