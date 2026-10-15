#!/usr/bin/env python3
"""replay_diff_partition.py -- partition the pixel difference between a NEW frame and the console's screenshot by
the draw that last wrote each pixel in NEW's replay of the same GS dump frame.

Inputs
  --frames DIR     the replay harness's progressive frames: DIR/k<idx>.ppm = `test_title_frame_replay <gsf1> --only 0-<idx>
                   --scanout <fbp>` for every draw index (or draw-group end) idx, ascending, plus DIR/groups.tsv
                   (end_idx, first_idx, draws, kicks, key, bbox, rgba -- one row per frame). The last-writer map is
                   the fold: a pixel's writer is the largest idx whose frame changed it (a pixel no frame touched is
                   writer -1 = the target's initial content).
  --new PPM        the NEW frame to compare (512x448 P6; --new-flip when stored bottom-up, e.g. --dump-frames grabs).
  --ref PPM        the console's screenshot (the dump's embedded 640x480 image, top-down); resized to the NEW size with
                   a bilinear filter before comparing (the dumps' CRTC output is the 512x448 frame scaled 5/4 x 15/14).
  --quant5         compare both sides after `v & 0xF8` (the PSMCT16 display drops the low 3 bits, GS User's Manual
                   2.3.1; NEW's displayed frame is CT16-truncated).  --substep N  the SUB-STEP bound (default 16).
  --examples N     example pixels per class (default 3).  --json OUT  the structured report.

The report: per writer draw (and per class = the draw's census key without addresses) the differing pixel count split
SUB-STEP (max|delta| <= substep) / OBJECT (> substep), its share of the differing pixels and of the draw's own coverage,
and example pixels (x, y, ref RGB, new RGB). `--selftest` runs the synthetic checks.
Needs numpy (the fold over a few hundred frames); exits 77 without it.
"""
import json
import os
import struct
import sys

try:
    import numpy as np
except ImportError:  # pragma: no cover
    np = None


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"P6":
        raise ValueError("%s: not a P6 PPM" % path)
    fields = []
    pos = 2
    while len(fields) < 3:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1
    w, h, _ = fields
    arr = np.frombuffer(data, dtype=np.uint8, count=w * h * 3, offset=pos).reshape(h, w, 3)
    return arr


def resize_bilinear(img, w, h):
    """Separable bilinear resample (pixel-centre aligned), the way the region tools resize the screenshot."""
    sh, sw = img.shape[:2]
    def axis(n_src, n_dst):
        x = (np.arange(n_dst) + 0.5) * n_src / n_dst - 0.5
        x = np.clip(x, 0, n_src - 1)
        i0 = np.floor(x).astype(int)
        i1 = np.minimum(i0 + 1, n_src - 1)
        f = (x - i0)[:, None]
        return i0, i1, f
    y0, y1, fy = axis(sh, h)
    x0, x1, fx = axis(sw, w)
    a = img.astype(np.float32)
    rows = a[y0] * (1 - fy[:, :, None]) + a[y1] * fy[:, :, None]
    cols = rows[:, x0] * (1 - fx.T[:, :, None]) + rows[:, x1] * fx.T[:, :, None]
    return np.clip(np.rint(cols), 0, 255).astype(np.uint8)


def read_groups(path):
    rows = []
    with open(path) as f:
        header = f.readline().rstrip("\n").split("\t")
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) < len(header):
                continue
            d = dict(zip(header, parts))
            d["end_idx"] = int(d["end_idx"])
            d["first_idx"] = int(d["first_idx"])
            rows.append(d)
    rows.sort(key=lambda r: r["end_idx"])
    return rows


def class_of(key):
    """The address-free class of a census key: kind, blend, test, texture format/size/function, zmsk."""
    toks = key.split()
    keep = []
    for t in toks:
        if t.startswith(("FRAME=", "ZBUF=", "ctx", "PABE=", "COLCLAMP=")):
            if t.startswith("ZBUF=") and "zmsk" not in t:
                continue
            if t.startswith("FRAME="):
                keep.append("FRAME/" + t.split("/")[1] if "/" in t else t)
            continue
        if t.startswith("TEX0="):
            keep.append("TEX0/" + t.split("/")[1] if "/" in t else t)
            continue
        if t.startswith("cbp="):
            continue
        keep.append(t)
    return " ".join(keep)


def fold_last_writer(frames_dir, groups):
    """Returns (writer map int32 HxW, the final frame HxWx3, per-idx coverage counts)."""
    writer = None
    prev = None
    coverage = {}
    for g in groups:
        idx = g["end_idx"]
        p = os.path.join(frames_dir, "k%d.ppm" % idx)
        if not os.path.exists(p):
            continue
        cur = read_ppm(p)
        if writer is None:
            writer = np.full(cur.shape[:2], -1, dtype=np.int32)
            prev = np.zeros_like(cur)
        if cur.shape != prev.shape:
            # the grab of a prefix in which the scanout target was not drawn yet comes back at the
            # harness's default size: it holds no pixel of the target -> no writer
            coverage[idx] = 0
            continue
        changed = np.any(cur != prev, axis=2)
        writer[changed] = idx
        coverage[idx] = int(changed.sum())
        prev = cur
    return writer, prev, coverage


def partition(writer, new, ref, groups, substep, quant5, examples):
    if quant5:
        new = new & 0xF8
        ref = ref & 0xF8
    d = np.abs(new.astype(np.int16) - ref.astype(np.int16)).max(axis=2)
    differ = d > 0
    sub = differ & (d <= substep)
    obj = d > substep
    total = int(differ.sum())
    by_idx = {}
    gmap = {g["end_idx"]: g for g in groups}
    idxs = np.unique(writer)
    for idx in idxs.tolist():
        m = writer == idx
        n_cov = int(m.sum())
        n_dif = int((m & differ).sum())
        n_sub = int((m & sub).sum())
        n_obj = int((m & obj).sum())
        ex = []
        if n_obj or n_dif:
            ys, xs = np.nonzero(m & (obj if n_obj else differ))
            step = max(1, len(ys) // max(examples, 1))
            for j in range(0, len(ys), step)[:examples]:
                y, x = int(ys[j]), int(xs[j])
                ex.append(dict(x=x, y=y, ref=[int(v) for v in ref[y, x]], new=[int(v) for v in new[y, x]], d=int(d[y, x])))
        g = gmap.get(idx)
        by_idx[idx] = dict(idx=idx, coverage=n_cov, differing=n_dif, substep=n_sub, object=n_obj, examples=ex,
                           key=(g["key"] if g else "(initial target content: no draw)"),
                           cls=(class_of(g["key"]) if g else "no-draw"), first_idx=(g["first_idx"] if g else -1))
    by_cls = {}
    for r in by_idx.values():
        c = by_cls.setdefault(r["cls"], dict(cls=r["cls"], draws=[], coverage=0, differing=0, substep=0, object=0, examples=[]))
        c["draws"].append(r["idx"])
        for k in ("coverage", "differing", "substep", "object"):
            c[k] += r[k]
        if len(c["examples"]) < examples:
            c["examples"] += r["examples"][:examples - len(c["examples"])]
    return dict(total_px=int(d.size), differing=total, substep=int(sub.sum()), object=int(obj.sum()),
                mean_abs_delta=float(d[differ].mean()) if total else 0.0, by_idx=by_idx, by_cls=by_cls)


def print_report(rep, label, top=12):
    T = rep["total_px"]
    print("== %s: differing %d of %d px (%.1f%%); SUB-STEP %d (%.1f%% of differing) OBJECT %d (%.1f%% of frame); mean |d| %.1f"
          % (label, rep["differing"], T, 100.0 * rep["differing"] / T, rep["substep"],
             100.0 * rep["substep"] / max(rep["differing"], 1), rep["object"], 100.0 * rep["object"] / T, rep["mean_abs_delta"]))
    rows = sorted(rep["by_cls"].values(), key=lambda c: -c["object"])
    print("  %-7s %-7s %-7s %-7s %-7s %s" % ("object", "substep", "differ", "cover", "obj/cov", "class (draw idxs)"))
    for c in rows[:top]:
        idxs = c["draws"]
        s = ",".join(str(i) for i in idxs[:6]) + (",…(%d)" % len(idxs) if len(idxs) > 6 else "")
        print("  %7d %7d %7d %7d %6.1f%%  %s [%s]" % (c["object"], c["substep"], c["differing"], c["coverage"],
                                                     100.0 * c["object"] / max(c["coverage"], 1), c["cls"][:150], s))
        for e in c["examples"][:3]:
            print("      ex (%d,%d) ref=%s new=%s |d|=%d" % (e["x"], e["y"], tuple(e["ref"]), tuple(e["new"]), e["d"]))


def selftest():
    import tempfile
    fails = 0
    def check(cond, what):
        nonlocal fails
        print("  %s  %s" % ("PASS" if cond else "FAIL", what))
        if not cond:
            fails += 1
    if np is None:
        print("numpy missing -> SKIP")
        return 77
    tmp = tempfile.mkdtemp()
    def write(path, arr):
        with open(path, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (arr.shape[1], arr.shape[0]))
            f.write(arr.tobytes())
    h, w = 8, 16
    f0 = np.zeros((h, w, 3), np.uint8)
    f0[:, :8] = (10, 20, 30)                   # draw 0 writes the left half
    f1 = f0.copy(); f1[2:6, 4:12] = (200, 100, 50)   # draw 1 writes a box over the middle
    f2 = f1.copy()                             # draw 2 writes nothing visible
    write(os.path.join(tmp, "k0.ppm"), f0)
    write(os.path.join(tmp, "k1.ppm"), f1)
    write(os.path.join(tmp, "k2.ppm"), f2)
    with open(os.path.join(tmp, "groups.tsv"), "w") as f:
        f.write("end_idx\tfirst_idx\tdraws\tkicks\tkey\tbbox\trgba\n")
        f.write("0\t0\t1\t4\tsprite+tex ctx2 FRAME=0x70000/CT32 fbmsk=0x0 ZBUF=0x150000/CT32 zmsk=1 TEX0=0x230000/CT32 1024x1024 tfx=0\t0,0,8,8\t80808000:4\n")
        f.write("1\t1\t1\t6\ttristrip+iip+tex+abe ctx1 ALPHA=0201 fix=0x00 FRAME=0x70000/CT32 fbmsk=0xff000000 ZBUF=0x150000/CT32 zmsk=1 TEX0=0x3b0000/T8 128x128 tfx=0 cbp=0x3b5400\t4,2,12,6\tffffff7f:6\n")
        f.write("2\t2\t1\t2\tsprite ctx2 FRAME=0x70000/CT32 fbmsk=0x0 ZBUF=0x150000/CT32 zmsk=1\t0,0,0,0\t0:2\n")
    groups = read_groups(os.path.join(tmp, "groups.tsv"))
    writer, final, cov = fold_last_writer(tmp, groups)
    check(cov == {0: 64, 1: 32, 2: 0}, "coverage per draw from the fold: %s" % cov)
    check(int((writer == -1).sum()) == 128 - 64 - 16 and writer[0, 0] == 0 and writer[3, 6] == 1 and writer[0, 12] == -1,
          "last writer: left half -> 0, box -> 1, right half untouched -> -1")
    ref = final.copy()
    ref[3, 6] = (0, 0, 0)        # the box's pixel differs by 200 (object)
    ref[0, 0] = (10, 20, 31)     # a left-half pixel differs by 1 (sub-step; same 5-bit value)
    ref[0, 12] = (0, 0, 7)       # an untouched pixel differs by 7 (sub-step, writer -1)
    rep = partition(writer, final, ref, groups, 16, False, 3)
    check(rep["differing"] == 3 and rep["object"] == 1 and rep["substep"] == 2, "3 differing: 1 object + 2 sub-step")
    c1 = rep["by_idx"][1]
    check(c1["object"] == 1 and c1["examples"][0]["x"] == 6 and c1["examples"][0]["ref"] == [0, 0, 0] and c1["examples"][0]["new"] == [200, 100, 50],
          "the object pixel is the box draw's, example carries ref/new RGB")
    check(rep["by_idx"][-1]["substep"] == 1 and rep["by_idx"][-1]["cls"] == "no-draw", "the untouched pixel is booked to writer -1")
    check(class_of(groups[1]["key"]) == "tristrip+iip+tex+abe ALPHA=0201 fix=0x00 FRAME/CT32 fbmsk=0xff000000 zmsk=1 TEX0/T8 128x128 tfx=0",
          "class key drops addresses/ctx, keeps blend/test/format: %s" % class_of(groups[1]["key"]))
    rq = partition(writer, final, ref, groups, 16, True, 3)
    check(rq["differing"] == 1 and rq["object"] == 1, "--quant5 drops the 1-step and 7-step differences (same 5-bit values)")
    big = resize_bilinear(np.tile(np.array([[[0, 0, 0]], [[80, 80, 80]]], np.uint8), (1, 2, 1)), 2, 3)
    check(big.shape == (3, 2, 3) and big[1, 0, 0] == 40, "bilinear resize interpolates the middle row (0,80 -> 40): %s" % big[:, 0, 0].tolist())
    print("replay_diff_partition selftest: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


def main(argv):
    if "--selftest" in argv:
        return selftest()
    if np is None:
        print("replay_diff_partition: numpy missing -> SKIP (77)")
        return 77
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--frames", required=True)
    ap.add_argument("--new", required=True)
    ap.add_argument("--ref", required=True)
    ap.add_argument("--new-flip", action="store_true")
    ap.add_argument("--ref-flip", action="store_true")
    ap.add_argument("--quant5", action="store_true")
    ap.add_argument("--substep", type=int, default=16)
    ap.add_argument("--examples", type=int, default=3)
    ap.add_argument("--top", type=int, default=12)
    ap.add_argument("--label", default="")
    ap.add_argument("--json")
    ap.add_argument("--writer-ppm", help="write the last-writer map as a false-colour PPM")
    a = ap.parse_args(argv[1:])
    groups = read_groups(os.path.join(a.frames, "groups.tsv"))
    writer, final, cov = fold_last_writer(a.frames, groups)
    new = read_ppm(a.new)
    if a.new_flip:
        new = new[::-1]
    ref = read_ppm(a.ref)
    if a.ref_flip:
        ref = ref[::-1]
    if ref.shape != new.shape:
        ref = resize_bilinear(ref, new.shape[1], new.shape[0])
    if writer.shape != new.shape[:2]:
        print("writer map %s vs new frame %s: size mismatch" % (writer.shape, new.shape[:2]))
        return 2
    rep = partition(writer, new, ref, groups, a.substep, a.quant5, a.examples)
    rep["frames_dir"] = a.frames
    rep["new"] = a.new
    rep["ref"] = a.ref
    rep["quant5"] = a.quant5
    rep["coverage_per_idx"] = cov
    label = a.label or ("%s vs %s%s" % (os.path.basename(a.new), os.path.basename(a.ref), " (5-bit)" if a.quant5 else ""))
    print("frames: %d progressive frames folded; writer -1 (never written) %d px; final frame vs --new identical pixels %d/%d"
          % (len(cov), int((writer == -1).sum()), int(np.all(final == new, axis=2).sum()), writer.size))
    print_report(rep, label, a.top)
    if a.writer_ppm:
        pal = ((np.arange(max(int(writer.max()) + 2, 1))[:, None] * np.array([97, 57, 193])) % 256).astype(np.uint8)
        img = pal[writer + 1]
        with open(a.writer_ppm, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (img.shape[1], img.shape[0]))
            f.write(img.tobytes())
    if a.json:
        with open(a.json, "w") as f:
            json.dump(rep, f, indent=1, default=lambda o: o.tolist() if hasattr(o, "tolist") else str(o))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
