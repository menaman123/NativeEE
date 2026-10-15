#!/usr/bin/env python3
"""aggregate_sweep.py -- sum the per-chunk `CMP cases=` lines of a sweep_vs_old.sh run.

Reads every `chunk_*.log` (and its `chunk_*.done`, `exit=N`) under --logs, requires
exactly one `CMP cases=N same=M diff=K old_fail=A new_fail=B` footer per chunk, and prints

    SWEEP cases=N same=M diff=K old_fail=A new_fail=B chunks=C

followed by one `DIFF <case> <driver line>` row per divergent case (their `.bin` files are
copied into --diffs when the corpus path is known from the driver's log header, else the
names are listed). Chunks without a footer or with a non-zero `.done` exit are reported as
`CHUNK <id> INCOMPLETE ...` and count as a failure.

Exit status: 0 iff diff == 0, every chunk complete, and (when --expect N is given) cases == N;
1 otherwise; 77 when --logs holds no chunk log at all.
"""
import argparse
import glob
import os
import re
import shutil
import sys

FOOTER = re.compile(r"^CMP cases=(\d+) same=(\d+) diff=(\d+) old_fail=(\d+) new_fail=(\d+)\s*$")
CASE_LINE = re.compile(r"^CMP (\S+\.bin) (SAME|DIFF)\b")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--logs", required=True, help="directory holding chunk_NN.log / chunk_NN.done")
    ap.add_argument("--expect", type=int, default=0, help="required total case count (0 = do not check)")
    ap.add_argument("--diffs", default="", help="copy every DIFF case here (default: <logs>/../diffs)")
    ap.add_argument("--corpus", default="", help="corpus directory the DIFF case names resolve in")
    args = ap.parse_args()

    logs = sorted(glob.glob(os.path.join(args.logs, "chunk_*.log")))
    if not logs:
        print("SWEEP SKIP: no chunk logs under %s" % args.logs)
        return 77

    totals = {"cases": 0, "same": 0, "diff": 0, "old_fail": 0, "new_fail": 0}
    diffs = []
    incomplete = []
    for log in logs:
        cid = os.path.basename(log)[len("chunk_"):-len(".log")]
        footer = None
        with open(log, "r", errors="replace") as fh:
            for line in fh:
                m = FOOTER.match(line)
                if m:
                    footer = [int(x) for x in m.groups()]
                    continue
                c = CASE_LINE.match(line)
                if c and c.group(2) == "DIFF":
                    diffs.append((c.group(1), line.rstrip("\n")))
        done_path = log[:-len(".log")] + ".done"
        done_exit = None
        if os.path.exists(done_path):
            with open(done_path) as fh:
                dm = re.search(r"exit=(\d+)", fh.read())
                done_exit = int(dm.group(1)) if dm else None
        if footer is None:
            incomplete.append("CHUNK %s INCOMPLETE: no `CMP cases=` footer (done=%s)" % (cid, done_exit))
            continue
        if done_exit is None:
            incomplete.append("CHUNK %s INCOMPLETE: no .done file" % cid)
        elif done_exit not in (0, 1):
            # 1 = the driver saw a DIFF (counted below); anything else is a bounded-runner kill.
            incomplete.append("CHUNK %s INCOMPLETE: driver exit=%d" % (cid, done_exit))
        for key, val in zip(("cases", "same", "diff", "old_fail", "new_fail"), footer):
            totals[key] += val

    print("SWEEP cases=%d same=%d diff=%d old_fail=%d new_fail=%d chunks=%d" %
          (totals["cases"], totals["same"], totals["diff"], totals["old_fail"], totals["new_fail"], len(logs)))
    for msg in incomplete:
        print(msg)

    if diffs:
        diff_dir = args.diffs or os.path.join(os.path.dirname(os.path.abspath(args.logs)), "diffs")
        os.makedirs(diff_dir, exist_ok=True)
        for name, line in diffs:
            print("DIFF %s  %s" % (name, line))
            if args.corpus:
                src = os.path.join(args.corpus, name)
                if os.path.exists(src):
                    shutil.copy2(src, os.path.join(diff_dir, name))
        with open(os.path.join(diff_dir, "DIFF_CASES.txt"), "w") as fh:
            fh.write("\n".join(name for name, _ in diffs) + "\n")
        print("DIFF cases listed in %s/DIFF_CASES.txt" % diff_dir)

    ok = totals["diff"] == 0 and not incomplete and len(diffs) == totals["diff"]
    if args.expect and totals["cases"] != args.expect:
        print("SWEEP FAIL: cases=%d != expected %d" % (totals["cases"], args.expect))
        ok = False
    if len(diffs) != totals["diff"]:
        print("SWEEP FAIL: %d DIFF lines vs footer diff=%d" % (len(diffs), totals["diff"]))
    print("SWEEP %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
