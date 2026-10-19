#!/usr/bin/env python3
"""Recover files from a split zip whose parts are partially missing.

Parts are fixed-size chunks of one big zip. We parse the central directory
(located in the last present part), then for every entry check whether its
local header + data lie entirely inside the byte ranges we actually have.
Recoverable entries are extracted and CRC32-verified.
"""
import os
import struct
import sys
import zlib

SRC = "/home/mohamd/Downloads/ucn/src"
PREFIX = "resources.zip.part"
NUM_PARTS = 20
OUT = sys.argv[1] if len(sys.argv) > 1 else "/home/mohamd/Projects/UCN_MVP_Xbox360/assets_raw/out"
REPORT = sys.argv[2] if len(sys.argv) > 2 else "/home/mohamd/Projects/UCN_MVP_Xbox360/assets_raw/report.txt"

# ---- load available parts -> byte ranges of the virtual zip ----
parts = {}
for n in range(1, NUM_PARTS + 1):
    p = os.path.join(SRC, f"{PREFIX}{n}")
    if os.path.exists(p):
        parts[n] = os.path.getsize(p)
if not parts:
    sys.exit("no parts found")
print("present parts:", sorted(parts))

ranges = []  # (start, end, path)
off = 0
for n in range(1, NUM_PARTS + 1):
    if n in parts:
        ranges.append((off, off + parts[n], os.path.join(SRC, f"{PREFIX}{n}")))
        off += parts[n]
    else:
        # assume full-size part for offset math; if the missing part were the
        # last one this would differ, but part 20 exists here.
        size = 16121441 if n < NUM_PARTS else parts.get(n, 16121428)
        off += size
total_size = off
print("virtual zip size:", total_size)

def read_range(a, b):
    """read [a,b) across all present parts; None if any byte is unavailable"""
    off = a
    chunks = []
    for s, e, p in ranges:
        if off >= b:
            break
        if off < s:
            return None        # gap
        if off < e:
            with open(p, "rb") as f:
                f.seek(off - s)
                take = min(e, b) - off
                chunks.append(f.read(take))
                off += take
    if off < b:
        return None
    return b"".join(chunks)

# ---- find EOCD in the tail of the virtual file ----
tail_start = max(0, total_size - 65557)
tail = read_range(tail_start, total_size)
if tail is None:
    # read max available tail from last present part
    s, e, p = ranges[-1]
    with open(p, "rb") as f:
        tail = f.read()
    tail_start = s

idx = tail.rfind(b"PK\x05\x06")
if idx < 0:
    sys.exit("EOCD not found")
eocd = tail[idx:idx + 22]
_, disk, cd_disk, n_this, n_total, cd_size, cd_off, _ = struct.unpack("<IHHHHIIH", eocd)
print(f"EOCD: entries={n_total} cd_size={cd_size} cd_off={cd_off}")

cd = read_range(cd_off, cd_off + cd_size)
if cd is None:
    sys.exit("central directory not fully present")

# ---- parse central directory ----
entries = []
pos = 0
while pos + 46 <= len(cd):
    sig = cd[pos:pos + 4]
    if sig != b"PK\x01\x02":
        break
    (ver, ver_need, flags, method, mtime, mdate, crc, csize, usize,
     nlen, xlen, clen, disk_start, iattr, eattr, lho) = struct.unpack(
        "<HHHHHHIIIHHHHHII", cd[pos + 4:pos + 46])
    name = cd[pos + 46:pos + 46 + nlen].decode("cp437")
    pos += 46 + nlen + xlen + clen
    entries.append(dict(name=name, method=method, flags=flags, crc=crc,
                        csize=csize, usize=usize, lho=lho, nlen=nlen, xlen=xlen))
print("parsed entries:", len(entries))

# ---- recover ----
os.makedirs(OUT, exist_ok=True)
ok = bad = miss = 0
lines = []
for e in entries:
    if e["flags"] & 0x1:
        lines.append(f"ENCRYPTED {e['name']}")
        miss += 1
        continue
    hdr = read_range(e["lho"], e["lho"] + 30)
    if hdr is None or hdr[:4] != b"PK\x03\x04":
        miss += 1
        continue
    # Local header may have zeroed sizes when bit 3 (data descriptor) is set;
    # authoritative sizes/CRC come from the central directory.
    _, _, _, _, _, _, _, _, _, lnlen, lxlen = struct.unpack("<IHHHHHIIIHH", hdr)
    method = e["method"]
    csize = e["csize"]
    data_start = e["lho"] + 30 + lnlen + lxlen
    blob = read_range(data_start, data_start + csize)
    if blob is None:
        miss += 1
        continue
    try:
        if method == 0:
            raw = blob
        elif method == 8:
            raw = zlib.decompressobj(-15).decompress(blob)
        else:
            lines.append(f"SKIP method={method} {e['name']}")
            miss += 1
            continue
    except Exception as ex:
        bad += 1
        lines.append(f"DECOMP-FAIL {e['name']}: {ex}")
        continue
    if len(raw) != e["usize"]:
        bad += 1
        lines.append(f"SIZE-FAIL {e['name']} got={len(raw)} want={e['usize']}")
        continue
    if e["crc"] and (zlib.crc32(raw) & 0xFFFFFFFF) != e["crc"]:
        bad += 1
        lines.append(f"CRC-FAIL {e['name']}")
        continue
    dest = os.path.join(OUT, e["name"])
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with open(dest, "wb") as f:
        f.write(raw)
    ok += 1

lines.insert(0, f"recovered={ok} crc_fail={bad} not_present={miss} total={len(entries)}")
with open(REPORT, "w") as f:
    f.write("\n".join(lines) + "\n")
print(lines[0])
