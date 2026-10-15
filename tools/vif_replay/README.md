# `tools/vif_replay/`

**Phase:** 5 (stage 5E part 2; the MTVU / runahead arms in 5G)
**Purpose:** the VIF equivalence arms. One binary (`build/tools/vif_replay/vif_replay`) that
feeds self-contained synthetic VIF1 streams through `Vif` on a fresh rig
(`RegisterOpStream ← GifParser ← GifArbiter`, `VuUnit vu0(0), vu1(1, &arbiter)`) and proves the
bulk UNPACK lanes byte-identical to the scalar reference loop. Its output is compared line for
line against the frozen OLD tool's arms by `vif_arms_vs_old.py` — that identity is Phase 5 gate
G6 (partial until 5G).

## Contents

- `vif_replay.cpp` — `main` + free functions (zero primary types). Grammar (usage on stderr, exit 2):
  - `[--vif-fast] --unpack-bulk` — 30 cases (11 CL==WL bulk-lane shapes, 5 declines, 11 skip-lane
    shapes, 3 skip-lane declines). Each case is run twice on a fresh rig: `VifUnpackSettings{bulk_lanes
    = false}` then `{true}`, same LCG seed `0xC0FFEE ^ index`, same position-sensitive canvas
    `0xA5 + (i & 0xFF)` over the 16 KB VU1 data memory, `regs(1).tops` pre-state where the case
    needs it, the stream padded to a quadword. Asserted: both legs end `Idle` with
    `pending_bytes == 0`; no lane ran on the OFF leg; a must-execute case ran exactly its lane and
    not the other; a must-decline case ran neither; the two 16 KB images are identical. Header
    `unpack-bulk equivalence selftest (vif_fast=N)` on stderr; per case
    `[UNPKB][<name>] PASS (bulk_cmds=N skip_cmds=M)` (or a `FAIL …` reason) and the summary
    `UNPKB EQUIVALENCE PASS: 30 cases, byte-identical VU1 data memory, must-execute/must-decline
    census clean` on stdout. Exit 0 / 1.
  - `--vif-fast` (anywhere on the line) — every feed goes through the whole-span parser
    (`VifSettings.fast_span = true`); without it the word latch is forced. The engine's norm is the
    span parser; the two ctest rows prove both feeds (the OLD tool pinned `PS2_VIF_FAST=0/1` here —
    that env pin became this option).
  - `--mtvu <N> [--vif-fast]` — ctest `VifMtvuSubmitEquivalence(Fast)` (RUN_SERIAL): per iteration
    three synthetic VIF1 chains walked inline, through the `MtvuWorker` ring (staged Batch / per-span
    alternating, the EE's VIF1_STAT poll alongside) and through the ring with mid-stream eager drains
    + one deferred boundary per chain (`GifDrainThread` up) → identical packet census + order,
    byte-identical VU1 data + code memory, identical VF/VI/TPC, position-exact boundaries; the fixed
    structural wait-point case. Output grammar verbatim from OLD (the `[W13R3-E3]` / `[W10-EEFEED]` /
    `[R168-DEFER]` tokens are line identity, not comments — P5-5G-ORACLE-GRAMMAR).
  - `--runahead --depth N` / `--runahead-off --depth 0` — ctest `VifRunaheadBound(Off)` (RUN_SERIAL): the
    OLD frame-cap selftest (A parked until the consumer presents at +25 ms, B frozen depth released by
    the 100 ms valve, C cap 0 never parks) on the injected `MtvuClock`; `--depth` replaces the OLD
    `PS2_MTVU_MAX_FRAMES` pin.
  - The OLD file mode (`<mem.bin> <tadr-hex> <dump-lo> <dump-hi> [out.bin] [vu1c] [vu1d]`) is
    NOT ported (P5-VIF-ARMS-SYNTHETIC: no `mem.bin` exists; the `VIF_REPLAY_*` variables die with it).
- `vif_arms_vs_old.py` — ctest `VifArmsVsOld`. Runs each arm on the NEW tool bounded and compares
  its lines with the `### ARM <name>` block of an OLD arm log (`/tmp/p5/5P/old_vif_arms.log` shape,
  or re-made from `OLD/build/tests/vif_replay` bounded with cwd pinned under `--out/oldcwd` when
  `--refresh-old` / the log is absent). Drops `[VU-MODE]` / `[run_bounded]` lines; blanks the
  timing tokens PLAN §4.2 excludes (`N busy views`, `N pkts eager-drained`, `(k/N iters)`,
  `parked Nms`, `at Nms`, `(Nms)`) — none occurs in the UNPACK arms, whose lines and exit codes must
  be exactly identical. Prints `ARM <name> SAME|DIFF …` and `VIFARMS arms=N same=M diff=K`;
  exit 0 iff `diff == 0`, 77 when neither OLD log nor OLD binary is available. `--arms all`
  adds the 5G arms.
- `CMakeLists.txt` — `vif_replay` at `-O2 -fno-strict-aliasing -fwrapv -Wall -Wextra -Werror`,
  linking `ps2r_vif` (→ `ps2r_vu ps2r_gif ps2r_ee ps2r_core ps2r_gs_format` transitively).

## Belongs / does not belong

Belongs: synthetic-stream arms that prove `engine/vif` against the OLD tool. Does not belong:
unit tests of the VIF contract (`tests/vif`), file-mode replay of recorded DMA chains (not
ported), engine code (`engine/vif`).

## OLD provenance

`tests/vif_replay.cpp` — `namespace unpkb_test` `:1292-1620` (the 30 generators `:1322-1477`,
`kCases` `:1479-1511`, `reset_case` / `run_leg` / `Run` `:1516-1618`), `main` `:1728-1788`
(the `--vif-fast` scrub `:1734-1744`, the arm dispatch, the usage line `:1784-1787`);
`tests/CMakeLists.txt:1379-1444` (the tool target and the 8 arm rows; the two
`PS2_VU1_EXEC_ASYNC=1` arms are DELETE and have no NEW counterpart). OLD linked the real
`host_app/vif.cpp` + `vu.cpp` and defined its own `GifArbiter::submit/drain`; NEW links the
engine libraries and drives the same `Vif` API the DMAC will.
