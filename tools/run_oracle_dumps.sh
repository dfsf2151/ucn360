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
