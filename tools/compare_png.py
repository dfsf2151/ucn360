#!/usr/bin/env python3
"""Compare a host render against a live CDP reference capture.

    python3 tools/compare_png.py RENDER.png REF.png [options]

Options:
    --diff OUT.png             write an amplified (x4) absolute-difference image
    --stack OUT.png            write the render above the reference
    --region NAME:X,Y,W,H      add a named region (repeatable); defaults cover the
                               whole frame only
    --quiet                    print just the summary line

Exit code is 0 when the frames match (same size and no strong pixels), 1 otherwise.

The live reference is a canvas grab of the running game (tools/cdp_frame_ref.js);
the host render comes from platform/host/frame_render. Differences are expected
where the game's own event program has changed the frame's initial state (hidden
objects, swapped animation frames) - that is M3 territory, see
docs/render-semantics.md.
"""
import argparse
import sys

from PIL import Image, ImageChops, ImageStat

STRONG_DELTA = 16  # on the 0..255 luma scale


def parse_region(text):
    name, _, spec = text.partition(':')
    x, y, w, h = (int(v) for v in spec.split(','))
    return name or spec, (x, y, w, h)


def region_stats(render, ref, box):
    a = render.crop(box)
    b = ref.crop(box)
    diff = ImageChops.difference(a, b)
    mean = sum(ImageStat.Stat(diff).mean) / 3.0
    luma = diff.convert('L')
    hist = luma.histogram()
    total = max(1, (box[2] - box[0]) * (box[3] - box[1]))
    strong = sum(hist[STRONG_DELTA + 1:])
    return mean, strong * 100.0 / total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('render')
    ap.add_argument('ref')
    ap.add_argument('--diff')
    ap.add_argument('--stack')
    ap.add_argument('--region', action='append', default=[])
    ap.add_argument('--quiet', action='store_true')
    args = ap.parse_args()

    render = Image.open(args.render).convert('RGB')
    ref = Image.open(args.ref).convert('RGB')
    if render.size != ref.size:
        print(f'size mismatch: render {render.size} vs ref {ref.size}')
        return 1

    regions = [parse_region(r) for r in args.region]
    if not regions:
        regions = [('full', (0, 0, render.width, render.height))]

    ok = True
    for name, box in regions:
        box = (box[0], box[1], min(box[0] + box[2], render.width), min(box[1] + box[3], render.height))
        mean, strong = region_stats(render, ref, box)
        if not args.quiet:
            print(f'{name:16} mean_abs={mean:7.3f}  pixels>|{STRONG_DELTA}|={strong:6.2f}%  box={box}')
        if strong > 0.0:
            ok = False

    if not args.quiet:
        diff = ImageChops.difference(render, ref)
        print(f'{"diff bbox":16} {diff.getbbox()}')

    if args.diff:
        diff = ImageChops.difference(render, ref).point(lambda v: min(255, v * 4))
        diff.save(args.diff)
        if not args.quiet:
            print(f'wrote {args.diff}')
    if args.stack:
        stack = Image.new('RGB', (render.width, render.height * 2 + 8), (20, 20, 20))
        stack.paste(render, (0, 0))
        stack.paste(ref, (0, render.height + 8))
        stack.save(args.stack)
        if not args.quiet:
            print(f'wrote {args.stack} (render above reference)')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
