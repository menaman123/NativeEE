#!/usr/bin/env python3
"""check_gsops_seed.py -- assert the recorded register-op stream seed.

For every `<case> <count> <hash>` line of the fixture, run `vu1_replay --gsops
<corpus>/<case>` and require the identical line (the interpreter's kicked GIF
stream, decoded by GifParser into RegisterOpStream: op count + FNV-1a 64 hash).
The fixture was generated once at stage 5C from the frozen corpus; any change is
a GIF-parser or VU-tier divergence to root-cause, never to regenerate silently.

usage: check_gsops_seed.py --tool <vu1_replay> --cases <dir> --seed <file>
exit 0 identical, 1 on any difference, 77 when the corpus directory is absent.
"""
import argparse
import os
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", required=True)
    ap.add_argument("--cases", required=True)
    ap.add_argument("--seed", required=True)
    args = ap.parse_args()
    if not os.path.isdir(args.cases):
        print("check_gsops_seed: corpus absent: %s -> SKIP" % args.cases)
        return 77
    with open(args.seed) as f:
        expected = [l.rstrip("\n") for l in f if l.strip()]
    bad = 0
    for line in expected:
        name = line.split()[0]
        path = os.path.join(args.cases, name)
        proc = subprocess.run([args.tool, "--gsops", path], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              universal_newlines=True, timeout=120)
        got = proc.stdout.strip()
        if proc.returncode != 0 or got != line:
            bad += 1
            print("GSOPS DIFF %s\n  want: %s\n  got:  %s (rc=%d)" % (name, line, got, proc.returncode))
    print("GSOPS seed lines=%d diff=%d" % (len(expected), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
