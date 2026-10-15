#!/usr/bin/env bash
# GsReplayCensus -- `gs_replay census <cap>` (CPU-only) prints exactly the CENSUS / PRIM /
# FLAGS / JOBS / HLE / CLASS lines the frozen baseline replay printed (tag aside).
#   gs_replay_census.sh <gs_replay> <corpus_dir> <cap> <workdir>
set -u
TOOL="$1"; CORPUS="$2"; CAP="$3"; WORK="$4"
GSDL="$CORPUS/$CAP.gsdl"; BASE="$CORPUS/baseline/$CAP.replay.txt"
if [ ! -f "$GSDL" ] || [ ! -f "$BASE" ]; then echo "SKIP: corpus capture $CAP or its baseline absent"; exit 77; fi
mkdir -p "$WORK"
"$TOOL" census "$GSDL" 2>/dev/null | sed -n 's/^\[GSCENSUS\] //p' > "$WORK/$CAP.census.new"
rc=${PIPESTATUS[0]}
if [ "$rc" -ne 0 ]; then echo "FAIL: gs_replay census exit $rc"; exit 1; fi
grep -E '^\[GSREPLAY\] (CENSUS|PRIM|FLAGS|JOBS|HLE|CLASS) ' "$BASE" | sed 's/^\[GSREPLAY\] //' > "$WORK/$CAP.census.old"
if ! diff -u "$WORK/$CAP.census.old" "$WORK/$CAP.census.new"; then echo "FAIL: census lines differ"; exit 1; fi
echo "lines compared: $(wc -l < "$WORK/$CAP.census.new")"
echo "GsReplayCensus PASS"
