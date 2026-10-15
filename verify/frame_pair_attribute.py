#!/usr/bin/env python3
"""frame_pair_attribute.py -- attribute a toggle pair's divergent frame keys from the two
legs' `--dump-frames` images (verify/run_leg.py --dump-frames; the binary writes the very
image frames.fhash hashed as <dir>/f<key>.ppm, top-down P6).

  frame_pair_attribute.py --model <dirA> --selected <dirB> [--keys 171,184,185 | --fhash]
                          [--window LO-HI] [--neighbours 2] [--out report.txt]

Per divergent key K it answers the three questions a live Graphics/Movie pair raises:
  1. WHERE the arms differ: differing pixel count, bounding box, the rows touched
     (`rows=a-b` -> edge rows vs whole frame), each arm's mean colour inside the box and
     whether either arm's box is a flat colour (`A_flat`/`B_flat`: one arm shows nothing).
  2. WHETHER it is a presentation-tick offset: B[K] is compared with A[K+d] for
     d in -N..N (and A[K] with B[K+d]); an exact (or near, mean abs diff < 1.0)
     neighbour match reports `tick_offset=d` -- the owned arm presents the model's
     frame d ticks earlier/later -- which is a cadence attribution, not a content one.
  3. WHETHER it is a content difference inside identical cadence: no neighbour matches.
The verdict line per key is one of:
  frame K: tick_offset=<d> (B[K]==A[K+d] exact|near)   -- cadence at a window edge
  frame K: one_arm_flat A|B rows=a-b                    -- one arm presents no picture there
  frame K: rows_only rows=a-b (h rows of H)             -- window-edge row difference
  frame K: content bbox=... pixels=n/N mean_abs=...     -- same cadence, different pixels
Nothing here edits run.meta or frames.fhash; keys default to the fhash lines that differ
when --fhash is given, else --keys. `--selftest` exercises every verdict on synthetic PPMs.
"""
import argparse
import os
import re
import sys
import tempfile


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    # P6 <w> <h> <max>\n<bytes>; comments (#) tolerated between tokens
    tokens = []
    pos = 0
    while len(tokens) < 4:
        m = re.compile(rb"\s*(#[^\n]*\n\s*)*(\S+)").match(data, pos)
        if not m:
            raise ValueError("bad PPM header: %s" % path)
        tokens.append(m.group(2))
        pos = m.end()
    if tokens[0] != b"P6":
        raise ValueError("not a P6 PPM: %s" % path)
    w, h, maxval = int(tokens[1]), int(tokens[2]), int(tokens[3])
    pos += 1  # single whitespace after maxval
    if maxval != 255:
        raise ValueError("unsupported maxval %d: %s" % (maxval, path))
    pixels = data[pos:pos + w * h * 3]
    if len(pixels) != w * h * 3:
        raise ValueError("truncated PPM: %s" % path)
    return w, h, pixels


def write_ppm(path, w, h, pixels):
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        f.write(pixels)


def frame_path(directory, key):
    return os.path.join(directory, "f%d.ppm" % key)


def compare(a, b):
    """(count of differing pixels, bbox (x0,y0,x1,y1) or None, rows set, mean abs diff over
    the frame, mean colour of a / b inside the bbox, a flat?, b flat?)."""
    (wa, ha, pa), (wb, hb, pb) = a, b
    if (wa, ha) != (wb, hb):
        return {"size_mismatch": ((wa, ha), (wb, hb))}
    diff = 0
    x0 = y0 = 1 << 30
    x1 = y1 = -1
    total_abs = 0
    rows = set()
    for y in range(ha):
        row_a = pa[y * wa * 3:(y + 1) * wa * 3]
        row_b = pb[y * wb * 3:(y + 1) * wb * 3]
        if row_a == row_b:
            continue
        rows.add(y)
        for x in range(wa):
            i = x * 3
            if row_a[i:i + 3] != row_b[i:i + 3]:
                diff += 1
                total_abs += abs(row_a[i] - row_b[i]) + abs(row_a[i + 1] - row_b[i + 1]) + abs(row_a[i + 2] - row_b[i + 2])
                x0, x1 = min(x0, x), max(x1, x)
                y0, y1 = min(y0, y), max(y1, y)
    result = {"pixels": diff, "total": wa * ha, "rows": rows, "height": ha}
    if diff == 0:
        result["mean_abs"] = 0.0
        return result
    result["bbox"] = (x0, y0, x1, y1)
    result["mean_abs"] = total_abs / (3.0 * diff)
    for name, p in (("A", pa), ("B", pb)):
        sums = [0, 0, 0]
        first = None
        flat = True
        n = 0
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                i = (y * wa + x) * 3
                px = p[i:i + 3]
                if first is None:
                    first = px
                elif flat and px != first:
                    flat = False
                sums[0] += px[0]; sums[1] += px[1]; sums[2] += px[2]
                n += 1
        result[name + "_mean"] = tuple(round(s / float(n), 1) for s in sums)
        result[name + "_flat"] = flat
    return result


def near(a, b, tolerance=1.0):
    r = compare(a, b)
    if "size_mismatch" in r:
        return False, None
    if r["pixels"] == 0:
        return True, "exact"
    if r["mean_abs"] < tolerance and r["pixels"] < r["total"] // 100:
        return True, "near"
    return False, None


def flat_frame(frame):
    w, h, p = frame
    return not w or not h or p == p[:3] * (w * h)


def rows_text(rows):
    if not rows:
        return "none"
    lo, hi = min(rows), max(rows)
    return "%d-%d" % (lo, hi) if hi > lo else "%d" % lo


def attribute_key(model_dir, selected_dir, key, neighbours, cache):
    def load(directory, k):
        path = frame_path(directory, k)
        if path not in cache:
            cache[path] = read_ppm(path) if os.path.isfile(path) else None
        return cache[path]
    a, b = load(model_dir, key), load(selected_dir, key)
    if a is None or b is None:
        return "frame %d: missing dump (%s)" % (key, "model" if a is None else "selected")
    r = compare(a, b)
    if "size_mismatch" in r:
        return "frame %d: size_mismatch model=%dx%d selected=%dx%d" % ((key,) + r["size_mismatch"][0] + r["size_mismatch"][1])
    if r["pixels"] == 0:
        return "frame %d: identical" % key
    # (2) a presentation-tick offset: the selected arm shows a neighbouring model frame
    # (first), or the model shows a neighbouring selected frame. A flat (uniform) frame
    # matches any flat frame and is no evidence: only non-flat matches count.
    offsets = [d for d in sorted(range(-neighbours, neighbours + 1), key=lambda v: (abs(v), v)) if d]
    for direction in ("selected", "model"):
        for d in offsets:
            if direction == "selected":
                other = load(model_dir, key + d)
                ok, how = near(other, b) if other is not None else (False, None)
                shown, reference = b, other
            else:
                other = load(selected_dir, key + d)
                ok, how = near(a, other) if other is not None else (False, None)
                shown, reference = a, other
            if ok and not flat_frame(shown) and not flat_frame(reference):
                if direction == "selected":
                    return ("frame %d: tick_offset=%+d (selected[%d]==model[%d] %s) rows=%s pixels=%d/%d"
                            % (key, d, key, key + d, how, rows_text(r["rows"]), r["pixels"], r["total"]))
                return ("frame %d: tick_offset=%+d (model[%d]==selected[%d] %s) rows=%s pixels=%d/%d"
                        % (key, -d, key, key + d, how, rows_text(r["rows"]), r["pixels"], r["total"]))
    x0, y0, x1, y1 = r["bbox"]
    detail = "bbox=%d,%d-%d,%d pixels=%d/%d mean_abs=%.2f model_mean=%s selected_mean=%s model_flat=%d selected_flat=%d" % (
        x0, y0, x1, y1, r["pixels"], r["total"], r["mean_abs"], r["A_mean"], r["B_mean"], r["A_flat"], r["B_flat"])
    if len(r["rows"]) * 2 < r["height"]:
        return "frame %d: rows_only rows=%s (%d rows of %d) %s" % (
            key, rows_text(r["rows"]), len(r["rows"]), r["height"], detail)
    if r["A_flat"] != r["B_flat"]:
        flat = "model" if r["A_flat"] else "selected"
        return "frame %d: one_arm_flat %s rows=%s %s" % (key, flat, rows_text(r["rows"]), detail)
    return "frame %d: content %s rows=%s" % (key, detail, rows_text(r["rows"]))


def fhash_keys(path):
    keys = {}
    with open(path) as f:
        for line in f:
            parts = line.strip().split(",")
            if len(parts) >= 2 and parts[0].isdigit():
                keys[int(parts[0])] = parts[1]
    return keys


def divergent_keys(model_leg, selected_leg):
    a = fhash_keys(os.path.join(model_leg, "frames.fhash"))
    b = fhash_keys(os.path.join(selected_leg, "frames.fhash"))
    return sorted(k for k in a if k in b and a[k] != b[k])


def selftest():
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        w, h = 16, 8
        black = bytes(w * h * 3)
        # textured pictures (a gradient): flat frames carry no cadence evidence by design
        pic1 = bytes(b for i in range(w * h) for b in (10 + i % 7, 20, 30))
        pic2 = bytes(b for i in range(w * h) for b in (40, 50 + i % 5, 60))
        # rows 0-1 differ only (edge rows)
        edge = bytearray(pic1); edge[0:w * 2 * 3] = bytes([99, 99, 99]) * (w * 2)
        model, selected = os.path.join(tmp, "a"), os.path.join(tmp, "b")
        os.makedirs(model); os.makedirs(selected)
        # keys: 170 identical; 171 selected shows pic1 while model still black -> tick_offset=+1
        write_ppm(frame_path(model, 170), w, h, black); write_ppm(frame_path(selected, 170), w, h, black)
        write_ppm(frame_path(model, 171), w, h, black); write_ppm(frame_path(selected, 171), w, h, pic1)
        write_ppm(frame_path(model, 172), w, h, pic1); write_ppm(frame_path(selected, 172), w, h, pic1)
        # 180: edge rows differ, no neighbour match
        write_ppm(frame_path(model, 180), w, h, pic1); write_ppm(frame_path(selected, 180), w, h, bytes(edge))
        # 184: model holds pic2, selected flat black
        write_ppm(frame_path(model, 184), w, h, pic2); write_ppm(frame_path(selected, 184), w, h, black)
        # 190: whole-frame content difference
        write_ppm(frame_path(model, 190), w, h, pic1); write_ppm(frame_path(selected, 190), w, h, pic2)
        cache = {}
        lines = [attribute_key(model, selected, k, 2, cache) for k in (170, 171, 180, 184, 190, 199)]
        ok &= lines[0] == "frame 170: identical"
        ok &= lines[1].startswith("frame 171: tick_offset=+1 (selected[171]==model[172] exact)")
        ok &= lines[2].startswith("frame 180: rows_only rows=0-1 (2 rows of 8)")
        # pic2 vs black: both boxes are flat -> whole-frame content with both flat flags set
        # the model holds the last picture, the selected arm presents a flat frame
        ok &= lines[3].startswith("frame 184: one_arm_flat selected rows=0-7") and "model_flat=0 selected_flat=1" in lines[3]
        ok &= "model_mean=(" in lines[2] and "selected_mean=(99.0, 99.0, 99.0)" in lines[2]
        ok &= lines[4].startswith("frame 190: content bbox=0,0-15,7 pixels=128/128") and "model_flat=0 selected_flat=0" in lines[4]
        ok &= lines[5] == "frame 199: missing dump (model)"
        # fhash-driven key discovery
        with open(os.path.join(model, "frames.fhash"), "w") as f:
            f.write("170,0x1\n171,0x2\n172,0x3\n")
        with open(os.path.join(selected, "frames.fhash"), "w") as f:
            f.write("170,0x1\n171,0x9\n172,0x3\n")
        ok &= divergent_keys(model, selected) == [171]
    print("frame_pair_attribute selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", help="model arm --dump-frames directory")
    ap.add_argument("--selected", help="selected arm --dump-frames directory")
    ap.add_argument("--keys", help="comma list of frame keys to attribute")
    ap.add_argument("--fhash", nargs=2, metavar=("MODEL_LEG", "SELECTED_LEG"),
                    help="derive the keys from the two legs' frames.fhash (every differing key)")
    ap.add_argument("--window", help="LO-HI: restrict to keys inside this range")
    ap.add_argument("--neighbours", type=int, default=2, help="tick offsets to test (default 2)")
    ap.add_argument("--out", help="also write the report here")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.model or not a.selected:
        ap.error("--model and --selected are required")
    keys = []
    if a.fhash:
        keys = divergent_keys(a.fhash[0], a.fhash[1])
    if a.keys:
        keys = sorted(set(keys) | {int(k) for k in a.keys.split(",") if k})
    if a.window:
        lo, hi = (int(v) for v in a.window.split("-"))
        keys = [k for k in keys if lo <= k <= hi]
    if not keys:
        print("frame_pair_attribute: no keys (give --keys or --fhash)")
        return 2
    cache = {}
    lines = [attribute_key(a.model, a.selected, k, a.neighbours, cache) for k in keys]
    report = "\n".join(lines) + "\n"
    sys.stdout.write(report)
    if a.out:
        with open(a.out, "w") as f:
            f.write(report)
    return 0


if __name__ == "__main__":
    sys.exit(main())
