#include <cstdio>
#include <cstring>
#include <string>

#include "assets/image_bank.h"
#include "assets/png.h"
#include "data/bundle.h"
#include "render/renderer.h"
#include "render/world.h"
#include "render/band_pool.h"
#include "assets/image_pack.h"
#include "data/program.h"
#include "game/defs.h"
#include "game/run.h"
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace {

const char* anchor_name(ucn::render::Anchor a) {
    switch (a) {
    case ucn::render::Anchor::kHotspot: return "hotspot";
    case ucn::render::Anchor::kOff: return "off";
    default: return "corner";
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: frame_render BUNDLE.BIN FRAME out.png [--anchor=off|corner|hotspot]\n"
                     "                   [--layers=desc|asc] [--root ASSET_DIR] [--csv out.csv]\n"
                     "                   [--ticks=N [--seed=S]]\n"
                     "defaults: anchor=off layers=desc (the calibrated live behaviour)\n"
                     "--ticks=N runs the frame's events for N ticks (no input) and draws the world state instead\n");
        return 2;
    }
    const std::string bundle_path = argv[1];
    const uint32_t frame_index = uint32_t(std::atoi(argv[2]));
    const std::string out_png = argv[3];
    ucn::render::Options options;
    std::string root = "assets_raw/out";
    std::string csv_path;
    bool write_csv = false;
    int ticks = -1, seed = 12345;
    std::string pack_path, fonts_path;
    for (int i = 4; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--anchor=corner") options.anchor = ucn::render::Anchor::kCorner;
        else if (arg == "--anchor=hotspot") options.anchor = ucn::render::Anchor::kHotspot;
        else if (arg == "--anchor=off") options.anchor = ucn::render::Anchor::kOff;
        else if (arg == "--layers=desc") options.layer_order = ucn::render::LayerOrder::kDescending;
        else if (arg == "--layers=asc") options.layer_order = ucn::render::LayerOrder::kAscending;
        else if (arg.rfind("--root=", 0) == 0) root = arg.substr(7);
        else if (arg.rfind("--csv=", 0) == 0) { csv_path = arg.substr(6); write_csv = true; }
        else if (arg.rfind("--ticks=", 0) == 0) ticks = std::atoi(arg.c_str() + 8);
        else if (arg.rfind("--seed=", 0) == 0) seed = std::atoi(arg.c_str() + 7);
        else if (arg.rfind("--pack=", 0) == 0) pack_path = arg.substr(7); // draw from the console's images.pak
        else if (arg.rfind("--fonts=", 0) == 0) fonts_path = arg.substr(8); // tools/font_pack.py output
        else {
            std::fprintf(stderr, "unknown option: %s\n", arg.c_str());
            return 2;
        }
    }

    ucn::data::Bundle bundle;
    std::string error;
    if (!bundle.open(bundle_path, &error)) {
        std::fprintf(stderr, "bundle: %s\n", error.c_str());
        return 1;
    }
    ucn::assets::ImageBank bank;
    if (!bank.load(bundle, root, &error)) {
        std::fprintf(stderr, "image bank: %s\n", error.c_str());
        return 1;
    }

    if (ticks >= 0) {
        ucn::data::Program program;
        ucn::game::Defs defs;
        if (!program.load(bundle, &error) || !defs.load(bundle, &error)) {
            std::fprintf(stderr, "engine data: %s\n", error.c_str());
            return 1;
        }
        ucn::game::Run run(defs, program);
        run.set_seed(seed & 65535);
        run.set_alpha_source([&](int32_t im, const uint8_t** px, int32_t* w, int32_t* h) {
            std::string err;
            const ucn::assets::Image* c = bank.crop(uint32_t(im), &err);
            if (!c) return false;
            *px = c->rgba.data(); *w = int32_t(c->width); *h = int32_t(c->height);
            return true;
        });
        run.start_frame(int32_t(frame_index));
        for (int t = 1; t <= ticks; t++)
            if (run.tick(std::floor(t * 1000.0 / 60.0)) != 0) { std::fprintf(stderr, "frame ends at tick %d\n", t); break; }
        ucn::assets::Image img;
        ucn::render::WorldStats ws;
        ucn::render::ImageGetter get = [&](int32_t h) { std::string e; return bank.crop(uint32_t(h), &e); };
        double scale = 1;
        ucn::assets::ImagePack pack;
        if (!pack_path.empty()) {
            if (!pack.open(pack_path, &error)) { std::fprintf(stderr, "pack: %s\n", error.c_str()); return 1; }
            if (const char* b = std::getenv("UCN_CACHE_MB")) pack.budget_bytes = size_t(std::atoi(b)) << 20;
            get = [&](int32_t h) { return pack.get(h); };
            scale = pack.scale();
        }
        ucn::render::FontPack fonts;
        if (!fonts_path.empty() && !fonts.open(fonts_path, &error)) { std::fprintf(stderr, "fonts: %s\n", error.c_str()); return 1; }
        // UCN_THREADS=N: render the bands on N worker threads plus this one, as the console does
        const int threads = std::getenv("UCN_THREADS") ? std::atoi(std::getenv("UCN_THREADS")) : 0;
        ucn::render::BandPool pool(threads);
        const ucn::render::Parallel par = pool.parallel();
        const ucn::render::Parallel* parallel = threads > 0 ? &par : nullptr;
        if (const char* bench = std::getenv("UCN_BENCH")) { // UCN_BENCH=N: time N renders of this state
            const int n = std::atoi(bench);
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < n; i++) ucn::render::render_world(run, defs, bundle, get, scale, &img, &ws, &error, fonts_path.empty() ? nullptr : &fonts, parallel);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n;
            std::printf("render: %.2f ms per frame (%d runs)\n", ms, n);
        }
        if (!ucn::render::render_world(run, defs, bundle, get, scale, &img, &ws, &error, fonts_path.empty() ? nullptr : &fonts, parallel)) {
            std::fprintf(stderr, "render: %s\n", error.c_str());
            return 1;
        }
        if (!ucn::assets::png_write_file(out_png, img, &error)) { std::fprintf(stderr, "write: %s\n", error.c_str()); return 1; }
        std::printf("frame=%u tick=%d drawn=%d hidden=%d texts_skipped=%d counters_skipped=%d missing_image=%d\n",
                    frame_index, run.ticks(), ws.drawn, ws.hidden, ws.texts_skipped, ws.counters_skipped, ws.missing_image);
        return 0;
    }

    ucn::assets::Image image;
    ucn::render::Stats stats;
    if (!ucn::render::render_frame(bundle, bank, frame_index, options, &image, &stats, &error)) {
        std::fprintf(stderr, "render: %s\n", error.c_str());
        return 1;
    }
    if (!ucn::assets::png_write_file(out_png, image, &error)) {
        std::fprintf(stderr, "write: %s\n", error.c_str());
        return 1;
    }
    std::printf("frame=%s size=%dx%d instances=%u drawn=%u skipped_no_anim=%u missing_image=%u anchor=%s\n",
                stats.frame_name.c_str(), stats.frame_width, stats.frame_height, stats.instances,
                stats.drawn, stats.skipped_no_anim, stats.missing_image, anchor_name(options.anchor));

    if (write_csv) {
        FILE* f = std::fopen(csv_path.c_str(), "w");
        if (!f) {
            std::fprintf(stderr, "cannot write %s\n", csv_path.c_str());
            return 1;
        }
        std::fprintf(f, "index,object,x,y,layer,order,image,drawn\n");
        for (const ucn::render::DrawItem& it : stats.items)
            std::fprintf(f, "%u,%u,%d,%d,%u,%u,%u,%d\n", it.index, it.object, it.x, it.y, it.layer,
                         it.order, it.image, it.drawn ? 1 : 0);
        std::fclose(f);
        std::printf("csv -> %s\n", csv_path.c_str());
    }
    return 0;
}
