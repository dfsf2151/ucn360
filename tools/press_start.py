#!/usr/bin/env python3
"""The warning screen's pulsing "PRESS ENTER" (Active 4's 21 frames, images 1757-1779) as "PRESS START", built from
each frame's own letters (P R E S T) and the A of "WARNING" (image 1755), tinted to that frame's grey.

    python3 tools/press_start.py   # exports the originals with asset_pack, writes assets_raw/override/H.png
"""
import subprocess
import tempfile
from pathlib import Path

from PIL import Image

FRAMES = [1757] + list(range(1759, 1778)) + [1779]
Y0, Y1 = 46, 63  # the prompt's rows; letter columns below, measured on 1764 (all frames share them)
P, R, E, S, T = (177, 188), (191, 202), (206, 216), (219, 230), (286, 299)
A = (216, 18, 230, 34)  # "WARNING"'s A in 1755


def main():
    out = Path("assets_raw/override")
    with tempfile.TemporaryDirectory() as tmp:
        args = ["platform/host/asset_pack/build/asset_pack", "build/bundle/bundle.bin", "/dev/null",
                "--export", f"1755:{tmp}/1755.png"]
        for h in FRAMES:
            args += ["--export", f"{h}:{tmp}/{h}.png"]
        subprocess.run(args, check=True)
        warning = Image.open(f"{tmp}/1755.png").convert("RGBA")
        for h in FRAMES:
            e = Image.open(f"{tmp}/{h}.png").convert("RGBA")
            g = lambda c: e.crop((c[0], Y0, c[1], Y1))
            grey = max(e.crop((177, Y0, 326, Y1)).getcolors(1 << 16), key=lambda c: c[1][3])[1][:3]
            a = warning.crop(A).resize((15, 17), Image.LANCZOS)
            tinted = Image.new("RGBA", a.size, grey + (255,))
            tinted.putalpha(a.split()[3])
            seq = [(g(P), 3), (g(R), 4), (g(E), 3), (g(S), 3), (g(S), 13), (g(S), 3), (g(T), 1), (tinted, 2),
                   (g(R), 3), (g(T), 0)]
            img = Image.new("RGBA", e.size, (0, 0, 0, 0))
            x = round(251.5 - sum(i.width + gap for i, gap in seq) / 2)
            for i, gap in seq:
                img.alpha_composite(i, (x, Y0))
                x += i.width + gap
            img.save(out / f"{h}.png")
    print(f"{len(FRAMES)} frames -> {out}")


if __name__ == "__main__":
    main()
