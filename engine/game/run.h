#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "data/program.h"
#include "game/defs.h"
#include "game/mask.h"
#include "game/value.h"

namespace ucn::game {

// The host/console engine: a port of the HTML5 runtime's frame run (CRun, `app.G`), its event program
// (CEventProgram, `N.prototype`) and the object components the program drives. Each function names the runtime
// function it ports (pretty-printed Runtime.js line + minified name; docs/runtime-map.md). Linked-list fields keep
// the runtime's encoding: a slot/OI index, or a negative value (the runtime's 0x80000000 bit) for "none".

constexpr int32_t kNone = INT32_MIN;
inline bool none(int32_t v) { return v < 0; }

struct Instance;
class Run;

struct PosBlock {              // td (`b`): sprite position block
    int32_t image = -1;        // Ha
    double angle = 0;          // Qa
    double sx = 1, sy = 1;     // lb, qb
    int32_t dir = 0;           // Ua
    int32_t speed = 0;         // aa
    int32_t anim_mode = 0;     // pe (w.Ec = 1 "walking" when the movement says so)
    int32_t min_speed = 0, max_speed = 0; // Zh, gb
    int32_t move_type = -1;    // Kc
    int32_t player = 0;        // $h
    bool changed = false, moved = false; // L, Ra
    double prev_x = 0, prev_y = 0, box_x1 = 0, box_y1 = 0, box_x2 = 0, box_y2 = 0; // gj hj vw xw ww yw
    int32_t prev_image = -1;   // Qr
    double prev_angle = 0;     // Pr
};

struct Animator {              // Ga (`X`)
    int32_t lm = 0;            // 0 normal, 1 appearing, 2 disappearing
    int32_t forced_anim = 0;   // Hg (anim + 1)
    int32_t forced_dir = 0;    // Nr (dir + 1)
    int32_t forced_speed = 0;  // fj (speed + 1)
    int32_t anim = -1;         // Xh
    int32_t or_dir = -1;       // Or
    int32_t cur_dir = -1;      // Mr
    int32_t frame = 0;         // Pf
    double counter = 0;        // xo
    int32_t forced_frame = 0;  // Kk (frame + 1)
    int32_t min_speed = -1, max_speed = 0; // ej, km
    int32_t speed_range = 0;   // dC
    double speed = 0;          // jm
    int32_t last_move_speed = 0; // tw
    int32_t loops = 0;         // Lk
    int32_t back_to = 0;       // eC
    int32_t frame_count = 0;   // Ld (0 = stopped)
    bool stopped = false;      // zo
    const Anim* oe = nullptr;
    const AnimDir* yo = nullptr;
    double last_angle = 0;     // fC
};

struct Movement {              // Ea (`B`) and its active movement `la`
    int32_t index = 0;         // Ow
    bool stop_flag = false;    // R
    int32_t pw = 0;            // Pw
    int32_t jo = 0;            // Jo
    int32_t type = 0;          // 0 static (Va), 1 mouse (ue), 5 path (ve)
    int32_t sf = 0;            // Sf
    int32_t mb = 0;            // Mb
    // mouse
    double mx1 = 0, my1 = 0, mx2 = 0, my2 = 0; // ot pt qt rt
    int32_t xp = 0, yp = 0, tj = 0;
    // path
    const MoveDef* def = nullptr; // Ia
    double start_x = 0, start_y = 0; // Dp Ep
    bool reversing = false;    // hd
    double pause = 0;          // kh
    int64_t acc = 0;           // jh (16.16)
    int32_t speed = 0;         // Fc
    int32_t step = 0;          // fe
    int32_t step_cos = 0, step_sin = 0; // Xm Ym
    double from_x = 0, from_y = 0, to_x = 0, to_y = 0; // lh mh Mj Nj
    int32_t step_len = 0;      // Cp
    bool bp = false;           // Bp
};

struct SpriteState {           // r (`D`)
    int32_t layer = 0;         // Yk
    int32_t js = 0;
    int32_t flags = 0;         // M
    int32_t ink = 0;           // Xb
    int32_t ink_param = 0;     // Nb
    double flash_interval = 0, flash_left = 0; // Xk, Em
};

struct Instance {
    int32_t slot = 0;          // Sb
    int32_t oi = 0;            // Ue
    int32_t handle = -1;       // Nq: frame instance handle
    int32_t type = 0;          // Ea
    int32_t creation = 0;      // Ou
    int32_t oil = 0;           // index of Fb in Run::oil
    int32_t next_same = kNone, prev_same = kNone; // Zb, rg
    int32_t sel_next = kNone;  // mc
    int32_t event_base = 0;    // Pu
    int32_t col_flags = 0;     // Te
    int32_t oc_flags = 0;      // qa
    int32_t flags = 0;         // Z
    int32_t su = 0;            // Su
    bool handled = true;       // Gn
    double x = 0, y = 0;       // v, u
    int32_t xfrac = 0, yfrac = 0; // pg, qg
    double w = 0, h = 0, hx = 0, hy = 0; // K J ha ia
    double ax = 0, ay = 0;     // Qu Ru
    int32_t layer = 0;         // od
    int32_t path_event_tick = 0, path_end_tick = 0; // Ol, Hn
    int32_t node_name = -1;    // Nl
    bool visible_list = true;  // ta: drawn when in a display list
    bool in_display = false;   // Oa
    bool display_back = false; // Dy: the layer's background list (Ib) instead of Pa
    const ObjectDef* def = nullptr; // N

    bool has_pos = false, has_move = false, has_anim = false, has_sprite = false, has_values = false;
    PosBlock b;
    Movement mv;
    Animator an;
    SpriteState sp;
    int32_t values_flags = 0;  // T.ks
    std::vector<double> values; // T.Va
    std::vector<std::string> strings; // T.te

    // counter (aa) / text (wd)
    double counter = 0;        // pa
    double cmin = 0, cmax = 0; // rb, Nc
    bool counter_float = false; // zi
    int32_t color1 = 0, color2 = 0; // Cc Ko
    bool has_text = false;     // ei != null
    std::string text;          // ei
    int32_t text_flags = 0;    // vC
    // Ini extension (Ka)
    std::string ini_group = "Group", ini_item = "Item", ini_file = "Ini.ini";
    int32_t ini_save = 0;      // je

    std::vector<int32_t> picked_now, picked_prev; // Mq, pA (once-per-instance lists for KG-style conditions)
};

struct Sample {               // Qa: one per sample handle; playing state only (no audio out on the host)
    double ms = -1;            // decoded duration (< 0: never loaded, so never "playing")
    int32_t loops = 0;         // Jh (0 = 1e7)
    bool active = false;       // Ai
    bool paused = false;       // uh
    bool started = false;      // a source exists
    double ends_at = 0;        // when `onended` fires, frame time
    double volume = 100;
};

struct OiEntry {               // I (`P[]`): one per object type in the frame
    int32_t oi = 0;            // Yc
    int32_t type = 0;          // Kd
    int32_t vflags = 0;        // MB = oc.Vi
    int32_t ink = 0, ink_param = 0; // LB KB
    int32_t oc_flags = 0;      // yr = oc.Ag
    int32_t first = kNone;     // kb
    int32_t count = 0;         // Dg
    int32_t col_flags = 0;     // Jd
    int32_t event_base = 0;    // Vv
    int32_t wv = 0;            // Wv
    int32_t oo = -1;
    std::string name;          // aj
    // selection (`vf`/`Re`/`Bd`)
    int32_t gen = 0;           // Xc
    int32_t sel_first = kNone; // fb
    int32_t sel_count = 0;     // ud
    // action iteration (`MH`)
    int32_t act_gen = -1, act_loop = 0; // Sv, Tv
    int32_t act_state = 0;     // $i
    int32_t act_cur = -1, act_next = -1; // Cg, bj
    bool act_more = false;     // Mf
    int32_t su_gen = 0;        // Uv
};

struct Group {                 // G
    uint8_t conditions = 0, actions = 0;
    int32_t flags = 0;         // Ka
    int32_t hi = 0, hl = 0;    // Hi, Hl
    uint32_t first_item = 0;   // into Run::items
};

struct Item {                  // a condition or action, with its runtime-mutable parameter values
    int32_t code = 0;
    int32_t oi = 0;            // ke
    int32_t oil = 0;           // ub, resolved at frame start (`Fn`)
    int32_t ma = 0, qe = 0;
    uint32_t first_param = 0;
    uint8_t param_count = 0;
    int16_t type() const { return int16_t(uint32_t(code) & 0xFFFF); }
    int16_t num() const { return int16_t(uint32_t(code) >> 16); }
};

struct Param {
    uint16_t code = 0;
    uint32_t first_value = 0;  // into Run::values (mutable copy)
    uint8_t value_count = 0;
    uint32_t first_token = 0, token_count = 0;
    int32_t oil = -1;          // Ub / ro resolved at frame start
};

class Run {
public:
    Run(const Defs& defs, const data::Program& program) : defs_(defs), program_(program) {}

    void set_seed(int32_t seed) { forced_seed_ = seed; }
    // The clock the runtime seeds a frame's random numbers from when the frame has no fixed seed (Mt: h.Oc & 65535,
    // h.Oc = Date.getTime()). The console sets its timer here before each frame start; it stays 0 (the same numbers
    // every time) until something does, and set_seed overrides it (the parity checks).
    void set_clock(uint32_t ms) { clock_ms_ = ms; }
    // Collision masks need the images' pixels: (image handle) -> RGBA8 crop of width x height, or false.
    using AlphaSource = std::function<bool(int32_t image, const uint8_t** rgba, int32_t* w, int32_t* h)>;
    void set_alpha_source(AlphaSource s) { alpha_ = std::move(s); }
    // Or ready-made full-size masks (the console's masks.pak); the run sets the hotspot.
    using MaskSource = std::function<bool(int32_t image, Mask* out)>;
    void set_mask_source(MaskSource s) { mask_src_ = std::move(s); }
    // The pointer in window (app) coordinates, h.Gf/Hf; the frame sees it through Eu (centred display).
    void set_mouse(double x, double y) { window_mouse_x_ = x; window_mouse_y_ = y; }
    // cI: start frame `index` (instances created, program prepared, timers reset).
    void start_frame(int32_t index);
    // vH: one game tick at frame time `now_ms` (ms since the frame started).
    int32_t tick(double now_ms);

    // State the oracle records (tools/oracle_capture.js).
    int32_t frame() const { return frame_; }
    int32_t ticks() const { return lb_; }
    double timer() const { return rf_; }
    int32_t seed() const { return dw_; }
    const std::vector<Instance*>& slots() const { return slots_; }
    std::string capture_json() const;
    const FrameDef* frame_def() const { return fd_; }
    double scroll_x() const { return ea_; }
    double scroll_y() const { return ga_; }
    // Per-layer display lists (the layer's Ib then Pa children): slots in draw order, back to front.
    const std::vector<std::vector<int32_t>>& display_back() const { return disp_back_; }
    const std::vector<std::vector<int32_t>>& display_main() const { return disp_main_; }
    // Is sprite `a`'s current image solid at frame point (x, y)? The test po makes for a click (mask, hotspot,
    // angle and scale); false where it is transparent or has no mask.
    bool sprite_covers(const Instance* a, double x, double y);
    int32_t oil_mismatches() const { return oil_mismatches_; } // items whose OI resolution differs from the live one

    // Ini store shared by all Ini objects: file -> group -> item -> value. ini_dirty: written since last save.
    std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> ini;
    bool ini_dirty = false;
    // Fixes where the HTML5 export differs from the game's own logic (the console turns them on; the parity checks
    // against the live HTML5 game leave them off).
    bool fixes = false;
    std::map<int32_t, int32_t> unported; // condition/action codes met that the engine has no port for -> count
    std::string ini_save_text() const;          // one "file\x1fgroup\x1fitem\x1fvalue" line per entry
    void ini_load_text(const std::string& text);

    // Input, between ticks, as the browser's handlers deliver it: keys by JS keyCode (Az/Bz), mouse buttons
    // 0 left, 1 middle, 2 right (zv/Av: Jc[200 + ...] and, on a press, the click events of po).
    void set_key(int32_t code, bool down) { if (code >= 0 && code < 256) keys_[code] = down; }
    void mouse_button(int32_t button, bool down, int32_t clicks = 1);
    // sn: the frame the game goes to after tick() returned zb (-1: the application ends).
    int32_t next_frame(int32_t zb) const;
    // The sound channels for a mixer: sample handle (-1 none), a count bumped on every (re)start, volume 0..100.
    int32_t channel_sample(int i) const { return channel_[i]; }
    uint32_t channel_start(int i) const { return channel_gen_[i]; }
    double channel_volume(int i) const { return channel_volume_[i]; }

    ~Run();

private:
    friend struct Exec;
    const Defs& defs_;
    const data::Program& program_;
    const FrameDef* fd_ = nullptr;
    int32_t frame_ = -1;
    int32_t forced_seed_ = -1;
    uint32_t clock_ms_ = 0;
    uint32_t frame_w_ = 0, frame_h_ = 0;
    int32_t oil_mismatches_ = 0;

    // CRun state
    std::vector<Instance*> slots_; // H
    std::vector<OiEntry> oil_;     // P
    int32_t live_ = 0;             // yb
    int32_t creations_ = 0;        // Bo
    int32_t lb_ = 0;               // Lb
    double rf_ = 0, go_ = 0, cm_ = 0, bc_ = 0; // Rf Go Cm Bc
    double vk_ = 0;                // Vk (frame start, in frame-time ms)
    int32_t dw_ = 0;               // Dw
    int32_t zb_ = 0, dm_ = 0;      // end-of-frame request and target
    int32_t qe_ = 0;               // qe: movement counter
    int32_t lc_ = 0;               // Lc
    int32_t eo_ = 0;               // Eo: destroy requests
    std::vector<uint32_t> tk_;     // Tk: destroy bitmap
    int32_t sc_ = 0, fw_ = 0;      // sC, Fw
    double ea_ = 0, ga_ = 0;       // scroll
    double oj_ = 0, pj_ = 0;       // pending scroll (oj, pj)
    double frame_ni_ = 0, frame_oi_ = 0; // A.Ni, A.Oi
    bool scroll_pending_ = false;  // Rk & h.Ct
    std::vector<double> layer_x_, layer_y_; // pb[].x/y (render offsets)
    double nd_ = 0, od_ = 0;       // Nd Od: frame size
    double um_ = 0, ym_ = 0, sm_ = 0, wm_ = 0, vm_ = 0, zm_ = 0, tm_ = 0, xm_ = 0; // activation areas
    int32_t uk_ = 0;               // Uk: mouse movement players
    int32_t mc_[4] = {}, wr_[4] = {}, vr_[4] = {}, ur_[4] = {255, 255, 255, 255}; // joystick bits
    double mouse_x_ = 0, mouse_y_ = 0; // lj mj
    double window_mouse_x_ = 0, window_mouse_y_ = 0; // h.Gf, h.Hf
    static constexpr double kAppW = 1920, kAppH = 1080; // h.ma, h.ua (application window)
    void mouse_to_frame() { // Eu: lj = Gf + ea - Vf with Vf = ma/2 - Pn/2
        mouse_x_ = window_mouse_x_ + ea_ - (kAppW / 2 - fd_->display_w / 2.0);
        mouse_y_ = window_mouse_y_ + ga_ - (kAppH / 2 - fd_->display_h / 2.0);
    }
    std::vector<int32_t> layer_counts_; // pb[layer].Hv
    struct Saved { std::vector<double> values; std::vector<std::string> strings; int32_t values_flags = 0;
                   std::string text; bool has_text = false; double counter = 0, cmin = 0, cmax = 0; };
    std::map<std::string, std::vector<Saved>> globals_; // h.vi: global objects' state by name + type
    void save_globals();    // VJ 6386 (frame end)
    void restore_globals(); // iv 6416 (frame start)
    std::vector<std::vector<int32_t>> disp_back_, disp_main_; // pb[layer].Ib / .Pa children
    double path_di_ = 0, path_fo_ = 0;  // di, Fo: the path movement's per-tick budget
    bool keys_[256] = {};          // h.Jc
    // sound manager (U, 5680): 32 channels holding sample handles, their volumes, and the samples
    int32_t channel_[32] = {};     // Wa (-1 = empty)
    double channel_volume_[32] = {}; // Xo
    uint32_t channel_gen_[32] = {};
    std::map<int32_t, Sample> samples_;
    double sound_now_ = 0;         // frame time the channels are updated to
    AlphaSource alpha_;
    MaskSource mask_src_;
    struct MaskSet { bool tried = false, ok = false; Mask base; std::vector<std::pair<std::array<double, 3>, Mask>> xf; };
    std::map<int32_t, MaskSet> masks_;

    // CEventProgram state
    std::vector<Group> groups_;
    std::vector<Item> items_;
    std::vector<Param> params_;
    std::vector<double> values_;   // mutable copy of the program's value pool for this frame
    std::vector<int32_t> token_oil_; // per-token OI-list index (Ub), resolved at frame start
    std::vector<int32_t> ec_;      // event list heads, by OI/type base + -num
    std::vector<int32_t> rc_;      // group of each list entry (-1 = end)
    std::vector<int32_t> kg_;      // condition index of each list entry
    int32_t ge_[8] = {};           // Ge: list base per system type (index -type)
    bool has_always_ = false;      // tC
    int32_t wb_ = 0;               // Wb
    bool kg_false_ = false;        // Kg
    int32_t hc_ = 0;               // Hc
    int32_t ff_ = -1, bi_ = -1;    // ff current / bi previous instance slot
    int32_t ok_ = -1, co_ = -1;    // Ok (oil entry index of the walk), Co
    bool pm_ = false, ac_ = false, kj_ = false, nk_ = false;
    int32_t mk_ = 0, ai_ = 0;      // Mk, ai
    int32_t lg_ = -1;              // Lg: current group
    int32_t pc_ = 0;               // pc
    int32_t iw_ = 0, jw_ = 0, hw_ = -1, tr_ = -1; // Iw Jw Hw Tr
    int32_t fs_ = 0;               // fs
    int32_t zr_ = 0;               // Zr
    bool pc_flag_ = false;         // pC
    bool do_ = false;              // Do
    std::vector<std::pair<int32_t, int32_t>> queue_; // Qk: (code, pc) for WH

    // expression evaluation
    std::vector<Value> ba_;        // Ba
    std::vector<int32_t> bm_;      // Bm (token index, -1 = end marker `as`)
    int32_t fa_ = 0, cd_ = 0;
    uint32_t rj_ = 0;              // first token of the expression being evaluated
    bool wf_ = false, hn_ = false;

    // frame start
    void init_frame();             // Mt + Uu + iw
    void create_start_objects();   // hu
    void prepare_program();        // jw + $p
    void reset_timers();           // tu
    int32_t create(int32_t handle, int32_t oi, double x, double y, int32_t dir, int32_t flags, int32_t layer,
                   int32_t slot); // su
    void link(Instance* a);        // bJ
    void unlink(Instance* a);      // aJ
    void destroy_slot(int32_t slot); // zq
    void request_destroy(int32_t slot); // jg
    void flush_destroys();         // eH
    int32_t random(int32_t n);     // random
    int32_t random_dir(int32_t mask); // Hu
    int32_t oil_of(int32_t oi) const; // Fn

    // objects
    void handle_object(Instance* a);
    void sprite_handle(Instance* a); // r.handle
    void sprite_hide(Instance* a);   // r.fo
    void sprite_show(Instance* a);   // r.Kv
    void move(Instance* a);          // B.move
    void move_static(Instance* a);
    void move_mouse(Instance* a);
    void move_path(Instance* a);
    bool path_step(Instance* a, int64_t amount); // tB
    void path_start_step(Instance* a, int32_t step);  // zk
    void path_start_back(Instance* a, int32_t step);  // Vl
    void path_speed(Instance* a);    // qB
    void path_events(Instance* a);   // We
    bool path_stop_at_end(Instance* a); // mr
    void path_stop(Instance* a);     // stop
    void path_reposition(Instance* a); // vB
    void movement_init(Instance* a, int32_t index, const ObjectDef* d, int32_t create_dir, int32_t type); // Ea.$
    bool collisions(Instance* a);    // Kf
    void anim_init(Instance* a);     // Ga.$
    void anim_reset(Instance* a, int32_t mode); // tA
    bool anim_step(Instance* a);     // Oe
    bool anim_save_box(Instance* a); // fn
    bool anim_update(Instance* a, int32_t mode); // sh
    bool anim_exists(const Instance* a, int32_t anim) const; // th
    void anim_force(Instance* a, int32_t anim); // Zp
    void anim_release(Instance* a); // rG
    void anim_force_speed(Instance* a, int32_t speed); // oG
    void anim_force_frame(Instance* a, int32_t frame); // nG
    void set_image_metrics(Instance* a, int32_t image); // Ii applied to K/J/ha/ia/Qu/Ru
    void image_box(int32_t image, double angle, double sx, double sy, double* w, double* h, double* hx, double* hy,
                   double* ax, double* ay) const; // Ii
    void set_x(Instance* a, double x); // h.bc
    void set_y(Instance* a, double y); // h.jc
    void set_dir(Instance* a, int32_t dir);
    void counter_set(Instance* a, double v); // mn
    void counter_note(Instance* a, double v); // gu
    void center_display(double x, double y, int32_t layer, int32_t flags); // eK
    void do_scroll();                 // doScroll -> bK, EK
    void display_add(Instance* a, int32_t index); // Lt / yl (index < 0: append)
    int32_t display_remove(Instance* a);           // xh: returns the old index
    void display_move(Instance* a, int32_t index); // be
    const Mask* mask_for(int32_t image, double angle, double sx, double sy); // ea.Af
    std::vector<Instance*> overlapping(Instance* a, int32_t image, double angle, double sx, double sy, double x,
                                       double y, int32_t only_oil); // am (only_oil < 0: every object)
    bool cond_overlap(Item& it);      // B.wA
    void counter_layout(Instance* k); // aa.hb
    int32_t counter_colour(const Param& p);
    bool cond_mouse_over(int32_t oil, bool negate); // eA
    void sound_reset();               // channels at frame start
    void sound_update();              // AG: retire or loop the samples that ended
    void sound_play(int32_t handle, int32_t loops, int32_t channel); // U.play
    void sound_stop_all();            // us
    bool sample_playing(int32_t handle); // nI
    bool any_playing();               // oI

    // events
    void run_list(int32_t start, Instance* trigger); // sf
    void run_actions(int32_t group);  // My
    void fire(int32_t code);          // ye
    bool fire_for(Instance* inst, int32_t code); // nd
    void special_lists();             // LG
    void run_queue();                 // WH
    Instance* sel_first(int32_t oil); // vf
    Instance* sel_next();             // Re
    void sel_drop();                  // Bd
    void sel_add(Instance* a);        // Il
    void sel_pick(int32_t oil, Instance* a); // sH
    Instance* sel_nth(int32_t oil, int32_t n); // dz
    Instance* expr_object(int32_t oil); // pk
    Instance* action_object(Item& it); // ab
    Instance* current_object(int32_t oil); // MH
    bool eval_condition(Item& it, Instance* trigger, bool triggered); // Ja / W
    void exec_action(Item& it);       // ka
    bool obj_filter_flag(Item& it, int which); // the `uf` filters

    // expressions
    Value eval_param(const Param& p); // Ta / ng
    Value get_expression();           // getExpression
    void eval_token(uint32_t t);      // evaluate
    double& pval(const Item& it, int param, int field);
    const Param& param(const Item& it, int i) const { return params_[it.first_param + uint32_t(i)]; }
    bool negated(const Item& it) const { return it.qe & 1; } // Q.Ij
    bool neg_result(const Item& it, bool b) const { return negated(it) ? !b : b; } // B.Iv
};

} // namespace ucn::game
