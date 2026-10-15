#!/usr/bin/env bash
# GsReplayDeterminism -- two bounded replays of one capture on one build print the same
# REPLAY_HASH and write byte-identical .gsrep artifacts (replay-vs-replay is STRICT).
#   gs_replay_determinism.sh <run_bounded.sh> <gs_replay> <corpus_dir> <cap> <timeout_s> <workdir>
set -u
RB="$1"; TOOL="$2"; CORPUS="$3"; CAP="$4"; BOUND="$5"; WORK="$6"
GSDL="$CORPUS/$CAP.gsdl"
if [ ! -f "$GSDL" ]; then echo "SKIP: corpus capture $CAP absent"; exit 77; fi
mkdir -p "$WORK"
for i in 1 2; do
    rm -f "$WORK/det$i.log" "$WORK/det$i.gsrep"
    bash "$RB" "$BOUND" "$WORK/det$i.log" -- "$TOOL" replay "$GSDL" --quiet --out "$WORK/det$i.gsrep"
    rc=$?
    if [ "$rc" -eq 77 ]; then echo "SKIP: no GL context"; exit 77; fi
    if [ "$rc" -ne 0 ]; then echo "FAIL: replay $i exit $rc"; exit 1; fi
    grep 'REPLAY_HASH' "$WORK/det$i.log"
done
H1=$(grep -o 'REPLAY_HASH=0x[0-9a-f]*' "$WORK/det1.log"); H2=$(grep -o 'REPLAY_HASH=0x[0-9a-f]*' "$WORK/det2.log")
[ -n "$H1" ] && [ "$H1" = "$H2" ] || { echo "FAIL: REPLAY_HASH differs between runs"; exit 1; }
cmp -s "$WORK/det1.gsrep" "$WORK/det2.gsrep" || { echo "FAIL: .gsrep differs between runs"; exit 1; }
echo "GsReplayDeterminism PASS"
