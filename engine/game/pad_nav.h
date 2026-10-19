#pragma once
#include <string>
#include <vector>

#include "game/run.h"

namespace ucn::game {

// A console aid, not the runtime's: with the office monitor up (counter "screen up?" = 1), the camera map's
// "map button" nearest the selected one (the one under "map target") in direction (dx, dy), as the window point
// to click (its box centre). False when the monitor is down or nothing lies that way.
inline bool monitor_up(const Run& run) {
    for (const Instance* a : run.slots())
        if (a && a->def && a->def->name == "screen up?") return a->counter == 1;
    return false;
}

inline bool camera_step(const Run& run, int dx, int dy, double* wx, double* wy) {
    const Instance *up = nullptr, *target = nullptr;
    std::vector<const Instance*> buttons;
    for (const Instance* a : run.slots()) {
        if (!a || !a->def) continue;
        const std::string& n = a->def->name;
        if (n == "screen up?") up = a;
        else if (n == "map target") target = a;
        else if (n == "map button" && a->visible_list) buttons.push_back(a);
    }
    if (!up || up->counter != 1 || buttons.empty()) return false;
    auto cx = [](const Instance* a) { return a->x - a->hx + a->w / 2; };
    auto cy = [](const Instance* a) { return a->y - a->hy + a->h / 2; };
    // the selected camera: the button nearest the target (it sits on the selected one)
    const double tx = target ? cx(target) : cx(buttons[0]), ty = target ? cy(target) : cy(buttons[0]);
    const Instance* sel = buttons[0];
    for (const Instance* b : buttons)
        if ((cx(b) - tx) * (cx(b) - tx) + (cy(b) - ty) * (cy(b) - ty) < (cx(sel) - tx) * (cx(sel) - tx) + (cy(sel) - ty) * (cy(sel) - ty)) sel = b;
    const double ox = cx(sel), oy = cy(sel);
    const Instance* best = nullptr;
    double best_score = 1e18;
    for (const Instance* b : buttons) {
        if (b == sel) continue;
        const double vx = cx(b) - ox, vy = cy(b) - oy;
        const double along = vx * dx + vy * dy, side = vx * dy - vy * dx;
        if (along <= 4) continue; // ponytail: straight-line pick, fine for the map's grid of buttons
        const double score = along + 2 * (side < 0 ? -side : side);
        if (score < best_score) { best_score = score; best = b; }
    }
    if (!best) return false;
    const FrameDef* fd = run.frame_def();
    *wx = cx(best) - run.scroll_x() + (1920 / 2.0 - fd->display_w / 2.0);
    *wy = cy(best) - run.scroll_y() + (1080 / 2.0 - fd->display_h / 2.0);
    return true;
}

// Another console aid, for the right stick: the clickable sprite (one the frame's events take clicks on, collision
// flag 256, as Run::mouse_button tests) nearest the pointer in direction (dx, dy), as a window point (its box
// centre), so coins, the parrot or the shop's buttons can be reached without steering the pointer onto them.
// A target must be what the player sees at its centre: the topmost solid sprite there (Run::sprite_covers, alpha
// at least 1/2, in draw order) is the target itself, or, for a hidden click zone, a small sprite of its own layer
// drawn over it (the shop's BUY labels). A large sprite on top (the camera feed over the office) hides it. False when nothing lies
// that way.
inline bool target_step(Run& run, double from_x, double from_y, double dx, double dy, double* wx, double* wy,
                        const Instance** hit = nullptr) {
    const FrameDef* fd = run.frame_def();
    if (!fd) return false;
    const double ox = 1920 / 2.0 - fd->display_w / 2.0 - run.scroll_x(), oy = 1080 / 2.0 - fd->display_h / 2.0 - run.scroll_y();
    auto alpha = [](const Instance* a) { // Ie
        const int32_t ink = a->sp.ink;
        return (ink & 4096) ? double((uint32_t(a->sp.ink_param) >> 24) & 255) / 255
                            : (ink & 4095) == 1 ? (128.0 - a->sp.ink_param) / 128 : 1.0;
    };
    // the sprites as drawn, bottom to top (world.cpp's order: per layer, then back and main lists)
    std::vector<const Instance*> drawn;
    for (size_t L = 0; L < run.display_main().size(); L++)
        for (const auto* list : {&run.display_back()[L], &run.display_main()[L]})
            for (int32_t slot : *list) {
                const Instance* a = run.slots()[size_t(slot)];
                if (a && a->type == 2 && a->visible_list && a->has_pos && alpha(a) >= 0.5) drawn.push_back(a);
            }
    const Instance* best = nullptr;
    double best_score = 1e18, bx = 0, by = 0;
    for (const Instance* a : run.slots()) {
        if (!a || a->type != 2 || !(a->col_flags & 256) || (a->flags & 1) || a->w <= 0 || a->h <= 0) continue;
        const double fx = a->x - a->hx + a->w / 2, fy = a->y - a->hy + a->h / 2; // frame coordinates
        const double cx = fx + ox, cy = fy + oy;
        if (cx < 0 || cy < 0 || cx >= 1920 || cy >= 1080) continue;
        const Instance* top = nullptr;
        for (auto it = drawn.rbegin(); it != drawn.rend() && !top; ++it)
            if (run.sprite_covers(*it, fx, fy)) top = *it;
        const bool shows = top == a || (top && top->w * top->h <= 1920.0 * 1080 / 8 && top->sp.layer == a->sp.layer &&
                                        (!a->visible_list || alpha(a) < 0.5));
        if (!shows) continue;
        const double vx = cx - from_x, vy = cy - from_y;
        const double along = vx * dx + vy * dy, side = vx * dy - vy * dx;
        if (along <= 12) continue; // ponytail: straight-line pick, the same as the camera map's
        const double score = along + 2 * (side < 0 ? -side : side);
        if (score < best_score) { best_score = score; best = a; bx = cx; by = cy; }
    }
    if (!best) return false;
    *wx = bx;
    *wy = by;
    if (hit) *hit = best;
    return true;
}

// For the D-pad on the menu: the up (or down) arrow the game shows on the portrait under the pointer ("cschar N",
// 150x200), as the window point to click. The arrows sit at the portrait's bottom left, 43 left of its centre and
// 47 / 78 below it (measured on frame 1). False off the menu or off every portrait.
inline bool portrait_arrow(const Run& run, double wx, double wy, bool up, double* ax, double* ay) {
    const FrameDef* fd = run.frame_def();
    if (!fd || run.frame() != 1) return false;
    const double ox = 1920 / 2.0 - fd->display_w / 2.0 - run.scroll_x(), oy = 1080 / 2.0 - fd->display_h / 2.0 - run.scroll_y();
    for (const Instance* a : run.slots()) {
        if (!a || !a->def || a->def->name.rfind("cschar ", 0) != 0 || !a->visible_list) continue;
        const double x0 = a->x - a->hx + ox, y0 = a->y - a->hy + oy;
        if (wx < x0 || wy < y0 || wx >= x0 + a->w || wy >= y0 + a->h) continue;
        *ax = x0 + a->w / 2 - 43;
        *ay = y0 + a->h / 2 + (up ? 47 : 78);
        return true;
    }
    return false;
}

} // namespace ucn::game
