#!/usr/bin/env python3
"""gsdump_census.py -- PCSX2 GS dump (.gs / .gs.zst) reader and draw census.

The dump is the GS register stream the reference emulator fed its GS for one or
more frames, so it is the ground truth for "what the title asked the GS to
draw". This tool lists every draw (a run of vertex kicks under one GS state),
groups the draws by their blend / test / texture-function state, and extracts
the embedded screenshot, so a dump is comparable with a `.gsdl` recording.

Format (PCSX2 GSDump, zstd-framed when the name ends in .zst; informed by the
read-only OLD tools/parse_gsdump.py, gsdump_state.py, dump_transfers.py):
  u32 0xFFFFFFFF | u32 header_size | GSDumpHeader (9 x u32: state_version,
  state_size, serial_offset, serial_size, crc, screenshot_w, screenshot_h,
  screenshot_offset, screenshot_size) | serial | screenshot (w*h*4 RGBA) |
  state (GSState freeze: version, env regs, 2 contexts, vertex regs, ...) |
  privileged regs (0x2000) | packets: 0 = transfer (u8 path, u32 size, data),
  1 = vsync (u8 field), 2 = read-FIFO (u32 size), 3 = priv regs (0x2000).

GIF decoding follows the GS User's Manual: PACKED (A+D, PRIM, RGBAQ, ST, UV,
XYZ2/XYZF2 with the ADC bit at 111, XYZ3/XYZF3 never kick), REGLIST (64-bit
registers two per quadword, odd totals padded), IMAGE (skipped, counted).
PRMODECONT=0 takes the drawing attributes from PRMODE instead of PRIM.

Usage:
  gsdump_census.py <dump> [--frame N] [--draws] [--screenshot out.ppm]
                   [--json out.json] [--max N]
  gsdump_census.py --selftest
"""
import json
import os
import struct
import sys
from collections import OrderedDict

PRIM_NAMES = {0: "point", 1: "line", 2: "linestrip", 3: "tri", 4: "tristrip", 5: "trifan", 6: "sprite"}
PSM_NAMES = {0: "CT32", 1: "CT24", 2: "CT16", 10: "CT16S", 19: "T8", 20: "T4", 27: "T8H", 36: "T4HL",
             44: "T4HH", 48: "Z32", 49: "Z24", 50: "Z16", 58: "Z16S"}

REG = {
    0x00: "PRIM", 0x01: "RGBAQ", 0x02: "ST", 0x03: "UV", 0x04: "XYZF2", 0x05: "XYZ2",
    0x06: "TEX0_1", 0x07: "TEX0_2", 0x08: "CLAMP_1", 0x09: "CLAMP_2", 0x0a: "FOG",
    0x0c: "XYZF3", 0x0d: "XYZ3", 0x0e: "A+D", 0x0f: "NOP",
    0x14: "TEX1_1", 0x15: "TEX1_2", 0x16: "TEX2_1", 0x17: "TEX2_2",
    0x18: "XYOFFSET_1", 0x19: "XYOFFSET_2", 0x1a: "PRMODECONT", 0x1b: "PRMODE", 0x1c: "TEXCLUT",
    0x22: "SCANMSK", 0x34: "MIPTBP1_1", 0x35: "MIPTBP1_2", 0x36: "MIPTBP2_1", 0x37: "MIPTBP2_2",
    0x3b: "TEXA", 0x3d: "FOGCOL", 0x3f: "TEXFLUSH",
    0x40: "SCISSOR_1", 0x41: "SCISSOR_2", 0x42: "ALPHA_1", 0x43: "ALPHA_2", 0x44: "DIMX", 0x45: "DTHE",
    0x46: "COLCLAMP", 0x47: "TEST_1", 0x48: "TEST_2", 0x49: "PABE", 0x4a: "FBA_1", 0x4b: "FBA_2",
    0x4c: "FRAME_1", 0x4d: "FRAME_2", 0x4e: "ZBUF_1", 0x4f: "ZBUF_2",
    0x50: "BITBLTBUF", 0x51: "TRXPOS", 0x52: "TRXREG", 0x53: "TRXDIR", 0x54: "HWREG",
    0x60: "SIGNAL", 0x61: "FINISH", 0x62: "LABEL",
}

VERTS_PER_PRIM = {0: 1, 1: 2, 2: 1, 3: 3, 4: 1, 5: 1, 6: 2}


def read_dump_bytes(path):
    data = open(path, "rb").read()
    if path.endswith(".zst") or data[:4] == b"\x28\xb5\x2f\xfd":
        try:
            import zstandard   # the module when the interpreter carries it ...
            data = zstandard.ZstdDecompressor().stream_reader(data).read()
        except ImportError:    # ... else the `zstd` CLI (the configured ctest interpreter may lack the module)
            import shutil
            import subprocess
            if not shutil.which("zstd"):
                raise
            data = subprocess.run(["zstd", "-d", "-c", "-q", path], check=True, capture_output=True).stdout
    return data


class GsState:
    """The GS drawing environment the census tracks (both contexts)."""

    def __init__(self):
        self.prim = 0
        self.prmode = 0
        self.prmodecont = 1
        self.texa = 0
        self.colclamp = 1
        self.pabe = 0
        self.dthe = 0
        self.fogcol = 0
        self.scanmsk = 0
        self.ctx = [dict(TEX0=0, TEX1=0, CLAMP=0, ALPHA=0, TEST=0, FRAME=0, ZBUF=0, XYOFFSET=0, SCISSOR=0, FBA=0)
                    for _ in range(2)]
        self.rgbaq = 0

    def attrs(self):
        """The drawing attributes in force: PRIM, or PRMODE when PRMODECONT=0 (PRIM type always from PRIM)."""
        src = self.prim if self.prmodecont else (self.prmode & ~7) | (self.prim & 7)
        return src

    def write(self, name, v):
        if name == "PRIM":
            self.prim = v & 0x7FF
        elif name == "PRMODE":
            self.prmode = v & 0x7FF
        elif name == "PRMODECONT":
            self.prmodecont = v & 1
        elif name == "TEXA":
            self.texa = v
        elif name == "COLCLAMP":
            self.colclamp = v & 1
        elif name == "PABE":
            self.pabe = v & 1
        elif name == "DTHE":
            self.dthe = v & 1
        elif name == "FOGCOL":
            self.fogcol = v
        elif name == "SCANMSK":
            self.scanmsk = v & 3
        elif name == "RGBAQ":
            self.rgbaq = v
        elif name[-2:] in ("_1", "_2"):
            c = int(name[-1]) - 1
            base = name[:-2]
            if base in self.ctx[c]:
                self.ctx[c][base] = v


def decode_draw_key(st, prim_type, ctx_i):
    """The census key of one draw from the live state (register fields quoted by name)."""
    a = st.attrs()
    iip = (a >> 3) & 1
    tme = (a >> 4) & 1
    fge = (a >> 5) & 1
    abe = (a >> 6) & 1
    fst = (a >> 8) & 1
    c = st.ctx[ctx_i]
    al = c["ALPHA"]
    alpha = (al & 3, (al >> 2) & 3, (al >> 4) & 3, (al >> 6) & 3, (al >> 32) & 0xFF)
    t = c["TEST"]
    test = (t & 1, (t >> 1) & 7, (t >> 4) & 0xFF, (t >> 12) & 3, (t >> 14) & 1, (t >> 15) & 1,
            (t >> 16) & 1, (t >> 17) & 3)
    f = c["FRAME"]
    frame = dict(fbp=(f & 0x1FF) * 8192, fbw=((f >> 16) & 0x3F) * 64, psm=(f >> 24) & 0x3F,
                 fbmsk=(f >> 32) & 0xFFFFFFFF)
    z = c["ZBUF"]
    zbuf = dict(zbp=(z & 0x1FF) * 8192, psm=(z >> 24) & 0xF, zmsk=(z >> 32) & 1)
    tex = None
    if tme:
        x = c["TEX0"]
        tex = dict(tbp=(x & 0x3FFF) * 256, tbw=((x >> 14) & 0x3F) * 64, psm=(x >> 20) & 0x3F,
                   tw=1 << ((x >> 26) & 0xF), th=1 << ((x >> 30) & 0xF), tcc=(x >> 34) & 1,
                   tfx=(x >> 35) & 3, cbp=((x >> 37) & 0x3FFF) * 256, cpsm=(x >> 51) & 0xF,
                   csm=(x >> 55) & 1, csa=(x >> 56) & 0x1F)
        t1 = c["TEX1"]
        tex["mmag"] = (t1 >> 5) & 1
        tex["mmin"] = (t1 >> 6) & 7
        cl = c["CLAMP"]
        tex["wms"] = cl & 3
        tex["wmt"] = (cl >> 2) & 3
    texa = (st.texa & 0xFF, (st.texa >> 32) & 0xFF, (st.texa >> 15) & 1)
    return dict(prim=prim_type, iip=iip, tme=tme, fge=fge, abe=abe, fst=fst, ctx=ctx_i, alpha=alpha,
                pabe=st.pabe, colclamp=st.colclamp, test=test, frame=frame, zbuf=zbuf, tex=tex, texa=texa,
                fba=c["FBA"] & 1)


def key_tuple(k, with_addr):
    """Hashable group key. with_addr adds FBP / TBP / CBP (the scene's buffer layout)."""
    tex = k["tex"]
    t = (k["prim"], k["iip"], k["tme"], k["fge"], k["abe"], k["fst"], k["ctx"],
         k["alpha"] if k["abe"] else None, k["pabe"], k["colclamp"], k["test"], k["frame"]["psm"],
         k["frame"]["fbmsk"], k["zbuf"]["psm"], k["zbuf"]["zmsk"], k["fba"],
         (tex["psm"], tex["tfx"], tex["tcc"], tex["cpsm"] if tex["psm"] in (19, 20, 27, 36, 44) else None,
          tex["tw"], tex["th"]) if tex else None,
         k["texa"] if (tex and tex["psm"] in (1, 2, 10)) else None)   # TEXA expands CT24/CT16 alpha
    if with_addr:
        t += (k["frame"]["fbp"], k["zbuf"]["zbp"], (tex["tbp"], tex["cbp"]) if tex else None)
    return t


def fmt_key(k):
    tex = k["tex"]
    s = "%s%s%s%s%s ctx%d" % (PRIM_NAMES.get(k["prim"], "?"), "+iip" if k["iip"] else "",
                             "+tex" if k["tme"] else "", "+abe" if k["abe"] else "", "+fge" if k["fge"] else "",
                             k["ctx"] + 1)
    if k["abe"]:
        s += " ALPHA=%d%d%d%d fix=0x%02x" % k["alpha"]
    s += " PABE=%d COLCLAMP=%d" % (k["pabe"], k["colclamp"])
    te = k["test"]
    s += " TEST(ate=%d atst=%d aref=0x%02x afail=%d date=%d datm=%d zte=%d ztst=%d)" % te
    s += " FRAME=%#x/%s fbmsk=%#x ZBUF=%#x/%s zmsk=%d" % (
        k["frame"]["fbp"], PSM_NAMES.get(k["frame"]["psm"], k["frame"]["psm"]), k["frame"]["fbmsk"],
        k["zbuf"]["zbp"], PSM_NAMES.get(k["zbuf"]["psm"], k["zbuf"]["psm"]), k["zbuf"]["zmsk"])
    if tex:
        s += " TEX0=%#x/%s %dx%d tfx=%d tcc=%d" % (tex["tbp"], PSM_NAMES.get(tex["psm"], tex["psm"]),
                                                   tex["tw"], tex["th"], tex["tfx"], tex["tcc"])
        if tex["psm"] in (19, 20, 27, 36, 44):
            s += " cbp=%#x cpsm=%s" % (tex["cbp"], PSM_NAMES.get(tex["cpsm"], tex["cpsm"]))
        s += " fst=%d" % k["fst"]
        if tex["psm"] in (1, 2, 10):
            s += " TEXA(ta0=0x%02x ta1=0x%02x aem=%d)" % k["texa"]
    return s


class Census:
    """Per-frame ordered draw lists from the packet stream."""

    def __init__(self, st):
        self.st = st
        self.frames = [[]]
        self.cur = None            # the open draw: dict(key, verts, kicks)
        self.images = []           # (frame, bytes)
        self.pending = 0           # kicks since the last primitive completed
        self.rgba = {}

    def _close(self):
        if self.cur and self.cur["verts"]:
            self.frames[-1].append(self.cur)
        self.cur = None

    def kick(self, xyz_lo):
        st = self.st
        prim_type = st.prim & 7
        a = st.attrs()
        ctx_i = (a >> 9) & 1
        k = decode_draw_key(st, prim_type, ctx_i)
        kt = key_tuple(k, True)
        if self.cur is None or self.cur["kt"] != kt:
            self._close()
            self.cur = dict(key=k, kt=kt, verts=0, rgba=OrderedDict(), xy=[])
        self.cur["verts"] += 1
        r = st.rgbaq & 0xFF
        g = (st.rgbaq >> 8) & 0xFF
        b = (st.rgbaq >> 16) & 0xFF
        al = (st.rgbaq >> 24) & 0xFF
        c = (r, g, b, al)
        self.cur["rgba"][c] = self.cur["rgba"].get(c, 0) + 1
        if len(self.cur["xy"]) < 4096:
            self.cur["xy"].append(((xyz_lo & 0xFFFF) / 16.0, ((xyz_lo >> 16) & 0xFFFF) / 16.0))

    def reg(self, name, v):
        # A state write that changes the draw key closes the open draw (the GS flushes
        # nothing itself; the grouping is the census's: one draw = one state vector).
        # A vertex register reached through A+D behaves as the register itself: XYZ2 /
        # XYZF2 kick, XYZ3 / XYZF3 do not (GS User's Manual, vertex kick registers).
        if name in ("XYZ2", "XYZF2"):
            self.kick(v & 0xFFFFFFFF)
            return
        if name in ("XYZ3", "XYZF3", "ST", "UV", "FOG", "NOP", "TEXFLUSH"):
            return
        self.st.write(name, v)

    def vsync(self):
        self._close()
        self.frames.append([])

    def image(self, nbytes):
        self._close()
        self.images.append((len(self.frames) - 1, nbytes))


def walk_transfer(data, census):
    """Decode one GIF transfer (any path) into register writes and kicks."""
    off = 0
    n = len(data)
    u64 = struct.Struct("<Q").unpack_from
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
        if flg == 0:   # PACKED
            for _ in range(nloop):
                for r in regs:
                    if off + 16 > n:
                        return
                    lo = u64(data, off)[0]
                    hi = u64(data, off + 8)[0]
                    off += 16
                    if r == 0x0e:
                        census.reg(REG.get(hi & 0xFF, "0x%02x" % (hi & 0xFF)), lo)
                    elif r == 0x00:
                        census.reg("PRIM", lo & 0x7FF)
                    elif r == 0x01:
                        # PACKED RGBAQ: R bits 0-7, G 32-39, B 64-71, A 96-103, Q 64-95 of the upper word
                        v = (lo & 0xFF) | (((lo >> 32) & 0xFF) << 8) | ((hi & 0xFF) << 16) | (((hi >> 32) & 0xFF) << 24)
                        census.reg("RGBAQ", v)
                    elif r in (0x04, 0x05):
                        adc = (hi >> 47) & 1
                        x = lo & 0xFFFF
                        y = (lo >> 32) & 0xFFFF
                        if not adc:
                            census.kick(x | (y << 16))
                    elif r in (0x0c, 0x0d):
                        pass   # XYZ3 / XYZF3: no kick
                    elif r in (0x02, 0x03, 0x0a, 0x0f):
                        pass   # ST / UV / FOG / NOP: vertex attributes, not census state
                    else:
                        census.reg(REG.get(r, "0x%02x" % r), lo)
        elif flg == 1:   # REGLIST: 64-bit registers, two per quadword
            total = nloop * nreg
            words = []
            need = (total + 1) // 2 * 16
            if off + need > n:
                return
            for i in range(total):
                words.append(u64(data, off + 8 * i)[0])
            off += need
            for i, w in enumerate(words):
                r = regs[i % nreg]
                if r in (0x04, 0x05):
                    census.kick(w & 0xFFFFFFFF)
                elif r in (0x0c, 0x0d, 0x02, 0x03, 0x0a, 0x0f):
                    pass
                elif r == 0x0e:
                    pass   # A+D is not a REGLIST register
                else:
                    census.reg(REG.get(r, "0x%02x" % r), w)
        else:            # IMAGE / IMAGE2
            size = nloop * 16
            census.image(size)
            off += size


def parse_dump(data):
    """Header, screenshot, initial state, and the packet walk. Returns a dict."""
    magic, header_size = struct.unpack_from("<II", data, 0)
    if magic != 0xFFFFFFFF:
        raise ValueError("bad magic %#x" % magic)
    hdr = struct.unpack_from("<9I", data, 8)
    state_version, state_size, serial_off, serial_size, crc, sw, sh, ss_off, ss_size = hdr
    off = 8 + 36
    serial = data[off:off + serial_size].decode("ascii", "replace")
    off += serial_size
    shot = data[off:off + ss_size]
    off += ss_size
    state = data[off:off + state_size]
    off += state_size
    priv0 = data[off:off + 0x2000]
    off += 0x2000

    st = GsState()
    # GSState freeze: u32 version, then the env registers and both contexts (u64 each).
    p = 4
    env_names = ["PRIM", "PRMODECONT", "TEXCLUT", "SCANMSK", "TEXA", "FOGCOL", "DIMX", "DTHE", "COLCLAMP",
                 "PABE", "BITBLTBUF", "TRXDIR", "TRXPOS", "TRXREG", "TRXREG_OBS"]
    env = {}
    for nm in env_names:
        env[nm] = struct.unpack_from("<Q", state, p)[0]
        p += 8
        if nm != "TRXREG_OBS":
            st.write(nm, env[nm])
    ctx_names = ["XYOFFSET", "TEX0", "TEX1", "CLAMP", "MIPTBP1", "MIPTBP2", "SCISSOR", "ALPHA", "TEST", "FBA",
                 "FRAME", "ZBUF"]
    for ci in range(2):
        for nm in ctx_names:
            v = struct.unpack_from("<Q", state, p)[0]
            p += 8
            st.write("%s_%d" % (nm, ci + 1), v)
    vtx = {}
    for nm in ["RGBAQ", "ST", "UV", "FOG", "XYZ"]:
        vtx[nm] = struct.unpack_from("<Q", state, p)[0]
        p += 8
    st.write("RGBAQ", vtx["RGBAQ"])
    st.prmode = st.prim

    census = Census(st)
    xfers = {0: 0, 1: 0, 2: 0}
    vsyncs = []
    privs = [priv0]
    while off < len(data):
        pid = data[off]
        off += 1
        if pid == 0:
            path = data[off]
            size = struct.unpack_from("<I", data, off + 1)[0]
            off += 5
            xfers[path] = xfers.get(path, 0) + 1
            walk_transfer(data[off:off + size], census)
            off += size
        elif pid == 1:
            field = data[off]
            off += 1
            vsyncs.append(field)
            census.vsync()
        elif pid == 2:
            off += 4
        elif pid == 3:
            privs.append(data[off:off + 0x2000])
            off += 0x2000
        else:
            raise ValueError("unknown packet id %d at %#x" % (pid, off - 1))
    census._close()
    return dict(serial=serial, crc=crc, shot_w=sw, shot_h=sh, shot=shot, state_version=state_version,
                env=env, init_state=st, census=census, xfers=xfers, vsyncs=vsyncs, privs=privs)


def dispfb(priv):
    out = {}
    # GS privileged register offsets (GS User's Manual 8.2): PMODE 0x00, DISPFB1 0x70, DISPLAY1 0x80,
    # DISPFB2 0x90, DISPLAY2 = DISPFB2 + 0x10.
    for nm, o in (("PMODE", 0x0), ("DISPFB1", 0x70), ("DISPLAY1", 0x80), ("DISPFB2", 0x90), ("DISPLAY2", 0x90 + 0x10)):
        out[nm] = struct.unpack_from("<Q", priv, o)[0]
    return out


def fmt_dispfb(v):
    return "fbp=%#x fbw=%d psm=%s dbx=%d dby=%d" % ((v & 0x1FF) * 8192, ((v >> 9) & 0x3F) * 64,
                                                    PSM_NAMES.get((v >> 15) & 0x1F, (v >> 15) & 0x1F),
                                                    (v >> 32) & 0x7FF, (v >> 43) & 0x7FF)


def write_ppm(path, w, h, rgba):
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        out = bytearray(w * h * 3)
        out[0::3] = rgba[0::4]
        out[1::3] = rgba[1::4]
        out[2::3] = rgba[2::4]
        f.write(out)


def group_frame(draws, with_addr):
    groups = OrderedDict()
    for i, d in enumerate(draws):
        kt = key_tuple(d["key"], with_addr)
        g = groups.get(kt)
        if g is None:
            g = groups[kt] = dict(key=d["key"], draws=0, verts=0, first=i, rgba={})
        g["draws"] += 1
        g["verts"] += d["verts"]
        for c, n in d["rgba"].items():
            g["rgba"][c] = g["rgba"].get(c, 0) + n
    return groups


def frame_report(fi, draws, show_draws, max_n, out):
    out.append("== frame %d: draws=%d verts=%d" % (fi, len(draws), sum(d["verts"] for d in draws)))
    groups = group_frame(draws, True)
    out.append("-- groups (class = prim/tex/blend state; addresses included): %d" % len(groups))
    for g in groups.values():
        top = sorted(g["rgba"].items(), key=lambda kv: -kv[1])[:3]
        out.append("  G draws=%-4d verts=%-6d first=%-4d %s | rgba top %s" % (
            g["draws"], g["verts"], g["first"], fmt_key(g["key"]),
            " ".join("%02x%02x%02x%02x:%d" % (c + (n,)) for c, n in top)))
    if show_draws:
        out.append("-- draws (ordered):")
        for i, d in enumerate(draws[:max_n]):
            xs = [p[0] for p in d["xy"]]
            ys = [p[1] for p in d["xy"]]
            bb = "bbox=(%.0f,%.0f)-(%.0f,%.0f)" % (min(xs), min(ys), max(xs), max(ys)) if xs else ""
            top = sorted(d["rgba"].items(), key=lambda kv: -kv[1])[:2]
            out.append("  [%d] n=%d %s %s rgba %s" % (
                i, d["verts"], fmt_key(d["key"]), bb,
                " ".join("%02x%02x%02x%02x:%d" % (c + (n,)) for c, n in top)))


def to_json(res, frame_sel):
    fr = []
    for fi, draws in enumerate(res["census"].frames):
        if frame_sel is not None and fi != frame_sel:
            continue
        fr.append(dict(frame=fi, draws=[dict(n=d["verts"], key=d["key"],
                                             rgba_top=sorted(d["rgba"].items(), key=lambda kv: -kv[1])[:4],
                                             bbox=([min(p[0] for p in d["xy"]), min(p[1] for p in d["xy"]),
                                                    max(p[0] for p in d["xy"]), max(p[1] for p in d["xy"])]
                                                   if d["xy"] else None))
                                        for d in draws]))
    return dict(serial=res["serial"], crc="%08X" % res["crc"], shot=[res["shot_w"], res["shot_h"]],
                vsyncs=res["vsyncs"], xfers=res["xfers"], frames=fr,
                dispfb=[{k: fmt_dispfb(v) if k.startswith("DISPFB") else "%#x" % v for k, v in dispfb(p).items()}
                        for p in res["privs"]])


# ---- selftest -------------------------------------------------------------------

def _qw(lo, hi):
    return struct.pack("<QQ", lo, hi)


def _giftag(nloop, eop, pre, prim, flg, regs):
    nreg = len(regs)
    lo = nloop | (eop << 15) | (pre << 46) | (prim << 47) | (flg << 58) | ((nreg & 0xF) << 60)
    hi = 0
    for i, r in enumerate(regs):
        hi |= r << (4 * i)
    return _qw(lo, hi)


def selftest():
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("PASS  " if cond else "FAIL  ") + what)
        ok = ok and cond

    st = GsState()
    c = Census(st)
    # PACKED: A+D ALPHA_1 = (0,2,0,1) FIX 0x80, PRIM sprite+tex+abe, then 2 kicks (one sprite) and an ADC vertex.
    pkt = _giftag(1, 0, 0, 0, 0, [0x0e, 0x0e, 0x0e, 0x0e])
    pkt += _qw(0x80_0000_0000 | (0 | (2 << 2) | (0 << 4) | (1 << 6)), 0x42)          # ALPHA_1
    pkt += _qw((0 << 24) | (2 << 16) | (0 << 0), 0x4c)                                # FRAME_1 CT32 fbp 0 fbw 128
    pkt += _qw(0x1F << 20 | 0, 0x06)                                                  # TEX0_1 (overwritten by the next write)
    pkt += _qw((2 << 20) | (1 << 35) | (1 << 34), 0x06)                               # TEX0_1 CT16, TFX=1 decal, TCC=1
    pkt += _giftag(1, 0, 1, 6 | (1 << 4) | (1 << 6), 0, [0x01, 0x05, 0x01, 0x05, 0x05])
    pkt += _qw(0x80 | (0x40 << 32), (0x20) | (0x80 << 32))                            # RGBAQ r=80 g=40 b=20 a=80
    pkt += _qw((100 * 16) | ((50 * 16) << 32), 0)                                     # XYZ2
    pkt += _qw(0x80 | (0x40 << 32), (0x20) | (0x80 << 32))
    pkt += _qw((200 * 16) | ((150 * 16) << 32), 0)                                    # XYZ2
    pkt += _qw((300 * 16) | ((350 * 16) << 32), 1 << 47)                              # XYZ2 with ADC: no kick
    walk_transfer(pkt, c)
    c._close()
    d = c.frames[0]
    check(len(d) == 1, "one draw from one state vector (got %d)" % len(d))
    k = d[0]["key"]
    check(d[0]["verts"] == 2, "two kicks counted, the ADC vertex not (got %d)" % d[0]["verts"])
    check(k["prim"] == 6 and k["tme"] == 1 and k["abe"] == 1, "PRIM from the PRE tag: sprite+tex+abe")
    check(k["alpha"] == (0, 2, 0, 1, 0x80), "ALPHA_1 decoded (0,2,0,1,fix=0x80): %s" % (k["alpha"],))
    check(k["tex"]["psm"] == 2 and k["tex"]["tfx"] == 1 and k["tex"]["tcc"] == 1, "TEX0 psm/tfx/tcc")
    check(k["frame"]["psm"] == 0 and k["frame"]["fbw"] == 128, "FRAME decoded")
    check(list(d[0]["rgba"].items())[0] == ((0x80, 0x40, 0x20, 0x80), 2), "PACKED RGBAQ lanes")
    check(d[0]["xy"][0] == (100.0, 50.0), "PACKED XYZ2 x/y lanes")
    # A state change mid-stream splits the draw; PRMODECONT=0 reads PRMODE.
    pkt2 = _giftag(1, 0, 0, 0, 0, [0x0e, 0x0e])
    pkt2 += _qw(0, 0x1a)                                                              # PRMODECONT=0
    pkt2 += _qw((1 << 4) | (1 << 3), 0x1b)                                            # PRMODE: TME IIP, no ABE
    pkt2 += _giftag(2, 1, 0, 0, 1, [0x05, 0x05, 0x05])                                # REGLIST 3 regs x 2 loops (odd pad)
    pkt2 += struct.pack("<QQQQQQ", 1, 2, 3, 4, 5, 6)
    pkt2 += struct.pack("<Q", 0)                                                      # padding to the quadword
    c2 = Census(st)
    walk_transfer(pkt2, c2)
    c2._close()
    check(len(c2.frames[0]) == 1 and c2.frames[0][0]["verts"] == 6, "REGLIST 6 kicks with odd padding")
    k2 = c2.frames[0][0]["key"]
    check(k2["abe"] == 0 and k2["iip"] == 1 and k2["prim"] == 6, "PRMODECONT=0 takes IIP/TME/ABE from PRMODE, type from PRIM")
    # IMAGE skip
    pkt3 = _giftag(2, 1, 0, 0, 2, [0]) + b"\x00" * 32
    c3 = Census(GsState())
    walk_transfer(pkt3, c3)
    check(c3.images == [(0, 32)] and not c3.frames[0], "IMAGE payload counted, no draw")
    # A+D to the vertex registers: XYZ2 kicks, XYZ3 does not (the title's frame-tail sprites)
    pkt4 = _giftag(1, 1, 1, 6, 0, [0x0e, 0x0e, 0x0e])
    pkt4 += _qw((10 * 16) | ((20 * 16) << 16), 0x05)        # XYZ2 via A+D (64-bit register layout)
    pkt4 += _qw((30 * 16) | ((40 * 16) << 16), 0x0d)        # XYZ3 via A+D: no kick
    pkt4 += _qw((50 * 16) | ((60 * 16) << 16), 0x05)
    c4 = Census(GsState())
    walk_transfer(pkt4, c4)
    c4._close()
    check(len(c4.frames[0]) == 1 and c4.frames[0][0]["verts"] == 2 and c4.frames[0][0]["xy"] == [(10.0, 20.0), (50.0, 60.0)],
          "A+D XYZ2 kicks (2), A+D XYZ3 does not")
    # grouping key: same state -> one group; blend-only key ignores addresses
    g = group_frame(d + d, True)
    check(len(g) == 1 and list(g.values())[0]["verts"] == 4, "group_frame merges identical state")
    check(key_tuple(k, False) == key_tuple(dict(k, frame=dict(k["frame"], fbp=0x70000)), False),
          "address-free key ignores FBP")
    check(key_tuple(k, True) != key_tuple(dict(k, frame=dict(k["frame"], fbp=0x70000)), True),
          "addressed key separates FBP")
    check("ALPHA=0201 fix=0x80" in fmt_key(k) and "TEXA(" in fmt_key(k), "fmt_key quotes ALPHA and TEXA for CT16")
    print("gsdump_census selftest: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main(argv):
    if "--selftest" in argv:
        return selftest()
    if len(argv) < 2:
        print(__doc__)
        return 2
    path = argv[1]
    frame_sel = None
    show = "--draws" in argv
    max_n = 100000
    shot_path = None
    json_path = None
    i = 2
    while i < len(argv):
        a = argv[i]
        if a == "--frame":
            frame_sel = int(argv[i + 1]); i += 1
        elif a == "--max":
            max_n = int(argv[i + 1]); i += 1
        elif a == "--screenshot":
            shot_path = argv[i + 1]; i += 1
        elif a == "--json":
            json_path = argv[i + 1]; i += 1
        i += 1
    data = read_dump_bytes(path)
    res = parse_dump(data)
    out = []
    out.append("dump=%s bytes=%d serial=%s crc=%08X screenshot=%dx%d state_version=%#x" % (
        os.path.basename(path), len(data), res["serial"], res["crc"], res["shot_w"], res["shot_h"],
        res["state_version"]))
    # PCSX2 records every GIF transfer at path index 3 (its merged GIF path); indexes 0-2 are not
    # populated by the dumper, so the real per-index tally is printed rather than a PATH1/2/3 split.
    xfer_tally = " ".join("%d=%d" % (k, v) for k, v in sorted(res["xfers"].items()) if v) or "none"
    out.append("transfers by path index: %s (total %d) vsyncs=%d (fields %s) frames=%d" % (
        xfer_tally, sum(res["xfers"].values()), len(res["vsyncs"]),
        "".join(str(f) for f in res["vsyncs"]), len(res["census"].frames)))
    e = res["env"]
    out.append("initial env: PRIM=%#x PRMODECONT=%d TEXA=%#x COLCLAMP=%d PABE=%d DTHE=%d" % (
        e["PRIM"], e["PRMODECONT"] & 1, e["TEXA"], e["COLCLAMP"] & 1, e["PABE"] & 1, e["DTHE"] & 1))
    for pi, p in enumerate(res["privs"]):
        d = dispfb(p)
        out.append("priv[%d]: PMODE=%#x DISPFB1(%s) DISPFB2(%s)" % (pi, d["PMODE"], fmt_dispfb(d["DISPFB1"]),
                                                                     fmt_dispfb(d["DISPFB2"])))
    imgs = {}
    for fi, nb in res["census"].images:
        imgs[fi] = imgs.get(fi, 0) + nb
    out.append("IMAGE bytes per frame: %s" % " ".join("f%d=%d" % kv for kv in sorted(imgs.items())))
    for fi, draws in enumerate(res["census"].frames):
        if frame_sel is not None and fi != frame_sel:
            continue
        frame_report(fi, draws, show, max_n, out)
    print("\n".join(out))
    if shot_path and res["shot_w"]:
        write_ppm(shot_path, res["shot_w"], res["shot_h"], res["shot"])
        print("screenshot -> %s (%dx%d)" % (shot_path, res["shot_w"], res["shot_h"]))
    if json_path:
        with open(json_path, "w") as f:
            json.dump(to_json(res, frame_sel), f)
        print("json -> %s" % json_path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
