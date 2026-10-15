# Scratchpad DMA completion and reachability

Date: 2026-10-06. Owned scope: `engine/dmac/**`, `tests/dmac/**`, and this
new report. Stage rules and reference freeze were read. The user's scoped
instructions supersede shared-build/phase-log requirements; DT-8 retires
OLD-memory identity as a reason to retain a known no-copy bug. OLD and the
reversed-title repository were read-only. No commit, shared build, full ctest,
or host-game execution was performed by this worker.

**Current direction (DT-9, user, 2026-10-07):** this is a historical implementation
and limitations record. Do not implement the residual hardware work described
below. Replace each reached copy/upload/submit intent natively and retire its
DMA path; track it in [HLE_COVERAGE.md](HLE_COVERAGE.md#scaffolding-retirement).

## Result

Implemented RAM ↔ scratchpad normal transfers and RAM → scratchpad source
chains. Both SPR MMIO pages now accept writes. Transfers advance MADR/SADR/QWC,
wrap the 16 KiB scratchpad, and complete through the existing CIS/INT1 path.
Unsupported requests remain observably busy instead of reporting success.
Bad memory accesses terminate with BEIS/CIS and retain outstanding QWC.
D_STAT status acknowledgement and interrupt-mask toggles now work through MMIO,
including recovery from BEIS. This also corrects the previously dropped
non-SPR D_STAT writes; parent integration must validate that broader effect.

This is functional transfer support for the statically identified title paths,
not a complete DMAC hardware scheduler.

## Primary references

Consulted online on 2026-10-06, without copying emulator implementations:

- Sony **EE User's Manual v6.0**, sections 5.1 (channel capabilities), 5.2.2–3
  (normal/source-chain transfers and burst TTE), 5.2.5 (interleave), 5.4
  (completion, invalid tags, bus errors), 5.6 (tag meanings), 5.9
  (registers and INT1 equation), and 5.10 (zero normal/interleave QWC prohibited).
  Mirror: `https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/EE_Users_Manual.pdf`.
  Download SHA-256:
  `55bdc71d3aebc33752a7dad6526b78248b6c258294fcf8a3ebed1f010da22365`.
- PS2SDK `common/include/ee_regs.h`, D8/D9 register addresses:
  `https://raw.githubusercontent.com/ps2dev/ps2sdk/master/common/include/ee_regs.h`.
- PS2SDK `ee/dma/src/dma.c`, `dma_channel_send_normal` and
  `dma_channel_send_chain`: initial QWC/TADR programming and status acknowledgement.
  `https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/dma/src/dma.c`.

The manual specifies fixed directions for channels 8/9, 14-bit aligned SADR,
16-bit QWC, and fixed-zero MADR.SPR. CNT/NEXT/REF/REFE/END are supported source
tags; REFS and CALL/RET are restricted to other channels. TTE on a burst channel
transfers the full tag. IRQ with TIE stops after its packet. BEIS is unmaskable;
software clears status by writing ones and toggles mask bits by writing ones.

## Static title evidence

Read-only source root:
`<projects>/the title-reversed`.

| Path | Evidence and conclusion |
|---|---|
| `src/platform/ps2/movie/mpegoutput.cpp:127,167,196,203` | `CopyMacroblocks` uses normal D9 RAM→SPR (`CHCR=0x101`), then D8 SPR→RAM (`0x100`), with SADR zero, MADR/QWC programmed and STR/QWC polling. Both normal directions are required if this retail path executes. |
| `src/platform/ps2/movie/mpegmotion.cpp:400,414,429` | `FetchReferences` builds pairs of REF tags, terminating in REFE. It programs D9 SADR/TADR, QWC=0 and CHCR=0x105. Source chains are required; a normal-only fix would still break motion compensation. |
| `src/platform/ps2/movie/decoder.cpp:135,1074,1075` | The generic settings helper can request interleave, but its two inspected callers configure IPU channels with interleave=0. No active SPR interleave start was found in the inspected reversed source. |
| `src/platform/ps2/graphics.cpp:25` and `renderer/dma.cpp:12` | Register-address tables include the SPR channels. Table membership/reset writes alone do not prove a DMA transfer is reached. |

The generated **NTSC** code independently corroborates the source:

- `games/<title>/recomp/recompiled_shard_029.cpp`,
  `FUN_002b8110` (`_cpr8`): blocks at guest `0x002b8210` and `0x002b828c`
  construct D9/D8 MMIO addresses and store normal start words `0x101`/`0x100`.
  `symbols/pal_ntsc_map.tsv` maps the reversed PAL `CopyMacroblocks` label
  `0x002b89a8` to this NTSC function with high confidence.
- `recompiled_shard_030.cpp`, `FUN_002bed28` (`dmaRefImage`):
  block `0x002bed98` builds REF/REFE tags with QWC `0x30`;
  block `0x002bee24` writes D9 SADR/TADR/QWC/CHCR and start word `0x105`.
  The manifest and symbol table identify this NTSC function directly.
  The reversed-source positional map for this function has low confidence;
  it was not used to infer the NTSC address.

Static sites are not live reachability proof. `engine/ipu/hle/libmpeg_bindings.cpp`
binds the public picture calls to HLE, so the retail DMA paths may be bypassed.
Parent owns live census for intros, attract, saves/loads and sustained gameplay.
Zero measured starts establish absence only in those measured runs.

## Implementation and census contract

- `DmacSprTransfer` owns transfer mechanics and bounded chain walking. Memory
  addresses/register widths are normalized on transfer, including direct HLE
  `StartChannel` entry; existing raw register-store behavior before a kick is
  otherwise retained.
- Normal copies accept address zero, copy complete spans up to each boundary,
  wrap SADR repeatedly, and never wrap invalid main-RAM addresses into RAM.
- Source chains implement CNT/NEXT/REF/REFE/END, zero-length packets, TTE,
  CHCR.TAG mirroring, IRQ/TIE, and a pre-existing nonzero QWC packet before the
  next tag. ASR registers are untouched; ch9 has no address stack.
- Continuing and IRQ-stopped chains maintain the next TADR. REFE/END have
  no successor specified in the consulted manual: this implementation retains
  the terminal tag address. Exact terminal TADR readback needs hardware capture;
  the tests deliberately do not claim that unspecified value as proven.
- Invalid REFS/CALL/RET tags terminate with CIS and no payload transfer, as
  undefined tags on this channel. Bus errors additionally set BEIS. Both count
  as faults, not completed transfers.
- D_STAT writes apply W1C to `0x0000e3ff` and toggle masks `0x63ff0000`.
  A newly enabled pending interrupt raises INT1. Acknowledging BEIS/CIS permits
  another transfer; dispatch alone does not clear an unacknowledged bus error.
- An SPR kick remains independent of deferred VIF1 observation/drain points.

Parent API, available from `dmac.h`:

```cpp
const DmacTransferStats& Dmac::TransferStats() const;
// stats.spr_from[mode], stats.spr_to[mode], raw CHCR.MOD = 0..3.
// Each DmacTransferCount has uint64_t:
// starts, completed, quadwords, rejected, unsupported, faults.
// stats.unsupported_spr_ctrl_writes counts rejected control setups, separately.
```

`starts` counts every requested start, including unsupported/reserved modes.
`quadwords` counts actually copied data and TTE tag quadwords, including progress
before a fault. `rejected == unsupported + faults`;
`starts == completed + rejected` after each synchronous call returns.
Counters accumulate across guest resets/restores, are not atomic, and must be
read on the EE owner thread or after quiescence. They are excluded from TAG_DMAC:
the fixed 324-byte header and 25-byte handler records are unchanged.

For the parent's `dma_spr` group, use modes 0/1/2 `starts` for each direction
and sum `unsupported`/`faults` over **all four modes in both directions**.
Add `unsupported_spr_ctrl_writes` to the parent's unsupported aggregate.
Fail live reachability acceptance on any unsupported setup, unsupported start
or faulted start. The separate setup count increases on every ignored D_CTRL
write with MFD!=0 or STS=fromSPR, even before a start. A regression programs
these fields through MMIO, observes the retained CTRL=0, then performs an
ordinary copy and proves the setup counter still rejects that run. This closes
the false-negative hole that testing only retained `g_.ctrl` would leave.

Rejected operations emit `PS2R_LOG(DMAC, Error, ...)` once per
direction/mode/reason over the object lifetime (bounded to at most 72 messages,
plus one ignored-control-write diagnostic).
The diagnostic includes CTRL, MOD, initial CHCR/MADR/SADR/QWC/TADR, a reason,
and stopping addresses/count. It flushes the logger immediately so a polling
stall followed by watchdog termination retains evidence. The logger's category
and level gates still apply: enable `dev.log_level=error` or higher for census
runs; quiet logging suppresses it. Suppression does not suppress counters.

## Historical hardware limitations — not an implementation backlog

The work suggestions in this table predate DT-9. A reached limitation now
requires the native intent replacement in HLE_COVERAGE, not the hardware
extension described in this historical record.

| Request/behavior | Current result and work needed |
|---|---|
| SPR_FROM destination chain; either direction interleave; reserved MOD=3 | Counted unsupported; STR remains set, no new CIS/BEIS, no copy. No active title start found in the inspected source. Implement destination-tag/STADR handling or D_SQWC stepping if live evidence requires it. |
| Normal QWC=0 | Counted unsupported and left busy. The manual prohibits this start; no invented zero-length completion or 65,536-qword transfer. |
| VU-memory normal transfers | Legal hardware requests but unsupported here; remain busy. Need VU memory routing and visibility tests. |
| FromSPR MFIFO or stall-source settings in D_CTRL | Counted unsupported before copying. Need ring/drain coordination and STADR progress. |
| Scratchpad chain sources; nonzero tag PCE | Counted unsupported, left busy. Need separately validated source/tag routing and priority-control behavior. |
| Cyclic/overlong source chain | A 10,000-tag host safety limit retains STR and counts unsupported; prior copied data remains. This is not claimed as hardware termination. |
| Partial chain rejection | Registers/data retain progress through preceding supported packets; no rollback. Reprogram/restore before retrying a rejected packet; automatic resume of every rejection point is not implemented. |
| Terminal REFE/END TADR | Kept at terminal tag; exact hardware readback remains unvalidated as described above. |
| Scheduler/global controls | Synchronous copies, no cycle stealing or arbitration; D_CTRL, D_ENABLEW, D_SQWC, MFIFO globals remain write-dropped. Nondefault SPR MFIFO/stall requests through D_CTRL are independently counted/logged despite the dropped write. DMA enable/suspend scheduling is not completed by this patch. |
| Register-face fidelity | Invalid/unused register fields remain raw until a transfer; full read-only/fixed-bit modeling is not included. |
| Title/integration validation | Parent owns app census, full suite, existing regression legs and live gameplay. This report does not claim those passed. |

## Integrated IPU coupling

The coordinated IPU changes retain channel IDs and savestate order:
FROM=3 at `0x1000b000` drains output, TO=4 at `0x1000b400` feeds input.
Both IPU MMIO pages accept writes. Output callbacks report transferred bytes;
MADR/QWC advance by that amount and STR remains set until output is complete.
The IPU output-ready callback retries armed transfers. The libipu register
adapter starts/resumes DMA through the controller.

`IpuDmaCoupling` covers direction, register progression, output armed before
data, partial drains, cancellation/rearming, valid RAM address zero, and
buffered input stop/restart. See the [IPU report](subsystem_completion_ipu.md)
for the snapshot normalization and its contiguous-source assumptions.

Input backpressure, IPU source chains, CPU FIFO ports and the retail RAW8
continuation remain explicit tasks. These buffered transfers do not constitute
a complete IPU hardware scheduler.

## Tests and handoff

The old `DmacSprStub` suite explicitly required unchanged memory and addresses.
It is replaced by `DmacSpr`, whose distinct patterns and whole-image checks
prove copying, untouched source/guard bytes and multiple wraps in both directions.
The independent register suite's SPR test is likewise corrected; its deferred
VIF, handler and savestate assertions remain. The libdma reset test now expects
SPR_TO resets and documented D_STAT changes while preserving the SDK's untouched
SIF status/mask bits. D_STAT acknowledgement remains a non-observation point for
deferred VIF1.

Standalone compiler commands use `clang++ -std=c++17 -Wall -Wextra -Werror
-O2 -g -I <repo>` for **all** `engine/dmac/*.cpp` and `engine/dmac/hle/*.cpp`.
Each focused test is recompiled against those objects and existing read-only
Debug archives for GS IR, VIF, VU, GIF, EE, core and GS format (plus GS sync for
the boundary test). All output is under `/tmp/subsystem-dma`.
Reproduction command:

```sh
python3 /tmp/subsystem-dma/focused_tests.py \
  test_dmac_spr test_dmac_regs test_dmac_libdma test_dmac_section \
  test_dmac_chain_walk test_dmac_deferred_vif1 test_dmac_gif_path3 \
  test_dmac_vif_chain test_dmac_tail_chase_boundary test_dmac_mtvu_feed
```

Parent build targets: the ten names above. `test_dmac_spr` replaces
`test_dmac_spr_stub`; its ctest name is `DmacSpr`.
The parent should rebuild through CMake after concurrent shared jobs finish,
then run its own full-suite and live verification.

Validation after the acknowledgement and ignored-control-observation fixes:

| Check | Result |
|---|---|
| `test_dmac_spr` | PASS, including copies, wrap, source chains, bounded rejection logs, BEIS acknowledgement/retry and dropped-control observation |
| `test_dmac_regs`, `test_dmac_libdma`, `test_dmac_section` | PASS |
| `test_dmac_chain_walk`, `test_dmac_deferred_vif1`, `test_dmac_gif_path3`, `test_dmac_vif_chain` | PASS |
| `test_dmac_tail_chase_boundary`, `test_dmac_mtvu_feed` | PASS |
| Scoped standard `LintOneClass`, `LintEnginePurity`, `LintHeaderGlobals`, `LintSessionTags`, `LintSizeBudget` | PASS; size/scoped allowlist warnings only, zero failures |
| Owned-path `git diff --check` | PASS |

Sanitizer reproduction uses the same read-only dependency archives, instruments
all DMA sources and the scratchpad test with AddressSanitizer and
UndefinedBehaviorSanitizer, and runs with a 120-second bound:

```sh
bash /tmp/subsystem-dma/run_sanitized.sh
```

The scratchpad sanitizer test passed with zero failures and no sanitizer findings;
the final log is `/tmp/subsystem-dma/spr_sanitized.log`.
