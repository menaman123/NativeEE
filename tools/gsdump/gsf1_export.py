#!/usr/bin/env python3
"""gsf1_export.py -- export ONE frame of a PCSX2 GS dump as a `GSF1` replay stream for
tests/render/test_title_frame_replay.cpp (the dump-frame replay harness).

The stream is the dump's frame as the GS consumed it: every host->local IMAGE upload with its
BITBLTBUF/TRXPOS/TRXREG fields, and every draw (one draw = one run of vertex kicks under one GS
state vector, the census's grouping) with the context registers in force and the vertices
already assembled into a triangle list by the GS primitive rules (engine/gs/state/vertex_kick.cpp:
strip window / parity / degenerate rule, sprite corner normalisation with the flat second-vertex
z and colour, triangle, fan).

Vertex kicks (GS User's Manual 3.2, 7.3.1.5-8): XYZ2/XYZF2 with ADC clear draw; XYZ3/XYZF3, or
ADC set, QUEUE the vertex without drawing -- in PACKED, REGLIST and through A+D alike. A queued
vertex belongs to the draw the NEXT drawing kick opens (the vertex queue is independent of the
register state). The title's cutscene letterbox strips are set this way: XYZ3, XYZ3, XYZ2, XYZ2
on a tristrip = two triangles; dropping the two queued vertices leaves a zero-width strip.
EVERY PRIM write initialises the vertex queue (GS User's Manual, PRIM register), also in the middle
of a draw whose state vector it does not change: the strip/fan window restarts there, so a run of
fans sent as PRIM, kick x4, PRIM, kick x3 is 2 + 1 triangles (never bridged through the old centre).
The 64-bit XYZF2/XYZF3 forms carry Z in bits 32-55 and F in 56-63 (XYZ2/XYZ3: Z in 32-63).

Stream (little-endian):
  magic 'GSF1', u32 record_count, then records:
    u32 type (1 = upload, 2 = draw)
    upload: u32 dbp, dbw_px, dpsm, dsax, dsay, rrw, rrh, nbytes; bytes
    draw:   u32 index; u32 prim_attr (the PRIM/PRMODE attribute word in force, PRIM type in bits 0-2);
            u32 use_uv (1: UV written after ST at the draw's first kick); u32 ctx;
            u64 TEX0 TEX1 CLAMP ALPHA TEST FRAME ZBUF XYOFFSET SCISSOR FBA MIPTBP1 MIPTBP2 (the draw's context)
            u64 TEXA; u32 colclamp, pabe; u32 nverts (expanded triangle-list vertices); VertexRec[nverts]
  VertexRec (36 B, engine/gs/hle/ir/vertex_rec.h): f32 x y z s t q; u16 u v; u8 r g b a fog draw_kick pad[2].

usage: gsf1_export.py <dump.gs.zst> --frame N --out <stream.gsf1> [--inspect IDX ...] [--no-carry]
       gsf1_export.py --selftest
The summary line names EXPORTER_VERSION: a stream's provenance is the dump name, that version and the date.
`--no-carry` reproduces the first exporter's QUEUE rule (docs/phases/PHASE8_IMPL.md, P8.21-A, 2026-10-04): a vertex
queued before a draw's first drawing kick was dropped (the GS keeps it). It does NOT reproduce that exporter's
sprite corner swap, which moved s/t but left u/v on the wrong corner (an fst=1 sprite's TR/BL corners carried the
other corner's UV), nor its strip window kept across a mid-draw PRIM write: streams exported with it
(games/<title>/verify/baseline/title_screen/pcsx2_start_screen_f1.gsf1: 10 fst sprite draws with
u=8 where 520 is right, 216 draws short of 876 vertices, draw [9] with 2 bridging triangles of 2 px) are frozen test
inputs, left as they are and recorded; never the rule for new streams.
Reuses tools/gsdump/gsdump_census.py's reader (Census subclass + a vertex-keeping transfer walk).
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gsdump_census as gc  # noqa: E402

u64 = struct.Struct("<Q").unpack_from

# Bumped whenever the assembly rules change (the regenerated streams' provenance names it).
# 1: XYZ3/ADC queue carried, sprite corner u/v (2026-10-06); 2: PRIM restarts the strip/fan window, XYZF Z 24-bit.
EXPORTER_VERSION = 2


def _xyzf_full(v):
    """A 64-bit XYZF2/XYZF3 value with F (bits 56-63) dropped: X/Y in 0-31, Z in 32-55."""
    return v & 0x00FFFFFFFFFFFFFF


class VertexCensus(gc.Census):
    """The census with the per-vertex attributes, the IMAGE payloads and the queued-vertex rule."""

    def __init__(self, st):
        super().__init__(st)
        self.st_reg = 0          # the ST register (s, t as two floats)
        self.uv_reg = 0          # the UV register (64-bit layout: U 0-13, V 16-29)
        self.uv_last = False     # UV written since ST
        self.xfers = []          # dict(frame, draws_before, bitbltbuf, trxpos, trxreg, trxdir, dbp.., data)
        self.last_xfer_regs = dict(BITBLTBUF=0, TRXPOS=0, TRXREG=0, TRXDIR=0)
        self.miptbp = [[0, 0], [0, 0]]
        self.records = []        # ("U", xfer) / ("D", draw) in stream order
        self.queued = []         # vertices queued (no drawing kick) since the last drawing kick
        self.restart = False     # a PRIM write since the last kick: the next vertex restarts the window
        self.carried = 0         # queued vertices carried into a newly opened draw (statistics)
        self.adc_a_d = 0         # A+D XYZ3/XYZF3 writes seen
        self.carry = True        # False: the first exporter's rule (queued vertices before a draw's first
                                 # drawing kick dropped) -- provenance of streams exported before 2026-10-06

    def _vertex(self, xyz_lo, xyz_full, adc):
        st = self.st
        z = (xyz_full >> 32) & 0xFFFFFFFF if xyz_full is not None else None
        s, t = struct.unpack("<ff", struct.pack("<Q", self.st_reg))
        q = struct.unpack("<f", struct.pack("<I", (st.rgbaq >> 32) & 0xFFFFFFFF))[0]
        v = dict(x=xyz_lo & 0xFFFF, y=(xyz_lo >> 16) & 0xFFFF, z=z, rgba=st.rgbaq & 0xFFFFFFFF, q=q, s=s, t=t,
                 u=self.uv_reg & 0x3FFF, v=(self.uv_reg >> 16) & 0x3FFF, kick=0 if adc else 1, uv_last=self.uv_last)
        if self.restart:
            v["restart"] = 1     # the vertex queue was initialised by a PRIM write before this vertex
            self.restart = False
        return v

    def kick(self, xyz_lo, xyz_full=None, adc=0):
        st = self.st
        before = self.cur
        v = self._vertex(xyz_lo, xyz_full, adc)
        if adc:
            # Queued, not drawn: it stays in the vertex queue for the next drawing kick (and is
            # listed on the open draw too, where the assembly treats it as a strip restart).
            self.queued.append(v)
            if self.cur is not None:
                self.cur["verts_full"].append(v)
            return
        super().kick(xyz_lo)
        if self.cur is not before:
            self.cur["verts_full"] = list(self.queued) if self.carry else []
            self.carried += len(self.queued)
            self.cur["index"] = len(self.frames[-1])   # the index it gets when closed
            self.cur["state"] = dict(prim=st.prim, prmode=st.prmode, prmodecont=st.prmodecont, texa=st.texa,
                                     colclamp=st.colclamp, pabe=st.pabe, ctx=[dict(c) for c in st.ctx])
            self.cur["use_uv"] = self.uv_last
            self.cur["miptbp"] = list(self.miptbp[self.cur["key"]["ctx"]])
            self.cur["attr"] = st.attrs()
            self.cur["frame"] = len(self.frames) - 1
            self.records.append(("D", self.cur))
        self.cur["verts_full"].append(v)
        self.queued = []

    def reg(self, name, v):
        if name == "ST":
            self.st_reg = v
            self.uv_last = False
            return
        if name == "UV":
            self.uv_reg = v
            self.uv_last = True
            return
        if name in ("XYZ2", "XYZF2"):
            self.kick(v & 0xFFFFFFFF, _xyzf_full(v) if name == "XYZF2" else v)
            return
        if name in ("XYZ3", "XYZF3"):
            self.adc_a_d += 1
            self.kick(v & 0xFFFFFFFF, _xyzf_full(v) if name == "XYZF3" else v, 1)
            return
        if name == "PRIM":
            # A PRIM write initialises the vertex queue (GS User's Manual, PRIM): the queued vertices are
            # gone and the strip/fan window restarts at the next vertex, inside an open draw too.
            self.queued = []
            self.restart = True
        if name.startswith("MIPTBP"):
            c = int(name[-1]) - 1
            self.miptbp[c][0 if name[6] == "1" else 1] = v
        if name in self.last_xfer_regs:
            self.last_xfer_regs[name] = v
        super().reg(name, v)

    def image_bytes(self, data):
        self._close()
        self.images.append((len(self.frames) - 1, len(data)))
        r = self.last_xfer_regs
        bb = r["BITBLTBUF"]
        self.xfers.append(dict(frame=len(self.frames) - 1, draws_before=len(self.frames[-1]), bitbltbuf=bb,
                               trxpos=r["TRXPOS"], trxreg=r["TRXREG"], trxdir=r["TRXDIR"],
                               dbp=((bb >> 32) & 0x3FFF) * 256, dbw=((bb >> 48) & 0x3F) * 64, dpsm=(bb >> 56) & 0x3F,
                               rrw=r["TRXREG"] & 0xFFF, rrh=(r["TRXREG"] >> 32) & 0xFFF,
                               dsax=(r["TRXPOS"] >> 32) & 0x7FF, dsay=(r["TRXPOS"] >> 48) & 0x7FF, data=data))
        self.records.append(("U", self.xfers[-1]))


def _packed_vertex(census, r, lo, hi):
    """PACKED XYZF2/XYZ2 (0x04/0x05, ADC at bit 111) and XYZF3/XYZ3 (0x0c/0x0d: queue only)."""
    x = lo & 0xFFFF
    y = (lo >> 32) & 0xFFFF
    z = (hi & 0xFFFFFFFF) if r in (0x05, 0x0d) else ((hi >> 4) & 0xFFFFFF)
    adc = 1 if r in (0x0c, 0x0d) else (hi >> 47) & 1
    census.kick(x | (y << 16), (x | (y << 16)) | (z << 32), adc)


def walk_transfer(data, census):
    """One GIF transfer into register writes, vertex kicks and IMAGE payloads (attributes kept)."""
    off = 0
    n = len(data)
    while off + 16 <= n:
        lo = u64(data, off)[0]
        hi = u64(data, off + 8)[0]
        off += 16
        nloop = lo & 0x7FFF
        pre = (lo >> 46) & 1
        prim = (lo >> 47) & 0x7FF
        flg = (lo >> 58) & 3
        nreg = (lo >> 60) & 0xF or 16
        regs = [(hi >> (4 * i)) & 0xF for i in range(nreg)]
        if nloop == 0:
            continue
        if pre and flg != 2:
            census.reg("PRIM", prim)
        if flg == 0:
            for _ in range(nloop):
                for r in regs:
                    if off + 16 > n:
                        return
                    lo = u64(data, off)[0]
                    hi = u64(data, off + 8)[0]
                    off += 16
                    if r == 0x0e:
                        census.reg(gc.REG.get(hi & 0xFF, "0x%02x" % (hi & 0xFF)), lo)
                    elif r == 0x00:
                        census.reg("PRIM", lo & 0x7FF)
                    elif r == 0x01:
                        # PACKED RGBAQ: R 0-7, G 32-39, B 64-71, A 96-103; Q stays the last ST write's.
                        v = (lo & 0xFF) | (((lo >> 32) & 0xFF) << 8) | ((hi & 0xFF) << 16) | (((hi >> 32) & 0xFF) << 24)
                        census.st.rgbaq = (census.st.rgbaq & ~0xFFFFFFFF) | v
                    elif r == 0x02:
                        # PACKED ST: S 0-31, T 32-63, Q 64-95
                        census.st_reg = lo
                        census.st.rgbaq = (census.st.rgbaq & 0xFFFFFFFF) | ((hi & 0xFFFFFFFF) << 32)
                        census.uv_last = False
                    elif r == 0x03:
                        census.uv_reg = (lo & 0x3FFF) | (((lo >> 32) & 0x3FFF) << 16)
                        census.uv_last = True
                    elif r in (0x04, 0x05, 0x0c, 0x0d):
                        _packed_vertex(census, r, lo, hi)
                    elif r in (0x0a, 0x0f):
                        pass
                    else:
                        census.reg(gc.REG.get(r, "0x%02x" % r), lo)
        elif flg == 1:
            total = nloop * nreg
            need = (total + 1) // 2 * 16
            if off + need > n:
                return
            words = [u64(data, off + 8 * i)[0] for i in range(total)]
            off += need
            for i, w in enumerate(words):
                r = regs[i % nreg]
                if r in (0x04, 0x05):
                    census.kick(w & 0xFFFFFFFF, _xyzf_full(w) if r == 0x04 else w)
                elif r in (0x0c, 0x0d):
                    census.kick(w & 0xFFFFFFFF, _xyzf_full(w) if r == 0x0c else w, 1)
                elif r == 0x01:
                    census.st.rgbaq = w
                elif r == 0x02:
                    census.st_reg = w
                    census.uv_last = False
                elif r == 0x03:
                    census.uv_reg = w
                    census.uv_last = True
                elif r in (0x0a, 0x0f, 0x0e):
                    pass
                else:
                    census.reg(gc.REG.get(r, "0x%02x" % r), w)
        else:
            size = nloop * 16
            census.image_bytes(data[off:off + size])
            off += size


def _attrs(v, ofx, ofy):
    v = dict(v)
    v["px"] = v["x"] / 16.0 - ofx
    v["py"] = v["y"] / 16.0 - ofy
    if v.get("uv_last"):
        v["fs"], v["ft"], v["fq"] = v["u"] / 16.0, v["v"] / 16.0, 1.0
    else:
        v["fs"], v["ft"], v["fq"] = v["s"], v["t"], v["q"]
    return v


def _sprite(v0, v1):
    v0, v1 = dict(v0), dict(v1)
    if v0["px"] > v1["px"]:
        v0["px"], v1["px"] = v1["px"], v0["px"]
        v0["fs"], v1["fs"] = v1["fs"], v0["fs"]
        v0["u"], v1["u"] = v1["u"], v0["u"]
    if v0["py"] > v1["py"]:
        v0["py"], v1["py"] = v1["py"], v0["py"]
        v0["ft"], v1["ft"] = v1["ft"], v0["ft"]
        v0["v"], v1["v"] = v1["v"], v0["v"]

    def flat(p):
        p = dict(p)
        p["z"] = v1["z"]
        p["rgba"] = v1["rgba"]
        return p
    tl = flat(v0)
    tr = flat(v0)
    tr["px"], tr["fs"], tr["u"] = v1["px"], v1["fs"], v1["u"]
    bl = flat(v1)
    bl["px"], bl["fs"], bl["u"] = v0["px"], v0["fs"], v0["u"]
    br = flat(v1)
    return [tl, tr, bl, tr, br, bl]


def expand(prim, verts, ofx, ofy):
    """VertexKick assembly (vertex_kick.cpp) -> triangle-list vertex dicts. A vertex marked `restart`
    (the first after a PRIM write) begins a new strip/fan window: the GS initialised the vertex queue."""
    out = []
    win = []
    parity = 0
    for v in verts:
        v = _attrs(v, ofx, ofy)
        draw = v.get("kick", 1)
        if v.get("restart"):
            win = []
            parity = 0
        win.append(v)
        if prim == 4:   # tristrip: 3-slot window, parity swap, degenerate skip, restart on a non-drawing kick
            while len(win) > 3:
                win.pop(0)
            if not draw:
                parity = 0
                continue
            if len(win) != 3:
                continue
            q0, q1, q2 = win
            same = lambda a, b: a["px"] == b["px"] and a["py"] == b["py"]  # noqa: E731
            if same(q0, q1) or same(q1, q2) or same(q0, q2):
                parity += 1
                continue
            out += [q0, q1, q2] if parity % 2 == 0 else [q1, q0, q2]
            parity += 1
            continue
        needed = {0: 1, 1: 2, 2: 2, 3: 3, 5: 3, 6: 2}.get(prim, 3)
        if draw:
            if len(win) >= needed:
                if prim == 3:
                    out += win[:3]
                    win = []
                elif prim == 5:
                    out += win[:3]
                    win.pop(1)
                elif prim == 6:
                    out += _sprite(win[0], win[1])
                    win = []
                else:
                    win = []
        else:
            parity = 0
            if len(win) >= needed:
                if prim == 5:
                    win.pop(1)
                else:
                    win = []
        if len(win) > 3:
            win.pop(0)
    return out


def pack_vertex(v):
    rgba = v["rgba"]
    return struct.pack("<ffffffHHBBBBBBBB", v["px"], v["py"], float(v["z"] or 0), v["fs"], v["ft"], v["fq"],
                       v["u"], v["v"], rgba & 0xFF, (rgba >> 8) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 24) & 0xFF,
                       0, 1, 0, 0)


def draw_record(r):
    """One draw record's bytes (+ the expanded triangle list for inspection)."""
    k = r["key"]
    c = r["state"]["ctx"][k["ctx"]]
    ofx = (c["XYOFFSET"] & 0xFFFF) / 16.0
    ofy = ((c["XYOFFSET"] >> 32) & 0xFFFF) / 16.0
    tris = expand(k["prim"], r["verts_full"], ofx, ofy)
    body = struct.pack("<IIIII", 2, r["index"], (r["attr"] & ~7) | k["prim"], 1 if r["use_uv"] else 0, k["ctx"])
    body += struct.pack("<12Q", c["TEX0"], c["TEX1"], c["CLAMP"], c["ALPHA"], c["TEST"], c["FRAME"], c["ZBUF"],
                        c["XYOFFSET"], c["SCISSOR"], c["FBA"], r["miptbp"][0], r["miptbp"][1])
    body += struct.pack("<QIII", r["state"]["texa"], r["state"]["colclamp"], r["state"]["pabe"], len(tris))
    for v in tris:
        body += pack_vertex(v)
    return body, tris


def export_frame(records, frame):
    recs = [r for r in records if r[1]["frame"] == frame]
    body = bytearray()
    nu = nd = nv = 0
    for kind, r in recs:
        if kind == "U":
            body += struct.pack("<9I", 1, r["dbp"], r["dbw"], r["dpsm"], r["dsax"], r["dsay"], r["rrw"], r["rrh"],
                                len(r["data"]))
            body += r["data"]
            nu += 1
        else:
            rec, tris = draw_record(r)
            body += rec
            nd += 1
            nv += len(tris)
    return b"GSF1" + struct.pack("<I", len(recs)) + bytes(body), nu, nd, nv


def parse(path, carry=True):
    class Configured(VertexCensus):
        def __init__(self, st):
            super().__init__(st)
            self.carry = carry
    gc.Census = Configured
    gc.walk_transfer = walk_transfer
    data = gc.read_dump_bytes(path)
    return gc.parse_dump(data)["census"]


def inspect(census, frame, idx):
    d = census.frames[frame][idx]
    rec, tris = draw_record(d)
    xs = [v["px"] for v in tris]
    ys = [v["py"] for v in tris]
    kinds = "".join(("|" if v.get("restart") else "") + ("K" if v.get("kick", 1) else "q") for v in d["verts_full"])
    print("draw [%d] %s" % (idx, gc.fmt_key(d["key"])))
    print("  kicks %s (%d vertices, %d queued, %d restarts) -> %d triangle verts bbox x %.2f..%.2f y %.2f..%.2f" % (
        kinds, len(d["verts_full"]), kinds.count("q"), kinds.count("|"), len(tris), min(xs) if xs else 0,
        max(xs) if xs else 0, min(ys) if ys else 0, max(ys) if ys else 0))
    for v in d["verts_full"][:8]:
        print("   %s xy=(%.3f,%.3f) z=%s rgba=%08x q=%g s=%g t=%g uv=(%d,%d)" % (
            "kick " if v.get("kick", 1) else "queue", v["x"] / 16.0, v["y"] / 16.0,
            ("%#x" % v["z"]) if v["z"] is not None else "?", v["rgba"], v["q"], v["s"], v["t"], v["u"], v["v"]))


# ---- selftest -------------------------------------------------------------------

def _qw(lo, hi):
    return struct.pack("<QQ", lo, hi)


def _ad(reg, v):
    return _qw(v, reg)


def _xyz(x, y, z):
    return (x * 16) | ((y * 16) << 16) | (z << 32)


def selftest():
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("PASS  " if cond else "FAIL  ") + what)
        ok = ok and cond

    # A+D stream: XYOFFSET (1792,1824), FRAME 0x70000 CT32 fbw 512, PRIM tristrip+iip+abe, RGBAQ black As 0x80,
    # then the letterbox strip as the title sends it: XYZ3, XYZ3, XYZ2, XYZ2 (two triangles over (0,0)-(512,67)).
    c = VertexCensus(gc.GsState())
    pkt = gc._giftag(9, 1, 0, 0, 0, [0x0e])
    pkt += _ad(0x18, (1792 * 16) | ((1824 * 16) << 32))              # XYOFFSET_1
    pkt += _ad(0x4c, (0x70000 // 8192) | ((512 // 64) << 16))        # FRAME_1 CT32 0x70000 fbw 512
    pkt += _ad(0x00, 4 | (1 << 3) | (1 << 6))                         # PRIM tristrip iip abe
    pkt += _ad(0x01, 0x80000000)                                      # RGBAQ (0,0,0,0x80)
    pkt += _ad(0x0d, _xyz(1792, 1891, 0xFFFFFFFF))                    # XYZ3 (0, 67)   queued
    pkt += _ad(0x0d, _xyz(1792, 1824, 0xFFFFFFFF))                    # XYZ3 (0, 0)    queued
    pkt += _ad(0x05, _xyz(2304, 1891, 0xFFFFFFFF))                    # XYZ2 (512, 67) draws
    pkt += _ad(0x05, _xyz(2304, 1824, 0xFFFFFFFF))                    # XYZ2 (512, 0)  draws
    pkt += _ad(0x3f, 0)                                               # TEXFLUSH (ignored)
    walk_transfer(pkt, c)
    c._close()
    d = c.frames[0]
    check(len(d) == 1, "one draw (got %d)" % len(d))
    check(c.adc_a_d == 2 and c.carried == 2, "two A+D XYZ3 writes queued and carried into the draw (%d, %d)" % (
        c.adc_a_d, c.carried))
    kinds = "".join("K" if v["kick"] else "q" for v in d[0]["verts_full"])
    check(kinds == "qqKK", "vertex order queue, queue, kick, kick (got %s)" % kinds)
    rec, tris = draw_record(d[0])
    xs = sorted(set(v["px"] for v in tris))
    ys = sorted(set(v["py"] for v in tris))
    check(len(tris) == 6 and xs == [0.0, 512.0] and ys == [0.0, 67.0],
          "the strip expands to two triangles over x 0..512, y 0..67 (verts %d, x %s, y %s)" % (len(tris), xs, ys))
    check(all(v["rgba"] == 0x80000000 for v in tris) and all(v["z"] == 0xFFFFFFFF for v in tris),
          "the queued vertices carry RGBAQ and Z as written")
    check(d[0]["key"]["frame"]["fbp"] == 0x70000 and d[0]["key"]["abe"] == 1, "the draw's state: FRAME 0x70000, abe")
    # The old rule (XYZ3 dropped) would give a zero-width strip: assemble only the two kicks and compare.
    tris_dropped = expand(4, [v for v in d[0]["verts_full"] if v["kick"]], 1792, 1824)
    check(tris_dropped == [], "two kicks alone draw nothing (the dropped-XYZ3 exporter's zero-width strip)")
    # PACKED XYZ3 (0x0d) and an ADC XYZ2 queue the same way; a PRIM write empties the queue.
    c2 = VertexCensus(gc.GsState())
    pkt2 = gc._giftag(1, 0, 1, 4 | (1 << 3), 0, [0x0d, 0x05, 0x05])
    pkt2 += _qw((10 * 16) | ((20 * 16) << 32), 7)                     # PACKED XYZ3 (10,20) z 7
    pkt2 += _qw((30 * 16) | ((40 * 16) << 32), 1 << 47)                # PACKED XYZ2 with ADC: queued
    pkt2 += _qw((50 * 16) | ((60 * 16) << 32), 9)                      # XYZ2 draws -> one triangle
    pkt2 += gc._giftag(1, 1, 1, 4 | (1 << 3), 0, [0x0d, 0x05, 0x05])   # PRE: PRIM written -> queue emptied
    pkt2 += _qw((11 * 16) | ((21 * 16) << 32), 7)
    pkt2 += _qw((31 * 16) | ((41 * 16) << 32), 1 << 47)
    pkt2 += _qw((51 * 16) | ((61 * 16) << 32), 9)
    walk_transfer(pkt2, c2)
    c2._close()
    d2 = c2.frames[0]
    rec2, tris2 = draw_record(d2[0]) if d2 else (b"", [])
    check(len(d2) == 1 and d2[0]["verts"] == 2, "one draw of two drawing kicks across the PRIM write (%s)" % (
        [x["verts"] for x in d2]))
    check(len(tris2) == 6 and tris2[0]["px"] == 10.0 - 0 and tris2[3]["px"] == 11.0,
          "PACKED XYZ3 + ADC XYZ2 + XYZ2 -> a triangle each (%d verts)" % len(tris2))
    # A PRIM write between a queued vertex and the next kicks re-initialises the queue (the vertex is gone).
    c3 = VertexCensus(gc.GsState())
    pkt3 = gc._giftag(4, 1, 1, 4 | (1 << 3), 0, [0x0e])
    pkt3 += _ad(0x0d, _xyz(10, 20, 7))                                # XYZ3 queued
    pkt3 += _ad(0x00, 4 | (1 << 3))                                   # PRIM rewritten: queue emptied
    pkt3 += _ad(0x05, _xyz(30, 40, 7))
    pkt3 += _ad(0x05, _xyz(50, 60, 7))
    walk_transfer(pkt3, c3)
    c3._close()
    tris3 = draw_record(c3.frames[0][0])[1] if c3.frames[0] else None
    check(c3.carried == 0 and tris3 == [], "the queue is re-initialised by the PRIM write (carried %d, tris %s)" % (
        c3.carried, None if tris3 is None else len(tris3)))
    # A PRIM write MID-draw (same state vector) restarts the fan/strip window: PRIM, fan of 4 kicks, PRIM, fan of
    # 3 kicks = 2 + 1 triangles (the GS initialised the vertex queue; the second fan has its own centre), never
    # 2 + 3 bridged through the first fan's centre.
    c4 = VertexCensus(gc.GsState())
    pkt4 = gc._giftag(9, 1, 1, 5 | (1 << 3), 0, [0x0e])
    pkt4 += _ad(0x05, _xyz(100, 100, 1))                              # fan 1: centre
    pkt4 += _ad(0x05, _xyz(200, 100, 1))
    pkt4 += _ad(0x05, _xyz(200, 200, 1))                              # triangle 1
    pkt4 += _ad(0x05, _xyz(100, 200, 1))                              # triangle 2
    pkt4 += _ad(0x00, 5 | (1 << 3))                                   # PRIM rewritten, same value: restart
    pkt4 += _ad(0x05, _xyz(300, 300, 1))                              # fan 2: centre
    pkt4 += _ad(0x05, _xyz(400, 300, 1))
    pkt4 += _ad(0x05, _xyz(400, 400, 1))                              # triangle 3
    pkt4 += _ad(0x3f, 0)
    walk_transfer(pkt4, c4)
    c4._close()
    d4 = c4.frames[0]
    tris4 = draw_record(d4[0])[1] if len(d4) == 1 else []
    kinds4 = "".join(("|" if v.get("restart") else "") + "K" for v in d4[0]["verts_full"]) if d4 else ""
    check(len(d4) == 1 and kinds4 == "|KKKK|KKK",
          "one trifan draw of 7 kicks, restarts at the GIFtag's PRE and after the 4th kick (%s)" % kinds4)
    check(len(tris4) == 9 and [v["px"] for v in tris4[6:]] == [300.0, 400.0, 400.0],
          "fan, PRIM, fan -> 2 + 1 triangles, the third on the second centre (got %d triangles: %s)" % (
              len(tris4) // 3, [v["px"] for v in tris4[6::3]]))
    # The same on a tristrip: kick x3 (one triangle), PRIM, kick x3 (one triangle) = 2, not 4.
    c5 = VertexCensus(gc.GsState())
    pkt5 = gc._giftag(7, 1, 1, 4 | (1 << 3), 0, [0x0e])
    for x, y in ((0, 0), (10, 0), (0, 10)):
        pkt5 += _ad(0x05, _xyz(x, y, 1))
    pkt5 += _ad(0x00, 4 | (1 << 3))
    for x, y in ((50, 50), (60, 50), (50, 60)):
        pkt5 += _ad(0x05, _xyz(x, y, 1))
    walk_transfer(pkt5, c5)
    c5._close()
    tris5 = draw_record(c5.frames[0][0])[1] if len(c5.frames[0]) == 1 else []
    check(len(tris5) == 6 and tris5[3]["px"] == 50.0, "strip, PRIM, strip -> 1 + 1 triangles (%d)" % (len(tris5) // 3))
    # The 64-bit XYZF2/XYZF3 forms: Z is bits 32-55, F bits 56-63 (dropped); XYZ2 keeps the full 32-bit Z.
    c6 = VertexCensus(gc.GsState())
    pkt6 = gc._giftag(3, 1, 1, 3 | (1 << 3), 0, [0x0e])
    pkt6 += _ad(0x0c, _xyz(1, 1, 0x123456) | (0xAB << 56))            # XYZF3: Z 0x123456, F 0xAB
    pkt6 += _ad(0x04, _xyz(2, 1, 0x654321) | (0xCD << 56))            # XYZF2: Z 0x654321, F 0xCD
    pkt6 += _ad(0x05, _xyz(1, 2, 0xFFFFFFFF))                         # XYZ2: Z 32-bit
    walk_transfer(pkt6, c6)
    c6._close()
    zs = [v["z"] for v in c6.frames[0][0]["verts_full"]] if c6.frames[0] else []
    check(zs == [0x123456, 0x654321, 0xFFFFFFFF], "XYZF3/XYZF2 Z masked to 24 bits, XYZ2 32-bit (%s)" % [hex(z) for z in zs])
    c7 = VertexCensus(gc.GsState())
    pkt7 = gc._giftag(1, 1, 1, 3 | (1 << 3), 1, [0x0c, 0x04, 0x05])   # REGLIST: the same three
    pkt7 += struct.pack("<QQQQ", _xyz(1, 1, 0x123456) | (0xAB << 56), _xyz(2, 1, 0x654321) | (0xCD << 56),
                        _xyz(1, 2, 0xFFFFFFFF), 0)
    walk_transfer(pkt7, c7)
    c7._close()
    zs7 = [v["z"] for v in c7.frames[0][0]["verts_full"]] if c7.frames[0] else []
    check(zs7 == [0x123456, 0x654321, 0xFFFFFFFF], "REGLIST XYZF3/XYZF2/XYZ2 the same (%s)" % [hex(z) for z in zs7])
    # The record layout: the header fields and the 36-byte VertexRec.
    check(len(rec) == 5 * 4 + 12 * 8 + 8 + 3 * 4 + 6 * 36, "draw record size %d" % len(rec))
    check(struct.unpack_from("<I", rec, 4)[0] == 0 and struct.unpack_from("<I", rec, 8)[0] & 7 == 4,
          "record index 0, PRIM type tristrip in the attribute word")
    print("gsf1_export selftest: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main(argv):
    if "--selftest" in argv:
        return selftest()
    if len(argv) < 2 or "--frame" not in argv:
        print(__doc__)
        return 2
    path = argv[1]
    frame = int(argv[argv.index("--frame") + 1])
    out = argv[argv.index("--out") + 1] if "--out" in argv else None
    inspects = [int(argv[i + 1]) for i, a in enumerate(argv) if a == "--inspect"]
    census = parse(path, carry="--no-carry" not in argv)
    stream, nu, nd, nv = export_frame(census.records, frame)
    restarts = sum(1 for d in census.frames[frame] for i, v in enumerate(d["verts_full"]) if i and v.get("restart"))
    print("gsf1_export v%d %s frame %d of %d: %d records (%d uploads, %d draws, %d triangle verts); A+D XYZ3/XYZF3 "
          "writes in the dump %d; queued vertices carried into draws %d; mid-draw PRIM restarts %d" % (
              EXPORTER_VERSION, os.path.basename(path), frame, len(census.frames), nu + nd, nu, nd, nv,
              census.adc_a_d, census.carried, restarts))
    if out:
        with open(out, "wb") as f:
            f.write(stream)
        print("-> %s (%d bytes)" % (out, len(stream)))
    for idx in inspects:
        inspect(census, frame, idx)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
