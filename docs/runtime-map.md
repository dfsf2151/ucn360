# Runtime map — the minified names the port relies on

Line numbers are in `build/runtime_pretty.js`: `web_probe/src/Runtime.js` pretty-printed by esbuild
(`esbuild web_probe/src/Runtime.js --log-level=warning > build/runtime_pretty.js`, 16,634 lines). The runtime is
Clickteam Fusion's HTML5 runtime; the classic C++/Java runtime names are given where the structure matches.

## Event codes

`code = (num << 16) | (object_type & 0xFFFF)`, both halves signed 16-bit. The factories (`R.create` actions,
`B.create` conditions, `ba.create` expressions) switch on the exact code first, then on `code & 0xFFFF0000` for
the per-object-type codes; anything else goes to the extension. `line` is the factory `case` that builds the
class; the class's `prototype` follows it in the file. Table: `python3 tools/runtime_factmap.py
build/runtime_pretty.js build/bundle/program.json --md`, behaviour column from reading each prototype.

Conditions have two entry points: `W(run)` when evaluated in the event loop, `Ja(run, instance)` when the
condition is the trigger of a triggered event. "Negatable" means the result goes through `B.Iv`/`B.Jf`/`B.Ak`
with the item's NOT flag (`Qe & Q.Ij`). Conditions that filter call `vf` (first selected instance), `Re` (next),
`Bd` (deselect current) and succeed while any instance stays selected.

| kind | type | num | code | uses | class | line | behaviour |
|---|---|---|---|---|---|---|---|
| C | -7 | -4 | -196615 | 4 | `qc` | 3231 | player joystick pressed: (Vr & Mc)[player] has all bits of s[0]; triggered form tests pc |
| C | -6 | -8 | -458758 | 6 | `kc` | 3201 | mouse button held: s[0].key 1/2/4 = left/right/middle, h.Jc[button] |
| C | -6 | -7 | -393222 | 87 | `lc` | 3204 | user clicks on object s[1] with button/click type s[0] (triggered: pc, clicked oi Jw, instance Hw) |
| C | -6 | -5 | -262150 | 1 | `mc` | 3210 | user clicks with button/click type s[0] (triggered on pc) |
| C | -6 | -4 | -196614 | 138 | `nc` | 3213 | mouse pointer is over object s[0] (eA; negatable) |
| C | -6 | -2 | -65542 | 19 | `oc` | 3219 | key s[0].key is held (h.Jc[key]; negatable) |
| C | -6 | -1 | -6 | 63 | `pc` | 3222 | upon pressing key s[0].key (held and newly pressed: KG) |
| C | -4 | -8 | -458756 | 246 | `tc` | 3165 | every s[0] ms: s[1] keeps the countdown (Ds = 0 means unset); subtracts Go each tick, fires and reloads at <= 0 |
| C | -4 | -7 | -393220 | 10 | `sc` | 3168 | timer reaches s[0] ms: fires once, latched in s[1].value by tick Lb |
| C | -4 | -2 | -65540 | 2 | `ic` | 3183 | timer Rf <= s[0] ms |
| C | -4 | -1 | -4 | 4 | `jc` | 3186 | timer Rf > s[0] ms |
| C | -3 | -1 | -3 | 71 | `hc` | 3162 | start of frame: Lb <= 2 |
| C | -2 | -3 | -131074 | 1 | `fc` | 3135 | no sample is playing (kc.oI; negatable) |
| C | -2 | -1 | -2 | 36 | `gc` | 3138 | sample s[0].ts is not playing (kc.nI; negatable) |
| C | -1 | -7 | -393217 | 429 | `cc` | 3105 | only one action when event loops (sets group flag G.Sm; My skips when Lb is Hi or Hi+1) |
| C | -1 | -6 | -327681 | 89 | `dc` | 3108 | run this event once (group flag G.kp latched) |
| C | -1 | -3 | -131073 | 85 | `ec` | 3117 | compare two general values (s[0] op s[1], op = s[1].Ei) |
| C | -1 | -1 | -1 | 50 | `Za` | 3123 | always |
| C | 2 | -42 | -2752510 | 1468 | `$a` | 3335 | compare alterable value #s[0].value with constant s[1] (filters selection) |
| C | 2 | -34 | -2228222 | 17 | `uc` | 3359 | pick one object at random (random(count)) |
| C | 2 | -32 | -2097150 | 15 | `vc` | 3365 | compare number of objects (Dg = live count) with s[0] |
| C | 2 | -29 | -1900542 | 39 | `wc` | 3374 | object is visible: r.dg (1, hidden) clear; negatable (filters selection) |
| C | 2 | -28 | -1835006 | 10 | `xc` | 3377 | object is invisible: r.dg (1, hidden) set (filters selection) |
| C | 2 | -27 | -1769470 | 22 | `yc` | 3380 | compare alterable value #expr(s[0]) (code 53) or #s[0].value with expression s[1] |
| C | 2 | -23 | -1507326 | 8 | `zc` | 3392 | object overlaps a backdrop (collision test with pa.Nm / pa.ee; negatable) |
| C | 2 | -21 | -1376254 | 5 | `Ac` | 3398 | movement event: b.Kc == O.tF (path movement end/node; triggered) |
| C | 2 | -17 | -1114110 | 13 | `Bc` | 3410 | compare X position with s[0] |
| C | 2 | -16 | -1048574 | 8 | `Cc` | 3413 | compare Y position with s[0] |
| C | 2 | -7 | -458750 | 5 | `Dc` | 3440 | object is stopped: b.aa (speed) == 0 (negatable) |
| C | 2 | -4 | -262142 | 196 | `Ec` | 3449 | object overlaps object s[0] (wA, bounding box / mask) |
| C | 2 | -3 | -196606 | 1 | `Fc` | 3452 | animation s[0] is playing: X.Xh == s[0] and X.Ld != 0 |
| C | 2 | -2 | -131070 | 19 | `Gc` | 3455 | animation s[0] is over: X.Xh == s[0] and X.Ld == 0 (also triggered) |
| C | 2 | -1 | -65534 | 2 | `Hc` | 3458 | compare animation frame X.Pf with s[0] |
| C | 3 | -29 | -1900541 | 1 | `wc` | 3374 | as (2,-29) for a text object |
| C | 7 | -81 | -5308409 | 1950 | `rc` | 3306 | compare counter value pa with s[0] (filters selection) |
| C | 7 | -4 | -262137 | 100 | `Ec` | 3449 | as (2,-4) for a counter |
| A | -5 | 0 | 65531 | 69 | `wb` | 2401 | create object (s[0] create param: oi, position, direction, layer) |
| A | -3 | 0 | 65533 | 2 | `sb` | 2263 | next frame (zb = h.rp) |
| A | -3 | 2 | 196605 | 38 | `tb` | 2269 | jump to frame s[0] (zb = h.qp, Dm = frame) |
| A | -3 | 4 | 327677 | 3 | `ub` | 2275 | end/restart application (zb = h.pp when h.yA) |
| A | -3 | 7 | 524285 | 4 | `vb` | 2284 | center display at position s[0] (eK) |
| A | -2 | 1 | 131070 | 16 | `ob` | 2191 | stop all samples |
| A | -2 | 11 | 786430 | 466 | `pb` | 2206 | play sample s[0] on channel s[1] (loop flag in s[0].RC) |
| A | -2 | 12 | 851966 | 32 | `qb` | 2209 | play sample s[0] on channel s[1], s[2] loops |
| A | -2 | 17 | 1179646 | 84 | `rb` | 2224 | set channel s[0] volume s[1] (0..100) |
| A | -2 | 18 | 1245182 | 4 | `da` | 2227 | no-op in this runtime |
| A | 2 | 1 | 65538 | 166 | `Fb` | 2529 | set position (s[0] position param; may set direction) |
| A | 2 | 2 | 131074 | 1 | `Gb` | 2532 | set X = floor(s[0]) |
| A | 2 | 6 | 393218 | 17 | `Hb` | 2544 | set movement speed (B.la.Je) |
| A | 2 | 13 | 851970 | 3 | `Ib` | 2565 | select movement s[0] (B.cK) |
| A | 2 | 15 | 983042 | 3 | `Jb` | 2571 | X.zo = true (stop animation) |
| A | 2 | 16 | 1048578 | 2 | `Kb` | 2574 | X.zo = false (start animation) |
| A | 2 | 17 | 1114114 | 270 | `Lb` | 2577 | change animation sequence to s[0] (X.Zp) |
| A | 2 | 19 | 1245186 | 2 | `Mb` | 2583 | force animation frame (X.oG) |
| A | 2 | 23 | 1507330 | 4 | `Nb` | 2595 | set direction s[0] (& 31) |
| A | 2 | 24 | 1572866 | 162 | `Ob` | 2598 | destroy (marks F.Me; text objects with h.Qs fade instead) |
| A | 2 | 26 | 1703938 | 455 | `Pb` | 2604 | hide: `D.fo()` sets r.dg (1, hidden), clears r.eg, stops flashing (h.ZI) |
| A | 2 | 27 | 1769474 | 279 | `Qb` | 2607 | show: `D.Kv()` clears r.dg, sets r.eg (32), stops flashing (h.$I) |
| A | 2 | 28 | 1835010 | 6 | `Rb` | 2610 | flash every s[0] ms |
| A | 2 | 31 | 2031618 | 1596 | `Sb` | 2619 | set alterable value #s[0] = s[1] |
| A | 2 | 32 | 2097154 | 156 | `Ub` | 2623 | add s[1] to alterable value #s[0] |
| A | 2 | 33 | 2162690 | 81 | `Wb` | 2627 | subtract s[1] from alterable value #s[0] |
| A | 2 | 40 | 2621442 | 7 | `Yb` | 2652 | force animation direction / restore (X.nG) |
| A | 2 | 58 | 3801090 | 3 | `Zb` | 2706 | move to front of layer (be(0)) |
| A | 2 | 61 | 3997698 | 1 | `$b` | 2715 | move to layer s[0] (1-based) |
| A | 2 | 63 | 4128770 | 6 | `ac` | 2721 | set ink effect by name s[0] (Add/Invert/Sub/Mono/Blend/XOR/OR/AND) |
| A | 2 | 65 | 4259842 | 226 | `bc` | 2727 | set alpha coefficient 255 - s[0] (D.Nb top byte, flag r.Ns) |
| A | 2 | 85 | 5570562 | 57 | `yb` | 2449 | set scale s[0] (Rw), s[1] != 0 = resample |
| A | 2 | 88 | 5767170 | 4 | `zb` | 2458 | set angle s[0] mod 360, s[1] != 0 = resample |
| A | 3 | 1 | 65539 | 1 | `Fb` | 2529 | as (2,1) for a text object |
| A | 3 | 24 | 1572867 | 1 | `Ob` | 2598 | as (2,24) for a text object |
| A | 3 | 26 | 1703939 | 21 | `Pb` | 2604 | as (2,26) for a text object |
| A | 3 | 27 | 1769475 | 68 | `Qb` | 2607 | as (2,27) for a text object |
| A | 3 | 88 | 5767171 | 114 | `xb` | 2431 | set text string s[0] (fD) |
| A | 7 | 1 | 65543 | 3 | `Fb` | 2529 | as (2,1) for a counter |
| A | 7 | 24 | 1572871 | 4 | `Ob` | 2598 | as (2,24) for a counter |
| A | 7 | 26 | 1703943 | 27 | `Pb` | 2604 | as (2,26) for a counter |
| A | 7 | 27 | 1769479 | 17 | `Qb` | 2607 | as (2,27) for a counter |
| A | 7 | 28 | 1835015 | 4 | `Rb` | 2610 | as (2,28) for a counter |
| A | 7 | 65 | 4259847 | 10 | `bc` | 2727 | as (2,65) for a counter |
| A | 7 | 80 | 5242887 | 612 | `Ab` | 2461 | set counter to s[0] (gu, mn) |
| A | 7 | 81 | 5308423 | 91 | `Bb` | 2464 | add s[0] to counter (OG) |
| A | 7 | 82 | 5373959 | 37 | `Cb` | 2467 | subtract s[0] from counter (RG) |
| A | 7 | 85 | 5570567 | 4 | `Db` | 2476 | set counter colour 1 (PG) |
| A | 7 | 86 | 5636103 | 4 | `Eb` | 2479 | set counter colour 2 (QG) |
| A | 32 | 80 | 5242912 | 7 | `extension` | — | Ini extension action 80 |
| A | 32 | 86 | 5636128 | 7 | `extension` | — | Ini extension action 86 |
| A | 32 | 87 | 5701664 | 39 | `extension` | — | Ini extension action 87 |
| A | 32 | 93 | 6094880 | 1 | `extension` | — | Ini extension action 93 |
| E | -1 | -2 | -65537 | 353 | `Xc` | 4064 | ")" (no-op token) |
| E | -1 | -1 | -1 | 83 | `Wc` | 4061 | "(" sub-expression |
| E | -1 | 0 | 65535 | 6844 | `bb` | 3896 | integer constant |
| E | -1 | 1 | 131071 | 236 | `Uc` | 3899 | random(expr) |
| E | -1 | 3 | 262143 | 204 | `Nc` | 3905 | string constant |
| E | -1 | 4 | 327679 | 1 | `Vc` | 3908 | str$(expr) |
| E | -1 | 23 | 1572863 | 16 | `Ic` | 3959 | double constant (marks the expression as float) |
| E | 0 | 0 | 0 | 7520 | `va` | 3866 | end of expression |
| E | 0 | 2 | 131072 | 205 | `Qc` | 3869 | + |
| E | 0 | 4 | 262144 | 36 | `Rc` | 3872 | - (unary when it starts an operand) |
| E | 0 | 6 | 393216 | 59 | `Sc` | 3875 | * |
| E | 0 | 8 | 524288 | 21 | `Tc` | 3878 | / (integer division unless the expression is float; x/0 = 0) |
| E | 2 | 16 | 1048578 | 459 | `Jc` | 4306 | alterable value #Lh of object Ub |
| E | 2 | 81 | 5308418 | 7 | `Yc` | 4226 | X scale b.lb of object Ub |
| E | 7 | 80 | 5242887 | 311 | `Zc` | 4235 | counter value pa of object Ub |
| E | 32 | 82 | 5373984 | 33 | `extension` | — | Ini extension expression 82 |

## Event engine — `N.prototype` (CEventProgram)

| name | line | meaning |
|---|---|---|
| `Wb` | — | evaluation generation; bumped per group evaluation. An OI whose `Xc != Wb` has no selection yet (all instances implicitly) |
| `Kg` | — | "conditions already false" during OR evaluation: selection calls return nothing |
| `vf(oi)` | 8004 | first instance of an OI (or qualifier, `oi & 0x8000`) for a filtering condition; starts the OI's selection from all instances if untouched this generation; `Hc` = count |
| `Re()` | 8059 | next instance in the current selection walk |
| `Bd()` | 8079 | remove the current instance from the selection (`ud--`) |
| `Il(inst)` | 8083 | add an instance to its OI's selection (triggers, "pick") |
| `pk(oi)` | 8130 | instance an expression reads: first selected, else first live |
| `ab(item)` | 8155 | instance an action applies to; with several selected, sets `Ac`/`kj` so `My` repeats the action once per instance (`ai` = loop index) |
| `sf(start, inst)` | 8410 | evaluate the groups of one event list from `start` (normal, OR, OR-filtered variants) and run the true ones |
| `My(group)` | 8470 | run a group's actions; honours once/not-always flags; repeats the flagged actions while `kj` |
| `ye(code)` / `nd(inst, code)` | 8335 / 8341 | fire the triggered-condition list for a code (globally / for one instance) |
| `$p(run)` | 8707 | per-frame preparation: event lists by code, qualifiers, OR markers |
| `jw()` | 8673 | reset group/parameter state at frame start (timer `Ds = 0`, group flags) |

Group (`G`) fields: `Yb` condition count, `xe` action count, `ib` items, `Ka` flags (`G.Sm` not-always,
`G.kp` once, `G.Ws` repeat-N (`Hl` counts down), `G.Rm` restrict-for (`Hl` = next allowed `Rf/10`), `G.Us`
inactive, `G.Vs` has OR (`G.Bx` the filtered-OR variant), `G.Ax` any of the flagged behaviours, `G.Xs` shuffle
selected positions after the actions (`nH`)), `Hi`/`Hl` their counters (sf 8410, My 8470).
Item fields: `ya` code, `ke` object type, `ub` OI (or qualifier), `Ma` action flags (`R.ji` repeat, `Q.jl` no
instance), `Qe` condition flags (`Q.Ij` NOT), `Ad` parameter count, `s` parameters.

## Run — `app.G` (CRun)

| name | meaning |
|---|---|
| `Lb` | tick counter of the current frame (1 on the first tick) |
| `Rf` | frame timer, ms since frame start; `Go` the last tick's delta |
| `Vk` | frame start time (Date ms) |
| `Dw` | LCG state: `random(n)`: `Dw = (31415*Dw + 1) & 0xFFFF; return (Dw*n) >>> 16` (line 6926) |
| `H` | instance slots; `P` OI list (one per object type in the frame) |
| `Mt()` | frame init (line 6140): allocates `H`, seeds `Dw` (`A.ir` or `time & 65535`) |
| `tu()` | frame timer/scroll reset (6724) |
| `vH()` | the per-tick game loop (6762) |
| `i` | the event program (`N`) |

## Instance fields

| name | meaning |
|---|---|
| `Sb` | slot index in `run.H` |
| `Ue` | object (OI) handle |
| `Ea` | object type (2 active, 3 text, 7 counter, ≥32 extension) |
| `Fb` | the OI list entry (`P[...]`): `kb` first instance, `Dg` live count, `fb`/`ud` selection, `aj` name |
| `Zb` / `mc` | next instance of the same OI / next in the selection (`0x80000000` = end) |
| `v`, `u` | x, y |
| `K`, `J`, `ha`, `ia` | width, height, hotspot x/y of the current image |
| `b` | sprite/position block: `Ha` image, `Qa` angle, `lb`/`qb` scale x/y, `Ua` direction, `aa` speed, `L` changed |
| `X` | animation: `Xh` current animation, `Pf` frame, `Ld` playing, `zo` stopped by action |
| `B` | movement (`B.la` the active movement) |
| `T` | alterable values `Va[]`, strings `te[]` |
| `D` | sprite render state: `M` flags (`r.dg` 1 hidden, `r.PF` 2 static movement, `r.Dt` 4 inactive (outside the activation area), `r.tl` 16 resample, `r.eg` 32 shown / flash phase, `r.Tj` 64 mouse-tested, `r.Sj` 128 box collisions), `Xb` ink effect (+`r.Ns` 4096 alpha), `Nb` effect parameter, `Xk`/`Em` flash |
| `pa` | counter value (type 7); `ei` text string (type 3) |
| `Z` | instance flags (`F.Me` destroyed) |
