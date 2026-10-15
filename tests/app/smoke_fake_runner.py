#!/usr/bin/env python3
"""smoke_fake_runner.py -- the test of the play-mode smoke tests.

A play smoke (root_play_mode_smoke.py, root_play_fmv_presents.py) judges a game
process it ran through verify/run_bounded.sh. Its verdict must never turn a
CRASHED or KILLED game process into a ctest SKIP: cutover flips 2 and 3 saw both
smokes report `runner exit=134` (SIGABRT) with an unfinalized run.meta and ctest
print `100% tests passed` over two dead game processes.

This module stands in for run_bounded.sh: `fake_runner_script()` writes an
executable that accepts the runner grammar (`TIMEOUT LOG -- binary args...`),
finds `--harness-out <dir>` among the binary's arguments, writes the run.meta text
it is given there and exits with the code it is given. `run_smoke()` runs a smoke
script against that runner with a fake title directory (the ELF path exists as an
empty file) and a fake binary, and returns the smoke's exit code and output. No
game process is started.
"""
import os
import shutil
import stat
import subprocess
import sys
import tempfile

ELF_REL = os.path.join("example_title.rep", "SLUS_123.45")

_RUNNER = """#!/bin/sh
# fake run_bounded.sh: TIMEOUT LOG -- cmd args...  (writes FAKE_META to --harness-out, exits FAKE_EXIT)
shift 2
[ "${1:-}" = "--" ] && shift
out=""
while [ $# -gt 0 ]; do
  if [ "$1" = "--harness-out" ]; then out="$2"; fi
  shift
done
if [ -n "$out" ] && [ -n "${FAKE_META:-}" ]; then
  mkdir -p "$out"
  printf '%s\\n' "$FAKE_META" > "$out/run.meta"
fi
exit "${FAKE_EXIT:-0}"
"""


def fake_runner_script(root):
    path = os.path.join(root, "fake_run_bounded.sh")
    with open(path, "w", encoding="utf-8") as f:
        f.write(_RUNNER)
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    return path


def fake_title(root):
    """A game-files directory whose ELF path exists (the smoke's missing-asset SKIP must not fire)
    and a 'binary' file (the smoke's binary-missing FAIL must not fire)."""
    game_files = os.path.join(root, "game_files")
    os.makedirs(os.path.dirname(os.path.join(game_files, ELF_REL)), exist_ok=True)
    open(os.path.join(game_files, ELF_REL), "w").close()
    binary = os.path.join(root, "nativeee")
    open(binary, "w").close()
    return game_files, binary


def run_smoke(script, exit_code, meta_text, extra_args=()):
    """Run `script` against the fake runner; returns (smoke exit code, combined output).
    `meta_text` = the run.meta the fake game process leaves (None: no run.meta at all)."""
    root = tempfile.mkdtemp(prefix="smoke_selftest_")
    runner = fake_runner_script(root)
    game_files, binary = fake_title(root)
    env = dict(os.environ)
    env["FAKE_EXIT"] = str(exit_code)
    env["FAKE_META"] = meta_text if meta_text is not None else ""
    cmd = [sys.executable, script, "--binary", binary, "--game-files", game_files, "--runner", runner,
           "--timeout", "5"] + list(extra_args)
    proc = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = proc.stdout.decode("utf-8", "replace")
    # a FAIL verdict keeps its artifacts for a human; the self-test's are fakes
    for line in out.splitlines():
        if line.startswith("artifacts kept: "):
            for path in line[len("artifacts kept: "):].split():
                shutil.rmtree(path, ignore_errors=True)
                try:
                    os.remove(path)
                except OSError:
                    pass
    shutil.rmtree(root, ignore_errors=True)
    return proc.returncode, out


def expect(cases, script):
    """cases: (name, exit_code, meta_text, extra_args, expected_rc, required_substring). Prints one
    line per case; returns True when every case matched."""
    ok = True
    for name, exit_code, meta_text, extra_args, want_rc, want_text in cases:
        rc, out = run_smoke(script, exit_code, meta_text, extra_args)
        good = rc == want_rc and (want_text in out)
        print("  %s %s: smoke exit=%d (want %d)%s" % ("PASS" if good else "FAIL", name, rc, want_rc,
                                                      "" if want_text in out else ", missing %r" % want_text))
        if not good:
            for line in out.splitlines():
                print("      | " + line)
            ok = False
    return ok
