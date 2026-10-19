# Plan 4 — M3: Event interpreter and tick-state parity

- **Date:** 2026-10-18
- **Spec:** `docs/superpowers/specs/2026-10-18-ucn-xbox360-port-design.md` (§12 verification, §13 "آلة الأحداث")
- **Prev:** Plan 3 (M2 host renderer — done, `38a01d1`); M0 present path `2236d30`
- **Goal (M3):** run the bundle's event program on the host exactly as the live runtime does: conditions, actions, expressions, object selection, the per-tick loop, and the object state they drive (animations, movements, values, counters, text). Exit criterion: for every frame, a ≥2000-tick run with no input has zero state differences against the live game, and frame 1 (menu) renders pixel-identical through `frame_render` once the world state drives it.

## What the probes already told us

- **The runtime is Clickteam Fusion's HTML5 runtime**, Closure-minified (`web_probe/src/Runtime.js`, 326 KB). Some class names survive (`CND_COMPAREGCONST_EQ`, `EXP_MOD`, …); most are two-letter names. `esbuild` (from the global `@shopify/cli` install) pretty-prints it to 16,634 lines, which is what the line numbers below refer to.
- **Factories** (pretty-printed lines): actions `R.create` (2100; exact-code switch, then `code & 0xFFFF0000` for object types ≥ 2), conditions `B.create` (3026, same two-level shape), expressions `ba.create` (3862), parameters `Ia.create` (10936), event groups `G.create` (9070). An event code is `(num << 16) | (object_type & 0xFFFF)`, both halves signed 16-bit.
- **The program uses** 36 condition codes, 53 action codes, 16 expression codes and 23 parameter kinds over 2520 groups / 10517 items. Object types in play: −1 system, −2 sound, −3 storyboard, −4 timer, −5 create, −6 keyboard/mouse, −7 player, 2 active, 3 text, 7 counter, 32 Ini extension (actions 80/86/87/93, expression 82). Type 33 ("Perspective", 6 objects) has no events.
- **The event engine is `N.prototype`** (CEventProgram): selection (`vf` first, `Re` next, `Bd` deselect, `Il` add, `pk` expression object, `ab` action objects with the repeat-for-each-instance loop), group evaluation `sf` with the OR variants, action execution `My`, triggered-condition lists `ye`/`nd` indexed by code. The per-tick loop is `vH` (CRun game loop): `Rf = now − frame_start`, `Go = delta`, `Lb += 1`, then input, object handlers, events.
- **Randomness** is the runtime's own 16-bit LCG: `Dw = (31415·Dw + 1) & 0xFFFF`, `random(n) = (Dw·n) >>> 16`, seeded at frame init (`Mt`) from `Date` (or the app header seed). `Math.random` is only used in two visual effects.
- **Time** enters only through `Date` and rAF/`setTimeout` pacing. A fake clock makes the live game fully deterministic: `tools/cdp_tick_oracle.js` records bit-identical runs (two 600-tick runs of frame 0 compare equal).
- Movements in use: 607 static, 5 mouse (type 1), ~16 path (type 5). Animations: speed min/max, loop count, back-to frame, image lists (already in the bundle). Global values/strings are unused; alterable values are used through actions (2,31)/(2,32)/(2,33) and condition (2,−42).

## Decisions

1. **Faithful port, not a re-design.** The C++ engine mirrors the runtime's structure (event lists by code, the selection list with its `Xc` generation counters, the OR/repeat semantics, the expression operator-precedence stack). Every ported function names its source (`Runtime.js` pretty line + minified name) in a comment. Where semantics are guessed, the oracle decides.
2. **Numbers are doubles**, as in JS; integer conversions (`Math.floor`, `>>> 16`, `& 65535`) are reproduced exactly where the runtime does them.
3. **Clock:** tick `t` runs at `floor(t·1000/60)` ms of frame time. The oracle steps 16/17/17 ms to hit exactly that; the console engine will read its own 60 Hz vblank count the same way.
4. **Seed:** tests force the LCG seed at frame init (`--seed`, default 12345) on both sides. The shipped game keeps the runtime's rule (header seed, else `time & 65535`).
5. **State record** (one JSON line per tick, `tools/oracle_capture.js`): tick, frame, frame timer, LCG state, and per instance `[slot, oi, x, y, image, anim, anim_frame, dir, sprite_flags, values, counter, text]`. `game_host` writes the same shape; `tools/compare_state.py` prints the first differing tick/object/field.
6. **PROGRAM becomes typed** (bundle format v2): groups → items → parameters → expression tokens, all fixed-width little-endian records with string indices, compiled from `program.json` by `tools/compile_bundle.py`. The JSON blob stays for one release as a cross-check, then goes.
7. **Object definitions grow** the data the interpreter needs: movement records (type, speed, path steps, mouse bounds), counter defs (min/max/initial/display), text defs (font, colour, strings), alterable-value defaults, creation flags. Each addition goes into the M1 format changelog.
8. **Name map:** `docs/runtime-map.md` records every minified name the port relies on (class, field, flag constant) with its meaning and evidence. It is the reference for anyone reading the C++ against `Runtime.js`.

## Conventions

- Work from `/home/mohamd/Projects/UCN_MVP_Xbox360` on branch `m3/event-interpreter`. Probe server `:8765` and headless Chrome CDP `:9222` up (see M2 plan).
- Oracle records go under `build/oracle/` (ignored), one `frame_<F>.jsonl` per frame run.
- Commit after each task.

---

## Task 0 — Land this plan and the tick oracle

Files: this plan, `tools/oracle_shim.js`, `tools/cdp_tick_oracle.js`, `tools/oracle_capture.js`.

```bash
node tools/cdp_tick_oracle.js --ticks 600 --out build/oracle/frame_0.jsonl
node tools/cdp_tick_oracle.js --ticks 600 --out /tmp/f0b.jsonl && cmp build/oracle/frame_0.jsonl /tmp/f0b.jsonl
```

Acceptance: the two recordings are identical.

## Task 1 — Runtime map

`tools/runtime_factmap.py`: from the pretty-printed `Runtime.js` and `program.json`, list every used condition/action/expression/parameter code with its runtime class and line. Seed `docs/runtime-map.md` with that table plus the engine names above (`N.prototype` selection/evaluation, `vH`, `Mt`, the sprite flags `r.*`, the instance fields `v/u/Sb/Ue/Ea/b/X/T/D/pa`).

Acceptance: every used code maps to a class (extension codes map to the Ini object's handlers).

## Task 2 — Bundle v2: typed program and interpreter object data

`tools/compile_bundle.py` writes `PROGRAM` v2 and the new object fields; `engine/data/bundle.{h,cpp}` reads them; `bundle_dump` prints group/item/token counts and asserts them against `program.json` (2520 / 10517 / token total).

## Task 3 — `engine/evm/`: the event engine core

`program.{h,cpp}` (typed view over PROGRAM, prepared event lists by code and object type, OR groups, qualifiers), `select.{h,cpp}` (the selection list: first/next/deselect/add, action-object iteration with repeat), `expr.{h,cpp}` (token evaluator with the operator stack), `run.{h,cpp}` (group evaluation `sf`, action execution `My`, triggered lists `ye`/`nd`, the tick `vH`). Unit tests in `platform/host/evm_test` for selection and expression precedence on hand-built programs.

## Task 4 — `engine/game/`: world and objects

Frame start (instance creation from the frame's instance list, in creation order), destruction, layers; actives (animation stepping with the runtime's speed/loop/back-to rules, direction, image, show/hide/flags, alterable values/strings, effects/ink), counters (value, min/max), texts (string), the static/mouse/path movements, frame timer and LCG.

## Task 5 — Conditions, actions, expressions

The 36 + 53 + 16 codes of the runtime map, plus the Ini extension's handlers backed by an in-memory store (save file is M7). Each one cites its runtime class.

## Task 6 — `platform/host/game_host` + `tools/compare_state.py`

`game_host BUNDLE.bin FRAME TICKS out.jsonl [--seed S]` writes the state record. `compare_state.py ref.jsonl ours.jsonl` prints the first difference and exits non-zero. Iterate frame by frame (0, 1, …, 15) until every frame matches for ≥2000 ticks with no input.

## Task 7 — Frame 1 pixels

`frame_render` gains `--state game_host.jsonl@TICK` (draw the world at a tick instead of the frame's initial state). Frame 1 at the oracle's capture tick matches the live canvas like frame 6 does.

## Results (2026-10-18/19, executed)

**State parity.** `tools/m3_parity.sh` runs every frame on `game_host` and diffs it against its live recording
(`build/oracle/frame_F.jsonl`, 2000 ticks, seed 12345, no input). Frames that leave on their own (3 after tick 119,
5 after 765, 9 after 1284, 14 after 807) match up to that tick and both sides end the frame on the same tick; the
others match all 2000 ticks, frame 8's scrolling included. Frame 13 ("gain item") has no recording: it leaves on
its first tick, before the oracle can observe it loaded, so it is covered only through the frames that jump to it
(M4 input runs); its layer values in `layers.json` are inferred (flags 19, coefficients 1, as every 1:1 frame). Every instance's slot, object, position (fractional, path movements included),
image, animation and frame, direction, sprite flags, alterable values, counter and text, plus the frame timer and
the LCG state, are identical.

**Pixels.** `frame_render --ticks=300` draws the world state (`engine/render/world.cpp`) and frame 1 (menu)
matches the live canvas captured at the same tick (`cdp_tick_oracle --png 300:...`): mean abs diff 0.149, 0.01%
strong pixels, all of them the "1.031" version string (text objects need font rasterisation, not drawn yet); the
rest is +-1 blend rounding.

**What the oracle needed** (all in `tools/cdp_tick_oracle.js` / `tools/oracle_shim.js`):
- a fake clock stepped 16/17/17 ms, so the frame timer is exactly `floor(t*1000/60)`;
- the LCG seed forced at every frame init (`Mt`);
- WebAudio `onended` driven by the fake clock (sample durations recorded in `sounds.json`);
- reaching a frame through the runtime's own jump path, then restarting it with `tK`: right after an asset load
  the browser loop has two callbacks pending and the first step would run two ticks at one clock time;
- emptying the layer containers before that restart (the reused frame object keeps the old run's sprites drawn);
- clearing `localStorage` (the Ini files) before every run;
- tick detection by the game's counter (tick 1 can run while the app still flags loading).

**Runtime facts the port relies on** (beyond `docs/runtime-map.md`): activation areas use the display size
(`Pn`/`On`), not the frame size; the mouse is seen through `Eu` (window position minus the centring offset);
missing animations share their substitute's object (`La.ZC`), so switching to one keeps the frame counter;
the HTML5 build has no Perspective extension (type 33 objects are never created); layers draw in ascending
index with each layer's gc / Ib / Pa containers in that order; scrolling ("center display") moves the
`z.FF` objects with the display.

**Not covered by the no-input runs** (M4 work, now part of the full-XEX push): clicks, keys, the pad, in-game
frame transitions (their start depends on the browser's load time, so each frame's own recording covers it),
text rendering.

## Out of scope for M3

- Input (mouse/keyboard/pad) scenarios and frame-to-frame navigation by input (M4).
- Sound playback (actions run and keep state; no audio out) and the save file (M7).
- Collision masks beyond what the no-input runs need (M4).
