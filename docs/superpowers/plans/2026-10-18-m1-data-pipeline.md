# Plan 2 — M1: Oracle dumps, the bundle compiler, and the host loader

- **Date:** 2026-10-18
- **Spec:** `docs/superpowers/specs/2026-10-18-ucn-xbox360-port-design.md` (see the dated architecture update in §5)
- **Goal (M1):** freeze the engine's data source. A host compiler turns (a) oracle dumps taken from the original live runtime and (b) the recovered assets into `bundle.bin` — the single file the engine reads on both host and console. Acceptance: a host C++ tool loads `bundle.bin` and prints frames/objects/images counts that match the oracle, and the program section carries every event group of all 16 frames.
- **Why this shape:** the live runtime (`window.__rt.Uc`) exposes its *compiled* program (`frame.un`), object definitions, and per-frame image tables. Taking those dumps is fidelity by construction (they are what the original game executes) and removes all ccj binary archaeology from the console path. `Application.ccj` and the ctfak JSON stay as cross-checks.
- **Cross-checks already available:** `assets_raw/dump/frames.json` (old live dump of all 16 frames: name/size/instance count/images), `assets_raw/dump/defs_merged.json` (638 objects), `assets_raw/ccj_dump/Ultimate Custom Night/JSON/application.json` (ctfak: frames, instances, objects, groups). Validated today: names, sizes, instance counts, x/y, `objectInfo == live Df` all match exactly.

## Conventions

- Work from `/home/mohamd/Projects/UCN_MVP_Xbox360`.
- The probe page and CDP endpoint must be up: `python3 -m http.server 8765` inside `web_probe/`, and headless Chrome with `--remote-debugging-port=9222`. The runner checks both.
- `node tools/cdp_eval.js "<js>"` evaluates the JS in the probe page and prints the returned string.
- Commit after each task.

---

## Bundle format v1 (frozen here; all offsets and integers little-endian)

```
header (16 bytes)
  u32 magic = 'U','C','N','B'
  u16 version = 1
  u16 section_count = 7
  u32 total_size
  u32 reserved = 0
section table (section_count × 12 bytes)
  u32 type, u32 offset, u32 size        // offset from file start

types: 1 STRINGS  2 FRAMES  3 OBJECTS  4 IMAGES  5 SOUNDS  6 PROGRAM  7 RESOURCES
```

`STRINGS`: `u32 count`, then per string: `u32 len`, `len` UTF-8 bytes (no terminator).

`FRAMES`: `u32 count`, `u32 off[count]` (from section start), then records:
```
u32 name_idx;  i32 width, height
u32 layer_count;  per layer: u32 name_idx; i32 x_coeff, y_coeff; u32 flags
u32 bg_mode;  u32 bg_image (0xFFFFFFFF = none); i32 bg_x, bg_y; u32 bg_color (0xRRGGBB)
u32 instance_count;  per instance: u32 object_handle; i32 x, y; u32 layer; u32 order;
                                   u32 parent_type, parent_handle
u32 image_handle_count;  u32 image_handles[]
```

`OBJECTS`: `u32 count`, `u32 off[count]`, then records:
```
u32 handle; u8 type; u8 flags; u32 name_idx
u8 ink_effect, ink_effect_value, blend; u32 backdrop_image; i32 rgb_coeff[4]
u32 value_count;  i32 values[]                        // counters / alterable values
u32 anim_count;  per animation:
    u32 dir_count;  per direction: u8 dir; u8 repeat; u16 reserved;
                                   i32 min_speed, max_speed, back_to;
                                   u32 frame_count; u32 image_handles[]
```

`IMAGES`: `u32 count`, `u32 off[count]`, then records (sorted by handle):
```
u32 handle; u32 file_idx                    // e.g. "M0005.png" or "0113.png"
i32 crop_x, crop_y, crop_w, crop_h
i32 src_w, src_h                            // intrinsic size of the file
i16 hotspot_x, hotspot_y                    // draw origin (live fields Zg/$g)
i16 off_x, off_y                            // live fields Ga/Ca (raw, semantics to confirm)
u16 flags                                   // live field Sa (observed 4..359)
```

`SOUNDS`: `u32 count`, `u32 off[count]`, then records: `u32 handle; u32 file_idx; u8 format` (0 ogg, 1 mp3). v1 ships 0 entries; the audio plan fills it.

Format changelog: 2026-10-18, before any implementation existed: added `back_to` (+`u16 reserved`) to the animation direction record, `bg_color` to the frame background, and widened the image `flags` field (`Sa`) to `u16` (observed 4..359); the M1 reader matches this layout.
Format changelog: 2026-10-18 (M2): added `backdrop_image` (u32, live `oc.Mh`, 0 when absent) after `blend` in the object record — type-1 backdrop objects carry a static image the renderer must draw (7 of 638 objects; also 3 type-0 entries).
Format changelog: 2026-10-18 (M3): header version 2; `PROGRAM` is typed (v2: flat frame/group/item/param/value/token tables, layout in `tools/compile_bundle.py` `w_program` and `engine/data/program.h`). The JSON blob left the `.bin`; `build/bundle/program.json` stays as the cross-check. `bundle.bin` is 1,554,127 bytes.

`PROGRAM` (v1): `u32 json_len; u8 json[]` — a JSON blob, one object `{ "frames": [ { "i": 0, "groups": [...] }, ... ] }`, groups exactly as dumped. Temporary encoding: the interpreter plan replaces it with a typed layout once the VM's needs are known.

`RESOURCES`: `u32 count`, `u32 off[count]`, then records: `u32 file_idx; u32 size; u8 kind` (0 texture, 1 audio, 2 other). Every file the engine opens at runtime must appear here.

Field-name notes (confirmed against the live runtime today): image `wb` = atlas index (0 = direct file), `rd`/`sd` = crop x/y, `width`/`height` = crop size, `Zg`/`$g` = hotspot (near sprite centres), `Un[handle]` = atlas index per handle in a frame. Layer coefficients in the ccj are `i32` written as two 32-bit halves (ctfak reads them as one double, hence `9.18e-41`); the real value is likely `i32 / 2^24` — confirm in M2, v1 stores raw `i32`.

---

## Task 0 — Land this plan

```bash
cd /home/mohamd/Projects/UCN_MVP_Xbox360
git add docs/superpowers/plans/2026-10-18-m1-data-pipeline.md
git commit -m "Add M1 data pipeline plan"
```

---

## Task 1 — `tools/dump_oracle.js` and `tools/run_oracle_dumps.sh`

Three modes: `frame` (meta, instances, images, Un), `program` (all event groups + samples), `objects` (all 638 definitions, deep snaps). Outputs land in `assets_raw/dump/oracle/`.

`tools/dump_oracle.js`:

```js
(() => {
  const u = window.__rt.Uc;
  const MODE = "__MODE__";
  const FRAME = __FRAME__;
  const MAX_ARR = 8192, MAX_KEYS = 128;

  function snap(v, depth, seen) {
    const t = typeof v;
    if (v === null || v === undefined) return null;
    if (t === 'number' || t === 'string' || t === 'boolean') return v;
    if (t === 'function') return 'fn:' + (v.name || '?');
    if (depth <= 0) return '$' + t;
    if (seen.has(v)) return '[cycle]';
    seen.add(v);
    try {
      if (Array.isArray(v)) {
        const out = [];
        const n = Math.min(v.length, MAX_ARR);
        for (let i = 0; i < n; i++) out.push(snap(v[i], depth - 1, seen));
        if (v.length > n) out.push('$more:' + (v.length - n));
        return out;
      }
      const out = {};
      const ctor = v.constructor && v.constructor.name;
      if (ctor && ctor !== 'Object') out.$ctor = ctor;
      const keys = Object.getOwnPropertyNames(v);
      for (const k of keys.slice(0, MAX_KEYS)) {
        try { out[k] = snap(v[k], depth - 1, seen); }
        catch (e) { out[k] = '$error:' + (e && e.message ? e.message : '?'); }
      }
      if (keys.length > MAX_KEYS) out.$more = keys.length - MAX_KEYS;
      return out;
    } finally {
      seen.delete(v);
    }
  }

  const load = () => { if (u.Db !== FRAME) { u.Db = FRAME; u.tK(); } };

  if (MODE === 'objects') {
    // Object common data (oc) is not loaded yet; the app's own ccj reader can fill each def.
    const defs = u.Tc.yc;
    const file = u.file;
    const out = [];
    for (let i = 0; i < defs.length; i++) {
      const d = defs[i];
      if (!d) { out.push(null); continue; }
      if (file && d.load && !d.oc) { try { d.load(file); } catch (e) { /* leave as is */ } }
      out.push(snap(d, 10, new Set()));
    }
    return JSON.stringify({ count: defs.length, objects: out });
  }

  load();
  const f = u.frame;

  if (MODE === 'frame') {
    const wd = f.wd || {};
    const list = wd.list || [];
    const inst = [];
    for (let k = 0; k < list.length; k++) {
      const o = list[k];
      if (!o) { inst.push(null); continue; }
      inst.push({
        named: { order: o.Pi, object: o.Df, x: o.gv, y: o.hv, fv: o.fv, BA: o.BA, Xq: o.Xq },
        raw: snap(o, 2, new Set()),
      });
    }
    const imgs = [];
    const ims = (u.ba && u.ba.images) || [];
    for (const it of ims) {
      if (!it) continue;
      const shallow = Object.assign({}, it);
      delete shallow.app; delete shallow.vb; delete shallow.wg; delete shallow.Tl; delete shallow.Hh;
      imgs.push({
        handle: it.handle, atlas: it.wb, crop_x: it.rd, crop_y: it.sd,
        w: it.width, h: it.height, hotspot_x: it.Zg, hotspot_y: it.$g,
        Ga: it.Ga, Ca: it.Ca, Sa: it.Sa, raw: snap(shallow, 2, new Set()),
      });
    }
    const un = f.Un || [];
    const unUsed = [];
    for (let h = 0; h < un.length; h++) if (un[h]) unUsed.push([h, un[h]]);
    return JSON.stringify({
      i: FRAME,
      instanceCount: wd.If,
      instances: inst,
      images: imgs,
      unUsed,
      meta: snap(f, 2, new Set()),
    });
  }

  if (MODE === 'program') {
    const un = f.un || {};
    const groups = (un.Xd || []).map((g, gi) => ({ gi, raw: snap(g, 8, new Set()) }));
    const summary = {}, samples = {};
    for (const k of Object.keys(un)) {
      const v = un[k];
      if (k === 'Uc' || k === 'l') { summary[k] = 'skipped (app/loader object)'; continue; }
      if (Array.isArray(v)) {
        summary[k] = v.length;
        samples[k] = v.slice(0, 4).map(x => snap(x, 4, new Set()));
      } else if (v && typeof v === 'object') {
        summary[k] = Object.keys(v).length;
        samples[k] = snap(v, 3, new Set());
      } else summary[k] = v;
    }
    return JSON.stringify({ i: FRAME, groupCount: groups.length, groups, summary, samples });
  }

  return JSON.stringify({ error: 'unknown mode ' + MODE });
})()
```

`tools/run_oracle_dumps.sh`:

```bash
#!/usr/bin/env bash
# Dumps the live runtime's compiled data into assets_raw/dump/oracle/ (Plan 2, Task 1).
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=assets_raw/dump/oracle
mkdir -p "$OUT/program"

need() { curl -sf --max-time 3 "$1" >/dev/null || { echo "MISSING: $2" >&2; exit 1; }; }
need "http://127.0.0.1:8765/" "start: (cd web_probe && python3 -m http.server 8765)"
need "http://127.0.0.1:9222/json/version" "start: headless chrome with --remote-debugging-port=9222"

run_js() { # mode frame outfile
  sed -e "s/__MODE__/$1/" -e "s/__FRAME__/$2/" tools/dump_oracle.js > /tmp/opencode/_dump.js
  node tools/cdp_eval.js "$(cat /tmp/opencode/_dump.js)" > "$3"
  printf '%s %s -> %s (%s bytes)\n' "$1" "$2" "$3" "$(wc -c < "$3")"
}

run_js objects 0 "$OUT/objects.json"
for i in $(seq 0 15); do
  run_js frame "$i" "$OUT/frame_$i.json"
  run_js program "$i" "$OUT/program/frame_$i.json"
done
echo "done"
```

Steps:

```bash
chmod +x tools/run_oracle_dumps.sh
tools/run_oracle_dumps.sh
python3 - <<'EOF'
import json, pathlib
o = pathlib.Path('assets_raw/dump/oracle')
for i in range(16):
    fr = json.load(open(o / f'frame_{i}.json'))
    pr = json.load(open(o / f'program/frame_{i}.json'))
    print(f"[{i:2d}] inst={fr['instanceCount']:4d} images={len(fr['images']):4d} groups={pr['groupCount']:4d}")
EOF
git add tools/dump_oracle.js tools/run_oracle_dumps.sh assets_raw/dump/oracle
git commit -m "Add live-runtime oracle dumper (frames, program, objects)"
```

Acceptance (all verified 2026-10-18): 16 frame files, 16 program files, one objects file (`count=638`, every def has a non-null `oc` — the dumper first calls `d.load(u.file)`, the app's still-live ccj reader, for every def). Per-frame instance counts match `assets_raw/dump/frames.json` (`instCount`) and the ctfak JSON (`len(frames[i].events.groups)`); frame 0 `len(unUsed)` = 1953 = old dump `UnNonzero`. Objects census matches `defs_merged.json` counts: `oc.Ti.nf` = 442, `oc.$e` = 631, `oc.jb` = 145, `oc.Wc` = 181. In program dumps: 10517 group entries / 13198 params, `Ad == len(s)` on every entry, zero `$more`/truncation markers inside groups. Decoded field mapping (live ↔ old names): `ya = (num<<16)|(objectType&0xFFFF)`, `ke`=objectInfo, `ub`=objectInfoList, `Ma`=flags, `mP`=identifier, `Ad`=param count, `s`=params; token encoding `code = (num<<16)|(type&0xFFFF)`, literals `{code:0xFFFF,value:N}`, object refs `{code:0x100002,Qh,Ub,Lh}` (e.g. System(type=0,num=2) = 131072=0x20000). `ba.images` accumulates across frame loads (317 after frame 1), so per-frame image usage comes from `f.Un` (`unUsed`), never `len(images)`; image field semantics (verified numerically): `wb`=atlas (0=direct file), `rd`/`sd`=crop x/y, `width`/`height`=crop size, `Zg`/`$g`=hotspot (≈centers).

---

## Task 2 — `tools/compile_bundle.py`

Inputs: `assets_raw/dump/oracle/*` (authoritative), `assets_raw/dump/defs_merged.json` (objects cross-check), `assets_raw/ccj_dump/.../application.json` (layers, instance layers/parents, ink fields, cross-check), `assets_raw/out/` (resource files). Output: `build/bundle/bundle.bin` + `build/bundle/bundle.json` (the same model, human-readable; the big program blob goes to `build/bundle/program.json` and is referenced from `bundle.json`).

```python
#!/usr/bin/env python3
"""Compile the engine bundle (v1) from oracle dumps and recovered assets.

bundle.bin layout is frozen in docs/superpowers/plans/2026-10-18-m1-data-pipeline.md.
"""
import json, struct, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ORACLE = ROOT / "assets_raw/dump/oracle"
OUT = ROOT / "build/bundle"
CTFAK = ROOT / "assets_raw/ccj_dump/Ultimate Custom Night/JSON/application.json"
ASSETS = ROOT / "assets_raw/out"

S_STRINGS, S_FRAMES, S_OBJECTS, S_IMAGES, S_SOUNDS, S_PROGRAM, S_RESOURCES = range(1, 8)

class Strings:
    def __init__(self): self.table, self.index = [], {}
    def get(self, s):
        if s not in self.index:
            self.index[s] = len(self.table); self.table.append(s)
        return self.index[s]

def pstr(x): return struct.pack("<I", len(x)) + x.encode("utf-8")

def w_frames(frames, strings):
    recs = []
    for f in frames:
        r = struct.pack("<Iii", strings.get(f["name"]), f["width"], f["height"])
        r += struct.pack("<I", len(f["layers"]))
        for L in f["layers"]:
            r += struct.pack("<IiiI", strings.get(L["name"]), L["x_coeff"], L["y_coeff"], L["flags"])
        r += struct.pack("<IIiiI", f["bg_mode"], f["bg_image"], f["bg_x"], f["bg_y"], f["bg_color"])
        r += struct.pack("<I", len(f["instances"]))
        for n in f["instances"]:
            r += struct.pack("<IiiIIIi", n["object"], n["x"], n["y"], n["layer"], n["order"],
                             n["parent_type"], n["parent_handle"])
        r += struct.pack("<I", len(f["images"]))
        r += b"".join(struct.pack("<I", h) for h in f["images"])
        recs.append(r)
    out = struct.pack("<I", len(recs)) + b"".join(struct.pack("<I", 0) for _ in recs)
    offs, body = [], b""
    base = 4 + 4 * len(recs)
    for r in recs:
        offs.append(base + len(body)); body += r
    out = struct.pack("<I", len(recs)) + b"".join(struct.pack("<I", o) for o in offs) + body
    return out

# w_objects / w_images / w_sounds / w_resources follow the frozen layouts the same way.
```

(The full file continues with `w_objects` (values + animations per the layout), `w_images`, `w_resources`, `parse_frame(i)` handling the live field mapping (`named.order/object/x/y`, metad fields), `parse_objects()` from the new oracle dump, `parse_images()` (union of per-frame images deduped by handle; file = `M{atlas:04d}.png` if atlas else `{handle:04d}.png`; verify each file exists in `assets_raw/out`), `parse_program()` (JSON passthrough), and `main()` with assertions:
- 16 frames; names/sizes/instance counts equal the old live dump `frames.json` and equal ctfak (`name`, `width`, `height`, `len(instances)`).
- per-frame image usage equals `len(unUsed)` in the oracle frame dump (frame 0 = 1953); every image's atlas resolves to a file that exists in `assets_raw/out` (`M{atlas:04d}.png`, atlas 0 → `{handle:04d}.png`).
- per-frame group count equals `groupCount` in the program dumps and `len(ctfak frames[i].events.groups)`.
- objects: 638; handles/names match defs_merged.json (`oc.Ti.nf`=442, `oc.$e`=631, `oc.jb`=145, `oc.Wc`=181).
Then writes `bundle.json` (same data, pretty) and `bundle.bin`, and prints a summary table.)

Steps:

```bash
python3 tools/compile_bundle.py
ls -la build/bundle/
git add tools/compile_bundle.py
git commit -m "Add bundle compiler (oracle dumps + assets -> bundle.bin)"
```

Acceptance: the script prints `assert ok` lines and a summary; `build/bundle/bundle.bin` exists; the JSON and binary agree (the script asserts both were built from the same in-memory model).

---

## Task 3 — `engine/data/bundle.h` / `bundle.cpp` + `platform/host/bundle_dump`

`engine/data/bundle.h`:

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ucn::data {

enum SectionType : uint32_t {
    kStrings = 1, kFrames = 2, kObjects = 3, kImages = 4,
    kSounds = 5, kProgram = 6, kResources = 7,
};

struct Section { uint32_t type = 0, offset = 0, size = 0; };

class Bundle {
public:
    bool open(const std::string& path, std::string* error);

    const std::vector<uint8_t>& bytes() const { return bytes_; }
    const Section* find(uint32_t type) const;
    const std::vector<std::string>& strings() const { return strings_; }
    const std::string* string_at(uint32_t index) const;

    uint32_t frame_count() const;
    std::string frame_name(uint32_t i) const;
    uint32_t frame_instance_count(uint32_t i) const;
    uint32_t object_count() const;
    uint32_t image_count() const;
    uint32_t resource_count() const;
    uint32_t program_json_len() const;

private:
    template <typename T> T read(uint32_t at) const;
    bool parse_strings(std::string* error);

    std::vector<uint8_t> bytes_;
    std::vector<Section> sections_;
    std::vector<std::string> strings_;
};

} // namespace ucn::data
```

`engine/data/bundle.cpp`: reads the whole file, checks magic/version/section count/total size, parses the table and strings; the accessors walk section records (`off[i]`) per the frozen layout. Every bounds access is checked; failures return 0 / empty and set `error` in `open` only (accessors stay total).

`platform/host/bundle_dump/main.cpp`:

```cpp
#include <cstdio>
#include "data/bundle.h"

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: bundle_dump BUNDLE.BIN\n"); return 2; }
    ucn::data::Bundle bundle;
    std::string error;
    if (!bundle.open(argv[1], &error)) { std::fprintf(stderr, "open failed: %s\n", error.c_str()); return 1; }
    std::printf("frames=%u objects=%u images=%u resources=%u program_json=%u bytes strings=%zu\n",
        bundle.frame_count(), bundle.object_count(), bundle.image_count(),
        bundle.resource_count(), bundle.program_json_len(), bundle.strings().size());
    for (uint32_t i = 0; i < bundle.frame_count(); i++)
        std::printf("frame[%2u] name=%-16s instances=%u\n", i, bundle.frame_name(i).c_str(),
            bundle.frame_instance_count(i));
    return 0;
}
```

`platform/host/bundle_dump/Makefile`: host `g++ -std=c++17 -O2 -Wall -Wextra -I../../../engine -o build/bundle_dump main.cpp ../../../engine/data/bundle.cpp` (three levels up: the tool lives at `platform/host/bundle_dump`).

Steps:

```bash
make -C platform/host/bundle_dump
platform/host/bundle_dump/build/bundle_dump build/bundle/bundle.bin | tee /tmp/opencode/bundle_dump.txt
git add engine/data platform/host/bundle_dump
git commit -m "Add bundle reader and host bundle_dump tool"
```

Acceptance: printed `frames=16`; per-frame instance counts identical to the oracle summary from Task 1; `objects=638`.

---

## Task 4 — Record results, check the M0 build

```bash
cd /home/mohamd/Projects/UCN_MVP_Xbox360
git log --oneline | head -5
tail -n 5 /tmp/opencode/ucn-toolchain-build.log 2>/dev/null || true
```

M1 is done when Tasks 1–3 acceptance lines all hold. Next plan: M2 (host renderer: bundle → sprites on a framebuffer, compared against probe screenshots), then M3 (the program interpreter running against the live oracle).

### Results (2026-10-18)

- Oracle dumps: 16 frame files, 16 program files, `objects.json` (638 defs, every `oc` populated by calling each def's own `load()` against the app's live ccj reader). Acceptance re-verified after the final dumper code: instance/group counts match `frames.json` and ctfak on all 16 frames, `unUsed`=1953 everywhere, 10517 program entries / 13198 params with `Ad == len(s)`, objects census 442/631/145/181.
- `tools/compile_bundle.py` runs clean; `build/bundle/bundle.bin` = **2,334,665 bytes** (strings 1566): FRAMES 16 records (layers 1–17, instances 1215 total), OBJECTS 638 (7136 animations / 7216 direction entries, zero alterable values), IMAGES 2100 (1953 atlased + 147 direct files), SOUNDS 0, PROGRAM 2520 groups / 10517 entries (1,792,034-byte JSON), RESOURCES 997 (388 textures + 609 audio; the `.cc1/.cch` containers excluded).
- Host reader acceptance line: `frames=16 objects=638 images=2100 resources=997 program_json=1792034 bytes strings=1566` and every per-frame `instances=` equals the oracle.
- Format tweaks landed before implementation (all listed in the changelog above): `back_to`, `bg_color` (frames 6/8/9/11 are white), image `flags` widened to `u16`.
- M0 in parallel: toolchain (gcc 15.3.0) and xsdk built; `ucn360_m0.xex` built and passes its checks under Xenia; the console drop is packaged in `dist/ucn360-m0/` (see the M0 plan for the handoff text).

## Out of scope for M1

- Sounds section contents (audio plan), movement/Wc deep data (interpreter plan), the final program encoding (interpreter plan), DXT conversion (console renderer plan).
