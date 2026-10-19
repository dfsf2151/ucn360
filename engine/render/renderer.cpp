#include "render/renderer.h"

#include <algorithm>
#include <map>

#include "data/reader.h"

namespace ucn::render {

namespace {

struct FirstImage {
    uint32_t handle = 0;
};

// Walks the OBJECTS section once and records each object's first animation image,
// falling back to the object's backdrop image (type-1 objects carry a static image).
std::map<uint32_t, FirstImage> build_first_images(const data::Bundle& bundle) {
    std::map<uint32_t, FirstImage> out;
    const uint32_t count = bundle.count(data::kObjects);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t at = bundle.record_offset(data::kObjects, i);
        if (at == 0) continue;
        data::Reader r{bundle.bytes(), at};
        const uint32_t handle = r.u32();
        r.u8();  // type
        r.u8();  // flags
        r.u32(); // name string index
        r.u8();  // ink effect
        r.u8();  // ink value
        r.u8();  // blend
        const uint32_t backdrop_image = r.u32();
        for (int k = 0; k < 4; ++k) r.i32(); // rgb coefficients
        const uint32_t value_count = r.u32();
        r.skip(size_t(value_count) * 4);
        const uint32_t anim_count = r.u32();
        FirstImage info;
        if (r.ok && anim_count > 0) {
            const uint32_t dir_count = r.u32();
            if (r.ok && dir_count > 0) {
                r.u8();   // dir index
                r.u8();   // repeat
                r.u16();  // reserved
                r.i32();  // min speed
                r.i32();  // max speed
                r.i32();  // back to
                const uint32_t frame_count = r.u32();
                if (r.ok && frame_count > 0) info.handle = r.u32();
            }
        }
        if (r.ok) {
            if (info.handle == 0) info.handle = backdrop_image;
            out[handle] = info;
        }
    }
    return out;
}

void blit(assets::Image* dst, const assets::Image& src, int32_t dx, int32_t dy) {
    const int32_t x0 = std::max<int32_t>(0, dx);
    const int32_t y0 = std::max<int32_t>(0, dy);
    const int32_t x1 = std::min<int32_t>(int32_t(dst->width), dx + int32_t(src.width));
    const int32_t y1 = std::min<int32_t>(int32_t(dst->height), dy + int32_t(src.height));
    if (x1 <= x0 || y1 <= y0) return;
    for (int32_t y = y0; y < y1; ++y) {
        const uint8_t* srow = src.rgba.data() + (size_t(y - dy) * src.width + (x0 - dx)) * 4;
        uint8_t* drow = dst->rgba.data() + (size_t(y) * dst->width + x0) * 4;
        for (int32_t x = x0; x < x1; ++x, srow += 4, drow += 4) {
            const uint32_t a = srow[3];
            if (a == 255) {
                std::copy(srow, srow + 4, drow);
            } else if (a != 0) {
                drow[0] = uint8_t((srow[0] * a + drow[0] * (255 - a)) / 255);
                drow[1] = uint8_t((srow[1] * a + drow[1] * (255 - a)) / 255);
                drow[2] = uint8_t((srow[2] * a + drow[2] * (255 - a)) / 255);
                const int da = int(drow[3]) + int(a);
                drow[3] = uint8_t(da > 255 ? 255 : da);
            }
        }
    }
}

} // namespace

bool render_frame(const data::Bundle& bundle, assets::ImageBank& bank, uint32_t frame_index,
                  const Options& options, assets::Image* out, Stats* stats, std::string* error) {
    auto fail = [&](const std::string& what) {
        if (error) *error = what;
        return false;
    };
    const uint32_t at = bundle.record_offset(data::kFrames, frame_index);
    if (at == 0) return fail("no such frame record");

    data::Reader r{bundle.bytes(), at};
    const uint32_t name_idx = r.u32();
    const int32_t width = r.i32();
    const int32_t height = r.i32();
    const uint32_t layer_count = r.u32();
    r.skip(size_t(layer_count) * 16);
    r.u32();  // bg mode
    r.u32();  // bg image
    r.i32();  // bg x
    r.i32();  // bg y
    const uint32_t bg_color = r.u32();
    const uint32_t instance_count = r.u32();
    if (!r.ok) return fail("frame record is truncated");
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
        return fail("bad frame dimensions");

    std::vector<DrawItem> items;
    items.reserve(instance_count);
    for (uint32_t i = 0; i < instance_count; ++i) {
        DrawItem item;
        item.index = i;
        item.object = r.u32();
        item.x = r.i32();
        item.y = r.i32();
        item.layer = r.u32();
        item.order = r.u32();
        r.u32(); // parent type
        r.u32(); // parent handle
        if (!r.ok) return fail("instance list is truncated");
        items.push_back(item);
    }

    const auto first_images = build_first_images(bundle);

    Stats local;
    local.frame_name = bundle.string_at(name_idx) ? *bundle.string_at(name_idx) : std::string();
    local.frame_width = width;
    local.frame_height = height;
    local.instances = instance_count;

    for (DrawItem& item : items) {
        auto it = first_images.find(item.object);
        if (it == first_images.end() || it->second.handle == 0) {
            local.skipped_no_anim++;
            continue;
        }
        item.image = it->second.handle;
    }

    std::stable_sort(items.begin(), items.end(), [&](const DrawItem& a, const DrawItem& b) {
        if (a.layer != b.layer) {
            return options.layer_order == LayerOrder::kAscending ? a.layer < b.layer : a.layer > b.layer;
        }
        // Inside one layer, instances keep creation order (first created draws first).
        return a.order < b.order;
    });

    out->width = uint32_t(width);
    out->height = uint32_t(height);
    const uint8_t bg_r = uint8_t(bg_color >> 16), bg_g = uint8_t(bg_color >> 8), bg_b = uint8_t(bg_color);
    const uint8_t bg_a = options.draw_background_color ? 255 : 0;
    out->rgba.assign(size_t(width) * height * 4, 0);
    for (size_t i = 0; i < out->rgba.size(); i += 4) {
        out->rgba[i] = bg_r;
        out->rgba[i + 1] = bg_g;
        out->rgba[i + 2] = bg_b;
        out->rgba[i + 3] = bg_a;
    }

    for (DrawItem& item : items) {
        if (item.image == 0) continue;
        std::string err;
        const assets::Image* src = bank.crop(item.image, &err);
        if (!src) {
            local.missing_image++;
            continue;
        }
        const assets::ImageRecord* rec = bank.record(item.image);
        int32_t dx = item.x, dy = item.y;
        if (rec) {
            switch (options.anchor) {
            case Anchor::kHotspot: dx -= rec->hotspot_x; dy -= rec->hotspot_y; break;
            case Anchor::kOff: dx -= rec->off_x; dy -= rec->off_y; break;
            case Anchor::kCorner: break;
            }
        }
        blit(out, *src, dx, dy);
        item.drawn = true;
        local.drawn++;
    }

    if (stats) {
        local.items = std::move(items);
        *stats = std::move(local);
    }
    return true;
}

} // namespace ucn::render
