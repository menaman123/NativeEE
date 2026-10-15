#!/usr/bin/env python3
"""vif_arms_vs_old.py -- run the NEW vif_replay arms and compare their output,
line by line, with the frozen OLD tool's arms.

Reference: a log written by the OLD arm runner (/tmp/p5/5P/run_old_vif_arms.sh
shape): blocks of `### ARM <name> env=[...] args=[...]` ... `### EXIT <name> = N`.
When --old-log is absent (or --refresh-old is given) and the OLD binary exists,
the reference is re-made here: every arm is run bounded from a cwd pinned under
--out/oldcwd (the OLD tool reads its env pins; that is allowed for the oracle run
only) into --out/old_vif_arms.log.

Arms compared: --arms all (the ctest row) covers every ported arm -- the two
UNPACK arms (5E), the two MTVU equivalence arms and the two runahead arms (5G);
--arms 5E keeps the UNPACK pair alone. The OLD ExecOn arms
(PS2_VU1_EXEC_ASYNC=1) are not in the table: that arm was never transplanted.

Normalisation (PLAN section 4.2): `[VU-MODE] ...` (OLD static init) and
`[run_bounded] ...` lines are dropped; the timing-dependent tokens named in
TIMING_TOKENS are blanked on both sides -- `N busy views`, `N pkts
eager-drained`, `(k/N iters)` in the MTVU PASS line and the `parked Nms` /
`at Nms` / `(Nms)` values of the runahead lines. NONE occurs in the UNPACK
arms, whose lines must therefore be exactly identical. Both exit codes must agree.

Prints `ARM <name> SAME|DIFF old_exit=N new_exit=N` per arm and
`VIFARMS arms=N same=M diff=K`; exit 0 iff diff == 0, 77 when neither the OLD
log nor the OLD binary is available (ctest SKIP), 2 on usage errors.
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OLD_ROOT = os.environ.get("PS2R_OLD_ROOT", "")  # the frozen OLD tree (read-only); empty = absent
DEFAULT_OLD_BIN = os.path.join(OLD_ROOT, "build", "tests", "vif_replay")
DEFAULT_OLD_LOG = "/tmp/p5/5P/old_vif_arms.log"
DEFAULT_NEW_BIN = os.path.join(REPO, "build", "tools", "vif_replay", "vif_replay")
DEFAULT_BOUNDED = os.path.join(REPO, "verify", "run_bounded.sh")

SKIP_PREFIXES = ("[VU-MODE]", "[run_bounded]")

# (name, OLD env pins, OLD args, NEW args, stage that owns it)
ARMS = [
    ("VifReplayMtvuEquivalence", {}, ["--mtvu", "20"], ["--mtvu", "20"], "5G"),
    ("VifReplayMtvuEquivalenceFast", {}, ["--mtvu", "20", "--vif-fast"], ["--mtvu", "20", "--vif-fast"], "5G"),
    ("VifUnpackBulkEquivalence", {}, ["--unpack-bulk"], ["--unpack-bulk"], "5E"),
    ("VifUnpackBulkEquivalenceFast", {}, ["--unpack-bulk", "--vif-fast"], ["--unpack-bulk", "--vif-fast"], "5E"),
    ("VifRunaheadBound", {"PS2_MTVU_MAX_FRAMES": "2"}, ["--runahead"], ["--runahead", "--depth", "2"], "5G"),
    ("VifRunaheadBoundOff", {"PS2_MTVU_MAX_FRAMES": "0"}, ["--runahead-off"], ["--runahead-off", "--depth", "0"], "5G"),
]

# Timing-dependent tokens (PLAN section 4.2 "NOT asserted"); each is replaced by
# its name on both sides before comparing. They only occur in the 5G arms (the
# NEW runahead arm prints fixed fake-clock values; the OLD one wall-clock ones).
TIMING_TOKENS = [
    (re.compile(r"\b\d+ busy views"), "<busy-views>"),
    (re.compile(r"\b\d+ pkts eager-drained"), "<pkts-eager-drained>"),
    (re.compile(r"\(\d+/\d+ iters\)"), "<iters>"),
    (re.compile(r"parked \d+ms"), "parked <ms>"),
    (re.compile(r"at \d+ms"), "at <ms>"),
    (re.compile(r"\(\d+ms\)"), "(<ms>)"),
]


def normalise(text):
    out = []
    for line in text.splitlines():
        line = line.rstrip()
        if not line or line.startswith(SKIP_PREFIXES):
            continue
        for rx, repl in TIMING_TOKENS:
            line = rx.sub(repl, line)
        out.append(line)
    return out


def parse_old_log(path):
    """-> {arm name: (exit code or None, [raw lines])}"""
    blocks = {}
    name = None
    lines = []
    with open(path, "r", errors="replace") as f:
        for raw in f:
            raw = raw.rstrip("\n")
            m = re.match(r"^### ARM (\S+) ", raw)
            if m:
                name = m.group(1)
                lines = []
                blocks[name] = [None, lines]
                continue
            m = re.match(r"^### EXIT (\S+) = (\d+)$", raw)
            if m and name == m.group(1):
                blocks[name][0] = int(m.group(2))
                name = None
                continue
            if name is not None:
                lines.append(raw)
    return {k: (v[0], v[1]) for k, v in blocks.items()}


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


def refresh_old(args, arms, base_env):
    """Re-run the OLD arms bounded (cwd pinned under --out) into --out/old_vif_arms.log."""
    oldcwd = os.path.join(args.out, "oldcwd")
    os.makedirs(oldcwd, exist_ok=True)
    log_path = os.path.join(args.out, "old_vif_arms.log")
    with open(log_path, "w") as out:
        for name, env_pins, old_args, _new_args, _stage in arms:
            env = dict(base_env)
            env.update(env_pins)
            pins = " ".join("%s=%s" % kv for kv in env_pins.items())
            out.write("### ARM %s env=[%s] args=[%s]\n" % (name, pins, " ".join(old_args)))
            code, text = run_bounded(args.bounded, args.timeout, os.path.join(args.out, "old_%s.log" % name),
                                     oldcwd, [args.old_bin] + old_args, env)
            out.write(text if text.endswith("\n") or not text else text + "\n")
            out.write("### EXIT %s = %d\n" % (name, code))
        out.write("DONE\n")
    return log_path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--new-bin", default=DEFAULT_NEW_BIN)
    ap.add_argument("--old-bin", default=DEFAULT_OLD_BIN)
    ap.add_argument("--old-log", default=DEFAULT_OLD_LOG, help="reference log (used when it exists unless --refresh-old)")
    ap.add_argument("--refresh-old", action="store_true", help="re-run the OLD arms bounded to remake the reference")
    ap.add_argument("--arms", default="all", help="'5E' (the UNPACK arms), 'all' (default: every ported arm), or a comma list of names")
    ap.add_argument("--out", required=True, help="output directory (logs, the OLD cwd)")
    ap.add_argument("--bounded", default=DEFAULT_BOUNDED)
    ap.add_argument("--timeout", type=int, default=300)
    args = ap.parse_args()

    if args.arms == "5E":
        arms = [a for a in ARMS if a[4] == "5E"]
    elif args.arms == "all":
        arms = list(ARMS)
    else:
        wanted = set(args.arms.split(","))
        arms = [a for a in ARMS if a[0] in wanted]
        if len(arms) != len(wanted):
            print("unknown arm(s): %s" % ", ".join(sorted(wanted - {a[0] for a in arms})), file=sys.stderr)
            return 2

    if not os.path.isfile(args.new_bin) or not os.access(args.new_bin, os.X_OK):
        print("SKIP: NEW binary missing: %s" % args.new_bin, file=sys.stderr)
        return 77
    os.makedirs(args.out, exist_ok=True)
    base_env = dict(os.environ)
    base_env["RB_POLL_S"] = "1"

    old_available = os.path.isfile(args.old_bin) and os.access(args.old_bin, os.X_OK)
    old_log = args.old_log
    if args.refresh_old or not os.path.isfile(old_log):
        if not old_available:
            print("SKIP: neither the OLD log (%s) nor the OLD binary (%s) is available" % (old_log, args.old_bin),
                  file=sys.stderr)
            return 77
        old_log = refresh_old(args, arms, base_env)
        print("reference remade from the OLD tool: %s" % old_log)
    old_blocks = parse_old_log(old_log)

    same = diff = 0
    for name, _env_pins, _old_args, new_args, _stage in arms:
        if name not in old_blocks:
            print("ARM %s DIFF (missing from the OLD log)" % name)
            diff += 1
            continue
        old_exit, old_lines = old_blocks[name]
        new_exit, new_text = run_bounded(args.bounded, args.timeout, os.path.join(args.out, "new_%s.log" % name),
                                         args.out, [args.new_bin] + new_args, base_env)
        old_n = normalise("\n".join(old_lines))
        new_n = normalise(new_text)
        ok = old_exit == new_exit and old_n == new_n
        print("ARM %s %s old_exit=%s new_exit=%d lines_old=%d lines_new=%d" %
              (name, "SAME" if ok else "DIFF", old_exit, new_exit, len(old_n), len(new_n)))
        if ok:
            same += 1
            continue
        diff += 1
        width = max(len(old_n), len(new_n))
        for i in range(width):
            o = old_n[i] if i < len(old_n) else "<absent>"
            n = new_n[i] if i < len(new_n) else "<absent>"
            if o != n:
                print("    old[%d]: %s" % (i, o))
                print("    new[%d]: %s" % (i, n))
    print("VIFARMS arms=%d same=%d diff=%d" % (len(arms), same, diff))
    return 0 if diff == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
