#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ucn::data { class Bundle; }

namespace ucn::game {

// Read-only definitions the interpreter runs on: the bundle's OBJRT, FRAMERT and IMAGES sections
// (layouts in tools/compile_bundle.py w_objrt / w_framert / w_images). Runtime names in comments.

struct ImageInfo {             // the runtime image record (ea): size, hotspot Ga/Ca, action point Zg/$g
    int32_t handle = -1;
    int32_t width = 0, height = 0;
    int32_t hot_x = 0, hot_y = 0;
    int32_t act_x = 0, act_y = 0;
};

struct AnimDir {               // Rd[dir]
    int32_t min_speed = 0;     // Jt
    int32_t max_speed = 0;     // It
    int32_t loops = 0;         // vy (0 = forever)
    int32_t back_to = 0;       // wy
    std::vector<int32_t> images; // Yj (xl = size)
};

struct Anim {                  // Ti.nf[anim]
    bool present = false;
    uint8_t rh[32] = {}, xi[32] = {}; // direction substitution tables (bit 64 = substituted)
    bool dir_present[32] = {};
    AnimDir dirs[32];
};

struct PathStep {              // bb[]
    int32_t speed = 0, dir = 0, dx = 0, dy = 0, cos = 0, sin = 0, length = 0, pause = 0;
    int32_t name = -1;         // string index
};

struct MoveDef {               // $e.Gd[]
    int32_t type = 0;          // Fv: 0 static, 1 mouse, 5 path
    int32_t player = 0;        // Xl
    int32_t moving_at_start = 0; // yB
    int32_t dirs = 0;          // xB: initial direction mask
    int32_t flags = 0;         // nr
    int32_t mouse_x1 = 0, mouse_x2 = 0, mouse_y1 = 0, mouse_y2 = 0; // ZA aB $A bB
    int32_t path_steps = 0;    // Wl
    int32_t min_speed = 0, max_speed = 0; // sB rB
    int32_t loop = 0, reposition = 0, reverse = 0; // Ev uB wB
    std::vector<PathStep> steps;
};

struct TextString { int32_t font = 0, flags = 0, color = 0, text = -1; };

struct ObjectDef {
    int32_t handle = -1;
    int32_t type = 0;          // Ee
    int32_t flags = 0;         // Rh
    int32_t ink = 0, ink_param = 0; // Pv, Qv
    int32_t oc_flags = 0;      // Ag (z.*)
    int32_t oc_vflags = 0;     // Vi
    std::string name;
    int32_t values_flags = 0;  // dm.Ya
    std::vector<int32_t> values;   // dm.values
    std::vector<int32_t> strings;  // lo: string indices
    int32_t anim_count = 0;    // Ti.mf (0 = no animations)
    std::vector<uint8_t> anim_exists; // Ti.wi
    std::vector<Anim> anims;
    std::vector<int32_t> anim_alias; // La.load: a missing animation shares a substitute's (same object)
    std::vector<MoveDef> moves;
    bool has_counter = false;
    int32_t counter_min = 0, counter_max = 0, counter_init = 0; // Wc kz jz iz
    bool has_display = false;  // jb
    int32_t disp_w = 0, disp_h = 0, disp_ov = 0, disp_type = 0, disp_flags = 0, disp_font = 0, disp_color1 = 0,
            disp_color2 = 0, disp_frame_count = 0, disp_fill = 0, disp_vertical = 0; // Oh Ph Ov af Bk Wi zg bm Yl Ui jo
    std::vector<int32_t> disp_frames;
    bool has_text = false;
    int32_t text_w = 0, text_h = 0, text_dk = 0; // bw cw Dk
    std::vector<TextString> texts;
};

struct FrameLayer { int32_t flags = 0; double x_coeff = 1, y_coeff = 1; }; // T: La, zj, Bj

struct FrameInstance { int32_t handle = 0, object = 0, x = 0, y = 0, create = 0, layer = 0, xq = 0; };

struct FrameDef {
    std::string name;
    int32_t flags = 0;         // Tb
    int32_t seed = -1;         // ir
    int32_t max_objects = 0;   // yg
    int32_t frame_rate = 60;   // GA
    int32_t virtual_right = 0, virtual_bottom = 0; // Vq
    int32_t layer_count = 0;   // wc
    int32_t display_w = 0, display_h = 0; // Pn, On: the display area (activation areas, centring)
    std::vector<FrameInstance> instances;
    std::vector<FrameLayer> layers;
};

struct Defs {
    std::vector<ObjectDef> objects;  // by handle
    int32_t sound_multiple = 1, sound_on = 1, sound_volume = 100; // kc.Rt, Cy, JA
    std::vector<double> sample_ms;   // decoded duration by sample handle (< 0 = not loaded)
    std::vector<FrameDef> frames;
    std::vector<ImageInfo> images;   // by image handle (handle -1 = no image)
    const data::Bundle* bundle = nullptr;

    bool load(const data::Bundle& bundle, std::string* error);
    const ObjectDef* object(int32_t handle) const {
        return handle >= 0 && size_t(handle) < objects.size() && objects[handle].handle == handle ? &objects[handle] : nullptr;
    }
    const ImageInfo* image(int32_t handle) const {
        return handle >= 0 && size_t(handle) < images.size() && images[handle].handle == handle ? &images[handle] : nullptr;
    }
    const std::string& str(int32_t index) const;
};

} // namespace ucn::game
