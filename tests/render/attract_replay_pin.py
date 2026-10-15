#!/usr/bin/env python3
"""Render_AttractReplay_<dump> -- one PCSX2 GS dump of the coldboot_attract leg (the in-engine
"THREE YEARS AGO..." cutscene, or the title screen after it) replayed through NEW's full lowering
and measured against the dump's own embedded screenshot (the console).

The replay stream is regenerated from the dump at test time (`tools/gsdump/gsf1_export.py --frame 1`,
~0.3-2 s; the stream is a derived artefact, the dump is the frozen input) into the scratch dir, then
`test_title_frame_replay <stream> --dump <ppm> --scanout 70000` grabs the 0x70000 composition
(512x448, top-down). Two kinds of dump:

  cutscene: (1) the LETTERBOX rows -- the console's black bars, rows 0..66 and 382..447 of 448
            (two 4-vertex strips whose first two vertices arrive as A+D XYZ3, GS User's Manual
            7.3.1.8: queued without a drawing kick) -- must be black in the replay outside the
            caption box (an ASSERTION: red with a stream whose exporter dropped the XYZ3 vertices);
            (2) the six region means of tests/app/root_attract_cutscene_frames.py (SKY / HORIZON /
            WATER / CAPTION / RIGHT / LEFT) vs the console shot, bound `--tol` absolute per channel
            (the live gate's 12), and the CAPTION bright share >= `--caption-min`.
  title:    the three title regions (games/<title>/verify/baseline/title_screen/title_regions.py: LOGO yellow mean,
            ORB bright share, OCEAN bright share) of the replay vs the same measure of the dump's shot
            resized to 512x448 (`--logo-tol` per channel, `--share-tol` on the shares).

`--measure-only` records the numbers and passes (a MEASUREMENT pin: the per-region deltas are
printed and kept in the test log; the bound is the TARGET, stated). Without it the bounds are
asserted. The letterbox assertion is never relaxed by `--measure-only`.
Exit 77 (SKIP) when the dump, the exporter, the replay binary or a GL context is missing.
"""
import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app"))
import root_attract_cutscene_frames as racf  # noqa: E402  (REGIONS, read_ppm, region_means, caption_bright)

LETTERBOX_ROWS = list(range(0, 67)) + list(range(382, 448))   # the console's black rows (512x448 frame)
CAPTION_X = (racf.REGIONS["CAPTION"][0], racf.REGIONS["CAPTION"][2])
LOGO_BOX = (104, 110, 416, 236)
ORB_BOX = (196, 150, 316, 290)
OCEAN_BOX = (0, 318, 512, 388)


def pixels(img, box, flip=False):
    """Iterate (r, g, b) over a 512x448-coordinate box of a PPM of any size (scaled)."""
    w, h, px = img
    sx, sy = w / 512.0, h / 448.0
    x0, x1 = int(round(box[0] * sx)), int(round(box[2] * sx))
    y0, y1 = int(round(box[1] * sy)), int(round(box[3] * sy))
    for yy in range(y0, y1):
        y = (h - 1 - yy) if flip else yy
        row = px[y * w * 3 + x0 * 3:y * w * 3 + x1 * 3]
        for i in range(0, len(row), 3):
            yield row[i], row[i + 1], row[i + 2]


def title_regions(img):
    """title_regions.py's three measures: LOGO yellow mean (R>150, R>G>B, B<120) + count, ORB and
    OCEAN bright share (max channel >= 230)."""
    yr = yg = yb = yn = 0
    for r, g, b in pixels(img, LOGO_BOX):
        if r > 150 and r > g > b and b < 120:
            yr += r
            yg += g
            yb += b
            yn += 1
    out = {"LOGO": ((yr / yn, yg / yn, yb / yn) if yn else (0.0, 0.0, 0.0)), "LOGO_n": yn}
    for name, box in (("ORB", ORB_BOX), ("OCEAN", OCEAN_BOX)):
        n = bright = 0
        for r, g, b in pixels(img, box):
            n += 1
            if max(r, g, b) >= 230:
                bright += 1
        out[name] = bright / n if n else 0.0
    return out


def letterbox_rows(img, rows=LETTERBOX_ROWS, caption_x=CAPTION_X):
    """Per letterbox row: the max channel value outside the caption columns (a 512x448 top-down frame)."""
    w, h, px = img
    out = {}
    for y in rows:
        row = px[y * w * 3:(y + 1) * w * 3]
        m = 0
        for x in range(w):
            if caption_x[0] <= x < caption_x[1]:
                continue
            m = max(m, row[x * 3], row[x * 3 + 1], row[x * 3 + 2])
        out[y] = m
    return out


def judge_cutscene(new, ref, tol, caption_min, black_max, measure_only):
    """(ok, lines): letterbox rows black (asserted), six region means within tol, caption bright."""
    lines = []
    ok = True
    lb = letterbox_rows(new)
    lit = sorted(y for y, m in lb.items() if m > black_max)
    lines.append("letterbox rows %d..%d + %d..%d outside the caption: max channel %d; lit rows %d%s" % (
        LETTERBOX_ROWS[0], 66, 382, 447, max(lb.values()), len(lit), (" " + str(lit[:12])) if lit else ""))
    if lit:
        ok = False
        lines.append("FAIL: letterbox: %d rows not black (max channel > %d): the console's bars are missing" % (
            len(lit), black_max))
    m, bright = racf.measure(new, False)
    rm, rbright = ref
    total, worst, where = racf.deltas(m, rm)
    where = where or ("-", "-")   # identical means: no worst channel
    lines.append("regions: sum|delta|=%.1f max|delta|=%.1f at %s/%s caption bright=%.3f (console %.3f)" % (
        total, worst, where[0], where[1], bright, rbright))
    lines.append("  NEW     " + " ".join("%s=%s" % (n, racf.fmt(m[n])) for n in racf.REGIONS))
    lines.append("  console " + " ".join("%s=%s" % (n, racf.fmt(rm[n])) for n in racf.REGIONS))
    over = []
    for name in racf.REGIONS:
        for i in range(3):
            d = abs(m[name][i] - rm[name][i])
            if d > tol:
                over.append("%s %s |delta| %.1f > %d" % (name, racf.CHANNELS[i], d, tol))
    if bright < caption_min:
        over.append("CAPTION bright share %.3f < %.3f" % (bright, caption_min))
    for o in over:
        lines.append(("MEASURED (target): " if measure_only else "FAIL: ") + o)
    if over and not measure_only:
        ok = False
    return ok, lines


def judge_title(new, ref, logo_tol, share_tol, measure_only):
    lines = []
    a, b = title_regions(new), title_regions(ref)
    lines.append("title regions NEW:     LOGO yellow=(%.0f,%.0f,%.0f) n=%d | ORB bright=%.3f | OCEAN bright=%.3f" % (
        *a["LOGO"], a["LOGO_n"], a["ORB"], a["OCEAN"]))
    lines.append("title regions console: LOGO yellow=(%.0f,%.0f,%.0f) n=%d | ORB bright=%.3f | OCEAN bright=%.3f" % (
        *b["LOGO"], b["LOGO_n"], b["ORB"], b["OCEAN"]))
    over = []
    for i, c in enumerate("RGB"):
        d = abs(a["LOGO"][i] - b["LOGO"][i])
        if d > logo_tol:
            over.append("LOGO yellow %s |delta| %.1f > %d" % (c, d, logo_tol))
    for name in ("ORB", "OCEAN"):
        d = abs(a[name] - b[name])
        if d > share_tol:
            over.append("%s bright share |delta| %.3f > %.3f" % (name, d, share_tol))
    for o in over:
        lines.append(("MEASURED (target): " if measure_only else "FAIL: ") + o)
    return (measure_only or not over), lines


def selftest():
    r = True
    w, h = 512, 448
    black, grey = bytes([0, 0, 0] * w), bytes([100, 100, 100] * w)
    # 67 black rows, content, 66 black rows: no lit letterbox row; one grey row at 30 -> lit row 30.
    rows = [black] * 67 + [grey] * (448 - 67 - 66) + [black] * 66
    img = (w, h, b"".join(rows))
    r &= letterbox_rows(img) and max(letterbox_rows(img).values()) == 0
    rows[30] = grey
    lit = letterbox_rows((w, h, b"".join(rows)))
    r &= lit[30] == 100 and sum(1 for v in lit.values() if v) == 1
    # The caption columns are excluded: a lit pixel inside the CAPTION box at row 400 is not a lit row.
    rows[30] = black
    row400 = bytearray(black)
    row400[200 * 3] = 255
    rows[400] = bytes(row400)
    r &= letterbox_rows((w, h, b"".join(rows)))[400] == 0
    # Title measures: a frame of yellow (240,220,10) in the logo box and 255s in the orb box.
    px = bytearray([0, 0, 0] * w * h)
    for y in range(LOGO_BOX[1], LOGO_BOX[3]):
        for x in range(LOGO_BOX[0], LOGO_BOX[2]):
            px[(y * w + x) * 3:(y * w + x) * 3 + 3] = bytes([240, 220, 10])
    t = title_regions((w, h, bytes(px)))
    r &= t["LOGO"] == (240.0, 220.0, 10.0) and t["LOGO_n"] == (LOGO_BOX[2] - LOGO_BOX[0]) * (LOGO_BOX[3] - LOGO_BOX[1])
    r &= t["ORB"] > 0.3 and t["OCEAN"] == 0.0    # the logo box overlaps the orb box
    ok, lines = judge_title((w, h, bytes(px)), (w, h, bytes(px)), 8, 0.1, False)
    r &= ok
    ok, lines = judge_title((w, h, bytes(px)), (w, h, bytes([0, 0, 0] * w * h)), 8, 0.1, False)
    r &= (not ok) and any(l.startswith("FAIL") for l in lines)
    ok, lines = judge_title((w, h, bytes(px)), (w, h, bytes([0, 0, 0] * w * h)), 8, 0.1, True)
    r &= ok and any(l.startswith("MEASURED") for l in lines)
    # Cutscene: the letterbox assertion stays red under --measure-only.
    ref = racf.measure((w, h, b"".join([black] * 67 + [grey] * 315 + [black] * 66)), False)
    bad = (w, h, b"".join([grey] * 448))
    ok, lines = judge_cutscene(bad, ref, 12, 0.0, 8, True)
    r &= (not ok) and any("FAIL: letterbox" in l for l in lines)
    good = (w, h, b"".join([black] * 67 + [grey] * 315 + [black] * 66))
    ok, lines = judge_cutscene(good, ref, 12, 0.0, 8, False)
    r &= ok
    print("attract_replay_pin selftest: %s" % ("PASS" if r else "FAIL"))
    return 0 if r else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", help="the PCSX2 .gs.zst dump")
    ap.add_argument("--shot", help="the dump's embedded screenshot as P6 PPM (640x480 top-down)")
    ap.add_argument("--exporter", help="tools/gsdump/gsf1_export.py")
    ap.add_argument("--replay", help="the test_title_frame_replay binary")
    ap.add_argument("--scratch", help="where the regenerated stream and the grab go")
    ap.add_argument("--kind", choices=("cutscene", "title"), default="cutscene")
    ap.add_argument("--frame", type=int, default=1)
    ap.add_argument("--tol", type=int, default=12, help="absolute per-channel bound on the six region means")
    ap.add_argument("--caption-min", type=float, default=0.02)
    ap.add_argument("--black-max", type=int, default=8, help="a letterbox row is black when every channel <= this")
    ap.add_argument("--logo-tol", type=int, default=8)
    ap.add_argument("--share-tol", type=float, default=0.15,
                    help="bound on the ORB/OCEAN bright-share deltas (measured replay-vs-shot 0.02 / 0.10: the CT32 composition vs the CT16 CRTC output)")
    ap.add_argument("--measure-only", action="store_true", help="print the region deltas; pass (the bound is the target)")
    ap.add_argument("--stream", help="an existing GSF1 stream (skips the export)")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not (args.dump and args.shot and args.replay and args.scratch and (args.exporter or args.stream)):
        ap.error("--dump, --shot, --replay, --scratch and --exporter (or --stream) are required")
    for p, what in ((args.dump, "dump"), (args.shot, "shot"), (args.replay, "replay binary")):
        if not os.path.isfile(p):
            print("SKIP: %s absent: %s" % (what, p))
            return 77
    os.makedirs(args.scratch, exist_ok=True)
    name = os.path.basename(args.dump).split(".")[0]
    stream = args.stream or os.path.join(args.scratch, "%s_f%d.gsf1" % (name, args.frame))
    if not args.stream:
        if not os.path.isfile(args.exporter):
            print("SKIP: exporter absent: %s" % args.exporter)
            return 77
        r = subprocess.run([sys.executable, "-u", args.exporter, args.dump, "--frame", str(args.frame), "--out", stream],
                           capture_output=True, text=True, timeout=90)
        print(r.stdout.strip())
        if r.returncode != 0 or not os.path.isfile(stream):
            print("FAIL: export rc=%d %s" % (r.returncode, r.stderr[-400:]))
            return 1
    ppm = os.path.join(args.scratch, "%s_f%d_0x70000.ppm" % (name, args.frame))
    if os.path.exists(ppm):
        os.remove(ppm)
    r = subprocess.run([args.replay, stream, "--dump", ppm, "--scanout", "70000"], capture_output=True, text=True, timeout=100)
    for ln in r.stdout.splitlines():
        if ln.startswith(("replayed", "test_title_frame_replay", "GlDevice")):
            print(ln)
    if r.returncode == 77:
        print("SKIP: replay binary skipped (no GL context or stream)")
        return 77
    new = racf.read_ppm(ppm) if os.path.isfile(ppm) else None
    if not new or (new[0], new[1]) != (512, 448):
        print("FAIL: no 512x448 grab from the replay (rc=%d)" % r.returncode)
        return 1
    ref = racf.read_ppm(args.shot)
    if not ref:
        print("SKIP: shot unreadable: %s" % args.shot)
        return 77
    if args.kind == "cutscene":
        ok, lines = judge_cutscene(new, racf.measure(ref, False), args.tol, args.caption_min, args.black_max, args.measure_only)
    else:
        ok, lines = judge_title(new, ref, args.logo_tol, args.share_tol, args.measure_only)
    for ln in lines:
        print(ln)
    print("Render_AttractReplay_%s: %s%s" % (name, "PASS" if ok else "FAIL", " (measurement)" if args.measure_only else ""))
    try:
        os.remove(ppm)
    except OSError:
        pass
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
