#!/usr/bin/env python3
"""hle_gate.py -- the VU1 HLE library gate over a corpus directory.

Runs `vu1_replay --hle --skip-unregistered <case>` on every *.bin under --cases
and requires `HLE PASS` (exit 0) for every case whose registered program name is
in --programs (comma-separated); cases whose program is not listed are reported
but not judged (their programs land in a later part), and unregistered images
(exit 77) are counted as skips. Exit 0 = every judged case PASSED, 1 = any FAIL /
FALLBACK / mismatch, 77 = the tool or the corpus is absent.
"""
import argparse
import os
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--cases", required=True)
    ap.add_argument("--programs", required=True, help="comma-separated program names to judge")
    ap.add_argument("--all", action="store_true", help="judge every registered program")
    ap.add_argument("--require-registered", action="store_true",
                    help="an unregistered case (exit 77) is a FAIL, not a skip")
    a = ap.parse_args()
    if not os.path.isfile(a.bin) or not os.path.isdir(a.cases):
        print("HLE-GATE SKIP: tool or corpus absent (%s, %s)" % (a.bin, a.cases))
        return 77
    judged = set(p for p in a.programs.split(",") if p)
    files = sorted(f for f in os.listdir(a.cases) if f.endswith(".bin"))
    n_pass = n_fail = n_skip = n_other = 0
    for f in files:
        p = subprocess.run([a.bin, "--hle", "--skip-unregistered", os.path.join(a.cases, f)],
                           capture_output=True, text=True, timeout=300)
        lines = p.stdout.rstrip("\n").split("\n") if p.stdout else []
        verdict = lines[-1] if lines else ""
        if p.returncode == 77:
            n_skip += 1
            if a.require_registered:
                n_fail += 1
                print("HLE-GATE FAIL %s (unregistered):\n%s" % (f, "\n".join(lines)))
            continue
        name = ""
        if verdict.startswith("HLE PASS: ") or verdict.startswith("HLE FAIL: "):
            name = verdict[len("HLE PASS: "):].split(" ")[0]
        elif verdict.startswith("HLE FALLBACK: "):
            name = verdict[len("HLE FALLBACK: "):].split(" ")[0]
        if not a.all and name not in judged:
            n_other += 1
            continue
        if p.returncode == 0 and verdict.startswith("HLE PASS: "):
            n_pass += 1
        else:
            n_fail += 1
            print("HLE-GATE FAIL %s (exit %d):\n%s" % (f, p.returncode, "\n".join(lines)))
    print("HLE-GATE cases=%d judged=%d pass=%d fail=%d unregistered=%d other_programs=%d"
          % (len(files), n_pass + n_fail, n_pass, n_fail, n_skip, n_other))
    return 1 if n_fail else 0


if __name__ == "__main__":
    sys.exit(main())
