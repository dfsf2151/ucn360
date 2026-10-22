// UCN on the Xbox 360: the engine the host checks against the live game (engine/game, engine/render), driven by the
// pad, drawn by the GPU (gpu_draw.cpp) into the 720p front buffer, mixed into XAudio. Files sit beside the .xex (game:\):
// bundle.bin, images.pak, masks.pak, sounds.pak; the save is ucn_save.dat, the log ucn360.log.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "data/bundle.h"
#include "data/program.h"
#include "game/defs.h"
#include "game/mask.h"
#include "game/pad_nav.h"
#include "game/run.h"
#include "render/world.h"
#include "gpu_draw.h"

extern "C" {
#include "xsdk/display.h"
#include "xsdk/gpu.h"
#include "xsdk/kernel.h"
#include "xsdk/log.h"
#include "xsdk/present.h"
#include "xsdk/thread.h"
}

namespace {

constexpr uint32_t kW = 1280, kH = 720;
constexpr double kScale = 2.0 / 3.0; // window 1920x1080 -> 1280x720 (images.pak is packed at this scale)

double now_ms() {
    uint64_t t = 0;
    KeQuerySystemTime(&t); // 100 ns units
    return double(t) / 10000.0;
}

bool read_file(const char* path, std::vector<uint8_t>* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    out->resize(size_t(f.tellg()));
    f.seekg(0);
    return bool(f.read(reinterpret_cast<char*>(out->data()), std::streamsize(out->size())));
}

// ---------------------------------------------------------------- audio
// The kernel's audio thread asks for 256 samples x 6 channels of big-endian floats at 48 kHz every 5.3 ms. The game
// thread publishes, per channel, which sample plays, a restart count and the volume; the callback owns playheads.

struct SoundRef { const uint8_t* data = nullptr; uint32_t len = 0; };
std::vector<uint8_t> g_sound_file;
std::vector<SoundRef> g_sounds; // by handle
uint32_t g_sound_rate = 22050;
float g_mulaw[256];

struct Wanted { volatile int32_t sample = -1; volatile uint32_t start = 0; volatile float volume = 1; };
Wanted g_want[32];
struct Voice { int32_t sample = -1; uint32_t start = 0; uint64_t pos = 0; }; // pos 32.32 in source samples
Voice g_voice[32];
uint32_t g_driver = 0;
float* g_frames = nullptr; // 2 frames, alternated
uint32_t g_frame_index = 0;
volatile bool g_paused = false; // the pause menu: silence, playheads held

enum { FRAME_CHANNELS = 6, CHANNEL_SAMPLES = 256, OUT_RATE = 48000 };

void build_mulaw() {
    for (int i = 0; i < 256; i++) { // G.711 decode
        const int u = ~i & 0xFF, sign = u & 0x80, exp = (u >> 4) & 7, man = u & 15;
        int v = ((man << 3) + 0x84) << exp;
        v -= 0x84;
        g_mulaw[i] = float(sign ? -v : v) / 32768.0f;
    }
}

} // namespace

extern "C" void ucn_audio_callback(const uint32_t* context);
extern "C" void ucn_audio_callback(const uint32_t*) {
    const uint32_t driver = __atomic_load_n(&g_driver, __ATOMIC_ACQUIRE);
    if (!driver) return;
    float* f = g_frames + (g_frame_index++ & 1) * FRAME_CHANNELS * CHANNEL_SAMPLES;
    static float mix[CHANNEL_SAMPLES];
    for (float& m : mix) m = 0;
    if (g_paused) {
        std::memset(f, 0, FRAME_CHANNELS * CHANNEL_SAMPLES * sizeof(float));
        (void)XAudioSubmitRenderDriverFrame(driver, f);
        return;
    }
    const uint64_t step = (uint64_t(g_sound_rate) << 32) / OUT_RATE;
    for (int c = 0; c < 32; c++) {
        Voice& v = g_voice[c];
        const int32_t want = g_want[c].sample;
        const uint32_t start = g_want[c].start;
        if (want != v.sample || start != v.start) { v.sample = want; v.start = start; v.pos = 0; }
        if (v.sample < 0 || size_t(v.sample) >= g_sounds.size()) continue;
        const SoundRef& s = g_sounds[size_t(v.sample)];
        const float vol = g_want[c].volume * 0.5f;
        for (int i = 0; i < CHANNEL_SAMPLES; i++) {
            const uint32_t idx = uint32_t(v.pos >> 32);
            if (idx >= s.len) { v.sample = -1; break; } // the run restarts it (start count) when it loops
            mix[i] += g_mulaw[s.data[idx]] * vol;
            v.pos += step;
        }
    }
    for (int i = 0; i < CHANNEL_SAMPLES; i++) {
        const float m = mix[i] > 1 ? 1 : mix[i] < -1 ? -1 : mix[i];
        for (int ch = 0; ch < FRAME_CHANNELS; ch++) f[ch * CHANNEL_SAMPLES + i] = ch < 2 ? m : 0.0f;
    }
    (void)XAudioSubmitRenderDriverFrame(driver, f);
}

namespace {

bool start_audio() {
    build_mulaw();
    if (!read_file("sounds.pak", &g_sound_file) || g_sound_file.size() < 16 || std::memcmp(g_sound_file.data(), "UCNS", 4))
        return false;
    auto le32 = [](const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; };
    const uint32_t n = le32(&g_sound_file[8]);
    g_sound_rate = le32(&g_sound_file[12]);
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t* t = &g_sound_file[16 + size_t(i) * 12];
        const uint32_t h = le32(t), off = le32(t + 4), len = le32(t + 8);
        if (size_t(off) + len > g_sound_file.size()) continue;
        if (h >= g_sounds.size()) g_sounds.resize(h + 1);
        g_sounds[h] = SoundRef{&g_sound_file[off], len};
    }
    g_frames = static_cast<float*>(MmAllocatePhysicalMemoryEx(0, 2 * FRAME_CHANNELS * CHANNEL_SAMPLES * sizeof(float),
                                                               PAGE_READWRITE, 0, UINT32_MAX, 0));
    if (!g_frames) return false;
    std::memset(g_frames, 0, 2 * FRAME_CHANNELS * CHANNEL_SAMPLES * sizeof(float));
    const XAUDIO_RENDER_DRIVER_CLIENT client = {.callback = ucn_audio_callback, .context = 0};
    uint32_t registered = 0;
    if (XAudioRegisterRenderDriverClient(&client, &registered) != ERROR_SUCCESS) return false;
    __atomic_store_n(&g_driver, registered, __ATOMIC_RELEASE);
    return true;
}

void publish_sound(const ucn::game::Run& run) {
    for (int c = 0; c < 32; c++) {
        g_want[c].volume = float(run.channel_volume(c) / 100.0);
        g_want[c].sample = run.channel_sample(c);
        g_want[c].start = run.channel_start(c);
    }
}

// ---------------------------------------------------------------- pad
// From the game's own control map (frame 6): D-pad = A/D/W/F (left door, right door, front vent, side vent),
// Y = S (monitor), B = Z (flashlight), X = space (desk fan), RB = C (catch fish), LB / Start = Enter, Back = Esc,
// RT = X (all off); LT held turns D-pad up/right/down/left and X/Y into 1-6. Left stick = pointer, A = click.

struct PadMap { uint16_t button; int32_t key; int32_t lt_key; };
const PadMap kPad[] = {
    {INPUT_GAMEPAD_DPAD_LEFT, 65, 52}, {INPUT_GAMEPAD_DPAD_RIGHT, 68, 50}, {INPUT_GAMEPAD_DPAD_UP, 87, 49},
    {INPUT_GAMEPAD_DPAD_DOWN, 70, 51}, {INPUT_GAMEPAD_Y, 83, 54}, {INPUT_GAMEPAD_B, 90, 0},
    {INPUT_GAMEPAD_X, 32, 53}, {INPUT_GAMEPAD_RIGHT_SHOULDER, 67, 0}, {INPUT_GAMEPAD_LEFT_SHOULDER, 13, 0},
    {INPUT_GAMEPAD_START, 13, 0}, {INPUT_GAMEPAD_BACK, 27, 0},
};

struct Pad {
    double mx = 960, my = 540;
    bool keys[256] = {};
    bool click = false;
    double idle_ms = 1e9; // since the stick or A last moved; the pointer hides after 3 s of it
    double erase_ms = 0;  // LT + RT held on the menu; at kEraseMs it holds DEL, the game's own "erase data"
    static constexpr double kEraseMs = 3000;
    uint16_t last_buttons = 0;
    bool guide = false; // the control map over the game, R3 toggling it
    double release_ms = -1; // a camera click from the D-pad, let go after this long
    double arrow_x = -1, arrow_y = -1; // a portrait arrow click's pointer, put back once it is let go
    bool rs_armed = true;   // the right stick jumps the pointer once per flick (back to the centre re-arms it)
    // Start in the office: ticks and sound stop and the pad drives the pause menu (Resume, Controls, Quit to menu)
    bool paused = false, controls = false;
    int sel = 0;
    double quit_ms = 0; // Quit holds Esc this long, the office's own "hold Esc to return to the menu" (500 ms)

    void pause(ucn::game::Run& run) {
        paused = true; controls = false; sel = 0;
        for (int k = 0; k < 256; k++) if (keys[k]) { keys[k] = false; run.set_key(k, false); }
        if (click) { click = false; run.mouse_button(0, false); }
    }
    void pause_input(uint16_t pressed) {
        if (controls) { if (pressed & (INPUT_GAMEPAD_A | INPUT_GAMEPAD_B | INPUT_GAMEPAD_START | INPUT_GAMEPAD_RIGHT_THUMB)) controls = false; return; }
        if (pressed & INPUT_GAMEPAD_DPAD_UP) sel = (sel + 2) % 3;
        if (pressed & INPUT_GAMEPAD_DPAD_DOWN) sel = (sel + 1) % 3;
        if (pressed & (INPUT_GAMEPAD_B | INPUT_GAMEPAD_START)) paused = false;
        else if (pressed & INPUT_GAMEPAD_A) {
            if (sel == 1) controls = true;
            else { paused = false; if (sel == 2) quit_ms = 700; }
        }
    }

    void arrow(ucn::game::Run& run, bool up) {
        double ax = 0, ay = 0;
        if (release_ms >= 0 || !ucn::game::portrait_arrow(run, mx, my, up, &ax, &ay)) return;
        arrow_x = mx; arrow_y = my;
        run.set_mouse(std::floor(ax), std::floor(ay));
        run.mouse_button(0, true);
        release_ms = 50;
    }

    void update(ucn::game::Run& run, double dt_ms) {
        INPUT_STATE st;
        std::memset(&st, 0, sizeof st);
        if (XamInputGetState(0, 0, &st) != ERROR_SUCCESS) std::memset(&st, 0, sizeof st); // no pad: nothing held
        const INPUT_GAMEPAD& g = st.gamepad;
        auto axis = [](int v) { const int dz = 7849; if (v > -dz && v < dz) return 0.0; return (v > 0 ? v - dz : v + dz) / 24918.0; };
        const uint16_t pressed = g.buttons & ~last_buttons;
        last_buttons = g.buttons;
        if (paused) { pause_input(pressed); return; }
        if ((pressed & INPUT_GAMEPAD_START) && run.frame() == 2) { pause(run); return; }
        const double sx = axis(g.thumb_lx), sy = axis(g.thumb_ly);
        idle_ms = sx != 0 || sy != 0 || (g.buttons & INPUT_GAMEPAD_A) ? 0 : idle_ms + dt_ms;
        // Right stick flicked: jump to the nearest clickable thing that way (coins, the parrot, the shop's BUY)
        const double rx = g.thumb_rx / 32767.0, ry = g.thumb_ry / 32767.0, rmag = std::sqrt(rx * rx + ry * ry);
        if (rmag < 0.3) rs_armed = true;
        else if (rmag > 0.7 && rs_armed) {
            rs_armed = false;
            double x = 0, y = 0;
            if (ucn::game::target_step(run, mx, my, rx / rmag, -ry / rmag, &x, &y)) { mx = x; my = y; idle_ms = 0; }
        }
        const double speed = 1500.0 * dt_ms / 1000.0; // window px per second at full tilt
        mx = std::min(1919.0, std::max(0.0, mx + sx * std::fabs(sx) * speed));
        my = std::min(1079.0, std::max(0.0, my - sy * std::fabs(sy) * speed));
        if (arrow_x < 0) run.set_mouse(std::floor(mx), std::floor(my)); // held on a portrait arrow till it is let go
        const bool lt = g.left_trigger > 64;
        // With the monitor up, the D-pad walks the camera map: point at the next camera that way and click it.
        const uint16_t dpad = INPUT_GAMEPAD_DPAD_UP | INPUT_GAMEPAD_DPAD_DOWN | INPUT_GAMEPAD_DPAD_LEFT | INPUT_GAMEPAD_DPAD_RIGHT;
        const bool cams = !lt && ucn::game::monitor_up(run);
        if (pressed & INPUT_GAMEPAD_RIGHT_THUMB) guide = !guide;
        if (release_ms >= 0 && (release_ms -= dt_ms) < 0) {
            run.mouse_button(0, (g.buttons & INPUT_GAMEPAD_A) != 0);
            if (arrow_x >= 0) { run.set_mouse(std::floor(arrow_x), std::floor(arrow_y)); arrow_x = arrow_y = -1; } // back on the portrait
        }
        // On the menu, D-pad up / down over a portrait clicks its arrow: the AI level up or down by one
        if (pressed & (INPUT_GAMEPAD_DPAD_UP | INPUT_GAMEPAD_DPAD_DOWN)) arrow(run, (pressed & INPUT_GAMEPAD_DPAD_UP) != 0);
        if (cams && (pressed & dpad) && release_ms < 0) {
            const int dx = (pressed & INPUT_GAMEPAD_DPAD_RIGHT) ? 1 : (pressed & INPUT_GAMEPAD_DPAD_LEFT) ? -1 : 0;
            const int dy = (pressed & INPUT_GAMEPAD_DPAD_DOWN) ? 1 : (pressed & INPUT_GAMEPAD_DPAD_UP) ? -1 : 0;
            double x = 0, y = 0;
            if (ucn::game::camera_step(run, dx, dy, &x, &y)) {
                mx = x; my = y;
                run.set_mouse(std::floor(mx), std::floor(my));
                run.mouse_button(0, true);
                release_ms = 50;
                idle_ms = 0;
            }
        }
        bool want[256] = {};
        for (const PadMap& m : kPad)
            if ((g.buttons & m.button) && !(cams && (m.button & dpad)) && !(m.button == INPUT_GAMEPAD_START && run.frame() == 2))
                want[lt && m.lt_key ? m.lt_key : m.key] = true;
        if (quit_ms > 0) { quit_ms -= dt_ms; want[27] = true; }
        const bool rt = g.right_trigger > 64;
        erase_ms = lt && rt && run.frame() == 1 ? erase_ms + dt_ms : 0;
        if (erase_ms >= kEraseMs) want[46] = true;
        else if (rt && !lt) want[88] = true;
        for (int k = 0; k < 256; k++)
            if (want[k] != keys[k]) { keys[k] = want[k]; run.set_key(k, want[k]); }
        const bool a = (g.buttons & INPUT_GAMEPAD_A) != 0;
        if (a != click && release_ms < 0) { click = a; run.mouse_button(0, a); }
    }
};

// Overlays, drawn after the game's own draws.
using ucn::render::Draw;
void push_rect(std::vector<Draw>& out, double x, double y, double w, double h, int32_t color, double alpha) {
    Draw d;
    d.kind = Draw::kBar;
    d.x = x; d.y = y; d.w = w; d.h = h; d.color = d.color2 = color; d.alpha = alpha;
    out.push_back(d);
}
void push_image(std::vector<Draw>& out, const GpuDraw& gpu, int32_t handle, double x, double y) {
    Draw d;
    d.s.img = gpu.image(handle);
    d.s.handle = handle;
    d.s.x = x; d.s.y = y;
    if (d.s.img) out.push_back(d);
}

// The erase timer: a bar filling over kEraseMs in the middle of the screen, red once the data is gone.
void push_erase_bar(std::vector<Draw>& out, double fraction) {
    const int w = 400, h = 24, x0 = int(kW) / 2 - w / 2, y0 = int(kH) / 2 - h / 2;
    const int fill = int(std::min(1.0, fraction) * (w - 8));
    push_rect(out, x0, y0, w, h, 0xFFFFFF, 1);
    push_rect(out, x0 + 2, y0 + 2, w - 4, h - 4, 0x000000, 1);
    push_rect(out, x0 + 4, y0 + 4, fill, h - 8, fraction >= 1 ? 0xDC1E1E : 0xDCDCDC, 1);
}

// The pointer: an arrow, black edged, white inside, its tip at the pointer.
ucn::assets::Image cursor_image() {
    static const char* kArrow[] = {
        "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ",
        "X.....X     ", "X......X    ", "X.......X   ", "X........X  ", "X.....XXXXX ", "X..X..X     ",
        "X.X X..X    ", "XX  X..X    ", "X    X..X   ", "     XXXX   "};
    ucn::assets::Image img;
    img.width = 12;
    img.height = 16;
    img.rgba.assign(12 * 16 * 4, 0);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 12; x++) {
            const char c = kArrow[y][x];
            uint8_t* d = &img.rgba[size_t(y * 12 + x) * 4];
            d[0] = d[1] = d[2] = c == '.' ? 255 : 0;
            d[3] = c == ' ' ? 0 : 255;
        }
    return img;
}

// The pad's control map (frame 6's backdrop, image 1802, packed at the screen's size) over the game, R3 toggling it:
// its text and buttons drawn as they are over the game dimmed to 2/5, its black left out.
ucn::assets::Image guide_image(const GpuDraw& gpu) {
    ucn::assets::Image img;
    if (!gpu.decode(1802, &img)) return img;
    for (size_t i = 0; i < img.rgba.size(); i += 4) img.rgba[i + 3] = img.rgba[i] + img.rgba[i + 1] + img.rgba[i + 2] > 120 ? 255 : 0;
    return img;
}
void push_guide(std::vector<Draw>& out, const GpuDraw& gpu, int32_t guide) {
    push_rect(out, 0, 0, kW, kH, 0x000000, 0.6);
    push_image(out, gpu, guide, 0, 0);
}

// The pause menu (tools/pause_menu.py, item centres kPauseItemY) over the game at half brightness, a box round the
// selection.
constexpr int kPauseItemY[3] = {330, 400, 470};
void push_pause(std::vector<Draw>& out, const GpuDraw& gpu, int32_t panel, int sel) {
    push_rect(out, 0, 0, kW, kH, 0x000000, 0.5);
    push_image(out, gpu, panel, 0, 0);
    const int y0 = kPauseItemY[sel] - 28, y1 = kPauseItemY[sel] + 28, x0 = 470, x1 = 810;
    push_rect(out, x0, y0, x1 - x0, 3, 0xC81E1E, 1);
    push_rect(out, x0, y1 - 3, x1 - x0, 3, 0xC81E1E, 1);
    push_rect(out, x0, y0 + 3, 3, y1 - y0 - 6, 0xC81E1E, 1);
    push_rect(out, x1 - 3, y0 + 3, 3, y1 - y0 - 6, 0xC81E1E, 1);
}

// The loading screen, from the CPU straight into the tiled front buffer, while the GPU's textures load.
void present_cpu(const ucn::assets::Image& img) {
    uint32_t address = 0;
    if (!NT_SUCCESS(xsdk_present_begin(&address, 1000))) return;
    volatile uint32_t* px = xsdk_present_pixels(address);
    for (uint32_t y = 0; y < std::min(kH, img.height); y++)
        for (uint32_t x = 0; x < std::min(kW, img.width); x++) {
            const uint8_t* p = &img.rgba[(size_t(y) * img.width + x) * 4];
            px[tiled_index(x, y, kW)] = uint32_t(p[2]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[0]) << 8 | 0xFF; // B,G,R,A
        }
    __asm__ volatile("sync" ::: "memory");
    xsdk_present_end();
}

void save(const ucn::game::Run& run) {
    const std::string t = run.ini_save_text();
    for (const char* path : {"ucn_save.dat", "cache:\\ucn_save.dat"}) {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (f.write(t.data(), std::streamsize(t.size())) && f.flush()) return;
    }
}

} // namespace

int main() {
    xsdk_log_write("ucn360: start");
    ucn::data::Bundle bundle;
    std::string error;
    ucn::data::Program program;
    ucn::game::Defs defs;
    if (!bundle.open("bundle.bin", &error) || !program.load(bundle, &error) || !defs.load(bundle, &error)) {
        xsdk_log_write("ucn360: FAIL bundle: %s", error.c_str());
        return 1;
    }
    if (!NT_SUCCESS(xsdk_gpu_open())) { xsdk_log_write("ucn360: FAIL gpu open"); return 1; }
    uint32_t width = 0, height = 0;
    const bool display = NT_SUCCESS(xsdk_present_open(XSDK_DISPLAY_720P, 2, 1, &width, &height));
    xsdk_log_write("ucn360: present %s %ux%u", display ? "open" : "absent", unsigned(width), unsigned(height));
    if (display) { // the loading screen (tools/loading_screen.py) while the images, sounds and masks load
        ucn::assets::Image loading;
        loading.width = kW;
        loading.height = kH;
        loading.rgba.resize(size_t(kW) * kH * 4);
        std::ifstream("loading.rgba", std::ios::binary).read(reinterpret_cast<char*>(loading.rgba.data()), std::streamsize(loading.rgba.size()));
        present_cpu(loading);
    }
    GpuDraw gpu;
    const double load_start = now_ms();
    if (!gpu.open("images.pak", &error)) { xsdk_log_write("ucn360: FAIL images: %s", error.c_str()); return 1; }
    xsdk_log_write("ucn360: images on the GPU in %.0f ms", now_ms() - load_start);

    ucn::render::FontPack fonts;
    const bool have_fonts = fonts.open("fonts.pak", &error);
    ucn::game::MaskPack masks;
    const bool have_masks = masks.open("masks.pak");
    const bool have_audio = start_audio();
    xsdk_log_write("ucn360: data loaded (%u frames, masks %d, audio %d)", unsigned(defs.frames.size()), int(have_masks),
                   int(have_audio));

    ucn::game::Run run(defs, program);
    run.fixes = true; // the game's intent over the HTML5 export's slips (Run::fixes)
    if (have_masks) run.set_mask_source([&](int32_t image, ucn::game::Mask* m) { return masks.load(image, m); });
    for (const char* path : {"ucn_save.dat", "cache:\\ucn_save.dat"}) { // the first that holds data
        std::ifstream f(path, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        if (!ss.str().empty()) { run.ini_load_text(ss.str()); break; }
    }
    // The volume / warning intro (frame 0) skips itself once the save says "adjust"; the console shows it every boot.
    for (auto& file : run.ini)
        for (auto& group : file.second) group.second.erase("adjust");
    int32_t first = 0; // debug: a `start_frame` file beside the .xex holding a frame number starts there
    std::ifstream("start_frame") >> first;
    run.set_clock(uint32_t(now_ms())); // each frame's random numbers start from the time, as the runtime's do
    run.start_frame(first >= 0 && first < int32_t(defs.frames.size()) ? first : 0);

    Pad pad;
    ucn::assets::Image panel;
    panel.width = kW;
    panel.height = kH;
    panel.rgba.resize(size_t(kW) * kH * 4);
    std::ifstream("pause.rgba", std::ios::binary).read(reinterpret_cast<char*>(panel.rgba.data()), std::streamsize(panel.rgba.size()));
    const int32_t pause_tex = gpu.add(panel), guide_tex = gpu.add(guide_image(gpu)), cursor_tex = gpu.add(cursor_image());
    std::vector<Draw> draws;
    const ucn::render::ImageGetter get = [&](int32_t h) { return gpu.image(h); };
    double frame_start = now_ms(), last = frame_start, last_save = frame_start, last_log = frame_start;
    int32_t tick = 0;
    uint32_t frames = 0;
    double spent[3] = {}; // ms in the ticks, laying the draws out, and handing them to the GPU (with the vsync wait)
    // debug: a `dump_frame` file asks for RGBA dumps at the ticks it lists (300 if empty), frame_dump_TICK.rgba
    std::vector<int32_t> dumps;
    if (std::ifstream d{"dump_frame"}) {
        for (int32_t t; d >> t;) dumps.push_back(t);
        if (dumps.empty()) dumps.push_back(300);
    }
    // debug: an `input_script` file replays game_host's --input ("T:down:x:y,T:up,T:move:x:y,T:key:code:1|0") at the
    // frame's own ticks, so a pad scenario can be checked in Xenia without a pad
    std::string script;
    std::getline(std::ifstream("input_script"), script);
    for (;;) {
        const double now = now_ms();
        double t0 = now;
        pad.update(run, now - last);
        last = now;
        // fixed 60 Hz: tick t runs at floor(t * 1000 / 60) ms of frame time, at most 4 behind
        int ran = 0;
        g_paused = pad.paused;
        if (pad.paused) frame_start = now - std::floor(tick * 1000.0 / 60.0); // the frame's clock waits too
        while (!pad.paused && ran < 4 && now - frame_start >= std::floor((tick + 1) * 1000.0 / 60.0)) {
            tick++;
            ran++;
            for (size_t p = 0; p < script.size();) {
                size_t e = script.find(',', p);
                if (e == std::string::npos) e = script.size();
                int at = 0, x = 0, y = 0;
                char kind[8] = {};
                const std::string ev = script.substr(p, e - p);
                p = e + 1;
                if (std::sscanf(ev.c_str(), "%d:%7[a-z]:%d:%d", &at, kind, &x, &y) < 2 || at != tick) continue;
                if (!std::strcmp(kind, "down") || !std::strcmp(kind, "move")) { pad.mx = x; pad.my = y; run.set_mouse(x, y); }
                if (!std::strcmp(kind, "down")) run.mouse_button(0, true);
                else if (!std::strcmp(kind, "up")) run.mouse_button(0, false);
                else if (!std::strcmp(kind, "key")) run.set_key(x, y != 0);
                else if (!std::strcmp(kind, "guide")) pad.guide = !pad.guide; // R3
                else if (!std::strcmp(kind, "arrow")) pad.arrow(run, x != 0); // D-pad up (1) / down (0) on the menu
                else if (!std::strcmp(kind, "pause")) pad.pause(run); // Start in the office
                else if (!std::strcmp(kind, "snap")) { // the right stick
                    double tx = 0, ty = 0;
                    if (ucn::game::target_step(run, pad.mx, pad.my, x, y, &tx, &ty)) { pad.mx = tx; pad.my = ty; run.set_mouse(tx, ty); }
                }
                xsdk_log_write("ucn360: script %s at frame %d tick %d", ev.c_str(), int(run.frame()), int(tick));
            }
            const int32_t zb = run.tick(std::floor(tick * 1000.0 / 60.0));
            if (zb != 0) {
                int32_t next = run.next_frame(zb);
                if (next < 0) next = 0;
                xsdk_log_write("ucn360: frame %d -> %d", int(run.frame()), int(next));
                run.set_clock(uint32_t(now_ms()));
                run.start_frame(next);
                tick = 0;
                frame_start = now_ms();
                break;
            }
        }
        if (ran == 4) frame_start = now - std::floor(tick * 1000.0 / 60.0); // too slow: drop the backlog
        if (have_audio) publish_sound(run);
        double t1 = now_ms();
        spent[0] += t1 - t0;

        uint32_t background = 0;
        ucn::render::collect_world(run, defs, bundle, get, kScale, &draws, &background, nullptr, &error,
                                   have_fonts ? &fonts : nullptr);
        if (pad.guide || (pad.paused && pad.controls)) push_guide(draws, gpu, guide_tex);
        if (pad.paused && !pad.controls) push_pause(draws, gpu, pause_tex, pad.sel);
        if (pad.erase_ms > 0) push_erase_bar(draws, pad.erase_ms / Pad::kEraseMs);
        // Hidden when idle only where nothing needs it: on the menu and in the office an unseen pointer would still click
        const bool pointer_screen = run.frame() == 1 || run.frame() == 2;
        if ((pad.idle_ms < 3000 || pointer_screen) && !pad.paused)
            push_image(draws, gpu, cursor_tex, std::floor(pad.mx * kScale), std::floor(pad.my * kScale));
        t0 = now_ms();
        spent[1] += t0 - t1;
        if (display) gpu.present(draws, background);
        spent[2] += now_ms() - t0;
        if (!dumps.empty() && tick >= dumps.front()) { // debug: `dump_frame` beside the .xex
            std::vector<uint8_t> rgba;
            gpu.read_back(&rgba);
            const std::string path = "cache:\\frame_dump_" + std::to_string(dumps.front()) + ".rgba"; // Xenia: game: is read-only
            std::ofstream(path, std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char*>(rgba.data()), std::streamsize(rgba.size()));
            xsdk_log_write("ucn360: dumped frame %d tick %d to %s", int(run.frame()), int(tick), path.c_str());
            dumps.erase(dumps.begin());
        }
        frames++;
        if (run.ini_dirty && now - last_save > 1000) { run.ini_dirty = false; save(run); last_save = now; }
        if (now - last_log > 5000) {
            xsdk_log_write("ucn360: frame %d tick %d, %u draws in 5 s (ms a draw: ticks %.1f, layout %.1f, gpu %.1f)",
                           int(run.frame()), int(tick), unsigned(frames), spent[0] / frames, spent[1] / frames,
                           spent[2] / frames);
            frames = 0;
            spent[0] = spent[1] = spent[2] = 0;
            last_log = now;
        }
    }
}
