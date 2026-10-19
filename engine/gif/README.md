# `engine/gif/`

**Phase:** 5  
**Purpose:** GIFtag parse (ParseSpan canonical) + the `GsSink` seam to the GS + the GIF register
file + the PATH1/2/3 arbiter (inline half 5A; MTVU markers / inbox / boundary slots 5G) + the
drain thread (5G); hle/ reserved.

## Belongs here

- `GifTag` (+ `GifPath`, `GifFormat`, `GsReg`, `GifTagClass`, `GifTransferState`), `GifRegs`,
  `GsSink`, `RegisterOpStream`, `GifParser`, `GifArbiter` (+ `GifArbiterPacket`, `GifMtvuMarker`,
  `GifBoundaryStats`), `GifMarkerOwner`, `GifBoundaryHook`, `GifDrainThread`

## Does not belong here

- the GS itself (Phase 6 implements `GsSink` as `GsState`); the legacy per-QW `ProcessPacket`
  entry (no caller); `PacketTemplate` (dropped: its gate defaulted OFF, zero template hits in every
  baseline); forced zbp literal (RendererProfile.vram); any game constant

## Files

| File | Class / types | OLD source (read-only) |
|---|---|---|
| `gif_tag.h/.cpp` | `GifTag` (`Parse(lo,hi)`), `GifPath`, `GifFormat`, `GsReg`, `GifTagClass` + `ClassifyGifTag`, `GifTransferState` | `host_app/gif.h:15-26, :29-87, :89-112`; `host_app/gif.cpp:3646-3673` (eligibility predicates) |
| `gs_sink.h/.cpp` | `GsSink` — abstract, 10 pure virtuals, one per GS mutation the parser performs, plus `ImageSpan(data, qwords)` (default: the per-quadword `ImageData` sequence; the parser delivers a tag's IMAGE quadwords as one span and `GsStateSink` stores it whole — track 1d) | the `g_gs_state` surface reached from `host_app/gif.cpp` (`:3641`, `:3926-3928`, `:3933`, `:4317`, `:3211-3264`, `:4541-5224`, `:4419-4422`) |
| `null_gs_sink.h/.cpp` | `NullGsSink : GsSink` — every call a no-op: the sink of a parser whose consumer is not the GS (the VU1 AOT tier's verifier judges a program by the bytes it kicked, taken at the arbiter's submit tap; `ps2r_gif`, no GS dependency) | NEW |
| `hle/gs_state_sink.h/.cpp`, `hle/gs_sink_fanout.h/.cpp` | `GsStateSink : GsSink` over the GS façade and `GsSinkFanout : GsSink` (library `ps2r_gif_hle`, integration item 6.4) — see `hle/README.md` | `host_app/gif.cpp:4374-4425` (FinishDMA flush) |
| (moved) `engine/gs/hle/ir/register_op_stream.h/.cpp` | `RegisterOpStream : GsSink` — the recording sink now lives beside the DrawIR records (Phase-5 checklist (h) move 1); `GsSink` stays here | — |
| `gif_regs.h/.cpp` | `GifRegs : MmioHandler` — CTRL/MODE/STAT/TAG0-3/CNT/P3CNT/P3TAG, VIF PATH3 mask, owns `GifTransferState` | `host_app/gif.h:120-202` (members); `host_app/gif.cpp:185-206` (Reset), `:215-300` (Read), `:302-341` (Write) |
| `gif_parser.h/.cpp` | `GifParser(GsSink&, GifRegs&, GifParserSettings)` — `ReceiveData` (size-0 drop, PATH3 stale reset, zero-copy span parse + tail buffer, M3R hold; per-path `Receives`/`ReceivedBytes` census = the byte-form parse that executed), `ParseSpan` canonical walk (BeginTag, PRE→PRIM, NLOOP=0, IMAGE bulk, PACKED strip3/rowable lanes + per-descriptor remainder, generic PACKED body, REGLIST two per QW + odd pad, FLG=3), the PACKED decoders, `DispatchRegister` → `DispatchGifRegister`, `FinishPath3`; `GifParserSettings{bulk_lanes}` is the equivalence-test knob, not a setting; `StageTriple` NEON twin under `PS2R_SIMD` | `host_app/gif.cpp:552, :588, :647-658, :733-745` (ReceiveData rules), `:3183-3200` (ProcessBuffer), `:3211-3264` (decoders), `:3266-3359` (SIMD triple), `:3585-4324` (ParseSpan), `:4374-4425` (FinishDMA), `:4541-4699` (ProcessPacked), `:4700-5156` (ProcessReglist), `:5157-5182` (ProcessImage) |
| `gif_register_dispatch.h/.cpp` | `DispatchGifRegister(GsSink&, reg, value)` — the one 64-bit register dispatcher (REGLIST halves and PACKED A+D pairs): PRIM masked to 11 bits, the four vertex registers as kicks (64-bit forms always draw for XYZ2/XYZF2), the forwarded-register table, the drop table (0x0B, 0x0E, 0x0F, 0x1C, undefined). Shared by the parser and `engine/vu/hle/KickLowering`'s register-block route, so a block delivered directly produces the parser's sink sequence | `host_app/gif.cpp:4700-5156` (the REGLIST / A+D dispatcher) |
| `gif_mtvu_marker.h` | `GifArbiterPacket` (path, is_directhl, is_path3_image, data, boundary_seq, bnd_enq_ns) + `GifMtvuMarker` (filled / sealed / detached) PODs | `host_app/gif_arbiter.h:85-121` |
| `gif_marker_owner.h` | `GifMarkerOwner` — abstract: `Running / OnWorkerThread / ActiveMarker`, the three routing questions the arbiter asks the MTVU worker (null = inline routing); keeps `engine/gif` free of VU/VIF headers | `vu1_async::IsRunning / OnWorkerThread / ActiveMarker` as used by `host_app/gif_arbiter.cpp:657-716` |
| `gif_boundary_hook.h` | `GifBoundaryHook` — abstract `OnBoundary(seq, enqueue_ns)`, ctor-injected into `GifArbiter` (replaces OLD's static `SetBoundaryHook` slot) | `host_app/gif_arbiter.h:335-402` (`BoundaryFn`) |
| `gif_arbiter.h/.cpp` | `GifArbiter(GifParser&, GifBoundaryHook* = null)` — inline half (5A): `submit/submitOwned` (sub-tag payloads dropped), buffer pool `acquireBuffer/recycleBuffer` (`kPoolMax=4`, 4 KiB..64 KiB), `drain()` in SUBMISSION ORDER → `GifParser::ReceiveData` per packet + `FinishPath3()` once per pass that delivered PATH3, `SetSubmitObserver`. MTVU half (5G): `openMarker/sealMarker/hasSealedReady` + private `collectReady` (ready-prefix promotion: plain slots + SEALED markers, stop at the first unsealed), the EE inbox (`inbox_` under `mtvu_mtx_`), routing (worker-thread submit → active marker; any submit while the worker or drain thread runs → inbox tail; else queue), `openBoundary(seq, enqueue_ns)` (0 → 1; feed-note tripwire) / `noteFeedForBoundary` / `dispatchBoundaryInline` (the ONE place a boundary slot is recognised, before the empty-data guard) / `boundary_stats()` (enqueued, delivered, order_violations — replaces the `vu1_async::NoteBoundary*` counters), `PendingBytes/PendingSlots` relaxed-atomic ledger (0 when the pipeline is empty), `clear()` (detach markers the worker still owns; bytes settle at their seal), `hasPending()` three-way, `SetMarkerOwner`, `AttachDrainThread/DrainThreadRunning/OnDrainThread/DrainFence` (foreign `drain()` while the thread runs = kick), `SetDrainObserver` (per-packet tap before delivery, boundaries included — the replay tool's drain-order record). Every cross-thread member carries its one-line contract in the header | `host_app/gif_arbiter.h:85-121, :123-195, :229-297, :300-333, :335-402, :403-452, :559-606`; `host_app/gif_arbiter.cpp:20-26, :579-783 (routing :657-716, :738-775), :933-951, :1138-1156, :1377, :1626, :1755-1808` |
| `gif_drain_thread.h/.cpp` | `GifDrainThread(GifArbiter&)` — `Start(on_start, user)` (attaches to the arbiter; must precede producer traffic) / `Stop()` (one final pass, join, detach; never hangs on a non-empty inbox) / `Running` / `OnDrainThread` / `Working` / `Notify` / `PassGeneration` / `DrainFence` (gen ≥ mark+2) / `DrainIdleGeneration` (savestate quiesce predicate: 1 when stopped, 0 while working or holding, else gen+1). Started ONLY by the composition root under `engine.mtvu`; the PLAN's `GifParser&` ctor parameter is dropped (the arbiter owns the parser) | `host_app/gif_arbiter.h:454-557, :607-679`; start rule `host_app/main.cpp:14228-14239`, stop order `:14780` |

## Register file notes (OLD behaviour carried verbatim)

- Window `0x10003000..0x100030FF`, 32-bit registers at 16-byte stride; the access width is ignored.
  `GifRegs` is NOT registered with `MmioRouter` in Phase 5 (integration checklist: range
  `0x10003000`, size `0x100`, displacing the `MmioSink` idle row `GIF`).
- STAT is composed live: M3R = MODE bit 0, M3P = the VIF MSKPATH3 mask, IMT = MODE bit 2,
  PSE = CTRL bit 3, OPH + APATH=3 while a tag is in transfer, FQC = FIFO quadwords (nothing ever fills
  the FIFO, so FQC reads 0); P1Q/P2Q/P3Q never set. APATH reads 3 regardless of the real path.
- CTRL write: RST first (zeroes ctrl, mode, fifo, VIF mask and the transfer; does NOT clear the
  parser's buffered tail), then PSE latched from the same write; no other CTRL bit is stored.
  MODE keeps bits 0-2 only.
- TAG1 readback shifts the normalised NREG=16 out of the 32-bit word (reads 0 = the hardware
  encoding); after reset the tag reads all zero (nregs 0, not 16).
- MODE.M3R holds received data in the FIFO for every path, not only PATH3 (parser rule: the
  data is appended to the tail and `ProcessBuffer` returns while the mask is set; the next
  delivery after the mask lifts parses the whole tail).

## Parser / arbiter notes (OLD behaviour carried verbatim)

- A PATH3 packet arriving while a tag is in transfer abandons that tag and clears the tail
  (every PATH3 chain is a self-contained stream); PATH1/2 continuations resume the tag. This
  also discards a mid-IMAGE transfer that `FinishPath3` deliberately kept open.
- NLOOP=0 with EOP=0 leaves the tag in transfer: under an A+D / unknown descriptor set the
  next quadword is consumed as one register write; under an all-known set the bulk branch
  consumes nothing and the next tag parses intact (the bulk branch is the only PACKED path
  for such tags).
- FLG=3 (DISABLED) consumes one quadword per step with no counter advance; the tag never
  completes.
- REGLIST / A+D XYZ2 and XYZF2 always draw (the 64-bit form carries no ADC bit); PACKED
  kicks honour ADC. PRIM is masked to 11 bits on every route (PRE, PACKED, REGLIST).
- Registers the GS never applied are not delivered: reserved 0x0B, A+D 0x0E, NOP 0x0F,
  TEXCLUT 0x1C and every undefined number are dropped by the dispatcher.
- PACKED FOG is delivered as the 8-bit F field; the OLD GS stored it as the PRIM fog-enable
  flag (f != 0), which Phase 6 must reproduce for identity.
- The strip3 / rowable lanes enter only when the run starts on a descriptor boundary; a span
  split mid-vertex or mid-row rides the per-descriptor remainder for the whole run and the
  next span re-enters the lane only if aligned then. The sink sequence is identical either
  way (`GifParserEquivalence`).
- `FinishPath3` (OLD `FinishDMA`): a mid-IMAGE transfer is kept open, nothing else happens
  (tail kept); any other incomplete tag is abandoned, the GS is told to finish, the tail is
  cleared. The arbiter calls it once after a drain pass that delivered any PATH3 packet.
- PATH1 packet buffers are pooled: `XgKick` hands the arbiter the buffer it staged the packet in
  (no second copy); the drain owner collects a pass's PATH1 buffers (capacity <= one VU1 data memory)
  and returns them under one lock (`recyclePath1Buffers`, bounded by `kPath1PoolMax`); the VU1 unit
  takes them back in batches (`acquirePath1Buffers`). Larger buffers fall to the staging pool's rule.
  `openMarker` reserves `kMarkerReservePackets` slots for the worker's fills.
- Submission order is the arbiter's whole contract: the order packets were submitted is the
  order the GS sees them; no path-priority sort. `is_directhl` / `is_path3_image` are computed
  and unused (DIRECTHL pre-empting a PATH3 IMAGE is not modelled).
- `GifDrainThread` is started only under `engine.mtvu` (composition root); the deterministic
  profile never starts it, so every harness leg runs the inline drain with no owner and no thread.
- MTVU order contract: the EE opens a marker at the stream position the span's packets would have
  had; the ONE worker fills it (its submits route to the active marker) and seals it; EE-side
  submits and boundary slots append at the inbox tail; the drain owner promotes the ready prefix
  and never blocks; a true sync point (`MtvuSync::Sync`, 5G part 2) seals everything first.
  `FinishPath3` stays per drain pass. `collectReady` runs whenever the inbox holds a slot
  (`PendingSlots() != 0`), not only while the worker runs (OLD gated it on `IsRunning()`; with the
  drain thread up and the worker down OLD's inbox was filled but never promoted — the NEW gate is
  observably identical wherever OLD's ran and covers that case).
- The MMIO fence (OLD `memory.cpp:897, :1078`): every GIF register read/write on the EE calls
  `GifArbiter::DrainFence()` first while the thread runs — an integration row (`GifRegs` is not
  routed this phase).

## Transplanted from (OLD, read-only)

- `gif.{h,cpp}, gif_arbiter.{h,cpp}, gif_template.h (decision only), w10_eefeed.h (B2 arm becomes
  unconditional), s8_ctxlatch.h (its order rule IS the submission-order contract; file not written)`
- 5G flags: `PS2_GIF_DRAIN_THREAD` NORM (class permanent, started under MTVU); the `PS2_PERF` loop
  census DIAG omitted; `PS2_R198_PATH3DEFER*`, `PS2_SHADOW_TAILFIX` (`s8tail::DropHeld`),
  `PS2_S8_PKTDUMP*`, the exec-thread `OnExecThread()` disjuncts — not written.

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- `engine/gif` includes no `engine/gs/` header other than `engine/gs/format/*.h`; `ps2r_gif` links
  `ps2r_core`, `ps2r_ee`, `ps2r_gs_format` only.
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4; stage record: `docs/phases/PHASE5_IMPL.md`.
