#!/usr/bin/env python3
"""Print a readable window of minified JS (Runtime.js) around a marker.

Splits on ; { } and inserts newlines, skipping string literals, so reverse
engineering can read one statement or block at a time.
"""
import sys, re

PATH = "/home/mohamd/Downloads/ucn/src/Runtime.js"

def load():
    return open(PATH, "r", encoding="utf-8", errors="replace").read()

def find_all(src, needle, limit=40):
    out = []
    for m in re.finditer(re.escape(needle), src):
        out.append(m.start())
        if len(out) >= limit:
            break
    return out

def format_js(src, start, length):
    chunk = src[start:start + length]
    out = []
    in_str = None
    i = 0
    while i < len(chunk):
        c = chunk[i]
        if in_str:
            out.append(c)
            if c == "\\" and i + 1 < len(chunk):
                out.append(chunk[i + 1]); i += 2; continue
            if c == in_str:
                in_str = None
            i += 1
            continue
        if c in "\"'`":
            in_str = c; out.append(c); i += 1; continue
        if c in ";{}":
            out.append(c)
            if c == ";":
                out.append("\n")
            elif c == "{":
                out.append("\n")
            else:
                out.append("\n")
            i += 1
            continue
        out.append(c)
        i += 1
    text = "".join(out)
    # indent by brace depth
    depth = 0
    lines = []
    for line in text.split("\n"):
        stripped = line.strip()
        if stripped.startswith("}"):
            depth = max(depth - 1, 0)
        lines.append(("    " * depth) + stripped)
        depth += line.count("{") - (1 if stripped.startswith("}") else 0)
    return "\n".join(lines)

def main():
    src = load()
    if len(sys.argv) < 2:
        print("usage: js_slice.py MARKER [count] [window]")
        return
    marker = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    window = int(sys.argv[3]) if len(sys.argv) > 3 else 4000
    hits = find_all(src, marker, count)
    if not hits:
        print(f"no hits for {marker!r}")
        return
    for n, at in enumerate(hits):
        print(f"===== hit {n} of {len(hits)} at {at} =====")
        print(format_js(src, max(0, at - window // 4), window))

if __name__ == "__main__":
    main()
