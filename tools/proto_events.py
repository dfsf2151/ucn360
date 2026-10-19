#!/usr/bin/env python3
"""Prototype parser for UCN HTML5 runtime ccj event chunks (build 291).

Mirrors ctfak-cpp Events.cpp, with the runtime-variant extra i32 fields after
ERes/ERev headers. Goal: land exactly on the '<<ER' terminator of every frame.
"""
import struct, sys

class R:
    def __init__(self, buf, pos=0):
        self.b = buf; self.p = pos
    def eof(self): return self.p >= len(self.b)
    def u8(self):
        v = self.b[self.p]; self.p += 1; return v
    def i8(self):
        v = struct.unpack_from("<b", self.b, self.p)[0]; self.p += 1; return v
    def i16(self):
        v = struct.unpack_from("<h", self.b, self.p)[0]; self.p += 2; return v
    def u16(self):
        v = struct.unpack_from("<H", self.b, self.p)[0]; self.p += 2; return v
    def i32(self):
        v = struct.unpack_from("<i", self.b, self.p)[0]; self.p += 4; return v
    def u32(self):
        v = struct.unpack_from("<I", self.b, self.p)[0]; self.p += 4; return v
    def f64(self):
        v = struct.unpack_from("<d", self.b, self.p)[0]; self.p += 8; return v
    def f32(self):
        v = struct.unpack_from("<f", self.b, self.p)[0]; self.p += 4; return v
    def ascii(self, n=-1):
        if n >= 0:
            s = self.b[self.p:self.p+n]; self.p += n
            return s.decode("ascii", "replace")
        e = self.b.index(b"\x00", self.p); s = self.b[self.p:e]; self.p = e+1
        return s.decode("ascii", "replace")
    def wide(self, n=-1):
        out = []
        if n >= 0:
            for _ in range(n):
                out.append(self.u16())
        else:
            while True:
                c = self.u16()
                if c == 0: break
                out.append(c)
        return "".join(chr(c) for c in out)

stats = {"unknown_param": 0, "unknown_expr": 0, "warn": []}

def read_expr_item(r):
    start = r.p
    ot = r.i16(); num = r.i16()
    if ot == 0 and num == 0:
        return None
    size = r.i16()
    if ot == -1:
        if num == 0: r.i32()
        elif num == 3: r.wide()
        elif num == 23: r.f64(); r.f32()
        elif num in (24, 50): r.i32(); r.i32()
        else: stats["unknown_expr"] += 1
    elif ot >= 2 or ot == -7:
        r.u16(); r.i16()
        if num in (16, 19): r.i16()
    r.p = start + size
    return True

def read_expr(r):
    r.i16()  # comparsion
    while True:
        if read_expr_item(r) is None:
            break
        if r.p + 4 > len(r.b):
            break

def read_param(r):
    start = r.p
    size = r.i16(); code = r.i16()
    if code == 1:
        r.i16(); r.u16(); r.i16()
    elif code in (2, 42):
        r.i32(); r.i32()
    elif code in (3, 4, 10, 11, 12, 17, 26, 31, 43, 57, 58, 60, 61):
        r.i16()
    elif code in (5, 25, 29, 34, 48, 56, 67, 70):
        r.i32()
    elif code in (6, 7, 35, 36):
        r.i16(); r.u16(); r.wide()
    elif code in (9, 21):
        r.u16(); r.i16(); r.i16(); r.i16(); r.i16(); r.i16(); r.i32()
        r.i16(); r.i16(); r.i16(); r.u16(); r.u16()
    elif code == 13:
        r.i32(); r.i32()
    elif code in (14, 44):
        r.u16()
    elif code in (15, 22, 23, 27, 28, 45, 46, 52, 53, 54, 59, 62):
        read_expr(r)
    elif code == 16:
        r.u16(); r.i16(); r.i16(); r.i16(); r.i16(); r.i16(); r.i32()
        r.i16(); r.i16(); r.i16()
    elif code == 18:
        r.u16(); r.i16(); r.i16(); r.i16(); r.i16(); r.i16(); r.i32()
        r.i16(); r.i16(); r.i16(); r.u16(); r.u16()
        r.p += 4; r.i16()
    elif code == 19:
        for _ in range(4): r.i16()
    elif code == 24:
        for _ in range(4): r.u8()
    elif code == 32:
        r.u8(); r.u8()
    elif code == 33:
        r.i16(); r.ascii(260); r.ascii()
    elif code == 38:
        r.u16(); r.u16(); r.wide()
    elif code == 39:
        r.i32(); r.i16()
    elif code in (40, 41, 64):
        r.ascii()
    elif code in (47, 51):
        r.i16(); r.i16()
    elif code == 49 or code == 50:
        r.i16()
    elif code == 55:
        n = r.i16(); r.i16(); r.i16()
        if n > 0: r.p += n
    elif code == 68:
        flags = r.i32(); r.i32(); r.i32()
        n = 0; mask = 1
        while n < 4:
            if (flags & mask) == 0: break
            mask <<= 4; n += 1
        md = 4
        for _ in range(n):
            r.i32(); r.i32()
            if flags & md: r.f64()
            else: r.i32(); r.p += 4
            md <<= 4
    elif code == 69:
        cnt = r.i32()
        for _ in range(cnt * 2): r.i16()
        r.p += 4
    else:
        stats["unknown_param"] += 1
        stats["warn"].append(f"unknown param code {code} at {start}")
        return  # leave position untouched (ctfak behaviour)
    r.p = start + size

def read_cond(r):
    r.u16(); r.i16(); r.i16(); r.u16(); r.i16()
    r.i8(); r.i8(); n = r.u8(); r.u8(); r.i16()
    for _ in range(n):
        read_param(r)

def read_action(r):
    r.u16(); r.i16(); r.i16(); r.u16(); r.i16()
    r.i8(); r.i8(); n = r.u8(); r.u8()
    for _ in range(n):
        read_param(r)

def read_group(r):
    r.i16()  # size (negative)
    nconds = r.u8(); nacts = r.u8()
    r.u16()          # flags
    r.i16()          # line
    r.i32()          # isRestricted
    r.i32()          # restrictCpt
    for _ in range(nconds):
        read_cond(r)
    for _ in range(nacts):
        read_action(r)
    return nconds, nacts

def split_chunks(buf, start, lim):
    pos = start; out = []
    while pos + 8 <= lim:
        cid, flag, size = struct.unpack_from("<hhI", buf, pos)
        if size < 0 or pos + 8 + size > lim: break
        out.append((cid, flag, size, pos)); pos += 8 + size
    return out

def parse_events(ev, verbose=False):
    r = R(ev)
    ident = r.ascii(4)
    assert ident == "ER>>", ident
    r.i16(); r.i16(); r.i16()
    for _ in range(17): r.i16()
    qc = r.i16()
    for _ in range(qc): r.u16(); r.i16()
    total_groups = 0
    while True:
        if r.p + 4 > len(ev): return None, "no terminator"
        ident = r.ascii(4)
        if ident == "ERes":
            r.i32()          # size
            saved = r.p
            nxt = ev[r.p:r.p+4]
            if nxt not in (b"ERev", b"ERop", b"<<ER"):
                if verbose: print(f"   ERes extra: {r.u32()} @ {saved}")
                else: r.i32()
        elif ident == "ERev":
            size = r.i32()
            saved = r.p
            nxt = ev[r.p:r.p+4]
            if nxt not in (b"ERes", b"ERop", b"<<ER"):
                extra = r.u32()
                if verbose: print(f"   ERev extra: {extra} @ {saved}")
            end = r.p + size
            while r.p < end:
                try:
                    read_group(r)
                except Exception as ex:
                    return None, f"group parse error at {r.p}: {ex}"
                total_groups += 1
            if r.p != end:
                return None, f"groups overran: {r.p} != {end}"
        elif ident == "ERop":
            r.i32()
        elif ident == "<<ER":
            return (total_groups, r.p), None
        else:
            return None, f"unknown section {ident!r} at {r.p-4}"

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else \
        "/home/mohamd/Projects/UCN_MVP_Xbox360/assets_raw/ccj/Application.ccj"
    data = open(path, "rb").read()
    frames = [c for c in split_chunks(data, 16, len(data)) if c[0] == 13107]
    print(f"frames: {len(frames)}")
    names = ["INTRO","Frame 1","Frame 2","GF scare","game over","WIN","control map",
             "Director","Chica High School","Chica Final","Samurai","Samurai Final",
             "Final","gain item","lost 2","your own"]
    for i, (cid, flag, size, at) in enumerate(frames):
        payload = data[at+8:at+8+size]
        ev = None
        for scid, sflag, ssize, sat in split_chunks(payload, 0, len(payload)):
            if scid == 13117:
                ev = payload[sat+8:sat+8+ssize]
        if ev is None:
            print(f"[{i:2d}] no events chunk"); continue
        res, err = parse_events(ev, verbose=(i == 0))
        nm = names[i] if i < len(names) else "?"
        if err:
            print(f"[{i:2d}] {nm:<18} len={len(ev):6d} FAIL: {err}")
        else:
            groups, end = res
            tail = ev[end:]
            print(f"[{i:2d}] {nm:<18} len={len(ev):6d} groups={groups:4d} end={end} tail={tail!r}")
    print("stats:", stats["unknown_param"], "unknown params;", stats["unknown_expr"], "unknown expr items")
    for w in stats["warn"][:10]: print("  ", w)

if __name__ == "__main__":
    main()
