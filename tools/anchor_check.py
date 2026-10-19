#!/usr/bin/env python3
"""Check the sprite anchor rule against a live reference.

For instances drawn in a frame, take each sprite's crop from its atlas and slide it
over the live reference around the position the host renderer used; the best match
(or the score at the candidate offsets) tells which anchor rule the game really uses:

    corner  -> (0, 0)             drawn at instance (x, y)
    hotspot -> (-hotspot_x, -hotspot_y)
    off     -> (-off_x, -off_y)

usage: anchor_check.py BUNDLE_JSON FRAME_CSV REF.png [--atlas-dir DIR] [--limit N]
"""
import argparse
import json
import os
import sys

from PIL import Image, ImageChops, ImageStat

def masked_score(ref, tpl_rgb, mask, x, y):
    if x < 0 or y < 0 or x + tpl_rgb.width > ref.width or y + tpl_rgb.height > ref.height:
        return 1e9
    region = ref.crop((x, y, x + tpl_rgb.width, y + tpl_rgb.height))
    d = ImageChops.difference(region, tpl_rgb)
    return sum(ImageStat.Stat(d, mask).mean)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bundle_json')
    ap.add_argument('frame_csv')
    ap.add_argument('ref_png')
    ap.add_argument('--atlas-dir', default='assets_raw/out')
    ap.add_argument('--limit', type=int, default=6)
    ap.add_argument('--window', type=int, default=200)
    args = ap.parse_args()

    bundle = json.load(open(args.bundle_json))
    images = {i['handle']: i for i in bundle['images']}
    ref = Image.open(args.ref_png).convert('RGB')

    rows = []
    with open(args.frame_csv) as f:
        next(f)
        for line in f:
            idx, obj, x, y, layer, order, image, drawn = (int(v) for v in line.split(','))
            if drawn and image not in [r['image'] for r in rows]:
                rows.append({'idx': idx, 'object': obj, 'x': x, 'y': y, 'image': image})

    checked = 0
    for row in rows:
        if checked >= args.limit:
            break
        rec = images[row['image']]
        atlas_path = os.path.join(args.atlas_dir, rec['file'])
        if not os.path.exists(atlas_path):
            continue
        atlas = Image.open(atlas_path).convert('RGBA')
        crop = (rec['crop_x'], rec['crop_y'], rec['crop_x'] + rec['crop_w'], rec['crop_y'] + rec['crop_h'])
        tpl = atlas.crop(crop)
        alpha = tpl.split()[3]
        opaque = sum(alpha.histogram()[200:]) / (tpl.width * tpl.height)
        if opaque < 0.02:  # mostly transparent sprites make weak templates
            continue
        tpl_rgb = tpl.convert('RGB')
        mask = alpha.point(lambda a: 255 if a > 200 else 0)

        x0, y0 = row['x'], row['y']
        cands = {
            'corner': (x0, y0),
            'hotspot': (x0 - rec['hotspot_x'], y0 - rec['hotspot_y']),
            'off': (x0 - rec['off_x'], y0 - rec['off_y']),
        }
        scores = {k: masked_score(ref, tpl_rgb, mask, *v) for k, v in cands.items()}

        # brute-force around the drawn position for the true offset
        best = (1e18, 0, 0)
        step = 2
        w = args.window
        for y in range(y0 - w, y0 + w + 1, step):
            for x in range(x0 - w, x0 + w + 1, step):
                s = masked_score(ref, tpl_rgb, mask, x, y)
                if s < best[0]:
                    best = (s, x, y)

        winner = min(scores, key=scores.get)
        delta = (best[1] - x0, best[2] - y0)
        print(f"img {row['image']:4} obj {row['object']:3} at ({x0},{y0}) hotspot=({rec['hotspot_x']},{rec['hotspot_y']}) off=({rec['off_x']},{rec['off_y']}) flags={rec['flags']}")
        print(f"    scores: corner={scores['corner']:.1f} hotspot={scores['hotspot']:.1f} off={scores['off']:.1f}  -> candidate winner: {winner}")
        print(f"    best match at ({best[1]},{best[2]}) delta=({delta[0]:+d},{delta[1]:+d}) score={best[0]:.1f}  (hotspot delta would be ({-rec['hotspot_x']:+d},{-rec['hotspot_y']:+d}), off delta ({-rec['off_x']:+d},{-rec['off_y']:+d}))")
        checked += 1

if __name__ == '__main__':
    sys.exit(main())
