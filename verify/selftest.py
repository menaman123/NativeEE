#!/usr/bin/env python3
"""selftest.py -- the Phase-0 admissibility self-test
(EQUIVALENCE_PLAN.md section 4: run the OLD binary twice per leg under the
deterministic profile; identical => the leg is ADMISSIBLE).

  selftest.py --binary <host_app> --anchors <a1,a2,...|@file>
              --frames N [--coldboot-frames M] --out <dir>
              [--reps 2] [--tree legacy|v2] [--repo ROOT]

Anchors: 'coldboot' or repo-relative .ps2st paths. Each leg runs --reps
times SEQUENTIALLY (never in parallel: same machine, no contention); reps
are then diffed pairwise against rep 1. Report: per leg
ADMISSIBLE/NONDET(first divergence)/ERROR. Exit 0 iff all ADMISSIBLE.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from diff_legs import diff_legs           # noqa: E402
from run_leg import run_leg, DEFAULT_REPO  # noqa: E402


def leg_name(anchor):
    if anchor == "coldboot":
        return "coldboot"
    base = os.path.basename(anchor)
    return base[:-len(".ps2st")] if base.endswith(".ps2st") else base


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--anchors", required=True,
                    help="comma list, or @file with one anchor per line")
    ap.add_argument("--frames", type=int, required=True)
    ap.add_argument("--coldboot-frames", type=int,
                    help="frame count override for the coldboot leg")
    ap.add_argument("--reps", type=int, default=2)
    ap.add_argument("--out", required=True)
    ap.add_argument("--tree", default="legacy", choices=["legacy", "v2"])
    ap.add_argument("--repo", default=DEFAULT_REPO)
    a = ap.parse_args()

    if a.anchors.startswith("@"):
        with open(a.anchors[1:]) as f:
            anchors = [l.strip() for l in f
                       if l.strip() and not l.startswith("#")]
    else:
        anchors = [x for x in a.anchors.split(",") if x]

    os.makedirs(a.out, exist_ok=True)
    results = []
    all_ok = True
    for anchor in anchors:
        name = leg_name(anchor)
        frames = a.frames
        if anchor == "coldboot" and a.coldboot_frames:
            frames = a.coldboot_frames
        rep_dirs = []
        err = None
        for rep in range(1, a.reps + 1):
            d = os.path.join(a.out, "%s.rep%d" % (name, rep))
            print("[selftest] running %s rep %d/%d (%d frames)..."
                  % (name, rep, a.reps, frames), flush=True)
            err = run_leg(a.binary, anchor, frames, d, a.repo, a.tree)
            if err:
                err = "rep%d: %s" % (rep, err)
                break
            rep_dirs.append(d)
        if err:
            results.append((name, "ERROR", err))
            all_ok = False
            continue
        verdict, detail = "ADMISSIBLE", ""
        for rep_d in rep_dirs[1:]:
            code, rep_lines = diff_legs(rep_dirs[0], rep_d)
            if code != 0:
                verdict = "NONDET" if code == 1 else "ERROR"
                first = [l for l in rep_lines if "FIRST DIVERGENT" in l
                         or "MISMATCH" in l or "MISSING" in l]
                detail = first[0] if first else "; ".join(rep_lines[:2])
                all_ok = False
                break
        results.append((name, verdict, detail))
        print("[selftest] %s: %s %s" % (name, verdict, detail), flush=True)

    print("\n=== selftest report (binary=%s tree=%s reps=%d) ==="
          % (a.binary, a.tree, a.reps))
    for name, verdict, detail in results:
        print("%-42s %-10s %s" % (name, verdict, detail))
    n_adm = sum(1 for r in results if r[1] == "ADMISSIBLE")
    print("=== %d/%d legs ADMISSIBLE ===" % (n_adm, len(results)))
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
