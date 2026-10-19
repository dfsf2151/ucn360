#include "game/defs.h"

#include <cstring>

#include "data/bundle.h"
#include "data/reader.h"

namespace ucn::game {

using data::Reader;

namespace {
const std::string kEmpty;
}

const std::string& Defs::str(int32_t index) const {
    const std::string* s = index >= 0 && bundle ? bundle->string_at(uint32_t(index)) : nullptr;
    return s ? *s : kEmpty;
}

bool Defs::load(const data::Bundle& b, std::string* error) {
    bundle = &b;
    auto fail = [&](const std::string& what) {
        if (error) *error = what;
        return false;
    };

    // IMAGES: handle, file, crop x/y/w/h, src w/h, action point (named hotspot in the bundle), hotspot (off), flags.
    const uint32_t ni = b.count(data::kImages);
    for (uint32_t i = 0; i < ni; i++) {
        Reader r{b.bytes(), b.record_offset(data::kImages, i)};
        ImageInfo im;
        im.handle = int32_t(r.u32());
        r.u32();
        r.i32(); r.i32();
        im.width = r.i32(); im.height = r.i32();
        r.i32(); r.i32();
        im.act_x = r.i16(); im.act_y = r.i16();
        im.hot_x = r.i16(); im.hot_y = r.i16();
        if (!r.ok || im.handle < 0) return fail("bad image record");
        if (size_t(im.handle) >= images.size()) images.resize(size_t(im.handle) + 1);
        images[size_t(im.handle)] = im;
    }

    const uint32_t no = b.count(data::kObjRt);
    if (no == 0) return fail("no OBJRT section");
    objects.resize(no);
    for (uint32_t i = 0; i < no; i++) {
        Reader r{b.bytes(), b.record_offset(data::kObjRt, i)};
        ObjectDef& o = objects[i];
        o.handle = r.i32(); o.type = r.i32(); o.flags = r.i32(); o.ink = r.i32(); o.ink_param = r.i32();
        o.oc_flags = r.i32(); o.oc_vflags = r.i32();
        o.values_flags = r.i32();
        o.values.resize(size_t(r.i32()));
        for (auto& v : o.values) v = r.i32();
        o.strings.resize(size_t(r.i32()));
        for (auto& v : o.strings) v = r.i32();
        o.anim_count = r.i32();
        o.anim_exists.resize(size_t(o.anim_count));
        for (auto& v : o.anim_exists) v = r.u8();
        o.anims.resize(size_t(o.anim_count));
        for (auto& a : o.anims) {
            a.present = r.u8() != 0;
            if (!a.present) continue;
            for (auto& v : a.rh) v = r.u8();
            for (auto& v : a.xi) v = r.u8();
            for (int d = 0; d < 32; d++) {
                a.dir_present[d] = r.u8() != 0;
                if (!a.dir_present[d]) continue;
                AnimDir& dd = a.dirs[d];
                dd.min_speed = r.i32(); dd.max_speed = r.i32(); dd.loops = r.i32(); dd.back_to = r.i32();
                dd.images.resize(size_t(r.i32()));
                for (auto& v : dd.images) v = r.i32();
                if (!r.ok) return fail("bad animation record");
            }
        }
        // La.load 7308: missing animations point at a substitute (La.ZC for the first 12, else the first one
        // present), so switching between an animation and its substitute is no change at all.
        static const int32_t kZC[48] = {3, 1, 2, 0, 2, 0, 0, 0, 1, 0, 0, 0, 0, 1, 2, 0, 0, 0, 0, 0, 0, 1, 2, 0,
                                        0, 1, 2, 0, 1, 2, 0, 0, 0, 1, 2, 0, 1, 2, 0, 0, 0, 1, 2, 0, 0, 1, 2, 0};
        o.anim_alias.resize(size_t(o.anim_count));
        for (int32_t a = 0; a < o.anim_count; a++) {
            o.anim_alias[size_t(a)] = a;
            if (o.anim_exists[size_t(a)]) continue;
            bool found = false;
            if (a < 12)
                for (int d = 0; d < 4; d++) {
                    const int32_t t = kZC[4 * a + d];
                    if (t < o.anim_count && o.anim_exists[size_t(t)]) { o.anim_alias[size_t(a)] = t; found = true; break; }
                }
            if (!found)
                for (int32_t d = 0; d < o.anim_count; d++) if (o.anim_exists[size_t(d)]) { o.anim_alias[size_t(a)] = d; break; }
        }
        o.moves.resize(size_t(r.i32()));
        for (auto& m : o.moves) {
            m.type = r.i32(); m.player = r.i32(); m.moving_at_start = r.i32(); m.dirs = r.i32(); m.flags = r.i32();
            m.mouse_x1 = r.i32(); m.mouse_x2 = r.i32(); m.mouse_y1 = r.i32(); m.mouse_y2 = r.i32();
            m.path_steps = r.i32(); m.min_speed = r.i32(); m.max_speed = r.i32();
            m.loop = r.i32(); m.reposition = r.i32(); m.reverse = r.i32();
            m.steps.resize(size_t(r.i32()));
            for (auto& s : m.steps) {
                s.speed = r.i32(); s.dir = r.i32(); s.dx = r.i32(); s.dy = r.i32(); s.cos = r.i32(); s.sin = r.i32();
                s.length = r.i32(); s.pause = r.i32(); s.name = r.i32();
            }
            if (!r.ok) return fail("bad movement record");
        }
        o.has_counter = r.u8() != 0;
        if (o.has_counter) { o.counter_min = r.i32(); o.counter_max = r.i32(); o.counter_init = r.i32(); }
        o.has_display = r.u8() != 0;
        if (o.has_display) {
            o.disp_w = r.i32(); o.disp_h = r.i32(); o.disp_ov = r.i32(); o.disp_type = r.i32(); o.disp_flags = r.i32();
            o.disp_font = r.i32(); o.disp_color1 = r.i32(); o.disp_color2 = r.i32(); o.disp_frame_count = r.i32();
            o.disp_fill = r.i32(); o.disp_vertical = r.i32();
            o.disp_frames.resize(size_t(r.i32()));
            for (auto& v : o.disp_frames) v = r.i32();
        }
        o.has_text = r.u8() != 0;
        if (o.has_text) {
            o.text_w = r.i32(); o.text_h = r.i32(); o.text_dk = r.i32();
            o.texts.resize(size_t(r.i32()));
            for (auto& t : o.texts) { t.font = r.i32(); t.flags = r.i32(); t.color = r.i32(); t.text = r.i32(); }
        }
        if (!r.ok) return fail("bad OBJRT record " + std::to_string(i));
        if (o.handle != int32_t(i)) return fail("OBJRT not in handle order");

        // Name from the OBJECTS record: u32 handle, u8 type, u8 flags, u32 name.
        Reader on{b.bytes(), b.record_offset(data::kObjects, i)};
        on.u32(); on.u8(); on.u8();
        o.name = str(int32_t(on.u32()));
    }

    if (const data::Section* s = b.find(data::kSoundRt)) {
        Reader r{b.bytes(), s->offset};
        sound_multiple = r.i32(); sound_on = r.i32(); sound_volume = r.i32();
        const int32_t n = r.i32();
        for (int32_t i = 0; i < n; i++) {
            const int32_t h = r.i32();
            uint64_t bits = uint64_t(r.u32());
            bits |= uint64_t(r.u32()) << 32;
            double ms;
            std::memcpy(&ms, &bits, sizeof ms);
            if (h >= 0) { if (size_t(h) >= sample_ms.size()) sample_ms.resize(size_t(h) + 1, -1); sample_ms[size_t(h)] = ms; }
        }
        if (!r.ok) return fail("bad SOUNDRT section");
    }

    const uint32_t nf = b.count(data::kFrameRt);
    frames.resize(nf);
    for (uint32_t i = 0; i < nf; i++) {
        Reader r{b.bytes(), b.record_offset(data::kFrameRt, i)};
        FrameDef& f = frames[i];
        f.name = b.frame_name(i);
        f.flags = r.i32(); f.seed = r.i32(); f.max_objects = r.i32(); f.frame_rate = r.i32();
        f.virtual_right = r.i32(); f.virtual_bottom = r.i32(); f.layer_count = r.i32();
        f.display_w = r.i32(); f.display_h = r.i32();
        f.layers.resize(size_t(r.i32()));
        for (auto& l : f.layers) { l.flags = r.i32(); l.x_coeff = r.i32() / 65536.0; l.y_coeff = r.i32() / 65536.0; }
        f.instances.resize(size_t(r.i32()));
        for (auto& in : f.instances) {
            in.handle = r.i32(); in.object = r.i32(); in.x = r.i32(); in.y = r.i32(); in.create = r.i32();
            in.layer = r.i32(); in.xq = r.i32();
        }
        if (!r.ok) return fail("bad FRAMERT record");
    }
    return true;
}

} // namespace ucn::game
