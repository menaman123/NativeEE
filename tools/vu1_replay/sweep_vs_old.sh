#!/usr/bin/env bash
# sweep_vs_old.sh -- the vutrace_collect sweep: every case of the frozen OLD tree's
# collected VU1 corpus replayed by the OLD vu1_replay and this tree's tool in `--compare`
# mode, chunked, sequential, bounded, resumable (PHASE5_PLAN section 4.5, gate G5).
#
#   sweep_vs_old.sh OUT_DIR [CHUNK] [--old-root DIR] [--new-bin FILE] [--old-bin FILE]
#                   [--corpus DIR] [--expect N] [--budget SECS]
#
#   OUT_DIR      where chunks/, logs/ (chunk_NN.log + chunk_NN.done), progress.txt and
#                SWEEP_DONE are written; re-running with the same OUT_DIR skips every chunk
#                that already has a .done file (resume).
#   CHUNK        cases per chunk (default 200; one bounded driver call per chunk).
#   --old-root   the frozen OLD tree (default: the CONSTRUCTION_PLAN path); the OLD tool is
#                <old-root>/build/tests/vu1_replay and the corpus <old-root>/vutrace_collect.
#   --new-bin    this tree's tool (default: <repo>/build/tools/vu1_replay/vu1_replay).
#   --expect N   pass to aggregate_sweep.py (the corpus size the gate pins; default 3293).
#   --budget S   wall budget per chunk handed to verify/run_bounded.sh (default 3000).
#
# Exit status: 0 iff aggregate_sweep.py reports diff=0 (and cases == --expect); 77 when the
# OLD tool or the corpus is absent (ctest SKIP); 1 on any DIFF / failed chunk; 2 on usage.
# Never builds or writes into the OLD tree: the driver pins the OLD cwd under OUT_DIR.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OLD_ROOT="${PS2R_OLD_ROOT:-}"   # the frozen OLD tree (read-only); --old-root overrides
NEW_BIN="$REPO/build/tools/vu1_replay/vu1_replay"
OLD_BIN=""
CORPUS=""
EXPECT=3293
BUDGET=3000
OUT=""
CHUNK=200
positional=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --old-root) OLD_ROOT="$2"; shift 2 ;;
    --new-bin)  NEW_BIN="$2";  shift 2 ;;
    --old-bin)  OLD_BIN="$2";  shift 2 ;;
    --corpus)   CORPUS="$2";   shift 2 ;;
    --expect)   EXPECT="$2";   shift 2 ;;
    --budget)   BUDGET="$2";   shift 2 ;;
    -h|--help)  sed -n '2,22p' "$0"; exit 0 ;;
    -*) echo "sweep_vs_old.sh: unknown option $1" >&2; exit 2 ;;
    *)
      if [ "$positional" -eq 0 ]; then OUT="$1"; elif [ "$positional" -eq 1 ]; then CHUNK="$1";
      else echo "sweep_vs_old.sh: too many positional arguments" >&2; exit 2; fi
      positional=$((positional+1)); shift ;;
  esac
done
if [ -z "$OUT" ]; then
  echo "usage: sweep_vs_old.sh OUT_DIR [CHUNK] [--old-root DIR] [--new-bin FILE] [--expect N]" >&2
  exit 2
fi
[ -n "$OLD_BIN" ] || OLD_BIN="$OLD_ROOT/build/tests/vu1_replay"
[ -n "$CORPUS" ]  || CORPUS="$OLD_ROOT/vutrace_collect"

if [ ! -x "$OLD_BIN" ] || [ ! -d "$CORPUS" ]; then
  echo "SWEEP SKIP: OLD tool ($OLD_BIN) or corpus ($CORPUS) absent"
  exit 77
fi
if [ ! -x "$NEW_BIN" ]; then
  echo "sweep_vs_old.sh: NEW tool not built: $NEW_BIN" >&2
  exit 2
fi
case "$CHUNK" in ''|*[!0-9]*) echo "sweep_vs_old.sh: CHUNK must be an integer" >&2; exit 2 ;; esac

mkdir -p "$OUT/chunks" "$OUT/logs"
OUT=$(cd "$OUT" && pwd)
# The driver resolves verify/run_bounded.sh relative to the repo; run from the repo root.
cd "$REPO" || exit 2

ls "$CORPUS"/*.bin | sort > "$OUT/all_cases.lst"
TOTAL=$(wc -l < "$OUT/all_cases.lst" | tr -d ' ')
if [ "$TOTAL" -eq 0 ]; then
  echo "SWEEP SKIP: corpus $CORPUS holds no .bin case"
  exit 77
fi
if [ ! -f "$OUT/progress.txt" ]; then
  echo "sweep start $(date -u +%FT%TZ) total=$TOTAL chunk=$CHUNK" > "$OUT/progress.txt"
else
  echo "sweep resume $(date -u +%FT%TZ) total=$TOTAL chunk=$CHUNK" >> "$OUT/progress.txt"
fi
# Chunk lists are regenerated only when absent so a resume keeps the original partition.
if ! ls "$OUT"/chunks/lst_* >/dev/null 2>&1; then
  split -l "$CHUNK" -d -a 2 "$OUT/all_cases.lst" "$OUT/chunks/lst_"
fi

n=0
for lst in "$OUT"/chunks/lst_*; do
  id=$(basename "$lst" | sed 's/lst_//')
  dir="$OUT/chunks/dir_$id"
  if [ ! -f "$OUT/logs/chunk_$id.done" ]; then
    # Symlink the chunk's cases into one directory so the driver's --cases DIR form is reused.
    rm -rf "$dir"; mkdir -p "$dir"
    while read -r f; do ln -s "$f" "$dir/$(basename "$f")"; done < "$lst"
    rm -f "$OUT/logs/chunk_$id.log"
    "$REPO/verify/run_bounded.sh" "$BUDGET" "$OUT/logs/chunk_$id.log" -- \
      python3 "$HERE/compare_vs_old.py" --mode compare \
        --old-bin "$OLD_BIN" --new-bin "$NEW_BIN" \
        --cases "$dir" --out "$OUT/chunks/out_$id"
    echo "exit=$?" > "$OUT/logs/chunk_$id.done"
  fi
  n=$((n+1))
  echo "chunk $id done ($n) $(date -u +%FT%TZ) $(grep '^CMP cases=' "$OUT/logs/chunk_$id.log" | tail -1)" >> "$OUT/progress.txt"
done
echo "sweep end $(date -u +%FT%TZ)" >> "$OUT/progress.txt"
touch "$OUT/SWEEP_DONE"

python3 "$HERE/aggregate_sweep.py" --logs "$OUT/logs" --expect "$EXPECT" --diffs "$OUT/diffs" --corpus "$CORPUS"
