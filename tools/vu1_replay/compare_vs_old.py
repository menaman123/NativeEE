#!/usr/bin/env python3
"""compare_vs_old.py -- run the OLD (frozen) and NEW vu1_replay tools over a corpus
directory, case by case, and compare their output textually.

Per case (sorted basenames):
  * `--compare <case>` on both binaries: exit codes and every stdout/stderr line
    (after dropping `[VU-MODE] ...` and `[run_bounded] ...`) must be identical.
  * optionally (`--postdata N`, the first N cases; `all` for every case):
    `--mode interp <case> <dump>` and `--mode cached <case> <dump>` on both
    binaries; the 16 KB data dump and the 608-byte `.regs` sidecar must be
    byte-identical per tier (the bit-exact per-tier oracle).

Every process runs through verify/run_bounded.sh (wall timeout; RB_POLL_S=1 so a
run costs ~1 s of polling) with the OLD binary's cwd pinned under --out so the
frozen tree is never written. Sequential; no multiprocessing.

Prints `CMP <case> SAME|DIFF old_exit=N new_exit=N` per case and
`CMP cases=N same=M diff=K old_fail=A new_fail=B`; exit 0 iff diff == 0,
77 when the OLD binary or the corpus is absent (ctest SKIP).
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OLD_ROOT = os.environ.get("PS2R_OLD_ROOT", "")  # the frozen OLD tree (read-only); empty = absent
DEFAULT_OLD_BIN = os.path.join(OLD_ROOT, "build", "tests", "vu1_replay")
DEFAULT_NEW_BIN = os.path.join(REPO, "build", "tools", "vu1_replay", "vu1_replay")
DEFAULT_CASES = os.path.join(OLD_ROOT, "vutrace_cases2")
DEFAULT_BOUNDED = os.path.join(REPO, "verify", "run_bounded.sh")

SKIP_PREFIXES = ("[VU-MODE]", "[run_bounded]")


def normalise(text):
    out = []
    for line in text.splitlines():
        line = line.rstrip()
        if line.startswith(SKIP_PREFIXES):
            continue
        out.append(line)
    return out


def run_bounded(bounded, timeout, log, cwd, cmd, env):
    if os.path.exists(log):
        os.remove(log)
    full = [bounded, str(timeout), log, "--"] + cmd
    proc = subprocess.run(full, cwd=cwd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        with open(log, "r", errors="replace") as f:
            text = f.read()
    except OSError:
        text = ""
    return proc.returncode, text


def files_identical(a, b):
    try:
        with open(a, "rb") as fa, open(b, "rb") as fb:
            return fa.read() == fb.read()
    except OSError:
        return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--old-bin", default=DEFAULT_OLD_BIN)
    ap.add_argument("--new-bin", default=DEFAULT_NEW_BIN)
    ap.add_argument("--cases", default=DEFAULT_CASES, help="corpus directory (or one case file)")
    ap.add_argument("--mode", default="compare", choices=["compare"], help="tool arm to compare (5C: compare)")
    ap.add_argument("--out", required=True, help="output directory (logs, dumps, the OLD cwd)")
    ap.add_argument("--bounded", default=DEFAULT_BOUNDED)
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--postdata", default="0",
                    help="also cmp the --mode interp/cached dumps for the first N cases ('all' = every case)")
    ap.add_argument("--limit", type=int, default=0, help="only the first N cases (0 = all)")
    args = ap.parse_args()

    if not os.path.isfile(args.old_bin) or not os.access(args.old_bin, os.X_OK):
        print("compare_vs_old: OLD tool absent: %s -> SKIP" % args.old_bin)
        return 77
    if not os.path.isfile(args.new_bin):
        print("compare_vs_old: NEW tool absent: %s" % args.new_bin)
        return 2
    if os.path.isdir(args.cases):
        cases = sorted(f for f in os.listdir(args.cases) if f.endswith(".bin"))
        cases = [os.path.join(args.cases, f) for f in cases]
    elif os.path.isfile(args.cases):
        cases = [args.cases]
    else:
        print("compare_vs_old: corpus absent: %s -> SKIP" % args.cases)
        return 77
    if args.limit > 0:
        cases = cases[: args.limit]
    if not cases:
        print("compare_vs_old: no cases under %s -> SKIP" % args.cases)
        return 77
    postdata_n = len(cases) if args.postdata == "all" else int(args.postdata)

    out = os.path.abspath(args.out)
    old_dir = os.path.join(out, "old")
    new_dir = os.path.join(out, "new")
    oldcwd = os.path.join(out, "oldcwd")
    for d in (old_dir, new_dir, oldcwd):
        os.makedirs(d, exist_ok=True)
    env = dict(os.environ)
    env["RB_POLL_S"] = "1"

    same = diff = old_fail = new_fail = 0
    for idx, case in enumerate(cases):
        name = os.path.basename(case)
        base = name[:-4] if name.endswith(".bin") else name
        rc_old, txt_old = run_bounded(args.bounded, args.timeout, os.path.join(old_dir, base + ".compare.log"),
                                      oldcwd, [args.old_bin, "--compare", case], env)
        rc_new, txt_new = run_bounded(args.bounded, args.timeout, os.path.join(new_dir, base + ".compare.log"),
                                      out, [args.new_bin, "--compare", case], env)
        if rc_old != 0:
            old_fail += 1
        if rc_new != 0:
            new_fail += 1
        n_old, n_new = normalise(txt_old), normalise(txt_new)
        ok = rc_old == rc_new and n_old == n_new
        detail = ""
        if idx < postdata_n:
            for tier in ("interp", "cached"):
                d_old = os.path.join(old_dir, "%s.%s.bin" % (base, tier))
                d_new = os.path.join(new_dir, "%s.%s.bin" % (base, tier))
                r1, t1 = run_bounded(args.bounded, args.timeout, os.path.join(old_dir, "%s.%s.log" % (base, tier)),
                                     oldcwd, [args.old_bin, "--mode", tier, case, d_old], env)
                r2, t2 = run_bounded(args.bounded, args.timeout, os.path.join(new_dir, "%s.%s.log" % (base, tier)),
                                     out, [args.new_bin, "--mode", tier, case, d_new], env)
                # the dump lines name different paths; compare only the `replayed` line
                rep1 = [l for l in normalise(t1) if l.startswith("replayed ")]
                rep2 = [l for l in normalise(t2) if l.startswith("replayed ")]
                data_same = files_identical(d_old, d_new)
                regs_same = files_identical(d_old + ".regs", d_new + ".regs")
                tier_ok = r1 == 0 and r2 == 0 and rep1 == rep2 and data_same and regs_same
                detail += " %s=%s" % (tier, "cmp_ok" if tier_ok else
                                      "cmp_DIFF(rc=%d/%d replayed=%s data=%s regs=%s)" %
                                      (r1, r2, rep1 == rep2, data_same, regs_same))
                ok = ok and tier_ok
        if ok:
            same += 1
        else:
            diff += 1
        print("CMP %s %s old_exit=%d new_exit=%d%s" % (name, "SAME" if ok else "DIFF", rc_old, rc_new, detail))
        if not ok and n_old != n_new:
            for l in n_old:
                print("   old| " + l)
            for l in n_new:
                print("   new| " + l)
        sys.stdout.flush()

    print("CMP cases=%d same=%d diff=%d old_fail=%d new_fail=%d" % (len(cases), same, diff, old_fail, new_fail))
    return 0 if diff == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
