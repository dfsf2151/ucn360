# Render semantics — calibrated against the live game (M2)

What the host renderer in `engine/render/renderer.cpp` does, and the evidence that it
matches the original HTML5 runtime. Calibration used frame 6 "control map" because the
live game sits on it with an event-independent state, so a static render can be compared
against the live canvas pixel by pixel.

## Reference capture (`tools/cdp_frame_ref.js`)

1. Freeze the frame in the running game (`u.Db = N; u.tK()`), read the frame's logical size.
2. Force the emulated viewport to that exact size (`Emulation.setDeviceMetricsOverride`),
   because the page sizes its canvas to the window (1920x993 in the headless window, which
   would film the game at a non-1:1 scale).
3. Poll until the pixels stop changing (frame switches show the game's loading bar).
4. Grab the canvas inside a `requestAnimationFrame` callback with `drawImage` onto a 2D
   canvas and `toDataURL` — `Page.captureScreenshot` returns a black canvas for this WebGL
   page, and `drawImage` reads the presented buffer reliably.

Output is exactly `frame.width × frame.height`, so host render and reference align 1:1.

## Rules, with the measurement that fixed each one

| Rule | Value | Evidence |
| --- | --- | --- |
| Sprite anchor | crop top-left → `(x - off_x, y - off_y)`, where `off` = ctfak `Ga`/`Ca` | GO button (frame 6, image 23, `off=(124,124)`): masked template search over the live canvas peaks at exactly `(x-124, y-124)`, score 1.2 (≈ bit-exact). Menu portraits (image 77, `off=(0,0)`): bright content starts at exactly `(x, y)`. SET ALL 0 (image 195, `off=(100,37)`): content bbox 3–5 px inside `(x-100, y-37)`, inconsistent with `(x, y)`. |
| Not the hotspot | ctfak `hotspot` (`Zg`/`$g`, ≈centre) is **not** the draw origin | image 77 has `hotspot=(69,103)` but draws at `(x, y)`; `hotspot` stays in the bundle for M3 aim/rotation work. |
| Layer order | descending layer index — the **last** ccj layer draws first (bottom) | frame 6 has an opaque 1920×1080 Active (image 143) on layer 1 and the Backdrop + GO on layer 0; live shows both, so layer 1 must sit underneath. With `--layers=desc` the frame matches pixel-perfect; with `--layers=asc` the GO button is buried. |
| Within a layer | ascending `order` (creation order, first created draws first) | frame 6: Backdrop (order 0) is covered by the GO button (order 1) in the live frame. |
| Backdrop images | type-1 (and type-0 "Quick Backdrop") objects draw their static `backdrop_image` (`oc.Mh`) at the instance position with no animation | frame 6: object 541 (Backdrop, image 1802, 1920×1080) reproduces the control-map artwork pixel-exactly (region mean diff 0.000). 7 objects carry a backdrop image. |
| Current image v1 | animation 0 → direction 0 → `frames[0]`; no animations → the backdrop image; neither → skipped (counted) | object census: 442 objects with animations, the rest are backdrops/strings/counters. |
| Background | frame-sized buffer filled with `bg_color` (`0xRRGGBB`, 0 = black) | frames 6/8/9/11 have white, degenerate here because layer-1 overlays cover the background. |
| Blend | straight alpha, source over destination, integer `(s*a + d*(255-a))/255` per channel | frame 6 end-to-end match: 0 pixels differ by more than 16 on the luma scale, GO region mean abs diff 0.008. |

Acceptance (frame 6, `build/ref/frame_6.png`): text region 0.000, keyboard 0.000,
GO button 0.008, whole frame `diff bbox = (1617,790,1865,1038)` with no strong pixels —
the residual is rounding on the button's antialiased edge.

## What M2 does *not* reproduce (M3)

The live screen is the frame's **initial state only until the event program runs**. On the
menu (frame 1) the live game has already hidden objects and swapped animation frames:

- object 27 "right side" (image 1569, a 390×1116 red `(248,0,0)` panel) draws in the initial
  state; the live menu does not show it.
- the character portraits draw their bright first frames; the live menu shows them dimmed
  (a different animation frame or an applied effect).
- "GO" (image 1631, white) draws *under* "right side" (order 398 > 374) with the calibrated
  order yet the live menu shows the white GO — consistent with "right side" being hidden by
  an event, not with a different stacking rule.

Consequently the frame-1 comparison (mean abs diff 163.6, 69% strong pixels) is dominated by
event-driven state, not by renderer error. Everything else in the pipeline is proven by the
frame-6 match: PNG decode (all colour types, 8/16-bit, palette), atlas cropping, the anchor
rule, layer/order sorting, alpha blending and PNG encode.

Also deferred: object ink/blend/rgb_coeff effects (13 objects carry non-zero ink),
per-instance scale/angle (the ccj instance records have no such fields), and the string
("String" object) text rendering.

## Reproduce

```bash
python3 tools/compile_bundle.py                 # writes build/bundle/bundle.bin
make -C platform/host/frame_render
node tools/cdp_frame_ref.js 6 build/ref/frame_6.png build/ref/frame_6.json
platform/host/frame_render/build/frame_render build/bundle/bundle.bin 6 build/diff/frame_6.png
python3 tools/compare_png.py build/diff/frame_6.png build/ref/frame_6.png
```
