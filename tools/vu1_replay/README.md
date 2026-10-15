# `tools/vu1_replay/`

**Phase:** 5 (stage 5C part 2; `--hle` / `--catalog` / full `--dump-kicks` in 5D)
**Purpose:** the VU1 corpus oracle. One binary (`build/tools/vu1_replay/vu1_replay`) that
replays a recorded VU1 program run (a `Vu1Case` file, `engine/vu/vu1_case.h`) on a FRESH
`VuUnit` through the interpreter tier or the cached tier and proves the two tiers identical
(16 KB post-run data memory + the whole 608-byte `VUregs`). Its `--compare` and `--mode`
outputs are compared textually / byte-for-byte against the frozen OLD tool by
`compare_vs_old.py` — that identity is Phase 5 gate G2.

## Contents

- `vu1_replay.cpp` — `main` + free functions (zero primary types). Grammar (usage on stderr, exit 2):
  - `<case.bin> [trace_out] [postdata_out]` — interpreter replay; `trace_out` accepted, no trace is written.
  - `--selftest` — fabricated case round-trip (WriteCase/ReadCase), two interp replays identical,
    interp == cached, transform ground truth (`TPC 0x38`, `VI01 5`, `VF09`). Exit 0 / 1.
  - `--mode interp|cached <case> [postdata_out]` — `replayed pc=… mode=… -> TPC=… Q=… vi01=… vf01=…`;
    with `postdata_out` the raw data memory and the `<postdata_out>.regs` 608-byte sidecar.
  - `--compare [--compare-cycle] [--expect-aot|--expect-noaot] <case | --fabricate>` — interp leg then
    cached leg, each on a fresh unit; `COMPARE PASS|FAIL: pc=0x%04x data_diff_bytes=%zu/16384
    regs_identical|regs_DIFFER aot_dispatches=0 (interp vs cached)` (+ per-byte / per-register
    detail lines on FAIL). `--compare-cycle` also requires the final VU clock and the kicked
    register-op stream (count + hash) to agree. `--expect-aot` → exit 77 (no native tier exists);
    `--expect-noaot` is always satisfied. Absent case file → exit 77; corrupt → exit 2.
  - `--bench N <case>` / `--bench-cached N <case>` — one unit per mode reused over N runs, only
    `Execute` timed; `BENCH mode=… runs=… total=… ms avg=… us/run bundles/run=… ns/bundle=… aot=0`
    and `CENSUS entries=<program table size>`.
  - `--dump-kicks <case> [out.txt]` — interp run; the `image=` header, `kicks= bytes= writes= tags=
    errors=`, then the decoded GIF stream (`GsPacketDiff::Print`: every tag, every canonical write).
    Exit 1 on a decode error.
  - `--gsops <case>` — one seed line `<case basename> <op count> <op hash>` (the Phase-6 seed).
  - `--cycle-cap N` (anywhere on the line) — bundles before a runaway program is cut (default
    1,000,000; the OLD `PS2_VU1_CYCLE_CAP` TEST-ONLY variable became this option).
  - `--hle [--hle-ref] [--skip-unregistered] [--exact] [--tol-xy N] [--tol-z N] [--tol-rgba N]
    [--tol-st F] [--strict-packets] [--max-lines N] <case>` — the title library's program for the
    case's (image, variant, entry) versus a fresh-unit interpreter leg: the `image=… dispatch=…`
    header, then `HLE PASS|FAIL: <program> vs interp -- writes A= B= vertices A= B= kicks A= B=
    packets A= B= mismatches= data=ok|MISMATCH regs=ok|MISMATCH|n/a direct=ok|fallback|MISMATCH|n/a
    (tol xy= z= rgba= st=)` (+ indented mismatch detail lines). Kicks are lowered by
    `GsPacketDiff::Decode` and diffed under the tolerance; `direct=` requires the typed-op projection
    (`GsDirectOpsCheck`) of every HLE kick to equal the byte decode exactly; `data=` requires entry
    data + the claimed `data_writes` to equal the interpreter's post-run memory; `regs=` compares the
    register file when the program claims it. Unregistered image → `HLE SKIP|FAIL: no HLE
    registered …` exit 77 / 1; a program decline → `HLE FALLBACK: …` exit 1. `--hle-ref` appends the
    interpreter itself as the wildcard program. The library program is called DIRECTLY (as OLD did).
  - `--catalog <dir | case…>` — one row per (image, entry): `image image_hash variant_hash entry cases
    kicks verts bundles bundle% extent dispatch prefix hle / prims` (interp leg for kicks / vertices /
    prims, cached leg for `bundles`), sorted by bundles descending, `CATALOG cases= images=
    hle_covered= bundle_coverage=` footer. Diff-empty against the frozen OLD catalog (gate G4).
  - `--registry` — `REGISTRY entries=223 distinct_variant_hashes=176` (the library self-report), then one
    row per program name in first-registration order (`entries= distinct_hashes= entry_pcs=`) and
    `REGISTRY programs=12`.
- `gs_packet_diff.h/.cpp` — `GsPacketDiff`: GIF packet → canonical `(register, value)` writes
  (`Decode`/`DecodePacket`, PACKED / REGLIST / IMAGE, the per-packet Q latch), `Normalize`,
  `Diff` under a `Tolerance`, `Format`, `Print`, `RegName`; the frozen oracle grammar.
- `gs_direct_ops_check.h/.cpp` — `GsDirectOpsCheck`: `PacketToOps` (the renderer direct path's
  typed-op conversion), `OpsToWrites`, `Matches` — the `direct=` proof.
- `hle_gate.py` — the ctest gate driver: `--hle --skip-unregistered` over a corpus directory, every
  case of the named `--programs` must PASS (`HLE-GATE cases= judged= pass= fail= unregistered=
  other_programs=`; exit 77 without the corpus).
- `hle_vs_old.py` — the G3 identity driver (ctest `Vu1ReplayHleCorpus`): NEW `--hle --skip-unregistered`
  on every corpus case, stdout lines + exit code equal to the frozen OLD tool's block (an `### CASE` /
  `### EXIT` log via `--old-log`, else the OLD binary run with cwd pinned to `--old-cwd`); prints
  `SAME|DIFF <case>` and `HLE cases=126 same=126 diff=0`; exit 0 iff `diff=0`, 77 when OLD / corpus absent.
- `catalog_vs_old.py` — the G4 identity driver (ctest `Vu1CatalogIdentity`): OLD and NEW `--catalog` over
  the corpus, `[VU-MODE]` skipped, unified diff must be empty (`CATALOG DIFF EMPTY (66 lines)`); both
  footers printed; exit 0 / 1 / 77.
- `xgk_capture_sink.h/.cpp` — `XgkCaptureSink : GsSink`: records every PATH1 packet verbatim
  through `GifArbiter::SetSubmitObserver` and forwards the drained stream into a
  `RegisterOpStream` (count + hash).
- `compare_vs_old.py` — the driver (sequential, every process through `verify/run_bounded.sh`,
  OLD cwd pinned under `--out`): `--compare` lines identical after dropping `[VU-MODE]` /
  `[run_bounded]`; `--postdata N|all` additionally `cmp`s the `--mode interp|cached` dumps per
  tier. Prints `CMP <case> SAME|DIFF old_exit= new_exit=` and
  `CMP cases=N same=M diff=K old_fail=A new_fail=B`; exit 0 iff `diff=0`, 77 when OLD is absent.
- `sweep_vs_old.sh` — the G5 sweep (ctest `Vu1CollectSweep`, 5H): `sweep_vs_old.sh OUT_DIR [CHUNK]
  [--old-root DIR] [--new-bin FILE] [--expect N] [--budget S]` sorts every `.bin` of the OLD tree's
  `vutrace_collect` corpus (3,293 cases), splits it into 200-case chunks (symlinked per-chunk dirs so
  `compare_vs_old.py --cases DIR` is reused unchanged), runs ONE bounded driver call per chunk
  (`verify/run_bounded.sh 3000`, OLD cwd pinned under `OUT_DIR`), writes `logs/chunk_NN.log` +
  `logs/chunk_NN.done` (`exit=N`) + `progress.txt`, touches `SWEEP_DONE`, then calls the aggregate.
  Resumable: a re-run with the same `OUT_DIR` skips every chunk that has a `.done`. Exit 0 iff the
  aggregate passes, 77 when the OLD tool or the corpus is absent, 1 on any DIFF / incomplete chunk.
- `aggregate_sweep.py` — sums the `CMP cases=` footers over `--logs/chunk_*.log`, requires one footer
  and a `.done` per chunk, prints `SWEEP cases=N same=M diff=K old_fail=A new_fail=B chunks=C`, lists
  every `DIFF <case>` row (and copies the cases into `--diffs` when `--corpus` is given, plus
  `DIFF_CASES.txt`); exit 0 iff `diff=0`, every chunk complete and `cases == --expect` (when given);
  1 otherwise; 77 with no chunk log.
- `CMakeLists.txt` — `ps2r_vu1_replay_lib` (the diff / direct-ops / capture TUs, shared with
  `tests/vu`) and `vu1_replay` at `-O2 -fno-strict-aliasing -fwrapv -Wall -Wextra -Werror`, linked to
  the lib and `ps2r_title_vu1_library` (both carry `ps2r_vu` → `ps2r_gif`, `ps2r_ee`, `ps2r_core`).

## Belongs here

The tool's `main`, its GS-side capture sink, the OLD-vs-NEW driver, and (5D) the GIF packet
diff / direct-ops check the `--hle` arm needs.

## Does not belong here

Engine semantics (tiers, decoder, stall model — `engine/vu/`), the title's VU1 HLE library
(`games/<title>/vu1_library/`), the harness lanes (`verify/`), any `getenv`.

## OLD provenance

`tests/vu1_replay.cpp` (grammars, `InstallCase`, `RunCaseCapture`, `Compare`, `BenchMode`,
`FabricateCase`; `:545-975` `HleCompare` / `DumpKicks` / `Catalog` / `ReferenceInterpreter`,
`:1005-1056` the `--hle` option scan), `host_app/hle/gs_packet.h` (`gsp::` decoder / differ →
`GsPacketDiff`), `host_app/hle/gs_direct_ops.h` (`PacketToOps` → `GsDirectOpsCheck`),
`tests/test_gs_packet.cpp` (→ `tests/vu/test_gs_packet.cpp`), `tests/vu1_test_shims.cpp` (the PATH1 tap — here the arbiter's submit
observer), `tests/vu1_xgk_capture.h`, `tests/CMakeLists.txt` (`Vu1ReplaySelfTest`,
`Vu1CachedCompare_*`). The OLD `--selftest` asserted a VUT trace file (diagnostics not
transplanted); this tree's selftest asserts replay determinism + tier identity instead.
