#!/usr/bin/env python3
"""List every event code the program uses with the runtime class that implements it.

    python3 tools/runtime_factmap.py RUNTIME_PRETTY.js build/bundle/program.json [--md]

RUNTIME_PRETTY.js is web_probe/src/Runtime.js pretty-printed by esbuild (any esbuild works):
    esbuild web_probe/src/Runtime.js --log-level=warning > build/runtime_pretty.js

The runtime builds conditions, actions and expressions in three factories. Each is an exact-code switch whose
default falls into a second switch on `code & 0xFFFF0000` (the num half, for object types >= 2); codes neither
switch knows go to the extension's own handler. The factories are found by their `X.create = function` header
and the switch nesting under it, not by line number, so a re-pretty-print still works.
"""
import collections
import json
import re
import sys

FACTORIES = {"A": "R", "C": "B", "E": "ba"}  # kind -> factory object name


def split(code):
    code &= 0xFFFFFFFF
    s = lambda v: v - 0x10000 if v >= 0x8000 else v
    return s(code & 0xFFFF), s(code >> 16)  # (object type, num)


def signed32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v >= (1 << 31) else v


def factory_switches(lines, name):
    """Return (exact, masked): code -> (class, line) for the factory `name.create`."""
    start = next(i for i, l in enumerate(lines) if re.match(r"\s*" + re.escape(name) + r"\.create = function", l))
    exact, masked, pend = {}, {}, []
    table, depth = exact, 0
    for i in range(start, len(lines)):
        s = lines[i]
        if "& 4294901760" in s and "switch" in s:
            table = masked
        for m in re.finditer(r"case (-?\d+):", s):
            pend.append(int(m.group(1)))
        m = re.search(r"= new ([A-Za-z_$][\w$]*)\(", s)
        if m and pend:
            for c in pend:
                table[c] = (m.group(1), i + 1)
            pend = []
        depth += s.count("{") - s.count("}")
        if i > start and depth <= 0:
            break
    return exact, masked


def main():
    rt, prog = sys.argv[1], sys.argv[2]
    md = "--md" in sys.argv
    lines = open(rt).read().split("\n")
    tables = {k: factory_switches(lines, v) for k, v in FACTORIES.items()}
    p = json.load(open(prog))
    used = {k: collections.Counter() for k in "CAE"}
    for f in p["frames"]:
        for g in f["groups"]:
            r = g["raw"]
            for k, it in enumerate(r["ib"]):
                used["C" if k < r["Yb"] else "A"][it["ya"]] += 1
                for prm in it.get("s") or []:
                    for tok in prm.get("Fa") or []:
                        used["E"][tok.get("code")] += 1
    missing = 0
    rows = []
    for kind in "CAE":
        exact, masked = tables[kind]
        for code, n in sorted(used[kind].items(), key=lambda kv: split(kv[0])):
            t, num = split(code)
            hit = exact.get(code) or masked.get(signed32(code & 0xFFFF0000))
            cls, line = hit if hit else (("extension" if t >= 32 else "UNMAPPED"), 0)
            missing += cls == "UNMAPPED"
            rows.append((kind, t, num, code, n, cls, line))
    if md:
        print("| kind | type | num | code | uses | class | line |")
        print("|---|---|---|---|---|---|---|")
        for r in rows:
            print("| %s | %d | %d | %d | %d | `%s` | %s |" % (r[0], r[1], r[2], r[3], r[4], r[5], r[6] or "—"))
    else:
        for r in rows:
            print("%s (%d,%d) %d x%d %s %s" % r)
    print("%d codes, %d unmapped" % (len(rows), missing), file=sys.stderr)
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
