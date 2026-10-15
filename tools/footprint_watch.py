#!/usr/bin/env python3
"""footprint_watch.py -- sample a running leg's physical footprint against its
frame progress.

Runs the given command (a NEW `nativeee` leg, typically through
`verify/run_bounded.sh`) and, every `--period` seconds, records one line

    <elapsed_s>\t<frames>\t<footprint_mb>\t<peak_mb>\t<resident_mb>

to `--out`, where `frames` is the line count of the leg's `frames.fhash`
(the vsync presents hashed so far) and the memory columns come from
`vmmap --summary <pid>` ("Physical footprint:" / "Physical footprint (peak):")
with a `ps -o rss=` fallback when vmmap is unavailable. Pass `--pid` instead of
a command to attach to a leg that is already running.

The sampler is a host instrument only: it never signals the leg and it never
reads guest state. It makes the by-hand method (`vmmap --summary` every few
seconds while a long leg runs) repeatable.

Usage:
  python3 -u tools/footprint_watch.py --out w.tsv --frames-file <leg>/frames.fhash \
      --period 5 -- verify/run_bounded.sh 600 leg.log -- build-release/app/host/nativeee ...
  python3 -u tools/footprint_watch.py --out w.tsv --frames-file ... --pid 12345
  python3 -u tools/footprint_watch.py --selftest
"""
import argparse
import os
import re
import subprocess
import sys
import time

_RE_FOOT = re.compile(r"Physical footprint:\s+([0-9.]+)([KMG]?)")
_RE_PEAK = re.compile(r"Physical footprint \(peak\):\s+([0-9.]+)([KMG]?)")


def to_mb(num, unit):
    """vmmap prints 12.3M / 1.5G / 900K; normalise to MiB (pure)."""
    v = float(num)
    if unit == "G":
        return v * 1024.0
    if unit == "K":
        return v / 1024.0
    if unit == "":
        return v / (1024.0 * 1024.0)
    return v


def parse_vmmap_summary(text):
    """(footprint_mb, peak_mb) from `vmmap --summary` output; None when absent (pure)."""
    foot = peak = None
    m = _RE_FOOT.search(text)
    if m:
        foot = to_mb(m.group(1), m.group(2))
    m = _RE_PEAK.search(text)
    if m:
        peak = to_mb(m.group(1), m.group(2))
    return foot, peak


def sample_memory(pid):
    """(footprint_mb, peak_mb, resident_mb) for `pid`; -1 for a column no tool gave."""
    foot = peak = rss = -1.0
    try:
        out = subprocess.run(["vmmap", "--summary", str(pid)], capture_output=True,
                             text=True, timeout=20).stdout
        f, p = parse_vmmap_summary(out)
        if f is not None:
            foot = f
        if p is not None:
            peak = p
    except (OSError, subprocess.SubprocessError):
        pass
    try:
        out = subprocess.run(["ps", "-o", "rss=", "-p", str(pid)], capture_output=True,
                             text=True, timeout=5).stdout.strip()
        if out:
            rss = float(out) / 1024.0
    except (OSError, subprocess.SubprocessError, ValueError):
        pass
    return foot, peak, rss


def count_lines(path):
    """Line count of the progress file (0 when it does not exist yet)."""
    try:
        with open(path, "rb") as f:
            return sum(1 for _ in f)
    except OSError:
        return 0


def find_leg_pid(root_pid, name="nativeee"):
    """The pid of the first descendant of `root_pid` whose command is `name`
    (the leg runs under run_bounded.sh); `root_pid` itself when none is found."""
    try:
        listing = subprocess.run(["pgrep", "-l", "-P", str(root_pid)], capture_output=True,
                                 text=True, timeout=5).stdout
    except (OSError, subprocess.SubprocessError):
        return root_pid
    children = []
    for line in listing.splitlines():
        parts = line.split(None, 1)
        if not parts:
            continue
        if len(parts) == 2 and os.path.basename(parts[1]) == name:
            return int(parts[0])
        children.append(int(parts[0]))
    for child in children:
        deeper = find_leg_pid(child, name)
        if deeper != child:
            return deeper
    return root_pid


def watch(pid, frames_file, out_path, period, alive):
    """Sample until `alive()` is false; one TSV line per sample; returns the samples."""
    t0 = time.time()
    rows = []
    with open(out_path, "w") as out:
        out.write("elapsed_s\tframes\tfootprint_mb\tpeak_mb\tresident_mb\n")
        while True:
            foot, peak, rss = sample_memory(pid)
            frames = count_lines(frames_file) if frames_file else -1
            row = (time.time() - t0, frames, foot, peak, rss)
            rows.append(row)
            out.write("%.1f\t%d\t%.1f\t%.1f\t%.1f\n" % row)
            out.flush()
            if not alive():
                break
            time.sleep(period)
    return rows


def selftest():
    ok = True
    text = ("ReadOnly portion of Libraries: Total=1.2G\n"
            "Physical footprint:         8.9G\n"
            "Physical footprint (peak):  18.4G\n")
    ok &= parse_vmmap_summary(text) == (8.9 * 1024.0, 18.4 * 1024.0)
    ok &= parse_vmmap_summary("Physical footprint:  512.0M\n") == (512.0, None)
    ok &= parse_vmmap_summary("nothing") == (None, None)
    ok &= abs(to_mb("2048", "K") - 2.0) < 1e-9 and to_mb("3", "M") == 3.0
    print("footprint_watch selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", help="TSV output path")
    ap.add_argument("--frames-file", help="the leg's frames.fhash (progress = line count)")
    ap.add_argument("--period", type=float, default=5.0, help="seconds between samples")
    ap.add_argument("--pid", type=int, help="attach to a running leg instead of launching one")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("cmd", nargs=argparse.REMAINDER, help="-- <command to launch>")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.out:
        ap.error("--out is required")
    cmd = a.cmd[1:] if a.cmd and a.cmd[0] == "--" else a.cmd
    if a.pid:
        pid = a.pid

        def alive():
            try:
                os.kill(pid, 0)
                return True
            except OSError:
                return False
        rows = watch(pid, a.frames_file, a.out, a.period, alive)
        rc = 0
    elif cmd:
        proc = subprocess.Popen(cmd)
        time.sleep(min(a.period, 2.0))
        pid = find_leg_pid(proc.pid)
        rows = watch(pid, a.frames_file, a.out, a.period, lambda: proc.poll() is None)
        rc = proc.returncode or 0
    else:
        ap.error("give --pid or -- <command>")
        return 2
    if rows:
        first, last = rows[0], rows[-1]
        print("footprint_watch: samples=%d frames %d -> %d footprint_mb %.1f -> %.1f peak_mb %.1f exit=%d"
              % (len(rows), first[1], last[1], first[2], last[2], last[3], rc))
    return rc


if __name__ == "__main__":
    sys.exit(main())
