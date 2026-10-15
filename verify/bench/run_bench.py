#!/usr/bin/env python3
"""run_bench.py -- the Release ms/frame baseline per leg (the performance round's starting table).

  run_bench.py --binary <nativeee Release copy> --out <dir> [--baseline <legs dir>] [--legs a,b]
               [--cap 2000] [--repo <OLD root: anchors + game_files>] [--tsv BASELINE.tsv] [--note TEXT]
               [--origin build-release/app/host/nativeee] [--perf-targets a,b]
  run_bench.py --compare A.tsv B.tsv [--threshold 1.10]

What is measured. Every leg of the frozen battery (resolved through verify/leg_spec.py's BASELINE_DIR
and verify/battery.py's leg reader -- never a literal path) is run ONCE through verify/run_leg.py
`--tree new` exactly as the regression gate runs it: `--profile deterministic --headless --frames N`
from the leg's anchor (coldboot or its savestate), N = the leg's frozen frame count capped at --cap
(default 2000: the 6000-frame coldboot_attract leg is cut at 2000 so the table costs minutes, not an
hour; the cap is recorded per row). The deterministic headless profile is the comparable, repeatable
measurement. The INSTRUMENT, read from the tree and from every row's run.meta: the GL 4.1 backend on a
HIDDEN window (`Config.video.backend` defaults to Gl and the deterministic profile does not change it;
`--headless` only hides the window -- `VideoStack::CreateContext(.., headless, ..)` logs "video: GL ..
hidden"; run.meta `video=gl`, `gl_renderer=<GPU>`), swap interval 0 (no vsync wait), no wall pacing
(`engine.pacing=freerun`), `engine.mtvu=off`, the GS consumer inline on the EE thread (`gs_queue=
mode=inline`), scale 1, and the harness lanes on: a per-present display grab (GL readback) hashed into
frames.fhash, the audio lane, the mem lane (RAM hashed every 60 frames), drawir/gsops hashing. So
`ms_frame` = EE recompiled code + IOP/VU work + GL draw submission + per-present readback + harness
hashing per present, on one machine and one GPU. It is NOT a software rasterizer and it is NOT play
mode's presented frame time: play mode keeps the GL backend but shows the window, paces to 60 Hz,
runs the GUI, defaults `engine.mtvu` on and does no readback/hashing -- the two are different
instruments, neither is a bound on the other without measuring both. (OLD's scripts/run_bench.sh
measured play mode: PS2_NOVSYNC=1 + the fast-path env, a GL window; its per-frame times came from the
host's bench_report.json.) The NEW binary parses `--bench-report` but no app code arms core/diag
Bench yet, so no per-frame report exists; the columns below come from run.meta and from sampling the
present-progress file. The header records the renderer(s) seen in run.meta (`renderer:` line) so the
GPU behind the numbers is self-evident.

Performance-target rows (`PERF-TARGET <class>` in the note) are NOT named here -- verify/ carries no
title data: a leg is a target when its LEG.tsv `notes` (read through verify/leg_spec.py) contains
`perf-target[:<class>]`, or when it is listed in `--perf-targets leg[:class],...` (the stage that
writes the table names them; the class defaults to `slowspot`).

Columns:

  frames_req     the --frames value passed (frozen count, capped)
  frames         presents counted by run.meta (<= frames_req: present-skips, FMV coalescing)
  proc_wall_s    the whole process (restore + run + shutdown), measured here (1 s granularity: run_bounded's poll)
  run_wall_ms    run.meta wall_ms (arming -> finalize; includes the savestate restore)
  ms_frame       (last present time - first present time) / (presents between) from the sampler --
                 the steady-state per-present cost, the number to compare between builds
  ms_frame_total run_wall_ms / frames (includes the restore/boot)
  win_p50 / win_p95   per-1 s window ms/frame from the sampler (p50 / p95 over the windows): a
                 coarse spread, not per-frame percentiles (those need the bench report wired)
  load_1m        `uptime` 1-minute load average just before the leg (the machine should be idle)
  other_legs     count of foreign `run_leg` processes alive at launch (0 = this bench alone)
  status         OK | FAIL:<run_leg verdict>

--set KEY=VAL (repeatable) runs every leg with that one-line toggle (run_leg.py --set): the selected arm of
the performance round. The header records `set: ...` and every row's note carries `set KEY=VAL` so a selected
table can never be mistaken for the default arm; compare it against the default-arm table of the SAME binary.

--compare A B prints per-leg ratios B/A of ms_frame (and ms_frame_total) and marks rows whose ratio
exceeds --threshold as REGRESSION-CANDIDATE: a REPORT for the performance round, no verdict here.
"""
import argparse
import datetime
import hashlib
import os
import platform
import re
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
VERIFY = os.path.dirname(HERE)
sys.path.insert(0, VERIFY)
import battery  # noqa: E402
import leg_spec  # noqa: E402

COLUMNS = ("leg", "frames_req", "frames", "proc_wall_s", "run_wall_ms", "ms_frame", "ms_frame_total",
           "win_p50", "win_p95", "load_1m", "other_legs", "status", "note")
PERF_TARGET_RE = re.compile(r"\bperf-target(?::([A-Za-z0-9_-]+))?", re.IGNORECASE)
PERF_TARGET_DEFAULT_CLASS = "slowspot"
DEFAULT_ORIGIN = "build-release/app/host/nativeee"
PROFILE_HEADER = ("profile: --profile deterministic --headless (run_leg.py --tree new): GL 4.1 backend on a HIDDEN window "
                  "(video.backend=gl default, --headless hides the window only), swap interval 0 (no vsync wait), "
                  "engine.pacing=freerun, engine.mtvu=off, gs_queue inline, video.scale=1, harness lanes on "
                  "(per-present GL readback + frame hash, audio, mem every 60 frames, drawir/gsops); NOT a soft rasterizer")


def renderer_line(rows):
    """`renderer: <video>/<gl_renderer> (n legs)[, ...]` from the rows' run.meta -- the GPU behind the numbers."""
    seen = {}
    for r in rows:
        k = r.get("renderer", "?")
        seen[k] = seen.get(k, 0) + 1
    return "renderer: " + (", ".join("%s (%d legs)" % (k, n) for k, n in sorted(seen.items())) or "none")


def parse_perf_targets(text):
    """`--perf-targets leg[:class],...` -> {leg: class}. No leg name is hard-coded in this file."""
    out = {}
    for item in (text or "").split(","):
        item = item.strip()
        if not item:
            continue
        leg, _, cls = item.partition(":")
        out[leg] = cls or PERF_TARGET_DEFAULT_CLASS
    return out


def perf_target_class(baseline, leg, explicit):
    """The leg's performance-target class or None: the --perf-targets list first, else a
    `perf-target[:<class>]` word in the leg's LEG.tsv `notes` read through verify/leg_spec.py (the leg
    reader; legs without a LEG.tsv carry no flag)."""
    if leg in explicit:
        return explicit[leg]
    spec = leg_spec.read_leg(baseline, leg)
    if spec is None:
        return None
    m = PERF_TARGET_RE.search(spec.get("notes", "") or "")
    if not m:
        return None
    return m.group(1) or PERF_TARGET_DEFAULT_CLASS


def resolve_baseline(explicit):
    """The legs directory: --baseline, else leg_spec.BASELINE_DIR (the title's games/<t>/verify/baseline once the
    move landed), else the pre-move verify/baseline. Resolved at run time, never a literal in a caller."""
    if explicit:
        return os.path.abspath(explicit)
    cand = getattr(leg_spec, "BASELINE_DIR", None)
    if cand and os.path.isdir(cand):
        return cand
    legacy = os.path.join(VERIFY, "baseline")
    if os.path.isdir(legacy):
        return legacy
    raise SystemExit("no legs directory: neither leg_spec.BASELINE_DIR nor verify/baseline exists")


def load_1m():
    try:
        return "%.2f" % os.getloadavg()[0]
    except OSError:
        return "-"


def other_run_legs():
    p = subprocess.run(["pgrep", "-fl", "run_leg"], stdout=subprocess.PIPE, text=True)
    mine = str(os.getpid())
    return sum(1 for l in p.stdout.splitlines() if l.strip() and not l.startswith(mine + " ")
               and "run_bench.py" not in l)


def binary_identity(binary):
    v = subprocess.run([binary, "--version"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=60)
    bid = next((l.strip() for l in v.stdout.splitlines() if "BUILD-ID" in l), "unknown")
    h = hashlib.md5()
    with open(binary, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return bid, h.hexdigest()


def machine_line():
    brand = ""
    try:
        brand = subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"], stdout=subprocess.PIPE, text=True).stdout.strip()
    except OSError:
        pass
    mem = ""
    try:
        mem = subprocess.run(["sysctl", "-n", "hw.memsize"], stdout=subprocess.PIPE, text=True).stdout.strip()
        mem = "%d GiB" % (int(mem) >> 30) if mem.isdigit() else mem
    except OSError:
        pass
    return "%s %s %s %s cores=%s" % (platform.system(), platform.release(), platform.machine(),
                                    (brand + " " + mem).strip(), os.cpu_count())


class Sampler(threading.Thread):
    """Polls the leg's frames.fhash line count every `period` seconds -> [(t, lines)]."""

    def __init__(self, path, period=0.02):
        super().__init__(daemon=True)
        self.path, self.period = path, period
        self.samples = []
        self.stop = threading.Event()

    def run(self):
        last = -1
        while not self.stop.is_set():
            n = count_lines(self.path)
            if n != last:
                self.samples.append((time.monotonic(), n))
                last = n
            time.sleep(self.period)
        # no final sample: the last CHANGE is the last present; the process exit is noticed by
        # run_bounded.sh only at its next poll and must not stretch the span


def count_lines(path):
    try:
        with open(path, "rb") as f:
            return f.read().count(b"\n")
    except OSError:
        return 0


def stats_from_samples(samples):
    """(ms_frame over the present span, win_p50, win_p95) or (None, None, None)."""
    pts = [(t, n) for t, n in samples if n > 0]
    if len(pts) < 2:
        return None, None, None
    t0, n0 = pts[0]
    t1, n1 = pts[-1]
    if n1 <= n0:
        return None, None, None
    mean = (t1 - t0) * 1000.0 / (n1 - n0)
    wins = []
    wstart, wn = t0, n0
    for t, n in pts[1:]:
        if t - wstart >= 1.0:
            if n > wn:
                wins.append((t - wstart) * 1000.0 / (n - wn))
            wstart, wn = t, n
    if not wins:
        return mean, mean, mean
    wins.sort()
    p50 = wins[len(wins) // 2]
    p95 = wins[min(len(wins) - 1, int(round(0.95 * (len(wins) - 1))))]
    return mean, p50, p95


def run_one(binary, baseline, leg, out_dir, repo, cap, perf_targets, sets=None):
    frames_frozen = battery.leg_frames(baseline, leg)
    frames = min(frames_frozen, cap) if cap else frames_frozen
    anchor = battery.anchor_for(leg, repo, baseline)
    timeout, stall = battery.leg_bounds(baseline, leg, None)
    leg_out = os.path.join(out_dir, leg)
    cmd = battery.new_leg_cmd(binary, anchor, frames, leg_out, repo, timeout, sets, stall)
    load, others = load_1m(), other_run_legs()
    os.makedirs(leg_out, exist_ok=True)
    progress = os.path.join(leg_out, "frames.fhash")
    if os.path.exists(progress):
        os.remove(progress)
    sampler = Sampler(progress)
    t_start = time.monotonic()
    sampler.start()
    env = dict(os.environ)
    env.setdefault("RB_POLL_S", "1")   # run_bounded.sh notices the exit within 1 s (proc_wall_s granularity)
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
    sampler.stop.set()
    sampler.join(timeout=5)
    proc_wall = time.monotonic() - t_start
    with open(leg_out + ".run_bench.log", "w") as f:
        f.write(proc.stdout)
    meta = {}
    mp = os.path.join(leg_out, "run.meta")
    if os.path.isfile(mp):
        meta = battery.parse_meta(mp) if hasattr(battery, "parse_meta") else parse_meta(mp)
    presents = int(meta.get("frames", "0") or 0)
    run_wall = int(meta.get("wall_ms", "0") or 0)
    ms_frame, p50, p95 = stats_from_samples(sampler.samples)
    status = "OK" if proc.returncode == 0 else "FAIL:" + (next((l for l in proc.stdout.splitlines() if "run_leg: FAIL" in l), "exit %d" % proc.returncode)).replace("\t", " ")
    note = []
    if frames != frames_frozen:
        note.append("capped from %d" % frames_frozen)
    cls = perf_target_class(baseline, leg, perf_targets)
    if cls:
        note.append("PERF-TARGET " + cls)
    if sets:
        note.append("set " + " ".join(sets))   # a selected-arm row can never be read as the default arm
    renderer = "%s/%s" % (meta.get("video", "?"), meta.get("gl_renderer", "?"))
    fmt = lambda v: "-" if v is None else "%.2f" % v
    return {"leg": leg, "frames_req": str(frames), "frames": str(presents), "proc_wall_s": "%.1f" % proc_wall,
            "run_wall_ms": str(run_wall), "ms_frame": fmt(ms_frame),
            "ms_frame_total": fmt(run_wall / presents if presents else None), "win_p50": fmt(p50), "win_p95": fmt(p95),
            "load_1m": load, "other_legs": str(others), "status": status, "note": "; ".join(note) or "-",
            "renderer": renderer}


def parse_meta(path):
    meta = {}
    with open(path) as f:
        for line in f:
            if "=" in line:
                k, v = line.rstrip("\n").split("=", 1)
                meta[k] = v
    return meta


def write_tsv(path, header_lines, rows):
    with open(path, "w") as f:
        for h in header_lines:
            f.write("# " + h + "\n")
        f.write("\t".join(COLUMNS) + "\n")
        for r in rows:
            f.write("\t".join(r[c] for c in COLUMNS) + "\n")


def read_tsv(path):
    rows, header = {}, []
    with open(path) as f:
        cols = None
        for line in f:
            line = line.rstrip("\n")
            if not line.strip():
                continue
            if line.startswith("#"):
                header.append(line[1:].strip())
                continue
            parts = line.split("\t")
            if cols is None:
                cols = parts
                continue
            row = dict(zip(cols, parts))
            rows[row["leg"]] = row
    return header, rows


def compare(a_path, b_path, threshold):
    ha, a = read_tsv(a_path)
    hb, b = read_tsv(b_path)
    print("A: %s" % (ha[0] if ha else a_path))
    print("B: %s" % (hb[0] if hb else b_path))
    print("%-40s %10s %10s %7s %10s %10s %7s  %s" % ("leg", "A ms/f", "B ms/f", "ratio", "A total", "B total", "ratio", "flag"))
    worst = 0.0
    for leg in sorted(set(a) | set(b)):
        ra, rb = a.get(leg), b.get(leg)
        if not ra or not rb:
            print("%-40s %s" % (leg, "only in " + ("A" if ra else "B")))
            continue
        try:
            fa, fb = float(ra["ms_frame"]), float(rb["ms_frame"])
            ta, tb = float(ra["ms_frame_total"]), float(rb["ms_frame_total"])
        except ValueError:
            print("%-40s %s" % (leg, "no number (status A=%s B=%s)" % (ra["status"], rb["status"])))
            continue
        r1 = fb / fa if fa else float("nan")
        r2 = tb / ta if ta else float("nan")
        worst = max(worst, r1)
        flag = "REGRESSION-CANDIDATE" if r1 > threshold else ("faster" if r1 < 1.0 / threshold else "-")
        print("%-40s %10.2f %10.2f %7.3f %10.2f %10.2f %7.3f  %s" % (leg, fa, fb, r1, ta, tb, r2, flag))
    print("compare: worst ms_frame ratio %.3f (threshold %.2f) -- report only, no verdict" % (worst, threshold))
    return 0


def selftest():
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)

    s = [(0.0, 0), (1.0, 0), (2.0, 1), (2.5, 2), (3.0, 3), (4.0, 5), (5.0, 7), (6.0, 11)]
    mean, p50, p95 = stats_from_samples(s)
    check(abs(mean - 400.0) < 1e-9, "span mean: (6.0-2.0)s / (11-1) presents = 400 ms -> %r" % mean)
    check(p50 is not None and p95 is not None and p50 <= p95, "window percentiles ordered -> %r <= %r" % (p50, p95))
    check(stats_from_samples([(0.0, 0), (1.0, 0)]) == (None, None, None), "no presents -> no numbers")
    import tempfile
    d = tempfile.mkdtemp(prefix="run_bench.")
    rows = [{c: "x" for c in COLUMNS}]
    rows[0].update({"leg": "L", "ms_frame": "10.00", "ms_frame_total": "12.00", "status": "OK"})
    write_tsv(os.path.join(d, "a.tsv"), ["hdr A"], rows)
    rows[0].update({"ms_frame": "12.00", "ms_frame_total": "13.00"})
    write_tsv(os.path.join(d, "b.tsv"), ["hdr B"], rows)
    h, r = read_tsv(os.path.join(d, "b.tsv"))
    check(h == ["hdr B"] and r["L"]["ms_frame"] == "12.00", "tsv round trip")
    check(compare(os.path.join(d, "a.tsv"), os.path.join(d, "b.tsv"), 1.10) == 0, "compare runs (ratio 1.200 flagged above)")
    base = resolve_baseline(None)
    check(os.path.isdir(base), "a legs directory resolves through leg_spec/battery -> %s" % base)
    pt = parse_perf_targets("a,b:island, c:x")
    check(pt == {"a": "slowspot", "b": "island", "c": "x"}, "--perf-targets parses leg[:class] -> %r" % pt)
    check(perf_target_class(base, "a", pt) == "slowspot" and perf_target_class(base, "zz", pt) is None,
          "explicit list wins; an unlisted leg without a LEG.tsv is no target")
    legdir = os.path.join(d, "legX")
    os.makedirs(legdir)
    with open(os.path.join(legdir, leg_spec.FILE), "w") as f:
        f.write(leg_spec.format_spec(600, 60, 300, 60, "coldboot", "", "OLD 20 fps spot; perf-target:slowspot"))
    check(perf_target_class(d, "legX", {}) == "slowspot", "LEG.tsv notes `perf-target:slowspot` read through leg_spec")
    with open(os.path.join(legdir, leg_spec.FILE), "w") as f:
        f.write(leg_spec.format_spec(600, 60, 300, 60, "coldboot", "", "no flag here"))
    check(perf_target_class(d, "legX", {}) is None, "LEG.tsv notes without the word -> no target")
    rl = renderer_line([{"renderer": "gl/Apple M4"}, {"renderer": "gl/Apple M4"}, {"renderer": "stub/none"}])
    check(rl == "renderer: gl/Apple M4 (2 legs), stub/none (1 legs)", "renderer header line -> %r" % rl)
    check("NOT a soft rasterizer" in PROFILE_HEADER and "HIDDEN window" in PROFILE_HEADER, "profile header names the GL hidden-window instrument")
    cmd = battery.new_leg_cmd("bin", "coldboot", 10, d, "repo", 60, ["hle.graphics_sky=on", "hle.graphics_frame=on"], None)
    check(cmd[-4:] == ["--set", "hle.graphics_sky=on", "--set", "hle.graphics_frame=on"],
          "--set pass-through reaches run_leg.py argv in order (the selected arm)")
    check("--set" not in battery.new_leg_cmd("bin", "coldboot", 10, d, "repo", 60, None, None),
          "no --set without a selection (the model arm)")
    import shutil
    shutil.rmtree(d, ignore_errors=True)
    print("run_bench selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary")
    ap.add_argument("--out")
    ap.add_argument("--baseline", help="legs directory (default: leg_spec.BASELINE_DIR, else verify/baseline)")
    ap.add_argument("--legs", help="comma list (default: every leg of the battery)")
    ap.add_argument("--cap", type=int, default=2000, help="frame cap per leg (0 = the frozen count)")
    ap.add_argument("--repo", default=battery.DEFAULT_REPO)
    ap.add_argument("--tsv", help="write the table here (default <out>/BASELINE.tsv)")
    ap.add_argument("--note", default="", help="free text for the header (e.g. the stage id)")
    ap.add_argument("--origin", default=DEFAULT_ORIGIN,
                    help="repo-relative origin of the measured binary, recorded in the header (default %s)" % DEFAULT_ORIGIN)
    ap.add_argument("--perf-targets", default="",
                    help="leg[:class],... rows to flag PERF-TARGET (else a `perf-target[:class]` word in the leg's LEG.tsv notes)")
    ap.add_argument("--compare", nargs=2, metavar=("A.tsv", "B.tsv"))
    ap.add_argument("--threshold", type=float, default=1.10)
    ap.add_argument("--set", action="append", default=[], metavar="KEY=VAL",
                    help="Config override passed through run_leg.py to the binary for EVERY leg (repeatable; "
                         "the one-line toggles). Recorded in the header and each row's note: the model/selected "
                         "arms of the performance round are two tables of the same frozen binary")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    for kv in a.set:
        if "=" not in kv or kv.startswith("="):
            ap.error("--set expects KEY=VAL, got %r" % kv)
    if a.selftest:
        return selftest()
    if a.compare:
        return compare(a.compare[0], a.compare[1], a.threshold)
    if not a.binary or not a.out:
        ap.error("--binary and --out are required for a run")
    binary = os.path.abspath(a.binary)
    baseline = resolve_baseline(a.baseline)
    legs = a.legs.split(",") if a.legs else battery.baseline_legs(baseline)
    os.makedirs(a.out, exist_ok=True)
    bid, md5 = binary_identity(binary)
    header = ["verify/bench BASELINE %s  binary=%s (copy at %s)  %s  md5=%s" % (
                  datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"), a.origin, binary, bid, md5),
              "machine: " + machine_line(),
              PROFILE_HEADER,
              "legs: %s (dir %s); frame cap %d" % (",".join(legs), baseline, a.cap),
              "columns: see verify/bench/README.md; ms_frame = steady-state ms per present from the progress sampler; one leg at a time"]
    if a.note:
        header.append("note: " + a.note)
    if a.set:
        header.append("set: " + " ".join("--set " + kv for kv in a.set))
    rows = []
    tsv = a.tsv or os.path.join(a.out, "BASELINE.tsv")
    perf_targets = parse_perf_targets(a.perf_targets)
    for leg in legs:
        # re-resolve per leg: the legs directory may move between legs (the P8.36-D `git mv`); a stale path
        # must re-resolve, never fail the table half-way
        if not a.baseline and not os.path.isdir(baseline):
            baseline = resolve_baseline(None)
            print("[run_bench] legs directory moved; now %s" % baseline, flush=True)
        print("[run_bench] %s leg %s load=%s other_run_legs=%d" % (datetime.datetime.now().strftime("%H:%M:%S"), leg, load_1m(), other_run_legs()), flush=True)
        row = run_one(binary, baseline, leg, a.out, a.repo, a.cap, perf_targets, a.set or None)
        rows.append(row)
        print("[run_bench]   -> frames %s/%s ms_frame %s total %s p50 %s p95 %s wall %ss %s" % (
            row["frames"], row["frames_req"], row["ms_frame"], row["ms_frame_total"], row["win_p50"], row["win_p95"],
            row["proc_wall_s"], row["status"]), flush=True)
        write_tsv(tsv, header + [renderer_line(rows)], rows)   # partial table survives an interrupted run
    print("[run_bench] table: %s" % tsv)
    return 0 if all(r["status"] == "OK" for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
