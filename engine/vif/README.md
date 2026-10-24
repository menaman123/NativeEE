# `engine/vif/`

**Phase:** 5  
**Purpose:** VIF0/VIF1 command processors — the register file, the quadword FIFO + word latch fed
by the DMAC (or the VIF1 FIFO window), the whole-span parser with the word-at-a-time state machine
as its slow path, every VIFcode (STCYCL … MSCAL/MSCALF/MSCNT, MPG, DIRECT/DIRECTHL, MSKPATH3,
UNPACK), UNPACK's scalar reference loop and the two VIF1 bulk lanes, the MMIO register file, and
the MTVU seam members the 5G worker drives.

## Files

| File | Type | Purpose | OLD provenance (`host_app/`, frozen) |
|---|---|---|---|
| `vif_regs.h` | `VifRegs` POD (23 × u32, frozen savestate order), `VifRegOffset` / `VifStat` / `VifErr` / `VifFbrst` constants, `VifCommand`, `MaskAction`, `VifState`, `VifUnitState` POD | zero primary types | `vif.h:106-218, :225-258`; `vif.cpp:917-918` (the parser cursor, made per-unit state) |
| `vif.h/.cpp` | `Vif : MmioHandler` | ctor-injected `VuUnit& vu0, vu1`, `GifArbiter&`, `GifRegs&`, `VifSettings`; `Reset`, `ProcessData` (span parser → word latch), `WriteFifo`, `ProcessFifo` (Idle / WaitData / DirectStream), `ExecuteCommand`, the TOPS/DBF swap + `VuUnit::Execute`, `Read/Write` over the FULL file (`kRegsSize = 0x180`), `CtxFor/BindVu1/SetTteFeed/ResetLatch/IsIdle`, `SetTopsObserver` | `vif.cpp:39-42, :703-830, :831-903, :1229-1866, :1906-2528, :3504-3602` |
| `vif_unpack.h/.cpp` | `VifUnpack` + `VifUnpackSettings{bulk_lanes=true}` | `GetUnpackSize`, `MaskActionFor`, the scalar `Execute` loop (all formats, USN, V2/V3/V4-5 rules, MODE, MASK, CL/WL, wrap, TOPS on VIF1), bulk-lane + table-lane dispatch + census (bulk / skip / table / scalar cmds+qw) | `vif.cpp:168-276, :769-775, :2680-3496` (dispatch `:2825-2870`) |
| `vif_unpack_bulk.h/.cpp` | `VifUnpackBulk` | lane A (`cl==wl`, V3-32/V4-32/V4-16/V4-8) and lane B (`wl==1, cl>1`, V3-32/V4-32/V4-8); NEON under `PS2R_SIMD`, scalar twin otherwise | `vif.cpp:309-311, :420-533, :576-687` |
| `vif_unpack_table.h/.cpp` | `VifUnpackTable` | the table lane after the bulk lanes: a constant `[format][masked][mode]` table of whole-command kernels for every other skipping-write shape (`cl >= wl`: S / V2 / V3-16 / V3-8 / V4-5, masked, MODE 1-3, WL > 1); MASK/MODE as per-row lane selects (NEON `vbsl` / scalar twin); declines partial payloads and `cl < wl` to the scalar loop. `VifUnpackScalar` proves every cell byte-identical to the reference over 14 976 (format, mask, mode, USN, CL/WL, NUM, phase) cells | NEW (the reference loop restated) |
| `vif_mmio_gate.h/.cpp` | `VifMmioGate : MmioHandler` | the router-facing surface over `Vif&`: reads forward only FBRST/ERR of either unit (else 0, counted `ReadsZeroed`), writes forward only FBRST/ERR + the VIF1 FIFO window `0x10005000..+0x10` (else dropped, counted `WritesDropped`) — the reference's routed observable (P5-VIF-MMIO-OBSERVABLE); the composition root registers the gate, not `Vif`, for `VIF0`/`VIF1`/`VIF1_FIFO` | `memory.cpp:583-615` (reads), `:1021-1052` (writes) |
| `vif_fast_span.h/.cpp` | `VifFastSpan` | the whole-span parser (decline set, DIRECT in-span / streaming, WAIT_DATA hand-off with the seeded partial quadword, payload + one lookahead word, `unpack_phase = start word & 1`) | `vif.cpp:967-1227` |
| `CMakeLists.txt` | `ps2r_vif` STATIC → `ps2r_vu ps2r_gif ps2r_ee ps2r_core`; `-Wall -Wextra -Werror -O2 -fno-strict-aliasing -fwrapv`; `PS2R_SIMD` passthrough | | |

## Belongs here

- The VIF's PS2 semantics: VIFcode decode, payload sizing, UNPACK expansion, the double-buffer
  rule, PATH2 submission, the register file, the FIFO/latch feed contract.

## Does not belong here

- The MTVU worker / drain thread (`engine/vu/tiers` at 5G) — the VIF only exposes the seam
  members; the DMAC chain walk (`engine/dmac`); the GIF parser (`engine/gif`); the VU1 HLE
  runtime (the composition root installs `SetTopsObserver` → `Vu1HleRuntime::SetVifTops`);
  the MMIO router registration itself (the composition root registers `VifMmioGate`).

## Behaviour notes (carried observables, documented divergences)

- `Reset()` clears every unit field (latch, cursor, direct buffer, TTE bracket, phase) — a superset
  of OLD's `Reset` (which left them) plus the tool's `reset_case`; CYCLE = 0x0101 like OLD.
- FBRST.RST clears the FIFO, state and pending bytes only (OLD scope); STAT.FQC is
  `fifo.size() / 16` over a quadword deque (OLD's 16× under-report, carried for `TAG_VIFS` identity).
- `Vif::Read/Write` address the full file (`offset = addr - base`, ROW/COL reachable); the OLD router
  exposed only FBRST/ERR (+ VIF1_STAT while MTVU ran) for reads and FBRST/ERR + the 0x10005000 FIFO
  window for writes — `VifMmioGate` is that observable; the root registers the gate (the MTVU
  VIF1_STAT busy view is the MTVU stage's, not the gate's). Census: `run.meta vif_mmio_gated=r<N>,w<N>`.
- VIF0: MSCAL kicks VU0 without any swap; MSCALF/MSCNT/BASE/OFFSET/MSKPATH3 are no-ops; UNPACK
  ignores the TOPS bit. `VifRegs.num` is never updated. Unknown VIFcodes are ignored (no ER1).
- CL < WL "filling write" follows OLD's loop (every cycle writes; vectors past the source read the
  lookahead word then zeros), not the hardware's ROW fill.
- The masked existing-quadword read goes through `VuUnit::ReadDataMem` (masked) — for VU0 OLD read
  a 4 KB array unmasked (UB above qw 255).

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time: `PS2_VIF_FAST`, `PS2_VIF_UNPACK_SIMD`, `PS2_VIF_UNPKBX` NORM
  (winning arm only; `VifSettings.fast_span` / `VifUnpackSettings.bulk_lanes` are proof knobs, not
  configuration); `PS2_UNPKT_CENSUS`, `PS2_VIF_ALIGN_LOG`, `PS2_SHADOW8_KTAP` DIAG omitted;
  `PS2_S8_VIFTAP`, `PS2_VIF_WTRACE`, `PS2_VU1_EXEC_ASYNC` (the shadow/staging seam) not written.
- Plan: `docs/phases/PHASE5_PLAN.md` §2.6 / §3.5; record: `docs/phases/PHASE5_IMPL.md` 5E.
- Proofs: `tests/vif` (NEW-only suites) and `tools/vif_replay` (`--unpack-bulk [--vif-fast]`, the
  bulk lanes vs the scalar loop on both feeds, textually identical to the frozen OLD tool's arms via
  `vif_arms_vs_old.py` — ctest `VifUnpackBulkEquivalence(Fast)`, `VifArmsVsOld`).
