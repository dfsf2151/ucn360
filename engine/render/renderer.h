#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "assets/image_bank.h"
#include "data/bundle.h"

namespace ucn::render {

// Where an instance's (x, y) lands relative to the sprite's crop rect. Calibrated
// against the live game (M2): the crop's top-left goes to (x - off_x, y - off_y),
// i.e. the image's Ga/Ca bias (not the ctfak "hotspot"). See docs/render-semantics.md.
enum class Anchor : uint8_t { kCorner = 0, kHotspot = 1, kOff = 2 };

// Layers draw in descending index order (the last layer in the ccj is the bottom);
// inside one layer instances keep ascending creation order. Calibrated (M2).
enum class LayerOrder : uint8_t { kAscending = 0, kDescending = 1 };

struct Options {
    Anchor anchor = Anchor::kOff;
    LayerOrder layer_order = LayerOrder::kDescending;
    bool draw_background_color = true;
};

struct DrawItem {
    uint32_t index = 0; // instance index inside the frame record
    uint32_t object = 0;
    int32_t x = 0, y = 0;
    uint32_t layer = 0, order = 0;
    uint32_t image = 0; // resolved image handle (0 = none)
    bool drawn = false;
};

struct Stats {
    uint32_t instances = 0, drawn = 0, skipped_no_anim = 0, missing_image = 0;
    std::string frame_name;
    int32_t frame_width = 0, frame_height = 0;
    std::vector<DrawItem> items;
};

// Renders a frame record (background + every drawable instance) into an RGBA8 buffer.
// The "current image" rule v1 is the object's first animation, first direction, first
// frame; objects without animations are skipped and counted.
bool render_frame(const data::Bundle& bundle, assets::ImageBank& bank, uint32_t frame_index,
                  const Options& options, assets::Image* out, Stats* stats, std::string* error);

} // namespace ucn::render
