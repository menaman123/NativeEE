#!/usr/bin/env python3
"""The s3 play-session workload as a bench row: replay `play_sessions/s3/session.padlog`
through run_leg.py (deterministic headless, cold boot, fresh mc_dir) and sample the
leg's frames.fhash line count every 20 ms; print ms/frame per 300-frame window plus the
run.meta counters the performance round reads.

Usage:
  run_s3.py --binary <nativeee> --padlog <session.padlog> --frames N --out <dir>
            [--window 300] [--game-files DIR] [--set k=v ...] [--timeout S] [--stall-s S]
            [--play]   # play profile wall-paced with a window: pace_late / underruns row

The deterministic row is the comparable ms/frame table (no pacing, no audio device); the
play row is wall-paced and only its run.meta counters (pace_late, pace_parks, audio_ring
underruns/dropped) are read.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import battery  # noqa: E402

COUNTERS = ("frames", "wall_ms", "finalized", "pace_late", "pace_parks", "pace_timeouts", "pace_late_fields", "pace_slept_ms",
            "audio_ring", "texres", "gfx_consumer", "drawir", "presents", "vu0_calls", "vu0_park_resumes",
            "sif_rpc_calls", "mtvu", "gs_queue", "domain", "footprint", "gsops_ops", "graphics_owned_output",
            "graphics_service", "tick_thread", "ee_cpu_total_ms", "park", "park_detail")


class Sampler(threading.Thread):
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


def count_lines(path):
    try:
        with open(path, "rb") as f:
            return f.read().count(b"\n")
    except OSError:
        return 0


def windows(samples, win):
    """[(first_frame, last_frame, ms_frame)] per `win` presents from the sampler's (t, lines)."""
    pts = [(t, n) for t, n in samples if n > 0]
    if len(pts) < 2:
        return []
    out = []
    # time at which the line count first reached >= k, by linear interpolation between samples
    def t_at(k):
        prev = pts[0]
        for t, n in pts[1:]:
            if n >= k:
                pt, pn = prev
                if n == pn:
                    return t
                return pt + (t - pt) * (k - pn) / float(n - pn)
            prev = (t, n)
        return None
    total = pts[-1][1]
    start = pts[0][1]
    k = ((start // win) + 1) * win
    while k + win <= total:
        t0, t1 = t_at(k), t_at(k + win)
        if t0 is None or t1 is None:
            break
        out.append((k, k + win, (t1 - t0) * 1000.0 / win))
        k += win
    return out


def load_1m():
    try:
        return "%.2f" % os.getloadavg()[0]
    except OSError:
        return "-"


def parse_meta(path):
    d = {}
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            if "=" in line:
                k, v = line.split("=", 1)
                d[k] = v
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--padlog", required=True)
    ap.add_argument("--frames", type=int, required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--window", type=int, default=300)
    ap.add_argument("--game-files", default=os.path.join(os.environ.get("PS2R_OLD_ROOT", ""), "game_files"))
    ap.add_argument("--repo", default=os.path.dirname(os.path.dirname(HERE)))
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--timeout", type=int, default=3000)
    ap.add_argument("--stall-s", type=int, default=600)
    ap.add_argument("--play", action="store_true", help="play profile, wall-paced, window shown")
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    leg_out = os.path.join(a.out, "leg")
    if os.path.isdir(leg_out):
        shutil.rmtree(leg_out)
    mc_dir = os.path.join(a.out, "mc")
    if os.path.isdir(mc_dir):
        shutil.rmtree(mc_dir)
    os.makedirs(mc_dir)
    sets = list(a.set)
    if a.play:
        # The play profile (wall-paced, window shown, audio device): run_leg.py knows only
        # the deterministic profile, so the binary is launched directly with the recorder
        # command's flags (docs/design/PLAY_SESSIONS.md) plus --inputs and --frames.
        os.makedirs(leg_out, exist_ok=True)
        cmd = [a.binary, "--profile", "default", "--harness-window", "--harness-out", leg_out,
               "--frames", str(a.frames), "--inputs", os.path.abspath(a.padlog), "--game-files", a.game_files,
               "--set", "paths.mc_dir=" + mc_dir, "--set", "paths.log_dir=" + os.path.join(a.out, "logs")]
        for kv in sets:
            cmd += ["--set", kv]
    else:
        cmd = battery.new_leg_cmd(a.binary, "coldboot", a.frames, leg_out, a.repo, a.timeout, sets, a.stall_s,
                                  os.path.abspath(a.padlog), mc_dir)
        cmd += ["--game-files", a.game_files]
    load = load_1m()
    others = subprocess.run(["pgrep", "-fl", "nativeee"], stdout=subprocess.PIPE, text=True).stdout
    others = [l for l in others.splitlines() if "run_s3.py" not in l and "pcsx2" not in l and "mcp" not in l]
    progress = os.path.join(leg_out, "frames.fhash")
    sampler = Sampler(progress)
    t_start = time.monotonic()
    print("cmd:", " ".join(cmd), flush=True)
    print("load_1m=%s other_game_procs=%d" % (load, len(others)), flush=True)
    sampler.start()
    env = dict(os.environ)
    env.setdefault("RB_POLL_S", "1")
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
    sampler.stop.set()
    sampler.join(timeout=5)
    wall = time.monotonic() - t_start
    with open(os.path.join(a.out, "run_leg.log"), "w") as f:
        f.write(proc.stdout)
    verdict = next((l for l in proc.stdout.splitlines() if "run_leg:" in l), "exit %d" % proc.returncode)
    print("run_leg exit=%d wall=%.1fs verdict=%s" % (proc.returncode, wall, verdict), flush=True)
    meta = {}
    mp = os.path.join(leg_out, "run.meta")
    if os.path.isfile(mp):
        meta = parse_meta(mp)
    rows = windows(sampler.samples, a.window)
    lines = ["# run_s3 %s binary=%s load_1m=%s other_game_procs=%d profile=%s sets=%s" % (
        time.strftime("%Y-%m-%d %H:%M:%S"), a.binary, load, len(others), "play" if a.play else "deterministic", " ".join(sets) or "-")]
    lines.append("window_first\twindow_last\tms_frame")
    for f0, f1, ms in rows:
        lines.append("%d\t%d\t%.2f" % (f0, f1, ms))
    pts = [(t, n) for t, n in sampler.samples if n > 0]
    if len(pts) >= 2 and pts[-1][1] > pts[0][1]:
        mean = (pts[-1][0] - pts[0][0]) * 1000.0 / (pts[-1][1] - pts[0][1])
        lines.append("# overall ms_frame=%.2f over presents %d..%d" % (mean, pts[0][1], pts[-1][1]))
    lines.append("# counters")
    for k in COUNTERS:
        if k in meta:
            lines.append("%s=%s" % (k, meta[k]))
    text = "\n".join(lines) + "\n"
    with open(os.path.join(a.out, "S3_WINDOWS.tsv"), "w") as f:
        f.write(text)
    sys.stdout.write(text)
    return 0 if proc.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
