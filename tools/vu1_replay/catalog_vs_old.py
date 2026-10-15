#!/usr/bin/env python3
"""catalog_vs_old.py -- the `--catalog` identity gate (G4): NEW `vu1_replay
--catalog <corpus>` line for line equal to the frozen OLD tool's, the
`[VU-MODE]` announce (and any bounded-runner lines) skipped.

The OLD reference is --old-log when given and present, else the OLD binary is
run (cwd pinned to --old-cwd so the frozen tree is never written). Writes
<out>/{old,new}_catalog.log and prints the footer of both plus the diff
verdict `CATALOG DIFF EMPTY` / `CATALOG DIFF <n> lines`; exit 0 = empty, 1 =
any difference, 77 = tool / corpus / reference absent.
"""
import argparse
import difflib
import os
import subprocess
import sys

SKIP_EXIT = 77


def strip(lines):
    return [l for l in lines if not l.startswith("[VU-MODE]") and not l.startswith("[run_bounded]")]


def run_catalog(binary, cases, cwd=None):
    p = subprocess.run([binary, "--catalog", cases], capture_output=True, text=True, timeout=1100, cwd=cwd)
    return strip(p.stdout.rstrip("\n").split("\n") if p.stdout else []), p.returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--new-bin", required=True)
    ap.add_argument("--cases", required=True)
    ap.add_argument("--old-log", default=None)
    ap.add_argument("--old-bin", default=None)
    ap.add_argument("--old-cwd", default=None)
    ap.add_argument("--out", default=None, help="directory for old_catalog.log / new_catalog.log")
    a = ap.parse_args()

    if not os.path.isfile(a.new_bin) or not os.path.isdir(a.cases):
        print("CATALOG SKIP: tool or corpus absent (%s, %s)" % (a.new_bin, a.cases))
        return SKIP_EXIT
    if a.old_log and os.path.isfile(a.old_log):
        old = strip([l.rstrip("\n") for l in open(a.old_log)])
        old_exit = 0
    elif a.old_bin and os.path.isfile(a.old_bin):
        if a.old_cwd:
            os.makedirs(a.old_cwd, exist_ok=True)
        old, old_exit = run_catalog(a.old_bin, a.cases, a.old_cwd)
    else:
        print("CATALOG SKIP: no OLD reference (log %s, bin %s)" % (a.old_log, a.old_bin))
        return SKIP_EXIT
    new, new_exit = run_catalog(a.new_bin, a.cases)

    if a.out:
        os.makedirs(a.out, exist_ok=True)
        open(os.path.join(a.out, "old_catalog.log"), "w").write("\n".join(old) + "\n")
        open(os.path.join(a.out, "new_catalog.log"), "w").write("\n".join(new) + "\n")
    diff = list(difflib.unified_diff(old, new, "old", "new", lineterm="", n=0))
    for l in diff:
        print(l)
    print("OLD footer: %s (exit %d)" % (old[-1] if old else "(empty)", old_exit))
    print("NEW footer: %s (exit %d)" % (new[-1] if new else "(empty)", new_exit))
    if not diff and old_exit == new_exit:
        print("CATALOG DIFF EMPTY (%d lines)" % len(new))
        return 0
    print("CATALOG DIFF %d lines" % len(diff))
    return 1


if __name__ == "__main__":
    sys.exit(main())
