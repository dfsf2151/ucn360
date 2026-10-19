#!/usr/bin/env bash
# Record each frame's runtime layer values (T: La flags, zj/Bj scroll coefficients) into
# assets_raw/dump/oracle/layers.json ({"<frame>": [[La, zj, Bj], ...]}): the ccj fields the bundle had for these
# are not in the runtime's units, and the scroll code (center display, layer offsets) needs the runtime's.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=assets_raw/dump/oracle/layers.json
EXPR='JSON.stringify(window.__rt.Uc.frame.pb.map(l => [l.La, l.zj, l.Bj]))'
tmp=$(mktemp -d)
for f in $(seq 0 15); do
  node tools/cdp_tick_oracle.js --ticks 1 --frame "$f" --eval "$EXPR" 2>/dev/null | tail -1 > "$tmp/$f.json"
  printf 'frame %s: %s\n' "$f" "$(cat "$tmp/$f.json")"
done
python3 - "$tmp" "$OUT" <<'PY'
import json, sys
out = {str(f): json.load(open(f"{sys.argv[1]}/{f}.json")) for f in range(16)}
json.dump(out, open(sys.argv[2], "w"))
print("->", sys.argv[2])
PY
rm -rf "$tmp"
