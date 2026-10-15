#!/usr/bin/env bash
# GsDrawlistToolDiffClean -- `drawlist_tool diff <cap> <cap>` on a corpus capture must
# print the clean verdict in the pinned grammar and exit 0. Bounded run; exit 77 (SKIP)
# when the frozen corpus is absent.
#   drawlist_tool_diff_clean.sh <run_bounded.sh> <drawlist_tool> <capture.gsdl> <logfile>
set -u
RB="$1"; TOOL="$2"; CAP="$3"; LOG="$4"
if [ ! -f "$CAP" ]; then echo "SKIP: corpus capture $CAP absent"; exit 77; fi
rm -f "$LOG"
bash "$RB" 300 "$LOG" -- "$TOOL" diff "$CAP" "$CAP"
rc=$?
cat "$LOG"
if [ "$rc" -ne 0 ]; then echo "FAIL: drawlist_tool diff exit $rc"; exit 1; fi
WANT="DIFF PASS: frames compared=208 mismatching=0 (offset=0 tol xy=0.0625 z=1 rgba=1 st=1e-05)"
if [ "$(cat "$LOG")" != "$WANT" ]; then echo "FAIL: output differs from the pinned clean verdict"; exit 1; fi
echo "GsDrawlistToolDiffClean PASS"
