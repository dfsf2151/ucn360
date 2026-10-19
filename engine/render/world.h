#pragma once
#include <functional>
#include <string>
#include <vector>

#include "assets/image_bank.h"
#include "game/run.h"

namespace ucn::render {

struct WorldStats {
    int32_t drawn = 0, hidden = 0, texts_skipped = 0, counters_skipped = 0, missing_image = 0;
};

// Draws the running frame as the runtime does (`me` root -> layer containers gc, Ib, Pa in layer order 0..n,
// children in list order): the frame's backdrops, then each layer's display lists. Sprites go to
// (x - Ga, y - Ca) with the instance's angle and scale about the hotspot; alpha follows the ink rules of the
// runtime's Ie (alpha byte with r.Ns, (128 - param) / 128 for semi-transparency, additive for ink 9).
// Text objects need font rasterisation and are skipped (counted); counters draw their digit images.
// The output is the application window (1920x1080 logical, frames smaller than the display are centred, the scroll
// applied) times `scale`; `image` returns an image handle's pixels already at that scale (nullptr: skip).
using ImageGetter = std::function<const assets::Image*(int32_t handle)>;

// fonts.pak (tools/font_pack.py): the game's fonts as glyph alpha bitmaps, already at the render scale.
struct Glyph { float advance = 0; int16_t x = 0, y = 0; uint16_t w = 0, h = 0; const uint8_t* alpha = nullptr; };
struct Font { float size = 0, line = 0; Glyph glyphs[95]; };
class FontPack {
public:
    bool open(const std::string& path, std::string* error);
    const Font* font(int32_t handle) const;
private:
    std::vector<uint8_t> file_;
    std::vector<std::pair<int32_t, Font>> fonts_;
};

// A sprite in window pixels: the image's (hot_x, hot_y) lands on (x, y), scaled by (sx, sy) and turned by `angle` degrees
// about it; alpha is the canvas's globalAlpha, additive its "lighter", smooth its image smoothing.
struct Sprite {
    const assets::Image* img = nullptr; // its pixels on the CPU; the console's GPU reads only its size
    int32_t handle = 0;                 // the image handle (the GPU's texture)
    double x = 0, y = 0;
    double hot_x = 0, hot_y = 0;
    double angle = 0, sx = 1, sy = 1;
    double alpha = 1;
    bool additive = false;
    bool smooth = false;
};

// One thing to draw, in order: render_world draws the list on the CPU, the console on its GPU.
struct Draw {
    enum Kind : uint8_t { kSprite, kText, kBar } kind = kSprite;
    Sprite s; // kSprite
    // kText: `text` laid out in the box (x, y, w, h) by layout_text, `flags` its alignment, `color` 0xRRGGBB;
    // kBar: the box filled from `color` (its left, or top) to `color2`, down the box when `flags` is 1
    const Font* font = nullptr;
    std::string text;
    double x = 0, y = 0, w = 0, h = 0, alpha = 1;
    int32_t flags = 0, color = 0, color2 = 0;
};

// Text in a box as the runtime lays it out (wd.hb -> rs -> n.Dz / Fl): paragraphs on '\n', words wrapped greedily
// to the box width, lines f.line apart, flag 1 centres and 2 right-aligns each line, 4 centres and 8 bottom-aligns the
// block. Calls glyph(g, x, y) with each glyph's top-left pixel.
void layout_text(const Font& f, const std::string& text, double left, double top, double w, double h, int32_t flags,
                 const std::function<void(const Glyph&, int32_t, int32_t)>& glyph);

// The running frame's draws, in order, and its background colour (0xRRGGBB), as render_world draws them.
bool collect_world(const game::Run& run, const game::Defs& defs, const data::Bundle& bundle, const ImageGetter& image,
                   double scale, std::vector<Draw>* out, uint32_t* background, WorldStats* stats, std::string* error,
                   const FontPack* fonts = nullptr);

// Runs job(0) .. job(n - 1), each once, in any order or at once, and returns when all are done. With it,
// render_world draws its horizontal bands in parallel; without it, one after another.
using Parallel = std::function<void(int n, const std::function<void(int)>& job)>;

bool render_world(const game::Run& run, const game::Defs& defs, const data::Bundle& bundle, const ImageGetter& image,
                  double scale, assets::Image* out, WorldStats* stats, std::string* error,
                  const FontPack* fonts = nullptr, const Parallel* parallel = nullptr);

} // namespace ucn::render
