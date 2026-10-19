#!/usr/bin/env python3
"""Build the console's sound pack: every sample decoded once on the host (ffmpeg) to 8-bit mono mu-law.

    python3 tools/sound_pack.py [out/sounds.pak] [--rate 22050]

Sources: assets_raw/out/<handle:04d>.ogg, else .mp3 (the order the runtime prefers). Layout (little-endian):
    "UCNS", u32 version 2, u32 count, u32 rate
    count x { i32 handle, u32 offset, u32 frames }
    then mu-law bytes (G.711), each sample 4-byte aligned.
Mu-law halves PCM16: 85 minutes at 22 kHz are 110 MB, which the console keeps in memory for its audio thread.
"""
import json
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "assets_raw/out"


def decode(handle, rate):
    for ext in ("ogg", "mp3"):
        p = SRC / f"{handle:04d}.{ext}"
        if p.exists():
            r = subprocess.run(["ffmpeg", "-v", "error", "-i", str(p), "-ac", "1", "-ar", str(rate), "-acodec", "pcm_mulaw", "-f", "mulaw", "-"],
                               capture_output=True)
            if r.returncode == 0:
                return r.stdout
            print(f"warning: {p.name}: {r.stderr.decode()[:200]}", file=sys.stderr)
    return None


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else ROOT / "build/xenon/sounds.pak"
    rate = int(sys.argv[sys.argv.index("--rate") + 1]) if "--rate" in sys.argv else 22050
    handles = sorted(int(k) for k in json.load(open(ROOT / "assets_raw/dump/oracle/sounds.json")) if k != "settings")
    with ThreadPoolExecutor(8) as ex:
        pcm = list(ex.map(lambda h: decode(h, rate), handles))
    table, data = b"", bytearray()
    base = 16 + 12 * len(handles)
    missing = 0
    for h, p in zip(handles, pcm):
        if p is None:
            missing += 1
            p = b""
        while len(data) % 4:
            data += b"\0"
        table += struct.pack("<iII", h, base + len(data), len(p))
        data += p
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(b"UCNS" + struct.pack("<III", 2, len(handles), rate) + table + bytes(data))
    print(f"{len(handles)} samples ({missing} missing) at {rate} Hz -> {out}, {out.stat().st_size / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
