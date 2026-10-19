#!/usr/bin/env python3
"""Bake the game's fonts into glyph bitmaps for the engine's text objects.

    python3 tools/font_pack.py out/fonts.pak [--scale 2/3]

Fonts: assets_raw/dump/oracle/fonts.json, the runtime's font table ({handle: [size px, weight, italic, face]},
recorded live). Each face goes through fc-match to the closest installed file ("Arial" -> its metric-compatible
twin). Glyphs 32..126 are drawn with PIL, top-anchored as canvas's textBaseline "top" draws them. Layout
(little-endian):
    "UCNF", u32 version 1, u32 font_count
    per font: i32 handle, f32 size, f32 line_height (size + ceil(size / 8), the runtime's Se), u32 glyph_count,
        per glyph: u16 char, f32 advance, i16 x, i16 y, u16 w, u16 h, w*h alpha bytes
All metrics are already multiplied by the scale.
"""
import json
import math
import struct
import subprocess
import sys
from fractions import Fraction
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent


def font_file(face, weight, italic):
    pattern = f"{face}:weight={'bold' if weight >= 600 else 'regular'}" + (":slant=italic" if italic else "")
    return subprocess.run(["fc-match", "-f", "%{file}", pattern], capture_output=True, text=True).stdout.strip()


def main():
    out = Path(sys.argv[1])
    scale = float(Fraction(sys.argv[sys.argv.index("--scale") + 1])) if "--scale" in sys.argv else 1.0
    fonts = json.load(open(ROOT / "assets_raw/dump/oracle/fonts.json"))
    blob = b""
    for handle, (size, weight, italic, face) in sorted(fonts.items(), key=lambda kv: int(kv[0])):
        px = max(1, round(size * scale))
        path = font_file(face, weight, italic)
        f = ImageFont.truetype(path, px)
        line = (size + math.ceil(size / 8)) * scale
        glyphs = b""
        for c in range(32, 127):
            ch = chr(c)
            adv = f.getlength(ch)
            l, t, r, b = f.getbbox(ch, anchor="la")
            w, h = max(0, r - l), max(0, b - t)
            img = Image.new("L", (max(1, w), max(1, h)), 0)
            if w and h:
                ImageDraw.Draw(img).text((-l, -t), ch, font=f, fill=255, anchor="la")
            glyphs += struct.pack("<HfhhHH", c, adv, l, t, w, h)
            glyphs += img.tobytes() if w and h else b""
        blob += struct.pack("<iffI", int(handle), px, line, 95) + glyphs
        print(f"font {handle}: {face} {size}px w{weight}{' italic' if italic else ''} -> {Path(path).name} at {px}px")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(b"UCNF" + struct.pack("<II", 1, len(fonts)) + blob)
    print(f"{len(fonts)} fonts -> {out}, {out.stat().st_size} bytes")


if __name__ == "__main__":
    main()
