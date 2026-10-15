# IPU completion and title reachability

Date: 2026-10-06. Initial scope: `engine/ipu/**`, `tests/ipu/**`, and this report.
The parent additionally authorized the precise IPU wiring in
`app/host/ee_bootstrap.cpp`, IPU direction/MMIO/callback changes in
`engine/dmac/dmac.cpp`, `dmac.h`, `dmac_channel.h`, and the matching
`tests/dmac/test_dmac_regs.cpp` expectations. The parent subsequently authorized
the stale channel-4 reset assertion in `tests/dmac/test_dmac_libdma.cpp`.
The DMA worker's SPR, D_STAT
and control-counter changes were preserved. No IOP, root/shared CMake,
parent census wiring or unrelated existing edits were changed.
No commit, host execution, full ctest, or build in shared build directories was performed.
OLD and `the title-reversed` were read only.

`docs/phases/STAGE_RULES.md` and `docs/REFERENCE_FREEZE.md` were read.
The user's scoped instruction delegates live/full-suite verification to the parent and
overrides the stage's shared-build and phase-log requirements. DT-8 retires OLD-memory
identity; OLD is historical evidence, not a reason to preserve known IPU defects.

**Current direction (DT-9, user, 2026-10-07):** this is a historical implementation
and limitations record. BDEC/IDEC completion, IPU scheduling and SDK register
fidelity are no longer tasks. Own the game's Movie lifecycle, decode,
conversion and presentation natively; retire the corresponding IPU/DMA paths.
The active inventory is [HLE_COVERAGE.md](HLE_COVERAGE.md#scaffolding-retirement).

## Result

Implemented VDEC, SETVQ, PACK RGB16/INDX4, and CSC RGB16/dither/threshold handling,
plus control/readback, input accounting, SETIQ bit offsets, and libipu resume fixes.
These are functional command implementations over supplied buffers, **not a completed
hardware scheduler or MPEG decoder**. BDEC remains unimplemented; IDEC remains a
partial, approximate decoder. Every BDEC/IDEC issue is now explicitly unsupported
in the census, including IDEC with default flags.
The bounded integration follow-up corrects channel direction and channel 4
MMIO, resumes pending output drains, and fixes libipu input snapshot loss
when the software buffer contains more than the eight reported IFC qwords.

## Sources and reachability

Primary semantics consulted online (retrieved 2026-10-06):

- Sony **EE User's Manual v6.0**, chapter 8: 8.2 (pixel/macroblock layouts),
  8.3 (registers/BP resume), 8.4 (commands), 8.5.3 (VDEC numeric tables),
  8.6.1–8.6.3 (CSC fixed-point arithmetic, transparency, ordered dither and VQ).
  Public mirror: `https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/EE_Users_Manual.pdf`.
  Chapter 5 supplies the FROM-IPU=3 / TO-IPU=4 direction and DMA registers.
- PS2SDK `common/include/ee_regs.h`: CMD/TOP are 64-bit, CTRL/BP are 32-bit;
  CPU FIFO input/output windows are distinct.
  `https://raw.githubusercontent.com/ps2dev/ps2sdk/master/common/include/ee_regs.h`.

No PCSX2/OpenGOAL implementation code was used. VLC numeric assignments and
pixel arithmetic were implemented from the manual.

Read-only title source root:
`<projects>/the title-reversed`.
The paths below are relative to that root; these are source-level call-graph findings,
not an exhaustive machine-code reachability proof.

1. `src/platform/ps2/movie/player.cpp`, `Platform::Movie::Open`:
   sets `MovieMpeg2` and passes `MovieRgb32` to `MovieDecoding::Open`.
   The source search found that call as the named external caller of the decoder's
   `Open` entry point.
2. `src/platform/ps2/movie/decoder.cpp`, `DecodePicture`:
   the MPEG2/RGB32 arm calls `sceMpegGetPicture`.
   The RGB16 arm calls `sceMpegGetPictureRAW8`, then issues BCLR and
   **CSC with OFM=RGB16 and DTE=1** around DMA transfers.
   The non-MPEG2 arm uses FDEC, writes CTRL, and issues **IDEC** directly.
   These alternative branches exist even though the inspected player selects
   MPEG2/RGB32.
3. `src/platform/ps2/movie/mpegdecode.cpp` issues BDEC and all VDEC table kinds;
   `mpegbits.cpp` issues FDEC; `mpegheaders.cpp` loads quantizers and writes
   CTRL; `mpegoutput.cpp` issues CSC. These belong to the retail decoder.
4. `src/platform/ps2/movie/ipu.cpp`, `sceIpuInit`: reset/BCLR, default intra
   and non-intra SETIQ, SETVQ CLUT, SETTH, reset/BCLR.
   `sceIpuRestartDMA` obtains FP, IFC and bit position from saved **IPU_BP**.
5. Current `engine/ipu/hle/libmpeg_bindings.cpp::Bind` replaces the outer
   `sceMpegGetPicture`, `sceMpegGetPictureRAW8`, and `sceMpegDemuxPss*`
   entry points. `MpegApi`/`MpegContext` use the injected FFmpeg decoder and
   `FrameWriter`; these paths do not enter the retail BDEC/VDEC/CSC decoder.
   Creation/reset wrappers still run generated bookkeeping, including CTRL
   writes. `_clearOnce`'s bit 23 write is **MP1**, not IDP as some historical
   comments in OLD describe it.
6. Current `LibipuApi::Init` bypasses the SDK's quantizer/CLUT/threshold
   seeding, issuing only the reset/BCLR brackets. Initialization therefore
   does not provide live evidence of actual SETIQ/SETVQ/SETTH command use.

OLD evidence, read only:
`$PS2R_OLD_ROOT/host_app/ipu.cpp` dispatch and
CSC/IDEC bodies confirm missing VDEC/SETVQ/PACK, stub BDEC, missing RGB16
output, ignored control bits and old float CSC arithmetic.
`host_app/sce_hle/libmpeg.cpp` describes the FFmpeg bypass and direct RGB16
plane write in the RAW8 handler; `host_app/sce_hle/libipu.cpp` records the
previous reset/snapshot implementation. No code was modified or built there.

| Opcode | Command | Title evidence / disposition |
|---|---|---|
| 0 | BCLR | Reachable through HLE libipu initialization and DMA restart; retained and corrected to seven BP bits. |
| 1 | IDEC | Direct branch in reusable movie driver for non-MPEG2 streams; not selected by the inspected player's Open. Partial implementation, **every issue counted unsupported**. |
| 2 | BDEC | Retail libmpeg macroblock decode behind replaced picture entry points. Stub retained with per-command visibility; not globally declared unreachable. |
| 3 | VDEC | Retail libmpeg bit parsing behind HLE. All manual tables implemented for supplied input. |
| 4 | FDEC | Retail decoder plus direct non-MPEG2 driver branch. Existing decode retained; shortages now counted. |
| 5 | SETIQ | Retail headers and SDK init; init seeding bypassed. FB and unaligned input implemented. |
| 6 | SETVQ | SDK init seeding bypassed by HLE. Full 16-entry RGB16 CLUT load implemented. |
| 7 | CSC | Retail output helpers behind HLE, plus direct RGB16 driver branch after RAW8. RGB32/RGB16, DTE, nine-bit thresholds implemented; RAW8 integration remains open below. |
| 8 | PACK | No issue site found in the inspected movie sources. Implemented both output formats because semantics are bounded and specified. This search is not global unreachability evidence. |
| 9 | SETTH | SDK init seeding bypassed. Nine-bit fields now preserved and applied to CSC / IDEC postprocessing. |
| 10–15 | Undefined | Counted as unsupported; no invented hardware behavior. |

Parent-provided historical baseline:
`/tmp/ps2r-subsystem-completion/before/guard/coldboot_debug/run.meta`:
547 FMV pictures, playbacks `201,346+`, libipu
`init=1,sync=4,stop=549,restart=547`, aggregate divergent=0.
This report does **not** treat those observations as a post-change live run or
as proof that alternative movie formats cannot occur.

## Census API for parent integration

All methods are on `ps2r::ipu::IpuDevice`, declared in `engine/ipu/ipu_device.h`:

```cpp
uint64_t CommandCount(uint8_t code) const;
uint64_t UnsupportedCommandCount(uint8_t code) const;
uint32_t LastUnsupportedCommand(uint8_t code) const;
uint32_t UnsupportedVariantMask(uint8_t code) const;
uint64_t DivergentCommandCount() const;
```

- `code` is the high command nibble, 0–15; out-of-range access returns zero.
- CommandCount counts every `WriteCmd`, including HLE-issued BCLR commands
  and commands which lack input. UnsupportedCommandCount counts **issues**,
  once per issue, rather than reasons or missing macroblocks.
- `DivergentCommandCount() == sum(UnsupportedCommandCount(code), code=0..15)`.
  Its coverage now includes IDEC and synchronous input shortages; the old
  aggregate undercounted these omissions.
- `LastUnsupportedCommand` contains the most recent full raw command word.
  `UnsupportedVariantMask` accumulates reasons: bit 0
  `kUnsupportedDecode` (BDEC/partial IDEC), bit 1 `kUnsupportedInput`
  (input needed but no asynchronous resume), bit 2 `kUnsupportedOpcode`.
- All are lifetime observer statistics: **RST and BCLR preserve them**.
  They are not added to any savestate section. There is no IPU state section
  in the current state registry; future serialization must exclude observer
  fields and initialize neither their arrays nor aggregate on guest restore.
- Sample on the owning EE thread or after quiescence; these are not atomics.
- Continue recording `LibmpegBindings::Census` (`pictures`, `raw8`,
  `served_total`, etc.) and `LibipuBindings::InitCalls`.
  A zero hardware command count while HLE picture counts grow is evidence
  of that measured bypass, not of unused MPEG decoding.

The parent can record all 16 issue/unsupported counts without changing these
APIs. Last-command/reason fields are useful when an unsupported count becomes
nonzero. Register writes outside the supported register face remain visible
through `UnhandledWriteCount`; that counter does not observe an unmapped CPU
FIFO window, which belongs to the MMIO router.

`LibipuApi::DmaSnapshotErrorCount() const` is a separate `uint64_t` lifetime
observer for rejected Stop/Restart operations. `Init` preserves it; it is
not serialized. The parent records `libipu.DmaSnapshotErrorCount()` as the
standalone `run.meta` field `ipu_dma_snapshot_errors`; its verifier requires
the field and rejects nonzero values. These failures are SDK snapshot operations,
not fictitious command issues, so they do not affect the per-opcode sums.

## Implementation details and deliberate behavior changes

- `ipu_vlc.h/.cpp`: table selection honors CTRL.PCT and MP1. VDEC packs
  decoded code length in bits 31:16 and signed value in bits 15:0, skips FB,
  advances only by a matched code length, and reports invalid codes as zero/ECD.
  A 32-bit lookahead is required, as the manual specifies.
- `ipu_color.h/.cpp`: fixed-point CSC uses the manual's coefficients and
  intermediate truncation, replacing the full-range floating formula.
  Thresholds are strict per-component comparisons and retain their ninth bit.
  RGB16 alpha is the IPU **translucency** bit, set only for A8=0x40; it is not
  a generic high-alpha-bit conversion. Dither retains half-unit coefficients.
  VQ compares squared RGB5 distance, ignores alpha and emits the earlier pixel
  in the low nibble. Equal-distance palette ties select the first index;
  the manual does not specify a tie winner, so hardware validation remains open.
- SETVQ and SETIQ consume complete tables only, preserving prior tables on a
  short input. SETIQ honors FB even when it leaves an unaligned cursor.
- CSC/PACK consume only available complete macroblocks; they do not advance
  BP over missing input or invent output. Shortage is visibly unsupported.
  Commands still finish synchronously rather than waiting for later FIFO feeds.
- CTRL writable fields latch; read-only status cannot be written. RST clears
  FIFO counts/status immediately, not on a second CTRL read. Consumed input
  quadwords are removed from the **reported** IFC, though the storage vector
  still retains bytes. CMD DATA preserves all 32 bits; BUSY belongs in bit 63.
  A 64-bit CMD write ignores its upper word without counting a spurious write.
- `LibipuApi::RestartDma` now takes rewind `(FP + IFC)` and initial BP from
  snapshot word [7] IPU_BP rather than word [8] IPU_CTRL.
- IDEC now formats its approximate decoded result as RGB16/32 with thresholds,
  SGN XOR and optional dither. This does **not** certify its parser, IDCT,
  colour arithmetic, control-dependent modes or SGN+dither clipping behavior.

## Bounded DMA coupling and snapshot correction

- Channel 3 (`IpuFrom`, `0x1000B000`) invokes the output drain; channel 4
  (`IpuTo`, `0x1000B400`) invokes the input feed. Channel 4's register page is
  now writable. App comments and snapshot direction fixtures agree.
- The feed callback remains `std::function<void(uint8_t*, size_t)>`.
  `Dmac::IpuDrainCallback` is `std::function<size_t(uint8_t*, size_t)>`:
  return actual transferred bytes, a multiple of 16 no larger than requested.
  Channel 3 advances MADR and reduces QWC by that amount. A short drain
  retains STR and does not signal completion; channel 4 advances MADR after
  its synchronous feed.
- `IpuDevice::SetOutputReady(std::function<void()>)` runs after a command
  appends output and updates status/census. The app retries channel 3 only
  while STR remains set. Guest reset preserves this host wiring.
  `DmacChannelFaceAdapter` accepts an optional `Kick(uint32_t channel)`;
  it stores CHCR first, invokes the starter only for STR writes, and preserves
  register changes made by the starter. The app injects `Dmac::StartChannel`.
  Stop/cancel writes do not trigger transfers.
- The buffer model accepts more than eight input qwords, while BP.IFC is
  capped at eight. `StopDma` computes unread qwords as
  `max(InputSize()/16 - BitPosition()/128, 0)`. Any excess over BP's reported
  rewind moves snapshot TO-MADR backwards and increases snapshot TO-QWC.
  For 24 unread qwords, 16 are represented as pending DMA and eight in IFC.
  Restart's existing eight-qword rewind therefore reloads all 24, with BP's
  low seven bits restoring the cursor within the first unread qword.
  The consumed whole-qword prefix is not replayed.
- This normalization is an explicit buffered-model adaptation, not a claim
  that a real hardware snapshot has different register values. All resume
  information is in the nine guest words; the regression resumes a fresh
  device from a copied snapshot and original source memory.
- Stop rejects an address too small for the rewind, a total count beyond
  the 16-bit QWC range, or input that is not whole qwords. It emits the invalid
  TO-QWC value `0x10000` and increments `DmaSnapshotErrorCount`.
  Restart validates supplied counts/rewinds before BCLR or either channel
  kick; rejection leaves FIFO/cursor intact and increments the same counter.
  Rewinding exactly to RAM address zero is valid. No count/address wrapping
  and no private snapshot cache are used.
- The app change has net **−1 line**: file 2426/2427 allowed lines and
  `EeBootstrap::Run` 2116/2118 allowed lines. No app census lines were edited.

## Historical hardware limitations — not an implementation backlog

The command/scheduler completion suggestions below predate DT-9 and are
superseded. Keep this description of current limitations as evidence; close
reached gaps through the native Movie replacements in HLE_COVERAGE.

| Gap | Required completion / acceptance tests |
|---|---|
| **BDEC remains a stub** | If a live title path issues it, capture command, CTRL and input and implement the reachable modes before claiming gameplay coverage. A hardware completion needs intra/non-intra blocks, CBP, DCR, frame/field RAW16 arrangement, DC/AC VLC, inverse quantization, scan modes and exact consumed bits. Acceptance: known macroblocks for each supported mode, invalid/start codes, preserved cursor and RAW16 output compared with PS2/reference captures. |
| **IDEC remains partial** | Missing MBT/MBAI/QSC changes, DTD/field layout, complete AC VLC, AS/IVF/QST/MP1 behavior, precise IDCT/mismatch behavior, correct partial-input continuation, and error/start-code handling. SGN+DTE clipping quirks and current approximate colour arithmetic also need validation. Acceptance: real valid slices with multiple MBs, quantizer changes, both DCT layouts, all reached CTRL modes, RGB32/RGB16 and cursor/status checks; keep unsupported counting until the entire reached combination is validated. Do not grow this patch into a full MPEG decoder. |
| **Scheduling/FIFOs/interrupts** | Eight-qword capacity, internal FP/prefetch model, input backpressure, BUSY across input starvation, command-completion interrupts and input-storage compaction remain absent. Output DMA now waits/retries on newly produced data, but this does not resume an input-starved IPU command. Acceptance: feed/drain one quadword at a time, pause mid-command, supply rest, verify no duplicate/lost bytes, correct BUSY/IFC/OFC/FP/BP, interrupt transitions and reset cancellation. Synchronous buffer tests are not evidence for full scheduling. |
| **CPU FIFO / remaining DMA coverage** | CPU FIFO ports remain unmapped. Normal buffered direction, channel 4 MMIO, output armed before data, partial drains, cancel/rearm and contiguous-input stop/resume are covered. IPU source chains, input backpressure, memory faults and noncontiguous input provenance are not completed by this patch. Snapshot normalization assumes unread input corresponds to unchanged contiguous source memory ending at TO-MADR. Acceptance: CPU 128-bit FIFO transactions, source-chain transitions/noncontiguous buffers and invalid-memory transfers, plus a full pipeline stop/resume fixture. |
| **RAW8 HLE contract** | Current RAW8 handler writes final RGB16 planes, ignores the raw output buffer as a hardware YCbCr destination, and relies on the subsequent driver hardware path not replacing the final result. Supporting the direct CSC branch requires a coordinated choice: supply real RAW8 data to the caller with correct DMA scheduling, or explicitly own the entire conversion branch at a title/HLE boundary. Acceptance: execute the retail RGB16 caller including subsequent CSC/DMA; verify the output plane survives, the other plane is untouched, and no unsupported count rises. The added HLE-boundary test does not execute that retail continuation. |
| **SDK initialization / stop wait** | Libipu init still omits matrix/CLUT/threshold seeding and does not reproduce every channel setup side effect; StopDma uses a synchronous shortcut instead of waiting for a draining output pipeline. Calls are counted, but skipped operations do not falsely count as issued commands. Acceptance: initialize a deliberately dirty unit against the SDK's actual default tables/thresholds and channel setup; stop/resume with pending output after FIFO scheduling exists. |
| **Boundary validation** | CSC below nominal video black clamps Y−16 to zero; confirm out-of-range RAW8 behavior on a hardware/reference fixture. Confirm equal-distance VQ ties. The manual specifies the main arithmetic but these boundary choices are not hardware-capture proof. |
| **Persistence / real title coverage** | No IPU hardware/FMV playback savestate section was added. Parent owns full app/live/save-load validation and recording per-command census across intros, attract, menu returns, loads and sustained gameplay. A successful short run or no PACK sites in the inspected source cannot close general PS2 reachability. |

## Tests and handoff

New `tests/ipu/test_ipu_completion.cpp`, CMake target `test_ipu_completion`,
ctest name `IpuCompletion`, covers:
VDEC tables and invalid codes, SETVQ atomicity, PACK RGB16/INDX4, nibble ordering,
alpha/dither, CSC chroma layout and threshold boundaries, SETIQ unaligned FB,
shortage accounting, BCLR masking, control writable masks, lifetime census
across RST/BCLR, aggregate/per-command consistency, and explicitly counted IDEC
fallback output formatting.

Updated tests:

- `test_ipu_device.cpp`: fixed-point colour vectors, immediate counts, RGB16
  output, no cursor advance past absent macroblocks, 64-bit CMD write semantics.
- `test_libipu_api.cpp`: immediate reset counts and BP-based rewind with
  deliberately conflicting CTRL fields; real adapter direction, kick ordering,
  restored fields, cancellation and preservation of callback register changes.
- `test_libmpeg_bindings.cpp`: five RGB32 and four RAW8 real FFmpeg picture
  deliveries preserve a staged IPU FIFO/result/threshold state and all command
  counters. Existing image-plane and HLE census assertions remain active.
- `test_ipu_dma_coupling.cpp` (target `test_ipu_dma_coupling`, ctest
  `IpuDmaCoupling`): real DMAC/IPU/adapter callbacks, RAM-to-input channel 4
  MMIO, output-to-RAM channel 3, armed-before-output, partial drain,
  cancellation/rearm, and no overwrite of an HLE destination without output.
  Stop/restart covers 24 qwords at bit positions 0/5/127/128/133/2047/2048/2053,
  consumed prefixes, the eight-qword boundary, fresh-device and repeated
  restoration, exact address-zero rewind, address/count rejection and error
  census survival across init. The real RAW8 retail continuation remains open.
- `test_dmac_regs.cpp`: channel 4 read/write mapping; the worker's existing
  SPR/D_STAT assertions are retained.
- `test_dmac_libdma.cpp`: channel 4 now joins the reset-register assertions,
  with QWC retained. The obsolete expectation that its MMIO writes are dropped
  was removed; existing SPR and D_STAT checks remain intact.

Focused standalone compilation used `clang++ -std=c++17 -Wall -Wextra -Werror`,
logging compiled out, with binaries/logs exclusively under
`/tmp/subsystem_completion_ipu`. Reproduction scripts there:

```sh
bash /tmp/subsystem_completion_ipu/run_focused.sh
bash /tmp/subsystem_completion_ipu/run_bindings.sh
bash /tmp/subsystem_completion_ipu/run_coupling.sh
bash /tmp/subsystem_completion_ipu/run_libdma.sh
```

The first compiles device/completion/arithmetic/libipu tests directly from the
owned sources. The second directly compiles libmpeg bindings and dependencies,
links Homebrew FFmpeg, and reads the frozen intro movie without executing any
OLD binary. Both run each test through this tree's `verify/run_bounded.sh`.
The coupling script compiles current DMAC/IPU sources and links private copies
of unchanged support archives from the existing Debug build. It writes only
under `/tmp/subsystem_completion_ipu`, not the shared build directories.

Recorded results:

| Check | Result |
|---|---|
| `test_ipu_device` | PASS |
| `test_ipu_completion` | PASS; command implementation also passed AddressSanitizer + UndefinedBehaviorSanitizer before the DMA follow-up |
| `test_mpeg_decode` | PASS |
| `test_libipu_api` | PASS, 64/64 checks |
| `test_libmpeg_bindings` | PASS, 124 checks; 5 RGB32 + 4 RAW8 pictures, zero failures |
| `test_ipu_dma_coupling` | PASS, including oversized-input snapshot and arithmetic guards |
| `test_dmac_regs` | PASS |
| `test_dmac_libdma` (`DmacLibdmaRows`) | PASS, standalone current-source compile/run after the stale channel-4 assertion fix |
| Scoped `LintOneClass` (`engine/ipu tests/ipu`) | PASS |
| Scoped `LintEnginePurity` (`engine/ipu`) | PASS |
| Scoped `LintHeaderGlobals`, `LintSessionTags`, `LintSizeBudget` | PASS, zero failures; scoped scans emit unrelated/stale allowlist and size warnings |
| Owned-path `git diff --check` | PASS |

Parent next commands, when shared builds are safe to resume:

```sh
cmake --build --preset macos-dev --target test_ipu_device test_ipu_completion test_ipu_dma_coupling test_mpeg_decode test_libipu_api test_libmpeg_bindings test_dmac_regs test_dmac_libdma -j4
ctest --preset macos-dev -R '^(IpuDevice|IpuCompletion|IpuDmaCoupling|MpegDecode|LibipuApi|LibmpegBindings|DmacRegs|DmacLibdmaRows)$' --output-on-failure
```

Then parent runs its full suite/live battery and records exact per-command
counts. This handoff does not claim those integration runs have passed.

Source is ready for the integrated Debug/Release build. Focused tests and
scoped lints above pass, and this worker has frozen source edits. The remaining
RAW8 retail continuation, complete BDEC/IDEC and full FIFO scheduling claims
are explicitly not closed by this handoff.

## Parent integration follow-up

The parent's full suite found a stale `DmacLibdmaRows` channel-4 no-op
expectation. The test-only correction above passed
`bash /tmp/subsystem_completion_ipu/run_libdma.sh`, using a standalone binary
and private support archives under `/tmp`; no host/game job or shared build
was run by this worker during the parent's probes.

The initial MP1 snapshot hypothesis for `RootPlayPolicyInert` was **ruled out**.
The parent reports b60 memory identical, b120 containing 42 changed words
without an MP1-bit delta, and all 121 frame hashes identical. A clock-only
legacy variant retained all new IPU code and restored b120 exactly to
`0x96dacb45cd7e529d` from the changed `0x460fc7fc90bf4cb1`.
This is parent-supplied isolation evidence for the clock change, not an IPU
regression. No frozen fixture was changed, and no further IPU change is needed
for this failure. This report does not claim the parent's entire suite passed.
