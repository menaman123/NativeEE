# `engine/dmac/`

**Phase:** 5  
**Purpose:** The EE DMA controller: ten channel register files + the global registers behind one
`MmioHandler` (not registered with the router this phase), the VIF0/VIF1/GIF DMAtag chain walker and
normal-mode transfers, the deferred VIF1 chain kick + END-slot tail chase, the kernel DMAC-handler
registry, and the `TAG_DMAC` savestate codec (exposed, unregistered). `hle/` = the sce libdma surface
(bodies + manifest `Bind`; the composition root installs; see `hle/README.md`).

## Files

| File | Class | Content | OLD source (`host_app/`) |
|---|---|---|---|
| `dmac_channel.h` | types only | `DmacChannelRegs` (7 × u32 = 28 B, the frozen savestate record), `DmacChannelId`, `DmacChcr` / `DmacStat` / `DmacRegOffset` / `DmacAddr` / `DmacTagId` constexpr | `dmac.h:23-61` |
| `dmac.h/.cpp` | `Dmac : MmioHandler` (+ `DmacGlobalRegs` POD) | register file; `Read` by channel RANGE (`0x8000..0xDFFF` → channel, seven register offsets, others 0), globals `0xE000-0xE060`, `0xF520` (D_ENABLER), `0xF590` reads 0; `Write` by 256-byte PAGE keyed on the `0xX000` values — PCR writable (CPC bit flushes a parked VIF1 kick), D_STAT status W1C / mask toggles; other globals dropped, the SIF1 / SIF2 pages dropped; both IPU and SPR pages writable; sub-word widening (`addr & ~3`; a sub-word store writes the raw value as the whole register; 8 bytes = two halves); CHCR STR → `StartChannel` → post-kick STR clear unless VIF1 parked, IPU output pending, or SPR request unsupported; `StartChannel` per channel (VIF1 chain → park; VIF0 chain inline; normal; GIF flushes the parked VIF1 first; IPU FROM/TO and SIF1 through injected `std::function` slots, null = complete only; SPR normal copies in both directions and SPR_TO source chains through DmacSprTransfer); `CompleteChannel` (STR clear, CIS, QWC 0, INT1 slot when CIS & CIM; BEIS also asserts INT1); `CheckInterrupt`; `DispatchInterrupt` (`$a0` = channel, `$gp`, pc/`$ra` saved, `-1` stops the chain, CIS acknowledged); `SavestateSection()` / `SerializeSection` / `ValidateSection` / `RestoreSection` | `dmac.cpp:1045-1253, :1254-1590, :2851-2864, :2927-3000`; `savestate.cpp:1183-1208, :1810-1832` |
| `chain_walker.h/.cpp` | `ChainWalker` (+ `DmacVifWalkResult` POD) | `WalkVifChain` (latch reset, FDR / mode / VIF1 TADR 0 early returns, `tadr & 0x0FFFFFFF` + scratchpad prefix, both-halves-all-ones terminator, CHCR.TAG mirror, the eight ids with the two-deep ASR stack, TTE quadword = tag words 2-3 zero-padded fed inside `SetTteFeed`, spans through `Vif::ProcessData`, IRQ+TIE, `kMaxTags = 10000`), `VifNormal` (`madr & 0x01FFFFFF`, RAM only), `WalkGifChain` (full-address tag reads, `lo == ~0 || qwc == 0xFFFF` terminator, REFE/REF by SPR bit and every other id by address prefix, ONE `submitOwned(Path3)` per chain, never drains), `GifNormal` (`submit(Path3)`), the VIF1 staged-feed hooks (stage 5G's MTVU worker; null = inline) | `dmac.cpp:495-987, :988-1044, :1592-1650, :1664-2707` |
| `dmac_deferred_vif1.h/.cpp` | `DmacDeferredVif1` (+ `DmacVif1TailRecord` POD) | park of the kick-time registers, `FlushDeferredVif1` (restore snapshot, run the bound walk, STR clear, arbiter drain), `ChaseVif1Tail` (flush, re-read every END-QWC-0 slot, `IsSaneRewrite` gate, resume, drop the batch, drain), `RecordTail` (dedup per slot) | `dmac.h:111-161`; `dmac.cpp:1383-1414, :2580-2600, :2708-2850` |
| `dmac_mtvu_feed.h/.cpp` | `DmacMtvuFeed(ChainWalker&, MtvuWorker&)` | installs the VIF1 staged-feed seam (`ChainWalker::SetVif1StagedFeed` ← `MtvuWorker::StagedFeedThunk / StagedFlushThunk / StagedResetThunk`) so every VIF1 walk is one staged Batch on the worker (spans + TTE quadwords as records, flushed at the walk exit; the walk-head latch reset as a ResetLatch segment); `Uninstall` / destruction restore the inline `Vif` feed. Constructed by the composition root only under `MtvuSettings::enabled` (the harness path never constructs it) | `PS2_EEFEED` B1 of `dmac.cpp:1664-2707` (the staged submit is the only feed shape); the worker side `vu1_async.cpp:1235-1364` |
| `hle/libdma_bindings.h/.cpp` | `LibdmaBindings` | the sce libdma.a bodies as `void (CpuContext&)` members over `Dmac&` + `MainMemory&` + `Scratchpad&`, every register access through `Dmac::Read/Write` (the page decides what lands): `SceDmaGetChan` (the channel's REGISTER block `0x10008000` … `0x1000D400`, 0 outside 0..9, writes nothing — the Phase 5 descriptor arena is retired, P4-CKPT0-LIBDMA-TABLE), `SceDmaReset` (non-SIF channels SADR/CHCR/TADR/MADR/ASR1/ASR0 ← 0, the two D_STAT stores, an all-zero env, mode 1 sets DMAE; → the DMAE bit read at entry), `SceDmaPutEnv` (20-byte sceDmaEnv decode with the STS/STD/MFD hardware encodings; 0 / −1..−4 with nothing written), `Memclr` (RAM + scratchpad zero fill, a byte outside both dropped + counted), the libgraph-band sync seams `SceDmaSync` / `SceGifSyncDma` (`FlushDeferredVif1()` then 0), and `Bind(SdkBindingManifest&, LibdmaBindings*)` registering the four `libdma.a` rows through thunks over the instance (the composition root calls it; see `hle/README.md`) | ELF libdma (`FUN_002c1eb8/1ef0/1f18/1ff8`); OLD `sce_hle/libdma.cpp:22-79` (structure), `gs_hle.cpp:936-957, :965-986` |
| `dmac_handler_registry.h/.cpp` | `DmacHandlerRegistry` (+ `DmacHandler` POD) | flat table grouped by cause ascending / insertion order within a cause (the reference's per-cause vectors in a sorted map); `Add` (cause 0..15, pc ≠ 0, `next` 0 = head / −1 = tail / before id), `Remove` (cause 0..9, remaining count), `SetActive`, `ForCause`, `Install` / `NextId` (savestate + syscall-table hand-over) | `dmac.h:9-16, :174-177`; `dmac.cpp:3003-3030`; `syscalls.cpp:1725-1810, :2275-2310` |

Not written (flag outcomes / dead code): `PS2_BAKE_DIAG`, `PS2_SHADOW8_KTAP` (DIAG, omitted); `PS2_S8_ARENA_MINVF`,
`PS2_S26_DECAL` (not written); `PS2_EEFEED` B1 = the only MTVU feed shape (staged hooks + one flush per walk exit);
every perf / census / trace probe; `DMAC::Reset()` (no caller in the reference — a fresh `Dmac` is all zero, D_ENABLER
reads 0); `SuspendedVifChain` / `ResumeSuspendedDma` / `HasSuspendedDma` (never armed in the reference).

Remaining limitations: GIF normal mode clears STR without CIS; the IPU TO / SIF1 / SIF2 register pages
are write-dropped (direct StartChannel entry remains available). A GIF chain's PATH3 packet stays queued until
the next VIF1 flush, HLE sync or frame boundary drains it. These are residual work, not required bug compatibility.

VIF/GIF memory model: tags and spans are read through `MainMemory::Pointer` / `Scratchpad::Pointer`; an out-of-range read yields
zeros (tags) or skips the span with MADR/QWC untouched (the reference's failed translate). The GIF walk's span pointer is
bounds-checked over the whole span here (the reference checked one byte) — identical on every in-range chain.

## Scratchpad transfers and residual coverage

`dmac_spr_transfer.h/.cpp` implements RAM ↔ SPR normal copies and RAM → SPR source chains
(CNT/NEXT/REF/REFE/END, full-tag burst TTE, packet-end IRQ/TIE). It advances MADR/QWC/SADR,
wraps scratchpad addresses, and completes with CIS. The SPR_TO MMIO page is writable.
The obsolete no-copy behavior is removed. D_STAT acknowledges status with write-one-to-clear
and toggles masks on writes of one; BEIS is unmaskable and can be acknowledged for recovery.

`dmac_transfer_stats.h` defines `Dmac::TransferStats()` lifetime observer counters:
`spr_from[4]` / `spr_to[4]` indexed by raw MOD, each with `starts`, `completed`, `quadwords`,
`rejected`, `unsupported`, `faults`. `unsupported_spr_ctrl_writes` counts ignored D_CTRL writes
requesting SPR MFIFO/stall-source modes, so a subsequent ordinary start cannot hide the request.
Parent acceptance must include this counter as well as all eight direction/mode buckets.
Counters and bounded first-error log state never enter the existing TAG_DMAC wire format.

Still unsupported: SPR_FROM destination chains; interleave; reserved MOD; zero-QWC normal starts;
VU memory; scratchpad source chains; nonzero tag priority control; MFIFO/stall coordination;
and source walks over the 10,000-tag host limit. Unsupported starts remain STR-busy without
new completion status. Invalid tags terminate with CIS; bad memory accesses additionally set
BEIS, retain remaining QWC, and count faults. No cycle/arbitration model is added. D_CTRL,
D_ENABLEW, D_SQWC and MFIFO control writes remain dropped. Terminal REFE/END TADR readback
is retained at the terminal tag and still needs hardware validation.

The bounded `DMAC` Error diagnostics require error-level logging or higher and flush immediately.
See [subsystem_completion_dma.md](../../docs/design/subsystem_completion_dma.md) for primary
manual references, title source/NTSC evidence, exact residual tasks, census mapping and tests.
The old `DmacSprStub` is replaced by `DmacSpr`; the new test also proves fault acknowledgement,
retry, ignored-control census and unchanged savestate bytes.

## Belongs here

- `Dmac`, `DmacSprTransfer`, `ChainWalker`, `DmacDeferredVif1`, `DmacHandlerRegistry`, the channel POD/constants; `hle/LibdmaBindings` (bodies + `Bind`; the `Bind` call is the composition root's).

## Does not belong here

- Router registration, `BoundarySequencer` steps (`ChaseVif1Tail` at SyncV / WaitForVblank), the syscall-table hand-over,
  the SIF1 / IPU bodies (`engine/sif`, Phase 7), `TAG_DMAC` registration — integration (`docs/phases/PHASE5_IMPL.md`).

## Transplanted from (OLD, read-only)

- `dmac.{h,cpp}`, `sce_hle/libdma.cpp` (structure; the libdma bodies follow the ELF's library).

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4; stage record: `docs/phases/PHASE5_IMPL.md` (5F).
