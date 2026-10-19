#!/usr/bin/env python3
"""Build a runnable web_probe directory for the UCN HTML5 runtime.

- Layout: web_probe/{index.html, src/Runtime.js, resources/*}
- Copies every recovered asset from assets_raw/out into resources/.
- Creates placeholders for entries missing from the split zip (from central
  directory manifest): transparent PNG for images, silent audio otherwise.
"""
import os
import shutil
import struct
import subprocess
import sys
import zlib

ROOT = "/home/mohamd/Projects/UCN_MVP_Xbox360"
SRC_PARTS = "/home/mohamd/Downloads/ucn/src"
RECOVERED = os.path.join(ROOT, "assets_raw/out")
PROBE = os.path.join(ROOT, "web_probe")
RES = os.path.join(PROBE, "resources")
GAME_DIR = "/home/mohamd/Downloads/ucn"

PART_SIZE = 16121441
NUM_PARTS = 20

def read_manifest():
    """Return list of entry names from the zip central directory."""
    off = 0
    ranges = []
    for n in range(1, NUM_PARTS + 1):
        p = os.path.join(SRC_PARTS, f"resources.zip.part{n}")
        if os.path.exists(p):
            sz = os.path.getsize(p)
            ranges.append((off, off + sz, p))
            off += sz
        else:
            off += PART_SIZE if n < NUM_PARTS else 16121428
    total = off
    def read_range(a, b):
        for s, e, p in ranges:
            if a >= s and b <= e:
                with open(p, "rb") as f:
                    f.seek(a - s)
                    return f.read(b - a)
        return None
    s, e, p = ranges[-1]
    tail = read_range(max(s, e - 65557), e)
    i = tail.rfind(b"PK\x05\x06")
    _, _, _, _, n_total, cd_size, cd_off, _ = struct.unpack("<IHHHHIIH", tail[i:i+22])
    cd = read_range(cd_off, cd_off + cd_size)
    names = []
    pos = 0
    while pos + 46 <= len(cd):
        if cd[pos:pos+4] != b"PK\x01\x02":
            break
        f = struct.unpack("<HHHHHHIIIHHHHHII", cd[pos+4:pos+46])
        name = cd[pos+46:pos+46+f[9]].decode("cp437")
        pos += 46 + f[9] + f[10] + f[11]
        names.append(name)
    return names

def write_min_png(path):
    # 1x1 transparent PNG
    sig = b"\x89PNG\r\n\x1a\n"
    def chunk(typ, data):
        c = struct.pack(">I", len(data)) + typ + data
        return c + struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF)
    ihdr = struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)
    idat = zlib.compress(b"\x00\x00\x00\x00\x00")
    with open(path, "wb") as f:
        f.write(sig + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b""))

def write_silent_audio(path):
    ext = os.path.splitext(path)[1].lower()
    if ext in (".mp3", ".ogg", ".m4a", ".wav"):
        cmd = ["ffmpeg", "-y", "-loglevel", "error", "-f", "lavfi", "-i",
               "anullsrc=r=22050:cl=mono", "-t", "0.3", path]
        try:
            subprocess.run(cmd, check=True, timeout=30)
            return True
        except Exception as ex:
            print("ffmpeg failed:", ex)
    with open(path, "wb") as f:
        f.write(b"\x00" * 64)
    return False

def main():
    os.makedirs(os.path.join(PROBE, "src"), exist_ok=True)
    os.makedirs(RES, exist_ok=True)

    shutil.copy2(os.path.join(GAME_DIR, "index.html"),
                 os.path.join(PROBE, "index_original.html"))
    shutil.copy2(os.path.join(GAME_DIR, "src/Runtime.js"),
                 os.path.join(PROBE, "src/Runtime.js"))

    names = read_manifest()
    print("manifest entries:", len(names))

    copied = 0
    for name in names:
        src = os.path.join(RECOVERED, name)
        dst = os.path.join(RES, name)
        if os.path.exists(src):
            shutil.copy2(src, dst)
            copied += 1
        else:
            ext = os.path.splitext(name)[1].lower()
            if ext == ".png":
                write_min_png(dst)
            elif ext in (".mp3", ".ogg", ".m4a", ".wav"):
                write_silent_audio(dst)
            else:
                open(dst, "wb").write(b"")
    print("copied recovered:", copied, "placeholders:", len(names) - copied)

if __name__ == "__main__":
    main()
