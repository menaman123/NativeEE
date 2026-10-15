#!/usr/bin/env python3
"""ring_decode_oracle.py -- cross-decoder proof for the ring log text form.

Runs the NEW test_ring_log binary (which writes <dir>/fixture.rlog and its own
decode <dir>/fixture.txt), then decodes the SAME .rlog with the frozen
original's offline decoder (OLD/build/ring_decode, run bounded, read-only) and
requires the two texts to be byte-identical. SKIPs (exit 0, message) when the
OLD binary is absent.

usage: ring_decode_oracle.py --test-binary PATH --scratch DIR --og-root OLD --bounded verify/run_bounded.sh
"""
import argparse
import os
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--test-binary", required=True)
    ap.add_argument("--scratch", required=True)
    ap.add_argument("--og-root", required=True)
    ap.add_argument("--bounded", required=True)
    a = ap.parse_args()

    os.makedirs(a.scratch, exist_ok=True)
    r = subprocess.run([a.test_binary, a.scratch], capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    if r.returncode != 0:
        print("ring_decode_oracle: NEW test binary failed")
        return 1

    old_decoder = os.path.join(a.og_root, "build", "ring_decode")
    if not os.path.isfile(old_decoder):
        print("ring_decode_oracle: SKIP (frozen OLD ring_decode not found at %s)" % old_decoder)
        return 0

    rlog = os.path.join(a.scratch, "fixture.rlog")
    new_txt = os.path.join(a.scratch, "fixture.txt")
    old_txt = os.path.join(a.scratch, "fixture_old.txt")
    log = os.path.join(a.scratch, "ring_decode_old.log")
    if os.path.exists(log):
        os.remove(log)
    # The OLD tool prints "file=<argv[1]>": pass the identical path string.
    rc = subprocess.run(["bash", a.bounded, "60", log, "--", old_decoder, rlog, old_txt]).returncode
    if rc != 0:
        print("ring_decode_oracle: OLD ring_decode exit %d (see %s)" % (rc, log))
        return 1
    with open(new_txt, "rb") as f:
        new = f.read()
    with open(old_txt, "rb") as f:
        old = f.read()
    if new != old:
        nl, ol = new.split(b"\n"), old.split(b"\n")
        for i in range(max(len(nl), len(ol))):
            x = nl[i] if i < len(nl) else b"<eof>"
            y = ol[i] if i < len(ol) else b"<eof>"
            if x != y:
                print("ring_decode_oracle: first difference at line %d\n  new: %s\n  old: %s"
                      % (i + 1, x.decode(errors="replace"), y.decode(errors="replace")))
                break
        return 1
    print("ring_decode_oracle: IDENTICAL (%d lines, %d bytes) vs frozen OLD ring_decode"
          % (new.count(b"\n"), len(new)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
