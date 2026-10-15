#!/usr/bin/env python3
"""frame_diff.py -- attribute the pixel difference between two presented frames.

Both inputs are binary PPM (P6, maxval 255) or BMP (24/32-bit, bottom-up) images
of the same size (the frames lane's native 512x448 scanout). The tool prints:

  * the frames-lane hash of each image (FrameHasher::HashNativeScale: FNV-1a 64
    over u32 w, u32 h, then R,G,B per pixel top-down) so a dump is proved to be
    the presented image by matching the lane's frames.fhash value;
  * the count of differing pixels and the bounding box of the difference;
  * a histogram of the per-pixel max |delta| (quantisation steps show up as
    multiples of 8, i.e. 5-bit steps; a missing object shows every magnitude);
  * per-channel signed mean delta (B - A) over the differing pixels (a global
    tone shift has one sign per channel; dither noise averages to ~0);
  * a coarse grid of regions (--grid N, default 64 px) with, per region, the
    differing-pixel count, the max magnitude and an FNV-1a hash of the region's
    B pixels -- the anchor for naming the draws that cover the region.

Options: --flip-a / --flip-b flip an input vertically before comparing (BMP and
bottom-up grabs); --crop x,y,w,h limits the report to a rectangle; --json writes
the structured report. Exit 0 when identical, 1 when any pixel differs.

5-bit truncation compare (--quantize-a N / --quantize-b N, N = bits kept per
channel): the GS stores a PSMCT16 frame as RGBA 5551 and drops the low bits when
DTHE = 0 (GS User's Manual, PSMCT16), so an 8-bit display of such a buffer (OLD's
display path) can only be compared after `v & ~((1 << (8 - N)) - 1)`. With it the
report also splits the residual into SUB-STEP (max |delta| <= --substep, default 16
= two 5-bit steps: sub-step render rounding) and OBJECT-level pixels (above it), and
lists the grid regions carrying object-level pixels.

--draws <drawlist_tool dump>: the OLD-side `.gsdl` dump (`drawlist_tool dump <rec>
--frame N`) whose draws' screen bounding boxes (`bbox (x0,y0,z)-(x1,y1,z)`) are
intersected with every object-level region: per region the draws covering it, by
HLE class, primitive and target (`fbp=`); --draws-fbp <hex> limits the join to the
draws targeting one frame buffer (the scene buffer the display copy reads).
"""
import argparse
import json
import os
import struct
import sys
import tempfile

FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
MASK64 = (1 << 64) - 1


def fnv1a(data, h=FNV_OFFSET):
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & MASK64
    return h


def read_ppm(path, data):
    # P6 header: magic, w, h, maxval separated by whitespace (comments allowed).
    tokens = []
    pos = 2
    while len(tokens) < 3:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        tokens.append(int(data[start:pos]))
    pos += 1
    w, h, maxval = tokens
    if maxval != 255:
        raise SystemExit(f"{path}: maxval {maxval} unsupported")
    rgb = data[pos:pos + w * h * 3]
    if len(rgb) != w * h * 3:
        raise SystemExit(f"{path}: short pixel data")
    return w, h, bytes(rgb)


def read_bmp(path, data):
    off = struct.unpack_from("<I", data, 10)[0]
    w, h, _planes, bpp = struct.unpack_from("<iihH", data, 18)
    if bpp not in (24, 32):
        raise SystemExit(f"{path}: {bpp}-bit BMP unsupported")
    bottom_up = h > 0
    h = abs(h)
    bpl = ((w * bpp // 8) + 3) & ~3
    px = bpp // 8
    out = bytearray(w * h * 3)
    for y in range(h):
        src_row = (h - 1 - y) if bottom_up else y
        row = data[off + src_row * bpl: off + src_row * bpl + w * px]
        o = y * w * 3
        for x in range(w):
            b, g, r = row[x * px], row[x * px + 1], row[x * px + 2]
            out[o] = r
            out[o + 1] = g
            out[o + 2] = b
            o += 3
    return w, h, bytes(out)


def load(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] == b"P6":
        return read_ppm(path, data)
    if data[:2] == b"BM":
        return read_bmp(path, data)
    raise SystemExit(f"{path}: not a P6 PPM or BMP")


def flip(w, h, rgb):
    stride = w * 3
    return b"".join(rgb[(h - 1 - y) * stride:(h - y) * stride] for y in range(h))


def lane_hash(w, h, rgb):
    return fnv1a(rgb, fnv1a(struct.pack("<II", w, h)))


def parse_crop(s, w, h):
    if not s:
        return 0, 0, w, h
    x, y, cw, ch = (int(v) for v in s.split(","))
    return x, y, cw, ch


def quantize(rgb, bits):
    """Keep the top `bits` bits of every channel (the PSMCT16 store with DTHE = 0 keeps 5)."""
    if bits >= 8:
        return rgb
    mask = (0xFF << (8 - bits)) & 0xFF
    table = bytes(v & mask for v in range(256))
    return rgb.translate(table)


DRAW_RE = __import__("re").compile(r"^\s*draw\[(\d+)\] seq=(\d+) (\S+) n=(\d+) fbp=(0x[0-9a-f]+)/(\S+)")
BBOX_RE = __import__("re").compile(r"bbox \((-?[\d.]+),(-?[\d.]+),-?[\d.]+\)-\((-?[\d.]+),(-?[\d.]+),-?[\d.]+\)")
HLE_RE = __import__("re").compile(r"^\s*HLE (\S+)")


def read_draws(path, fbp=None):
    """[{idx, seq, prim, n, fbp, psm, bbox (x0,y0,x1,y1), hle}] from a drawlist_tool dump (one frame)."""
    draws = []
    cur = None
    for line in open(path, errors="replace"):
        m = DRAW_RE.match(line)
        if m:
            cur = {"idx": int(m.group(1)), "seq": int(m.group(2)), "prim": m.group(3), "n": int(m.group(4)), "fbp": m.group(5), "psm": m.group(6),
                   "bbox": None, "hle": None}
            draws.append(cur)
            continue
        if cur is None:
            continue
        mb = BBOX_RE.search(line)
        if mb:
            cur["bbox"] = tuple(float(mb.group(i)) for i in range(1, 5))
        mh = HLE_RE.match(line)
        if mh:
            cur["hle"] = mh.group(1)
    if fbp is not None:
        draws = [d for d in draws if int(d["fbp"], 16) == fbp]
    return draws


def draws_over(draws, region):
    x0, y0, x1, y1 = region["x"], region["y"], region["x"] + region["w"], region["y"] + region["h"]
    out = []
    for d in draws:
        if d["bbox"] is None:
            continue
        bx0, by0, bx1, by1 = d["bbox"]
        if bx0 < x1 and bx1 > x0 and by0 < y1 and by1 > y0:
            out.append(d)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--flip-a", action="store_true")
    ap.add_argument("--flip-b", action="store_true")
    ap.add_argument("--grid", type=int, default=64)
    ap.add_argument("--crop", default="")
    ap.add_argument("--json", default="")
    ap.add_argument("--top", type=int, default=12, help="regions listed (by differing pixels)")
    ap.add_argument("--quantize-a", type=int, default=8, help="bits kept per channel of A before comparing (5 = the PSMCT16 store, DTHE 0)")
    ap.add_argument("--quantize-b", type=int, default=8, help="bits kept per channel of B before comparing")
    ap.add_argument("--substep", type=int, default=16, help="max |delta| of a SUB-STEP pixel (default 16 = two 5-bit steps); above = object-level")
    ap.add_argument("--draws", default="", help="drawlist_tool dump of the OLD frame: join the object-level regions with the draws' bboxes")
    ap.add_argument("--draws-fbp", default="", help="only the draws targeting this frame buffer (hex), e.g. 0x70000")
    args = ap.parse_args()

    wa, ha, a = load(args.a)
    wb, hb, b = load(args.b)
    if (wa, ha) != (wb, hb):
        raise SystemExit(f"size mismatch: A {wa}x{ha} vs B {wb}x{hb}")
    w, h = wa, ha
    if args.flip_a:
        a = flip(w, h, a)
    if args.flip_b:
        b = flip(w, h, b)
    print(f"A {args.a}: {w}x{h} fhash=0x{lane_hash(w, h, a):016x}")
    print(f"B {args.b}: {w}x{h} fhash=0x{lane_hash(w, h, b):016x}")
    if args.quantize_a < 8:
        a = quantize(a, args.quantize_a)
        print(f"A truncated to {args.quantize_a} bits per channel (v & 0x{(0xFF << (8 - args.quantize_a)) & 0xFF:02x}); compared fhash=0x{lane_hash(w, h, a):016x}")
    if args.quantize_b < 8:
        b = quantize(b, args.quantize_b)
        print(f"B truncated to {args.quantize_b} bits per channel; compared fhash=0x{lane_hash(w, h, b):016x}")

    cx, cy, cw, ch = parse_crop(args.crop, w, h)
    g = max(1, args.grid)
    gw = (w + g - 1) // g
    regions = {}
    hist = {}
    chan_sum = [0, 0, 0]
    chan_sign = [[0, 0], [0, 0], [0, 0]]  # per channel: (B>A count, B<A count)
    ndiff = 0
    x0 = y0 = 1 << 30
    x1 = y1 = -1
    for y in range(cy, cy + ch):
        row = y * w * 3
        for x in range(cx, cx + cw):
            i = row + x * 3
            ar, ag, ab = a[i], a[i + 1], a[i + 2]
            br, bg, bb = b[i], b[i + 1], b[i + 2]
            if ar == br and ag == bg and ab == bb:
                continue
            ndiff += 1
            dr, dg, db = br - ar, bg - ag, bb - ab
            m = max(abs(dr), abs(dg), abs(db))
            hist[m] = hist.get(m, 0) + 1
            for c, d in enumerate((dr, dg, db)):
                chan_sum[c] += d
                if d > 0:
                    chan_sign[c][0] += 1
                elif d < 0:
                    chan_sign[c][1] += 1
            x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
            key = (y // g) * gw + (x // g)
            r = regions.get(key)
            if r is None:
                r = regions[key] = [0, 0, 0]
            r[0] += 1
            r[1] = max(r[1], m)
            if m > args.substep:
                r[2] += 1
    total = cw * ch
    print(f"differing pixels: {ndiff} of {total} ({100.0 * ndiff / max(1, total):.2f}%)")
    if ndiff == 0:
        print("IDENTICAL")
        if args.json:
            with open(args.json, "w") as f:
                json.dump({"identical": True, "w": w, "h": h}, f, indent=1)
        return 0
    print(f"bbox: x {x0}..{x1}, y {y0}..{y1} ({x1 - x0 + 1}x{y1 - y0 + 1})")
    mult8 = sum(n for m, n in hist.items() if m % 8 == 0)
    print(f"max|delta| histogram (top 16): {sorted(hist.items())[:16]}")
    print(f"magnitudes that are multiples of 8 (5-bit steps): {mult8} of {ndiff} ({100.0 * mult8 / ndiff:.1f}%)")
    print("per-channel over differing pixels (B - A): "
          + ", ".join(f"{n} mean {chan_sum[c] / ndiff:+.2f} (B>A {chan_sign[c][0]}, B<A {chan_sign[c][1]})"
                      for c, n in enumerate("RGB")))
    substep = sum(n for m, n in hist.items() if m <= args.substep)
    print(f"residual split: SUB-STEP (max|delta| <= {args.substep}) {substep} px ({100.0 * substep / ndiff:.1f}%), OBJECT-level (> {args.substep}) {ndiff - substep} px")
    rows = []
    for key, (n, m, obj) in regions.items():
        ry, rx = divmod(key, gw)
        px0, py0 = rx * g, ry * g
        px1, py1 = min(w, px0 + g), min(h, py0 + g)
        hb = FNV_OFFSET
        for y in range(py0, py1):
            hb = fnv1a(b[(y * w + px0) * 3:(y * w + px1) * 3], hb)
        rows.append({"x": px0, "y": py0, "w": px1 - px0, "h": py1 - py0, "diff": n, "max": m, "object_px": obj,
                     "b_hash": f"0x{hb:016x}"})
    rows.sort(key=lambda r: (-r["diff"], r["y"], r["x"]))
    print(f"regions ({g}x{g} grid): {len(rows)} with differences; top {args.top} by differing pixels:")
    for r in rows[:args.top]:
        print(f"  region x={r['x']:3d} y={r['y']:3d} {r['w']}x{r['h']}: diff={r['diff']:5d} max={r['max']:3d} object_px={r['object_px']:4d} b_hash={r['b_hash']}")
    obj_rows = sorted((r for r in rows if r["object_px"]), key=lambda r: (-r["object_px"], r["y"], r["x"]))
    print(f"object-level regions (pixels with max|delta| > {args.substep}): {len(obj_rows)}")
    for r in obj_rows[:args.top]:
        print(f"  object region x={r['x']:3d} y={r['y']:3d} {r['w']}x{r['h']}: object_px={r['object_px']:4d} max={r['max']:3d} (diff={r['diff']})")
    joins = []
    if args.draws:
        fbp = int(args.draws_fbp, 16) if args.draws_fbp else None
        draws = read_draws(args.draws, fbp)
        print(f"draws read from {args.draws}: {len(draws)}" + (f" targeting fbp=0x{fbp:x}" if fbp is not None else ""))
        for r in obj_rows[:args.top]:
            over = draws_over(draws, r)
            by_class = {}
            for d in over:
                k = f"{d['hle']} {d['prim']} -> {d['fbp']}/{d['psm']}"
                by_class[k] = by_class.get(k, 0) + 1
            small = [d for d in over if d["bbox"] and (d["bbox"][2] - d["bbox"][0]) * (d["bbox"][3] - d["bbox"][1]) < 0.25 * w * h]   # not a full-screen pass
            joins.append({"region": [r["x"], r["y"], r["w"], r["h"]], "object_px": r["object_px"], "draws": len(over), "by_class": by_class,
                          "localised_draws": [{"idx": d["idx"], "seq": d["seq"], "prim": d["prim"], "n": d["n"], "hle": d["hle"], "bbox": d["bbox"]} for d in small[:12]]})
            print(f"  region x={r['x']} y={r['y']}: {len(over)} draws over it; by class {by_class}")
            for d in small[:6]:
                print(f"    localised draw[{d['idx']}] seq={d['seq']} {d['prim']} n={d['n']} {d['hle']} bbox=({d['bbox'][0]:.0f},{d['bbox'][1]:.0f})-({d['bbox'][2]:.0f},{d['bbox'][3]:.0f})")
    if args.json:
        with open(args.json, "w") as f:
            json.dump({"identical": False, "w": w, "h": h, "a": args.a, "b": args.b, "quantize_a": args.quantize_a, "quantize_b": args.quantize_b,
                       "fhash_a": f"0x{lane_hash(w, h, a):016x}", "fhash_b": f"0x{lane_hash(w, h, b):016x}",
                       "diff": ndiff, "substep_px": substep, "object_px": ndiff - substep, "substep_max": args.substep,
                       "bbox": [x0, y0, x1, y1], "hist": sorted(hist.items()),
                       "chan_mean": [s / ndiff for s in chan_sum], "regions": rows, "object_regions": obj_rows, "draw_joins": joins}, f, indent=1)
    return 1


def selftest():
    """Unit checks: the lane hash (the 512x448 black frame is the frames lane's known value), the PPM/BMP
    readers agree, identical -> exit 0, a one-pixel edit -> bbox + histogram + per-region report, flips."""
    fails = []

    def check(cond, what):
        print(("PASS  " if cond else "FAIL  ") + what)
        if not cond:
            fails.append(what)

    w, h = 512, 448
    black = bytes(w * h * 3)
    check(lane_hash(w, h, black) == 0xA741B33A5C037124, "lane hash of the 512x448 black frame == 0xa741b33a5c037124")
    d = tempfile.mkdtemp(prefix="frame_diff_selftest.")
    try:
        a_path, b_path, bmp_path = (os.path.join(d, n) for n in ("a.ppm", "b.ppm", "a.bmp"))
        with open(a_path, "wb") as f:
            f.write(b"P6\n# comment\n%d %d\n255\n" % (w, h))
            f.write(black)
        img = bytearray(black)
        x, y = 300, 100
        img[(y * w + x) * 3:(y * w + x) * 3 + 3] = bytes((16, 0, 200))
        with open(b_path, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (w, h))
            f.write(bytes(img))
        # a 24-bit bottom-up BMP of image b: the reader must flip it to top-down
        bpl = (w * 3 + 3) & ~3
        with open(bmp_path, "wb") as f:
            f.write(b"BM" + struct.pack("<IHHI", 54 + bpl * h, 0, 0, 54))
            f.write(struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, bpl * h, 2835, 2835, 0, 0))
            for yy in range(h - 1, -1, -1):
                row = bytearray()
                for xx in range(w):
                    r, g, bb = img[(yy * w + xx) * 3:(yy * w + xx) * 3 + 3]
                    row += bytes((bb, g, r))
                row += bytes(bpl - w * 3)
                f.write(row)
        wa, ha, ra = load(a_path)
        wb, hb, rb = load(b_path)
        wc, hc, rc = load(bmp_path)
        check((wa, ha, wb, hb) == (w, h, w, h) and ra == black, "PPM reader: size + comment line + pixels")
        check(rb == bytes(img), "PPM reader: the edited pixel survives")
        check((wc, hc) == (w, h) and rc == bytes(img), "BMP reader: 24-bit bottom-up decodes to the same top-down pixels")
        check(flip(w, h, flip(w, h, rb)) == rb and flip(w, h, rb) != rb, "flip is an involution and not the identity")
        argv = sys.argv
        sys.argv = ["frame_diff.py", a_path, a_path]
        check(main() == 0, "identical images -> exit 0")
        js = os.path.join(d, "r.json")
        sys.argv = ["frame_diff.py", a_path, b_path, "--json", js, "--grid", "64"]
        check(main() == 1, "one differing pixel -> exit 1")
        rep = json.load(open(js))
        check(rep["diff"] == 1 and rep["bbox"] == [x, y, x, y], "bbox is the edited pixel")
        check(rep["hist"] == [[200, 1]], "histogram: one pixel of max|delta| 200")
        check(len(rep["regions"]) == 1 and rep["regions"][0]["x"] == 256 and rep["regions"][0]["y"] == 64,
              "one 64x64 region (x=256,y=64) carries the difference")
        check(abs(rep["chan_mean"][0] - 16) < 1e-9 and abs(rep["chan_mean"][2] - 200) < 1e-9, "per-channel mean delta over differing pixels")
        sys.argv = ["frame_diff.py", b_path, bmp_path]
        check(main() == 0, "PPM vs its BMP twin -> identical")
        # the 5-bit truncation: an 8-bit image whose pixels differ from a 5-bit image only in the low 3 bits compares IDENTICAL
        # after --quantize-a 5; a pixel differing by a whole step stays, and is split sub-step vs object-level
        q5 = bytearray(black)
        full = bytearray(black)
        for i in range(0, 3 * 100, 3):
            q5[i:i + 3] = bytes((0x48, 0x80, 0xf8))
            full[i:i + 3] = bytes((0x4f, 0x87, 0xff))     # the same 5-bit values with low bits set
        q5[300:303] = bytes((0x48, 0x80, 0xf8))
        full[300:303] = bytes((0x58, 0x80, 0xf8))          # +16 = two 5-bit steps (sub-step class)
        q5[600:603] = bytes((0x00, 0x00, 0x00))
        full[600:603] = bytes((0x80, 0x00, 0x00))          # +128: object-level
        q_path, f_path = os.path.join(d, "q5.ppm"), os.path.join(d, "f8.ppm")
        for pth, img2 in ((q_path, q5), (f_path, full)):
            with open(pth, "wb") as f:
                f.write(b"P6\n%d %d\n255\n" % (w, h))
                f.write(bytes(img2))
        check(quantize(bytes(full), 5) == bytes(full).translate(bytes(v & 0xF8 for v in range(256))), "quantize keeps the top 5 bits (v & 0xF8)")
        js2 = os.path.join(d, "q.json")
        sys.argv = ["frame_diff.py", f_path, q_path, "--quantize-a", "5", "--json", js2]
        check(main() == 1, "8-bit A vs 5-bit B after --quantize-a 5: only the whole-step pixels remain -> exit 1")
        rep2 = json.load(open(js2))
        check(rep2["diff"] == 2 and rep2["substep_px"] == 1 and rep2["object_px"] == 1 and rep2["quantize_a"] == 5,
              "the 100 low-bit pixels vanish; 1 sub-step (+16) and 1 object-level (+128) pixel remain -> diff %d substep %d object %d" % (rep2["diff"], rep2["substep_px"], rep2["object_px"]))
        check(len(rep2["object_regions"]) == 1 and rep2["object_regions"][0]["x"] == 192 and rep2["object_regions"][0]["object_px"] == 1,
              "the object-level region is the 64x64 cell of pixel x=200 (x=192) -> %r" % rep2["object_regions"][:1])
        # the draw join: a dump with two draws, one localised over the object region, one full-screen
        dump = os.path.join(d, "dump.txt")
        with open(dump, "w") as f:
            f.write("== frame[1] vsync=1 draws=2 jobs=1\n")
            f.write("  draw[0] seq=1 sprite+tex n=96 fbp=0x000000/CT16 fbw=512 fbmsk=0x00000000 z=0x150000/CT32(zmsk) scis=(0,0)-(1024,1024)\n")
            f.write("           ALPHA 2021 fix=32 | TEST zte=1 | bbox (0.0,0.0,8388607)-(512.0,448.0,8388607) vh=0\n           HLE raw\n")
            f.write("  draw[1] seq=5 tristrip+tex n=60 fbp=0x070000/CT32 fbw=512 fbmsk=0x00000000 z=0x150000/CT32 scis=(0,0)-(511,447)\n")
            f.write("           ALPHA 2101 fix=0 | TEST zte=1 | bbox (190.5,-3.0,16857)-(230.0,40.0,305623) vh=1\n           HLE id=0a0b0c0d img=0123456789abcdef pc=0x0 ord=0 inst=1\n")
            f.write("  draw[2] seq=9 tristrip n=12 fbp=0x070000/CT32 fbw=512 fbmsk=0x00000000 z=0x150000/CT32 scis=(0,0)-(511,447)\n")
            f.write("           ALPHA 2101 fix=0 | TEST zte=1 | bbox (400.0,400.0,1)-(440.0,440.0,2) vh=2\n           HLE id=deadbeef img=0 pc=0x0 ord=0 inst=1\n")
        dr = read_draws(dump)
        check(len(dr) == 3 and dr[1]["bbox"] == (190.5, -3.0, 230.0, 40.0) and dr[1]["hle"] == "id=0a0b0c0d" and dr[0]["fbp"] == "0x000000",
              "read_draws parses idx/seq/prim/fbp/psm/bbox/HLE -> %r" % (dr[1],))
        check([x["idx"] for x in read_draws(dump, 0x70000)] == [1, 2], "--draws-fbp keeps the draws targeting one frame buffer")
        sys.argv = ["frame_diff.py", f_path, q_path, "--quantize-a", "5", "--json", js2, "--draws", dump, "--draws-fbp", "0x70000"]
        main()
        rep3 = json.load(open(js2))
        check(len(rep3["draw_joins"]) == 1 and rep3["draw_joins"][0]["draws"] == 1 and [x["idx"] for x in rep3["draw_joins"][0]["localised_draws"]] == [1],
              "the object region is joined with the one localised draw over it (the far draw and the display copy excluded) -> %r" % rep3["draw_joins"][0]["by_class"])
        sys.argv = argv
    finally:
        for n in os.listdir(d):
            os.unlink(os.path.join(d, n))
        os.rmdir(d)
    print("frame_diff selftest: " + ("PASS" if not fails else "FAIL (%d)" % len(fails)))
    return 0 if not fails else 1


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        sys.exit(selftest())
    sys.exit(main())
