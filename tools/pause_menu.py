#!/usr/bin/env python3
"""The console's pause panel: raw RGBA 1280x720, clear outside the panel. platform/xenon/game/main.cpp draws the
selection box around item i at kPauseItemY[i] (the centres below), so the two lists must agree.

    python3 tools/pause_menu.py build/xenon/pause.rgba [preview.png]
"""
import sys

from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/google-noto/NotoSans-CondensedBlack.ttf"
BOLD = "/usr/share/fonts/google-noto/NotoSans-Bold.ttf"
ITEMS = [("RESUME", 330), ("CONTROLS", 400), ("QUIT TO MENU", 470)]  # = kPauseItemY


def main():
    img = Image.new("RGBA", (1280, 720), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((420, 170, 860, 560), radius=18, fill=(10, 10, 12, 225), outline=(150, 20, 20, 255), width=3)
    d.text((640, 238), "PAUSED", font=ImageFont.truetype(FONT, 64), fill=(235, 235, 235, 255), anchor="mm")
    for text, y in ITEMS:
        d.text((640, y), text, font=ImageFont.truetype(FONT, 36), fill=(225, 225, 225, 255), anchor="mm")
    d.text((640, 528), "A  select      B  back", font=ImageFont.truetype(BOLD, 20), fill=(150, 150, 150, 255), anchor="mm")
    if len(sys.argv) > 2:
        prev = Image.new("RGBA", img.size, (60, 60, 60, 255))
        prev.alpha_composite(img)
        prev.save(sys.argv[2])
    open(sys.argv[1], "wb").write(img.tobytes())


if __name__ == "__main__":
    main()
