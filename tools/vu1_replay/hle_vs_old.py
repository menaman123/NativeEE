#!/usr/bin/env python3
"""hle_vs_old.py -- the VU1 HLE library identity gate (G3): NEW `vu1_replay --hle
--skip-unregistered <case>` over every corpus case, every stdout line + the exit
code compared with the frozen OLD tool's block.

Reference = an OLD log in the `### CASE <name>` / lines / `### EXIT <name> = N`
block grammar (--old-log). When the log lacks a case (or --old-log is not
given) the OLD binary is run bounded (--old-bin, cwd pinned to --old-cwd so the
frozen tree is never written) and its `[VU-MODE]` announce stripped. Prints one
`SAME <case>` / `DIFF <case>` line per case and the footer
`HLE cases=N same=S diff=D`; exit 0 = diff 0, 1 = any DIFF, 77 = the NEW tool,
the corpus, or every OLD reference is absent.
"""
import argparse
import os
import subprocess
import sys

SKIP_EXIT = 77


def parse_old_log(path):
    blocks = {}
    cur = None
    if not path or not os.path.isfile(path):
        return blocks
    for line in open(path):
        line = line.rstrip("\n")
        if line.startswith("### CASE "):
            cur = line[len("### CASE "):]
            blocks[cur] = {"lines": [], "exit": None}
        elif line.startswith("### EXIT "):
            name, code = line[len("### EXIT "):].split(" = ")
            if name in blocks:
                blocks[name]["exit"] = int(code)
            cur = None
        elif line.startswith("### "):
            cur = None
        elif cur is not None:
            blocks[cur]["lines"].append(line)
    return {k: v for k, v in blocks.items() if v["exit"] is not None}


def strip_announce(lines):
    return [l for l in lines if not l.startswith("[VU-MODE]") and not l.startswith("[run_bounded]")]


def run(cmd, cwd=None, timeout=300):
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, cwd=cwd)
    lines = p.stdout.rstrip("\n").split("\n") if p.stdout else []
    return strip_announce(lines), p.returncode, p.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--new-bin", required=True)
    ap.add_argument("--cases", required=True, help="corpus directory of *.bin cases")
    ap.add_argument("--old-log", default=None, help="OLD reference log (### CASE / ### EXIT blocks)")
    ap.add_argument("--old-bin", default=None, help="OLD vu1_replay, run bounded when the log lacks a case")
    ap.add_argument("--old-cwd", default=None, help="cwd for OLD runs (never the frozen tree)")
    ap.add_argument("--out", default=None, help="write the NEW blocks (+ DIFF detail) here")
    ap.add_argument("--quiet", action="store_true", help="print only DIFF lines and the footer")
    a = ap.parse_args()

    if not os.path.isfile(a.new_bin) or not os.path.isdir(a.cases):
        print("HLE SKIP: tool or corpus absent (%s, %s)" % (a.new_bin, a.cases))
        return SKIP_EXIT
    old = parse_old_log(a.old_log)
    old_bin_ok = a.old_bin is not None and os.path.isfile(a.old_bin)
    if not old and not old_bin_ok:
        print("HLE SKIP: no OLD reference (log %s, bin %s)" % (a.old_log, a.old_bin))
        return SKIP_EXIT
    if old_bin_ok and a.old_cwd:
        os.makedirs(a.old_cwd, exist_ok=True)

    files = sorted(f for f in os.listdir(a.cases) if f.endswith(".bin"))
    same = diff = 0
    out = open(a.out, "w") if a.out else None
    for f in files:
        base = f[:-4]
        path = os.path.join(a.cases, f)
        new_lines, new_exit, new_err = run([a.new_bin, "--hle", "--skip-unregistered", path])
        ref = old.get(base)
        if ref is None and old_bin_ok:
            o_lines, o_exit, _ = run([a.old_bin, "--hle", "--skip-unregistered", path], cwd=a.old_cwd)
            ref = {"lines": o_lines, "exit": o_exit}
        ok = ref is not None and ref["lines"] == new_lines and ref["exit"] == new_exit
        if out:
            out.write("### CASE %s\n%s\n### EXIT %s = %d\n" % (base, "\n".join(new_lines), base, new_exit))
        if ok:
            same += 1
            if not a.quiet:
                print("SAME %s" % base)
        else:
            diff += 1
            print("DIFF %s" % base)
            detail = "--- OLD (exit %s)\n%s\n--- NEW (exit %d)\n%s\n" % (
                ref["exit"] if ref else "?", "\n".join(ref["lines"]) if ref else "(no reference)",
                new_exit, "\n".join(new_lines))
            if new_err:
                detail += "--- NEW stderr\n" + new_err + "\n"
            print(detail, end="")
            if out:
                out.write("### DIFF %s\n%s" % (base, detail))
    line = "HLE cases=%d same=%d diff=%d" % (same + diff, same, diff)
    if out:
        out.write("### DONE %s\n" % line)
        out.close()
    print(line)
    return 0 if diff == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
