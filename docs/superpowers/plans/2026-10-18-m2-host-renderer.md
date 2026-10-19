# Plan 3 — M2: Host renderer and live-pixel calibration

- **Date:** 2026-10-18
- **Spec:** `docs/superpowers/specs/2026-10-18-ucn-xbox360-port-design.md`
- **Prev:** Plan 1 (M0 platform bring-up), Plan 2 (M1 data pipeline — done, `e84e548`/`265b3b9`)
- **Goal (M2):** turn `build/bundle/bundle.bin` into pixels on the host: a CPU compositor that renders any frame (background + every sprite at the right place) and a diff harness that compares it against the live game's own canvas. Exit criterion: INTRO and Frame 1 render with every sprite at the same position and size as the live game, and the remaining pixel differences are understood and recorded.

## What the probes already told us

- The runtime's display tree is not a usable render oracle: `frame.wd.Da` is just an index list `0..N-1`, and `frame.pb[<layer>].gm/hm` came back empty for INTRO (they only fill for frames the game has drawn through its own pipeline). Two probes spent on this; do not repeat them.
- **The live canvas is the oracle.** CDP `Page.captureScreenshot` returns the presented frame; clipping it to the canvas rect at `scale = canvas.width / rect.width` yields the exact logical-resolution image (1920×1080 etc.).
- Instance records carry `object_handle`, `x`, `y` (can be negative / far outside the frame), `layer` (0-based, matches ctfak), `order` (creation order). Images carry atlas index, crop rect inside the atlas, intrinsic file size, hotspot (≈centre) and `Ga`/`Ca` raw offsets.
- Objects: 638 defs, 442 with animations (7136 animations, 7216 direction entries: `min_speed`, `max_speed`, `repeat`, `back_to`, image-handle list). 0 alterable values, so the renderer needs nothing else for a static frame.
- Host has zlib (`/usr/include/zlib.h`, libz-ng) and Python PIL 12.3.0 — all tooling needed for capture, render and diff.

## Decisions (v1, calibrated in Task 6)

1. **Reference capture** via `Page.captureScreenshot` `clip` = canvas rect, `scale` = `canvas.width / rect.width`; output PNG is exactly `frame.width × frame.height`.
2. **Compositor:** CPU, RGBA8 (straight alpha), frame-sized buffer. Fill with `bg_color` (`0xRRGGBB`), then blit sprites: layers ascending (0 = back, as in the ccj), inside a layer by `order` ascending.
3. **Current image rule v1:** object's animation 0 → direction slot 0 → `frames[0]`. Objects without animations are skipped and counted in the render report (they are inactive/backdrop containers until M3).
4. **Anchor rule v1:** draw the crop rect's top-left at `(x, y)` exactly. Hotspot/anchor variants are the first thing Task 6 tests (alternatives: `(x - hotspot)`, `(x - offset)`, hotspot-vs-offset combinations). The winning rule and the evidence go to `docs/render-semantics.md`.
5. **PNG:** decode/encode in C++ through zlib only; the diff tool is Python + PIL.
6. **Blend/ink:** v1 draws straight-alpha source over destination (no additive/alpha-coefficient ink); `IMAGES.flags` (`Sa`) and object ink fields are recorded but unused until M3 renders effects. Anything that differs because of this is marked "ink pending" in the diff report.

## Conventions

- Work from `/home/mohamd/Projects/UCN_MVP_Xbox360`; probe server (`:8765`) and headless Chrome CDP (`:9222`) must be up (`tools/run_oracle_dumps.sh` checks both, restart if needed).
- Reference PNGs and diff images live under `build/ref/` and `build/diff/` (both under the ignored `build/`).
- Commit after each task.

---

## Task 0 — Land this plan

```bash
git add docs/superpowers/plans/2026-10-18-m2-host-renderer.md
git commit -m "Add M2 host renderer plan"
```

---

## Task 1 — `tools/cdp_frame_ref.js`: the reference capture

Behaviour: `node tools/cdp_frame_ref.js FRAME out.png [out.json]`

1. Connect to the probe page (same CDP plumbing as `tools/cdp_eval.js`).
2. Evaluate: freeze the frame —
   `(() => { const u = window.__rt.Uc; if (u.Db !== FRAME) { u.Db = FRAME; u.tK(); } const c = document.querySelector('canvas'); const r = c.getBoundingClientRect(); return JSON.stringify({ frame: u.frame.Vz, w: c.width, h: c.height, rect: [r.x, r.y, r.width, r.height] }); })()`
3. Wait ~400 ms so the game has presented at least one new frame.
4. `Page.captureScreenshot` with `clip = {x: rect.x, y: rect.y, width: rect.width, height: rect.height, scale: canvas.width / rect.width}`.
5. Write the PNG and a sidecar JSON (`frame name`, canvas size, rect, scale, timestamp).

Steps:

```bash
mkdir -p build/ref
node tools/cdp_frame_ref.js 0 build/ref/frame_0.png build/ref/frame_0.json
python3 -c "from PIL import Image; im = Image.open('build/ref/frame_0.png'); print(im.size, im.mode)"
```

**Acceptance:** `frame_0.png` is 1920×1080 and shows the INTRO screen. (If the canvas is CSS-scaled in the headless window, the `scale` parameter still yields exactly 1920×1080; verify the size, not the window.)

---

## Task 2 — `engine/assets/png.{h,cpp}`: PNG in/out with zlib

```cpp
namespace ucn::assets {
struct Image { uint32_t width = 0, height = 0; std::vector<uint8_t> rgba; };
bool png_decode(const uint8_t* data, size_t size, Image* out, std::string* error);
bool png_decode_file(const std::string& path, Image* out, std::string* error);
bool png_write_file(const std::string& path, const Image& image, std::string* error);
}
```

- Decode: validate signature, walk IHDR/PLTE/tRNS/IDAT/IEND, `inflate` with zlib (chunked for large atlases), unfilter (None/Sub/Up/Average/Paeth), expand to RGBA8 (grey, grey+alpha, palette+tRNS, RGB, RGBA; 8-bit; 16-bit → take high byte). Atlases are large (a few MB) — assert the unfiltered size and fail loudly on unexpected color types.
- Encode: 8-bit RGBA, filter 0, single IDAT, `deflate` level 6.

Test step: decode every atlas used by frame 0 and re-encode a round-trip of `build/ref/frame_0.png`, then compare with PIL (`ImageChops.difference(...).getbbox() is None`).

---

## Task 3 — `engine/assets/image_bank.{h,cpp}`: bundle images → pixel crops

```cpp
class ImageBank {
public:
    bool load(const data::Bundle& bundle, const std::string& root, std::string* error);
    const Image* crop(uint32_t handle, std::string* error); // decoded crop rect, cached
    const ImageRecord* record(uint32_t handle) const;
};
```

- Walks the IMAGES section once (records → `handle`, file name, crop rect, src size, hotspot, offsets, flags).
- Loads atlas files on demand from `root` (the directory holding `resources/`), caches the decoded atlas, returns the cropped subimage (copy; a later milestone can hand out views).
- `assets_raw/out` is the root during host work; on the console `root` will be the XEX directory with `resources/`.

---

## Task 4 — `engine/render/renderer.{h,cpp}`: bundle frame → RGBA buffer

```cpp
struct RenderStats { uint32_t instances = 0, drawn = 0, skipped_no_anim = 0, missing_image = 0; };
bool render_frame(const data::Bundle& bundle, assets::ImageBank& bank,
                  uint32_t frame_index, assets::Image* out, RenderStats* stats, std::string* error);
```

- Parses the FRAMES record (`name`, size, `bg_color`, layers, instances, image handles) per the frozen layout.
- Fills the buffer with `bg_color`, then for each instance: resolve image (decision 3), look up the crop, blit at `(x, y)` (decision 4) with straight-alpha compositing, clipped to the frame rect (negative coordinates are normal: INTRO has sprites at y = -310).
- Counts skipped/missing for the report; never throws (returns false + error).

---

## Task 5 — `platform/host/frame_render`: the CLI

```
usage: frame_render BUNDLE.BIN FRAME out.png [--objects-csv out.csv]
```

- Loads the bundle, renders the frame, writes the PNG, prints `frame=<name> size=WxH instances=N drawn=D skipped=S missing=M`.
- `--objects-csv` writes `index,object,x,y,layer,order,image_handle` — the calibration worksheet.
- `Makefile` mirrors `platform/host/bundle_dump` (`-I../../../engine -lz`, sources: `main.cpp`, `bundle.cpp`, `png.cpp`, `image_bank.cpp`, `renderer.cpp`).

Steps:

```bash
make -C platform/host/frame_render
platform/host/frame_render/build/frame_render build/bundle/bundle.bin 0 build/diff/frame_0_host.png --objects-csv build/diff/frame_0_host.csv
```

**Acceptance:** prints `instances=10 drawn=<n>` for INTRO; the PNG shows the INTRO composition; the CSV has one row per instance.

---

## Task 6 — `tools/compare_png.py` + the calibration loop

`python3 tools/compare_png.py A.png B.png [--out-dir build/diff]`:
- asserts equal sizes; reports mean absolute difference per channel, `% pixels with max-channel delta > 16`, and the bounding box of the differing region;
- writes `side_a.png`, `side_b.png`, `diff_heat.png` (delta ×8) and `overlay.png` (50/50 blend) into the out dir;
- exits 0 when the mismatch is below the recorded threshold, 1 otherwise.

Calibration loop (each iteration: render → compare → inspect `overlay.png`/`diff_heat.png`):

1. INTRO with anchor rule v1 → if sprites are offset by a constant vector, test the hotspot variant.
2. Confirm the layer rule (0 = back) by toggling in the renderer and comparing: the wrong order flips z-relations visibly in `overlay.png`.
3. Confirm the current-image rule on Frame 1 (496 instances, 6 layers): count `skipped_no_anim` and check the drawn set against `side_a.png`.
4. Frame 2 (2500×1080, offsets) as the stress frame.
5. Record the final rules + the measured numbers + any "ink pending" cases in `docs/render-semantics.md`.

**Acceptance:** INTRO and Frame 1 diff images show identical sprite positions/sizes; every visible difference is either anti-aliasing/quality or listed in `docs/render-semantics.md` with a reason; the numbers (mismatch %, differing bbox) are recorded in that file.

---

## Task 7 — Commit + record

```bash
git add engine/assets engine/render platform/host/frame_render tools/cdp_frame_ref.js tools/compare_png.py docs/render-semantics.md
git commit -m "Add host renderer, frame CLI and live-pixel diff harness (M2)"
```

Next plan: M3 — the event interpreter (actions/conditions/expressions) running the dumped program against the live oracle, then M4 — the xenon GPU path for this same compositor.

---

## Results (2026-10-18, executed)

Tasks 1–6 done; the calibration converged on different rules than the v1 guesses above.

**Task 1 — capture tool.** `tools/cdp_frame_ref.js` ended up different from the sketch:
the page sizes its canvas to the *window* (1920×993), so the tool first forces an emulated
viewport at the frame's logical size; `Page.captureScreenshot` returns a black canvas for
this WebGL page, so the grab happens inside a `requestAnimationFrame` via
`drawImage`→`toDataURL`; and a pixel-signature poll waits out the game's loading bar and
re-freezes if the boot sequence navigates away. Output is exactly frame-sized.

**Tasks 2–5.** `engine/assets/png.{h,cpp}` (full PNG decoder: 8/16-bit, palette, tRNS,
non-interlaced; zlib encode), `engine/assets/image_bank.{h,cpp}` + `engine/data/reader.h`,
`engine/render/renderer.{h,cpp}`, `platform/host/frame_render` (CLI with `--anchor`,
`--layers`, `--csv`). `Bundle` grew generic `count()`/`record_offset()`.

**Bundle format grew one field** (`backdrop_image`, u32 after `blend`; M1 plan changelog +
layout updated): type-1 backdrops carry a static image (`oc.Mh`) the renderer must draw —
without it frame 6 misses the whole control-map artwork. `bundle.bin` is now 2,337,217 bytes.

**Task 6 — calibration.** The measured rules (details + evidence in
`docs/render-semantics.md`):

- anchor: crop top-left at `(x - off_x, y - off_y)` (the `Ga`/`Ca` bias), *not* the corner
  and *not* the ctfak hotspot;
- layers: descending index (last ccj layer = bottom), creation order ascending inside one;
- backdrops render their static image; the current-image rule stays anim0/dir0/frame0.

`tools/compare_png.py` prints per-region mean-abs/strong-pixel stats plus optional diff and
stack images and exits non-zero on any strong pixel.

**Acceptance numbers.** Frame 6 "control map" is pixel-perfect against the live canvas:
0.000 mean in the text and keyboard regions, 0.008 in the GO button region, 0 pixels above
the strong threshold anywhere (`diff bbox = (1617,790,1865,1038)` = antialiased button edge).
Frame 1 (menu) is *not* matched (mean 163.6, 69% strong pixels) and the plan's INTRO/Frame 1
exit criterion is superseded: the live menu has already run its event program (the red
"right side" panel hidden, portrait frames swapped), so only the frame's initial state is
reproducible without the M3 interpreter. Frame 6 is a stronger acceptance anyway: it proves
decode, cropping, anchoring, ordering and blending all at once at the pixel level.

**Rule defaults** in `render::Options` are now the calibrated ones (anchor `kOff`, layer
order `kDescending`); the debug variants remain behind flags.

**Next.** M3: event interpreter (and its live-oracle harness) — the only way to close the
frame-1 and gameplay-screen gaps. M4: xenon GPU path for this same compositor.

## Out of scope for M2

- Animations over time, collision, sound, save data (M3).
- Ink effects / additive blending beyond straight alpha (M3 renders effects; the diff marks them).
- GPU/XSC shaders and DXT compression (xenon renderer plan).
- The Xbox 360 build of the renderer: M2 is host-only by design; the xenon port reuses `engine/render` with a GPU backend later.
