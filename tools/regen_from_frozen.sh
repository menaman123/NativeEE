#!/usr/bin/env bash
# regen_from_frozen.sh -- regenerate games/<title>/recomp/ from the FROZEN original
# recompiler (PHASE3_PLAN section 3.1 = CONSTRUCTION_PLAN A.1, executable form).
#
#   tools/regen_from_frozen.sh              two bounded runs into build/regen/{a,b}, `diff -r`
#                                           must be empty, then a/ is copied into the title's
#                                           recomp/ dir and MANIFEST.sha256 + MANIFEST.md written
#   tools/regen_from_frozen.sh --out DIR    one bounded run into DIR (used by the two-run mode)
#   tools/regen_from_frozen.sh --check [DIR]
#                                           re-hash the generated recomp output against the title's
#                                           MANIFEST.sha256 (ctest RecompManifestIntact); exit 1 on any
#                                           change. DIR = the directory holding the generated files
#                                           (the build's PS2R_RECOMP_GEN_DIR, <build>/generated/games/
#                                           <title>/recomp); default: the title's recomp/ in the tree
#                                           (a developer's regenerated copy -- the generated files are
#                                           NOT committed; see cmake/TitleInputs.cmake)
#   tools/regen_from_frozen.sh --iop        STREAM.IRX (CONSTRUCTION A.1 "STREAM.IRX (Phase 4)"):
#                                           two bounded runs of the frozen iop_recompiler_tool into
#                                           build/regen_iop/{a,b}, diff empty, BOTH outputs byte-identical
#                                           to the frozen OLD build-v2 md5s, then a/ is installed into
#                                           games/<title>/iop/stream_irx/ + MANIFEST.sha256/.md + INVOCATION.md
#   tools/regen_from_frozen.sh --check --iop [DIR]
#                                           re-hash the generated iop_recompiled.{h,cpp} (DIR, default the
#                                           tree's iop/stream_irx/) against its MANIFEST.sha256
#                                           (ctest IopRecompManifestIntact)
#
# Inputs (the game's files are never in the tree): the title ELF is $NATIVEEE_TITLE_ELF (else
# $PS2R_OLD_ROOT/game_files/example_title.rep/SLUS_123.45), STREAM.IRX is $NATIVEEE_STREAM_IRX
# (else $PS2R_OLD_ROOT/game_files/STREAM.IRX); the IOP tool is the in-tree build
# build/tools/iop_recompiler/ps2r_iop_recompiler (else $PS2R_IOP_TOOL). The first title's regeneration
# passes --legacy-title-stubs: the in-tree tool's generic discovery (relocation-based entries, table
# exclusion, --base/--namespace/--import-call) is off, the frozen tool's first-title facts are on, so the
# output stays byte-identical; the frozen OLD tool ignores unknown arguments. The stub lists are the
# committed games/<title>/recomp/inputs/ (else OLD's tmp_analysis/).
#   tools/regen_from_frozen.sh --tool BIN ...
#                                           run BIN instead of the default tool. The DEFAULT tool (DP-23,
#                                           since P8.9-A) is NEW's own build of the recompiler,
#                                           build/tools/recompiler_v2/ps2r_recompiler_v2 (sources under
#                                           tools/recompiler_v2/, provenance in its PROVENANCE.md); its
#                                           md5 is reported, not gated. Same cwd/stub/bound protocol.
#   tools/regen_from_frozen.sh --frozen-old ...
#                                           run OLD's frozen build-v2-nog/recompiler_tool_v2 (md5-gated)
#                                           -- the pre-P8.9-A generation, kept for proofs/diffs only
#                                           (it carries BUG-072: a function split at its delay slot).
#
# Rules honoured (STAGE_RULES / REFERENCE_FREEZE): the OLD tree is read only -- its BINARY is
# executed, nothing under OLD is written. The tool opens `logs/recompiler_tool_v2/runtime_log.txt`
# RELATIVE TO CWD (main_v2.cpp:480-484) and looks the stub files up in cwd first
# (recompiler.cpp:8003-8021), so cwd is PINNED to a NEW-side scratch directory that holds copies
# of the two stub inputs. Every tool run goes through verify/run_bounded.sh.
set -u

OLD="${PS2R_OLD_ROOT:-}"   # the frozen OLD tree (read-only); only needed without NATIVEEE_TITLE_ELF / NATIVEEE_STREAM_IRX
NEW="$(cd "$(dirname "$0")/.." && pwd)"
TITLE="${PS2R_TITLE:-<title>}"
TOOL="$NEW/build/tools/recompiler_v2/ps2r_recompiler_v2"
TOOL_IS_FROZEN=0
if [ "${1:-}" = "--frozen-old" ]; then
  TOOL="$OLD/build-v2-nog/recompiler_tool_v2"; TOOL_IS_FROZEN=1; shift 1
elif [ "${1:-}" = "--tool" ]; then
  [ -n "${2:-}" ] || { echo "usage: regen_from_frozen.sh --tool BIN [--out DIR]" >&2; exit 2; }
  TOOL="$2"; TOOL_IS_FROZEN=0; shift 2
  case "$TOOL" in /*) ;; *) TOOL="$PWD/$TOOL" ;; esac   # run_one cds into the scratch cwd
fi
ELF="${NATIVEEE_TITLE_ELF:-$OLD/game_files/example_title.rep/SLUS_123.45}"
if [ -f "$NEW/games/$TITLE/recomp/inputs/tiny_stubs.txt" ]; then
  STUBS_SRC="$NEW/games/$TITLE/recomp/inputs"
else
  STUBS_SRC="$OLD/tmp_analysis"
fi
OUT="$NEW/games/$TITLE/recomp"
WORK="$NEW/build/regen"
BOUND="$NEW/verify/run_bounded.sh"
TIMEOUT_S=1800
# The shard split is a GENERATOR setting (the tool's --shards N; recompiler.cpp
# write_sharded_cpp_files packs the address-ordered bodies into N TUs by instruction
# weight). Changing it moves only the TU boundaries: the concatenated bodies are
# byte-identical across counts (tools/recomp_shard_bodies_sha.py proves it, and the
# installed MANIFEST.md records the bodies hash). 32 is the installed split; the
# per-TU compile-time table lives in docs/design/PLATFORMS.md.
SHARDS="${PS2R_RECOMP_SHARDS:-32}"
case "$SHARDS" in ''|*[!0-9]*) echo "regen: PS2R_RECOMP_SHARDS must be a positive integer (got '$SHARDS')" >&2; exit 2 ;; esac
# The registration table of initialize_recompiled_functions() (8,857 entries x 2 lambdas) is
# emitted as PS2R_RECOMP_INIT_PARTS file-static part functions (the tool's --init-parts P;
# recompiler.cpp write_cpp_tail): as ONE function body GCC 13 needs > 9 GiB at -O0 and is
# killed on an 8-18 GiB host (CHARTER2_B1.md); 32 parts of <= 277 entries compile in < 1 GiB.
# Registration order and every statement are unchanged; the shard bodies are untouched
# (the bodies hash below is independent of both knobs). 1 = the classic single body.
INIT_PARTS="${PS2R_RECOMP_INIT_PARTS:-32}"
case "$INIT_PARTS" in ''|*[!0-9]*) echo "regen: PS2R_RECOMP_INIT_PARTS must be a positive integer (got '$INIT_PARTS')" >&2; exit 2 ;; esac

# Frozen provenance (CONSTRUCTION A.1). A mismatch means the oracle moved: stop, do not emit.
EXPECT_TOOL_MD5="6235a2f9e08121b4b694a870dcffc95c"
EXPECT_ELF_MD5="36583bd799a803cae7b8f2a7b2ca6592"
EXPECT_STUBS_MD5="045de1a51dadfd46ae58609bcd0ea2e8"
EXPECT_RETURNERS_MD5="b6537d0d014818ea4915a8538aaa9ddd"

md5_of() { md5 -q "$1" 2>/dev/null || md5sum "$1" | cut -d' ' -f1; }

check_input() {  # $1 path, $2 expected md5, $3 label
  if [ ! -f "$1" ]; then echo "regen: missing $3: $1" >&2; exit 3; fi
  local got; got="$(md5_of "$1")"
  if [ "$got" != "$2" ]; then
    echo "regen: $3 md5 $got != frozen $2 ($1)" >&2; exit 3
  fi
}

# ---------------------------------------------------------------------------------------------
# STREAM.IRX (CONSTRUCTION A.1, "STREAM.IRX (Phase 4)"; PHASE4_PLAN 2.10 / 4.2).
# The frozen build-v2 iop_recompiler_tool has no cwd-relative reads or writes (only the two
# --output-* ofstreams), but cwd is pinned inside NEW/build/regen_iop for hygiene all the same.
# Unlike the EE tool, an OLD byte oracle EXISTS here: build-v2/iop_recompiled.{h,cpp} (= build-v2-nog).
# ---------------------------------------------------------------------------------------------
IOP_TOOL="${PS2R_IOP_TOOL:-$NEW/build/tools/iop_recompiler/ps2r_iop_recompiler}"
IOP_IRX="${NATIVEEE_STREAM_IRX:-$OLD/game_files/STREAM.IRX}"
IOP_OUT="$NEW/games/$TITLE/iop/stream_irx"
IOP_WORK="$NEW/build/regen_iop"
IOP_TIMEOUT_S=600
EXPECT_IOP_TOOL_MD5="32a0c90ce604ddaaf1ba089bf2489c08"
EXPECT_IRX_MD5="d24082fd3560959a77d18ae9c43db51b"
EXPECT_IRX_BYTES=116156
EXPECT_IOP_H_MD5="c95059f165f8b68de442169ac932f871"
EXPECT_IOP_CPP_MD5="b2102157c7dfa8285f21eadbb42c41a4"

iop_run_one() {  # $1 = output dir (absolute)
  local dir="$1"
  rm -rf "$dir"; mkdir -p "$dir/cwd"
  ( cd "$dir/cwd" && "$BOUND" "$IOP_TIMEOUT_S" "$dir/iop_gen.log" -- \
      "$IOP_TOOL" --input "$IOP_IRX" --output-header "$dir/iop_recompiled.h" --output-cpp "$dir/iop_recompiled.cpp" \
      --legacy-title-stubs )
  local rc=$?
  if [ "$rc" -ne 0 ]; then echo "regen --iop: tool exit $rc (see $dir/iop_gen.log)" >&2; return "$rc"; fi
  return 0
}

if [ "${1:-}" = "--check" ] && [ "${2:-}" = "--iop" ]; then
  IOP_CHECK_DIR="${3:-$IOP_OUT}"
  if [ ! -f "$IOP_OUT/MANIFEST.sha256" ]; then echo "regen --check --iop: no $IOP_OUT/MANIFEST.sha256" >&2; exit 1; fi
  if [ ! -f "$IOP_CHECK_DIR/iop_recompiled.cpp" ]; then echo "regen --check --iop: no generated iop_recompiled.cpp in $IOP_CHECK_DIR (build the title first; the generated output is not committed)" >&2; exit 1; fi
  ( cd "$IOP_CHECK_DIR" && shasum -a 256 -c "$IOP_OUT/MANIFEST.sha256" --quiet ) && { echo "regen --check --iop: $IOP_CHECK_DIR matches $IOP_OUT/MANIFEST.sha256"; exit 0; }
  echo "regen --check --iop: $IOP_CHECK_DIR DIFFERS from $IOP_OUT/MANIFEST.sha256" >&2; exit 1
fi

if [ "${1:-}" = "--iop" ]; then
  if [ -n "${PS2R_IOP_TOOL:-}" ] || [ "$IOP_TOOL" = "$NEW/build/tools/iop_recompiler/ps2r_iop_recompiler" ]; then
    [ -x "$IOP_TOOL" ] || { echo "regen --iop: tool $IOP_TOOL is not an executable (build it: cmake --build --preset macos-dev --target ps2r_iop_recompiler)" >&2; exit 3; }
    echo "regen --iop: tool $IOP_TOOL md5 $(md5_of "$IOP_TOOL") (NEW-built IOP recompiler, tools/iop_recompiler/; md5 reported, not gated)"
  else
    check_input "$IOP_TOOL" "$EXPECT_IOP_TOOL_MD5" "frozen iop_recompiler_tool (build-v2)"
  fi
  check_input "$IOP_IRX" "$EXPECT_IRX_MD5" "STREAM.IRX"
  irx_bytes="$(stat -f %z "$IOP_IRX")"
  if [ "$irx_bytes" -ne "$EXPECT_IRX_BYTES" ]; then echo "regen --iop: STREAM.IRX is $irx_bytes bytes, frozen $EXPECT_IRX_BYTES" >&2; exit 3; fi
  mkdir -p "$IOP_WORK"
  t0=$(date +%s)
  iop_run_one "$IOP_WORK/a" || exit $?
  t1=$(date +%s)
  iop_run_one "$IOP_WORK/b" || exit $?
  t2=$(date +%s)
  echo "regen --iop: run a $((t1-t0)) s, run b $((t2-t1)) s"
  if ! diff -r --exclude=cwd --exclude=iop_gen.log "$IOP_WORK/a" "$IOP_WORK/b" > "$IOP_WORK/determinism.diff"; then
    echo "regen --iop: NON-DETERMINISTIC output -- see $IOP_WORK/determinism.diff" >&2; exit 4
  fi
  echo "regen --iop: determinism: diff -r a b is EMPTY"
  # Byte oracle: both emitted files must equal the frozen OLD build-v2 output.
  got_h="$(md5_of "$IOP_WORK/a/iop_recompiled.h")"; got_cpp="$(md5_of "$IOP_WORK/a/iop_recompiled.cpp")"
  if [ "$got_h" != "$EXPECT_IOP_H_MD5" ] || [ "$got_cpp" != "$EXPECT_IOP_CPP_MD5" ]; then
    echo "regen --iop: BYTE ORACLE MISMATCH: iop_recompiled.h $got_h (frozen $EXPECT_IOP_H_MD5), iop_recompiled.cpp $got_cpp (frozen $EXPECT_IOP_CPP_MD5)" >&2; exit 6
  fi
  echo "regen --iop: byte oracle OK: iop_recompiled.h md5 $got_h, iop_recompiled.cpp md5 $got_cpp (= OLD build-v2 = build-v2-nog)"
  mkdir -p "$IOP_OUT"
  rm -f "$IOP_OUT/iop_recompiled.h" "$IOP_OUT/iop_recompiled.cpp"
  cp "$IOP_WORK/a/iop_recompiled.h" "$IOP_WORK/a/iop_recompiled.cpp" "$IOP_OUT/"
  N_GETENV="$(grep -c 'getenv(' "$IOP_OUT/iop_recompiled.cpp")"
  N_STRMQ="$(grep -c 'getenv("PS2_STRMQ")' "$IOP_OUT/iop_recompiled.cpp")"
  N_FUN="$(grep -cE '^void iop_func_[0-9a-f]{8}\(IopContext& iop_ctx\);' "$IOP_OUT/iop_recompiled.h" || true)"
  ( cd "$IOP_OUT" && shasum -a 256 iop_recompiled.h iop_recompiled.cpp > MANIFEST.sha256 )
  {
    echo "# MANIFEST -- games/$TITLE/iop/stream_irx (generated; do not edit by hand)"
    echo
    echo "Generated $(date -u +%Y-%m-%dT%H:%M:%SZ) by tools/regen_from_frozen.sh --iop (two bounded runs, diff -r empty, byte oracle matched)."
    echo
    echo "| Input | md5 |"; echo "|---|---|"
    echo "| iop_recompiler_tool (\`OLD/build-v2\`, frozen) | $(md5_of "$IOP_TOOL") |"
    echo "| STREAM.IRX (\`OLD/game_files\`, $irx_bytes bytes) | $(md5_of "$IOP_IRX") |"
    echo
    echo "| Output file | bytes | md5 | frozen OLD build-v2 md5 |"; echo "|---|---|---|---|"
    echo "| iop_recompiled.h | $(stat -f %z "$IOP_OUT/iop_recompiled.h") | $got_h | $EXPECT_IOP_H_MD5 |"
    echo "| iop_recompiled.cpp | $(stat -f %z "$IOP_OUT/iop_recompiled.cpp") | $got_cpp | $EXPECT_IOP_CPP_MD5 |"
    echo
    echo "SHA-256 per file: MANIFEST.sha256 (checked by ctest IopRecompManifestIntact via --check --iop)."
    echo "\`getenv\` sites in iop_recompiled.cpp: $N_GETENV (of which \`PS2_STRMQ\` $N_STRMQ) -- emitted by the frozen tool; GENERATED_EXEMPT in tests/core/check_getenv_sites.py until 9.1."
  } > "$IOP_OUT/MANIFEST.md"
  {
    echo "# INVOCATION -- how games/$TITLE/iop/stream_irx/iop_recompiled.{h,cpp} was produced (CONSTRUCTION A.1 STREAM.IRX; PHASE4_PLAN 2.10/4.2)"
    echo
    echo "Generated $(date -u +%Y-%m-%dT%H:%M:%SZ) by \`tools/regen_from_frozen.sh --iop\`. The output is NEVER hand-edited (rule 5.3)."
    echo
    echo "## Provenance"
    echo
    echo "| Item | Value |"; echo "|---|---|"
    echo "| Tool | \`OLD/build-v2/iop_recompiler_tool\` md5 \`$(md5_of "$IOP_TOOL")\` (frozen; executed, never rebuilt; \`build-v2-nog\`'s copy is a later build of the same sources) |"
    echo "| Input | \`OLD/game_files/STREAM.IRX\` md5 \`$(md5_of "$IOP_IRX")\`, $irx_bytes bytes |"
    echo "| cwd | \`NEW/build/regen_iop/{a,b}/cwd\` -- pinned (the tool has no cwd-relative reads/writes: only the two --output-* ofstreams) |"
    echo "| Bound | \`verify/run_bounded.sh $IOP_TIMEOUT_S\` per run; measured: run a $((t1-t0)) s, run b $((t2-t1)) s |"
    echo "| Byte oracle | \`OLD/build-v2/iop_recompiled.h\` md5 \`$EXPECT_IOP_H_MD5\`, \`OLD/build-v2/iop_recompiled.cpp\` md5 \`$EXPECT_IOP_CPP_MD5\` (identical in \`build-v2-nog\`) -- regenerated files MATCH: \`$got_h\` / \`$got_cpp\` |"
    echo
    echo "## Command (per run; \$DIR = build/regen_iop/a then build/regen_iop/b)"
    echo
    echo '```'
    echo "cd \$DIR/cwd"
    echo "verify/run_bounded.sh $IOP_TIMEOUT_S \$DIR/iop_gen.log -- \\"
    echo "  $IOP_TOOL --input $IOP_IRX \\"
    echo "  --output-header \$DIR/iop_recompiled.h --output-cpp \$DIR/iop_recompiled.cpp"
    echo '```'
    echo
    echo "Determinism: \`diff -r --exclude=cwd --exclude=iop_gen.log build/regen_iop/a build/regen_iop/b\` is EMPTY; a/ is installed."
    echo
    echo "## Tool report (stdout/stderr of run a)"
    echo
    echo '```'
    grep -vE '^\[run_bounded\]' "$IOP_WORK/a/iop_gen.log" | head -200
    echo '```'
    echo
    echo "## Shape"
    echo
    echo "- \`iop_recompiled.h\` $(stat -f %z "$IOP_OUT/iop_recompiled.h") bytes, \`iop_recompiled.cpp\` $(stat -f %z "$IOP_OUT/iop_recompiled.cpp") bytes; $N_FUN function declarations."
    echo "- The header includes \`../host_app/iop_context.h\`, \`../host_app/iop_memory.h\`, \`../host_app/iop_stubs.h\` by RELATIVE path: the include-compat root is \`abi/\` (\`abi/host_app/*.h\` forward to \`engine/iop/*\` and \`stub_surface.h\`)."
    echo "- \`getenv\` sites in \`iop_recompiled.cpp\`: $N_GETENV (\`PS2_STRMQ\` $N_STRMQ; FLAG_LEDGER DELETE -- OFF-unless-set dead arms, NEW sets no env). \`CoreSingleGetenvSite\` carries a dated GENERATED_EXEMPT for exactly this file until 9.1's in-tree \`iop_recompiler\` regenerates it without the probes."
  } > "$IOP_OUT/INVOCATION.md"
  echo "regen --iop: installed into $IOP_OUT (iop_recompiled.h, iop_recompiled.cpp; getenv sites $N_GETENV); manifests written"
  exit 0
fi

# --check [DIR]: re-hash the generated output (DIR; default the tree's recomp/) against the committed manifest.
# The manifest also lists continuations.h and inputs/*.txt: those are read from DIR when present there
# (the build regenerates continuations.h beside the shards), else from the tree's recomp/.
if [ "${1:-}" = "--check" ]; then
  CHECK_DIR="${2:-$OUT}"
  if [ ! -f "$OUT/MANIFEST.sha256" ]; then echo "regen --check: no $OUT/MANIFEST.sha256" >&2; exit 1; fi
  if [ ! -f "$CHECK_DIR/recompiled.cpp" ]; then echo "regen --check: no generated recompiled.cpp in $CHECK_DIR (build the title first; the generated output is not committed)" >&2; exit 1; fi
  CHECK_RC=0
  while read -r want rel; do
    [ -n "$rel" ] || continue
    if [ -f "$CHECK_DIR/$rel" ]; then f="$CHECK_DIR/$rel"; elif [ -f "$OUT/$rel" ]; then f="$OUT/$rel"; else echo "regen --check: $rel MISSING" >&2; CHECK_RC=1; continue; fi
    got="$(shasum -a 256 "$f" | cut -d' ' -f1)"
    if [ "$got" != "$want" ]; then echo "regen --check: $rel DIFFERS ($got != $want)" >&2; CHECK_RC=1; fi
  done < "$OUT/MANIFEST.sha256"
  [ "$CHECK_RC" -eq 0 ] || { echo "regen --check: $CHECK_DIR DIFFERS from $OUT/MANIFEST.sha256" >&2; exit 1; }
  echo "regen --check: $CHECK_DIR matches $OUT/MANIFEST.sha256"
  # The split-independent bodies hash MANIFEST.md records (absent in manifests written before
  # the shard knob existed: then only the per-file hashes are checked).
  BODIES_EXPECT="$(grep -oE '^[0-9a-f]{64}  bodies\(' "$OUT/MANIFEST.md" 2>/dev/null | head -1 | cut -c1-64)"
  if [ -n "$BODIES_EXPECT" ]; then
    python3 "$NEW/tools/recomp_shard_bodies_sha.py" "$CHECK_DIR" --check "$BODIES_EXPECT" || { echo "regen --check: bodies hash DIFFERS from MANIFEST.md" >&2; exit 1; }
  fi
  exit 0
fi

if [ "$TOOL_IS_FROZEN" = "1" ]; then
  check_input "$TOOL" "$EXPECT_TOOL_MD5" "frozen recompiler_tool_v2"
  TOOL_DESC="\`OLD/build-v2-nog/recompiler_tool_v2\` (frozen; executed, never rebuilt; pre-P8.9-A generation)"
else
  [ -x "$TOOL" ] || { echo "regen: tool $TOOL is not an executable (build it: cmake --build --preset macos-dev --target ps2r_recompiler_v2)" >&2; exit 3; }
  echo "regen: tool $TOOL md5 $(md5_of "$TOOL") (NEW-built recompiler, tools/recompiler_v2/; md5 reported, not gated)"
  TOOL_DESC="NEW's \`build/tools/recompiler_v2/ps2r_recompiler_v2\` (sources \`tools/recompiler_v2/\`, \`PROVENANCE.md\`; DP-23)"
fi
check_input "$ELF" "$EXPECT_ELF_MD5" "title ELF"
check_input "$STUBS_SRC/tiny_stubs.txt" "$EXPECT_STUBS_MD5" "tiny_stubs.txt"
check_input "$STUBS_SRC/tiny_returners.txt" "$EXPECT_RETURNERS_MD5" "tiny_returners.txt"

# One run into a directory. cwd pinned to WORK/<name>; logs/ lands there.
run_one() {  # $1 = output dir (absolute)
  local dir="$1"
  rm -rf "$dir"; mkdir -p "$dir/cwd"
  cp "$STUBS_SRC/tiny_stubs.txt" "$STUBS_SRC/tiny_returners.txt" "$dir/cwd/"
  ( cd "$dir/cwd" && "$BOUND" "$TIMEOUT_S" "$dir/recomp_gen.log" -- \
      "$TOOL" --input "$ELF" --output-header "$dir/recompiled.h" --output-cpp "$dir/recompiled.cpp" \
              --shards "$SHARDS" --init-parts "$INIT_PARTS" --direct-calls )
  local rc=$?
  if [ "$rc" -ne 0 ]; then echo "regen: tool exit $rc (see $dir/recomp_gen.log)" >&2; return "$rc"; fi
  "$BOUND" 90 "$dir/continuations.log" -- python3 "$NEW/tools/recompiler_v2/continuations.py" "$dir" || return $?
  return 0
}

if [ "${1:-}" = "--out" ]; then
  [ -n "${2:-}" ] || { echo "usage: regen_from_frozen.sh --out DIR" >&2; exit 2; }
  run_one "$2"; exit $?
fi

# Two-run determinism proof.
mkdir -p "$WORK"
t0=$(date +%s)
run_one "$WORK/a" || exit $?
t1=$(date +%s)
run_one "$WORK/b" || exit $?
t2=$(date +%s)
echo "regen: run a ${t1}-${t0}=$((t1-t0)) s, run b $((t2-t1)) s"

# The emitted files only (cwd/ holds logs + stub copies; recomp_gen.log holds the report).
if ! diff -r --exclude=cwd --exclude=recomp_gen.log --exclude=continuations.log "$WORK/a" "$WORK/b" > "$WORK/determinism.diff"; then
  echo "regen: NON-DETERMINISTIC output -- see $WORK/determinism.diff" >&2; exit 4
fi
echo "regen: determinism: diff -r a b is EMPTY ($(ls "$WORK/a" | grep -c '^recompiled') emitted files)"

# Install a/ into the title's recomp/ (the ONLY writer of that directory).
mkdir -p "$OUT/inputs"
rm -f "$OUT"/recompiled.h "$OUT"/recompiled.cpp "$OUT"/recompiled_shard_*.cpp
cp "$WORK/a"/recompiled.h "$WORK/a"/recompiled.cpp "$WORK/a"/recompiled_shard_*.cpp "$OUT/"
cp "$WORK/a/continuations.h" "$OUT/"
cp "$STUBS_SRC/tiny_stubs.txt" "$STUBS_SRC/tiny_returners.txt" "$OUT/inputs/"
# The tool's full log (121k "Recompiling block" lines) stays in build/regen; the REPORT lines
# (PHASE3_PLAN 3.2) are extracted and each expected line must be present -- else the stage fails.
REPORT="$WORK/a/report.txt"
grep -E '^\[v2\]|^Found stub file|^Auto-registered|^Successfully generated common TU|^\[W16R1\]' "$WORK/a/recomp_gen.log" > "$REPORT"
expect_line() {
  if ! grep -qF -- "$1" "$REPORT"; then echo "regen: EXPECTED REPORT LINE MISSING: $1" >&2; exit 5; fi
}
expect_line "[v2] no --ghidra-input: emitting the pure discovery set"
# P8.9-A (BUG-072): 12 delay-slot weak seeds rejected -> 8819 - 12 = 8807 functions; the same 8848 gap
# words (no code lost: every rejected seed's words are reached through the containing function).
expect_line "[v2] discovered 8807 functions, 177 jump-table sites, 8848 unreached gap words"
expect_line "[v2] delay-slot rule: 12 weak seed(s) rejected, 1 strong entry in a delay slot"
expect_line "[v2] emission model: 8807 functions (0 aliases, 0 pinned), 177 jr table sites"
expect_line "Found stub file: tiny_stubs.txt"
expect_line "Auto-registered 0 tiny 'jr ra; nop' stubs"
expect_line "Found stub file: tiny_returners.txt"
expect_line "Auto-registered 36 tiny 'return N' stubs"
expect_line "($SHARDS shard(s))"
N_FUN="$(grep -cE '^void FUN_[0-9a-f]{8}\(CpuContext& ctx' "$OUT/recompiled.h")"
N_ENTRY="$(grep -cE '^void entry\(CpuContext& ctx' "$OUT/recompiled.h")"
if [ "$N_FUN" -ne 8806 ] || [ "$N_ENTRY" -ne 1 ]; then
  echo "regen: header shape: FUN_ decls=$N_FUN (expected 8806) entry=$N_ENTRY (expected 1)" >&2; exit 5
fi
echo "regen: report lines OK; header declares $N_FUN FUN_ + $N_ENTRY entry = $((N_FUN+N_ENTRY)) functions"
( cd "$OUT" && shasum -a 256 recompiled.h recompiled.cpp continuations.h recompiled_shard_*.cpp inputs/tiny_stubs.txt inputs/tiny_returners.txt > MANIFEST.sha256 )
BODIES_SHA_LINE="$(cd "$NEW" && python3 tools/recomp_shard_bodies_sha.py "games/$TITLE/recomp")" || exit 5

# Human-readable manifest (md5 per file + provenance + date).
{
  echo "# MANIFEST -- games/$TITLE/recomp (generated; do not edit by hand)"
  echo
  echo "Generated $(date -u +%Y-%m-%dT%H:%M:%SZ) by tools/regen_from_frozen.sh (two bounded runs, diff -r empty; tool: $TOOL_DESC)."
  echo
  echo "| Input | md5 |"; echo "|---|---|"
  echo "| recompiler: $TOOL_DESC | $(md5_of "$TOOL") |"
  echo "| SLUS_123.45 (title ELF, $(stat -f %z "$ELF") bytes) | $(md5_of "$ELF") |"
  echo "| inputs/tiny_stubs.txt ($(wc -l < "$OUT/inputs/tiny_stubs.txt" | tr -d ' ') lines) | $(md5_of "$OUT/inputs/tiny_stubs.txt") |"
  echo "| inputs/tiny_returners.txt ($(wc -l < "$OUT/inputs/tiny_returners.txt" | tr -d ' ') lines) | $(md5_of "$OUT/inputs/tiny_returners.txt") |"
  echo
  echo "| Output file | bytes | md5 |"; echo "|---|---|---|"
  for f in recompiled.h recompiled.cpp $(cd "$OUT" && ls recompiled_shard_*.cpp); do
    echo "| $f | $(stat -f %z "$OUT/$f") | $(md5_of "$OUT/$f") |"
  done
  echo
  echo "Total emitted: $(cd "$OUT" && ls recompiled.h recompiled.cpp recompiled_shard_*.cpp | wc -l | tr -d ' ') files, $(cd "$OUT" && du -ch recompiled.h recompiled.cpp recompiled_shard_*.cpp | tail -1 | cut -f1)."
  echo "SHA-256 per file: MANIFEST.sha256 (checked by ctest RecompManifestIntact via --check)."
  echo
  echo "Init table: $INIT_PARTS part function(s) (PS2R_RECOMP_INIT_PARTS; the tool's --init-parts; 1 = the classic single body)."
  echo
  echo "Shard split: $SHARDS TUs (PS2R_RECOMP_SHARDS; the tool's --shards). Split-independent bodies hash"
  echo "(tools/recomp_shard_bodies_sha.py; the include prologues stripped, bodies concatenated in address order):"
  echo
  echo '```'
  echo "$BODIES_SHA_LINE"
  echo '```'
} > "$OUT/MANIFEST.md"
{
  echo "# INVOCATION -- how games/$TITLE/recomp was produced (PHASE3_PLAN 3.1/3.2; CONSTRUCTION A.1)"
  echo
  echo "Generated $(date -u +%Y-%m-%dT%H:%M:%SZ) by \`tools/regen_from_frozen.sh\`. The output is NEVER hand-edited."
  echo
  echo "## Provenance"
  echo
  echo "| Item | Value |"; echo "|---|---|"
  echo "| Tool | $TOOL_DESC md5 \`$(md5_of "$TOOL")\` |"
  echo "| ELF | \`OLD/game_files/example_title.rep/SLUS_123.45\` md5 \`$(md5_of "$ELF")\`, $(stat -f %z "$ELF") bytes |"
  echo "| inputs/tiny_stubs.txt | md5 \`$(md5_of "$OUT/inputs/tiny_stubs.txt")\` ($(wc -l < "$OUT/inputs/tiny_stubs.txt" | tr -d ' ') lines; OLD \`tmp_analysis/\`) |"
  echo "| inputs/tiny_returners.txt | md5 \`$(md5_of "$OUT/inputs/tiny_returners.txt")\` ($(wc -l < "$OUT/inputs/tiny_returners.txt" | tr -d ' ') lines) |"
  echo "| cwd | \`NEW/build/regen/{a,b}/cwd\` -- pinned; the tool opens \`logs/recompiler_tool_v2/runtime_log.txt\` relative to cwd (\`main_v2.cpp:480-484\`) and finds the stub files in cwd first (\`recompiler.cpp:8003-8021\`) |"
  echo "| Bound | \`verify/run_bounded.sh $TIMEOUT_S\` per run; measured: run a $((t1-t0)) s, run b $((t2-t1)) s |"
  echo
  echo "## Command (per run; \$DIR = build/regen/a then build/regen/b)"
  echo
  echo '```'
  echo "cd \$DIR/cwd && cp OLD/tmp_analysis/tiny_stubs.txt OLD/tmp_analysis/tiny_returners.txt ."
  echo "verify/run_bounded.sh $TIMEOUT_S \$DIR/recomp_gen.log -- \\"
  echo "  $TOOL \\"
  echo "  --input $ELF \\"
  echo "  --output-header \$DIR/recompiled.h --output-cpp \$DIR/recompiled.cpp --shards $SHARDS --init-parts $INIT_PARTS --direct-calls"
  echo "# NO --ghidra-input / --overrides / --manual-json: pure-discovery emission (main_v2.cpp:499)."
  echo '```'
  echo
  echo "Determinism: \`diff -r --exclude=cwd --exclude=recomp_gen.log build/regen/a build/regen/b\` is EMPTY; a/ is installed."
  echo
  echo "## Report lines (extracted from the tool's stdout/stderr; the script fails if an expected line is absent)"
  echo
  echo '```'
  grep -vE '^Successfully generated shard' "$REPORT"
  echo '```'
  echo
  echo "Per-shard function counts:"
  echo
  echo '```'
  grep -E '^Successfully generated shard' "$REPORT"
  echo '```'
  echo
  echo "## Shape"
  echo
  echo "- \`recompiled.h\` declares $N_FUN \`FUN_xxxxxxxx(CpuContext& ctx, uint32_t addr = 0)\` + \`entry\` = $((N_FUN+N_ENTRY)) functions (the pure-discovery set with the P8.9-A delay-slot rule -- 12 fewer than the frozen tool's 8819, BUG-072; NOT the 9,327 of the frozen \`build-v2-nog/recompiled.h\`, which is the Ghidra+overrides emission)."
  echo "- $(cd "$OUT" && ls recompiled.h recompiled.cpp recompiled_shard_*.cpp | wc -l | tr -d ' ') emitted files, $(cd "$OUT" && du -ch recompiled.h recompiled.cpp recompiled_shard_*.cpp | tail -1 | cut -f1) total (common TU $(stat -f %z "$OUT/recompiled.cpp") bytes, header $(stat -f %z "$OUT/recompiled.h") bytes)."
  echo "- \`getenv\` sites: common TU $(grep -c getenv "$OUT/recompiled.cpp"), shards $(cat "$OUT"/recompiled_shard_*.cpp | grep -c getenv) (PHASE3_PLAN 3.3: emitted by the frozen tool; OFF unless the variable is set; LintNoGetenv exemption for exactly this file)."
  echo
  echo "## Provenance of \`build-v2-nog\` (why there is no byte-compare against it)"
  echo
  echo "\`OLD/build-v2-nog/recompiled*.cpp\` were emitted WITH \`--ghidra-input\` + overrides (9,327 functions with aliases/compat/pins)."
  echo "This directory is the PURE-DISCOVERY emission of the same frozen tool over the same ELF; the two are different"
  echo "emission models by design, so the only byte-identity proof is the two-run determinism above plus \`MANIFEST.sha256\`."
} > "$OUT/INVOCATION.md"
echo "regen: installed into $OUT ($(cd "$OUT" && ls recompiled.h recompiled.cpp recompiled_shard_*.cpp | wc -l | tr -d ' ') files); manifests written"
exit 0
