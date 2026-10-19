#include <sstream>
#include <fstream>
// game_host BUNDLE.bin FRAME TICKS out.jsonl [--seed S] [--root assets_raw/out] [--masks masks.pak]
//           [--input "T:down:x:y,T:up,T:key:code:1|0"] [--follow 1] [--png GLOBAL_TICK:out.png]
// Runs FRAME for TICKS game ticks with no input and writes one state record per tick, in the shape
// tools/oracle_capture.js records from the live game (tools/compare_state.py diffs the two).
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "data/bundle.h"
#include "data/program.h"
#include "assets/image_bank.h"
#include "assets/png.h"
#include "render/world.h"
#include "game/defs.h"
#include "game/pad_nav.h"
#include "game/run.h"

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: game_host BUNDLE.bin FRAME TICKS out.jsonl [--seed S]\n");
        return 2;
    }
    int seed = 12345;
    std::string root = "assets_raw/out", masks, input, save;
    bool fixes = false;
    bool follow = false;
    std::string png;
    for (int i = 5; i + 1 < argc; i++) {
        if (!std::strcmp(argv[i], "--seed")) seed = std::atoi(argv[i + 1]);
        if (!std::strcmp(argv[i], "--root")) root = argv[i + 1];
        if (!std::strcmp(argv[i], "--masks")) masks = argv[i + 1]; // the console's masks.pak instead of PNG alpha
        if (!std::strcmp(argv[i], "--input")) input = argv[i + 1];  // tools/cdp_tick_oracle.js --input
        if (!std::strcmp(argv[i], "--follow")) follow = std::atoi(argv[i + 1]) != 0; // go on through frame changes
        if (!std::strcmp(argv[i], "--fixes")) fixes = std::atoi(argv[i + 1]) != 0; // Run::fixes, as the console runs
        if (!std::strcmp(argv[i], "--save")) save = argv[i + 1]; // a console ucn_save.dat to load first
        if (!std::strcmp(argv[i], "--png")) png = argv[i + 1]; // TICK:file, the world drawn after that (global) tick
    }
    ucn::data::Bundle bundle;
    std::string error;
    if (!bundle.open(argv[1], &error)) { std::fprintf(stderr, "bundle: %s\n", error.c_str()); return 1; }
    ucn::data::Program program;
    if (!program.load(bundle, &error)) { std::fprintf(stderr, "program: %s\n", error.c_str()); return 1; }
    ucn::game::Defs defs;
    if (!defs.load(bundle, &error)) { std::fprintf(stderr, "defs: %s\n", error.c_str()); return 1; }
    const int frame = std::atoi(argv[2]), ticks = std::atoi(argv[3]);
    if (frame < 0 || size_t(frame) >= defs.frames.size()) { std::fprintf(stderr, "no frame %d\n", frame); return 1; }

    ucn::assets::ImageBank bank;
    if (!bank.load(bundle, root, &error)) { std::fprintf(stderr, "images: %s\n", error.c_str()); return 1; }

    ucn::game::Run run(defs, program);
    ucn::game::MaskPack mask_pack;
    if (!masks.empty()) {
        if (!mask_pack.open(masks)) { std::fprintf(stderr, "cannot open %s\n", masks.c_str()); return 1; }
        run.set_mask_source([&](int32_t image, ucn::game::Mask* m) { return mask_pack.load(image, m); });
    } else run.set_alpha_source([&](int32_t image, const uint8_t** px, int32_t* w, int32_t* h) {
        std::string err;
        const ucn::assets::Image* im = bank.crop(uint32_t(image), &err);
        if (!im) { std::fprintf(stderr, "mask: image %d: %s\n", image, err.c_str()); return false; }
        *px = im->rgba.data(); *w = int32_t(im->width); *h = int32_t(im->height);
        return true;
    });
    run.set_seed(seed & 65535);
    if (!save.empty()) {
        std::ifstream f(save, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        run.ini_load_text(ss.str());
    }
    run.fixes = fixes;
    run.start_frame(frame);
    if (run.oil_mismatches()) std::fprintf(stderr, "warning: %d items resolve to a different OI-list index\n", run.oil_mismatches());
    FILE* out = std::fopen(argv[4], "w");
    if (!out) { std::fprintf(stderr, "cannot write %s\n", argv[4]); return 1; }
    int base = 0;
    double px = 960, py = 540; // the pointer, for snap // the tick the current frame started after (--follow)
    for (int t = 1; t <= ticks; t++) {
        for (size_t p = 0; p < input.size();) { // "T:down:x:y" / "T:up" / "T:key:code:1|0" before tick T
            size_t e = input.find(',', p);
            if (e == std::string::npos) e = input.size();
            int at = 0, a = 0, b = 0;
            char kind[8] = {};
            const std::string ev = input.substr(p, e - p);
            p = e + 1;
            // tick numbers count within the current frame, as the oracle's do
            if (std::sscanf(ev.c_str(), "%d:%7[a-z]:%d:%d", &at, kind, &a, &b) < 2 || at != t - base) continue;
            if (!std::strcmp(kind, "down") || !std::strcmp(kind, "move")) { px = a; py = b; }
            if (!std::strcmp(kind, "down")) { run.set_mouse(a, b); run.mouse_button(0, true); }
            else if (!std::strcmp(kind, "up")) run.mouse_button(0, false);
            else if (!std::strcmp(kind, "move")) run.set_mouse(a, b); // the pointer alone, as the pad's stick moves it
            else if (!std::strcmp(kind, "key")) run.set_key(a, b != 0);
            else if (!std::strcmp(kind, "snap") || !std::strcmp(kind, "grab")) { // the right stick: "T:snap:dx:dy" (grab: and press A)
                double x = 0, y = 0;
                const ucn::game::Instance* h = nullptr;
                if (ucn::game::target_step(run, px, py, a, b, &x, &y, &h)) {
                    px = x; py = y;
                    run.set_mouse(x, y);
                    if (!std::strcmp(kind, "grab")) run.mouse_button(0, true);
                }
                std::fprintf(stderr, "snap %d,%d at frame tick %d -> %.0f,%.0f %s\n", a, b, t - base, px, py, h && h->def ? h->def->name.c_str() : "(none)");
            }
            else if (!std::strcmp(kind, "list")) {
                const auto* fd = run.frame_def();
                const double ox = 960 - fd->display_w / 2.0 - run.scroll_x(), oy = 540 - fd->display_h / 2.0 - run.scroll_y();
                for (const auto* o : run.slots()) if (o && o->def && (o->def->name.rfind("buy", 0) == 0 || (o->col_flags & 256)))
                    std::fprintf(stderr, "  %-22s type %d col %d fl %d vis %d w %.0f h %.0f c %.0f,%.0f layer %d ink %d\n", o->def->name.c_str(), o->type, o->col_flags, o->flags, int(o->visible_list), o->w, o->h, o->x - o->hx + o->w / 2 + ox, o->y - o->hy + o->h / 2 + oy, o->sp.layer, o->sp.ink);
            }
            else if (!std::strcmp(kind, "cam")) { // "T:cam:dx:dy": the console's D-pad on the monitor (click; T:up after)
                double x = 0, y = 0;
                if (ucn::game::camera_step(run, a, b, &x, &y)) { run.set_mouse(x, y); run.mouse_button(0, true); }
                std::fprintf(stderr, "cam %d,%d at tick %d -> %.0f,%.0f\n", a, b, t, x, y);
            }
        }
        const int zb = run.tick(std::floor(double(t - base) * 1000.0 / 60.0)); // the oracle clock: 16/17/17 ms
        // The tick that ends the frame is not recorded: in the live game the frame change happens inside that
        // same loop pass, so its capture already shows the next frame.
        if (zb != 0) {
            std::fprintf(stderr, "tick %d: frame end requested (%d)\n", t, zb);
            if (!follow) break;
            int next = run.next_frame(zb);
            if (next < 0) next = 0;
            std::fprintf(stderr, "  -> frame %d\n", next);
            run.start_frame(next); // the console's loop: ticks restart with the frame; scenario ticks stay global
            base = t;
            continue;
        }
        std::fputs(run.capture_json().c_str(), out);
        std::fputc('\n', out);
        if (!png.empty() && std::atoi(png.c_str()) == t) {
            ucn::assets::Image img;
            const ucn::render::ImageGetter get = [&](int32_t h) { std::string e; return bank.crop(uint32_t(h), &e); };
            ucn::render::FontPack fp;
            const bool hf = fp.open("build/bundle/fonts_1x.pak", &error);
            ucn::render::render_world(run, defs, bundle, get, 1.0, &img, nullptr, &error, hf ? &fp : nullptr);
            if (std::getenv("UCN_BENCH")) { // time 10 more renders of this state (full size, one thread)
                const auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < 10; i++) ucn::render::render_world(run, defs, bundle, get, 1.0, &img, nullptr, &error, hf ? &fp : nullptr);
                std::fprintf(stderr, "render: %.2f ms per frame\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10);
            }
            ucn::assets::png_write_file(png.substr(png.find(':') + 1), img, &error);
        }
    }
    std::fclose(out);
    for (const auto& u : run.unported)
        std::fprintf(stderr, "unported code %d (type %d, num %d) met %d times\n", u.first, int16_t(uint32_t(u.first) & 0xFFFF),
                     int16_t(uint32_t(u.first) >> 16), u.second);
    return 0;
}
