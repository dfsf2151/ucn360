#!/usr/bin/env bash
# Record each sample's decoded duration as the live runtime loaded it, frame by frame, into
# assets_raw/dump/oracle/sounds.json ({"<handle>": ms, ..., "settings": [multiple_sounds Rt, sound_on Cy,
# default_volume JA]}). The engine's sound channels play for these durations, the same ones
# tools/oracle_shim.js uses for `onended`.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=assets_raw/dump/oracle/sounds.json
EXPR='(() => { const u = window.__rt.Uc, s = u.Sg, out = {}; for (const q of s.bl || []) if (q && q.buffer) out[q.handle] = q.buffer.duration * 1000; out.settings = [u.kc.Rt ? 1 : 0, u.kc.Cy ? 1 : 0, u.kc.JA]; return JSON.stringify(out); })()'
tmp=$(mktemp -d)
for f in $(seq 0 15); do
  node tools/cdp_tick_oracle.js --ticks 1 --frame "$f" --eval "$EXPR" > "$tmp/$f.json" 2>/dev/null
  printf 'frame %s: %s entries\n' "$f" "$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))))' "$tmp/$f.json")"
done
python3 - "$tmp" "$OUT" <<'PY'
import json, sys, glob
merged = {}
for p in sorted(glob.glob(sys.argv[1] + "/*.json")):
    for k, v in json.load(open(p)).items():
        assert merged.get(k, v) == v, f"sample {k}: {merged[k]} vs {v}"
        merged[k] = v
json.dump(dict(sorted(merged.items(), key=lambda kv: (kv[0] == 'settings', int(kv[0]) if kv[0] != 'settings' else 0))), open(sys.argv[2], 'w'), indent=0)
print(f"{len(merged) - 1} samples -> {sys.argv[2]}")
PY
rm -rf "$tmp"
