#!/usr/bin/env python3
"""Draw the control map (frame 6's backdrop, image 1802) for the Xbox 360 pad: the game's own list of actions with
the console's buttons (platform/xenon/game/main.cpp, kPad) in place of the keys.

    python3 tools/pad_map.py assets_raw/override/1802.png
"""
import sys

from PIL import Image, ImageDraw, ImageFont

BOLD = "/usr/share/fonts/google-noto/NotoSans-Bold.ttf"
W, H = 1920, 1080
TEXT = ImageFont.truetype(BOLD, 34)
SMALL = ImageFont.truetype(BOLD, 22)
COLORS = {"A": (96, 176, 40), "B": (210, 40, 40), "X": (40, 100, 210), "Y": (230, 180, 20)}


def button(d, x, y, name):
    """One pad control at (x, y) (left, vertical centre); returns its width."""
    if name in COLORS:  # face buttons: coloured discs
        d.ellipse((x, y - 20, x + 40, y + 20), fill=COLORS[name], outline=(255, 255, 255), width=2)
        d.text((x + 20, y), name, font=SMALL, fill=(255, 255, 255), anchor="mm")
        return 40
    if name.startswith("D"):  # D-pad: a cross with the pressed arm lit
        arm = name[1:]
        c = (x + 20, y)
        for k, (dx, dy) in {"U": (0, -1), "D": (0, 1), "L": (-1, 0), "R": (1, 0)}.items():
            r = (c[0] + dx * 13 - 7, c[1] + dy * 13 - 7, c[0] + dx * 13 + 7, c[1] + dy * 13 + 7)
            d.rectangle(r, fill=(240, 200, 40) if k == arm or arm == "" else (90, 90, 90))
        d.rectangle((c[0] - 7, c[1] - 7, c[0] + 7, c[1] + 7), fill=(90, 90, 90))
        return 40
    w = int(d.textlength(name, font=SMALL)) + 24  # shoulders, triggers, Start, Back, stick: a rounded tag
    d.rounded_rectangle((x, y - 18, x + w, y + 18), radius=12, fill=(70, 70, 70), outline=(255, 255, 255), width=2)
    d.text((x + w / 2, y), name, font=SMALL, fill=(255, 255, 255), anchor="mm")
    return w


def row(d, x, y, controls, label):
    for i, c in enumerate(controls):
        if c == "+":
            d.text((x + 4, y), "+", font=TEXT, fill=(255, 255, 255), anchor="lm")
            x += 30
            continue
        x += button(d, x, y, c) + 8
    d.text((x + 10, y), "- " + label, font=TEXT, fill=(255, 255, 255), anchor="lm")


def main():
    img = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    d = ImageDraw.Draw(img)
    left = [(["LT", "+", "DU"], "Power Generator"), (["LT", "+", "DR"], "Silent Ventilation"),
            (["LT", "+", "DD"], "Heater"), (["LT", "+", "DL"], "Power AC"), (["LT", "+", "X"], "Global Music Box"),
            (["LT", "+", "Y"], "All OFF"), (["RT"], "All OFF"), (["B"], "Flashlight")]
    right = [(["DU"], "Close Forward Vent"), (["DL"], "Close Left Door"), (["Y"], "Monitor"),
             (["DR"], "Close Right Door"), (["DD"], "Close Side Vent"), (["RB"], "Catch Fish (Old Man Con.)"),
             (["LB"], "Close Ad (El Chip)"), (["X"], "Desk Fan ON/OFF"), (["Hold BACK"], "Return to Menu")]
    for i, (c, t) in enumerate(left):
        row(d, 330, 120 + i * 60, c, t)
    for i, (c, t) in enumerate(right):
        row(d, 1000, 120 + i * 60, c, t)
    bottom = [(["LEFT STICK"], "Move the pointer"), (["RIGHT STICK"], "Jump the pointer to coins, buttons, the shop"),
              (["A"], "Click"),
              (["D"], "Switch camera (Monitor up)"), (["LT", "+", "RT"], "Hold 3 s on the menu: erase data"),
              (["R3"], "Show / hide this guide while playing"), (["START"], "Pause (in the office)")]
    for i, (c, t) in enumerate(bottom):
        row(d, 330, 664 + i * 50, c, t)
    img.save(sys.argv[1])


if __name__ == "__main__":
    main()
