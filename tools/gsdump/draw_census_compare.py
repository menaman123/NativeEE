#!/usr/bin/env python3
"""draw_census_compare.py -- the same draw census over `.gsdl` recordings (OLD / NEW) and PCSX2 `.gs.zst`
dumps, and the N-way group diff.

Every source is reduced to one frame's ordered draw list, each draw keyed by the GS state the
blend / test / texture-function pipeline sees (gsdump_census.key_tuple): PRIM type, IIP, TME, FGE,
ABE, FST, context, ALPHA A/B/C/D/FIX (when ABE), PABE, COLCLAMP, TEST (ATE/ATST/AREF/AFAIL/DATE/
DATM/ZTE/ZTST), FRAME PSM/FBMSK, ZBUF PSM/ZMSK, FBA, TEX0 PSM/TFX/TCC/CPSM/TW/TH, TEXA (CT24/CT16
textures). The address-free key (no FBP/ZBP/TBP/CBP) is the comparison key: the same scene on two
trees lays its buffers out identically, but the comparison must not depend on it.

A `.gsdl` draw additionally carries its HLE handler (class) name; the dump has none (raw stream).

Usage:
  draw_census_compare.py SRC[:frame] [SRC2[:frame] ...] [--draws] [--json out.json] [--names]
    SRC = path.gsdl | path.gs | path.gs.zst ; frame = 0-based frame index (default: the last full frame)
  draw_census_compare.py --selftest
"""
import json
import os
import struct
import sys
from collections import OrderedDict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gsdump_census as gc  # noqa: E402

DRAW = struct.Struct("<IIQ" "8B" "8B" "6I" "4B" "4H" "4B" "QQII" "8B" "4B" "3I" "4B" "I" "4H" "8B" "IIQ6ff")
TAIL = struct.Struct("<IIQHHffB3x")
VERT = struct.Struct("<fff fff HH 4B BB H")
assert DRAW.size == 188 and TAIL.size == 32 and VERT.size == 36


def read_gsdl(path):
    """Frames of draws from a .gsdl (v1/v2). Each draw: dict(key, verts, rgba, xy, handler, name)."""
    data = open(path, "rb").read()
    magic, version, flags = struct.unpack_from("<III", data, 0)
    if magic != 0x524C4432:
        raise ValueError("not a .gsdl: magic %#x" % magic)
    verts_incl = flags & 1
    names = {}
    frames = [[]]
    pos = 24
    while pos + 8 <= len(data):
        kind, size = struct.unpack_from("<II", data, pos)
        pos += 8
        payload = data[pos:pos + size]
        pos += size
        if kind == 4:
            h, n = struct.unpack_from("<II", payload, 0)
            names[h] = payload[8:8 + n].decode("ascii", "replace")
        elif kind == 2:
            frames[-1].append(draw_from_rec(payload, version, verts_incl))
        elif kind == 3:
            frames.append([])
    if not frames[-1]:
        frames.pop()
    for fr in frames:
        for d in fr:
            d["name"] = names.get(d["handler"], "raw" if d["handler"] == 0 else "hle:%08x" % d["handler"])
    return frames


def draw_from_rec(payload, version, verts_incl):
    f = DRAW.unpack_from(payload, 0)
    rec_size = 220 if version >= 2 else 188
    handler, ctx = 0, 0
    if version >= 2 and len(payload) >= 220:
        t = TAIL.unpack_from(payload, 188)
        handler, ctx = t[0], t[7]
    tme = f[11]
    tex = None
    if tme:
        # the record stores TEX0.TW/TH as the log2 exponents; the census key uses texels
        tex = dict(tbp=f[19], tbw=f[20], psm=f[21], tw=1 << f[22] if f[22] < 16 else f[22],
                   th=1 << f[23] if f[23] < 16 else f[23], tcc=f[14], tfx=f[13], cbp=f[24],
                   cpsm=f[15], csm=f[16], csa=f[17], mmag=f[25], mmin=f[26], wms=f[27], wmt=f[28])
    key = dict(prim=f[3] & 7, iip=f[4], tme=tme, fge=f[5], abe=f[7], fst=f[6], ctx=ctx,
               alpha=(f[44], f[45], f[46], f[47], f[48]), pabe=f[42], colclamp=f[41],
               test=tuple(f[65:73]), frame=dict(fbp=f[53], fbw=f[54], psm=f[56], fbmsk=f[55]),
               zbuf=dict(zbp=f[60], psm=f[57], zmsk=f[58]), tex=tex, texa=(f[33], f[34], f[35]), fba=f[43])
    n = f[73]
    rgba = OrderedDict()
    xy = []
    if verts_incl and f[74]:
        for i in range(n):
            v = VERT.unpack_from(payload, rec_size + i * 36)
            c = (v[8], v[9], v[10], v[11])
            rgba[c] = rgba.get(c, 0) + 1
            if len(xy) < 4096:
                xy.append((v[0], v[1]))
    else:
        bb = f[76:82]
        xy = [(bb[0], bb[1]), (bb[3], bb[4])]
    return dict(key=key, verts=n, rgba=rgba, xy=xy, handler=handler)


def load_source(spec):
    """'path[:frame]' -> (label, frame_index, draws)."""
    path, _, fsel = spec.rpartition(":") if (":" in spec and not spec.endswith(".gsdl") and
                                            not spec.endswith(".zst") and not spec.endswith(".gs")) else (spec, "", "")
    if path.endswith(".gsdl"):
        frames = read_gsdl(path)
    else:
        res = gc.parse_dump(gc.read_dump_bytes(path))
        frames = res["census"].frames
        for fr in frames:
            for d in fr:
                d["name"] = "raw"
        # the dump's trailing frame is the open one after the last VSYNC
        if frames and not frames[-1]:
            frames.pop()
    if fsel == "":
        fi = len(frames) - 1
    else:
        fi = int(fsel)
    return os.path.basename(path), fi, frames[fi]


def frame_groups(draws, with_addr):
    groups = OrderedDict()
    for i, d in enumerate(draws):
        kt = gc.key_tuple(d["key"], with_addr)
        g = groups.get(kt)
        if g is None:
            g = groups[kt] = dict(key=d["key"], draws=0, verts=0, first=i, rgba={}, names={})
        g["draws"] += 1
        g["verts"] += d["verts"]
        nm = d.get("name", "raw")
        g["names"][nm] = g["names"].get(nm, 0) + 1
        for c, n in d["rgba"].items():
            g["rgba"][c] = g["rgba"].get(c, 0) + n
    return groups


def fmt_group(g):
    top = sorted(g["rgba"].items(), key=lambda kv: -kv[1])[:3]
    return "draws=%-4d verts=%-6d first=%-4d [%s] %s | rgba %s" % (
        g["draws"], g["verts"], g["first"], ",".join("%s:%d" % kv for kv in sorted(g["names"].items())),
        gc.fmt_key(g["key"]), " ".join("%02x%02x%02x%02x:%d" % (c + (n,)) for c, n in top))


def short_key(k):
    """The address-free blend-state description (one line, for the diff tables)."""
    tex = k["tex"]
    s = "%s%s%s%s%s" % (gc.PRIM_NAMES.get(k["prim"], "?"), "+iip" if k["iip"] else "", "+tex" if k["tme"] else "",
                        "+abe" if k["abe"] else "", "+fge" if k["fge"] else "")
    if k["abe"]:
        s += " ALPHA=%d%d%d%d/%02x" % k["alpha"]
    s += " cc=%d pabe=%d fba=%d" % (k["colclamp"], k["pabe"], k["fba"])
    s += " TEST(%d,%d,%02x,%d,%d,%d,%d,%d)" % k["test"]
    s += " F:%s/%08x Z:%s/zm%d" % (gc.PSM_NAMES.get(k["frame"]["psm"], k["frame"]["psm"]), k["frame"]["fbmsk"],
                                   gc.PSM_NAMES.get(k["zbuf"]["psm"], k["zbuf"]["psm"]), k["zbuf"]["zmsk"])
    if tex:
        s += " T:%s %dx%d tfx%d tcc%d fst%d" % (gc.PSM_NAMES.get(tex["psm"], tex["psm"]), tex["tw"], tex["th"],
                                               tex["tfx"], tex["tcc"], k["fst"])
        if tex["psm"] in (1, 2, 10):
            s += " TEXA(%02x,%02x,%d)" % k["texa"]
    return s


def compare(sources, out):
    """sources: list of (label, fi, draws). Prints per-source group tables and the N-way diff."""
    tables = []
    for label, fi, draws in sources:
        out.append("== %s frame[%d]: draws=%d verts=%d" % (label, fi, len(draws), sum(d["verts"] for d in draws)))
        groups = frame_groups(draws, True)
        for g in groups.values():
            out.append("  G " + fmt_group(g))
        tables.append(frame_groups(draws, False))
    keys = OrderedDict()
    for t in tables:
        for kt, g in t.items():
            keys.setdefault(kt, g["key"])
    labels = [s[0] for s in sources]
    out.append("== N-way diff by address-free blend-state key (%d keys); columns = draws/verts per source" % len(keys))
    out.append("   %s | verdict | key" % " | ".join("%-14s" % l[:14] for l in labels))
    rows = []
    for kt, k in keys.items():
        cells = []
        present = []
        for t in tables:
            g = t.get(kt)
            cells.append("%d/%d" % (g["draws"], g["verts"]) if g else "-")
            present.append(bool(g))
        if all(present):
            verdict = "all"
        else:
            verdict = "missing:" + ",".join(l for l, p in zip(labels, present) if not p)
        names = sorted({n for t in tables for n in (t.get(kt) or {}).get("names", {})})
        rows.append((verdict, cells, k, names))
        out.append("   %s | %s | %s [%s]" % (" | ".join("%-14s" % c for c in cells), verdict, short_key(k),
                                          ",".join(names)))
    return rows


def selftest():
    ok = True

    def check(c, w):
        nonlocal ok
        print(("PASS  " if c else "FAIL  ") + w)
        ok = ok and c

    # A synthetic v2 DrawRec: sprite+tex+abe ALPHA (0,2,0,1) FIX 0, CT32 frame, T8 texture TFX 0 TCC 1, ctx 1.
    fields = [0] * 83
    fields[3] = 6; fields[4] = 1; fields[7] = 1; fields[11] = 1; fields[13] = 0; fields[14] = 1; fields[15] = 0
    fields[19] = 0x3b0000; fields[20] = 64; fields[21] = 19; fields[22] = 5; fields[23] = 5; fields[24] = 0x3b0400
    fields[41] = 1; fields[44] = 0; fields[45] = 2; fields[46] = 0; fields[47] = 1; fields[48] = 0
    fields[53] = 0x70000; fields[54] = 512; fields[55] = 0xff000000; fields[56] = 0; fields[57] = 0; fields[58] = 1
    fields[60] = 0x150000; fields[65:73] = [0, 4, 4, 1, 0, 0, 1, 2]; fields[73] = 2; fields[74] = 1
    verts = VERT.pack(1.0, 2.0, 0.0, 0, 0, 1, 0, 0, 255, 255, 255, 127, 0, 1, 0)
    verts += VERT.pack(3.0, 4.0, 0.0, 0, 0, 1, 0, 0, 255, 255, 255, 127, 0, 1, 0)
    rec = DRAW.pack(*fields) + TAIL.pack(0x1234, 0, 0, 0, 0, 0.0, 7.0, 0) + verts
    d = draw_from_rec(rec, 2, 1)
    check(d["verts"] == 2 and d["xy"] == [(1.0, 2.0), (3.0, 4.0)] and d["rgba"] == {(255, 255, 255, 127): 2},
          "DrawRec vertices / rgba decoded")
    d2 = draw_from_rec(DRAW.pack(*fields) + TAIL.pack(0x1234, 0, 0, 0, 0, 0.0, 7.0, 1) + verts, 2, 1)
    check(d["handler"] == 0x1234 and d["key"]["ctx"] == 0 and d2["key"]["ctx"] == 1,
          "v2 tail: handler and the gs_context byte (not off_y)")
    # The same state through the dump path yields the same address-free key.
    st = gc.GsState()
    c = gc.Census(st)
    pkt = gc._giftag(1, 0, 0, 0, 0, [0x0e] * 5)
    pkt += gc._qw((0 | (2 << 2) | (0 << 4) | (1 << 6)), 0x42)                                   # ALPHA_1 0201
    pkt += gc._qw((0x38) | (8 << 16) | (0 << 24) | (0xff000000 << 32), 0x4c)                    # FRAME_1 0x70000 CT32 fbw512 fbmsk
    pkt += gc._qw((0xa8) | (1 << 32), 0x4e)                                                     # ZBUF_1 0x150000 zmsk
    pkt += gc._qw((0x3b0000 // 256) | (1 << 14) | (19 << 20) | (5 << 26) | (5 << 30) | (1 << 34) | ((0x3b0400 // 256) << 37), 0x06)
    pkt += gc._qw((4 << 1) | (4 << 4) | (1 << 12) | (1 << 16) | (2 << 17), 0x47)               # TEST_1
    pkt += gc._giftag(1, 1, 1, 6 | (1 << 3) | (1 << 4) | (1 << 6), 0, [0x05, 0x05])
    pkt += gc._qw(16 | (32 << 32), 0) + gc._qw(48 | (64 << 32), 0)
    gc.walk_transfer(pkt, c)
    c._close()
    kd = c.frames[0][0]["key"]
    check(gc.key_tuple(kd, False) == gc.key_tuple(d["key"], False),
          "dump key == gsdl key (address-free): %s" % short_key(d["key"]))
    check(gc.key_tuple(kd, True) == gc.key_tuple(d["key"], True), "dump key == gsdl key (with addresses)")
    # compare(): a key present in A and missing in B is reported.
    out = []
    rows = compare([("A", 0, [d, d]), ("B", 0, [])], out)
    check(rows[0][0] == "missing:B" and rows[0][1] == ["2/4", "-"], "N-way diff: missing in B, 2 draws / 4 verts in A")
    print("draw_census_compare selftest: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main(argv):
    if "--selftest" in argv:
        return selftest()
    json_path = None
    specs = []
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--json":
            json_path = argv[i + 1]
            i += 1
        elif not a.startswith("--"):
            specs.append(a)
        i += 1
    if not specs:
        print(__doc__)
        return 2
    sources = [load_source(s) for s in specs]
    out = []
    rows = compare(sources, out)
    if "--draws" in argv:
        for label, fi, draws in sources:
            out.append("== %s frame[%d] ordered draws" % (label, fi))
            for i, d in enumerate(draws):
                xs = [p[0] for p in d["xy"]]
                ys = [p[1] for p in d["xy"]]
                bb = "bbox=(%.0f,%.0f)-(%.0f,%.0f)" % (min(xs), min(ys), max(xs), max(ys)) if xs else ""
                top = sorted(d["rgba"].items(), key=lambda kv: -kv[1])[:2]
                out.append("  [%d] n=%d [%s] %s %s rgba %s" % (
                    i, d["verts"], d.get("name", "raw"), gc.fmt_key(d["key"]), bb,
                    " ".join("%02x%02x%02x%02x:%d" % (c + (n,)) for c, n in top)))
    print("\n".join(out))
    if json_path:
        with open(json_path, "w") as f:
            json.dump([dict(verdict=r[0], cells=r[1], key=r[2], names=r[3]) for r in rows], f)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
