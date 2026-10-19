// Objects: creation and destruction, the OI lists, and the components the tick drives (sprite handler,
// movements, animation). Ported from build/runtime_pretty.js; line numbers and minified names per function.
#include <algorithm>
#include <cmath>

#include "game/run.h"

namespace ucn::game {

namespace {
// Object types (p.*) and flag constants (docs/runtime-map.md).
constexpr int32_t kActive = 2, kText = 3, kQuestion = 4, kCounter = 7, kExtension = 32;
// oc.Ag (z.*)
constexpr int32_t kZMovements = 16, kZAnimations = 32, kZValues = 256, kZSprite = 512, kZAt = 4096,
                  kZNeverDestroy = 8192, kZQj = 16384, kZHy = 32768, kZGy = 131072, kZEF = 1048576, kZJy = 8;
// oc.Vi
constexpr int32_t kViVisibleAtStart = 8, kViAutoRotate = 64, kViBF = 4;
// sprite flags r.*
constexpr int32_t kRHidden = 1, kRStatic = 2, kRInactive = 4, kREg = 32, kRTj = 64, kRSj = 128;
// creation flags h.*
constexpr int32_t kHNoMovement = 1, kHHidden = 2, kHText = 4, kHvx = 8;
// h.dt: inside frame-start creation
constexpr int32_t kLcCreating = 512;
// I.* collision flags
constexpr int32_t kIQd = 256, kIJp = 512, kIKp = 1024;
// animation numbers (w.*)
constexpr int32_t kAnimStopped = 0, kAnimWalking = 1, kAnimRunning = 2, kAnimAppear = 3, kAnimDisappear = 4;
const int32_t kAnimOrder[] = {0, 1, 2, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, -1}; // Ga.Nt
// frame flags (A.Tb, E.*)
constexpr int32_t kTbTimeBased = 32768, kTbNx = 32;

int32_t to_i32(double d) { // ToInt32
    if (!std::isfinite(d)) return 0;
    double t = std::trunc(d);
    double m = std::fmod(t, 4294967296.0);
    if (m < 0) m += 4294967296.0;
    return int32_t(uint32_t(m));
}
uint32_t to_u32(double d) { return uint32_t(to_i32(d)); }
} // namespace

int32_t Run::random(int32_t n) { // 6926 random
    int32_t b = 31415 * dw_ + 1;
    dw_ = b &= 65535;
    return int32_t((uint32_t(b) * uint32_t(n)) >> 16); // `b * a >>> 16`: b < 65536, n small, product fits
}

int32_t Run::random_dir(int32_t a) { // 6931 Hu
    if (a == 0 || a == -1) return random(32);
    int32_t c = 0, d = 0;
    uint32_t e = uint32_t(a);
    for (int b = 0; b < 32; b++) { if (e & 1) { d++; c = b; } e >>= 1; }
    if (d == 1) return c;
    d = random(d);
    e = uint32_t(a);
    for (int b = 0; b < 32; b++) {
        if ((e & 1) && (d--, d < 0)) return b;
        e >>= 1;
    }
    return 0;
}

int32_t Run::oil_of(int32_t oi) const { // 8962 Fn (no qualifiers in this program)
    int32_t c = 0;
    while (c < int32_t(oil_.size()) && oil_[size_t(c)].oi != oi) c++;
    return c;
}

void Run::image_box(int32_t image, double b, double c, double d, double* w, double* h, double* hx, double* hy,
                    double* ax, double* ay) const { // 7568 Ii, 7587 iH
    const ImageInfo* e = defs_.image(image);
    if (!e) { *w = *h = *hx = *hy = *ax = *ay = 0; return; }
    double a = e->width, f = e->height, g = e->hot_x, m = e->hot_y, q = e->act_x, ee = e->act_y;
    if (c != 1) { g *= c; q *= c; a *= c; }
    if (d != 1) { m *= d; ee *= d; f *= d; }
    if (b != 0) {
        double dd, gg;
        if (b == 90) { dd = 0; gg = 1; }
        else if (b == 180) { dd = -1; gg = 0; }
        else if (b == 270) { dd = 0; gg = -1; }
        else { gg = b * M_PI / 180; dd = std::cos(gg); gg = std::sin(gg); }
        const double bx = g, by = m;
        double h2 = -bx * dd, n2 = -bx * gg, l2 = -by * dd, ff = -by * gg;
        double mm = h2 + ff, qq = l2 - n2;
        double ex = a - bx;
        h2 = ex * dd; n2 = ex * gg;
        double e1 = h2 + ff;
        l2 -= n2;
        double fy = f - by;
        double k = h2 + fy * gg;
        fy = fy * dd - n2;
        double p2 = mm + k - e1, u2 = qq + fy - l2;
        double minx = std::min(mm, std::min(e1, std::min(k, p2)));
        double miny = std::min(qq, std::min(l2, std::min(fy, u2)));
        double maxx = std::max(mm, std::max(e1, std::max(k, p2)));
        double maxy = std::max(qq, std::max(l2, std::max(fy, u2)));
        double cx = q - bx, cy = ee - by;
        q = cx * dd + cy * gg - minx;
        ee = cy * dd - cx * gg - miny;
        g = -minx; m = -miny;
        a = maxx - minx; f = maxy - miny;
    }
    *w = a; *h = f; *hx = g; *hy = m; *ax = q; *ay = ee;
}

void Run::set_image_metrics(Instance* a, int32_t image) {
    if (!defs_.image(image)) return; // Ii returned null: keep the old metrics
    image_box(image, a->b.angle, a->b.sx, a->b.sy, &a->w, &a->h, &a->hx, &a->hy, &a->ax, &a->ay);
}

// ---------------------------------------------------------------- OI lists

void Run::link(Instance* a) { // 6586 bJ
    int32_t c = 0;
    while (c < int32_t(oil_.size()) && oil_[size_t(c)].oi != a->oi) c++;
    OiEntry& b = oil_[size_t(c)];
    if (none(b.first)) {
        b.first = a->slot;
        a->prev_same = c | INT32_MIN;
        a->next_same = kNone;
    } else {
        Instance* h = slots_[size_t(b.first)];
        a->prev_same = h->prev_same;
        h->prev_same = a->slot;
        a->next_same = h->slot;
        b.first = a->slot;
    }
    a->event_base = b.event_base;
    a->oil = c;
    a->col_flags = b.col_flags;
    if (a->handle == -1) a->handle = b.oo;
    else if (b.oo == -1) b.oo = a->handle;
    b.count += 1;
}

void Run::unlink(Instance* a) { // 6597 aJ
    OiEntry& b = oil_[size_t(a->oil)];
    --b.count;
    if (!none(a->prev_same)) {
        Instance* c = slots_[size_t(a->prev_same)];
        if (!none(a->next_same)) {
            Instance* n = slots_[size_t(a->next_same)];
            if (c) c->next_same = a->next_same;
            if (n) n->prev_same = a->prev_same;
        } else if (c) c->next_same = kNone;
    } else if (!none(a->next_same)) {
        Instance* c = slots_[size_t(a->next_same)];
        if (c) { c->prev_same = a->prev_same; b.first = c->slot; }
    } else b.first = kNone;
}

// ---------------------------------------------------------------- creation / destruction

int32_t Run::create(int32_t handle, int32_t oi, double x, double y, int32_t dir, int32_t f, int32_t g, int32_t m) {
    // 6444 su. x/y = NAN means "the frame instance's position" (the runtime's 2147483648 sentinel).
    const FrameInstance* l2 = nullptr;
    if (handle != -1)
        for (const auto& fi : fd_->instances) if (fi.handle == handle) { l2 = &fi; break; }
    const ObjectDef* n2 = defs_.object(oi);
    if (!n2) return -1;
    if (!(n2->oc_vflags & kViVisibleAtStart)) f |= kHHidden;
    if (live_ >= int32_t(slots_.size())) return -1;
    const int32_t type = n2->type;
    if (type != kActive && type != kText && type != kCounter && type < kExtension) return -1; // types 4,5,6,9 unused
    // yd: an extension object exists only if the runtime build has the extension. This build has Ini (type 32);
    // Perspective (type 33) is missing, so its objects are never created (none appear in any live capture).
    if (type > kExtension) return -1;
    if (m < 0) for (m = 0; m < int32_t(slots_.size()) && slots_[size_t(m)]; m++) {}
    if (m >= int32_t(slots_.size())) return -1;
    Instance* k = new Instance();
    slots_[size_t(m)] = k;
    live_++;
    k->def = n2;
    k->oc_flags = n2->oc_flags;
    if (m > sc_) fw_++;
    k->slot = m;
    creations_++;
    if (creations_ == 0) creations_ = 1;
    k->creation = creations_;
    k->oi = oi;
    k->handle = handle;
    k->type = type;
    link(k);
    k->handled = true;
    if (!(k->oc_flags & kZHy)) {
        k->oc_flags &= ~kZQj;
        if ((k->col_flags & kIJp) && (fd_->flags & kTbNx)) k->oc_flags |= kZQj;
        if (k->col_flags & (kIQd | kIKp)) k->oc_flags |= kZQj;
    }
    if (std::isnan(x)) x = l2 ? l2->x : 0;
    k->x = x;
    if (std::isnan(y)) y = l2 ? l2->y : 0;
    k->y = y;
    if (l2 && g == -1) g = l2->layer;
    if (g < 0) g = 0;
    k->layer = g;
    const int32_t js = ++layer_counts_[size_t(g)];
    // components
    if (k->oc_flags & (kZAnimations | kZMovements | kZSprite)) { k->has_pos = true; k->b = PosBlock(); }
    if (k->oc_flags & kZMovements) {
        k->has_move = true;
        if (!(f & kHNoMovement)) movement_init(k, 0, n2, dir, -1);
    }
    if (k->oc_flags & kZAnimations) { k->has_anim = true; anim_init(k); }
    if (k->oc_flags & kZSprite) { // 13936 r.$H
        k->has_sprite = true;
        SpriteState& d = k->sp;
        d.layer = g;
        d.js = js;
        d.flags = kRTj;
        if (!(k->col_flags & kIQd)) d.flags &= ~kRTj;
        if (oil_[size_t(k->oil)].vflags & kViBF) d.flags |= kRSj;
        if (f & kHHidden) {
            d.flags |= kRHidden;
            if (type == kText) { k->flags |= 8192; d.flags &= ~kRTj; } // F.fh
        } else d.flags |= kREg;
        d.ink = oil_[size_t(k->oil)].ink;
        d.ink_param = oil_[size_t(k->oil)].ink_param;
        // The HTML5 export draws Phantom Freddy (blend coefficient 0) from the night's first tick, though its events
        // only fade him in from 255 when his level is above 0 (office G322-G324). His jumpscare sprites start at
        // alpha 0; with fixes on he starts there too, so at level 0 he never shows.
        if (fixes && n2 && n2->name == "Phantom Freddy" && !(d.ink & 4096)) { d.ink |= 4096; d.ink_param = 0x00FFFFFF; }
        // The flashlight's beam (black around a white cone, fully opaque) is drawn with a light-adding effect in the
        // Windows game; the HTML5 export draws it plain, a black screen with a cone. Additive (ink 9) restores the look.
        if (fixes && n2 && n2->name == "lightbeam") d.ink = (d.ink & ~4095) | 9;
        if (k->b.move_type == 0) d.flags |= kRStatic;
    }
    if (k->oc_flags & kZValues) { // 14024 Ca.$
        k->has_values = true;
        k->values.assign(26, 0.0);
        k->strings.assign(10, std::string());
        k->values_flags = n2->values_flags;
        for (size_t c = 0; c < n2->values.size() && c < k->values.size(); c++) k->values[c] = n2->values[c];
        for (size_t c = 0; c < n2->strings.size() && c < k->strings.size(); c++) k->strings[c] = defs_.str(n2->strings[c]);
    }
    // type init: k.$(oc, q)
    if (type == kCounter) { // 10069 aa.$
        if (n2->has_display) { k->w = n2->disp_w; k->h = n2->disp_h; k->color1 = n2->disp_color1; if (n2->disp_type == 2 || n2->disp_type == 3) k->color2 = n2->disp_color2; }
        else { k->w = 1; k->h = 1; }
        k->cmin = n2->counter_min; k->cmax = n2->counter_max; k->counter = n2->counter_init;
        k->counter_float = false;
        counter_layout(k);
    } else if (type == kText) { // 10493 wd.$
        k->w = n2->text_w; k->h = n2->text_h;
        k->cmax = n2->text_dk;
        k->color1 = n2->texts.empty() ? 0 : n2->texts[0].color;
        k->has_text = false;
        k->text_flags = f;
        if ((f & kHText) && !n2->texts.empty()) { k->has_text = true; k->text = defs_.str(n2->texts[0].text); }
    }
    // D.Tu(true) -> hz: into the layer's display list, visible unless created hidden.
    if (k->has_sprite) {
        k->visible_list = !(k->sp.flags & kRHidden);
        k->display_back = !(k->oc_flags & kZAnimations) && (k->oc_flags & kZAt);
        display_add(k, -1);
    }
    return m;
}

void Run::display_add(Instance* a, int32_t index) {
    if (a->in_display) return;
    const size_t L = size_t(std::min(std::max(a->has_sprite ? a->sp.layer : a->layer, 0), int32_t(disp_main_.size()) - 1));
    auto& list = a->display_back ? disp_back_[L] : disp_main_[L];
    if (index < 0 || index >= int32_t(list.size())) list.push_back(a->slot);
    else list.insert(list.begin() + index, a->slot);
    a->in_display = true;
}

int32_t Run::display_remove(Instance* a) {
    if (!a->in_display) return -1;
    a->in_display = false;
    for (auto* lists : {&disp_back_, &disp_main_})
        for (auto& list : *lists)
            for (size_t i = 0; i < list.size(); i++)
                if (list[i] == a->slot) { list.erase(list.begin() + long(i)); return int32_t(i); }
    return -1;
}

void Run::display_move(Instance* a, int32_t index) { // be: clamp to [0, children]
    if (!a->in_display) return;
    const int32_t c = display_remove(a);
    if (index > c) index--; // Aa.be: the index counts the child's old place
    const size_t L = size_t(std::min(std::max(a->sp.layer, 0), int32_t(disp_main_.size()) - 1));
    auto& list = a->display_back ? disp_back_[L] : disp_main_[L];
    if (index > int32_t(list.size())) index = int32_t(list.size());
    if (index < 0) index = 0;
    list.insert(list.begin() + index, a->slot);
    a->in_display = true;
}

void Run::destroy_slot(int32_t a) { // 6534 zq
    Instance* c = slots_[size_t(a)];
    if (!c) return;
    display_remove(c); // D.Gb -> xh
    unlink(c);
    delete c;
    slots_[size_t(a)] = nullptr;
    live_--;
}

void Run::request_destroy(int32_t a) { // 6542 jg
    tk_[size_t(a) / 32] |= 1u << (a & 31);
    eo_++;
}

void Run::flush_destroys() { // 6546 eH
    if (eo_ == 0) return;
    for (int32_t a = 0; a < int32_t(slots_.size());) {
        uint32_t b = tk_[size_t(a) / 32];
        if (b != 0) {
            tk_[size_t(a) / 32] = 0;
            for (int32_t c = 0; b != 0 && c < 32; c++) {
                if (b & 1) {
                    Instance* d = slots_[size_t(a + c)];
                    if (d && oil_[size_t(d->oil)].count == 1) fire_for(d, (d->type & 0xFFFF) | -2162688);
                    destroy_slot(a + c);
                    eo_--;
                }
                b >>= 1;
            }
            if (eo_ == 0) break;
        }
        a += 32;
    }
}

// ---------------------------------------------------------------- per-tick handlers

void Run::handle_object(Instance* a) {
    if (a->type == kActive || a->type == kCounter || a->type == kText) {
        if (a->has_sprite) sprite_handle(a);
        if (a->has_pos) a->b.changed = false;
    } else if (a->type >= kExtension) { // 10737 yd.handle
        if (a->oc_flags & kZSprite) sprite_handle(a);
        else if ((a->oc_flags & 48) == 16 || (a->oc_flags & 48) == 48) move(a);
        else if ((a->oc_flags & 48) == 32) anim_step(a);
        if (a->ini_save > 0) a->ini_save--; // Ini Nu: the save countdown
        if (a->has_pos) a->b.changed = false;
    }
}

void Run::sprite_hide(Instance* a) { // 13995 fo
    if (!(a->sp.flags & kRHidden)) { a->sp.flags |= kRHidden; if (a->has_pos) a->b.changed = true; a->visible_list = false; }
}

void Run::sprite_show(Instance* a) { // 13998 Kv (layers are never hidden in this program)
    if (a->sp.flags & kRHidden) {
        a->sp.flags &= ~kRHidden;
        a->flags &= ~8192; // F.fh
        if (a->has_pos) a->b.changed = true;
        a->visible_list = true;
    }
}

void Run::sprite_handle(Instance* a) { // 13953 r.handle (fades are unused: no object has a transition)
    SpriteState& d = a->sp;
    if (!(d.flags & kRInactive)) {
        if (d.flash_interval != 0) {
            d.flash_left -= go_;
            if (d.flash_left < 0) {
                d.flash_left = d.flash_interval;
                if (!(d.flags & kREg)) { d.flags |= kREg; sprite_show(a); }
                else { d.flags &= ~kREg; sprite_hide(a); }
            }
        }
        if (a->has_move) move(a);
        if (!a->has_pos || a->b.player == 0) {
            const double b = a->x - a->hx, c = a->y - a->hy, dd = b + a->w, e = c + a->h;
            if (a->oc_flags & kZQj) {
                // the app flag nD is clear in this game: nothing to do
            } else if (!(dd >= um_ && b <= sm_ && e >= ym_ && c <= wm_)) {
                if (dd >= vm_ && b <= tm_ && e >= zm_ && c <= xm_) { d.flags |= kRInactive; d.js = display_remove(a); }
                else if (!(a->oc_flags & kZNeverDestroy)) request_destroy(a->slot);
            }
        }
    } else {
        const double b = a->x - a->hx, c = a->y - a->hy, dd = b + a->w, e = c + a->h;
        if (dd >= um_ && b <= sm_ && e >= ym_ && c <= wm_) {
            d.flags &= ~kRInactive;
            display_add(a, -1);   // Tu(false): back into the list...
            display_move(a, d.js); // ...at its old place (be(js))
        }
    }
}

bool Run::collisions(Instance* a) { // 6826 Kf: no collision triggers in this program, only the flag resets
    if (a->has_move) { a->mv.stop_flag = false; a->mv.jo = 0; }
    return false;
}

// ---------------------------------------------------------------- movements

void Run::movement_init(Instance* b, int32_t a, const ObjectDef* c, int32_t create_dir, int32_t e) { // 16400 Ea.$
    if (create_dir != -2) b->b.dir = create_dir;
    b->mv.pw = oil_[size_t(b->oil)].wv;
    b->b.move_type = -1;
    if (a < int32_t(c->moves.size())) {
        const MoveDef& m = c->moves[size_t(a)];
        b->mv.index = a;
        if (e == -1) e = m.type;
        b->b.move_type = e;
        b->mv.type = (e == 1 || e == 5) ? e : 0;
        // mu: a direction outside 0..31 is picked at random from the movement's direction mask
        int32_t dir = b->b.dir;
        if (dir < 0 || dir >= 32) {
            int32_t d = 0;
            uint32_t ee = uint32_t(m.dirs);
            for (int i = 0; i < 32; i++) { if (ee & 1) d++; ee >>= 1; }
            if (d == 0) dir = 0;
            else {
                d = random(d);
                ee = uint32_t(m.dirs);
                for (dir = 0;; dir++) { const uint32_t f = ee; ee >>= 1; if ((f & 1) && (d--, d < 0)) break; }
            }
        }
        b->b.dir = dir;
        Movement& v = b->mv;
        if (v.type == 0) { // Va.$
            b->b.speed = 0; b->b.moved = true; b->b.changed = true;
        } else if (v.type == 1) { // 15596 ue.$
            b->b.player = m.player;
            v.mx1 = m.mouse_x1 + b->x; v.my1 = m.mouse_y1 + b->y;
            v.mx2 = m.mouse_x2 + b->x; v.my2 = m.mouse_y2 + b->y;
            v.yp = v.xp = b->b.speed = 0;
            b->b.min_speed = 0; b->b.max_speed = 100;
            v.tj = m.flags;
            if (m.moving_at_start == 0) { b->b.speed = 0; } // Xn -> stop()
            b->b.changed = true;
        } else { // 15646 ve.$
            v.def = &m;
            v.start_x = b->x; v.start_y = b->y;
            v.reversing = false;
            v.pause = 0;
            b->path_event_tick = 0;
            b->b.min_speed = m.min_speed; b->b.max_speed = m.max_speed;
            v.acc = 0;
            path_start_step(b, 0);
            if (m.moving_at_start == 0) path_stop(b);
            b->b.speed = v.speed;
            b->b.changed = true;
            if (m.steps.empty()) path_stop(b);
        }
    }
    if (b->b.move_type == -1) { b->b.move_type = 0; b->mv.type = 0; b->b.speed = 0; b->b.moved = true; b->b.changed = true; b->b.dir = 0; }
}

void Run::move(Instance* a) {
    switch (a->mv.type) {
    case 1: move_mouse(a); break;
    case 5: move_path(a); break;
    default: move_static(a); break;
    }
}

void Run::move_static(Instance* a) { // 16311 Va.move
    if (a->has_anim) anim_step(a);
    if (a->b.moved) { a->b.moved = false; collisions(a); }
}

void Run::move_mouse(Instance* a) { // 15609 ue.move
    Movement& m = a->mv;
    double x = a->x, y = a->y;
    if (m.mb == 0 && ur_[a->b.player - 1 < 0 ? 0 : a->b.player - 1] != 0) {
        x = mouse_x_; if (x < m.mx1) x = m.mx1; if (x > m.mx2) x = m.mx2;
        y = mouse_y_; if (y < m.my1) y = m.my1; if (y > m.my2) y = m.my2;
        int32_t c = to_i32(x - a->x), d = to_i32(y - a->y), e = 0;
        if (c < 0) { c = -c; e |= 1; }
        if (d < 0) { d = -d; e |= 2; }
        int32_t f = (c + d) << 2;
        if (f > 250) f = 250;
        a->b.speed = f;
        if (f != 0) {
            static const int32_t il[] = {2599, 0, 844, 31, 479, 30, 312, 29, 210, 28, 137, 27, 78, 26, 25, 25, 0, 24};
            if (d == 0) d = 1;
            const double cc = double(c << 8) / d;
            int32_t i = 0;
            while (!(cc >= il[i])) i += 2;
            int32_t dir = il[i + 1];
            if (e & 2) dir = (-dir + 32) & 31;
            if (e & 1) dir = (((-((dir - 8) & 31)) & 31) + 8) & 31;
            a->b.dir = dir;
        }
    }
    if (a->b.speed != 0) { m.yp = 0; m.xp = a->b.speed; }
    m.yp++;
    if (m.yp > 10) m.xp = 0;
    a->b.speed = m.xp;
    if (a->has_anim) anim_step(a);
    a->x = x; a->y = y;
    a->b.changed = true;
    qe_++;
    m.sf = qe_;
    collisions(a);
}

void Run::path_stop(Instance* a) { // 15863 ve.stop
    Movement& m = a->mv;
    if (m.mb == 0) m.mb = m.speed | 32768;
    m.speed = 0;
    m.stop_flag = true;
}

bool Run::path_stop_at_end(Instance* a) { // 15831 mr
    Movement& m = a->mv;
    m.mb = m.speed = 0;
    m.stop_flag = true;
    m.bp = false;
    return true;
}

void Run::path_reposition(Instance* a) { // 15837 vB
    if (a->mv.def->reposition) { a->x = a->mv.start_x; a->y = a->mv.start_y; a->b.changed = true; }
}

void Run::path_speed(Instance* a) { // 15822 qB
    Movement& m = a->mv;
    const PathStep& st = m.def->steps[size_t(m.step)];
    m.step_len = st.length;
    int32_t s = st.speed;
    const double b = m.pause;
    if (b != 0) { m.pause = 20 * b; m.mb = s |= 32768; }
    if (m.mb != 0) s = 0;
    if (s != m.speed || s != 0) { m.speed = s; m.bp = m.stop_flag = true; }
    a->b.speed = m.speed;
}

void Run::path_start_step(Instance* a, int32_t s) { // 15816 zk
    Movement& m = a->mv;
    if (s >= int32_t(m.def->steps.size())) { path_stop(a); return; }
    const PathStep& st = m.def->steps[size_t(s)];
    m.reversing = false;
    m.step = s;
    m.pause = st.pause;
    m.step_cos = st.cos; m.step_sin = st.sin;
    m.from_x = a->x; m.from_y = a->y;
    m.to_x = a->x + st.dx; m.to_y = a->y + st.dy;
    a->b.dir = st.dir;
    path_speed(a);
}

void Run::path_start_back(Instance* a, int32_t s) { // 15819 Vl
    Movement& m = a->mv;
    if (s >= int32_t(m.def->steps.size())) { path_stop(a); return; }
    const PathStep& st = m.def->steps[size_t(s)];
    m.reversing = true;
    m.step = s;
    m.step_cos = -st.cos; m.step_sin = -st.sin;
    m.from_x = a->x; m.from_y = a->y;
    m.to_x = a->x - st.dx; m.to_y = a->y - st.dy;
    a->b.dir = (st.dir + 16) & 31;
    path_speed(a);
}

void Run::path_events(Instance* a) { // 15826 We
    if (a->path_event_tick == lb_) {
        pc_ = 0;
        fire_for(a, -1310720 | (a->type & 0xFFFF));
        fire_for(a, -2293760 | (a->type & 0xFFFF));
    }
    if (a->path_end_tick == lb_) { pc_ = 0; fire_for(a, -1376256 | (a->type & 0xFFFF)); }
}

bool Run::path_step(Instance* a, int64_t amount) { // 15707 tB
    Movement& m = a->mv;
    const MoveDef& def = *m.def;
    double acc = double(amount) + double(m.acc);
    int64_t b = int64_t(to_u32(acc) >> 16);
    if (b < m.step_len) {
        m.acc = int64_t(acc);
        const double ny = double(b) * m.step_sin / 16384 + m.from_y;
        a->x = double(b) * m.step_cos / 16384 + m.from_x;
        a->y = ny;
        a->b.changed = true;
        collisions(a);
        return m.stop_flag;
    }
    b -= m.step_len;
    double r = double(int32_t(uint32_t(b) << 16) | (to_i32(acc) & 65535));
    if (m.speed != 0) r /= m.speed;
    path_di_ += (to_i32(r) >> 5) & 65535;
    a->x = m.to_x; a->y = m.to_y;
    a->b.changed = true;
    collisions(a);
    if (m.stop_flag) return true;
    a->path_event_tick = lb_;
    a->node_name = -1;
    int32_t s = m.step;
    m.acc = 0;
    if (!m.reversing) {
        s++;
        if (s < def.path_steps) {
            a->node_name = def.steps[size_t(s)].name;
            path_start_step(a, s);
            path_events(a);
            return m.stop_flag;
        }
        a->path_end_tick = lb_;
        m.step = s;
        if (def.reverse != 0) {
            m.reversing = true;
            s--;
            a->node_name = def.steps[size_t(s)].name;
            path_start_back(a, s);
            path_events(a);
            return m.stop_flag;
        }
        path_reposition(a);
        if (def.loop == 0) { path_stop_at_end(a); path_events(a); return m.stop_flag; }
        s = 0;
    } else {
        a->node_name = def.steps[size_t(s)].name;
        m.pause = def.steps[size_t(s)].pause;
        s--;
        if (s >= 0) { path_start_back(a, s); path_events(a); return m.stop_flag; }
        path_reposition(a);
        if (!m.reversing) { path_events(a); return m.stop_flag; }
        if (def.loop == 0) { path_stop_at_end(a); path_events(a); return m.stop_flag; }
        s = 0;
        m.reversing = false;
    }
    path_start_step(a, s);
    path_events(a);
    return m.stop_flag;
}

void Run::move_path(Instance* a) { // 15665 ve.move
    Movement& m = a->mv;
    a->path_event_tick = 0;
    a->b.anim_mode = kAnimWalking;
    if (a->has_anim) anim_step(a);
    if (m.speed == 0) {
        double p = m.pause;
        if (p == 0) { a->b.speed = 0; collisions(a); return; }
        p -= go_;
        if (p > 0) { m.pause = p; a->b.speed = 0; collisions(a); return; }
        m.pause = 0;
        m.speed = m.mb & 32767;
        m.mb = 0;
        a->b.speed = m.speed;
    }
    double s = (fd_->flags & kTbTimeBased) ? 256 * bc_ : 256;
    path_di_ = s;
    for (;;) {
        bool b = false;
        path_fo_ = s;
        s *= m.speed;
        double lw;
        int32_t si = to_i32(s) << 5;
        if (524288 >= si) lw = si;
        else { s = 16384.0 / m.speed; path_fo_ = s; lw = 524288; }
        for (;;) {
            m.bp = false;
            if (path_step(a, int64_t(lw)) && !m.bp) { b = true; break; }
            if (path_di_ == path_fo_) { b = true; break; }
            if (path_di_ > path_fo_) { path_di_ -= path_fo_; s = path_di_; break; }
            b = true; // the runtime's MT_Speed branch (an undefined global); never reached here
            break;
        }
        if (b) break;
    }
}

// ---------------------------------------------------------------- animation (Ga, 7345)

bool Run::anim_exists(const Instance* a, int32_t n) const { // th
    return n >= 0 && n < int32_t(a->def->anim_exists.size()) && a->def->anim_exists[size_t(n)] != 0;
}

void Run::anim_reset(Instance* a, int32_t mode) { // tA
    a->b.anim_mode = mode;
    Animator& x = a->an;
    x.stopped = false;
    x.frame = 0; x.counter = 0; x.forced_frame = 0; x.forced_speed = 0; x.forced_dir = 0; x.forced_anim = 0;
    x.or_dir = -1; x.min_speed = -1; x.anim = -1;
    x.yo = nullptr; x.oe = nullptr;
    anim_save_box(a);
}

void Run::anim_init(Instance* a) { // Ga.$
    a->an.lm = 0;
    anim_reset(a, kAnimWalking);
    if (anim_exists(a, kAnimAppear)) {
        a->an.lm = 1; anim_force(a, kAnimAppear);
        if (a->an.loops == 0) a->an.loops = 1; // Ot
        anim_save_box(a);
    } else {
        int i = 0;
        for (; kAnimOrder[i] >= 0 && !anim_exists(a, kAnimOrder[i]); i++) {}
        if (kAnimOrder[i] < 0 && anim_exists(a, kAnimDisappear)) {
            a->an.lm = 2; anim_force(a, kAnimDisappear);
            if (a->an.loops == 0) a->an.loops = 1;
            anim_save_box(a);
        }
    }
}

bool Run::anim_step(Instance* a) { // Oe
    switch (a->an.lm) {
    case 0: return anim_save_box(a);
    case 1: // pG
        anim_update(a, 1);
        if (a->an.forced_anim != kAnimAppear + 1) {
            if (anim_exists(a, kAnimStopped) || anim_exists(a, kAnimWalking) || anim_exists(a, kAnimRunning)) {
                a->an.lm = 0; anim_release(a);
            } else a->an.lm = 2; // uA: start disappearing (not used by any object here)
        }
        break;
    case 2: // qG
        anim_update(a, 1);
        if (a->an.forced_anim != kAnimDisappear + 1) request_destroy(a->slot);
        break;
    }
    return false;
}

bool Run::anim_save_box(Instance* a) { // fn
    PosBlock& b = a->b;
    b.prev_x = a->x; b.box_x1 = a->x - a->hx; b.box_x2 = b.box_x1 + a->w;
    b.prev_y = a->y; b.box_y1 = a->y - a->hy; b.box_y2 = b.box_y1 + a->h;
    b.prev_image = b.image; b.prev_angle = b.angle;
    return anim_update(a, 1);
}

void Run::anim_force(Instance* a, int32_t n) { a->an.forced_anim = n + 1; anim_update(a, 0); }   // Zp
void Run::anim_release(Instance* a) { a->an.forced_anim = 0; anim_update(a, 0); }              // rG
void Run::anim_force_speed(Instance* a, int32_t s) {                                          // oG
    if (s < 0) s = 0;
    if (s > 100) s = 100;
    a->an.forced_speed = s + 1; anim_update(a, 0);
}
void Run::anim_force_frame(Instance* a, int32_t f) {                                          // nG
    if (f >= a->an.frame_count) f = a->an.frame_count - 1;
    if (f < 0) f = 0;
    a->an.forced_frame = f + 1; anim_update(a, 0);
}

bool Run::anim_update(Instance* inst, int32_t mode) { // sh
    Animator& x = inst->an;
    PosBlock& pb = inst->b;
    const ObjectDef* def = inst->def;
    if (def->anim_count == 0) return false;
    int32_t c = pb.speed, d = pb.anim_mode;
    if (x.forced_speed != 0) c = x.forced_speed - 1;
    if (d == kAnimWalking) { if (c == 0) d = kAnimStopped; if (c >= 75) d = kAnimRunning; }
    if (x.forced_anim != 0) d = x.forced_anim - 1;
    if (d != x.anim) {
        x.anim = d;
        if (d >= def->anim_count) d = def->anim_count - 1;
        const Anim* an = &def->anims[size_t(def->anim_alias[size_t(d)])];
        if (an != x.oe) { x.oe = an; x.cur_dir = -1; x.frame = 0; if (!(inst->oc_flags & kZEF)) x.counter = 0; }
    }
    if (!x.oe || !x.oe->present) return false;
    int32_t e = pb.dir % 32;
    bool rotated = false;
    if (x.forced_dir != 0) e = x.forced_dir - 1;
    if (x.cur_dir != e) {
        x.cur_dir = e;
        const Anim& oe = *x.oe;
        if (!oe.dir_present[e]) {
            if (oe.xi[e] & 64) e = oe.xi[e] & 63;
            else if (oe.rh[e] & 64) e = oe.rh[e] & 63;
            else {
                const int32_t dd = e;
                if (x.or_dir < 0) e = oe.rh[e] & 63;
                else { e -= x.or_dir; e = 15 < (e & 31) ? oe.rh[dd] & 63 : oe.xi[dd] & 63; }
            }
        } else x.or_dir = e;
        const AnimDir* dir = oe.dir_present[e] ? &oe.dirs[e] : nullptr;
        if (oe.dir_present[0] && (def->oc_vflags & kViAutoRotate)) {
            pb.angle = 360.0 * x.cur_dir / 32;
            dir = &oe.dirs[0];
            x.yo = nullptr;
            rotated = true;
        }
        if (x.yo != dir && dir) {
            x.yo = dir;
            x.loops = dir->loops;
            x.back_to = dir->back_to;
            if (dir->min_speed != x.min_speed || dir->max_speed != x.max_speed) {
                x.min_speed = dir->min_speed; x.max_speed = dir->max_speed;
                x.speed_range = dir->max_speed - dir->min_speed;
                x.speed = dir->min_speed;
                x.last_move_speed = -1;
            }
            x.frame_count = int32_t(dir->images.size());
            if (x.forced_frame != 0 && x.forced_frame - 1 >= x.frame_count) x.forced_frame = 0;
            if (x.frame >= x.frame_count) x.frame = 0;
            const int32_t img = dir->images[size_t(x.frame)];
            if (!x.stopped) {
                pb.image = img;
                set_image_metrics(inst, img);
                pb.changed = pb.moved = true;
            }
            if (x.frame_count == 1) {
                if (x.min_speed == 0) x.frame_count = 0;
                if (pb.image == 0) return false;
                set_image_metrics(inst, pb.image);
                return false;
            }
        }
    }
    if ((mode == 0 && x.forced_frame == 0) || (!rotated && x.frame_count == 0)) return false;
    double a = x.speed_range;
    if (c != x.last_move_speed) {
        x.last_move_speed = c;
        if (a == 0) {
            x.speed = x.min_speed;
            if (x.forced_speed != 0) x.speed = x.forced_speed - 1;
        } else {
            const int32_t dd = pb.max_speed - pb.min_speed;
            if (dd == 0) {
                if (x.forced_speed != 0) { a = a * c / 100 + x.min_speed; if (a > x.max_speed) a = x.max_speed; }
                else { a /= 2; a += x.min_speed; }
            } else { a = a * c / dd + x.min_speed; if (a > x.max_speed) a = x.max_speed; }
            x.speed = a;
        }
    }
    const AnimDir* yo = x.yo;
    if (!yo) return false;
    int32_t f = x.forced_frame;
    if (f == 0) {
        if (x.speed == 0 || x.stopped) return false;
        double cc = x.counter;
        f = x.frame;
        double e2 = x.speed;
        if (fd_->flags & kTbTimeBased) e2 = std::round(e2 * bc_);
        for (cc += e2; 100 < cc;) {
            cc -= 100;
            f++;
            if (f >= x.frame_count && (f = x.back_to, x.loops != 0 && (x.loops--, x.loops == 0))) {
                x.frame = x.frame_count - 1;
                x.frame_count = 0;
                if (x.forced_anim != 0) x.forced_speed = x.forced_dir = x.forced_anim = 0;
                if (x.frame < int32_t(yo->images.size())) {
                    const int32_t img = yo->images[size_t(x.frame)];
                    if (img != pb.image) { pb.image = img; pb.changed = pb.moved = true; }
                }
                x.counter = cc;
                if (lc_ & kLcCreating) return false;
                if (rotated) { pb.changed = pb.moved = true; set_image_metrics(inst, pb.image); }
                pc_ = x.anim;
                return fire_for(inst, -131072 | (inst->type & 0xFFFF));
            }
        }
        x.counter = cc;
    } else f--;
    x.frame = f;
    pb.changed = pb.moved = true;
    const int32_t img = yo->images[size_t(f)];
    if (pb.image != img || x.last_angle != pb.angle) {
        pb.image = img;
        x.last_angle = pb.angle;
        if (img >= 0) set_image_metrics(inst, img);
    }
    return false;
}

void Run::set_x(Instance* a, double v) { // h.bc
    if (a->has_move) {
        Movement& m = a->mv;
        if (m.type == 5) { // ve.bc
            const double b0 = a->x;
            a->x = v;
            double b = b0 - m.from_x;
            const double nv = v - b;
            m.to_x = m.to_x - m.from_x + nv;
            const double old = m.from_x;
            m.from_x = nv;
            m.start_x -= old - nv;
            m.stop_flag = true; a->b.changed = a->b.moved = true;
        } else if (m.type == 1) {
            if (a->x != v) { a->x = v; m.stop_flag = true; a->b.changed = a->b.moved = true; }
        } else {
            if (a->x != v) { a->x = v; m.stop_flag = true; a->b.changed = true; }
            a->b.moved = true;
        }
    } else if (a->x != v) { a->x = v; if (a->has_pos) a->b.changed = a->b.moved = true; }
}

void Run::set_y(Instance* a, double v) { // h.jc
    if (a->has_move) {
        Movement& m = a->mv;
        if (m.type == 5) {
            const double b0 = a->y;
            a->y = v;
            double b = b0 - m.from_y;
            const double nv = v - b;
            m.to_y = m.to_y - m.from_y + nv;
            const double old = m.from_y;
            m.from_y = nv;
            m.start_y -= old - nv;
            m.stop_flag = true; a->b.changed = a->b.moved = true;
        } else if (m.type == 1) {
            if (a->y != v) { a->y = v; m.stop_flag = true; a->b.changed = a->b.moved = true; }
        } else {
            if (a->y != v) { a->y = v; m.stop_flag = true; a->b.changed = true; }
            a->b.moved = true;
        }
    } else if (a->y != v) { a->y = v; if (a->has_pos) a->b.changed = a->b.moved = true; }
}

void Run::set_dir(Instance* b, int32_t c) { // the shared tail of Nb (2,23) and Fb (2,1)
    c &= 31;
    if (b->b.dir != c) {
        b->b.dir = c;
        b->b.changed = true;
        // B.la.He(c): static/mouse/path movements ignore it
        if (b->type == kActive && b->has_anim) anim_update(b, 0);
    }
}

// aa.hb (10169), run when the counter is made and whenever its value, limits or colours change (mn, PG, QG): the
// box collisions see and the display draws. Digits (type 1) sit left of and above (x, y), so the hotspot is the
// strip's whole size; an image counter (4) takes its frame's box; a bar (2 vertical, 3 horizontal) grows with the
// value along its axis, from the far end when flag 256 is set. Font counters (5) keep the display size. ponytail:
// type 5 needs font metrics, add when a collision needs it.
void Run::counter_layout(Instance* k) {
    const ObjectDef* d = k->def;
    if (!d || !d->has_display) return;
    if (d->disp_type == 1) {
        std::string txt = Value::number_string(k->counter);
        const int32_t pad = d->disp_flags & 15;
        if (!k->counter_float && pad) {
            if (int32_t(txt.size()) > pad) txt = txt.substr(txt.size() - size_t(pad));
            else while (int32_t(txt.size()) < pad) txt = "0" + txt;
        }
        double w = 0, h = 0;
        for (char ch : txt) {
            int32_t i = -1;
            if (ch == '-') i = 10; else if (ch == '.') i = 12; else if (ch == '+') i = 11;
            else if (ch == 'e' || ch == 'E') i = 13; else if (ch >= '0' && ch <= '9') i = ch - '0';
            if (i < 0 || i >= int32_t(d->disp_frames.size())) continue;
            if (const ImageInfo* im = defs_.image(d->disp_frames[size_t(i)])) { w += im->width; h = std::max<double>(h, im->height); }
        }
        k->w = k->hx = w;
        k->h = k->hy = h;
    } else if (d->disp_type == 2 || d->disp_type == 3) {
        const double len = d->disp_type == 2 ? d->disp_h : d->disp_w; // ha.YD = 2, ha.XD = 3
        const double og = k->cmax <= k->cmin ? 0 : (k->counter - k->cmin) * len / (k->cmax - k->cmin);
        const bool from_end = (d->disp_flags & 256) != 0;
        if (d->disp_type == 3) { k->hy = 0; k->h = d->disp_h; k->w = og; k->hx = from_end ? og - d->disp_w : 0; }
        else { k->hx = 0; k->w = d->disp_w; k->h = og; k->hy = from_end ? og - d->disp_h : 0; }
    } else if (d->disp_type == 4 && !d->disp_frames.empty()) {
        const int32_t n = d->disp_frame_count;
        int32_t og = k->cmax <= k->cmin ? 0 : int32_t(std::floor((k->counter - k->cmin) * n / (k->cmax - k->cmin)));
        og = std::min(og, n - 1);
        if (const ImageInfo* im = defs_.image(d->disp_frames[size_t(std::max(og, 0))])) {
            k->w = im->width; k->h = im->height; k->hx = im->hot_x; k->hy = im->hot_y;
        }
    }
}

void Run::counter_note(Instance* a, double v) { // gu
    if (!a->counter_float && v != std::ceil(v)) a->counter_float = true;
}

void Run::counter_set(Instance* a, double v) { // mn
    if (!a->counter_float) {
        v = v < 0 ? std::ceil(v) : std::floor(v);
        if (v < a->cmin) v = a->cmin;
        if (v > a->cmax) v = a->cmax;
        if (v != std::round(a->counter)) { a->counter = v; if (a->has_pos) a->b.changed = true; counter_layout(a); }
    } else {
        if (v < a->cmin) v = a->cmin;
        if (v > a->cmax) v = a->cmax;
        if (v != a->counter) { a->counter = v; if (a->has_pos) a->b.changed = true; counter_layout(a); }
    }
}

// ---------------------------------------------------------------- collisions

const Mask* Run::mask_for(int32_t image, double angle, double sx, double sy) { // ea.Af(0, angle, sx, sy)
    MaskSet& m = masks_[image];
    if (!m.tried) {
        m.tried = true;
        const uint8_t* px = nullptr;
        int32_t w = 0, h = 0;
        const ImageInfo* im = defs_.image(image);
        if (im && alpha_ && alpha_(image, &px, &w, &h) && px) { m.base.build(px, w, h, im->hot_x, im->hot_y); m.ok = true; }
        else if (im && mask_src_ && mask_src_(image, &m.base)) { m.base.hot_x = im->hot_x; m.base.hot_y = im->hot_y; m.ok = true; }
    }
    if (!m.ok) return nullptr;
    if (angle == 0 && sx == 1 && sy == 1) return &m.base;
    for (auto& e : m.xf) if (e.first[0] == angle && e.first[1] == sx && e.first[2] == sy) return &e.second;
    Mask t;
    t.transform(m.base, angle, sx, sy); // an empty result stays an empty mask, as the runtime keeps it
    m.xf.push_back({{angle, sx, sy}, std::move(t)});
    return &m.xf.back().second;
}

std::vector<Instance*> Run::overlapping(Instance* a, int32_t b, double c, double d, double e, double f, double g,
                                        int32_t only_oil) { // am 6858
    std::vector<Instance*> q;
    f -= a->hx;
    const double h2 = f + a->w;
    g -= a->hy;
    const double l2 = g + a->h;
    constexpr int32_t kFh = 8192, kMe = 1;
    if ((a->flags & kFh) || (a->flags & kMe)) return q;
    const bool u2 = a->type == kActive && !(a->sp.flags & 128); // fine (mask) collisions
    const int32_t z2 = a->type == kActive ? a->sp.layer : -1;
    const int32_t w2 = a->flags;
    a->flags |= kFh;
    const Mask* v2 = nullptr;
    bool v2_tried = false;
    auto consider = [&](Instance* A) {
        const double P = A->x - A->hx, C = A->y - A->hy;
        if (!(P < h2 && P + A->w > f && C < l2 && C + A->h > g)) return;
        if (A->type == kActive) {
            if (!((z2 < 0 || z2 == A->sp.layer) && (A->sp.flags & kRTj))) return;
            if (!u2 || (A->sp.flags & 128)) { q.push_back(A); return; }
            if (!v2_tried) { v2_tried = true; v2 = mask_for(b, c, d, e); }
            const Mask* n2 = mask_for(A->b.image, A->b.angle, A->b.sx, A->b.sy);
            if (v2 && n2 && v2->overlaps(f, g, 0, *n2, P, C, 0)) q.push_back(A);
        } else q.push_back(A);
    };
    if (only_oil >= 0) {
        for (int32_t s = oil_[size_t(only_oil)].first; !none(s);) {
            Instance* A = slots_[size_t(s)];
            s = A->next_same;
            if (!(A->flags & kFh) && !(A->flags & kMe)) consider(A);
        }
    } else {
        for (Instance* A : slots_) if (A && !(A->flags & kFh)) consider(A);
    }
    a->flags = w2;
    return q;
}

bool Run::cond_overlap(Item& it) { // B.wA 3870
    const Param& p0 = params_[it.first_param];
    if (kg_false_) { sel_first(it.oil); sel_first(p0.oil); return false; }
    const bool b = it.qe & 1;
    Instance* c = sel_first(it.oil);
    if (!c) return b;
    int32_t d = hc_;
    std::vector<Instance*> g;
    do {
        const std::vector<Instance*> f = overlapping(c, c->b.image, c->b.angle, c->b.sx, c->b.sy, c->x, c->y, p0.oil);
        if (f.empty()) { if (!b) { d--; sel_drop(); } }
        else {
            bool any = false;
            for (Instance* x : f) if (!(x->flags & 1)) { g.push_back(x); any = true; }
            if (b) { if (any) { d--; sel_drop(); } }
            else if (!any) { d--; sel_drop(); }
        }
        c = sel_next();
    } while (c);
    if (d == 0) return false;
    c = sel_first(p0.oil);
    if (!c) return false;
    d = hc_;
    if (!b) {
        do {
            if (std::find(g.begin(), g.end(), c) == g.end()) { d--; sel_drop(); }
            c = sel_next();
        } while (c);
        return d != 0;
    }
    do {
        if (std::find(g.begin(), g.end(), c) != g.end()) { d--; sel_drop(); }
        c = sel_next();
    } while (c);
    return d != 0;
}

bool Run::cond_mouse_over(int32_t a, bool b) { // eA 6993
    Instance* c = sel_first(a);
    if (!c) return b;
    int32_t d = hc_;
    std::vector<Instance*> n2;
    for (Instance* g : slots_) {
        if (!g) continue;
        const double m = g->x - g->hx, q = g->y - g->hy, h2 = m + g->w, l2 = q + g->h;
        if (mouse_x_ >= m && mouse_x_ < h2 && mouse_y_ >= q && mouse_y_ < l2 && !(g->flags & 1)) {
            if (g->type == kActive && !(g->sp.flags & 128)) {
                const Mask* mk = mask_for(g->b.image, 0, 1, 1);
                if (mk && mk->test_point(mouse_x_ - g->x, mouse_y_ - g->y, g->b.angle, g->b.sx, g->b.sy)) n2.push_back(g);
            } else n2.push_back(g);
        }
    }
    if (n2.empty()) return b;
    if (!b) {
        do {
            if (std::find(n2.begin(), n2.end(), c) == n2.end()) { d--; sel_drop(); }
            c = sel_next();
        } while (c);
        return d != 0;
    }
    do {
        if (std::find(n2.begin(), n2.end(), c) != n2.end()) return false;
        c = sel_next();
    } while (c);
    return true;
}

} // namespace ucn::game
