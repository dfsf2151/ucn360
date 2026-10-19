#!/usr/bin/env python3
"""Compile the engine bundle (v2) from oracle dumps and recovered assets.

Layout: docs/superpowers/plans/2026-10-18-m1-data-pipeline.md, section "Bundle format v1"; PROGRAM is v2 (typed,
docs/superpowers/plans/2026-10-18-m3-event-interpreter.md Task 2 and w_program below).

Sources
  assets_raw/dump/oracle/                   live-runtime dumps: frames, program, objects
  assets_raw/dump/frames.json               older live dump (cross-check)
  assets_raw/dump/defs_merged.json          older object dump (cross-check)
  assets_raw/ccj_dump/.../application.json  ctfak JSON (layers, instance parents, ink fields)
  assets_raw/out/                           recovered asset files

Outputs
  build/bundle/bundle.bin    the file the engine reads on host and console
  build/bundle/bundle.json   the same model, human-readable (program groups excluded)
  build/bundle/program.json  the dumped program as JSON (cross-check for the typed PROGRAM section)
"""
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ORACLE = ROOT / "assets_raw/dump/oracle"
OLD_FRAMES = ROOT / "assets_raw/dump/frames.json"
OLD_DEFS = ROOT / "assets_raw/dump/defs_merged.json"
CTFAK_PATH = ROOT / "assets_raw/ccj_dump/Ultimate Custom Night/JSON/application.json"
ASSETS = ROOT / "assets_raw/out"
OUT = ROOT / "build/bundle"

S_STRINGS, S_FRAMES, S_OBJECTS, S_IMAGES, S_SOUNDS, S_PROGRAM, S_RESOURCES, S_OBJRT, S_FRAMERT, S_SOUNDRT = range(1, 11)

FRAME_COUNT = 16

# Layer flag bit order (bit i = LAYER_FLAGS[i]); the JSON above lists them in this order.
LAYER_FLAGS = ["XCoefficient", "YCoefficient", "DoNotSaveBackground", "Visible",
               "WrapHorizontally", "WrapVertically", "Redraw", "ToHide", "ToShow"]

KIND_TEXTURE, KIND_AUDIO, KIND_OTHER = 0, 1, 2
AUDIO_SUFFIXES = {".ogg", ".mp3"}


def jload(path):
    return json.loads(Path(path).read_text())


def u32(v):
    if not (-0x80000000 <= v <= 0xFFFFFFFF):
        raise ValueError(f"u32 out of range: {v}")
    return v & 0xFFFFFFFF


def i32(v):
    if not (-0x80000000 <= v <= 0x7FFFFFFF):
        raise ValueError(f"i32 out of range: {v}")
    return v


def u8(v):
    if not (0 <= v <= 0xFF):
        raise ValueError(f"u8 out of range: {v}")
    return v


def u16(v):
    if not (0 <= v <= 0xFFFF):
        raise ValueError(f"u16 out of range: {v}")
    return v


def i16(v):
    if not (-0x8000 <= v <= 0x7FFF):
        raise ValueError(f"i16 out of range: {v}")
    return v


def double_bits(v):
    """Raw 64-bit pattern of the ctfak double (the ccj wrote two 32-bit halves)."""
    return struct.unpack("<Q", struct.pack("<d", float(v)))[0]


def coeff_raw(v):
    """Layer coefficient: the high 32-bit half of the ctfak double, as raw i32.

    ctfak reads the ccj's i32 pair as one double, so both halves live in the bit
    pattern; the low half is zero in every observed layer, the high half carries
    the varying data (0x37A00000, 0x37999980, 0x37933320 or 0). Raw bits are
    stored; M2 decides how to scale them.
    """
    return struct.unpack("<i", struct.pack("<I", (double_bits(v) >> 32) & 0xFFFFFFFF))[0]


def layer_flags_bits(flags):
    v = 0
    for i, name in enumerate(LAYER_FLAGS):
        if flags.get(name):
            v |= 1 << i
    return v


def png_size(path):
    data = path.read_bytes()[:33]
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise ValueError(f"not a PNG: {path}")
    return struct.unpack(">II", data[16:24])


class Strings:
    def __init__(self):
        self.table = []
        self.index = {}

    def get(self, s):
        if s not in self.index:
            self.index[s] = len(self.table)
            self.table.append(s)
        return self.index[s]

    def __len__(self):
        return len(self.table)


def with_offsets(records):
    """u32 count, u32 off[count] (from section start), then the records."""
    n = len(records)
    base = 4 + 4 * n
    offs = []
    cur = base
    for r in records:
        offs.append(cur)
        cur += len(r)
    return (struct.pack("<I", n)
            + b"".join(struct.pack("<I", o) for o in offs)
            + b"".join(records))


# ---------------------------------------------------------------- parsing


def parse_frames(ctf):
    frames = []
    old = jload(OLD_FRAMES)
    assert len(old) == FRAME_COUNT, f"frames.json has {len(old)} frames"

    # image metadata (union over frames) for the Un[h] == atlas consistency check
    img_meta = {}
    for i in range(FRAME_COUNT):
        fd = jload(ORACLE / f"frame_{i}.json")
        for im in fd["images"]:
            img_meta.setdefault(im["handle"], im)

    for i in range(FRAME_COUNT):
        fd = jload(ORACLE / f"frame_{i}.json")
        md = fd["meta"]
        cf = ctf["frames"][i]
        of = old[i]

        name = md["Vz"]
        width, height = md["pd"], md["uc"]
        assert name == cf["name"] == of["name"], f"frame {i} name mismatch"
        assert (width, height) == (cf["width"], cf["height"]) == (of["w"], of["h"]), \
            f"frame {i} size mismatch"

        layers = []
        for L in cf["layers"]:
            layers.append({
                "name": L["name"],
                "x_coeff": coeff_raw(L["xCoefficient"]),
                "y_coeff": coeff_raw(L["yCoefficient"]),
                "flags": layer_flags_bits(L["flags"]),
            })
        assert md["wc"] == len(layers), f"frame {i} layer count: live {md['wc']} vs ctfak {len(layers)}"

        bg = cf["background"]
        bg_color = (u32(bg[0]) << 16) | (u32(bg[1]) << 8) | u32(bg[2])
        assert not bg[3], f"frame {i} background has a fourth component {bg}"
        assert md["AA"] == bg_color, f"frame {i} background color live {md['AA']:#x} vs ctfak {bg_color:#x}"

        inst_live = fd["instances"]
        inst_cf = cf["instances"]
        inst_old = of["instances"]
        assert fd["instanceCount"] == len(inst_live) == len(inst_cf) == len(inst_old) == of["instCount"], \
            f"frame {i} instance count mismatch"
        instances = []
        for k, it in enumerate(inst_live):
            n = it["named"]
            live6 = [n["order"], n["object"], n["x"], n["y"], n["fv"], n["BA"]]
            assert n["order"] == k, f"frame {i} instance {k} order {n['order']}"
            assert live6 == list(inst_old[k][:6]), f"frame {i} instance {k} old-dump mismatch"
            c = inst_cf[k]
            assert c["handle"] == n["order"] and c["objectInfo"] == n["object"], \
                f"frame {i} instance {k} handle/object mismatch"
            assert (c["x"], c["y"]) == (n["x"], n["y"]), f"frame {i} instance {k} xy mismatch"
            assert c["layer"] == n["BA"] and c["parentType"] == n["fv"], \
                f"frame {i} instance {k} layer/parentType mismatch"
            instances.append({
                "object": n["object"], "x": n["x"], "y": n["y"],
                "layer": n["BA"], "order": n["order"],
                "parent_type": c["parentType"], "parent_handle": c["parentHandle"],
            })

        un_used = fd["unUsed"]
        assert len(un_used) == of["UnNonzero"], \
            f"frame {i} unUsed {len(un_used)} vs old UnNonzero {of['UnNonzero']}"
        images = []
        for h, atlas in un_used:
            assert img_meta[h]["atlas"] == atlas, f"frame {i} handle {h} atlas {atlas} vs meta {img_meta[h]['atlas']}"
            images.append(h)

        frames.append({
            "name": name, "width": width, "height": height,
            "layers": layers,
            "bg_mode": 0, "bg_image": 0xFFFFFFFF, "bg_x": 0, "bg_y": 0,
            "bg_color": bg_color,
            "instances": instances,
            "images": images,
        })
    print(f"assert ok: {FRAME_COUNT} frames; names/sizes/instances/layers match oracle + ctfak + frames.json")
    print(f"assert ok: per-frame images == UnNonzero (1953 each); Un[h] == image atlas on every handle")
    return frames, img_meta


def parse_objects(ctf, img_meta):
    raw = jload(ORACLE / "objects.json")
    objs = raw["objects"]
    assert raw["count"] == len(objs)
    old = jload(OLD_DEFS)
    assert len(old) == len(objs)
    cobj = ctf["objects"]

    objects = []
    anim_total = dir_total = values_total = 0
    for h, d in enumerate(objs):
        assert d["mo"] == h, f"object index {h} handle {d['mo']}"
        oc = d.get("oc") or {}
        name = d["Rv"]

        c = cobj[h]
        assert c["handle"] == h and c["name"] == name, f"object {h} ctfak mismatch"
        assert c["objectType"] == d["Ee"], f"object {h} type live {d['Ee']} vs ctfak {c['objectType']}"

        oldc = old[h]
        assert oldc["handle"] == h and oldc["name"] == name, f"object {h} old-dump mismatch"

        values = []
        dm = oc.get("dm")
        if isinstance(dm, dict) and dm.get("values") is not None:
            values = [i32(v) for v in dm["values"]]
            assert len(values) == dm.get("sr", len(values)), f"object {h} value count"

        anims = []
        ti = oc.get("Ti")
        if isinstance(ti, dict):
            nf = ti.get("nf") or []
            for a in range(ti.get("mf") or 0):
                entry = nf[a] if a < len(nf) else None
                dirs = []
                if isinstance(entry, dict):
                    for slot, fd in enumerate(entry.get("Rd") or []):
                        if not isinstance(fd, dict):
                            continue
                        frames = [u32(x) for x in (fd.get("Yj") or [])]
                        for fh in frames:
                            assert fh in img_meta, f"object {h} anim image {fh} has no metadata"
                        dirs.append({
                            "dir": slot, "repeat": u8(fd.get("vy") or 0),
                            "min_speed": i32(fd.get("Jt") or 0),
                            "max_speed": i32(fd.get("It") or 0),
                            "back_to": i32(fd.get("wy") or 0),
                            "frames": frames,
                        })
                anims.append(dirs)
        assert len(anims) == len(oldc.get("anim", {}).get("anims", []) or []) if oldc.get("anim") else len(anims) == 0, \
            f"object {h} animation count vs old dump"

        objects.append({
            "handle": h, "type": u8(d["Ee"]), "flags": u8(d["Rh"]), "name": name,
            "ink_effect": u8(c["inkEffect"]), "ink_effect_value": u8(c["inkEffectValue"]),
            "blend": u8(c["blend"]), "rgb_coeff": [i32(v) for v in c["rgbCoeff"]],
            "backdrop_image": u32(oc.get("Mh") or 0),
            "values": values, "anims": anims,
        })
        anim_total += len(anims)
        dir_total += sum(len(a) for a in anims)
        values_total += len(values)

    census = {
        "anim_objects": sum(1 for d in objs if ((d.get("oc") or {}).get("Ti") or {}).get("nf")),
        "movement_objects": sum(1 for d in objs if (d.get("oc") or {}).get("$e")),
        "counter_objects": sum(1 for d in objs if (d.get("oc") or {}).get("jb") is not None),
        "wc_objects": sum(1 for d in objs if (d.get("oc") or {}).get("Wc") is not None),
    }
    old_census = {
        "anim_objects": sum(1 for d in old if d.get("anim")),
        "movement_objects": sum(1 for d in old if d.get("movements")),
        "counter_objects": sum(1 for d in old if d.get("counter")),
        "wc_objects": sum(1 for d in old if d.get("Wc") is not None),
    }
    assert census == old_census, f"objects census {census} vs old {old_census}"
    print(f"assert ok: objects={len(objects)} handles/names/types match; census {census}")
    backdrop_total = sum(1 for o in objects if o["backdrop_image"])
    print(f"  animations: {anim_total} with {dir_total} direction entries; alterable values: {values_total} (none used); backdrops with image: {backdrop_total}")
    print("  note: movements/counters/Wc are in the dumps but deferred to the interpreter plan (format extension)")
    return objects


def parse_images(img_meta):
    images = []
    missing = []
    for h in sorted(img_meta):
        im = img_meta[h]
        atlas = im["atlas"]
        fname = f"M{atlas:04d}.png" if atlas else f"{h:04d}.png"
        path = ASSETS / fname
        if not path.exists():
            missing.append((h, fname))
            continue
        sw, sh = png_size(path)
        images.append({
            "handle": h, "file": fname,
            "crop_x": i32(im["crop_x"]), "crop_y": i32(im["crop_y"]),
            "crop_w": i32(im["w"]), "crop_h": i32(im["h"]),
            "src_w": i32(sw), "src_h": i32(sh),
            "hotspot_x": i16(im["hotspot_x"]), "hotspot_y": i16(im["hotspot_y"]),
            "off_x": i16(im["Ga"]), "off_y": i16(im["Ca"]), "flags": u16(im["Sa"]),
        })
    assert not missing, f"image files missing in assets_raw/out: {missing[:5]} ({len(missing)} total)"
    print(f"assert ok: images={len(images)}; every file exists in assets_raw/out "
          f"(atlases: {sum(1 for i in images if i['file'].startswith('M'))}, direct: {sum(1 for i in images if not i['file'].startswith('M'))})")
    return images


def parse_program(ctf):
    frames = []
    groups_total = entries_total = 0
    for i in range(FRAME_COUNT):
        pr = jload(ORACLE / "program" / f"frame_{i}.json")
        nexp = len((ctf["frames"][i].get("events") or {}).get("groups") or [])
        assert pr["groupCount"] == nexp, f"frame {i} group count live {pr['groupCount']} vs ctfak {nexp}"
        groups = pr["groups"]
        for g in groups:
            ib = (g.get("raw") or {}).get("ib") or []
            entries_total += len(ib)
            for ent in ib:
                assert ent.get("Ad") == len(ent.get("s") or []), f"frame {i} group {g['gi']} Ad != len(s)"
        groups_total += len(groups)
        frames.append({"i": i, "groups": groups})
    print(f"assert ok: program groups={groups_total} entries={entries_total}; counts match ctfak on all frames")
    program = {"frames": frames}
    blob = json.dumps(program, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    return program, blob, {"groups": groups_total, "entries": entries_total}


def parse_resources():
    resources = []
    skipped = []
    for path in sorted(ASSETS.iterdir()):
        if not path.is_file():
            continue
        suffix = path.suffix.lower()
        if suffix == ".png":
            kind = KIND_TEXTURE
        elif suffix in AUDIO_SUFFIXES:
            kind = KIND_AUDIO
        elif suffix in (".cc1", ".cch"):
            skipped.append(path.name)
            continue
        else:
            kind = KIND_OTHER
        resources.append({"file": path.name, "size": path.stat().st_size, "kind": kind})
    if skipped:
        print(f"  note: excluded container archives from RESOURCES: {skipped}")
    counts = {k: sum(1 for r in resources if r["kind"] == k) for k in (KIND_TEXTURE, KIND_AUDIO, KIND_OTHER)}
    print(f"assert ok: resources={len(resources)} (textures={counts[KIND_TEXTURE]}, audio={counts[KIND_AUDIO]}, other={counts[KIND_OTHER]})")
    return resources


# ---------------------------------------------------------------- writing


def w_strings(strings):
    out = struct.pack("<I", len(strings.table))
    for s in strings.table:
        b = s.encode("utf-8")
        out += struct.pack("<I", len(b)) + b
    return out


def w_frames(frames, strings):
    recs = []
    for f in frames:
        r = struct.pack("<Iii", strings.get(f["name"]), i32(f["width"]), i32(f["height"]))
        r += struct.pack("<I", len(f["layers"]))
        for L in f["layers"]:
            r += struct.pack("<IiiI", strings.get(L["name"]), i32(L["x_coeff"]), i32(L["y_coeff"]), u32(L["flags"]))
        r += struct.pack("<IIiiI", u32(f["bg_mode"]), u32(f["bg_image"]), i32(f["bg_x"]), i32(f["bg_y"]), u32(f["bg_color"]))
        r += struct.pack("<I", len(f["instances"]))
        for n in f["instances"]:
            r += struct.pack("<IiiIIII", u32(n["object"]), i32(n["x"]), i32(n["y"]),
                             u32(n["layer"]), u32(n["order"]),
                             u32(n["parent_type"]), u32(n["parent_handle"]))
        r += struct.pack("<I", len(f["images"]))
        r += b"".join(struct.pack("<I", u32(h)) for h in f["images"])
        recs.append(r)
    return with_offsets(recs)


def w_objects(objects, strings):
    recs = []
    for o in objects:
        r = struct.pack("<IBBI", u32(o["handle"]), o["type"], o["flags"], strings.get(o["name"]))
        r += struct.pack("<BBB", o["ink_effect"], o["ink_effect_value"], o["blend"])
        r += struct.pack("<I", u32(o["backdrop_image"]))
        for v in o["rgb_coeff"]:
            r += struct.pack("<i", i32(v))
        r += struct.pack("<I", len(o["values"]))
        for v in o["values"]:
            r += struct.pack("<i", i32(v))
        r += struct.pack("<I", len(o["anims"]))
        for dirs in o["anims"]:
            r += struct.pack("<I", len(dirs))
            for d in dirs:
                r += struct.pack("<BBHiii", d["dir"], d["repeat"], 0,
                                 i32(d["min_speed"]), i32(d["max_speed"]), i32(d["back_to"]))
                r += struct.pack("<I", len(d["frames"]))
                r += b"".join(struct.pack("<I", u32(h)) for h in d["frames"])
        recs.append(r)
    return with_offsets(recs)


def w_images(images, strings):
    recs = []
    for im in images:
        r = struct.pack("<II", u32(im["handle"]), strings.get(im["file"]))
        r += struct.pack("<iiii", im["crop_x"], im["crop_y"], im["crop_w"], im["crop_h"])
        r += struct.pack("<ii", im["src_w"], im["src_h"])
        r += struct.pack("<hh", im["hotspot_x"], im["hotspot_y"])
        r += struct.pack("<hh", im["off_x"], im["off_y"])
        r += struct.pack("<H", im["flags"])
        recs.append(r)
    return with_offsets(recs)


def w_sounds():
    return struct.pack("<I", 0)


# PROGRAM v2 parameter layout: the value fields each parameter code stores, in order (runtime field names; see
# docs/runtime-map.md). Expression parameters (PARAM_EXPRESSION) store Ei (the comparison operator) and own a
# token run. Fields marked * are the runtime's per-frame resolved OI-list indices, kept so the engine can check
# its own resolution against the live one.
PARAM_FIELDS = {
    1: ["Qh", "type", "Ub*"],                      # object
    2: ["Oc", "ar"],                               # time
    6: ["ts", "RC"], 35: ["ts", "RC"],             # sample
    9: ["qo", "Hk", "Dr", "Er", "Br", "gw", "Ar", "Cr", "ro*", "hw", "eq", "Ny"],  # create object
    16: ["qo", "Hk", "Dr", "Er", "Br", "gw", "Ar", "Cr", "ro*", "hw"],              # position
    10: ["value"], 17: ["value"], 26: ["value"], 32: ["value"], 50: ["value"], 57: ["value"],
    14: ["key"], 44: ["key"],
    24: ["color"],
    25: ["value", "Ds"], 29: ["value", "Ds"],
    64: ["Eb$"],                                   # string (string-table index)
}
PARAM_EXPRESSION = {15, 22, 23, 27, 45}
TOKEN_INT, TOKEN_STRING, TOKEN_DOUBLE = 65535, 262143, 1572863


def w_program(program, strings):
    """PROGRAM v2 (M3 plan, Task 2): typed groups / items / parameters / tokens, in flat tables.

    u32 version (2); u32 frame_count, group_count, item_count, param_count, value_count, token_count
    frames[]: u32 first_group, group_count
    groups[]: u8 conditions, u8 actions, u16 flags, i32 hi, i32 hl, u32 first_item               (16 bytes)
    items[]:  i32 code, u16 oi, u16 oil*, u8 ma, u8 qe, u8 param_count, u8 tn, u16 identifier,
              u16 reserved, u32 first_param                                                       (20 bytes)
    params[]: u16 code, u8 value_count, u8 reserved, u32 first_value, u32 first_token, u32 token_count (16 bytes)
    values[]: f64
    tokens[]: i32 code, i32 oi (Qh, -1 none), i32 aux (Lh), i32 oil* (Ub, -1 none), f64 value        (24 bytes)
    """
    frames, groups, items, params, values, tokens = [], [], [], [], [], []
    for fr in program["frames"]:
        frames.append(struct.pack("<II", len(groups), len(fr["groups"])))
        for g in fr["groups"]:
            r = g["raw"]
            groups.append(struct.pack("<BBHiiI", u8(r["Yb"]), u8(r["xe"]), u16(r["Ka"]), i32(r["Hi"]), i32(r["Hl"]),
                                      len(items)))
            for it in r["ib"]:
                ps = it.get("s") or []
                items.append(struct.pack("<iHHBBBBHHI", i32(it["ya"]), u16(it["ke"] & 0xFFFF), u16(it["ub"] & 0xFFFF),
                                         u8(it["Ma"]), u8(it["Qe"]), u8(len(ps)), u8(it["tn"]),
                                         u16(it.get("mP", 0)), 0, len(params)))
                for prm in ps:
                    code = prm["code"]
                    first_value, first_token, ntok = len(values), len(tokens), 0
                    if code in PARAM_EXPRESSION:
                        values.append(prm["Ei"])
                        for t in prm["Fa"]:
                            tc = t["code"]
                            if tc == TOKEN_STRING:
                                v = strings.get(t["Eb"])
                            else:
                                v = t.get("value", 0)
                            tokens.append(struct.pack("<iiiid", i32(tc), i32(t.get("Qh", -1)), i32(t.get("Lh", 0)),
                                                      i32(t.get("Ub", -1)), float(v)))
                            ntok += 1
                    else:
                        for f in PARAM_FIELDS[code]:
                            name = f.rstrip("*$")
                            v = prm[name]
                            values.append(strings.get(v) if f.endswith("$") else v)
                    params.append(struct.pack("<HBBIII", u16(code), u8(len(values) - first_value), 0, first_value,
                                              first_token, ntok))
    head = struct.pack("<7I", 2, len(frames), len(groups), len(items), len(params), len(values), len(tokens))
    return (head + b"".join(frames) + b"".join(groups) + b"".join(items) + b"".join(params)
            + b"".join(struct.pack("<d", float(v)) for v in values) + b"".join(tokens)), {
        "groups": len(groups), "items": len(items), "params": len(params), "values": len(values),
        "tokens": len(tokens)}


def w_objrt(strings):
    """OBJRT (M3): what the interpreter needs from each object definition, one record per object in handle order.

    Field names are the runtime's (docs/runtime-map.md); i32 unless noted, strings are string-table indices
    (-1 = none). Record:
      handle, type (Ee), flags (Rh), ink (Pv), ink_param (Qv), oc_flags (Ag), oc_vflags (Vi)
      values: ks, n, values[n]                      (oc.dm; n = 0 when absent)
      strings: n, strings[n]                         (oc.lo)
      anims: count (Ti.mf, 0 = none), exists u8[count] (Ti.wi), then per anim: present u8; when present
             rh u8[32], xi u8[32], then per direction 0..31: present u8; when present
             min_speed (Jt), max_speed (It), loops (vy), back_to (wy), frame_count (xl), images[frame_count] (Yj)
      movements: n; per movement: type (Fv), player (Xl), moving_at_start (yB), dirs (xB), flags (nr),
             mouse ZA aB $A bB, path steps (Wl) min_speed (sB) max_speed (rB) loop (Ev) reposition (uB)
             reverse (wB), step_count; per step: speed (UA) dir (tv) dx (uv) dy (vv) cos (sv) sin (xv)
             length (TA) pause (wv) name (ne, string)
      counter: present u8; when present min (kz) max (jz) initial (iz)     (oc.Wc of a counter)
      display: present u8; when present Oh Ph Ov af Bk Wi zg bm Yl Ui jo, n, frames[n]   (oc.jb; Ui: bar fill, 1 solid
               2 gradient; jo: gradient vertical)
      text: present u8; when present width (bw) height (cw) Dk, n; per string: font (Jm) flags (Vo) color (ax) text (gi)
    """
    raw = jload(ORACLE / "objects.json")["objects"]
    recs = []
    for h, d in enumerate(raw):
        oc = d.get("oc") or {}
        r = struct.pack("<7i", h, i32(d["Ee"]), i32(d["Rh"]), i32(d["Pv"]), i32(d["Qv"]), i32(oc.get("Ag", 0)),
                        i32(oc.get("Vi", 0)))
        dm = oc.get("dm")
        vals = (dm or {}).get("values") or []
        r += struct.pack("<ii", i32((dm or {}).get("Ya", 0)), len(vals)) + b"".join(struct.pack("<i", i32(v)) for v in vals)
        lo = (oc.get("lo") or {}).get("$a") or []
        r += struct.pack("<i", len(lo)) + b"".join(struct.pack("<i", strings.get(t) if t is not None else -1) for t in lo)
        ti = oc.get("Ti")
        if isinstance(ti, dict):
            mf = ti["mf"]
            r += struct.pack("<i", mf) + bytes(u8(1 if v else 0) for v in ti["wi"][:mf])
            for a in range(mf):
                an = ti["nf"][a] if a < len(ti["nf"]) else None
                if not an:
                    r += b"\0"
                    continue
                r += b"\1" + bytes(u8(v) for v in an["rh"]) + bytes(u8(v) for v in an["xi"])
                assert len(an["rh"]) == 32 and len(an["xi"]) == 32 and len(an["Rd"]) == 32, f"object {h} anim {a}"
                for dd in an["Rd"]:
                    if not dd:
                        r += b"\0"
                        continue
                    r += b"\1" + struct.pack("<5i", i32(dd["Jt"]), i32(dd["It"]), i32(dd["vy"]), i32(dd["wy"]), i32(dd["xl"]))
                    assert len(dd["Yj"]) == dd["xl"], f"object {h} anim {a} frames"
                    r += b"".join(struct.pack("<i", i32(v)) for v in dd["Yj"])
        else:
            r += struct.pack("<i", 0)
        mv = (oc.get("$e") or {}).get("Gd") or []
        r += struct.pack("<i", len(mv))
        for m in mv:
            r += struct.pack("<5i", i32(m["Fv"]), i32(m["Xl"]), i32(m["yB"]), i32(m["xB"]), i32(m["nr"]))
            r += struct.pack("<4i", *(i32(m.get(k, 0)) for k in ("ZA", "aB", "$A", "bB")))
            r += struct.pack("<6i", *(i32(m.get(k, 0)) for k in ("Wl", "sB", "rB", "Ev", "uB", "wB")))
            steps = m.get("bb") or []
            r += struct.pack("<i", len(steps))
            for st in steps:
                r += struct.pack("<8ii", *(i32(st[k]) for k in ("UA", "tv", "uv", "vv", "sv", "xv", "TA", "wv")),
                                 strings.get(st["ne"]) if st.get("ne") is not None else -1)
        wc = oc.get("Wc")
        if d["Ee"] == 7 and wc:
            r += b"\1" + struct.pack("<3i", i32(wc["kz"]), i32(wc["jz"]), i32(wc["iz"]))
        else:
            r += b"\0"
        jb = oc.get("jb")
        if jb:
            fr = jb.get("frames") or []
            r += b"\1" + struct.pack("<11i", *(i32(jb.get(k, 0)) for k in ("Oh", "Ph", "Ov", "af", "Bk", "Wi", "zg", "bm", "Yl", "Ui", "jo")))
            r += struct.pack("<i", len(fr)) + b"".join(struct.pack("<i", i32(v)) for v in fr)
        else:
            r += b"\0"
        if d["Ee"] == 3 and wc:
            vb = wc.get("Vb") or []
            r += b"\1" + struct.pack("<4i", i32(wc["bw"]), i32(wc["cw"]), i32(wc["Dk"]), len(vb))
            for t in vb:
                r += struct.pack("<4i", i32(t["Jm"]), i32(t["Vo"]), i32(t["ax"]), strings.get(t["gi"]))
        else:
            r += b"\0"
        recs.append(r)
    return with_offsets(recs)


def w_framert(strings):
    """FRAMERT (M3): the runtime frame header and its instance list (A.wd.list order), one record per frame.

      flags (Tb), seed (ir), max_objects (yg), frame_rate (GA), virtual_right, virtual_bottom (Vq), layers (wc),
      display_w (Pn), display_h (On), n_layers; per layer: flags (La), x_coeff, y_coeff (zj, Bj as 16.16),
      n; per instance: handle (Pi), object (Df), x (gv), y (hv), create (fv; 0 = at frame start), layer (BA), Xq
    """
    recs = []
    layers = jload(ORACLE / "layers.json")  # tools/dump_layers.sh
    for i in range(FRAME_COUNT):
        fd = jload(ORACLE / f"frame_{i}.json")
        m = fd["meta"]
        r = struct.pack("<7i", i32(m["Tb"]), i32(m["ir"]), i32(m["yg"]), i32(m["GA"]), i32(m["Vq"]["right"]),
                        i32(m["Vq"]["bottom"]), i32(m["wc"]))
        r += struct.pack("<2i", i32(m["Pn"]), i32(m["On"]))
        lay = layers[str(i)]
        assert len(lay) == m["wc"], f"frame {i}: {len(lay)} runtime layers vs wc {m['wc']}"
        r += struct.pack("<i", len(lay)) + b"".join(struct.pack("<3i", i32(La), i32(round(zj * 65536)), i32(round(bj * 65536)))
                                                   for La, zj, bj in lay)
        inst = fd["instances"]
        r += struct.pack("<i", len(inst))
        for k, it in enumerate(inst):
            x = it["raw"]
            r += struct.pack("<7i", *(i32(x[f]) for f in ("Pi", "Df", "gv", "hv", "fv", "BA", "Xq")))
        recs.append(r)
    return with_offsets(recs)


def w_soundrt():
    """SOUNDRT (M3): the sound manager's settings and every sample's decoded duration, as the live runtime has them
    (assets_raw/dump/oracle/sounds.json, tools/dump_sounds.sh). The engine's channels play samples for these
    durations; the oracle shim fires `onended` after the same ones.

      i32 multiple_sounds (Rt), sound_on (Cy), default_volume (JA); i32 n; n x (i32 handle, f64 ms)
    """
    d = jload(ORACLE / "sounds.json")
    rt, cy, ja = d.pop("settings")
    items = sorted((int(k), float(v)) for k, v in d.items())
    return struct.pack("<4i", i32(rt), i32(cy), i32(ja), len(items)) + b"".join(struct.pack("<id", h, ms) for h, ms in items)


def w_resources(resources, strings):
    recs = []
    for res in resources:
        recs.append(struct.pack("<IIB", strings.get(res["file"]), u32(res["size"]), res["kind"]))
    return with_offsets(recs)


def build_bin(model, strings, program):
    # Payload sections first: they intern their names into the string table.
    frames_sec = w_frames(model["frames"], strings)
    objects_sec = w_objects(model["objects"], strings)
    images_sec = w_images(model["images"], strings)
    resources_sec = w_resources(model["resources"], strings)
    program_sec, program_counts = w_program(program, strings)
    objrt_sec = w_objrt(strings)
    framert_sec = w_framert(strings)
    model["program"].update(program_counts)
    sections = [
        (S_STRINGS, w_strings(strings)),
        (S_FRAMES, frames_sec),
        (S_OBJECTS, objects_sec),
        (S_IMAGES, images_sec),
        (S_SOUNDS, w_sounds()),
        (S_PROGRAM, program_sec),
        (S_RESOURCES, resources_sec),
        (S_OBJRT, objrt_sec),
        (S_FRAMERT, framert_sec),
        (S_SOUNDRT, w_soundrt()),
    ]
    header_size = 16 + 12 * len(sections)
    table = b""
    payload = b""
    offset = header_size
    for stype, data in sections:
        table += struct.pack("<III", stype, offset, len(data))
        payload += data
        offset += len(data)
    total = header_size + len(payload)
    header = struct.pack("<4sHHI I", b"UCNB", 2, len(sections), total, 0)
    return header + table + payload


def main():
    if not (ORACLE / "objects.json").exists():
        print("oracle dumps missing; run tools/run_oracle_dumps.sh first", file=sys.stderr)
        return 1
    ctf = jload(CTFAK_PATH)

    frames, img_meta = parse_frames(ctf)
    objects = parse_objects(ctf, img_meta)
    images = parse_images(img_meta)
    program, program_blob, program_stats = parse_program(ctf)
    resources = parse_resources()

    model = {
        "format": {"magic": "UCNB", "version": 2},
        "frames": frames,
        "objects": objects,
        "images": images,
        "resources": resources,
        "program": {"frames": len(frames), "groups": program_stats["groups"],
                    "entries": program_stats["entries"], "file": "program.json"},
    }

    strings = Strings()
    blob = build_bin(model, strings, program)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "program.json").write_bytes(program_blob)
    (OUT / "bundle.json").write_text(json.dumps({**model, "strings": strings.table}, indent=1, ensure_ascii=False))
    (OUT / "bundle.bin").write_bytes(blob)

    print()
    print(f"strings={len(strings)} bytes={len(blob)}")
    for i, f in enumerate(frames):
        print(f"frame[{i:2d}] {f['name']:<16} {f['width']}x{f['height']} "
              f"layers={len(f['layers'])} instances={len(f['instances']):4d} images={len(f['images']):4d}")
    print(f"objects={len(objects)} images={len(images)} resources={len(resources)}")
    print(f"wrote {OUT/'bundle.bin'} ({len(blob)} bytes)")
    print(f"wrote {OUT/'bundle.json'}, {OUT/'program.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
