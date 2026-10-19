#!/usr/bin/env python3
"""Diff a game_host state record against the live oracle's, tick by tick.

    python3 tools/compare_state.py REF.jsonl OURS.jsonl [--ticks N] [--all]

Both files hold one JSON record per game tick (tools/oracle_capture.js / platform/host/game_host). The run is
compared for the frame it starts in: when the game leaves that frame on its own, the reference's ticks after
that belong to the next frame (whose start depends on the browser's load time, so its own recording covers it),
and game_host stops at the same tick. Instances are matched by slot. Numbers compare with a 1e-9 relative
tolerance. Prints the first difference (every difference with --all, capped) and exits non-zero on one.
"""
import json
import sys

FIELDS = ["slot", "oi", "x", "y", "image", "anim", "anim_frame", "dir", "sprite_flags", "values", "counter", "text"]


def same(a, b):
    if isinstance(a, (int, float)) and isinstance(b, (int, float)) and not isinstance(a, bool):
        return a == b or abs(a - b) <= 1e-9 * max(1.0, abs(a), abs(b))
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(same(a[k], b[k]) for k in a)
    return a == b


def load(path):
    """Records of the first frame, in order, and whether the run left that frame afterwards."""
    recs = [json.loads(line) for line in open(path)]
    recs = [r for r in recs if r["t"] >= 1]
    if not recs:
        return [], False
    f0 = recs[0]["f"]
    out = []
    for r in recs:
        if r["f"] != f0 or (out and r["t"] <= out[-1]["t"]):
            return out, True
        out.append(r)
    return out, False


def diff_tick(a, b):
    msgs = []
    for k in ("t", "f", "rf", "seed"):
        if not same(a[k], b[k]):
            msgs.append(f"{k}: ref {a[k]} ours {b[k]}")
    ra = {o[0]: o for o in a["o"]}
    rb = {o[0]: o for o in b["o"]}
    for s in sorted(set(ra) | set(rb)):
        if s not in rb:
            msgs.append(f"slot {s} (oi {ra[s][1]}): only in ref")
            continue
        if s not in ra:
            msgs.append(f"slot {s} (oi {rb[s][1]}): only in ours")
            continue
        for i, name in enumerate(FIELDS):
            if not same(ra[s][i], rb[s][i]):
                msgs.append(f"slot {s} (oi {ra[s][1]}) {name}: ref {ra[s][i]!r} ours {rb[s][i]!r}")
    return msgs


def main():
    ref, ref_left = load(sys.argv[1])
    ours, _ = load(sys.argv[2])
    limit = int(sys.argv[sys.argv.index("--ticks") + 1]) if "--ticks" in sys.argv else None
    show_all = "--all" in sys.argv
    if limit is not None:
        ref = [r for r in ref if r["t"] <= limit]
        ours = [r for r in ours if r["t"] <= limit]
    diffs = 0
    for i, a in enumerate(ref):
        if i >= len(ours):
            print(f"tick {a['t']}: ours stopped after {len(ours)} ticks")
            return 1
        msgs = diff_tick(a, ours[i])
        if msgs:
            diffs += 1
            print(f"tick {a['t']}:")
            for m in msgs[:20]:
                print("   ", m)
            if not show_all or diffs >= 10:
                return 1
    if len(ours) > len(ref) and ref_left:
        print(f"ref leaves frame {ref[0]['f']} after tick {ref[-1]['t']}; ours keeps going")
        return 1
    if diffs:
        print(f"{diffs} ticks differ")
        return 1
    end = f", then the game leaves frame {ref[0]['f']} (both sides)" if ref_left else ""
    print(f"ok: frame {ref[0]['f'] if ref else '?'}: {len(ref)} ticks match{end}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
