#!/usr/bin/env bash
# M3 acceptance: every frame, run with no input, matches its live recording (build/oracle/frame_F.jsonl from
# tools/cdp_tick_oracle.js --ticks 2000 --frame F) for every tick it stays in that frame.
#   tools/m3_parity.sh [FRAME...]
set -uo pipefail
cd "$(dirname "$0")/.."
make -s -C platform/host/game_host >/dev/null || exit 1
frames=("$@")
[ ${#frames[@]} -eq 0 ] && frames=($(seq 0 15))
fail=0
for f in "${frames[@]}"; do
  ref=build/oracle/frame_$f.jsonl
  [ -f "$ref" ] || { echo "frame $f: no recording"; fail=1; continue; }
  ours=build/oracle/ours_$f.jsonl
  n=$(wc -l < "$ref")
  rm -f "$ours"
  if ! platform/host/game_host/build/game_host build/bundle/bundle.bin "$f" "$n" "$ours" 2>/dev/null; then
    echo "frame $f: game_host failed"; fail=1; continue
  fi
  python3 tools/compare_state.py "$ref" "$ours" | head -12 || true
  python3 tools/compare_state.py "$ref" "$ours" >/dev/null || fail=1
done
exit $fail
