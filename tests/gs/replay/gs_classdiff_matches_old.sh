#!/usr/bin/env bash
# GsClassdiffMatchesOld -- `gs_replay classdiff <cap> <cap> --seq` (CPU-only) is clean
# (exit 0, equal=1, no sequence divergence) and its per-class draw / vertex totals equal
# the frozen baseline replay's CLASS lines.
#   gs_classdiff_matches_old.sh <gs_replay> <corpus_dir> <cap> <workdir>
set -u
TOOL="$1"; CORPUS="$2"; CAP="$3"; WORK="$4"
GSDL="$CORPUS/$CAP.gsdl"; BASE="$CORPUS/baseline/$CAP.replay.txt"
if [ ! -f "$GSDL" ] || [ ! -f "$BASE" ]; then echo "SKIP: corpus capture $CAP or its baseline absent"; exit 77; fi
mkdir -p "$WORK"
OUT="$WORK/$CAP.classdiff.log"
"$TOOL" classdiff "$GSDL" "$GSDL" --seq > "$OUT" 2>&1
rc=$?
cat "$OUT"
if [ "$rc" -ne 0 ]; then echo "FAIL: classdiff exit $rc"; exit 1; fi
grep -q 'SUMMARY classes=[0-9]* draws_a=[0-9]* draws_b=[0-9]* equal=1$' "$OUT" || { echo "FAIL: SUMMARY not equal=1"; exit 1; }
grep -q 'SEQ len_a=[0-9]* len_b=[0-9]* first_div=-1 mismatches=0$' "$OUT" || { echo "FAIL: SEQ not clean"; exit 1; }
sed -n 's/^\[GSCLASSDIFF\] class=\([^ ]*\) draws_a=\([0-9]*\) draws_b=[0-9]* d_draws=[-0-9]* verts_a=\([0-9]*\) .*/CLASS \1 draws=\2 verts=\3/p' "$OUT" > "$WORK/$CAP.class.new"
grep -E '^\[GSREPLAY\] CLASS ' "$BASE" | sed 's/^\[GSREPLAY\] //' > "$WORK/$CAP.class.old"
if ! diff -u "$WORK/$CAP.class.old" "$WORK/$CAP.class.new"; then echo "FAIL: per-class totals differ from the baseline CLASS lines"; exit 1; fi
echo "classes compared: $(wc -l < "$WORK/$CAP.class.new")"
echo "GsClassdiffMatchesOld PASS"
