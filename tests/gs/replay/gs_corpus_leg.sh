#!/usr/bin/env bash
# GsCorpus_<cap> -- one corpus capture replayed through the NEW gs_replay (bounded), its
# REPLAY_HASH compared to the frozen baseline `<cap>.replay.txt` (or any adjudicated
# alternate in baseline/ALLOWLIST.txt) and its .gsrep compared byte for byte to the
# frozen `<cap>.gsrep`. Exit 77 (SKIP) when the corpus or the baseline is absent.
#   gs_corpus_leg.sh <run_bounded.sh> <gs_replay> <corpus_dir> <cap> <timeout_s> <workdir>
set -u
RB="$1"; TOOL="$2"; CORPUS="$3"; CAP="$4"; BOUND="$5"; WORK="$6"
GSDL="$CORPUS/$CAP.gsdl"; BASE="$CORPUS/baseline/$CAP.replay.txt"; BREP="$CORPUS/baseline/$CAP.gsrep"
if [ ! -f "$GSDL" ] || [ ! -f "$BASE" ]; then echo "SKIP: corpus capture $CAP or its baseline absent"; exit 77; fi
mkdir -p "$WORK"
LOG="$WORK/$CAP.replay.log"; REP="$WORK/$CAP.gsrep"
rm -f "$LOG" "$REP"
bash "$RB" "$BOUND" "$LOG" -- "$TOOL" replay "$GSDL" --quiet --out "$REP"
rc=$?
if [ "$rc" -eq 77 ]; then echo "SKIP: no GL context"; exit 77; fi
if [ "$rc" -ne 0 ]; then echo "FAIL: gs_replay exit $rc (bounded)"; tail -5 "$LOG"; exit 1; fi
NEW=$(grep -o 'REPLAY_HASH=0x[0-9a-f]*' "$LOG" | head -1 | cut -d= -f2)
OLD=$(grep -o 'REPLAY_HASH=0x[0-9a-f]*' "$BASE" | head -1 | cut -d= -f2)
ALT=$(grep -E "^$CAP " "$CORPUS/baseline/ALLOWLIST.txt" 2>/dev/null | awk '{print $2}')
echo "OLD $(grep 'REPLAY_HASH' "$BASE")"
echo "NEW $(grep 'REPLAY_HASH' "$LOG")"
ok=0
[ -n "$NEW" ] && [ "$NEW" = "$OLD" ] && ok=1
for a in $ALT; do [ "$NEW" = "$a" ] && ok=1 && echo "NOTE: matches ALLOWLIST alternate $a"; done
if [ "$ok" -ne 1 ]; then echo "FAIL: REPLAY_HASH differs from the frozen baseline"; exit 1; fi
if [ -f "$BREP" ] && [ "$NEW" = "$OLD" ]; then
    if ! cmp -s "$REP" "$BREP"; then echo "FAIL: .gsrep differs from the frozen baseline"; exit 1; fi
    echo ".gsrep byte-identical to the baseline"
fi
echo "GsCorpus_$CAP PASS"
