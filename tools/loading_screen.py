#!/usr/bin/env python3
"""The console's loading screen: the menu's wall of characters (frame 1 as the engine draws it), blurred and
darkened, with the title over it. Raw RGBA 1280x720, which the title shows while its packs load.

    python3 tools/loading_screen.py build/xenon/loading.rgba [preview.png]
"""
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFilter, ImageFont

FONT = "/usr/share/fonts/google-noto/NotoSans-CondensedBlack.ttf"


def main():
    with tempfile.TemporaryDirectory() as tmp:
        menu = tmp + "/menu.png"
        subprocess.run(["platform/host/frame_render/build/frame_render", "build/bundle/bundle.bin", "1", menu,
                        "--ticks=120", "--fonts=build/bundle/fonts_1x.pak"], check=True, capture_output=True)
        wall = Image.open(menu).convert("RGB").crop((30, 20, 1630, 1060))  # the portraits, not the side panel
    wall = wall.resize((1280, int(1280 * wall.height / wall.width))).crop((0, 0, 1280, 720))
    wall = wall.filter(ImageFilter.GaussianBlur(3))
    dark = Image.new("RGB", wall.size, (0, 0, 0))
    img = Image.blend(wall, dark, 0.55)
    # vignette: darker towards the edges, a red glow behind the title
    shade = Image.new("L", img.size, 0)
    d = ImageDraw.Draw(shade)
    for i in range(60):
        d.ellipse((-200 + i * 8, -150 + i * 6, 1480 - i * 8, 870 - i * 6), fill=int(i * 3.5))
    img = Image.composite(img, dark, shade.filter(ImageFilter.GaussianBlur(40)))
    glow = Image.new("RGB", img.size, (120, 0, 0))
    gmask = Image.new("L", img.size, 0)
    ImageDraw.Draw(gmask).ellipse((240, 230, 1040, 470), fill=110)
    img = Image.composite(glow, img, gmask.filter(ImageFilter.GaussianBlur(60)))
    d = ImageDraw.Draw(img)
    title, sub = ImageFont.truetype(FONT, 92), ImageFont.truetype(FONT, 40)
    for text, font, y, color in (("ULTIMATE", title, 280, (235, 235, 235)), ("CUSTOM NIGHT", title, 375, (235, 235, 235)),
                                 ("LOADING...", sub, 640, (200, 40, 40))):
        d.text((643, y + 3), text, font=font, fill=(0, 0, 0), anchor="mm")  # drop shadow
        d.text((640, y), text, font=font, fill=color, anchor="mm")
    img.convert("RGBA").save(sys.argv[2]) if len(sys.argv) > 2 else None
    open(sys.argv[1], "wb").write(img.convert("RGBA").tobytes())


if __name__ == "__main__":
    main()
