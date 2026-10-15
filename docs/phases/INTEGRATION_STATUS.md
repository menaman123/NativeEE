# INTEGRATION_STATUS — one paragraph per gate run (autonomous mode from 2026-09-30 09:46)

Read with `docs/phases/DECISIONS_LOG.md`. Full records: `docs/phases/INTEGRATION_IMPL.md`.

## 2026-09-30 09:50 — start of autonomous mode
Position: INTEGRATION_PLAN §2 items 4.1–4.9 closed or carried as recorded; 4.10 (async-read completion model) LANDED at unit
level, blocked at leg level on the load-wait spin (T0 slice-frozen by design) → the user approved option (a): bind the
Phase-4 `LoaderModel::ShouldYieldPollSlice` spin-site lift (I4.10-C). Phase-4 memory-lane gate: NOT PASSED — coldboot
IDENTICAL-UNDER-MASK through boundary 59, no boundary 60 yet; 11 anchors DIVERGENT at checkpoint 0 (un-landed 5.3 / 6.2 /
sink (c) / the STREAM-REPLY-PATCH words, coldboot-only mask). Next: I4.10-C (lift + pin test + BUG-041 docs), I4.10-D
(gate + INT-I410A-ISSUE-SLICE attribution), then Section 3.

## 2026-09-30 12:30 — gate run I4.10-D (after I4.10-C: the LoaderSpinLift at both load-wait sites)
Checkpoint reached: coldboot now PRODUCES boundary 60 (`60,0x8d000860de96f0b5`); the Phase-4 memory-lane gate is NOT PASSED
— boundary 60 is DIVERGENT under mask with every region attributed: (1) the first load-wait poll runs in slice 60 in NEW and in
slice 61 in OLD, because OLD's headroom predicate under the deterministic profile is its S96/S112 host budget model (synthetic
first-frame base 8800 counts), not a clock — an "OLD is wrong" claim → **DP-3** (proposed §7.1 row, no mask, awaiting you);
(2) OLD's cdvd-delivery-fired vblank ticks advance T0 by 2 quanta by frame 60 → **DP-4 (a)**; (3) an OLD-only path-string residue at
0x1effef0 → **DP-4 (b)**. DP-2 superseded by DP-3. Checkpoints 120+ are absent: NEW stalls at frame 63 in the title's completion
copy after a consume (INT-I410C-F63-RUNAWAY-COPY, a byte store outside RAM) — NEW-side, being attributed next (I4.10-E).
Anchors: 11/11 run, all DIVERGENT at checkpoint 0 (un-landed 5.3/6.2, sink (c), STREAM words) — unchanged. Items: §2 4.10 closed
at leg level (boundary 60 produced; BUG-041 docs landed); §2 remaining: 4.1 (gate), 4.3 (when a leg shows archive misses), 4.7
(BUG-026 re-run past frame 60). Pending decisions: DP-3, DP-4 (a)/(b). Commits: f4398be → this run's commit below.

## 2026-09-30 14:45 — gate run I4.10-F (after I4.10-E: BUG-042, the oversized-window ring routing fixed and pinned)
Checkpoint reached: coldboot hashes to frame 73 (boundary 60 produced; 120 absent). First divergent checkpoint 60 — byte-identical to
run 1 (same three classes: DP-3 one-poll issue shift, DP-4 (a) OLD delivery-fired T0 ticks, DP-4 (b) 0x1effef0 residue); no new
checkpoint attribution needed; consecutive runs on 60 = 2 (a third without new attribution is a MUST-STOP). Progress: the frame-63
runaway completion copy is FIXED — the retail `FUN_002aac80` routes a window ≥ ring cap straight to dest with nothing staged; NEW staged
unconditionally (BUG-042, pinned by `LoaderCompletionOversized`, 11 checks fail without the fix). New NEW-side stall at frame 73:
a second load-wait poll loop (level-load dispatch, `FUN_002b4a78 → … → FUN_001e3308`) sees STREAM slots 0 and 2 stuck at "issued" with
no in-flight read in the model (INT-I410F-F73-SLOT-STUCK-ISSUED) — construction gap candidate, next stage. DP-5 written by I4.10-E
(four further retail≠OLD=NEW loader gaps; the landed routing's NEW-vs-OLD consequence at ≥ 120). Anchors: 11/11 DIVERGENT at
checkpoint 0, unchanged (one leg needed a re-run alone: host load). Audio W0–11 identical. Pending decisions: DP-3, DP-4, DP-5.

## 2026-09-30 19:05 — gate run I4.10-J (run 3 on checkpoint 60) → MUST-STOP DP-7 written; fix path pre-authorized
Checkpoint reached: still 60 (frames hashed to 73); the same three classes at 60 (DP-3, DP-4 a/b), nothing new → the "three consecutive
runs" rule fired (DP-7). Root cause of the frame-73 stall now fully traced: the title stages STREAM records itself (0x7e/0x7f), the
recompiled module serves them, but its CD worker polls `sceSifDmaStat` until NEGATIVE while NEW's sifman HLE returns 0 forever (a wrong
SDK constant, `engine/iop/hle/sifman/sifman_module.cpp:22`) — so the DP-6 (a) seam (landed, I4.10-I: the wait yields to the cooperative
IOP for module-owned slots; 324/324 green) cannot retire the slot yet. Next: I4.10-K (sifman constant fix + pins + BUG-043 docs + record
corrections), I4.10-L (gate run 4 with OLD b120: the DP-3 re-convergence test). Anchors: 11/11 DIVERGENT at checkpoint 0, unchanged.
Pending decisions for you: DP-3, DP-4 (a)/(b) (DP-1, DP-6, DP-7 (a) decided under pre-authorization — veto possible).

## 2026-09-30 20:10 — gate run I4.10-L (run 4): checkpoint 120 produced; DP-7 MUST-STOP lifted; coldboot reaches the first FMV
Checkpoint reached: 120 (coldboot runs to frame 135 and parks at `libipu.a sceIpuInit` — the first FMV = sink (d), the Phase-4 stopping point
for coldboot). Fixes: sifman `sceSifDmaStat` SDK constant (−1 = complete; pin `SifmanDmaStatComplete`) and the wait seam's per-lap real
`Ship(0)` (I4.10-K); BUG-043 docs. Checkpoint 60: unchanged three classes (DP-3, DP-4 a/b). Checkpoint 120: DIVERGENT; DP-3's re-convergence
CONFIRMED on the loader lane (record count, ring head, dest, fs objects agree); five classes attributed in **DP-8**: (1) NEW-side T0 over-advance
— the seam's per-lap sync RPC/IOP waits fire a Demand tick (one field of T0) per kernel wait, 484 extra quanta by 120 — a timing-domain
question (MUST-STOP; attribution stage next, decision yours); (2) a heap-layout shift (~411k words, same assets at −0x5f3c0; attribution
stage next); (3) DP-4 (b) ×3 OLD-only payload buffers; (4) DP-5's ring tail materialised; (5) STREAM client counters (lap-count derived).
Audio W0–21 identical. Anchors 11/11 unchanged (DIVERGENT at 0; the seams never engage there). Open review item: `LoaderCompletionModule`
pin is DISABLED until its rig opens the ring stream (cmd 13) — fix-up stage next. Pending decisions for you: DP-3, DP-4, DP-5, DP-8 (1b, 3–5).
Section 2 is otherwise exhausted; after the attribution stage the run moves to Section 3 (Phase 5 items 5.2–5.9) with 6.2 pulled forward
for the anchors (reason recorded when done).

## 2026-09-30 21:10 — I4.10-M closed (no gate run): DP-8 (1) attributed — the T0 over-advance is the Phase-3 wait seam at 0x191bd0
The `LoaderCompletionModule` pin is enabled and green (326/326; red-without-fix proven three ways). DP-8 class (1): NOT the seam's RPC waits
— every one of the 484 extra Demand ticks comes from the headroom query `FUN_00191bd0` being modelled as a wait (`hle_TimerWaitMicroseconds`);
my recommendation (unbind that seam, run the recompiled predicate) is in DECISIONS_LOG under DP-8 — a timing-domain item, yours to decide.
DP-8 class (2) (heap shift): first differing allocation not yet named (recipe recorded). Section 2 work is complete except the gate verdict,
which waits on DP-3/4/5/8. The run now moves to Section 3 (Phase 5 items), starting with 5.7 (VU/VIF/GIF/DMAC savestate sections — the
anchors restore cold VU/VIF/GIF/DMAC state today, the likeliest first-checkpoint divergence), then 5.5 (boundary steps + libdma sync
seams), then 6.2 pulled forward (GS privileged page; reason: anchors read it at their first frame), then the Phase 5 lane gate.

## 2026-09-30 22:40 — Section 3 items 5.7 and 5.5 closed (no gate run)
5.7: the VU0C/VU0D/VU1C/VU1D/VIFS/GIFS/VU1R codecs in OLD's byte layouts + TAG_DMAC registered in OLD's section order; all 11 anchors decode and
re-serialise byte-identically (except VU1R's running byte: OLD saved the MTVU worker running=1, NEW writes 0 — recorded); process pin
`RootRegistersVuVifGifSections`. 5.5 rows 1–2: `dmac.ChaseVif1Tail()` runs before every SyncV boundary (and the WaitForVblank mirror);
`sceDmaSync`/`sceGifSyncDma` are libdma manifest rows (6 rows) and `sceGsSyncPath` flushes the deferred VIF1 kick; pins `DmacTailChaseAtBoundary`,
`RootBindsDmacSyncSeams`; MTVU wrapping carried to 5.6 (play mode). Suite 333/333. Evidence: the anchors' boundary-2 divergence vs OLD is
UNCHANGED (artdet: 38,881 RAM words / 750 SPAD) — VU0C/VU1C/DMAC/VIFS/GIFS now restore byte-identical to OLD, but VU0D/VU1D/VU1R differ
because NO VU program runs in NEW's anchors (`vu1_kicks=0`, no VIF1 kick parked, coldboot line 60 unchanged). Next: an evidence stage attributing
the anchors' first-checkpoint divergence region by region (why the title's render path never kicks VIF1/VU1 in NEW: GS privileged reads from the
sink (6.2), a CMSAR1 path, sink (c) readbacks?), then the wiring it names (6.2 pull-forward expected). Pending decisions unchanged (DP-3/4/5/8).

## 2026-09-30 23:40 — I5.G1 (evidence): the anchors' divergence is attributed — two composition-root omissions, not Phase-5/6 work
VU1 DOES run in NEW's anchors (`vu1_hle=5189/6/0`, `gsops_ops=975106`; the `vu1_kicks` key counts CMSAR1 starts only). The first point where
NEW's slice-1 control flow leaves OLD's is the STREAM.IRX RPC reply: NEW's module answers as at cold boot because the anchors' IOPM section (IOP RAM
+ kernel statics; `IopmSection::MakeSpec` exists since Phase 4) is NEVER registered in the composition root — the title then clears its stream
tables, re-sends stream commands, starts a fade, and the heap cascades (≈99 % of the 38,881 words; same classes on f46203 and slowspot). Second,
independent: libmc rows return −100 at anchors because `McCommandModel::Bound` is raised only by the pre-anchor mcserv init. GS privileged page:
no RAM footprint at anchors (frame lane only). The I5.1-F proposal to extend the STREAM-REPLY-PATCH mask to anchors is WITHDRAWN (the words are the
cold module's). Next: register the IOPM section (+ pin), derive `Bound` at restore (+ pin), re-run the anchors → the Phase 5 lane gate attempt.

## 2026-10-01 00:50 — I5.G2 closed; gate run I5.G3 (Phase 5 lane gate attempt): NOT PASSED, 12/12 DIVERGENT, new attribution on the anchors
I5.G2: the IOPM savestate section is registered (anchors restore the IOP RAM + kernel statics; the STREAM reply now matches OLD's progression) and
libmc's readiness is DERIVED from libmc 3.0.1's own client record (the serve word of its `sceSifClientData` at the title datum 0x3c8580 — an EE
datum, not an invented flag); pins `RootRegistersIopmSection`, `McBoundDerivedFromClientRecord`; plan rows 4.11/4.12. Suite 335/335. The fix moved
only ~190 of the anchors' 38,881 differing words: I5.G1's "cold reply → heap cascade" hypothesis is refuted. I5.G3 attributed the real upstream:
after a restore NEW's STREAM.IRX streaming worker thread (tid 2, prio 40, the module's `for(;;) WaitSema` loop) is DORMANT in the live kernel while
the anchor/OLD carry it active (its semaphore holds an unconsumed token) — the music stream never resumes (`libsd_trans=0/0/0`, `cdvd_deliveries=0/0`
on every anchor; audio window 0 differs on 8/11 anchors), the stream position words differ, and the heap class follows. A NEW construction gap
(IOPM reconcile / module boot) — next stage. Coldboot unchanged (60/120 classes = DP-3/4/5/8; FMV park at 135). Pending decisions: DP-3/4/5/8.

## 2026-10-01 02:40 — I5.G4 closed (no gate run: the I5.G5 battery stage was cut by the pipeline's idle deadline — re-queued)
The STREAM.IRX streaming worker (tid 2) was never CREATED after an anchor restore: the module's entry creates only tid 1, the worker is created
lazily by the title's stream-open RPC — which the restored RAM says already happened (OLD applied its load-state after the init frame; NEW
restores before any EE frame). Fix (construction gap, pre-authorized): `ThbaseModule::RestoreThread` + `StreamIrxPlugin::BootSymmetricRepark`
re-create every image thread record and dispatch it to its first park (the boot contract); pins: the IOPM oracle extended (red without the fix),
process pin `RootRestoresStreamWorker` (the stream position word now advances as OLD's: 0x5f20 → 0x6050). Suite 336/336. The anchors' stream
position class is closed; the IOPM lane equals OLD's at boundary 2. Remaining anchor divergence (artdet 38,663 words) is the HEAP class — upstream
still unnamed; new lead: the worker now runs the voice path but `cdvd_deliveries=0/0` on every anchor (the title-armed CD job / hard timer may
also need boot-symmetric re-arming after restore). Next: I5.G5 (gate attempt #2, re-queued) in parallel with an evidence stage on the CD-delivery
lead + the HEAP first-differing-allocation. Pending decisions unchanged (DP-3/4/5/8).

## 2026-10-01 03:30 — gate run I5.G5 (Phase 5 lane gate attempt #2): NOT PASSED 12/12; BOTH legs are now blocked on DP-8 (1) — your decision
I5.G4's worker fix is confirmed end to end on every anchor (worker restored and parked on its semaphore; libsd voice path runs), and the anchors'
remaining divergence is attributed by I5.G5/I5.G6 to ONE upstream: NEW's T0 advances 5 × 9600 counts per frame on the anchors where OLD advances
1 × 9600 (budget anchor `0xb97668` +0xbb80/frame; delta-time 5/60 s vs 1/60 s; the title's tick word +749 vs +149) — the four per-frame kernel
waits each fire a Demand tick because the headroom QUERY `FUN_00191bd0` is bound to the Phase-3 WAIT seam (`hle_TimerWaitMicroseconds`). The title
then runs its timers 5× faster, the fade and the overlay's DMA-chain list appear two slices early, and the per-frame bump pools shift (the "HEAP"
class: the malloc heap itself is identical). The same seam produced coldboot's 484 extra quanta (DP-8 class 1). **Decision needed (DP-8 (1),
timing-domain MUST-STOP): my recommendation is option (i) — unbind the wait seam at 0x191bd0 and let the recompiled headroom predicate run over the
slice-frozen T0 (LoaderSpinLift supplies the load-wait exits; the other six sites are work-bounded).** Until then neither the Phase-4 nor the Phase-5
gate can move. CD-delivery lead (I5.G6): OLD's anchor logs show no CD reads either; NEW drops in-flight deferred CD completions at the IOPM restore
(candidate construction gap, to verify). Independent work continues: 6.2 (GS privileged page → GsState) + 6.4 (GsSink adapter) pulled forward
for the frames lane, Section 3's remaining items (5.3/5.6 play-mode MTVU, 5.8/5.9 docs), the CD-completion re-schedule after restore.

## 2026-10-01 05:05 — I6.2-A (6.2 + 6.4 pulled forward) and I5.G7 closed (no gate run)
6.2 CLOSED: the GS privileged MMIO range routes to `GsState` through `GsMmioHandler` (one shadow — the libgraph rows now write `gs_state.Priv()`);
`unhandled_mmio=0` on coldboot and anchors. 6.4 CLOSED except the identity stamp (no `Vu1HleRuntime` observer yet — carried to 6.3): `GsStateSink`
maps the Phase-5 GIF stream onto the façade, `GsSinkFanout` keeps the `gsops.ohash` recorder for G-C1, a counting DrawIR sink writes `drawir.ohash`
per boundary (anchors render: artdet 16,439 draws in 10 frames). Suite 347/347; coldboot line 60 unchanged. I5.G7: the "dropped in-flight CD
completion" lead is VOID (no CD read in flight at any anchor; OLD drops its deferred queue at restore too); new lead with evidence: the STREAM.IRX
heartbeat hard timer is not re-armed after the restore (`iop_timer_running=none` vs 1 at coldboot; the image descriptor shows id 1 armed) — same
construction-gap class as the worker (I5.G8 next). Gates unchanged; DP-8 (1) decision still the blocker for both legs.

## 2026-10-01 06:40 — I5.G8 and I6.3-A closed (no gate run): the frames lane exists
I5.G8: the heartbeat hard timer is re-armed at restore from the module's descriptor (boot-symmetric; pins) — it was NOT the CD-delivery/audio
upstream (counts unchanged; that class stays DP-8 (1)). I6.3-A (6.5 + 6.3 pulled forward): the live GL video stack in `ReplayDriver`'s shape
(`video=gl`, `gl_renderer=Apple M4`), the DrawIR consumer into `DrawPipeline` on the EE thread at the boundary (the cross-thread queue deferred —
observably identical ordering under the profile), real present/grab/hash into `frames.fhash`. Coldboot's first 63 frames are VALUE-IDENTICAL to the
frozen baseline; from key 64 the baselines skip coalesced presents (OLD presented once per present, NEW once per boundary) → **DP-9** (keying, yours;
recommendation recorded); values diverge from key 69 (unattributed — needs the .gsdl recorder tap to run `gs_replay classdiff`). Suite 352/352.
Pre-authorized decisions taken: the live `has_snapshot = true` rule (checklist item 5) is a construction gap in `DrawPipeline` (no setter) → fixed
next with the L4 pin (I6.3-B); `module_layout.h` edit (title data) accepted. Pending for you: DP-3/4/5/8 (gates), DP-9 (frames keying).

## 2026-10-01 07:30 — I6.3-B closed (no gate run): the frames lane renders OLD's pictures, one clock ahead
The live `has_snapshot` rule + the L4 arm are in `DrawPipeline` (pinned, fails without); a second construction gap found by the first leg is fixed:
`GsState` and the video stack owned two different `VramImage`s (the decoders read an image the GS never wrote) — one image now. Result: of coldboot's
69 frame values 62–130, 64 occur in OLD's baseline — the steady logo frame is pixel-identical; the fade-in frames match OLD's at a faster phase
(NEW 70 = OLD 77, 71 = 82, 74 = 97 …): the title's time-driven fade runs on NEW's leading clock = DP-8 (1) again. Not a GS/render bug, not OLD-wrong.
The `.gsdl` recorder tap is NOT landed: enabling a diag category needs a Config schema change or a diag-build-only rule → **DP-10** (yours; recommendation
(b) recorded by the stage). Suite 353/353. Every lane on every leg is now attributed and waiting on DP-8 (1) (+ DP-3/4/5 on coldboot, DP-9 keying);
the run continues with Section 5 (Phase 7: IPU/FMV rows so coldboot passes its first FMV; FMV audio; device backends) and the docs items.

## 2026-10-01 08:30 — I7.3-A (Phase 7 items 5/6/7/17/18 pulled forward): the IPU/FMV rows are wired; coldboot no longer parks at frame 135 but the first FMV does not play yet
Landed: `IpuDevice` on the MMIO router + the DMAC ch3/ch4 face (7.4 CLOSED, `unhandled_mmio=0`), `GuestMemory`/the title's `FmvContextLayout`/`MpegApi`, 15 libmpeg + 4 libipu
manifest rows (hit on the leg), three `sceCdSt*` wrapper rows the title needed (construction gap, pinned), `avcodec_version` in run.meta. The coldboot leg now runs to the
1500-frame cap (`park=none`), but after `sceMpegCreate` the game never calls `sceCdStStart`/`sceCdStRead`/`sceMpegDemuxPss` (INT-I73A-FMV-NO-DEMUX: candidates — the
stream reader's seek answer, a CD/STREAM status the FMV driver polls, or `sceMpegCreate` failing (`_sceMpegError1` ×2)) → attribution next (I7.3-B) with the missing
`LibmpegBindings` test. **DP-11** (DemuxPssRing's 5th argument is in `$t0` on this ELF, OLD read `[sp+0x10]`; unreachable today) recorded, not applied. Suite 354/354.

## 2026-10-01 09:40 — I7.3-B closed: the first FMV starts; it serves 2 pictures per movie instead of 201/346 (next)
Attribution + fix (construction gap): the title's FMV opener passes its `sceCdlFILE` record in SCRATCHPAD (0x70003ec0); NEW's `sceCdSearchFile` row rejected any non-RAM
record pointer and answered 0, so the opener skipped `sceCdStInit/StStart` (OLD rejected only null). Record pointers now accept RAM or scratchpad (pinned). Post-fix coldboot:
`sceCdStRead ×1410`, `sceMpegDemuxPss ×1414`, `GetPicture ×4` — the stream flows — but only 2 pictures serve per movie vs the golden 201/346 (`Invalid frame dimensions 0x0`,
`avcodec_send_packet failed` ×6; hypothesis: the game streams both movies through StRead→DemuxPss before per-frame GetPicture — an end-gate question) →
INT-I73B-FMV-TWO-PICTURES, next stage. `LibmpegBindings` test (88 checks, RAW8 plane select pinned by mutation); DP-11 register unchanged with its comment. Suite 355/355.

## 2026-10-01 10:40 — I7.3-C closed: the FMV serve gap is the unlanded plan row 7.7 (`fmv_abort_hold`), not an engine gap
Offline, NEW's decoder/demux reproduces all 547 golden pictures (k1=2 s1=201, k2=15 s2=346). Title-driven: the engine answers every ask as OLD's did; the title's load-loop
pump front-loads the whole file (instant CD delivery under the deterministic profile — the same pin OLD ran with), its refill callback hits EOF and the game sets its abort
flag after 2 pictures. OLD's frozen binary neutralised that abort each scene tick (`[FMV-ABORT-HOLD]`, `main.cpp:12768-12796`) — exactly INTEGRATION_PLAN row 7.7 / Phase 7
item 10 (P7-ABORT-HOLD, HOOK_RECLASSIFICATION class: emulation-bug workaround with a retirement gate). An engine pin proves the engine serves exactly 201 when the title keeps
asking (`TestOldMovieTitleCadence`). Next: I7.7-A lands `fmv_abort_hold` under `games/<t>/hle/workarounds/` with guards, counter, pin and the reclassification row (pre-authorized:
a plan-sanctioned class), then the FMV window check 201/346 under the live binary. Suite 355/355.

## 2026-10-01 11:50 — I7.7-A closed: both coldboot FMVs play their golden windows (201 / 346 pictures); row 7.7 CLOSED
`fmv_abort_hold` landed as the plan prescribes (guards, counter `fmv_abort_holds=2`, pins, HOOK_RECLASSIFICATION row with the retirement gate = paced CD delivery). Coldboot
1500 frames: `fmv_pictures_served=547`, `fmv_served_per_playback=201,346+`. Lanes: frames — pre-FMV classes stand (DP-3/4/5/8/9); INSIDE the FMV window NEW's frame hashes match
OLD's at no offset (0/1341) → the FMV picture presentation route differs (carried to 7.3/7.6 — next evidence stage); audio — the FMV PCM is not in the mixer yet (7.2, next);
mem — checkpoint 0 standing class. Suite 357/357 (RecompSymbolCensus is marginal under -j8: 248 s alone vs the 300 s cap — hygiene finding for tests/ee).

## 2026-10-01 13:40 — I7.2-A re-reviewed and re-merged; IDOC-A (docs-only carried items 5.8/5.9/6.8/7.9) landed
I7.2-A's revised record: the FMV picture layout is bit-exact vs the golden (30/32 strips; the 2 spots are the title's post-serve writes); the FMV window occupies ~15
`frames.fhash` lines on NEW because the libmpeg park / idle release advance the pacer without raising boundaries while OLD re-presented one frame 216 times on its wall-clock
free-run — a lane-SAMPLING difference, both cadences non-hardware; the iteration-1 "OLD-wrong" claim is withdrawn; **DP-12** (pair the FMV window by picture via the oracle —
recommended) is yours. One record-only wording MAJOR remains open (plane-2 "reused" vs "near-match"). IDOC-A: 5.8 doc deltas, 5.9 twelve BUG-KB rows (BUG-046..057), 6.8 ledger
deltas L1–L8 (+ BUG-058..063), 7.9 flag/hack ledger + FLAG_PHASE_COUNTS regen + phases README — one MAJOR open (BUG-064 for P6-T4-UPLOAD-DROP missing) → fix-up next.
Remaining construction (all play-mode or docs): 7.1 audio device sink, 7.5 pad/input + config mapping, 7.6 FmvActive → present policy/savestate refusal, 7.8 fingerprint,
5.3/5.6 MTVU half + `--vu1-library none`. Gates: all lanes attributed; waiting on DP-3/4/5/8/9/10/11/12.

## 2026-10-01 14:50 — I7.1-A closed: play-mode audio device + live pad input; fingerprint folds the decoder version
7.1: the mixer's 800-frame output is pushed into the SDL audio ring after the hash (never the hashed buffer); the device opens in play mode only — headless legs byte-identical
before/after. 7.5: `PlatformInput` (SDL input system on the main thread; replay > live > neutral), `InputSettings` from the config, bindings path rule = next to the executable,
else the game-files dir. 7.8: `avcodec_version` folded into `env_hash` (cross-engine NOTE only). Play-mode smoke: `audio_sink=sdl pad_input=live video=gl` for 60 frames; a
realtime windowed leg stalls at frame 60 (INT-I71A-PLAY-MODE-STALL-F60 — the realtime pacing path; not a deterministic-lane item; next evidence). BUG-064 filed; suite 360/360.
Remaining code: 7.6 (FmvActive → present policy / savestate refusal), 5.3/5.6 (MTVU half + `--vu1-library none`, play mode), 6.6/6.7 residue (interp, present policy — pacing).

## 2026-10-01 16:10 — I7.6-A closed: play mode runs (realtime tick thread); row 7.6 landed (savestate refusal during FMV)
The frame-60 play stall was a composition-root omission: no realtime ticker was ever started; `TickThread` now paces play mode (deterministic legs byte-identical). Play smoke:
120 frames with `audio_sink=sdl pad_input=live video=gl`; 600 frames complete in 233 s (DP-13: the Play boundary is not wall-locked — a timing-domain item, yours). Row 7.6:
`Quiescence` refusal predicates (savestate refused while an FMV is active); the present-policy consumer waits on plan 6.7's module. Suite 362/362. Remaining code: 5.3/5.6 (MTVU
half + `--vu1-library none`, play mode). Then the Section 6 readiness report.

## 2026-10-01 19:10 — autonomous run paused at Section 6: every lane attributed; the gates wait on your decisions (see INTEGRATION_READINESS.md)
Last stages: I5.3-A (MTVU half for play mode + MtvuSync sites; `--vu1-library none` → DP-14), I6.4-B (the DrawIR identity stamp wired with OLD's HandlerId numbering — the
class renderers now route stamped draws; `ticks=` per-cause census). All plan rows are landed/closed except the deferred pacing/interp modules (6.6/6.7), the carried items
(4.3/4.6/4.7, 5.6 play-mode sites, 7.5 item 13, 7.8 item 12) and the gates themselves. Suite 368/368 at HEAD. **Phase 8 entry criteria are NOT met**; the single highest-leverage
decision is **DP-8 (1)** (unbind the `0x191bd0` wait seam — it is the upstream of ≈99 % of the anchors' divergence and of coldboot's 484 extra quanta), then DP-3/4/5 (coldboot
rows), DP-9/DP-12 (frames keying / FMV pairing). The run has no further item that does not depend on a pending decision; it stops here and resumes on your answers in
DECISIONS_LOG.md.

## 2026-10-02 06:30 — decisions DP-1..14 applied (I8.1-A/I8.2-A); gate run 1 of the new series (I8.3-G): NOT PASSED, the T0 class is GONE
DP-8 (1): the headroom query runs its recompiled predicate; `ticks=demand=0` on every leg; the anchors' clock words now equal OLD's (budget anchor +9600/frame, delta-time
1/60 s). Coldboot frames: the fade sequence is OLD's shifted by exactly one frame (44/46 keys; candidate cause = OLD's delivery-fired ticks, DP-4 (a) — an accepted row now).
DP-4 resolved by I8.3-G: the 8275 count derived (a torn 16-bit widen across a T0 wrap during OLD's second read + truncation; rows written, no mask) and the 0x1effef0 writer
named (OLD's `StreamRegScratch` on its downward HLE heap; row + the 6-word mask). Checkpoint 60: every region attributed to decided rows. Checkpoint 120: remaining = the
heap-layout shift (DP-8 (2), now shown INDEPENDENT of T0), OLD's PTC-STUB slots (**DP-15**, row+mask proposal — yours), DP-5 ring tail, STREAM counters (DP-8 (5)).
Anchors: 11/11 DIVERGENT at checkpoint 0 and NO leg finishes 600 frames inside the harness's 120 s bound: with headroom always answered, the title's work-bounded
maintenance loops run to their bound every slice (OLD's budget model said "no headroom" on 3 of 4 queries per gameplay frame) — ≈10× guest work and the anchors' remaining
20 k-word class (**DP-16**: (d) harness `--timeout` pass-through now + an evidence stage; the predicate semantics (b)/(c)/(e) are yours). FMV window: tool landed, plane check
not yet run (NOT PAIRED 547/547, 0/2). Next: I8.4 (harness fixes + plane check + DP-10 recorder; evidence on DP-16 (a)/DP-8 (2)/the W142 audio), then gate run 2.

## 2026-10-02 10:40 — gate run 2 (I8.5-G): both NOT PASSED; the anchors' 11 k class is a NEW construction gap (pad READY stage lost at restore); DP-17 re-attributed (needs you)
Harness: `battery.py --timeout` (DP-16 (d)), FMV window `PAIRED-BY-PICTURE (547/547, plane-check 2/2 movies)` with every plane of every state read; the checkpoint-phase pairing is PROVED both
ways (OLD `state_b<N>` = boundary N+1; NEW label = boundary; honest same-index counts: b2 20,050 words, b60 49,866). Anchors: 10/11 reach 600 frames (f57292 parks `fatal_no_jump` at 190 —
`unhandled_mmio=124`, a NEW item), all DIVERGENT at checkpoint 0 / W0; the float/animation/overlay class is `PadProtocol::Stamp` recomputing the stage from a rebased elapsed and zeroing the
anchor's READY controller → the title's controller-disconnected hold (case 15) freezes animation on NEW only. Fix + pins = I8.6-A (running; construction gap, pre-authorized), then gate run 3.
**DP-17 (yours):** the W142 silence / case-6 park is NOT the STREAM path: NEW makes host-served loader completions visible only at the next EE boundary (one read per boundary; OLD completes
22–23 per frame in-frame; hardware ≈6/frame) so the Beach hub load that exits case 6 takes ≈4,900 boundaries instead of 852. Options in DP-17: (a) in-slice delivery at the engaged load-wait
poll keyed on the modelled due time (recommended; a timing-domain decision → yours), (b) boundary-only + ≈6,000-frame coldboot legs, (c) k-per-boundary budget. Also yours: DP-15 (PTC-STUB
slots row+mask), DP-8 (2)/(5). Coldboot checkpoints 0/60/120 unchanged from run 1 (every region attributed to decided rows or DPs).

## 2026-10-02 13:00 — gate runs 3–4 (anchors): two more NEW construction gaps fixed (BUG-065 pad READY, BUG-066 IOP clock frozen after restore); what remains is yours
Anchors: artdet's same-boundary word count fell 20,050 → 62 (b2) and 49,866 → 107 (b60) after BUG-065; the STREAM.IRX heartbeat now beats on every anchor after BUG-066 (NEW runs the module's retail
16727 µs period — computed from its timer compare at 36.864 MHz). Remaining anchor classes, each attributed with the new diag `--watch-word` knob: (1) **DP-19** — NEW runs the load-wait poll (`LoaderSpinLift`,
one iteration per lap: 3 polls by boundary 2) where OLD ran ZERO (its wall-quantized headroom replacement said "no headroom") and hardware runs ≈100 per lap; the polls complete loads that spawn objects
(24 extra skeletons on NEW's third post-restore frame = the f57292 "72 k-word heap shift" = per-frame bump arenas), OLD's scene stays frozen; the f57292/f59991 stalls walk the resulting visible-group list
(DP-18 is downstream of DP-19). (2) **DP-20** — OLD's restored heartbeat beats ONCE (a stale boot due) and then is dead (its IopClock has the BUG-066 defect): OLD never releases finished voices after a
restore; NEW does every frame → the audio lane's W0/W1 residual; options: accept as a §7 row (no mask) + model "a restored running timer is due at the restore" with pins. (3) float-LSB words (1–2 ULP;
writers `FUN_00294b18`/`FUN_00296228` — op not yet named; Ps2FloatRounding rows). (4) pad `seq` (4 words; a PADS field = format change: not proposed). Coldboot: checkpoints 0/60/120 unchanged; key-69 fade =
the title clock crossing covered by the accepted DP-4 (a) row; the DP-8 (2)(b) first differing allocation between 60 and 120 is in the `0x31afb8` heap (F:96–120), unnamed (the F:62 candidate was refuted);
DP-17 (delivery visibility), DP-15 (PTC slots), DP-8 (5) are yours. Phase-8 run.meta criteria: `unhandled_mmio=0` on every finishing leg; `avcodec_version`, `libgraph_rows`, `gs_priv_writes` present; the plan's
`dmac_unmodelled_kicks` key does not exist in run.meta (the DMAC census is `dmac_tail_chases*`/`dmac_sif_kicks` — the plan's key list needs the rename). Next, independent of your decisions: DP-13 (approved:
wall-locked Play boundary + present policy, play-mode only), the host render path's throughput (gl 750 ms/frame on the EE thread — `TextureResolver::DecodeFallback` dominates; evidence + fix if a cache gap),
and the coldboot 60–120 allocation (OLD-lldb vehicle ready).

## 2026-10-02 16:00 — FMVs play; DP-13 landed; throughput class fixed; what is left is yours
Your report ("the FMVs are not running, at most a single frame of the second one") was the present cadence: the window was re-presented only at the EE frame boundary and a movie never crosses that loop
(547 pictures inside ~10 boundaries); movie 2 additionally drained as a blur because the pacer carried movie 1's host debt. Landed (play mode only; deterministic legs byte-identical through the FMV window,
pin): a DISPLAY TICK — a window present on every field a pace park waits through and on every serve no park follows (hardware scans DISPFB out every vblank) — and `FmvPacer`'s opt-in bounded re-anchor
(slow motion instead of a blur when the host is behind). Pin `RootPlayFmvPresents`: every served picture presented (547/547). DP-13 (a)+(b) + row 6.7's present policy landed (I8.10-A; MAJOR 1 closed by
I8.11-A). Throughput (I8.10-B → I8.11-A, BUG-067): the texture cache was hitting 99 %, but `DecodeFallback` copied all 4 MiB of VRAM and built byte vectors before every lookup — gone; artdet gl 612 → 330
ms/frame (Debug build, under contention). The movies now take ≈ 18 s (movie 1) + 26 s (movie 2) of wall in the Debug build — the remaining per-picture cost is the serve (demux + decode + strip writes), the
title's 1.2 MB GIF IMAGE upload of the plane and (fixed just now) the display tick's needless frame grab; a Release build (`cmake --preset macos-release`) is being built and lane-checked — use
`build-release/app/host/nativeee` for play once I confirm its deterministic lanes equal the Debug build's. Evidence closed: DP-8 (2)(b) first differing heap allocation = OLD's SESSION-14 appended populate
(row proposal in DP-8), the float-LSB words = VU0 micro `0x790` concat operands (DP-21: operand capture needed on both sides), Section 6 stability: 2/2 legs measured STABLE across 3 reps (artdet, coldboot;
the other 10 need an overnight run at the gl lane's cost). Nothing independent of your decisions remains on the gate path: DP-15, DP-17, DP-18 (downstream of 19), DP-19, DP-20, DP-21, DP-8 (2)(b)/(5).

## 2026-10-02 16:05 — the Release build: lanes identical to Debug, 7× faster; the FMVs play in real time
`cmake --preset macos-release && cmake --build --preset macos-release -j4` (0 warnings; `build-release/app/host/nativeee`): the deterministic coldboot 200-frame lanes (frames 200/200, mem 4/4, audio 33/33 windows)
are byte-identical to the Debug build's, in 5.7 s instead of 40.4 s. Play mode (window, 200 boundaries, 21.9 s wall): the pacer parks every picture (`fmv=…late=3,parks=536,park_ticks=1069`, one display tick
per field `display_ticks=1069`), movie 1 6.5 s (authored 6.7), movie 2 11.6 s (authored 11.5); `pace_late=12` of 200 boundaries. **Use the Release binary to play.** The Debug build stays the harness/CI binary.

## 2026-10-02 17:40 — Section 6 stability half MET (12/12 STABLE); the autonomous series stops here
`battery.py --reps 3` on all 12 legs (dev binary copy, gl lane): three reps byte-identical on frames/audio/mem for every leg (the two DP-18 stall legs stall at the same frame every rep with identical lanes up to
it) → ADMISSIBLE-STABLE 12/12. Release build lanes = Debug lanes (200 frames, incl. drawir/gsops hashes). The IDENTICAL half of §4.8 is blocked entirely on your decisions: DP-15, DP-17, DP-18 (downstream of
DP-19), DP-19, DP-20, DP-21, DP-8 (2)(b)/(5). Nothing independent remains; answer in DECISIONS_LOG.md and the series resumes from I8.13.

## 2026-10-02 17:55 — POLICY CHANGE applied: no MUST-STOP list; the run decides, measures by score, and runs until Section 6 is MET
`DECISIONS_PENDING.md` → `DECISIONS_LOG.md` (every decision MADE with evidence/options/choice/outcome). First entries under the policy: DP-17 (a)
(delivery visible at the poll, toggle `cdvd.delivery_visibility`), DP-20 (a)+(c) (restored running timer due at the restore, toggle
`iop.restored_timer_due`; OLD's dead heartbeat rowed), DP-15 (a) (PTC-STUB slots row + mask — the one permitted mask class), DP-8 (2)(b)
(OLD's SESSION-14 populate rowed), DP-19 (a)+(d) (OLD's zero polls rowed; the title's own loop stands), DP-21 (a) (operand capture first),
DP-18 (downstream, re-evaluated after gate run 5). Tooling: `verify/score.py` (the score tuple) and batteries on the Release binary behind a
per-run Release-vs-Debug identity guard (12 legs × 3 reps ≈ 35 min instead of ≈ 4 h). Order: tooling + DP-17 (a) + DP-20 (c) → gate run 5
(score S0 with toggles off, then each toggle on; KEEP/REVERT by score) → whatever it exposes.

## 2026-10-02 23:55 — FINAL: the integration run stops; Phase 8 begins
Under the 17:43 policy every pending decision was made and measured (DECISIONS_LOG.md summary table): DP-15 KEPT (−18,540 words), DP-20 (c) REVERTED
(+193), DP-17 OPEN after three attempts (BUG-068 fixed on the way; poll still one read per boundary because the lift yields per lap; a `pc=0` null-listener
dispatch under poll ordering = DP-22), DP-21 OPEN (the VU0 op is identical on both trees; the operand difference is upstream), DP-18 OPEN (downstream),
DR-1: no honest re-baseline window exists. Gate of record S2 at HEAD: `(0/12 identical, 8,939,244 words, 392/392 pins)`, 12/12 STABLE. Section 6 is NOT MET
as written — only OPEN classes remain — so the final verdict is written in INTEGRATION_READINESS.md and Phase 8 starts with its carried list (boot load
first). Play the Release build: `cmake --preset macos-release && cmake --build --preset macos-release -j4 && build-release/app/host/nativeee`.

## 2026-10-03 19:00 — Phase 8 item (1): the boot reaches the title screen; DP-17 attempt 4 KEPT; music plays
Hardware reference measured on the PCSX2 fork (DECISIONS_LOG HW-1): the Beach load takes 500–780 frames on the console; the title's load-wait loop runs ONE lap per frame; the STREAM module
round trip costs ≈ 160 µs (bursts of replies 48k EE cycles apart) — so the console fits ≈ 100 module round trips in a frame. NEW's poll model now does the same: the IOP's service runs inside the
frame on its modelled clock, the lap continues on replies, the frame ends when the next modelled event is past the slice, one drive for module and host reads, completions in issue order (P8.1–P8.5;
BUG-068/069/070 fixed on the way, DP-22 decided from the title's code: stage-0 deserialise precedes the notify). Result: the deterministic coldboot leaves boot case 6 at b1080–b1140 (≈ 840–900 frames
of load vs the reference's 500–780) and renders the title screen; BUG-071 (retail's blocking `sceSifCallRpc` runs the end function before the waiter resumes — NEW deferred it) made the title music
start (`snd_kon=8`). Score: S4 `(0, 8935538)` BETTER than S2 `(0, 8939244)` — coldboot b60 fell from 12,547 to 125 words (the DP-3 class is gone: both trees consume Hub01.psm by boundary 60) → KEPT,
default `cdvd.delivery_visibility=poll`; suite 393/393; gate of record S5 running. Open on this item: coldboot b120 +8,665 words (the faster load — to attribute), the audio content from the music
onset differs from OLD (W142 vs W191 and 0/20 windows equal — INT-P86A-ONSET-CONTENT), the two DP-18 stall anchors (next, per your order), then DP-21.

## 2026-10-04 00:20 — Phase 8 item (2) done: the two DP-18 anchors run to 600 frames; NEW owns the recompiler
The f57292/f59991 stalls were a recompiler defect, not a model: the frozen generation split retail `0x178470` at its branch delay slot, so every fall-through entry re-executed the
`addiu sp,-0x10` prologue and leaked 16 bytes of stack per call until the title's visible-group walk ran with its locals below the scratchpad (BUG-072). Fix: NEW now owns the EE
recompiler (DP-23 — OLD's `recompiler_tool_v2` transplanted with provenance, proved to regenerate the frozen shards byte-for-byte before any change), with two rules added: a weak
seed at a branch delay slot is never a function start, and every externally-entered address gets a switch case. Regenerated: 8819 → 8807 functions, 12 delay-slot starts removed, 3
runs byte-identical, diff fully explained; unit pin `Bug072SpBalance` red → green; both anchors `frames=600 finalized=1 park=none` (were 15 / 126). Suite 398/398; Release rebuilt;
gate S6 running (12/12 legs expected to run for the first time). Gate of record so far S5 `(0, 8934538, 393)`. Next: DP-21 (item 3), then the carried list (PADS `seq`, VU special2
decode, GS pipeline off the EE thread); the coldboot b120 +8,665 words and the music-onset content (W142 vs W191) remain open attributions on item (1).

## 2026-10-04 01:40 — gate S6: all 12 legs run and are stable; outside-mask words 8,934,538 → 463,533
With BUG-072 fixed the two DP-18 anchors finish (f57292's boundary-2 pair fell from 72,101 to 188 words), so the 8.39 M-word "missing image" penalty is gone: S6 `(0/12 identical, 463,533 words,
pins 398)`, battery META-OK 12/12, every leg ADMISSIBLE (3 reps identical). What remains, by size: coldboot b120 432,344 words (93 % — OLD's hook-induced heap shift, the DP-8 (2)(b) row; the DP-5 ring
tail; the poll-mode load's +8,665), then the anchors' b60 pairs (f46949 8,836; slowspot_f37092 b2 4,771; f44111 2,879; f40391 1,842; the rest ≤ 1,230) and b2 pairs (≤ 1,820). DP-21 (the pose-matrix
ULPs) is OPEN after three attempts: the differing operand is a blend quaternion on the scratchpad stack written by the title's vtable controllers — an EE-side quaternion-pipeline difference upstream of
every VU0 call, with no OLD-side cause nameable without an OLD/hardware capture of that frame. Next: the carried list in order — the PADS `seq` field (a savestate-format gap: 4 words per anchor), the VU
special2 `0x3C` decode, the GS pipeline off the EE thread — and, for the definition of done, a word-level attribution ledger per probe pair (every residual word → its row/class, a relocation-aware
equality proof for the coldboot heap shift) so that re-baselining under the OLD-defect rows can be done honestly or the classes reported OPEN.

## 2026-10-04 12:10 — FMV sound (your report): fixed in play mode; rebuild Release and listen
Reference (PCSX2 fork, `start_button_before_fmv.p2s`): the title's own EE code sets the SPU2 core-0 external-input volume at movie start — `FUN_002ae318 → FUN_002af458(1.0f) → sceSdRemote(1, 0x8010, 0x0F80/0x1080, 0x7FFF)` — and to 0 at movie end. NEW's core received exactly those sets (`bvol_sets=8, bvol_peak=0x7fff`); the gap was the clock: the mixer was pumped only at EE frame boundaries and none pass during a movie, so the queued movie PCM was mixed only after the end-of-movie BVOL=0 → silence. Play mode now pumps one mixer quantum per display field (FMV pace parks, late serves, kernel waits, disc parks) into the device ring; the hash lane keeps its 800 frames per boundary (deterministic lanes byte-identical). Release play leg: `mixed_audible=872800` of 897,024 movie frames, `audio_device nonsilent=1055` quanta. BUG-079, DP-29 (the baseline's silent FMV window is OLD's identical defect — row text `P7-FMV-BVOL-OLD-UNSET` proposed, not filed: the deterministic lane is unchanged so no gate moves). Rebuild (`cmake --build --preset macos-release -j4`) and run `build-release/app/host/nativeee` — the movies should have sound now; tell me if not. Known: ring catch-up drops after host stalls (INT-P816A-RING-CATCHUP-DROPS, 1,959 underruns in a Debug leg) — audible as brief dropouts on a slow host; the Release build has far fewer.

## 2026-10-04 — FINAL (Phase 8 closing stage P8.19-A): rows filed with proofs, DT-3/DT-4 decided, the ledger booked, gate S8; Section 6 NOT MET — only OPEN classes remain
The proposed accepted-difference rows are filed in `EQUIVALENCE_PLAN.md` §7.1 with the ledger proofs attached (the SESSION-14 row now records the record pool's relocation — 294,660 both-sided relocation-equal words at coldboot b120, the pool's layout diverging at OLD's four hook-era `0x6080` records seq 67–70 of F:96; the ZERO-POLLS and dead-heartbeat rows carry the EE client's per-slot records and voice statics with their censused writers; new rows for OLD's restarted pad `seq`, OLD's 8-bit display of a 5-bit PSMCT16 buffer, and the baseline's silent FMV window), the two tooling decisions are MADE (DT-3: NONSTATE is a reported column, never subtracted — 39,568 words; DT-4: the ledger-gated, artefact-bound effective-reference mechanism is the only way a window is ever re-based), and the proved classes are booked in the ledger's directives with their proofs. The ledger over the 24 probe pairs now reads ROW 301,937 / DP 0 / OPEN 120,751 / MIXED 1,153 / NONSTATE 39,568 / UNATTRIBUTED 124 (was ROW 7,005); yet no window is 100 % attributed — every re-baseline dry run refuses (35/35) — so the frozen OLD baselines stay the gate's reference unchanged. Gate of record S8 (Release, identity guard IDENTICAL, 3 reps, ctest 420/420 run alone): ``(legs_identical=0/12, outside_mask_words=463,533, pins_green=420/420)``, EQUAL to S7 on the score tuple as expected (rows and booking change no lane); 12/12 legs stable, every lane divergent. **Section 6 (12/12 IDENTICAL) is NOT MET; what remains is the OPEN list in `INTEGRATION_READINESS.md` (FINAL VERDICT 2026-10-04): the page-pool allocation classes (3 attempts), the float-rounding/DP-21 operands (3), the DP-5 (b) second ring (0 attempts — the one untried class), the audio order/content/restore-phase windows (2), frames Class B (2), the un-censused STREAM statics, 124 unattributed words — each with the arbiter or knob that would close it named.** The carried list of 2026-10-02 is exhausted (items 1–7 landed or OPEN). Play the Release build: boot to the title in ≈ 15 s, FMVs with sound, pad live.

## 2026-10-05 04:50 — your title-screen report, the 13th leg, and a memory defect the new leg exposed
Title screen (orb effects / logo / ocean highlights): the blend lowering is EXONERATED — against the PCSX2 `start_screen` GS dump, 74 of 75 blend-state keys are identical on
PCSX2/OLD/NEW and every effect draw reaches NEW's pipeline with the same ALPHA/TEST/TFX; replaying the dump's own frame through NEW's full pipeline reproduces PCSX2 on all
three regions (LOGO (241,217,19) vs (238,216,16); orb/ocean brightness within a few %) — those replays are now gates (`Render_TitleFrameReplay`, `Render_TitleFrameReplayDisplay`,
`Render_TitleLogoDrawPin`, `Texsrc_TitleAtlasDecode`). The LIVE run still draws the logo with a flat (231,199,73) that no CLUT in RAM or VRAM holds (BUG-081, OPEN): the texture
bytes, VRAM pages, vertices and registers equal the dump's at the boundary, so the defect is in what the live decode consumes at draw time (slots re-uploaded 2–6× per frame);
the stale-cache hypothesis was tested and refuted (P8.23-A). Two gaps were fixed on the way (BUG-082 ReadView newer-writer; the ALPHA 0122 arm).
The 13th leg `coldboot_attract` (coldboot → title → the whole "Three years ago…" cutscene → back): OLD baseline frozen at 6000 frames (OLD is UNSTABLE in its load phases — two
shapes recorded; the majority timeline is frozen), 5 probes with proofs, five PCSX2 GS dumps inside the cutscene with provenance. It immediately found BUG-083: the gsops
register-op recorder kept every GIF op for the whole leg (a 12 GiB node; ≈ 11 MB per title frame; the OS killed the leg at 4096 frames) — fixed (hash-only, footprint census +
pins); NEW now runs the leg to 6000 frames at a 491 MB peak. Gate S9 (13 legs × 3 reps) is running. FMV sound in play mode was fixed earlier today (P8.16-A).

## 2026-10-05 12:30 — the three title-screen defects are FIXED; the attract scene is gated against PCSX2; gate S10 running
Root cause (BUG-084): `DrawFlush::FillTexture` used TEX1.K as a raw unsigned 12-bit value — the title's K = 0xFBB (−4.3125) became +251.7, so every mip-mapped texture clamped to its
smallest level: the logo sampled one flat texel of its 8×8 mip, the orb/ocean effects their faded last levels. One sign extension (GS manual 3.4.12) fixed all three: the live title
frame now measures LOGO (238,215,14) / ORB bright 0.443 / OCEAN 0.701 vs PCSX2 (238,216,16) / 0.417 / 0.659 (was (213,177,65) / 0.098 / 0.472). Found by tracing what the live
decode consumes (the texture bytes, VRAM, vertices and registers were all identical to the dump — the blend lowering and the texture pipeline were exonerated step by step: P8.20-B →
P8.21-A → P8.24-A). Gates: `Render_TitleFrameReplay*` (the PCSX2 dump replayed through NEW), `RootTitleScreenEffects` (the live title vs the PCSX2 screenshot), `Gs_Inv40` (K sign).
The 13th leg `coldboot_attract` (6000 frames through the whole attract cutscene) is frozen from OLD and runs on NEW (BUG-083's 11 MB/frame recorder leak fixed); its probe pairs are
scene-aligned by the boot edge (OLD b850 ↔ NEW b1133, +283 — OLD's wall-pumped load reaches the title earlier; the offset is reported on every verdict line); `RootAttractCutsceneFrames`
compares four cutscene moments against the five PCSX2 GS dumps (12/channel absolute; measured residual ≤ 5.2). Rebuild Release and look at the title screen. S10 = the 13-leg gate at HEAD.

## 2026-10-06 11:45 — Phase 8's equivalence half is CLOSED (your decision of 11:23, DT-8); the battery is now a regression gate; the product half starts
What closed: the memory-identity goal against OLD. The final ledger is written as-is (`docs/phases/PHASE8_LEDGER_FINAL.md`): 0 of 13 legs identical to the frozen OLD lanes at gate S12 (`(0, 20084447, 448)` raw / `(0, 20064909, 448)` unbooked), every remaining class OPEN with its attempt count and the console's verdict where one exists — nobody attributes residual memory words any more. What the gates are now: (1) the 13-leg battery as a regression gate (`games/<title>/games/<title>/verify/baseline/REGRESSION_REF.tsv` from S12; `score.py --regression`; `gate_run.py` prints `regression: PASS|FAIL` — S12 and the P8.33-A libmc-fix gate both PASS), (2) the PCSX2 GS-dump replay tests as the rendering correctness gate, (3) `console_compare.py` for new disputes; a visual or audible defect against PCSX2 outranks any word count. One record corrected per the P8.33-B review: the cutscene letterbox was never a renderer class — the bars are the title's own XYZ3-led strips, dropped by the old replay exporter and drawn correctly by the live engine. What the product half is (P8.36-D/L/R running now): the frozen baselines move to `games/<title>/verify/`, the size-budget / header-globals lints become ctests with a per-leg bench baseline, and the rendering-gate tooling (corrected exporter, cutscene replay pins, replay≈live, the logo LOD rule) — then `app/gui`, the play-mode hotkeys, the input recorder for your two long-play sessions, and the performance round.

## 2026-10-07 — first game-library replacements; source-based retirement plan

Reviewed Smartkin/the title-reversed at `38d533d0033b48b5d6b49880c49f5eeeade0f38e`
with sub-agents and checked NTSC boundaries before binding them. New components
use the requested names `MathLibrary`, `MovieDecoder` and `GameLibraries`
(DT-10). The point transform and active MPEG-2 Create/Reset/picture path are
enabled; the 39-run census records 13,049,751 point transforms,
8,619 pictures and zero IPU RestartDMA calls or movie fallbacks.
The direct sound-bank header reader is tested but its binding remains disabled:
it changed a frozen memory fixture while preserving frames/audio. Final builds
have zero warnings; 474 tests passed, four skipped and two disabled, including
Debug/Release identity and PCSX2 rendering pins. The standing regression gate
**FAILS** (20,084,539 raw, **+92** against S12 / 20,042,931 unbooked).
The raw increase is confined to attract; all frame/audio first-divergent keys
are unchanged. The enabled replacements remain working candidates with
acceptance open. No reference rule, mask, directive or frozen data was changed.
[PORT_PLAN](../design/PORT_PLAN.md) and its 73-row boundary ledger sequence the
remaining work; [HLE_COVERAGE](../design/HLE_COVERAGE.md#scaffolding-retirement)
tracks the models still reached. Next is resolving library integration under
the standing gate at game boundaries, followed by the remaining movie lifecycle
and ordered `Graphics` submission. No whole PS2 subsystem is claimed retired.
Artifacts: `<work>/int_work/library_retirement/`;
full implementation record in `PHASE8_IMPL.md`.

## Cutover (2026-10-08 →)

One flip per stage on `main` (CUTOVER.md); each line quotes the result it read. Reference lane:
`int_work/m_series/cutover/REFERENCE` (ref0 = the pre-flip run on `main` 726a5e26). Stage records:
`docs/design/M_CUTOVER.md`, `int_work/m_series/cutover/<stage>/SECTION.md`.

- 2026-10-08 flip 1 textures-at-load = `hle.graphics_sprites` (the key owning the most texture consumers, the 40 startup icons; `TEXTURE_OWNERSHIP.md` §Selection map) — **REVERTED: census not zero** — on the 4600-frame acceptance leg `graphics_sprites=replaced=0,model_calls=21928` (smokes: 425 / 547), `graphics_textures=loaded=0,…,uploaded=0`, `graphics_refusals_textures=total=40,unsupported_state=40,first=frame:60/source_frame:0/n:1/producer:0x1bc920/detail:3/reason:unsupported_state` (every startup-icon `TextureRead` refused `TextureBindings::Result::InvalidMemory`; the original drew everything). Everything else held: Release 0/0, suite `100% tests passed, 0 tests failed out of 597`, `RootPlayModeSmoke Passed 6.20 sec`, `RootMovieLifecyclePlay Passed 24.41 sec`, leg `diff_legs: IDENTICAL (lanes=frames,audio)` + mem identical vs ref0. Flip commit 1cd2a13e, revert commit follows. Owner: Zeno (M6/M7 `TextureBindings`); gap: retire the 40 `InvalidMemory` refusals at `0x1bc920` (frame 60) so `graphics_textures.loaded=40`, `graphics_sprites.replaced>0`, `model_calls=0`. Slowspot (reverted binary, load_1m 5.11 — not an idle baseline) 54.58 ms/frame (ref 50.95; ratio 1.071).
- 2026-10-08 flip 2 Graphics bridge = `hle.graphics_frame` (SetUpFrame clear, WriteFrameHead, both display Sends, SetupPCRTC layout; census `Graphics.frame.{setup,send,interrupt,clear,layout}.model_calls`, owned `graphics_frame_output.{completed,setups,layouts}`) — **REVERTED: the acceptance leg ABORTS** — deterministic coldboot 4600: `run_leg: FAIL: run.meta not finalized`, `libc++abi: terminating due to uncaught exception of type std::runtime_error: Graphics ordered delivery failed` (`GraphicsOutput::Record`, `ordering_errors || failed_groups`) at frame 63 (reproducible with `--frames 63/64`; `--frames 62` completes with `graphics_frame_send=calls=1,replaced=0,model_calls=1`, `graphics_refusals_frame=total=1,…,first=frame:61/source_frame:0/n:1/producer:0x181968/detail:7/reason:ordering` = `FrameService::kSendFrame`, one owned group `pending=1`); `RootMovieLifecyclePlay` aborts the game process too (`runner exit=134`, `Graphics walk did not deliver every accepted predecessor` at frame 128; re-run `Graphics output delivery failed` at 63) and the script reports it as Skipped; `RootPlayModeSmoke Passed 6.25 sec` with `Graphics.frame.send.model_calls = 1` (census not zero) and owned `graphics_frame_output=completed=58,setups=59,layouts=58`. The 63 frame hashes / 10 audio windows before the abort are identical to ref0's prefix; mem advisory: first divergent key 0 (`0,0x2cfcc4f064d14998` vs ref0 `0,0x03018fa3110d34b5`; also key 60; deterministic on the `--frames 62/63` probes and the play smoke) — guest RAM differs from boundary 0 with the frame key on, before the frame-61 refusal. Held: Release `FAILED=0 warning:=0 ld_warning=0`, component suite ALONE `100% tests passed, 0 tests failed out of 597`, config/lints 16/16. Flip commit e57a3281 (re-applicable); revert commit restores the default byte-identical to ea664a27; reverted binary (BUILD-ID 2026-10-08 14:28:00) 597/597, smokes `6.17 sec` / `24.35 sec`, RootAnchor 6/6, lints 13/13; slowspot 52.74 ms/frame (ref 50.95; load_1m 5.86 — report only). Owner: Zeno — the frame-61 `kSendFrame` refusal must leave no pending owned work and the following Link-walk fault must refuse (INTERFACES.md §8a), and `GraphicsOutput::Record` must publish the census of a faulted run instead of throwing; the boundary-0 mem divergence must be attributed (expected retired-model residue vs an owned write the model never made). `REFERENCE` stays `ref0/leg`. Record: `int_work/m_series/cutover/flip2/SECTION.md`.
- 2026-10-08 flip 3 native Strip primitives = `hle.graphics_strips=all` (the four flat `Strip::Draw` producers `0x1a9120`/`0x1a92a0`/`0x1a9448`/`0x1a9660` delivered to the owned scene, STRIP_SCENE.md; census `Graphics.strip.{plain,tinted,placed,placed_tinted}.model_calls`, owned `graphics_strip_output.completed`) — **REVERTED: the acceptance leg ABORTS at frame 118 (a selected strip draw's delivery faults)** — deterministic coldboot 4600: `run_leg: FAIL: run.meta not finalized`, `libc++abi: terminating due to uncaught exception of type std::runtime_error: Graphics ordered delivery failed` (`GraphicsOutput::Record`, `failed_groups=1`) after 118 presented frames — the same frame as Zeno's round-2 gate-4 abort (`strips-all (118)`); round 3's refuse-not-abort (`db16dc1f`, `GraphicsService::TryStrip` capture/environment/record refusals) landed in main but does not reach this fault: it is a COMMITTED-arm delivery failure (`GraphicsScene::DrawStrip` of the FIRST owned strip group of the title returns non-Ok → `GraphicsBridge` group `Failed` → `GraphicsService::EndWalk` `Poll` = `DeliveryFailed` → `Fault("Graphics output delivery failed")`), which INTERFACES.md §8a keeps as a fault. `--frames 117` completes (`graphics_strip_output=captured=8,completed=0,failed=0`, `graphics_service=strip_calls=8,replaced=8,…,owned_strips=8,…,linked=0,unsubmitted=8`, `Graphics.strip.*.model_calls = 0`, `graphics_refusals_strips=total=0`); `--frames 118` aborts; the control arm `--set hle.graphics_strips=plain --frames 118` completes with `linked=1,queued_groups=1,completed=8` (Plain delivers the same first group through the retained DrawIR path). Diagnostic census of the faulted 118-frame run (uncommitted local build that publishes before `Record` throws): `graphics_strip_output=captured=16,completed=0,failed=1`, `graphics_owned_output=queued=16,completed=0,pending=15,failed=1`, `graphics_service=…,linked=1,…,queued_groups=1,completed=0,recorded_work=16,completed_work=0,ordering_errors=0,failed_groups=1`, `graphics_refusals_strips=total=0,…,first_fault=none` — owned work ZERO. Both smokes ABORT the game process the same way (`runner exit=134`, `Graphics output delivery failed` after 116 presented frames; `root_play_mode_smoke.py` / `root_play_fmv_presents.py` report them as Skipped). The 118 frame hashes / 19 audio windows before the abort are identical to ref0's prefix; mem keys 0 and 60 identical to ref0 (no advisory divergence). Held: Release `FAILED=0 warning:=0 ld_warning=0` (BUILD-ID 2026-10-08 15:24:59), component suite ALONE `100% tests passed, 0 tests failed out of 597`, config/lints 16/16. Flip commit e3435827 (re-applicable); revert commit restores the default byte-identical to 9b70755c; reverted binary (BUILD-ID 2026-10-08 15:40:00) 597/597, smokes `RootPlayModeSmoke Passed 6.16 sec` / `RootMovieLifecyclePlay Passed 24.40 sec`, RootAnchor 6/6, lints 13/13; slowspot 51.57 ms/frame (ref 50.95; load_1m 6.74 — report only; a first run at load 6.47 read 57.17, kept as `bench-underload-1`). Owner: Zeno — the first owned `GraphicsScene::DrawStrip` of frame 118 (result ∈ {InvalidFrame, UnclosedDependency, ResourceFailure, MissingScene, StaleScene, DrawFailure}; `CanDrawStrip` passed at capture) must either complete or be refused at capture time (§8a), and `GraphicsOutput::Record` must publish the census of a faulted run (carried from flip 2). `REFERENCE` stays `ref0/leg`. Record: `int_work/m_series/cutover/flip3/SECTION.md`.
- 2026-10-08 flip 4 MovieDecoder lifecycle = `hle.movie_lifecycle=on` DEFAULT — census `ipu_commands=bclr=0,idec=0,bdec=0,vdec=0,fdec=0,setiq=0,setvq=0,csc=0,pack=0,setth=0,unknown=0` (ref0 `bclr=4`), `ipu_operations=init=0,sync=0,stop=0,restart=0` (ref0 `init=2,stop=3`), `libipu_calls=init=0,sync=0,stop=0,restart=0` (ref0 `init=1,stop=3`), `movie_audio=…,model_pops=0` (ref0 `model_pops=32200`), `movie_decoder=…,audio_original_calls=0,stream_original_calls=0,input_original_calls=0,audio_playback_original_calls=0`, `movie_stream=…,prepare_original=0,open_original=0,close_original=0` (ref0 3/3/3), `fmv=…,retained_init_calls=0` (ref0 1) = 0 on the acceptance leg AND both smokes; owned `movie_decoder=…,initialized=1,started=3,closed=3,eof_deferred=2320,priming_eof=3,audio_feeds=8566,volume_changes=6,stream_seeks=3,stream_reads=6246,stream_bytes=102334464,demux_windows=6246,registrations_retired=6,audio_started=3,audio_stopped=3`, `movie_stream=prepared=3,opened=3,closed=3,seeks=3,reads=6302,sectors=50864,errors=0`, `movie_audio=selected=1,gain_changes=6,nonzero_changes=3,peak=32767,pops=32200` on the leg (movie smoke: `started=2,closed=2,audio_feeds=1953,pictures=547`, `movie_audio pops=9842 audible_frames=865600`, `fmv_pcm mixed_audible=865600`); the 97-row catalog census identical to ref0 (`nonzero=73 zero=19 absent=5` — the lifecycle counters are run.meta rows, not catalog rows); frames/audio IDENTICAL to ref0 (`diff_legs: IDENTICAL (lanes=frames,audio, masked 0 B, coalesced 0)`, 4600 keys / 767 windows, last `4600,0xcdb1ff109c38971c` / `766,0xfd605d29418b3536`); mem advisory first divergent key 180 (`180,0xd3593dcced9e5827` vs ref0 `180,0x99d6c871e93314f8`; keys 0/60/120 identical — the first movie's Initialise at the FMV window start (170): the retired `sceMpegInit`/`sceMpegAddCallback`/`sceMpegDemuxPss`/`sceIpuInit` bodies no longer write their guest-RAM contexts, `sdk_rows_hit_list` drops exactly those rows); slowspot 50.47 ms/frame at load_1m 3.65 (ref BASELINE 50.95, ratio 0.991; flip 3 revert 51.57 @6.74). Held: Release `FAILED=0 warning:=0 ld_warning=0` (BUILD-ID 2026-10-08 16:21:28), config/lints 16/16, component suite ALONE `100% tests passed, 0 tests failed out of 597` (22 skips = flip 3's set), game-process group -j1 73 run: `RootPlayModeSmoke Passed 8.58 sec` (runner exit=0, finalized=1), `RootMovieLifecyclePlay Passed 31.37 sec` (runner exit=0, `PASS: play mode presented every movie picture`), RootAnchor* 6/6, Render_AttractReplay 5/5, RootAttractCutsceneFrames, the four retained-arm pins (`RootBindsFmvRows`/`RootHoldsFmvAbort`/`RootMixesFmvPcm`/`RootPlayFmvPresents`, now `--set hle.movie_lifecycle=off` explicitly) Passed, `RootCdvdPollVisibility Passed 83.03 sec` (ref0's pre-existing failure — with the owned lifecycle the b120+ boundary images are no longer refused `movie_software … still require Close`); `RootReleaseLanesIdentical` first read `guard=DIVERGENT coldboot/mem.mhash line 4` against the STALE Debug binary (`build/`, BUILD-ID 2026-10-07 11:42:40, pre-flip default) and `guard=IDENTICAL` / `ALL PASS` after `cmake --build --preset macos-dev --target nativeee` (BUILD-ID 2026-10-08 16:54:50). Commit e62dfa68 (default + table + explicit pins). `REFERENCE -> flip4/leg`. Owner Bacon (M2). Record: `int_work/m_series/cutover/flip4/SECTION.md`.
- 2026-10-08 flip 5 MathLibrary helpers = `hle.math_helpers=on` DEFAULT — census: all 61 catalog rows `{MathLibrary,CollisionMath,TrigMath,AnimationMath,BoundsMath,ClippingMath,JointLibrary}.generated.*` (`<field>.generated_body_<entry>`) = 0 on the acceptance leg AND both smokes (REFERENCE/ref0: 11,636,915 retired executions over the same leg — `rotate_vector=1778261`, `transform_vector=1421080`, `multiply_matrices=950895`, `rotation_matrix=191301`, `cos_sin16=657733`, `angle_from=727273`, `turn_toward=66606`, `start_edges=343686`, `angle_pairs=2156844`, `transformed_points=571944`, `transform=828478`, `animate=718948`, …); owned counts EQUAL the retired counts entry by entry (`math_library rotate_vector=1778261,…`, `trig_math cos_sin16=657733,angle_from=727273,…`, `collision_math start_edges=343686,finish_edges=112224`, `animation_math angle_pairs=2156844`, `bounds_math triangle=84798,points=7080,transformed_points=571944`, `joint_library transforms=828478,matrices_written=828478,done_returns=0,animations=718948,blended_animations=1821,angle_reads=2156844` [= 3 × animations]; the one delta `identity_matrix` 101484 → 101479 is the zero-angle axis-matrix branch reusing the host IdentityMatrix without a nested dispatch — `trig_math.cpp:310-315`); `generated_continuations=0`, `unsupported_inputs=0`, `generated_interiors=0`, `unsupported_entries=0`, `refused_rotate=0,refused_transform=0,refused_animate=0,refusal_state=0,refusal_frame=0,refusal_objects=0,refusal_channels=0,faults=0`; unexercised in both arms on the leg (0/0): clipping_math, joint_library.rotate (RotateJoint), trig pitch/roll/matrix_x/rotation_axis*/matrix_axis*/snap_yaw, collision sixteen_groups_range (as round 2); play smoke `trig_math cos_sin16=6604,angle_from=3313,matrix_y=48,matrix_z=62`, movie smoke `cos_sin16=44020,angle_from=32929`, every `generated_body_*=0`; catalog summary leg `nonzero=25 zero=67 absent=5` (the 36 non-math rows identical to flip 4). RESTORED anchors (the round-2 revert cause, 11/13 legs aborted in `JointLibrary`): RootAnchor* 6/6 on the compiled default (`RootAnchorF57292 Passed 18.70 sec`, `RootAnchorF59991 Passed 18.75 sec`, `RootAnchorPadReady 2.26`, `RootAnchorHeartbeat 3.35`, `RootAnchorHeartbeatAtRestore 3.34`, `RootAnchorGsRestored 2.31`) and a 300-frame `ps2state_slot0_f57292` leg `run_leg: OK` with `joint_library … transforms=38008,matrices_written=33808,done_returns=4200,animations=37916,…,refused_*=0,faults=0`, every `generated_body_*=0` (Socrates' round-3 quad-wise spans + refuse-not-abort, main 30677403/e0eeb855); frames/audio IDENTICAL to flip 4 (= ref0) (`diff_legs: IDENTICAL (lanes=frames,audio, masked 0 B, coalesced 0)`, 4600 keys / 767 windows, last `4600,0xcdb1ff109c38971c` / `766,0xfd605d29418b3536`); mem advisory first divergent key 1080 (`1080,0x3d47b409b9189dde` vs flip4 `1080,0xd27d7e407152c179`; keys 0…1020 identical — the same first key the round-2 same-binary pair read: the retired generated bodies' and the owned adapters' dead stack bytes differ); slowspot 50.07 ms/frame at load_1m 2.18 (ref BASELINE 50.95, ratio 0.983; flip 4 50.47 → 0.992). Held: Release `FAILED=0 warning:=0 ld_warning=0` (BUILD-ID 2026-10-08 17:36:51), config/lints 16/16, component suite ALONE `100% tests passed, 0 tests failed out of 597` (22 skips = flip 4's set; all 23 `MathLibrary*` tests Passed — the subsystem's OLD-bug pins), game-process group -j1 `100% tests passed, 0 tests failed out of 73` (`RootPlayModeSmoke Passed 6.24 sec`, `RootMovieLifecyclePlay Passed 24.59 sec`, both `runner exit=0` in the -V smoke re-run; `RootReleaseLanesIdentical Passed 71.13 sec` with the Debug target rebuilt from the flipped source first, BUILD-ID 2026-10-08 17:39:55). Commit 0fdde3fb (default + table + explicit pins: `test_config.cpp` default on / `Set off → off`, `test_texture_consumers.cpp` control arm `math_helpers=false` stated + compiled default pinned on). `REFERENCE -> flip5/leg`. Owner Socrates (M1). Record: `int_work/m_series/cutover/flip5/SECTION.md`.
- 2026-10-08 flip 6 StreamLibrary file jobs = `hle.stream_files` (StartRead `0x2ad9a8`, IsReading `0x2adaf0`, WaitRead `0x2ada30`, StartSoundBankRead `0x2ada68` on `FileSchedule`; census `stream_library.fallbacks`, owned `stream_library.{file_opens,reads,deliveries}` / `file_schedule.{issued,delivered,consumed}`) — **REVERTED: the frozen legacy anchors refuse to load** — with the key on `SessionServices` is enabled and every restore requires GCLK+SVCS: `nativeee: --load-state …/artdet_f37152.ps2st: selected service deadlines require GCLK` (same for `ps2state_slot0_f57292/f59991`, `slowspot_f40298`, `slowspot_f37092` and the b120 probe image); the game-process group read `73% tests passed, 20 tests failed out of 73` (RootAnchor* 6/6, 11 more anchor-restoring Root tests, the anchor halves of `RootReleaseLanesIdentical`/`RootGsQueueThreadIdentical`, and `RootCdvdPollVisibility`/`RootHeadroomQueryRecompiled`, which measure the retained CDVD poll/headroom arm the owned schedule removes). Census not provable either: on the leg `stream_library=file_opens=4,sample_opens=2,reads=170,deliveries=170,polls=636,waits=24,audio_reads=120,…,fallbacks=2645` (REFERENCE `fallbacks=3421`) — the bank-start entry and the IsReading/WaitRead polls on bank requests fall back by design and the counter has no per-entry split. What held: Release 0/0/0 (BUILD-ID 18:25:18), suite ALONE `100% tests passed, 0 tests failed out of 597`, smokes `runner exit=0` ×2, acceptance leg `run_leg: OK` 4600 frames `diff_legs: IDENTICAL (lanes=frames,audio, masked 0 B, coalesced 0)` vs flip5/leg (mem advisory first key 60), `file_schedule=issued=170,delivered=170,consumed=170`, STREAM RPC `13332→13199` (`sceSifCallRpc 13334→13201`). Flip commit `16c409bd`, revert `e91e5daa`; reverted binary (BUILD-ID 18:49:03): Release 0/0/0, suite 597/597, game group `100% tests passed, 0 tests failed out of 73` (RootAnchor* 6/6), smokes `runner exit=0`; slowspot 50.50 ms/frame at load 2.88 on the reverted binary (ref BASELINE 50.95 → 0.991; flip 5 50.07 → 1.009; the flip binary cannot run the slowspot anchor). `hle.files` not flipped (depends on this stage holding); its probe refuses the same anchors: `selected disc files require FILS`. Owner: Herschel (M3) — legacy GCLK/SVCS (and FILS) import for the frozen anchors in the root restore (with M0), and a per-entry `stream_library` fallback split so criterion (c) is provable; `REFERENCE` stays `flip5/leg`.
- 2026-10-08 flip 1b textures-at-load re-attempt = `hle.graphics_sprites` (after Stage 15 integrated Zeno's scratchpad-stack TextureRead repair 1dc1b80c; `git cherry-pick -x 1cd2a13e` → ee57c253) — **REVERTED: census not zero / owned sprite work zero; the frozen legacy anchors refuse to restore; save-state capture refused** — the repair holds (`graphics_refusals_textures=total=0,…,first=none`, `graphics_textures=loaded=40,destroyed=0,model_calls=287,failed=0,uploaded=0,restores=0` on the 4600-frame leg and both smokes; flip 1 read `total=40` / `loaded=0`), but `graphics_sprites=replaced=0,model_calls=21928` on the leg (exactly flip 1's count; smokes 425 / 517), `graphics_refusals_sprites=total=0`: no `Sprite::Draw` of the coldboot→attract leg or the smokes references one of the 40 owned startup icons (`TextureSelection::Sprite → Material → Probe` finds no Hud owner and returns silently), so the model drew every sprite and the owned draw count is zero — the per-entry row `Graphics.sprites.model_calls` cannot read zero on this leg. Also: with a Graphics producer selected the restore requires the `Graphics selection` (GXSL) section the frozen anchors lack — `nativeee: --load-state …/artdet_f37152.ps2st: required section missing: Graphics selection` (×13; also `old_b120.ps2st`, `ps2state_slot0_f57292.ps2st`) → game-process group `80% tests passed, 15 tests failed out of 76`; and every `--state-every` checkpoint of the coldboot leg is refused (`RootCdvdPollVisibility` 0/25 images: `Graphics shared surfaces: pending retained writeback dependency` ×22, `incoming TextureTable has an unclassified owner` ×2, `Graphics unsubmitted=8` ×1; `RootMetaArenaOff` RAM descriptors 345 vs console 385). Held: Release 0/0/0 (BUILD-ID 19:58:20), suite ALONE `100% tests passed, 0 tests failed out of 597`, smokes `runner exit=0` ×2, leg `run_leg: OK` `diff_legs: IDENTICAL (lanes=frames,audio, masked 0 B, coalesced 0)` vs flip5/leg (mem advisory first key 60 — the owned icon conversion's guest-RAM footprint). Revert `09db3b74` (five files byte-identical to ea3d7217); reverted binary (BUILD-ID 20:37:23): Release 0/0/0, suite 597/597, game group `100% tests passed, 0 tests failed out of 76`, smokes `runner exit=0` ×2; slowspot 49.52 ms/frame at load 2.80 (BASELINE 50.95 → 0.972; flip 6 revert 50.50 → 0.981). Owner: Zeno (M6/M7; GXSL/TXAS legacy import with Herschel's GCLK/SVCS/FILS import; a per-ID sprite census or a leg that draws the icons; checkpoint capture with sprites selected; a `RootMetaArenaOff` pin). `REFERENCE` stays `flip5/leg`. Record: `int_work/m_series/cutover/flip1b/SECTION.md`.
- 2026-10-08 flip 7 MovieDecoder pictures = `hle.graphics_movie` (the owned Movie picture producers decode/queue/draw, `hle/fmv/movie_graphics.cpp`; census `Graphics.movie.{queue_original,draw_original}` + `movie_graphics.decode_original`, owned `movie_graphics.{decoded,upload_intents,view_intents,draw_intents,presents}`; flip 6c1aa22a) — **REVERTED: frames NOT identical to REFERENCE (the round-2 presentation-seam divergence, still unattributed as the retired model's defect); the frozen legacy anchors refuse to restore; save-state capture refused** — the census itself held: leg `movie_graphics=decoded=2326,upload_intents=2323,view_intents=2317,draw_intents=2317,presents=2323,decode_original=0,queue_original=0,draw_original=0,errors=0` (REFERENCE `decode_original=2326,queue_original=2323,draw_original=2323`), movie smoke `decoded=547,…,presents=545,decode/queue/draw_original=0`, play smoke all 0 (no movie); but `python3 verify/diff_legs.py <REFERENCE=flip5/leg> flip7/leg --lanes frames,audio --allow-build-diff` → `frames.fhash: FIRST DIVERGENT frame at key 171 (paired by key): (170 keys paired before it, coalesced=0) A: 171,0xaa40d7823a22a254 B: 171,0x012901761a28705b`, `audio.ahash: identical (767 lines)`, `diff_legs: DIVERGENT` — divergent keys 171, 184, 185, 4026, 4027 (5 of 4600; `fmv_window=170-4025`); the owner-requested `--dump-frames` pair (200 frames, same binary, `--allow-set-difference hle.graphics_movie`) reads `f171.ppm` 100 % of pixels different (the model arm's f171 == f172 — the first picture — while the owned arm shows a different picture), `f184.ppm` 99.9 % (owned arm = its drawn buffer `f184_drawn_0x70000.ppm`; the model holds the last picture, its f184 == f185), `f185.ppm` 9.1 % (rows 54..434); the game-process group `82% tests passed, 14 tests failed out of 76` (13 anchor restores `nativeee: --load-state …/artdet_f37152.ps2st: required section missing: Graphics selection`, also `old_b120.ps2st` and the slowspot anchor; `RootCdvdPollVisibility` `FAIL [poll] boot case 0 >= 6 at the final image (b60)` — 2 of 25 `--state-every` images written, 23 `.refused`: `graphics_resources: Graphics shared surfaces: pending retained writeback dependency` ×22, `graphics: Graphics unsubmitted=8,pending=0,ordering_errors=0` ×1 at b120). What held: Release `FAILED=0 warning:=0 ld_warning=0` (BUILD-ID 2026-10-08 21:17:20), config/lints/TextureConsumers 18/18, component suite ALONE `100% tests passed, 0 tests failed out of 597`, `RootPlayModeSmoke Passed 6.27 sec` / `RootMovieLifecyclePlay Passed 24.78 sec` (`runner exit=0`), leg `run_leg: OK` 4600 frames `env_hash=0x4b170af3afd2b4c9`. `hle.movie_step` NOT attempted (requires graphics_movie). Revert 2a4e8785 (config.h / gen_settings_table.py / settings_table.tsv byte-identical to 1a60ebe4; explicit `--set hle.graphics_movie=off` kept on the four retained-arm Root tests and the three root anchor scripts); reverted binary (BUILD-ID 2026-10-08 21:46:39): Release 0/0/0, suite `100% tests passed, 0 tests failed out of 597`, game group `100% tests passed, 0 tests failed out of 76` (RootAnchor* 6/6, `RootCdvdPollVisibility Passed 56.16 sec`), smokes `runner exit=0` ×2; slowspot `ms_frame 49.69` at load 3.99 (ref BASELINE 50.95 → 0.975; flip 1b revert 49.52 → 1.003; report only). Owners: Bacon (M2) with Zeno (M7) for the presentation seam (`GraphicsMovie::Hooks::select/clear` → `VideoStack::SelectMovieFrame/ClearMovieFrame` vs the model's scanout; the `--dump-frames` pixels are in `int_work/m_series/cutover/flip7/pair/`), Zeno (+Herschel) for the GXSL legacy import and the checkpoint predicate (the flip-1b gaps 1 and 3). `REFERENCE` stays `flip5/leg`.
- 2026-10-08 flip 8 Audio: `hle.music` (MusicLibrary — MsReadFile/PlayStream/InterleaveStream/AddStreamChannel/SetMibEndOffset/DisableKeyOn/AllowKeyOn/StopStream owned, Status/Reply shared; through `SoundOutput(samples, music, output)` the key also composes the sample/bank/control owner and, through the root, the owned file delivery + `SessionServices`; census `music_library.fallback` + the retained-side `libsd_trans`/`iop_cdvd.reads`/`snd_kon` — no catalog row exists; flip d74126cc) — **REVERTED: frames AND audio NOT identical to REFERENCE; the frozen legacy anchors refuse to restore** — acceptance leg `run_leg: OK` 4600 frames, `diff_legs: DIVERGENT (lanes=frames,audio, masked 0 B, coalesced 0)`: `frames.fhash: FIRST DIVERGENT frame at key 212 … A: 212,0xd7d799fae96b2f14 B: 212,0xebad29d49e48a8bc` (4376 of 4600 keys differ; from the title scene the owned arm runs 6 presented frames ahead — the stage11 service-time lead, REGRESSION_ATTRIBUTION.md §2-3 reproduced: `loader_waits_module=42/170 → 0/0`, `iop_inframe_us=240324 → 16966`, request ticks 131→130 … 2208→2200), `audio.ahash: FIRST DIVERGENT audio window at line 178 (key 177): A: 177,0xdaa2946b6b2bcd32 B: 177,0x0a6fb8af536edb25` (the owned arm is SILENT over windows 177-201 and 674-699; `audio_first_nonsilent_window=177 → 202`: `LoaderFileIo::StartMusicRead` submits each 4 MiB track window as one read charged at its full sector extent before readiness — MUSIC.md "Timing limit", `music_library reads=4 bytes=16777216`); both attributed to the OWNED arm's timing, not to a retired-model defect; mem advisory first key 0. Census of the owned entries held: leg `music_library=windows=4,plays=4,children=4,interleaves=4,ends=4,prepares=4,starts=4,stops=2,reads=4,delivered=4,bytes=16777216,stale=0,errors=0,fallback=0,statuses=6792`, `sound_banks=issued=120,delivered=120,…,fallbacks=0`, `sound_controls=staged=566,applied=566,…,fallback=0`, `stream_library=…,reads=294,deliveries=294,…,fallbacks=0`, `libsd_trans=0/0/0` (REF 5256576/640/0), `iop_cdvd=reads=0` (REF 369), `snd_kon=0` (REF 16); smokes `fallback=0` with `sound_banks 42/42`, `stream_library reads=65/66`. Anchors: every frozen image refuses `selected service deadlines require GCLK` → game group `74% tests passed, 20 tests failed out of 76` (flip 6's 20 names; `RootCdvdPollVisibility` also `FAIL [poll] snd_kon > 0 … (the title music keyed)` — its 1500-frame poll leg read `snd_kon=0`, `music_library plays=2 fallback=0`, `audio_first_nonsilent_window=218`, and its `--state-every` images were WRITTEN: `PASS [poll] boot case 7 >= 6 at the final image (b1440)`); the slowspot bench anchor refuses the same way. Release 0/0/0 (BUILD-ID 2026-10-08 22:28:37), config/TextureConsumers 18/18, suite `100% tests passed, 0 tests failed out of 597`, `RootPlayModeSmoke Passed 6.20 sec`, `RootMovieLifecyclePlay Passed 24.63 sec` (`runner exit=0`). `hle.sound_samples` NOT attempted (the leg already ran the sample owner: `sound_samples_binding=on`). Owner Herschel (M5/M4): music initial readiness (preload/refill instead of the complete-window charge), `hle.service_time` settling the bank-wait deadline, the Audio-composed legacy import (HANDOFF #3 §10 item 1), a catalog row for the music/sample/file models. Revert 28a9a079 (default files byte-identical to 1a60ebe4; pins kept default-independent): Release 0/0/0 (BUILD-ID 22:59:38), 18/18, suite `100% tests passed, 0 tests failed out of 597`, game group `100% tests passed, 0 tests failed out of 76` (RootAnchor* 6/6, `RootCdvdPollVisibility Passed 58.13 sec`, `RootReleaseLanesIdentical Passed 71.19 sec`), smokes `runner exit=0` ×2; slowspot `ms_frame 49.68` at load 3.32 (ref BASELINE 50.95 → 0.975; flip 7 revert 49.69 → 1.000). `REFERENCE` stays `flip5/leg`. Record: `int_work/m_series/cutover/flip8/SECTION.md`.
- 2026-10-09 flip 2b Graphics bridge re-attempt = `hle.graphics_frame` (after Stage 16 integrated Zeno's fixes 2/3/4, main 10a62733; e57a3281 no longer applies — re-expressed as 0d0f7b9e) — **REVERTED: census not zero on the leg; frames NOT identical to REFERENCE; `RootMovieLifecyclePlay` aborts; the frozen legacy anchors refuse `required section missing: Graphics selection`** — `RootPlayModeSmoke Passed 6.21 sec` reads EXACTLY the owner's HANDOFF 4 §4 values (`graphics_frame_send=calls=59,replaced=59,model_calls=0`, `graphics_frame_other=…,refused=0,empty_sends=1`, `graphics_refusals_frame=total=0,…,first=none`, `graphics_frame_output=completed=58,setups=59,layouts=58,retained_clears=0`, `graphics_output_faulted=0`), but the deterministic coldboot `--frames 63` reads `graphics_refusals_frame=total=1,…,first=frame:62/source_frame:2/n:1/producer:0x19f1c8/detail:514/reason:ordering` (`0x200|UnclosedDependency` on the second Link's StartFrame — the frame-61 Send is now an `empty_send`, so fix 2's hypothesised cause is not it), `retained_clears=1`, `graphics_owned_output=…,failed=1`, `graphics_output_faulted=1`; the 4600 leg COMPLETES (`run_leg: OK`; fix 4 publishes the faulted census) with `graphics_frame_output=completed=0,setups=1,layouts=1,retained_clears=2` and `Graphics.frame.send.model_calls=4534`, `interrupt 2323`, `clear 3307`, `layout 6860` (`refused=17024`, last `producer:0x19aff0/detail:3`), `frames.fhash: FIRST DIVERGENT frame at key 84 … A: 84,0xa741b33a5c037124 B: 84,0x32b7c8a196897e0f` (4457 of 4600; `audio.ahash: identical (767 lines)`; timing rows = REFERENCE: the refused Sends copy the unwritten `g_FrameChain`); `RootMovieLifecyclePlay` / `RootPlayWallLock` `runner exit=134`, `park=exception`, `park_detail=Graphics walk did not deliver every accepted predecessor` after a second `kSendFrame` refusal `first=frame:174/source_frame:313/n:1/producer:0x181968/detail:7` (the first display Send after the first movie starts); game group `78% tests passed, 17 tests failed out of 76` (12 anchor restores `required section missing: Graphics selection`, the two aborts, `RootPlayPolicyInert` frames, `RootCdvdPollVisibility` 1/25 images — 24 `.refused` `graphics: Graphics owned pending=0,failed=1`); mem advisory boundary 0 ATTRIBUTED: `diff_legs.py --allow-set-difference hle.graphics_frame --mask-ranges …/mem_boundary0_mask.txt` → `IDENTICAL-UNDER-MASK (lanes=mem, masked RAM0 1376 B, SPAD 0 B, META 0 B)` (row `P7-FRAME-CHAIN` added to EQUIVALENCE_PLAN.md §7.1). Held: Release 0/0/0 (BUILD-ID 2026-10-09 00:31:46), config/lints 18/18, component suite ALONE `100% tests passed, 0 tests failed out of 598`. Revert e55988bb (the five default/pin files byte-identical to 10a62733): Release 0/0/0 (BUILD-ID 00:58:48), 18/18, suite 598/598, game group `100% tests passed, 0 tests failed out of 76` (RootAnchor* 6/6, `RootCdvdPollVisibility Passed 62.56 sec`, `RootReleaseLanesIdentical Passed 71.16 sec`), smokes `runner exit=0` ×2; slowspot `ms_frame 50.05` at load 3.76 (ref BASELINE 50.95 → 0.982; flip 8 revert 49.68 → 1.007); the flip binary cannot load the bench anchor (GXSL). Owner Zeno (+Herschel for the GXSL import): `int_work/m_series/graphics/CUTOVER_FLIP2B_FLIP3B_TO_ZENO_2026-10-09.md`. `REFERENCE` stays `flip5/leg`. Record: `int_work/m_series/cutover/flip2b/SECTION.md`.
- 2026-10-09 flip 3b native Strip primitives re-attempt = `hle.graphics_strips=all` — **NOT ATTEMPTED (flip 2b did not hold)**; the owner's §4 re-test was run as `--set hle.graphics_strips=all` on the reverted binary: `--frames 118/120` probes and both smokes COMPLETE (`graphics_output_faulted=0`, `runner exit=0`), `graphics_strip_output=captured=32,completed=16,failed=0,retained=8,first_refusal=514` (120 frames; movie smoke `captured=160,completed=152,retained=8`), `Graphics.strip.{plain,tinted,placed,placed_tinted}.model_calls=0`, `graphics_refusals_strips=total=8,unsupported_state=8,faults=0,first=frame:117/source_frame:57/n:1/producer:0x1a9120/detail:514` (**F = 0x200|UnclosedDependency** on the title's first owned strip group); a flip would still revert on the anchors (`--set hle.graphics_strips=all --anchor artdet_f37152.ps2st` → `required section missing: Graphics selection`). Record: `int_work/m_series/cutover/flip3b/SECTION.md`.
- 2026-10-09 flip 8b MovieDecoder pictures re-run under DT-13 = `hle.graphics_movie` (then `hle.movie_step`, not attempted) — **REVERTED: DT-13 blocking criterion (1) — the frozen legacy anchors refuse to load: `nativeee: --load-state …/save_states/artdet_f37152.ps2st: required section missing: Graphics selection`; game-process group `82% tests passed, 14 tests failed out of 76`** (flip a51e18b4 = 6c1aa22a re-expressed on 349bb028, BUILD-ID 2026-10-09 08:51:45; revert 6632f958). Nothing since Stage 16 (10a62733) changed the GXSL class: `engine/core/savestate/section_registry.h:26` `required = true` by default and `GraphicsRuntime::RegisterState` (`app/host/graphics_runtime.cpp:245-280`) sets no legacy-absent policy for `Graphics selection`; the direct probe `run_leg … --anchor artdet_f37152.ps2st --frames 4` → `run_leg: FAIL: run.meta frames=0 way below requested 4`. Failing (same 14 as flip 7): `RootRegistersVuVifGifSections`, `RootBindsDmacSyncSeams`, `RootRegistersIopmSection`, `RootRestoresStreamWorker`, `RootRestoresStreamHeartbeat`, `RootRoutesGsPageAndDrawIr`, `RootVideoStackPresents`, `RootVu1LibraryNone`, `RootFramesKeyConvention`, `RootAnchorGsRestored` (`runner[gs_restored] exit=2`), `RootColdbootProbeResumes` (`runner exit=2`, `run.meta frames=0`), `RootReleaseLanesIdentical` (`guard=DIVERGENT artdet/debug leg failed`), `RootGsQueueThreadIdentical`, `RootCdvdPollVisibility` (`FAIL [poll] boot case 0 >= 6 at the final image (b60)`: 2/25 `--state-every 60` images, 23 `.refused` = 22 × `graphics_resources: Graphics shared surfaces: pending retained writeback dependency`, 1 × `graphics: Graphics unsubmitted=8,pending=0,ordering_errors=0`); the slowspot bench anchor refuses the same way (`frames 0/600 … FAIL`). HELD: (1-partial) Release 0/0/0, config/lints 18/18, component suite ALONE `100% tests passed, 0 tests failed out of 598` (22 skips = flip 2b's set), the six `RootAnchor*` Soft-backend anchors pass through their explicit `--set hle.graphics_movie=off`; (2) acceptance leg `run_leg: OK` 4600 frames `finalized=1 park=none park_detail=frames_cap env_hash=0x4b170af3afd2b4c9` (= REFERENCE), `fmv_window=170-4025`, `fmv_served_per_playback=201,346,1779`; smokes `RootPlayModeSmoke Passed 6.20 sec` / `RootMovieLifecyclePlay Passed 24.49 sec`, both `runner exit=0`, `run.meta finalized=1`; no refusal storm — `movie_graphics … errors=0`, `graphics_movie_transport=interrupt_model_calls=2323,present_model_calls=4540,failed=0`; (3) census `Graphics.movie.queue_original = 0`, `Graphics.movie.draw_original = 0`, `movie_graphics=decoded=2326,upload_intents=2323,view_intents=2317,draw_intents=2317,presents=2323,decode_original=0,queue_original=0,draw_original=0,errors=0` on the leg (REFERENCE `decode_original=2326,queue_original=2323,draw_original=2323`), play smoke all 0 (no movie in 120 frames), movie smoke `decoded=547,…,presents=545,decode/queue/draw_original=0`; catalog `counters=104 nonzero=28 zero=76 absent=0` (REFERENCE 97: 25/67/5; the 7 new StreamLibrary rows and the 5 GraphicsRuntime rows appear). RECORDED (not judged): frames first key 171 — `frames.fhash: FIRST DIVERGENT frame at key 171 … A: 171,0xaa40d7823a22a254 B: 171,0x012901761a28705b`; 5 of 4600 keys differ (171, 184, 185, 4026, 4027 — flip 7's set exactly); `tools/frame_diff.py` on the same-binary 200-frame `--dump-frames` pair (fhash values = the lanes'): f171 100.00 % px, mean max|Δ| 7.15 (max 26; sub-step 99.8 %, chan mean B−A R+5.93 G+1.39 B+6.92), f184 99.87 % px, mean |Δ| 7.66 (max 40), f185 9.08 % px (rows 54..434), mean |Δ| 9.46 over the differing px (0.86 over all; every magnitude a multiple of 8); 4026/4027 not dumped (a 4030-frame dump ≈ 3.7 GB per arm; REFERENCE 4026 == 4027 as 184 == 185). Audio: `audio.ahash: identical (767 lines)` — 0 differing windows, `audio_first_nonsilent_window=177` both. Bacon's stage-40 repairs for 171/184/185 (`port-movie-r3` b527569e/b5820a0a, HANDOFF 40 §1.3: "owned wrong twice") are NOT in `main`; the deltas are therefore flip 7's. Reverted state 6632f958 (default/pin files byte-identical to 349bb028; only a test comment names the revert): Release 0/0/0 (BUILD-ID 09:20:34), 18/18, suite 598/598, game group `100% tests passed, 0 tests failed out of 76` (RootAnchor* 6/6, `RootCdvdPollVisibility Passed 77.11 sec`, `RootReleaseLanesIdentical Passed 101.46 sec`), smokes `runner exit=0` ×2; slowspot `ms_frame 51.30` (total 53.02, p50 51.35, p95 54.70) at load_1m 5.13 — above the BASELINE launch rule 4.0, report only (ref BASELINE 50.95 → 1.007; flip 2b revert 50.05 → 1.025; a first run at load 15.81 read 92.24, kept as `bench-underload-1`). `hle.movie_step` NOT attempted (requires `graphics_movie`). Owner: Zeno (M6/M7) with Herschel — the GXSL legacy import (flip-1b gap 1) and the `graphics_resources` checkpoint predicate (gap 3) gate every Graphics-family flip. `REFERENCE` stays `flip5/leg`. Record: `int_work/m_series/cutover/flip8b/SECTION.md`.

Play-session recorder (from `docs/design/PLAY_SESSIONS.md`; `./build-release/app/host/nativeee --help` on
build 2026-10-08 12:50:33 lists `--profile`, `--harness-window`, `--record-input [file]`, `--frames <N>`,
`--harness-out <dir>`, `--set key=value`; `--game-files <dir>` is parsed (`engine/core/config/config_loader.cpp:267`,
used by every Root test) but MISSING from the `--help` text — reported, not changed here):

```sh
cd <repo> && mkdir -p play_sessions/saves play_sessions/session-1 && cp -R play_sessions/saves play_sessions/session-1/mc_before
./build-release/app/host/nativeee --profile default --harness-window \
  --record-input "$PWD/play_sessions/session-1/session-1.padlog" --frames 36000 \
  --game-files $PS2R_OLD_ROOT/game_files \
  --harness-out "$PWD/play_sessions/session-1" \
  --set "paths.log_dir=$PWD/play_sessions/session-1/logs" \
  --set "paths.mc_dir=$PWD/play_sessions/saves"
```

## All-on build (2026-10-09 →)

ALL-ON MODE (CUTOVER.md, user 2026-10-09 10:12) supersedes the one-flip-per-stage procedure: ONE Release with every
`hle.*` default on except the keys proven to abort the title coldboot. Record: `int_work/m_series/cutover/allon/SECTION.md`;
detail: `docs/design/M_CUTOVER.md` "All-on build". Every line below quotes a result that was read.

**Binary (frozen):** `int_work/m_series/cutover/allon/bin/nativeee_release` — `PS2RECOMP-BUILD-ID 2026-10-09 10:30:39`,
sha256 `b65138a024176abcb8757667d89c51a62f4dd7547bce038d6e8763d72007d57b` (`= build-release/app/host/nativeee` at the
end of the stage; `cmp` clean before every game run). Release `FAILED=0 warning:=0 ld_warning=0 BUILD_RC=0`.

**Keys: 21 on, 6 off.** ON (default): `math_helpers`, `culling`, `movie_lifecycle`, `movie_step`, `movie_start`, `movie_wait`,
`movie_stop`, `sound_bank_headers`, `files`, `sound_samples`, `music`, `music_buffers`, `input_service`, `save_storage`,
`audio_mix`, `service_time`, `graphics_distortion`, `graphics_sky`, `graphics_movie`, `meta_arena`, `libmc_format_in_t0`.
OFF with the reason:
- `hle.graphics_frame` — the four proven aborts (flips 2/2b: frame 62-63 `Graphics ordered delivery failed`).
- `hle.graphics_strips` stays `plain` (flip 3: frame 118 delivery fault; flip 3b `F=0x200|UnclosedDependency`).
- `hle.graphics_sprites` — flips 1/1b (owned sprite work zero, census not provable; checkpoint capture refused).
- `hle.stream_files` — flip 6 (the frozen anchors refuse `selected service deadlines require GCLK`); its file delivery is
  composed anyway by `sound_samples || music` (`stream_library … reads=290 … fallbacks=0`).
- `hle.graphics_shadows` — found by the all-on bisect: the all-23-on coldboot ABORTS at frame 113,
  `park=exception park_pc=0x2b5dfc park_detail=Shadow model load could not establish owned geometry` (`ShadowLoads::Run`,
  `games/<t>/hle/graphics/effects/shadow_loads.cpp:68`; `run_leg: FAIL: runner exit=134`). Owner Zeno.
- `hle.graphics_text` — found by the all-on bisect: with shadows off the coldboot ABORTS at frame 119,
  `park=exception park_pc=0x1a01cc park_detail=Graphics Movie presentation release refused` (`graphics_movie.cpp:140`) —
  the title's FIRST owned text group (frame 117, `graphics_text=replaced=2`) fails delivery (`graphics_service … recorded_work=6,
  completed_work=0,failed_groups=1`, `graphics_owned_output=queued=64,completed=58,pending=5,failed=1`,
  `graphics_output_faulted=1`) and the next owned enqueue (the movie clear) is refused; reproduced with text alone
  (`--set hle.graphics_shadows=off --set hle.graphics_sky=off --set hle.graphics_distortion=off`, frame 119, same
  park_detail) — the frames 60-220 Graphics-bridge class (CUTOVER.md's parallel evidence stage, Zeno). With both off the
  1300-frame coldboot completes.

**Acceptance leg** (`verify/run_leg.py --binary build-release/app/host/nativeee --tree new --anchor coldboot --frames 1300
--profile deterministic --timeout 1200 --stall-s 300 --state-every 60 --out int_work/m_series/cutover/allon/leg`) →
`run_leg: OK`, run.meta `frames=1300 finalized=1 park=none park_detail=frames_cap env_hash=0x4b170af3afd2b4c9 video=gl
build_id=PS2RECOMP-BUILD-ID 2026-10-09 10:30:39`, `graphics_output_faulted=0`, last `1300,0x9792e00955a62324`. TITLE SCREEN
REACHED: the boot state machine (RAM `0x98C140` word0 bits 44..49, `root_cdvd_poll_visibility.py`'s reading) `b1200:
boot_case=6`, **`b1260: boot_case=7`**, and the title clock word (RAM `0x323534`) advances `b1200 354531 → b1260 363471`; the
1800-frame confirmation leg (`allon/leg1800`, `run_leg: OK`, `finalized=1`) reads case 7 on every image b1260…b1740 with the
clock advancing `363471, 372411, 381351, 392339, 401279, 410219, 419159, 428099, 437039` (`allon/leg1800.boot.txt`).
Boot timing vs REFERENCE (flip5/leg): `frames.fhash: FIRST DIVERGENT frame at key 120`; the two intro movies start at
`fmv_window=401-414` (REFERENCE 170): frames 151-366 show the title's MEMORY-CARD CHECK ("Checking memory card (PS2) in memory
card slot 1 … do not remove", `bisect_delay/f180_allon.png`) — the retained arm skipped it (BUG-089: `*format` read from the
stack); under the owned Input/Save services AND the libmc t0 read the title checks the inserted card (+216 frames; both
`hle.save_storage` and `hle.libmc_format_in_t0` must be off to skip it — `bisect_delay/summary.txt`). Not an abort; recorded.

**ctest.** Config/lints/TextureConsumers `100% tests passed, 0 tests failed out of 18`; component suite ALONE
(`-j8 -E '^(Root|GsCorpus|Render_AttractReplay|Render_TitleFrame|GsReplay)'`) **`100% tests passed, 0 tests failed out of 598`**
(22 skips, identical set to flip 8b). Game-process group `-j1` (Debug `nativeee` rebuilt first, BUILD-ID 10:43:46):
**`75% tests passed, 19 tests failed out of 76`** — EVERY red is a frozen-anchor restore/capture refusal under the composition
(recorded, not weakened; keys named): `nativeee: --load-state …/artdet_f37152.ps2st: Restore needs owned logical clock state.`
(×12; also `old_b120.ps2st`, `ps2state_slot0_f57292/f59991.ps2st`, `slowspot_f40298_s1789693059.ps2st` ×2) — key
`hle.input_service`/`hle.save_storage` (`InputSave::Register(…, &session_clock)`); behind it the chain read by the probes
(`allon/anchor_probe/summary.txt`): `selected disc files require FILS` — `hle.files`; `selected service deadlines require GCLK` —
`hle.music`/`hle.sound_samples` (+`service_time`/`audio_mix`/`music_buffers`); `required section missing: Graphics selection` —
`hle.graphics_movie`/`graphics_sky`/`graphics_distortion`; with all four groups off the anchor restores (`frames=4`, `run_leg: OK`).
Red: `RootRegistersVuVifGifSections`, `RootBindsDmacSyncSeams`, `RootRegistersIopmSection`, `RootRestoresStreamWorker`,
`RootRestoresStreamHeartbeat`, `RootRoutesGsPageAndDrawIr`, `RootVideoStackPresents`, `RootVu1LibraryNone`,
`RootFramesKeyConvention`, `RootAnchorGsRestored`, `RootAnchorPadReady`, `RootAnchorF57292`, `RootAnchorF59991`,
`RootAnchorHeartbeat`, `RootAnchorHeartbeatAtRestore`, `RootColdbootProbeResumes`, the anchor halves of
`RootReleaseLanesIdentical` / `RootGsQueueThreadIdentical` (`run_leg: FAIL: run.meta frames=0 way below requested 60`; both
coldboot halves `run_leg: OK`), and `RootCdvdPollVisibility` (`FAIL [poll] boot case 0 >= 6 at the final image (b60)`: 2/25
`--state-every` images, 23 `.refused` = 18 × `graphics_resources: Graphics shared surfaces: pending retained writeback dependency`,
5 × `graphics: Graphics unsubmitted=8,pending=0,ordering_errors=0` — the Graphics producers' checkpoint predicate). Explicit
old-value pins added (nothing weakened): the retained-movie-arm tests state `movie_step/start/wait/stop=off`, `music_buffers=off`,
`audio_mix=off` beside `movie_lifecycle=off`/`graphics_movie=off`; `RootHeadroomQueryRecompiled`/`RootCdvdPollVisibility` state
`sound_samples=off service_time=off music_buffers=off audio_mix=off movie_start/wait/stop=off` beside `stream_files=off`/`music=off`;
the three Soft-backend anchor scripts state `graphics_text/sky/shadows/distortion=off` + `movie_step/start/wait/stop=off`; the
seven boot-timing pins (`RootBindsFmvRows`, `RootMixesFmvPcm`, `RootPlayFmvPresents`, `RootMovieLifecyclePlay`,
`RootPlayPolicyInert`, `RootTitleScreenEffects`, `RootAttractCutsceneFrames` — a movie inside N boundaries, the frames-1..130
fixture, frame key 2519, the cutscene windows) state `--set hle.input_service=off --set hle.save_storage=off
--set hle.libmc_format_in_t0=off` (`PS2R_RETAINED_BOOT_TIMING_ARM`; the three scripts gained a `--set` passthrough) and all
seven pass (`ctest-root-pinned-rerun.log`: `100% tests passed, 0 tests failed out of 7`). Smokes -j1: `RootPlayModeSmoke Passed`
(`runner exit=0`, `finalized=1`, compiled defaults), `RootMovieLifecyclePlay Passed` (`runner exit=0`, `PASS: play mode presented
every movie picture`); the same movie smoke on the FULL all-on composition with a 440-frame budget (census-only, unregistered):
`PASS fmv served_total=547 >= 500 (both movies served inside 440 boundaries)`, `PASS: play mode presented every movie picture`.

**Known at publication (the user's play test; each a stage):** (1) TITLE MUSIC SILENT — `music_library=windows=2,plays=2,…,
starts=0,…,errors=2,refused_buffers=2` on the 1300- and 1800-frame legs (`audio_first_nonsilent_window=none`):
`MusicBuffers::CanPlay` refuses both title plays (`music_library.cpp:103`); with `--set hle.music_buffers=off --set
hle.movie_start=off --set hle.movie_wait=off --set hle.movie_stop=off` the music plays (`starts=2,delivered=2,bytes=8388608`,
`audio_first_nonsilent_window=237`, `allon/probe6-music_buffers_off_1800`). Key `hle.music_buffers` (Herschel) — kept ON per the
rule (it does not block the title screen). (2) the memory-card check at boot (+216 frames; hardware behaviour, see above).
(3) `hle.graphics_sky` STILL EXECUTING the model: `graphics_sky_start=replaced=0,model_calls=54`, `graphics_refusals_sky=total=54,
missing_resource=54,…,first=frame:1246/source_frame:1731/n:1/producer:0x1b9cf8/detail:4/reason:missing_resource` (every title-screen
sky start refused; owner Zeno). (4) the 19 anchor-restore tests above (legacy GCLK/FILS/GXSL/clock import — Herschel/Zeno/M0/M9).

**Census (leg 1300 / play smoke 120 / movie smoke all-on 440)** — full table in `docs/design/M_CUTOVER.md` and
`allon/census_table.md`: ZERO (model 0, owned > 0): `math_helpers` (61 `generated_body_*` rows 0; owned `cos_sin16=271453`,
`joint_library transforms=11346 animations=6611`), `culling` (10 `CullingLibrary.generated.*` rows 0 — REFERENCE 1.5 M; owned
`setup_chunk=270 load_model=1602 test_cell=3992 test_box_at=3482`), `movie_lifecycle` (`ipu_commands.bclr=0 … model_pops=0`;
owned `started=2 closed=2 audio_feeds=1953`), `graphics_movie` (`Graphics.movie.{queue,draw}_original=0`; owned `decoded=547
presents=545`; retained transport `present_model_calls=1240` by design), `movie_step/start/wait/stop` (`Movie.player.* = 0`;
owned `steps=545 starts=2 waits=545 stops=2`), `sound_samples` (`sound_banks 120/120 fallbacks=0`, `sound_controls 40/40
fallback=0`, `stream_library reads=290 fallbacks=0`, `libsd_trans=0/0/0`, `loader_slot_status=0/0`), `audio_mix`
(`spu_mix_calls=0`; `owned_channel_samples=58400`), `service_time` (`installed=1 sequence=42`; `loader_waits_module=0/0`),
`sound_bank_headers` (`loaded=2`), `input_service` (`pad_calls read=0 served=0`, `padman_calls=0/0/0/0`; owned `samples=1300
reports=1829`), `save_storage` (`mc_calls getinfo=0 sync=0`, `mcserv_calls=0/0`; owned `calls=1779 published=1`),
`music_buffers` (`fallback=0`; `allocations=5 sound_loans=2`). `music`: model 0 (`fallback=0`, `libsd_trans=0/0/0`,
`iop_cdvd.reads=0`, `snd_kon=0`) but the OWNED plays REFUSED (`refused_buffers=2`, `starts=0`) — see Known (1). STILL EXECUTING:
`graphics_sky` (`model_calls` 54/432/54, owned `replaced=0`). NOT EXERCISED (0/0): `files` (`opens=0 reads=0 original_calls=0`),
`graphics_distortion` (`graphics_distortion_draw=calls=0,model_calls=0`; owned `event_turns=54 settings captured=1`, no distortion
draw before the title). Catalog `census.txt`: `summary: counters=104 nonzero=14 zero=90 absent=0` — the 14 non-zero rows are the
six OFF keys' model rows (`Graphics.frame.*` 1/1237/545/1727/1783, `Graphics.screen.effects` 1230, `Graphics.shadow.{begin,end}`
54/54, `Graphics.sprites` 16508, `Graphics.strip.plain` 16, `Graphics.text` 5165) and `Graphics.sky.*` 54/432/54.

**Slowspot** (`verify/bench/run_bench.py --binary allon/bin/nativeee_release --legs slowspot_f37092_s1789428268 --set <the
retained anchor arm>`): the anchor refuses under the compiled defaults (`Restore needs owned logical clock state.`, `frames
0/600 … FAIL`); with input/save/files/audio/graphics keys stated off (`allon/bench.log`) `frames 600/600 ms_frame 51.15 total
52.81 p50 51.21 p95 54.07 wall 33.0s OK` at load_1m 5.89 (> the 4.0 launch rule — report only); `--compare verify/bench/
BASELINE.tsv` → `50.95 51.15 1.004` (threshold 1.10, report only); vs flip 8b `51.30 51.15 0.997`.

**Play command** (Release, the `default` = play profile, from `docs/design/PLAY_SESSIONS.md`'s recorder command — no `--frames`
cap, `--record-input` optional; every flag listed by `nativeee --help`):
```sh
cd <repo> && mkdir -p play_sessions/saves play_sessions/allon-1/logs
int_work/m_series/cutover/allon/bin/nativeee_release --profile default --harness-window \
  --game-files $PS2R_OLD_ROOT/game_files \
  --harness-out "$PWD/play_sessions/allon-1" \
  --set "paths.log_dir=$PWD/play_sessions/allon-1/logs" \
  --set "paths.mc_dir=$PWD/play_sessions/saves" \
  --record-input "$PWD/play_sessions/allon-1/allon-1.padlog"      # optional: omit to play without recording
```
(`int_work/m_series/cutover/allon/bin/nativeee_release --version` → `PS2RECOMP-BUILD-ID 2026-10-09 10:30:39`. To hear the
title music until Known (1) is fixed add `--set hle.music_buffers=off --set hle.movie_start=off --set hle.movie_wait=off
--set hle.movie_stop=off`.)

## All-on build, no exceptions (2026-10-09 →)

ALL-ON, NO EXCEPTIONS (CUTOVER.md, user 2026-10-09 ~11:30) supersedes the six-off list: EVERY `hle.*` toggle is the compiled
default (27 keys, `graphics_strips=all`). Acceptance = Release 0/0/0 + ctest green only. Record:
`int_work/m_series/cutover/allon_noexc/SECTION.md`; detail + the PER-TOGGLE CENSUS EXPECTATIONS: `docs/design/M_CUTOVER.md`
"All-on build, no exceptions". Every line below quotes a result that was read.

**Binary (frozen):** `int_work/m_series/cutover/allon_noexc/bin/nativeee_release` — `PS2RECOMP-BUILD-ID 2026-10-09 12:36:09`,
sha256 `8ff272041f1ad0eaa75123cf70824b8165a3ae0a937c6a8d0e12f86a966fb43f` (`= build-release/app/host/nativeee`; `cmp` clean,
`BIN_FROZEN_OK`). Release: full build `FAILED=0 warning:=0 ld_warning=0 BUILD_RC=0` (482 steps, `build-release-1.log`) + the
incremental relink after the session-path fault-pc fix `FAILED=0 warning:=0 ld_warning=0` (`build-release.log`).

**Keys: 27 on, 0 off.** The six the first all-on build kept off are on: `graphics_frame`, `graphics_strips=all`,
`graphics_sprites`, `graphics_shadows`, `graphics_text`, `stream_files`. `gen_settings_table --check` →
`reproducible (97 rows) -> PASS` (+ `harness.inject_fault_frame`). Commits `48a388e9` (defaults), `25049c05` (`--set`
degradation + the fault-injection pin), `2d4c2818` (triage bundle, drawlist ring, census at exit), `0e49db9f` (Root pins +
session-path fault pc), and the publication commit.

**Product behaviour in this binary:**
1. CRASH PATH → TRIAGE BUNDLE. Any park but `none` (an uncaught exception, a GuestPark, a `std::terminate` on any thread; play
   AND harness mode) writes `<harness-out | paths.log_dir>/triage/<UTC timestamp>/{census.txt, drawlist_last200.txt, fault.txt,
   backtrace.txt, run.meta}` before terminating (`engine/core/triage/TriageBundle`, `engine/gs/hle/ir/DrawlistRing` = a FIXED ring
   of 200 frames / 32768 draw records allocated once — BUG-083's lesson; `app/host/leg_closer.cpp`). run.meta gains
   `triage_bundle=<dir>`; stderr announces `nativeee: triage bundle written: <dir>`. Pins: `CoreTriageBundle` (synthetic fault →
   every file present and non-empty; the ring's bound), `RootTriageBundleOnFault` (TIMEOUT 120: the real binary at
   `--set harness.inject_fault_frame=100` → `runner exit=134`, `park=exception`, `frames=100`, the five files, the `census:`
   block — `Passed 2.41 sec`).
2. `--set hle.<key>=off` for EVERY hle.* key in play mode (`ConfigValidator::DegradeDependents`): a prerequisite turned off DEGRADES
   its dependents with one logged line each — `[config] hle.movie_step disabled: requires hle.graphics_movie` — instead of a
   refusal (unit per rule in `Coretest_config`; the real binary's line is pinned by `RootTriageBundleOnFault`). Validate keeps its
   refusals as the invariant checker.
3. The CENSUS is PRINTED at exit: a `census:` block (the run.meta rows, two-space indented) on stdout, normal exit (`probe-normal-exit.log`
   line 1 `census:` … `park=none park_detail=frames_cap`) and crash path (`smoke/play_mode_smoke.runner.log` line 6 `census:` …
   `park=exception`); without a harness `census:` carries the park rows and `(no run.meta: the harness was not armed)`.

**Suite (binary 8ff27204…):** config/lint/schema group `100% tests passed, 0 tests failed out of 19`; the component suite ALONE
`-j8 --timeout 900 -E '^(Root|GsCorpus|Render_AttractReplay|Render_TitleFrame|GsReplay)'` → **`100% tests passed, 0 tests failed
out of 599`**, `Total Test time (real) = 183.58 sec`, 22 `***Skipped` (the same fixture-skip set as the previous all-on,
`skipset.txt`); the game-process group `-j1` (Debug `nativeee` rebuilt first: `build-debug-nativeee.log` 0/0) →
**`74% tests passed, 20 tests failed out of 77`**, `Total Test time (real) = 844.67 sec`. The 20 reds:
- 19 = the frozen-anchor refusals of the previous all-on (RECORDED, not weakened; `root_fail_logs/anchor_refusals.txt`, 17 lines):
  `nativeee: --load-state …/artdet_f37152.ps2st: Restore needs owned logical clock state.` ×12, `…/old_b120.ps2st` ×1,
  `…/ps2state_slot0_f57292.ps2st` ×1, `…/f59991.ps2st` ×1, `…/slowspot_f40298_s1789693059.ps2st` ×2 — key `hle.input_service` /
  `hle.save_storage` (`app/host/input_save.cpp:99` → `session_clock.cpp:139`; behind it FILS `hle.files`, GCLK
  `hle.music`/`sound_samples`/`stream_files`, GXSL the Graphics producers): `RootRegistersVuVifGifSections`, `RootBindsDmacSyncSeams`,
  `RootRegistersIopmSection`, `RootRestoresStreamWorker`, `RootRestoresStreamHeartbeat`, `RootRoutesGsPageAndDrawIr`,
  `RootVideoStackPresents`, `RootVu1LibraryNone`, `RootFramesKeyConvention`, `RootAnchorGsRestored`, `RootAnchorPadReady`,
  `RootAnchorF57292`, `RootAnchorF59991`, `RootAnchorHeartbeat`, `RootAnchorHeartbeatAtRestore`, `RootColdbootProbeResumes`,
  `RootReleaseLanesIdentical` / `RootGsQueueThreadIdentical` (both coldboot halves `run_leg: OK`; the artdet half `run_leg: FAIL:
  run.meta frames=0 way below requested 60`), `RootCdvdPollVisibility` (`FAIL [poll] boot case 0 >= 6 at the final image (b60)`:
  the `--state-every` images refused by the Graphics checkpoint predicate — `root_fail_logs/cdvd_poll_refused.txt`: 18 ×
  `graphics_resources: Graphics shared surfaces: pending retained writeback dependency`, 23 × `graphics: Graphics owned
  pending=0,failed=1`, 5 × `graphics: Graphics unsubmitted=8,pending=0,ordering_errors=0` — key: the Graphics producers).
- 1 = **`RootPlayModeSmoke`** at the all-on composition = **KNOWN-CRASH #1** (below). Not an anchor refusal, not weakened: the
  smoke IS the all-on play-mode probe.
- Old-value pins (nothing weakened): the 19 Root tests whose premise is a coldboot past frame 60 state the retained Graphics arms
  EXPLICITLY (`tests/app/CMakeLists.txt` `PS2R_RETAINED_GRAPHICS_ARM = --set hle.graphics_frame=off --set hle.graphics_strips=plain
  --set hle.graphics_sprites=off --set hle.graphics_shadows=off --set hle.graphics_text=off`; nine root scripts gained the `--set`
  passthrough); `tests/core/test_config.cpp` and `tests/title/textures/test_texture_consumers.cpp` state every new default and
  its old value. First game run without them: `53% tests passed, 36 tests failed out of 77` (`ctest-root-1.log`: 23 aborts at
  frame 63/113 + the 13 refusals).

**KNOWN-CRASH #1 (the all-on play-mode smoke; `smoke/play_mode_smoke/`, frozen binary):** `runner exit=134`, run.meta `frames=63
finalized=1 park=exception park_pc=0x1a01cc park_tid=1 park_detail=Graphics Movie presentation release refused
graphics_output_faulted=1 triage_bundle=…/root_play_smoke_4y8o4zr7/triage/20261009T171056Z` (copied to
`int_work/m_series/cutover/allon_noexc/smoke/play_mode_smoke/triage/20261009T171056Z/`). Its `fault.txt`:
```
park=exception park_pc=0x1a01cc park_tid=1
error: Graphics Movie presentation release refused
faulting_draw: draw seq=8 frame=62 inst=0 handler=0x0 kick_pc=0x0 ord=0 prim=6 ctx=1 verts=192 fb=0x2f0000/512 psm=2 z=0x1c0000 tex=0x1c0000/50 abe=0 bbox=[8,0,0,512,448,0]
producer: graphics_output_faulted=1
  graphics_owned_output=queued=7,completed=5,pending=0,failed=1,retained=1
  graphics_movie_transport=interrupt_model_calls=0,present_model_calls=3,failed=1
  graphics_refusals_frame=total=3,missing_resource=0,unsupported_state=0,ordering=3,faults=0,first=frame:62/source_frame:2/n:1/producer:0x19f1c8/detail:514/reason:ordering,last=frame:63/source_frame:2/n:3/producer:0x19aff0/detail:3/reason:ordering,first_fault=none
throw_site: noted on thread b9a4d6611fc0e9c what=Graphics Movie presentation release refused
```
(`app/host/graphics_movie.cpp:140`: the owned Frame producer's first sends are refused `ordering` at frame 62 (producers `0x19f1c8`
detail 514, `0x19aff0` detail 3), the owned output fails one item, and the Movie clear's `EnqueueOwned` is refused → `GraphicsMovie::
Dispatch` throws — flips 2/2b's class.) Bisect on the frozen binary (`bisect/summary.txt`, 300-frame deterministic coldboots):
`--set hle.graphics_frame=off` → `frames=113 park_detail=Shadow model load could not establish owned geometry`; `+ hle.graphics_shadows=off`
→ `frames=119 park_detail=Graphics Movie presentation release refused` (the text group's delivery failure); `+ hle.graphics_text=off`
→ `frames=300 park=none` (sprites and strips=all stay on). This crash is the graphics-bridge shared-cause attribution stage's
input (the graphics worktree `port-graphics-r2`'s partial work is preserved, untouched).

**Play command** (Release, `--profile default` = the play profile; from `docs/design/PLAY_SESSIONS.md`'s recorder command — no
`--frames` cap, `--record-input` optional; every flag listed by `nativeee_release --help`). The crash path writes the bundle under
`--harness-out` (`play_sessions/allon-noexc-1/triage/<UTC>/`) — attach that directory to the bug report:
```sh
cd <repo> && mkdir -p play_sessions/saves play_sessions/allon-noexc-1/logs
int_work/m_series/cutover/allon_noexc/bin/nativeee_release --profile default --harness-window \
  --game-files $PS2R_OLD_ROOT/game_files \
  --harness-out "$PWD/play_sessions/allon-noexc-1" \
  --set "paths.log_dir=$PWD/play_sessions/allon-noexc-1/logs" \
  --set "paths.mc_dir=$PWD/play_sessions/saves" \
  --record-input "$PWD/play_sessions/allon-noexc-1/allon-noexc-1.padlog"      # optional: omit to play without recording
```
Bisect form — turn ONE key back off per run (every hle.* key; the dependents degrade with a `[config] …` line): append
`--set hle.<key>=off` (`--set hle.graphics_strips=plain` for the enum). To get past KNOWN-CRASH #1 to the title:
`--set hle.graphics_frame=off --set hle.graphics_shadows=off --set hle.graphics_text=off`. To hear the title music (Known (1) of the
previous all-on, `hle.music_buffers`): `--set hle.music_buffers=off` (degrades `movie_start/wait/stop` with three `[config]` lines).
The exit census (`census:` block on stdout, also `run.meta` / the bundle's `census.txt`) is read against the PER-TOGGLE CENSUS
EXPECTATIONS table in `docs/design/M_CUTOVER.md` "All-on build, no exceptions".

**Census of the all-on smoke (63 frames, the crash):** catalog `summary: counters=104 nonzero=3 zero=101 absent=0` — the three non-zero
rows are `Graphics.frame.layout.model_calls=1`, `Graphics.frame.send.model_calls=1` (the refused frame sends fell back to the model
before the fault) and `Graphics.screen.effects.model_calls=3` (no toggle). Owned work before the fault: `graphics_frame_setup=calls=1,
replaced=1,model_calls=0`, `graphics_frame_clear=calls=3,replaced=3,model_calls=0`, `graphics_textures=loaded=40`, `stream_library=
file_opens=4,…,reads=3,deliveries=2,…,fallbacks=0`, `input_service=…,samples=63`, `save_storage=…,calls=…`.

### 2026-10-09 17:18 — fixes 2: the Graphics-bridge shared cause + the silent title music — KNOWN-CRASH #1 CLOSED, music plays

Integration (`integration-20261009-fixes2` from `bfaa6cb2`, `git merge --ff-only` into `main`; record
`int_work/m_series/cutover/allon_fixed/`). Two APPROVED stages (`int_work/m_series/graphics/SHARED_CAUSE_REVIEW.md`,
`int_work/m_series/stream/MUSIC_BUFFERS_REVIEW.md`), cherry-picked `-x` (patch-id check: every owner commit `git cherry` `-`):
- Graphics (Zeno, `port-graphics-r2`): `13d80546 4907310b 11b77a74 6f37e71a 1805a371 2db61778 67adb637 39fc3f7d 14d10c97 19f70e73
  ba342434 b5eb6760` → `41dae2c3..f91e8db5`. SKIPPED `a2974164` (Zeno's copy of the music fix — the same clause as Herschel's, whose
  approved commit carries the unit pin, the ServiceState join and the Root pin). Build `-j4`: `FAILED=0 warning:=0 ld_warning=0
  BUILD_RC=0` (377 steps, `build-release-step1-graphics.log`).
- `d1ab9bf1 test(graphics): Re-express three stale pins under the group lifecycle` (integrator): three test binaries were STALE
  in the owner's `build-graphics` (linked 2026-10-08) — rebuilt on `build-release` they read `EffectsScene: 67 checks, 3 failures`,
  `GraphicsCheckpoint: 161 checks, 1 failures`, `GraphicsProducers` abort `Graphics producer draw failed at its ordered slot`. Each
  pinned the pre-fix behaviour the approved fix changes (a same-buffer ledger entry → settled, not refused; a renderer failure at
  the slot → throw, not false; a declined owned sky → `refused_slots`, not `failures`); re-expressed stricter the way the owner
  re-expressed `test_graphics_frame_clear.cpp` (67→75, 161→162, 320→321 checks). Focused Graphics set `-j4` after it
  (`ctest-step1-graphics-focused.log`): `100% tests passed, 0 tests failed out of 105` (6 pre-existing oracle skips).
- Music (Herschel, `port-services-r3`): `2ab2bfa7 db815f09 704dffb6` → `17b7ffca 8fddb7bc 966187d1`, clean. Build `-j4`:
  `FAILED=0 warning:=0 ld_warning=0 BUILD_RC=0` (219 steps, `build-release-step2-music.log`). Focused services set + lints
  (`ctest-step2-music-focused.log`): `100% tests passed, 0 tests failed out of 41`; `MusicSelfOracle: checks=4366 failures=0
  compared_frames=614400 rpc=804`, `ALL PASSED`.

**Binary (frozen, kept beside the previous):** `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.966187d1` —
`PS2RECOMP-BUILD-ID 2026-10-09 17:18:06`, sha256 `973606bc68985d92f8d5a3c7f71653826c00a6f81f9aee53ccd6d5e8217f87fe` (`cmp` clean,
`BIN_FROZEN_OK`; the previous `nativeee_release` = `8ff27204…` stays). `main` HEAD `966187d1`. Keys: 27 on, 0 off, unchanged.

**Suite (binary `a7198e05…` = the same sources before the build-id relink; `pre-relink/`):** component suite ALONE
`ctest --test-dir build-release -j8 --timeout 900 -E '^(Root|GsCorpus|Render_AttractReplay|Render_TitleFrame|GsReplay)'` →
**`100% tests passed, 0 tests failed out of 600`**, `Total Test time (real) = 206.06 sec`, 22 `***Skipped` = the previous all-on's
set exactly (`skipset.txt` diff empty). Game-process group `-j1` (Debug `nativeee` rebuilt first, `build-debug-nativeee.log`
`FAILED=0 warning:=0 ld_warning=0`, 99 steps) → **`76% tests passed, 19 tests failed out of 80`** (+1 disabled `GsCorpus_slowspot`),
`Total Test time (real) = 709.92 sec`. The 19 reds (`ctest-root.log`, `root_fail_logs/`), every one read:
- 19 = the frozen-anchor refusals of the previous all-on, the SAME 19 names (diff of the two red lists = `RootPlayModeSmoke` only,
  now green): `root_fail_logs/anchor_refusals.txt` — `Restore needs owned logical clock state.` ×17 lines (`artdet_f37152` ×12,
  `slowspot_f40298_s1789693059` ×2, `ps2state_slot0_f57292` ×1, `ps2state_slot0_f59991` ×1, `old_b120` ×1): `RootRegistersVuVifGifSections`,
  `RootBindsDmacSyncSeams`, `RootRegistersIopmSection`, `RootRestoresStreamWorker`, `RootRestoresStreamHeartbeat`, `RootRoutesGsPageAndDrawIr`,
  `RootVideoStackPresents`, `RootVu1LibraryNone`, `RootFramesKeyConvention`, `RootAnchorGsRestored`, `RootAnchorPadReady`, `RootAnchorF57292`,
  `RootAnchorF59991`, `RootAnchorHeartbeat`, `RootAnchorHeartbeatAtRestore`, `RootColdbootProbeResumes`, `RootReleaseLanesIdentical` /
  `RootGsQueueThreadIdentical` (coldboot halves `run_leg: OK`; artdet half `run.meta frames=0`), and `RootCdvdPollVisibility`
  (`FAIL [poll] boot case 0 >= 6 at the final image (b60)`; its `--state-every` refusal class CHANGED as the shared-cause stage §4.6
  predicted: the 18 × `pending retained writeback dependency` are GONE (`SettleForCapture`), what remains is 10 × `graphics: Graphics
  unsubmitted=8,pending=0,ordering_errors=0 (recording open before Link)` — `root_fail_logs/cdvd_poll_refused.txt`). RECORDED, not
  weakened; key `hle.input_service`/`hle.save_storage` → GCLK chain, as before.
- 0 other reds. **`RootPlayModeSmoke` PASSED** (`Passed 6.26 sec`) at the compiled defaults — KNOWN-CRASH #1 is closed.
  **`RootGraphicsBridgeAllOn` PASSED** (`20.85 sec`: the five 300-frame shapes frame63/shadows/text/strips/sprites, each `park=none`).
  **`RootTitleMusicPlays` PASSED** (`26.46 sec`). `RootMovieLifecyclePlay`, `RootTriageBundleOnFault` passed.

**RE-TEST on the frozen binary `973606bc…` (zero keys off):**
- `RootPlayModeSmoke`'s exact command, artifacts kept (`smoke/play_mode_smoke/run.meta`): `runner exit=0`, `frames=120 finalized=1
  video=gl park=none park_detail=frames_cap`, `graphics_output_faulted=0`, `graphics_owned_output=queued=290,completed=278,pending=12,
  failed=0,retained=0,refused=0,cancelled=0`, `graphics_movie_transport=interrupt_model_calls=0,present_model_calls=60,failed=0,
  release_refused=0`, `graphics_frame_send=calls=59,replaced=59,model_calls=0`, `graphics_text=replaced=5,model_calls=0,failed=0,refused=0`,
  `graphics_shadow_assets=…,converted=16,…,failed=0,refused=0,…,rejected=0`, every `graphics_refusals_*=total=0`, no `triage_bundle`.
  `music_library=…plays=0…` (the title music starts after frame 120 — not exercised by the smoke; `errors=0 refused_buffers=0`).
- Coldboot 1300 (`leg/`): `verify/run_leg.py --binary build-release/app/host/nativeee --tree new --anchor coldboot --frames 1300
  --profile deterministic --timeout 3000 --stall-s 600 --out …/allon_fixed/leg` → `run_leg: OK`, `frames=1300 finalized=1 park=none
  park_detail=frames_cap graphics_output_faulted=0`; lanes identical to the pre-relink run of the same sources (`frames.fhash` /
  `audio.ahash` `cmp` clean; last keys `1300,0x9947b140cfe6cf44`, `216,0xea6c7ef37aef3c71`).
- Coldboot 1800 with `--state-every 60` (`pre-relink/leg1800/`, `boot_census_leg1800.txt`): `run_leg: OK frames=1800 finalized=1 park=none`;
  **TITLE REACHED**: `b1260: boot_case=7 title_clock=0x00058bcf (363471)`, `first image at boot case 7: b1260`, `title clock advancing
  across case-7 images: YES (b1260:363471, b1320:372411, … b1740:437039)` — the same boundaries and clock words as the 21-on all-on's
  leg1800. **MUSIC PLAYS**: `music_library=windows=2,plays=2,children=2,interleaves=2,ends=2,prepares=2,starts=2,stops=0,reads=2,
  delivered=2,bytes=8388608,stale=0,errors=0,refused_window=0,refused_buffers=0,refused_voice=0,fallback=0`, `music_buffers=installed=1,
  allocations=5,resizes=3,…,sound_loans=2,disc_loans=2,returns=4,errors=0,…,active_loans=0`, `audio_first_nonsilent_window=237`
  (= the `music_buffers=off` reference exactly; previously `starts=0 errors=2 refused_buffers=2`, `none`). The `--set hle.music_buffers=off`
  play-command workaround is obsolete.
- Census block (1800): `graphics_service=…,completed_work=8331,refused_work=0,ordering_errors=0,failed_groups=0,discarded_groups=2,
  discarded_walks=2`, `graphics_owned_output=queued=15726,completed=15720,pending=4,failed=0,retained=0,refused=0,cancelled=2`,
  `graphics_strip_output=captured=2216,completed=2216,failed=0`, `graphics_frame_send=calls=1237,replaced=1233,model_calls=4` +
  `graphics_refusals_frame=total=4,…,ordering=4,…,first=frame:405/…/producer:0x181968/detail:7,last=frame:420/…` and
  `graphics_refusals_strips=total=2,…,ordering=2,…,first=frame:405/source_frame:545/n:1/producer:0x17ed50/detail:4,last=frame:418/…`
  (the title's own `ResetBuckets` at the movie start/end: the two dropped groups DISCARDED, the four transition Sends refused to the
  model Send — RECORDED by the stage, §5), `graphics_text=replaced=1139,model_calls=0`, `graphics_shadow_assets=reads=643,converted=16,
  …,refused=0,rejected=0`, `graphics_refusals_shadows=total=0`, `graphics_refusals_textures/sprites/text=total=0`,
  `graphics_refusals_sky=total=554,missing_resource=554` (unchanged class: every title-screen sky start refused `missing_resource`, the sky
  model STILL EXECUTES), `graphics_sprites=replaced=0,model_calls=17508,refused=0` (selection policy: only the startup owner set is owned —
  the sprite model STILL EXECUTES, RECORDED). Catalog `summary: counters=104 nonzero=6 zero=98 absent=0` (`census_nonzero_leg1800.txt`:
  `Graphics.frame.send.model_calls=4`, `Graphics.screen.effects.model_calls=1730` (no toggle), `Graphics.sky.{start,model,finish}.model_calls=
  554/4432/554`, `Graphics.sprites.model_calls=17508`); every other replaced model at ZERO incl. the frame layout/setup/clear/interrupt,
  strips, text, shadows, movie, music, buffers, files, input, saves.
- Frames vs the 21-on all-on's leg (`allon/leg`, DT-13 RECORDED not judged, `diff_vs_allon21.txt`): `frames.fhash: FIRST DIVERGENT frame at
  key 86` (the owned frame producer now draws from frame 61 — the retained arm drew those frames before), `audio.ahash: identical (217 lines)`.

**Closed:** KNOWN-CRASH #1 (frame 63 `Graphics Movie presentation release refused`) and the whole flip-1/2/2b/3/3b/6/7 abort class
(frames 60/113/118/119 under the bridge keys; the sixth shape 405/418 found behind them) — `graphics_frame`, `graphics_strips=all`,
`graphics_sprites`, `graphics_shadows`, `graphics_text` run through the title with `graphics_output_faulted=0`. Known (1) of the
first all-on (silent title music under `hle.music_buffers`) — fixed. **Still open (named):** the 19 frozen-anchor refusals (GCLK);
the sky `missing_resource` class; the sprite selection scope; the 4 `kSendFrame` detail-7 Sends and 2 discarded groups at the movie
transitions; the music complete-window charge (first audible window 237 — timing only); the `service_time` bank-wait lead.

**Play command:** as above with the new binary — `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.966187d1` in place of
`nativeee_release`; no `--set … =off` needed to reach the title or to hear the music.

### 2026-10-09 20:26 — s2 play bugs integrated (record; the binary the user played s3 on)

Integration `integration-20261009-s2` → `main` `7e5f0abb` (ff-only; record `int_work/m_series/cutover/allon_s2/`): shadows
`a34400c1 da18ecdb 8f509891` (a default mesh without owned input REFUSES, never faults; RootShadowLevelEntry pin) and audio
`4d9c2204 1a7a8615 f80c8b72 7e5f0abb` (pause holds the voices it finds; the music stream starts at its buffer fill; RootMenuSfxAudible,
RootTitleMusicPlays pins). Component suite ALONE `100% tests passed, 0 tests failed out of 600`; game group `-j1` `76% tests passed,
20 tests failed out of 84` = the 19 frozen-anchor refusals + `RootShadowLevelEntry` (its 6000-frame route stayed on the main menu:
`graphics_shadow_mesh calls=0` — the shared `memcards/` of the build tree held an autosave; the s3 pin below isolates its card).
Re-test on the frozen binary `100% … out of 4`. **Binary (frozen):** `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.7e5f0abb`,
`PS2RECOMP-BUILD-ID 2026-10-09 20:25:51`, sha256 `d639d9943f6b8709dedb61e2124e6f2895f84a154af8abdb623f75582fa91e54` (the user's
s3 session ran the same sources as `build-release` 19:47:57). The user's s3 report on it: no shadows + a white streak over Crash,
F5 silent, `pace_late=2794/3068`, `audio_ring underruns=9552` (CUTOVER.md "S3 PLAY BUGS").

### 2026-10-09 23:35 — s3 play bugs: shadows DRAW, the streak is gone, the performance round — F5/F8 NOT yet (NEEDS_CHANGES)

Integration `integration-20261010-s3` from `7e5f0abb`, `git merge --ff-only` into `main` → code HEAD **`dd9b20b0`** (11 commits; + this record's docs-only commit on top = `main` HEAD, no source change; record
`int_work/m_series/cutover/allon_s3/`). Two of the three s3 stages APPROVED and cherry-picked `-x` (patch-id: every owner commit
`git cherry` `-` after the pick; no shared root file touched by either):
- **Shadows (Lagrange, `port-textures`; review `int_work/m_series/graphics/LAGRANGE_SHADOWS_S3_REVIEW.md` APPROVED):** `422c096c e4fd6eb0
  f9867640 3888daec` → `fe5f4b1e 8ae5c693 65bdd600 13f108ae`. Build `-j4`: `FAILED=0 warning:=0 ld_warning=0 BUILD_RC=0` (241 steps,
  `build-release-step1-shadows.log`). Focused effects set + lints `-j4` (`ctest-step1-shadows-focused.log`): `100% tests passed, 0 tests
  failed out of 28` (1 skip = `ShadowAssetsSelfOracle`, in the fixture skipset); `ShadowPass: 129 … pins`, `EffectsScene: 84 checks, 0
  failures`, `ShadowOwnership: 1229 checks`, `ShadowApplication: 48 … checks`.
- **Performance (Zeno, `port-graphics-r2`; review `int_work/m_series/graphics/PERF_S3_REVIEW.md` APPROVED):** `f1d43f22 9fb530a7 e25f3bef
  73a1a5ae 2c4e36a0 3c7b6597 b5709082` → `675ff5e4 469b025e 55495d07 c086e5ff 3318516f 7ae51965 dd9b20b0`. Build `-j4`: `FAILED=0
  warning:=0 ld_warning=0 BUILD_RC=0` (448 steps, `build-release-step2-perf.log`). Focused GS/texres/lint set `-j4`
  (`ctest-step2-perf-focused.log`): `100% tests passed, 0 tests failed out of 110` (3 pre-existing stub skips `Gs_Inv25/26/30`);
  `row addresser: 1327104 addresses compared, 1327104 equal`, `test_texsrc prep_bytes: 85 checks, 0 failures`, `test_vram_image viewsig:
  17 checks, 0 failures`, `test_transfer_engine cursor: 20 checks, 0 failures`. `73a1a5ae` (the title's VU1 library on the MTVU worker's
  unit) is a BEHAVIOUR change in play (raw route → the library + class renderers, the route the deterministic acceptance legs already
  draw through) — accepted here as the stage attributed it; the review's identity-pin recommendation stays open (below).
- **Savestates (Bernoulli, `port-product` `a94d1131`; review `int_work/m_series/input_saves/SAVESTATES_S3_REVIEW.md`): VERDICT
  NEEDS_CHANGES — NOT integrated.** The review's list: (1) the F8 preflight must compare the slot's `.meta composition=` (and `build_id`)
  with `SavestateSlots::Composition(cfg)` BEFORE returning Working, refusing by name in the LIVE session (today a same-tag-set slot of
  another profile closes the session and the relaunched process exits 2 — `root_play_relaunch_restore_r6a.log`); add `ReadMeta` + a unit
  case + a Root case with the play window still open; (2) `main.cpp` `execv(argv[0])` → the resolved running image
  (`_NSGetExecutablePath` / `/proc/self/exe`, fallback `execvp`) or resolve in the preflight; (3) re-run `RootPlaySaveRestore` and
  `RootPlayRelaunchRestore` once with `--keep` (alone) and preserve `run.meta`, both `frames.fhash` lanes, `slot0.ps2st.meta`,
  `restores/<stamp>/run.meta` under `int_work/m_series/input_saves/root_pins/`. Its seven commits and six isolated patches
  (`bernoulli-16*.patch`) are preserved in the worktree.

**Binary (frozen, kept beside the previous):** `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.dd9b20b0` —
`PS2RECOMP-BUILD-ID 2026-10-09 22:55:06`, sha256 `5605757753f9cbc63f121baf5c9770058d03a9afb2c13a2cd94d4b49ceec81e0` (`= build-release/
app/host/nativeee`, `cmp` clean, `BIN_FROZEN_OK`; the sha was unchanged from before the suite through the merge:
`release-binary.sha256.{pre-suite,pre-root,final}`). `nativeee_release.7e5f0abb` / `.966187d1` / `nativeee_release` stay. Keys: 27 on, 0 off,
unchanged (no Config key added; `gen_settings_table` untouched).

**Suite (binary `56057577…`):** component suite ALONE `ctest --test-dir build-release -j8 --timeout 900 -E '^(Root|GsCorpus|Render_AttractReplay|
Render_TitleFrame|GsReplay)'` → **`100% tests passed, 0 tests failed out of 601`**, `Total Test time (real) = 157.12 sec`, 22 `***Skipped`
= the previous set exactly (`skipset.txt` diff empty, `SKIPSET_SAME`). Game-process group `-j1` (Debug `nativeee` rebuilt first:
`build-debug-nativeee.log` `FAILED=0 warning:=0 ld_warning=0`, 139 steps) → **`77% tests passed, 19 tests failed out of 84`** (+1 disabled
`GsCorpus_slowspot`), `Total Test time (real) = 1187.56 sec` (`ctest-root.log`, `ctest-root.LastTest.log`). The 19 reds (`red_list.txt`,
`red_names.txt`; `diff` against the previous 19 names EMPTY — `RED_SET_SAME_AS_PREVIOUS_19`), every one read:
- 19 = the frozen-anchor refusals, unchanged: `root_fail_logs/anchor_refusals.txt` = the SAME 17 lines `nativeee: --load-state …:
  Restore needs owned logical clock state.` (`artdet_f37152` ×12, `slowspot_f40298_s1789693059` ×2, `ps2state_slot0_f57292` ×1,
  `ps2state_slot0_f59991` ×1, `old_b120` ×1): `RootRegistersVuVifGifSections`, `RootBindsDmacSyncSeams`, `RootRegistersIopmSection`,
  `RootRestoresStreamWorker`, `RootRestoresStreamHeartbeat`, `RootRoutesGsPageAndDrawIr`, `RootVideoStackPresents`, `RootVu1LibraryNone`,
  `RootFramesKeyConvention`, `RootAnchorGsRestored`, `RootAnchorPadReady`, `RootAnchorF57292`, `RootAnchorF59991`, `RootAnchorHeartbeat`,
  `RootAnchorHeartbeatAtRestore`, `RootColdbootProbeResumes`, `RootReleaseLanesIdentical` / `RootGsQueueThreadIdentical` (the artdet half
  `run.meta frames=0`), `RootCdvdPollVisibility` (`FAIL [poll] boot case 0 >= 6 at the final image (b60)`). RECORDED, not weakened; key
  `hle.input_service`/`hle.save_storage` → GCLK owned-clock state, as before.
- 0 other reds. **`RootShadowLevelEntry` PASSED** (`463.21 sec`; was red on `7e5f0abb` — s3 bug #1's pin): `RootShadowLevelEntry: ALL PASS`
  (26/26, `root_shadow_level_entry_judge.txt`): `graphics_shadow_mesh=calls=80682,replaced=80682,model_calls=0`,
  `graphics_shadow=instances=80682,blocks=16288,groups=241968,completed=6309,empty=1710,faults=0,refused=0,refused_meshes=0,
  last_mesh_refusal=none`, `graphics_shadow_scene=published=4599,empty=1710` (4599+1710 = 6309), `graphics_refusals_shadows=total=0`,
  `PASS frame f7000 dark footprint under Crash: sand between the feet 107.0 <= ring 144.4 - 22`, `PASS frame f7000 no white vertical
  streak: longest near-white run 0 px`, f7500 `107.1` / `0 px` — the same census the owner's leg read, on the combined binary (the
  frame dump leg at `wall_ms=462274`, was 653407 before the perf commits). `RootPlayModeSmoke` `Passed 6.18`, `RootGraphicsBridgeAllOn`
  `Passed 20.93`, `RootTitleMusicPlays` `Passed 32.65`, `RootMenuSfxAudible` `Passed 38.65`, `RootMovieLifecyclePlay` `Passed 24.59`,
  `RootMtvuPlaySmoke` `Passed 6.21`, `RootMtvuOffDeterministic`, `RootTriageBundleOnFault`, `RootFootprintBounded` passed. Re-test `-j1` after
  the merge on the frozen (identical) binary (`ctest-retest.log`): `RootGraphicsBridgeAllOn Passed 15.85`, `RootPlayModeSmoke Passed 6.17`,
  `RootTitleMusicPlays Passed 22.39`, `RootMenuSfxAudible Passed 30.44` — `100% tests passed, 0 tests failed out of 4`.
  (`RootPlaySaveRestore` is not on `main`: the savestates stage was not integrated.)

**s3 bugs — fixed / open, with evidence (play-profile replays of the user's own `play_sessions/s3/session.padlog`, wall-paced
`--harness-window`, one game process, `verify/bench/run_s3.py --play`; before = `nativeee_release.7e5f0abb`, after = this binary;
`play1500-{before,after}/`, `play4400-{before,after}/`, each with `S3_WINDOWS.tsv`, `leg/run.meta`, the lanes, `run_leg.log`):**
1. **SHADOWS — FIXED.** Before (4400 frames, through the first level): `graphics_shadow_mesh=calls=25512,replaced=0,model_calls=25512`,
   `graphics_shadow=instances=0,…,refused_meshes=25512,last_mesh_refusal=check:4/result:2/object:0xbca9a0/geometry:0xbaa624/model_id:
   0xf37a152c/asset_id:0x0`, `graphics_refusals_shadows=total=25512,unsupported_state=25512,…,producer:0x1be9f8/detail:516` — the user's
   s3 census exactly. After: `graphics_shadow_mesh=calls=25428,replaced=25428,model_calls=0`, `graphics_shadow=instances=25428,blocks=4646,
   groups=76224,completed=3110,empty=1589,faults=0,refused=0,refused_meshes=0,last_mesh_refusal=none`, `graphics_shadow_scene=published=1521,
   empty=1589`, `graphics_refusals_shadows=total=0`. Attribution (owner, verified by the review): check 4 / result 2 = Shape/UnsupportedSource
   on `needsEye` — every default mesh's type-0B shader answers `NeedsEye=1` (TR `renderer/resources.cpp:138`, `shaderclasses.cpp:461`; NTSC
   `0x1d9330` `jr ra; li v0,1`); needsEye only sizes the placed instance (11 quadwords, block closes at the 8th — TR `models.cpp:30-34,431-459`);
   the shader vtable constant was wrong (`0x2fa8f0` = slot 5 of the real table → `ShadowAssets::kShaderVtable = 0x2fa8c8`, the factory
   store at `0x1bb948`); the default mesh's `screen`/`camera` matrices live on the main thread's SCRATCHPAD stack (TR `entry.cpp:61-62,83`) →
   `ShadowSource::Value` serves `0x7000xxxx` through the same reader. **The white streak** = a refused mesh ran the MODEL body inside an
   OWNED bracket (the begin had bypassed `StartShadows`' half-size target set-up, so the bucket-21 additive strips landed in the scene) —
   now a mesh inside an owned bracket is drawn by NEITHER arm (attributed `refused_meshes`/`last_mesh_refusal`), and a pass with ZERO
   accepted meshes PUBLISHES NOTHING (`EffectsScene::Shadows` empty guard before lease/clear/composite; `empty_shadow_passes`;
   `graphics_shadow=…,empty=N`, new row `graphics_shadow_scene=published=N,empty=N`). Pinned: `RootShadowLevelEntry` (census + pixel
   footprint + no-streak pins, red on `7e5f0abb` by the owner's `red2_7e5f0abb` leg, green here), `EffectsScene::EmptyPass`,
   `ShadowPass` RefusedMesh/BlockCapacity, `ShadowAssetsSelfOracle` eye-poisoned stride-11 cases.
2. **F5 / F8 / `--load-state` in play — OPEN (stage NEEDS_CHANGES, not integrated).** In this binary F5 is still the reproduction save
   under `logs/reproductions/` of the paused session menu (PLAY_SESSIONS.md) and F8 its live restore; neither captures the all-on
   composition (the user's "nothing written, no visible refusal" stands). `--load-state` accepts only a state whose owners all restore —
   none exists for the play composition yet (the frozen anchors refuse `Restore needs owned logical clock state`). To report a bug by
   state until the stage lands: record the session (`--record-input <file>.padlog`, already in the play command) and attach the padlog +
   `run.meta` + any `triage/<UTC>/` bundle; the recording replays deterministically (`--inputs`, as this stage's legs did).
3. **PERFORMANCE — IMPROVED, gate (pace_late → 0 in the level) NOT met; the level is still every frame late.** Same workload, same
   machine, one game process, load_1m at launch in each header (none idle: 5.0–6.6):

   | replay | binary | load_1m | pace_late | pace_parks | audio_ring underruns / dropped | title/FMV ms/frame | level ms/frame | overall | texres prep_bytes |
   |---|---|---|---|---|---|---|---|---|---|
   | 1500 fr (title → FMV) | before `7e5f0abb` | 5.21 | **377**/1500 | 1123 | **273** / 49888 | 17.16–24.41 | — | 31.78 | 4.52 GB |
   | 1500 fr | after `dd9b20b0` | 5.89 | **291**/1500 | 1209 | **121** / 0 | 16.96–17.88 | — | 30.35 | 0.48 GB |
   | 4400 fr (→ first level) | before `7e5f0abb` | 6.64 | **2919**/4400 | 1481 | **3712** / 123136 | 17.07–24.90 | 56.9–59.3 | 45.89 | 236 GB |
   | 4400 fr | after `dd9b20b0` | 5.02 | **2604**/4400 | 1796 | **892** / 240352 | 17.16–22.03 | **35.0–35.7** | 36.98 | 8.7 GB |

   (the 300–600 window = the boot/title disc load at ~77 ms/frame on both; 2700–3000 = the level load, 160 → 149.) Title + FMV run AT pace
   (16.96–18.06 vs the 16.7 ms period) and the FMV's audio drops are gone over 1500 frames (`dropped 49888 → 0`); the first level went from
   ~58 to ~35 ms/frame (−40 %) with `underruns 3712 → 892` (ring `dropped` ROSE 123136 → 240352 on the 4400 pair — recorded, not
   attributed: the faster producer overfills the ring where the slower one starved it), but at 35 ms every level frame is still late (`pace_late` 2604 of 4400 ≈ the
   1400 level frames + the load). Where the level frame goes (owner's `sample(1)` profile, verified by the review): the GS pipeline runs
   INLINE on the EE thread (~72 %: `TransferEngine::WriteImage`, `TextureResolver`, `TargetRefresher::RefreshCt16`, class renderers + GL);
   the structural item is DP-25 (`GsQueue::Pipelined` off the EE thread with the VRAM-image barrier) — a fidelity-contract decision for the
   parent with its own identity gate (PERFORMANCE.md §7, PERF_S3_1.md §7). The route in play changed: `drawir identity_stamped 0 → 130228`
   (1500) / `6779815` (4400), `classes raw-other 5492706 → 12060`, `vu1_hle_worker=3765107/15/10`, `graphics_kick_lowering_worker=
   submitted=6779815,…,lowered=5610922,raw=1168893` — play now draws through the title's VU1 library + class renderers, the deterministic
   legs' route. Fidelity of the pure fixes: deterministic lanes identical over 8000 frames (fixes 1+2) and 4400 (all; review §3,
   independently reproduced over 3600). Everything in `graphics_*` lifecycle rows on the 4400 after-leg: `graphics_output_faulted=0`,
   `graphics_owned_output=queued=64544,completed=64540,pending=1,failed=0,retained=0,refused=0,cancelled=3`, `graphics_service … refused_work=0,
   ordering_errors=0, failed_groups=0`, `park=none park_detail=frames_cap`, `music_library=…starts=4…errors=0 refused_*=0`.
   Hazard recorded by the perf stage (PERF_S3_1.md §6), NOT reproduced on this binary (both 4400-frame play replays and the 7600-frame
   deterministic pin passed the level entry): a play-mode level load that spans > 64 frames of bracketed shadow passes with no kick hit
   `Shadow pass limit` (`shadow_pass.cpp` `pending_.size() >= 64`) once on the faster pre-shadow-fix binary — if the user's session aborts
   at level entry with that text, the triage bundle names it; owner Lagrange.

**Play command:** INTEGRATION_STATUS.md "All-on build, no exceptions" with `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.dd9b20b0`
in place of `nativeee_release`; no `--set … =off` needed. The exit `census:` block now carries `graphics_shadow=…,empty=N`,
`graphics_shadow_scene=published=N,empty=N` (expected on a level: `graphics_shadow_mesh model_calls=0`, `refused_meshes=0`,
`instances/blocks/groups > 0`, `published > 0`, `graphics_refusals_shadows total=0`), `vu1_hle_worker=runs/misses/declines`,
`graphics_kick_lowering_worker=…`, `gsops_recorder=off` and `texres … sig_hits=` in play.

**Still open (named):** F5/F8/`--load-state` (Bernoulli, the three review items); the level's pace (DP-25 decision + the WriteImage bulk
store, `RefreshCt16` attribution, `glReadPixels` readback — PERFORMANCE.md §7); the perf review's non-blocking items (an identity pin for the
worker-library route + the single-PATH1-producer assertion, the mip-chain fields in the texres reuse key, re-freezing `bin/nativeee_fix1/_fix3`,
the idle-machine table); the 19 frozen-anchor refusals (GCLK); the sky `missing_resource` class (`graphics_refusals_sky=total=3111` on the 4400
leg, model still executes); the sprite selection scope (`graphics_sprites=replaced=381,model_calls=19251`); the 6 `graphics_frame_send`
model Sends at the movie transitions; INTERFACES.md §8a wording for a call inside a committed bracket (Zeno).

### 2026-10-10 01:5x — play-mode savestates: F5 at the title / in play, F8 relaunch, `--load-state` — s3 bug #2 CLOSED

Integration `integration-20261010-saves` from `1dd99548`, `git merge --ff-only` into `main` → code HEAD **`96c8525c`** (18 commits; + this
record's docs-only commit on top = `main` HEAD, no source change; record `int_work/m_series/cutover/allon_saves/SECTION.md`). The re-review
`int_work/m_series/input_saves/SAVESTATES_S3_REVIEW2.md` is **APPROVED** (the three items of the first review closed with code + unit +
real-process evidence; Zeno's settle is a state model of the Present → SyncV → Link boundary with no new hook). Cherry-picked `-x`
(patch-id: every owner commit `git cherry` `-` after the pick, one exception named):
- **Graphics capture-time settle (Zeno, `port-graphics-r2` bc7fa8eb; INTERFACES.md §13):** `6ef21062 85f6a086 0a21e498 d8d095a0 f6ee870b
  2ab79d17 bc7fa8eb` → `01ce10b3 5fe00058 32baf92f c5d33ea8 da0ca041 2a7eb93b c3a903c1`. Build `-j4`: `FAILED=0 warning:=0 ld_warning=0
  BUILD_RC=0` (517 steps, `build-release-step1-graphics.log`). Focused graphics/effects/session/lint set `-j4` (`ctest-step1-graphics-focused.log`):
  `100% tests passed, 0 tests failed out of 81` (3 pre-existing self-oracle skips).
- **F5/F8/`--load-state` slots (Bernoulli, `port-product` 22ab19d8):** `57aea8fb a8820ad4 555647d1 d34a2cc9 b54dcb21 b3add337 b07a4a02
  b0d7bdf1 f50aad37 22ab19d8` → `2341329f e56bbb9a 6789b695 cd0968b8 6f523d0a 8e9a5310 184681b4 f7faf671 50b54dce d8bdf67e`. `555647d1`
  reads `+` only because its `tests/app/CMakeLists.txt` hunk met Zeno's `RootPlaySaveAtTitle` block at the same line — resolved by keeping
  both blocks (the combined tree vs `port-product` = Zeno's 46 files only, vs `port-graphics-r2` = Bernoulli's 37 files only;
  `texture_selection.cpp` = Zeno's `d8d095a0` = patch 17d + `#include <algorithm>`; patches 17a–f therefore not applied). Build `-j4`:
  `FAILED=0 warning:=0 ld_warning=0 BUILD_RC=0` (574 steps, `build-release-step2-savestates.log`). Focused product/session/config/lint set
  `-j4` (`ctest-step2-savestates-focused.log`): `100% tests passed, 0 tests failed out of 36`; `SavestateSlots: 72 checks, 0 failures`;
  `gen_settings_table: docs/filemap/settings_table.tsv reproducible (100 rows) -> PASS` (+ `paths.save_dir`, `harness.save_at_frame`,
  `harness.restore_at_frame`). **Keys: 27 on, 0 off, unchanged.**
- **Integrator `96c8525c test(savestates): Retire RootPlaySaveRefusal; settle admits boundary 120`:** the owners' and the reviewer's named
  interaction — on the combined binary the test's trigger at boundary 120 WRITES (`ctest-save-refusal-combined.log`: `run.meta
  savestate=written,path=…/slot0.ps2st,frame=120,boundary=120`, `FAIL R: run.meta savestate=refused` ×3, `PASS R` ×2) exactly as its CMake
  note foresaw ("turns RED on purpose … or retire it"). Retired with the reason recorded in place (the loud refusal path stays pinned by the
  script's `--expect-refusal` judge + `RootPlaySaveRestoreSelftest`, the `SessionControls`/`SavestateSlots` units, and `RootPlayRelaunchRestore`
  leg D's real-process F8 refusal with the session kept open); two settle comments "INTERFACES section 12" → 13 (the reviewer's nit). Build
  `-j4` 0/0/0 (64 steps); `Lint*` + `GraphicsService` + `Root*Selftest` + `SettingsTableReproducible` `100% tests passed, 0 tests failed out of 29`.

**Binary (frozen):** `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.96c8525c` — `PS2RECOMP-BUILD-ID 2026-10-10 01:16:32`,
sha256 `c1fe47f1d30560ccc4818d8eb2ff9ce240004b7c41848a4c44e416ad9af884a5` (`= build-release/app/host/nativeee`, `cmp` clean,
`BIN_FROZEN_OK`; sha unchanged pre-suite → pre-root → final and after the re-test; `strings` carries `composition differs:`, `cannot resolve
the running executable image`, `graphics_capture_settle`). `nativeee_release.dd9b20b0` (previous) kept; the first all-on binary
`nativeee_release` (12:36:09, `8ff27204…`) DELETED for disk (`bin/DELETED.txt`; its `.sha256`/`BUILD-ID.txt` records stay).

**Suite (binary `c1fe47f1…`):** component suite ALONE `ctest --test-dir build-release -j8 --timeout 900 -E '^(Root|GsCorpus|Render_AttractReplay|
Render_TitleFrame|GsReplay)'` → **`100% tests passed, 0 tests failed out of 602`**, `Total Test time (real) = 136.98 sec`, 22 `***Skipped` =
the previous set exactly (`SKIPSET_SAME`). Game-process group `-j1` (Debug `nativeee` rebuilt first: `build-debug-nativeee.log` `FAILED=0
warning:=0 ld_warning=0`, 225 steps) → **`80% tests passed, 18 tests failed out of 90`** (+1 disabled `GsCorpus_slowspot`), `Total Test time
(real) = 1064.28 sec` (`ctest-root.log`, `ctest-root.LastTest.log`). The 18 reds (`red_names.txt`), every one read:
- 18 = the frozen-anchor refusals, the previous 19 names MINUS `RootCdvdPollVisibility`: `root_fail_logs/anchor_refusals.txt` = 19 lines
  `nativeee: --load-state …: Restore needs owned logical clock state.` (`artdet_f37152` ×14, `slowspot_f40298_s1789693059` ×2,
  `ps2state_slot0_f57292` ×1, `ps2state_slot0_f59991` ×1, `old_b120` ×1): `RootRegistersVuVifGifSections`, `RootBindsDmacSyncSeams`,
  `RootRegistersIopmSection`, `RootRestoresStreamWorker`, `RootRestoresStreamHeartbeat`, `RootRoutesGsPageAndDrawIr`, `RootVideoStackPresents`,
  `RootVu1LibraryNone`, `RootFramesKeyConvention`, `RootAnchorGsRestored`, `RootAnchorPadReady`, `RootAnchorF57292`, `RootAnchorF59991`,
  `RootAnchorHeartbeat`, `RootAnchorHeartbeatAtRestore`, `RootColdbootProbeResumes`, `RootReleaseLanesIdentical` / `RootGsQueueThreadIdentical`
  (the artdet half `run.meta frames=0`). RECORDED, not weakened; key `hle.input_service`/`hle.save_storage` → GCLK owned-clock state, as before.
- **`RootCdvdPollVisibility` PASSED** (`52.35 sec`, `ALL PASS`; red since the first all-on): its `--state-every 60` images under the retained
  graphics arm were refused `graphics: Graphics unsubmitted=8,pending=0,ordering_errors=0 (recording open before Link)` — the settle admits
  that boundary; `[poll] boot case by image: … b1320:6`, `PASS [poll] boot case 6 >= 6 at the final image (b1320)`.
- 0 other reds. **`RootPlaySaveAtTitle` PASSED** (`20.65 sec`; new, Zeno): `A run.meta savestate=written,path=…/capture/savestates/slot0.ps2st,
  frame=1300,boundary=1300`, `graphics_capture_settle=settles=32,unlinked=32,source_frame=1784,groups=2,items=4,owned=4,buckets=21:1/26:3`;
  `B run.meta restored_vsync_frame=1300`, `graphics_restore_unlinked=source_frame=1784,groups=2,items=4,owned=4,buckets=21:1/26:3` →
  `RootPlaySaveAtTitle: PASS (trigger save_at_frame, boundary 1300, unlinked items 4)`. **`RootPlaySaveRestore` PASSED** (`5.27 sec`; new,
  Bernoulli): `savestate=written,…,frame=60,boundary=60`, `restored_vsync_frame=60`, `PASS B: identity: every restored key k (61..179) hashes
  equal to the uninterrupted key k+d (d=1; first mismatch at d=1: None)`. **`RootPlayRelaunchRestore` PASSED** (`40.29 sec`; new, Bernoulli,
  a real window): leg B `nativeee: savestate F8 restore Relaunch: …/build-release/app/host/nativeee --profile default --harness-window
  --harness-out …/leg_b/restores/<stamp> …`, `run.meta R(<stamp>) restored_vsync_frame=60`; leg D `nativeee: savestate F8 restore Refused:
  …/saves_det/savestates/slot0.ps2st: composition differs: profile(saved=deterministic,live=default) -- the slot was captured under another
  composition; its --load-state would refuse after this session closed, so the session stays open` (no Relaunch, the window ran to its cap).
  `RootPlayModeSmoke` `Passed 6.17`, `RootGraphicsBridgeAllOn` `15.64`, `RootShadowLevelEntry` `371.22`, `RootTitleMusicPlays` `22.37`,
  `RootMenuSfxAudible` `30.51`, `RootMovieLifecyclePlay` `24.59`, `RootMtvuPlaySmoke` `6.17`, `RootMtvuOffDeterministic`, `RootTriageBundleOnFault` passed.
- Re-test `-j1` on `main` after the merge (`ctest-retest.log`; binary `cmp`-identical to the frozen copy before and after): `RootGraphicsBridgeAllOn
  Passed 15.60`, `RootPlayModeSmoke 6.17`, `RootShadowLevelEntry 358.64`, `RootPlaySaveAtTitle 20.70`, `RootPlaySaveRestore 5.37`,
  `RootPlayRelaunchRestore 40.68` — **`100% tests passed, 0 tests failed out of 6`**.

**F5 / F8 / `--load-state` in this binary (s3 bug #2 — CLOSED; the full contract: `docs/design/PLAY_SESSIONS.md` "Savestates", the
Graphics side `games/<t>/hle/graphics/evidence/INTERFACES.md` §13):**
- **Where the slots land:** `<save root>/savestates/slot<N>.ps2st` + `slot<N>.ps2st.meta` (`frame=`, `wall_ms=`, `build_id=`, `composition=`
  every `hle.*` key + the profile, `sections=` the image's tags). The save root is `paths.save_dir` if set, else the `--harness-out` directory,
  else `paths.log_dir` (else `logs`). With the play command below the slots land in `play_sessions/saves/savestates/slot0.ps2st`; `--set
  save.slot=N` picks another slot. `--help` states it: "F5/F8 savestates: `--set save.slot=N` picks the slot, `--set paths.save_dir=<dir>`
  where <dir>/savestates/slot<N>.ps2st lands".
- **F5** (in the play window) captures the all-on composition at the next admissible SyncV boundary — at the TITLE and IN PLAY (the Graphics
  owner settles the open recording instead of refusing `Graphics owned pending=N`). stderr (and the SAVE log + the on-screen notice/menu):
  `nativeee: savestate F5 save Queued: …` the moment the key is pressed, then `… Completed: Savestate written: <slot> (frame F, sidecar
  <slot>.meta)` — or `… Refused: No admissible boundary in 120 consecutive boundaries; the last owner to refuse: <owner>: <reason>` naming the
  owner (today's remaining refusers: a movie playing — `fmv_active`, press again after it; an owned Graphics fault; an immediate owned slot in
  flight; an undelivered sealed submission). Never silent. The restored session's FIRST field omits the captured frame's not-yet-Linked owned
  groups (UI strips, sky, clear, shadow pass), attributed in run.meta `graphics_restore_unlinked=…,items=I` + exactly I `ordering` refusals
  detail 5 (`kRestoredUnlinked`) at the Link producer and one log line — visible for 1/60 s, then identical (§13 last paragraph).
- **F8** restores the browser's selection or the newest slot as a RELAUNCH (a live in-process rewind is refused by the M11 contract while the
  services are retained). Preflight with the session still OPEN: the live registry (`Refused: required section missing: <TAG> (<owner>)`), the
  slot's `.meta` against the live composition (`Refused: <slot>: composition differs: profile(saved=deterministic,live=default) -- the slot was
  captured under another composition; its --load-state would refuse after this session closed, so the session stays open`; a slot of another
  build of the same composition only WARNS: `Warning: <slot>: build_id differs: saved=…, live=… -- the image is of another build of this
  composition; restoring anyway`; a missing sidecar refuses `no sidecar <path>`), and the running executable image resolved from the kernel
  (`Refused: cannot resolve the running executable image for the relaunch: …`). Only then: `… Completed: Restarting from <slot> (--load-state)`,
  `… Relaunch: <argv>` — the window closes and the SAME image re-executes with the same arguments plus `--load-state <slot>` (`--harness-out`
  nested under `<out>/restores/<stamp>`, a `--record-input` renamed `<stem>-r<stamp>.padlog`, `paths.save_dir` pinned). If the exec itself
  fails: `F8 restore Failed: exec <image>: <errno> (execvp <argv0>: <errno>) -- the live session is already closed; nothing is running. Restart
  by hand: <argv>`, exit 2. A refused F8 leaves the live session exactly as it was.
- **`--load-state <slot>`** on the same binary and composition: the play command plus `--load-state "$PWD/play_sessions/saves/savestates/slot0.ps2st"`
  (a harness leg: `--profile deterministic --headless --frames N --load-state <slot>`). A slot of another composition refuses at startup
  (`nativeee: --load-state <slot>: Saved clock/provider selection differs from the running session.`); the frozen PCSX2-era anchors still refuse
  `Restore needs owned logical clock state.` (the 18 reds above). The restored run's `frames.fhash` is keyed from `restored_vsync_frame=F` (F+1…).
- **To report a bug by state:** press F5 at the moment, then attach `play_sessions/saves/savestates/slot0.ps2st` + `.meta`, the padlog and
  `run.meta` (and any `triage/<UTC>/` bundle). The parent reproduces with `--load-state` on the same frozen binary (the `.meta build_id=` names it).

**Play command:** INTEGRATION_STATUS.md "All-on build, no exceptions" with `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.96c8525c`
in place of `nativeee_release` — unchanged otherwise (`paths.mc_dir=$PWD/play_sessions/saves` already there; the slots join it under
`play_sessions/saves/savestates/`; add `--set "paths.save_dir=<dir>"` to put them elsewhere); no `--set … =off` needed.

**Still open (named):** the level's pace (DP-25 decision; PERFORMANCE.md §7); the 18 frozen-anchor refusals (GCLK); the restored first field's
omitted owned groups (per-family intent serialisation, INTERFACES §13 — a fidelity item, not a defect); the perf review's non-blocking items;
the sky `missing_resource` class; the sprite selection scope; the 6 movie-transition model Sends; the review's non-blocking notes
(`WriteMeta`/`Preflight` re-read the 44 MB image; `settles=` counts predicate calls; `bare_name_argv0` cannot distinguish `execv(image)` from
the `execvp` fallback).

### 2026-10-10 15:5x — s4 play bugs integrated: shadows in the PRESENTED frame, `gs.queue=pipelined` (+ its deadlock fix), the kick-unsupported attribution

Integration `integration-20261010-s4` from `9948796b`, `git merge --ff-only` into `main` → code HEAD **`ff12dca8`** (16 commits; record
`int_work/m_series/cutover/allon_s4/SECTION.md`, the second integrator's half under `allon_s4/fix/`). Three owner stages, three independent
reviews, all **APPROVED** (`math/KICK_UNSUPPORTED_S4_REVIEW.md`, `graphics/PERF_S4_REVIEW.md`, `graphics/LAGRANGE_COMPOSITE_S4_REVIEW.md`).
Cherry-picked `-x` (every owner commit `git cherry` `-` after the pick: 15 × `-`). **Keys: 27 on, 0 off, unchanged**; one non-`hle.*`
default changed: **`gs.queue` = `pipelined`** (`gen_settings_table --check` → `reproducible (100 rows) -> PASS`).
- **Kick census (Hypatia, `port-math` 811d169e → `332f8bbe`):** `graphics_kick_lowering_unsupported_by_program=<hash>:<library program>@<entry>:
  <shape>:<count>,…` beside the `graphics_kick_lowering[_worker]` rows. Build 0/0/0 (711 steps), focused 23/23, `test_kick_lowering` 281 checks;
  with/without-census identity over the 4000-frame s4 deterministic replay (drawir/gsops/frames/mem/audio lanes IDENTICAL — the reviewer's §5).
- **Pipelined GS (Zeno, `port-graphics-r2` `9aed4588 5c845bd0 4c7ecd92 5ffd9387 e44a6030 e2a456ab` → `80c45308 8cc9da9a f276398d f506119c
  303d310a dc706476`; DP-25 (b)):** the split VRAM image — the EE keeps its image, the GS consumer thread its own, the EE's five mutators
  log `VramOp`s that the consumer replays in stream order before each draw (`VramMirror`, `VramOpLog`, `DrawIrThreadQueue::FlushVramOps`); the
  EE's two reads of pipeline-side state (`LocalToLocal` source, the savestate's VRAM section) go through `Sync()`. Build 0/0/0 (626 steps),
  focused 35/35. Identity pinned: `RootGsQueuePipelinedIdentical` (`coldboot/frames.fhash inline==pipelined: identical (200 lines)` + mem/drawir/
  gsops; `Passed 10.57`); the owner's s4 replay 4000 frames `diff_legs: IDENTICAL (lanes=meta,frames,audio,mem)` + `drawir`/`gsops` `cmp`
  identical, the level route 7600 frames `pin=IDENTICAL` (PERF_S4_REVIEW §1, re-run by the reviewer). A consumer-thread fault now surfaces on
  the producer thread (`f276398d`). PERFORMANCE.md §8 carries the round.
- **Shadows in the presented frame (Lagrange, `port-textures` `71783d5e 424e32e5 05d2b1c4 1d8a1696 28834804 5324ba41 2a412197 49b6a581` →
  `3d0af48c cfb89205 b8f7a454 9f8ffb1c 0f4e3491 d357b49f 334086fb 4d53681f`):** s4 bug #1 — the pass accepted everything (`refused=0`) yet the
  PRESENTED frame showed no footprint at the player's `video.scale=2`. Attribution (`4d53681f`, `green3/draw_routes.txt` line 3223 of 3233):
  the composite's target (the scene's `FRAME=070000`), scissor (whole frame), blend (`ALPHA=2021 fix=20` = Cd − Cs·32/128, TR
  `QuarterTakenAway`) and ordering (after the 3186 world draws, before the HUD) all match TR `renderer/shadows.cpp ApplyShadows`; the fault was
  the pass's private half-size images allocated at `frame/2 × scale` while `DrawStripCount` rasterises at the logical half size — at scale 2
  the volumes landed in the top-left quarter and the composite sampled an empty region. Fix `3d0af48c`: the half-size images stay NATIVE at
  any scene scale; only the composite step samples at `scale` (pin `ShadowPixels`: the volume darkens `(90·s, 44·s)`, not `(45·s, 22·s)`, dark
  count ∝ s²). **`RootShadowLevelEntry` now judges the PRESENTED frame** (a readback of the window-sized framebuffer after the display quad —
  `Backend::ArmPresentCapture/ReadPresented`, `p<key>.ppm` beside `f<key>.ppm`; `--frames 7600 --timeout 2400`, TIMEOUT 2500, RUN_SERIAL): RED
  on the frozen `96c8525c` (`f7000 … 133.3 <= ring 144.5 - 22` FAIL; no presented lane), GREEN here: `PASS presented frame p7000 dark footprint
  under Crash: sand between the feet 105.9 <= ring 144.5 - 22 (512x448)`, `p7500 … 106.5`, `f7000/f7500` at 1024x896 the same, `no white
  vertical streak … 0 px` ×4, `RootShadowLevelEntry: ALL PASS` (`Passed 1089.27 sec`, `frames=7600 video_scale=2`).
  **The `empty` passes explained** (s4 report item): `graphics_shadow_empty=frames=6310,one_bracket=6310,many_brackets=0,no_mesh=1710,redirected=0,
  empty_frames=1710,last_published_frame=9203,ranges=1731-3436/4600-4603` — every empty pass is a begin/end bracket with NO accepted mesh
  (`no_mesh`, none `redirected`), and the frames are exactly the title/menu/load stretch BEFORE the level (1731–3436: no shadow-casting
  character in the scene) plus the four frames of the level's ENTRY CUT (4600–4603); `empty == graphics_shadow empty == graphics_shadow_scene
  empty` (1710). Not Crash off-screen, not other characters — frames without a caster.
- **Integrator `ff12dca8 fix(host): Defer off-main window resizes per the VideoStack contract (pipelined GS deadlock)`:** the first game-group
  run on the combined binary had ONE red beyond the frozen-anchor set — **`RootPlayRelaunchRestore` hung** (leg C, `--profile default
  --harness-window … restore_at_frame=100`; no `frames.fhash` growth for 40 s; `allon_s4/relaunch_bisect/`: `gs.queue=inline` completes,
  `pipelined` hangs; `pipelined_hang.sample.txt`): the main thread in `MovieBoot::Restore → GraphicsOutput::SettleOwnedForCapture →
  DrawIrThreadQueue::Sync()` waits for the GS consumer; the consumer in `DrawIrQueueConsumer::OnJob → VideoStack::OnWindowResizedOffMain →
  SessionRuntime::OnMain → Park::Wait` waits for the main thread. The off-main entry had grown a synchronous `main_dispatch_` round trip
  (`1c1a64d1`) against its own stated contract (off-main resizes are DEFERRED to `ApplyPendingWindowResize()` at the frame boundary) — under
  `inline`/lockstep the window job never ran while main waited in `Sync()`, so it never showed. Fix: `OnWindowResizedOffMain` always records
  the pending geometry (counted, coalescing) and never calls into the main thread; `ApplyPendingWindowResize` at slot 0 (now in EVERY gs.queue
  mode — `ConfigureDisplay` and the checkpoint's restore use the off-main entry regardless of a consumer thread) dispatches to main from the
  game's host thread, where main is pumping. A model of the contract: no sleeps, no timeouts. Pins: **`VideoStackDefersOffMainResize`** (new
  unit, visible window: a resize posted from a non-main thread while the test's main thread blocks waiting for it completes with ZERO dispatch
  attempts, `deferred_resizes` counts 1 then 2 (coalesced), the boundary dispatches exactly once, an empty boundary nothing; 10 checks — 9 RED
  on the old function) and `RootPlayRelaunchRestore` (`Passed 40.23` alone, `46.79` in the group — was the hang). Release 0/0/0; app/gs focused
  155/155.

**Binary (frozen):** `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.ff12dca8` — `PS2RECOMP-BUILD-ID 2026-10-10 14:53:26`, sha256
`409dbfdaa608ce3f009cc025014b34b591750e965bffbfa4770ed19318603d25` (`= build-release/app/host/nativeee`, `cmp` clean, `BIN_FROZEN_OK`; sha
unchanged pre-suite → game group → merge → freeze). `nativeee_release.96c8525c` (previous) kept; `nativeee_release.dd9b20b0` DELETED for disk
(`bin/DELETED.txt`; its `.sha256`/`BUILD-ID` records stay).

**Suite (binary `409dbfda…`):** component suite ALONE `-j8 --timeout 900 -E '^(Root|GsCorpus|Render_AttractReplay|Render_TitleFrame|GsReplay)'` →
**`100% tests passed, 0 tests failed out of 606`** (253.20 s; 22 `***Skipped` = the previous set, `SKIPSET_SAME`). Game-process group `-j1` (Debug
`nativeee` rebuilt first, 0/0/0) → **`80% tests passed, 18 tests failed out of 91`** (+1 disabled `GsCorpus_slowspot`), `Total Test time (real) =
1985.59 sec` (`allon_s4/fix/ctest-root.log`, `ctest-root.LastTest.log`). The 18 reds = **exactly the frozen-anchor set of `96c8525c`, by name**
(`fix/red_list.txt` vs `allon_saves/red_names.txt`: `RED_SET_SAME_AS_96c8525c`): `RootRegistersVuVifGifSections`, `RootBindsDmacSyncSeams`,
`RootRegistersIopmSection`, `RootRestoresStreamWorker`, `RootRestoresStreamHeartbeat`, `RootRoutesGsPageAndDrawIr`, `RootVideoStackPresents`,
`RootVu1LibraryNone`, `RootFramesKeyConvention`, `RootAnchorGsRestored`, `RootAnchorPadReady`, `RootAnchorF57292`, `RootAnchorF59991`,
`RootAnchorHeartbeat`, `RootAnchorHeartbeatAtRestore`, `RootColdbootProbeResumes`, `RootReleaseLanesIdentical` / `RootGsQueueThreadIdentical` (the
artdet half `run.meta frames=0`) — each anchor class re-run by hand on this binary (`fix/root_fail_logs/anchor_refusals.txt`): `--load-state
…/{artdet_f37152, ps2state_slot0_f57292, ps2state_slot0_f59991, slowspot_f40298_s1789693059}.ps2st: Restore needs owned logical clock state.`
RECORDED, not weakened (GCLK). **0 other reds.** Passed: `RootGraphicsBridgeAllOn 15.98`, `RootPlayModeSmoke 6.28`, `RootTitleMusicPlays 20.47`,
`RootShadowLevelEntry 1089.27`, `RootMenuSfxAudible 51.28`, `RootPlayWallLock 8.39`, `RootPlayFmvPresents 24.68`, `RootMovieLifecyclePlay 24.88`,
`RootMtvuPlaySmoke 6.31`, `RootCdvdPollVisibility 86.12`, `RootGsQueuePipelinedIdentical 10.57`, `RootPlaySaveAtTitle 35.79`, `RootPlaySaveRestore
8.67`, `RootPlayRelaunchRestore 46.79` (leg B `savestate F8 restore Relaunch: …`, `R(<stamp>) restored_vsync_frame=60`, `RootPlayRelaunchRestore: PASS`).
Re-test `-j1` on `main` after the merge (`fix/ctest-retest.log`; binary `cmp`-identical to the frozen copy): RootPlayModeSmoke, RootGraphicsBridgeAllOn,
RootPlaySaveAtTitle, RootPlayRelaunchRestore, RootGsQueuePipelinedIdentical, RootShadowLevelEntry → **`100% tests passed, 0 tests failed out of
6`** (`RootGraphicsBridgeAllOn 26.74`, `RootPlayModeSmoke 6.34`, `RootShadowLevelEntry 1113.65`, `RootGsQueuePipelinedIdentical 10.60`,
`RootPlaySaveAtTitle 36.87`, `RootPlayRelaunchRestore 46.81`; 1241.05 s). Lints + settings table + Verify*/Retirement* with the docs edits in the
tree: `100% tests passed, 0 tests failed out of 40` (`fix/ctest-lints-docs.log`).

**Performance (s4 bug #2) — the pipelined GS, before → after.** The owner's idle A/B (`graphics/perf_s4/S4_IDLE_TABLE.md`; on battery, Low Power Mode,
so absolute numbers throttled, the A/B fair; s4 padlog 4000 frames): deterministic ms/frame overall `28.57/28.23 → 25.21/24.87` (lower in EVERY
300-frame window), play ms/frame `66.78/31.69/33.71 → 28.98/28.98/28.89`, `pace_late 3829/3626/3630 → 2676/2723/2521`, `underruns 9451/2905/4264 →
1777/1967/1871`. The integrator's 2000-frame wall-paced play replay of `play_sessions/s3/session.padlog` (`verify/bench/run_s3.py --play`;
`allon_s4/fix/play2000-*`; one game process; before = frozen `96c8525c` twice at load 7.75 / 3.75, after = this binary at load 4.76):
**`pace_late 1800/1822 → 1652`, `underruns 2063/2038 → 1155`**, ms/frame overall `34.65/34.80 → 32.23` (windows 600–1200: `21.9/23.6 → 19.1/18.7`;
1200–1800: `29.2/29.9 → 27.0/27.3`), wall `69.9/70.2 → 65.1 s`; after `gs_queue=mode=thread,policy=pipelined,enqueued=643566,consumed=643566,
frames=3156,frame_waits=2000,max_depth=3146,vram_ops=296475,split_image=1,deferred_resizes=1` (the one deferred OS resize = the first DISPLAY,
applied at the boundary). The absolute gate (`pace_late → 0`) is NOT reached; the reviewer's observations stand: play-mode audio `dropped` rises
under pipelined (`5184 → 51456` here; the deterministic audio lane is identical, so pacing, not the GS stage — the audio-pacing owner's item on mains);
`gs_xfer local_local=0` on every leg (the `SetLocalToLocalSource` drain and the savestate VRAM read are inline-equivalent by reading but unpinned).
`--set gs.queue=inline` remains the override.

**Kick-unsupported (s4 item #3) — attributed, NOT a missing family.** Census build `07:22:07`, the s4 padlog deterministic replay 7400 frames
(`math/kick_unsupported_s4/leg/run.meta`): `graphics_kick_lowering=submitted=2949038,queued=2382260,lowered=2382260,raw=566778,unsupported=566778`
(19.2 %; the s4 session's worker row 975729/5367310 = 18.2 %) and `graphics_kick_lowering_unsupported_by_program=0x1266e845aa7b25c5:vs_poscolor_strip
@0x0000:packed:e:516600, 0x4bba921c0ffde926:vs_textured_tristrip@0x0000:packed:e:49222, 0x342ea759820ad9d0:vs_zero_kick_a_cont@0x1750:packed:
215215215215:528, …` (7 rows, sum = 566,778). **99.9 % (`packed:e`) is the material's A+D GS register block** — the single-tag PACKED A+D
packet a multi-shader material's pass hands through VU1 before its geometry (TR `renderer.h:121-128 AddressDataTag`, `shaders.cpp:85-110
WriteRegistersToVu`, `materials.cpp:94-108 WriteShaders` for `shaderCount >= 2`; `PlainWrites=7`/`TexturedWrites=9`) — the program family IS in the
library and its geometry kick IS lowered; only the lowering's shape bound declines the state packet and hands it to the raw GIF parser, which
applies the same register writes (same sink: correctness unaffected, the cost is the raw path on the MTVU worker). 0.09 % is the decal strip's
12-descriptor tag (`4 × {ST,RGBAQ,XYZ2}`, TR `game/decals.cpp:30-32`). **No HLE_COVERAGE §2 family ran unregistered** (`vu1_hle_worker=2998945/0/10`,
`vu1_hle=1984424/0/10` → `misses=0` on both routes). **Next M8 proposal (Hypatia, review APPROVED): slice A "state-packet lowering"** — accept
`flg==Packed && nregs==1 && regs[0]==0xE && eop` in `KickLowering`, deliver `BeginTag → (PRE: PRIM) → WriteRegister(addr&0x7F, data) per loop → EndOfPacket`
(the parser's own A+D dispatch calls), behind `hle.kick_state_lowering` (default off until flipped); counters `lowered_state == former packed:e sum`,
the `_unsupported_by_program` row loses every `packed:e`; self-oracle pin `Raw()` vs `Lower()` same `RegisterOpStream` (7/9/9+N writes, PRE on/off,
interleaved with geometry) + a `Vu1HleMechanism` corpus case with `node.w=1` asserting `unsupported==0`; leg identity drawir/gsops on-vs-off. Slice B
(0.09 %): the `({2,1,5}|{2,1,4})×k` descriptor pattern. The level's per-program split still needs the wall-paced play replay reading the `_worker_` row.

**HAZARD — the s4 recording (`play_sessions/s4/session.padlog`) does NOT replay deterministically** (Lagrange §6, reviewer-confirmed): under the
deterministic fields with the user's card copied to `paths.mc_dir`, the replay sits on the load-file menu with EMPTY slots from frame ~3000
(`graphics_shadow_mesh=calls=0`, `graphics_shadow_empty ranges=1731-7484`; a fresh card diverges earlier at "no save data"); the kick census replay
diverges at frame 83 onto the title/menu/cutscene route (`load_model=1602` vs the session's 450373, `graphics_shadow instances=0`). The user's
session was wall-paced (`pace_late=2070`, `tick_thread ticks=8994` for 3993 frames), so the padlog's indices are the INPUT clock's and the
deterministic boot reaches the title ~440 frames later (first bracket 1731 vs ~1290). Also open: why the copied card lists EMPTY (`mc_bound=0`,
`save_storage … committed=0` in both sessions). For Bernoulli (inputs/saves): a padlog replay needs the recording's input clock, or the recorder
writes tick-indexed entries the deterministic replay maps 1:1. Until then the s3 recording is the route of record for the first level
(RootShadowLevelEntry, the perf replays above); the s4 numbers quoted here are the owners' wall-paced or title-route figures, as labelled.

**Play command:** INTEGRATION_STATUS.md "All-on build, no exceptions" with `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.ff12dca8`
in place of `nativeee_release` — unchanged otherwise; no `--set … =off` needed. Expected on the first level at scale 2: the footprint under
Crash in the window, `graphics_shadow … refused=0`, `graphics_shadow_empty=…,no_mesh=E,redirected=0,empty_frames=E` with `E == graphics_shadow
empty`, `gs_queue=mode=thread,policy=pipelined,…,split_image=1,deferred_resizes=1`.

**Still open (named):** the level's pace (`pace_late` 1652/2000 on the s3 route; PERFORMANCE.md §8 — the next lever is the kick state-packet
lowering above and the idle-machine table on mains); the 18 frozen-anchor refusals (GCLK); the s4 padlog replay hazard (Bernoulli); the restored
first field's omitted owned groups (INTERFACES §13); play-mode audio `dropped` under pipelined; the unpinned `LocalToLocal`/VRAM-section drains; the
sky `missing_resource` class; the sprite selection scope; the 6 movie-transition model Sends.

### 2026-10-10 23:3x — DT-14 clock contract integrated: the game at 60 Hz wall time, the renderer never blocks it, audio never drops; F5 reaches SessionControls from the game window

Integration `integration-20261011-clock` from `8bd76e91`, `git merge --ff-only` into `main` → code HEAD **`dc1a8a3a`** (21 commits; record
`int_work/m_series/cutover/allon_clock/SECTION.md`; the decision `docs/phases/DECISIONS_LOG.md` DT-14). Two owner stages, two independent
reviews, both **APPROVED** (`graphics/CLOCK_CONTRACT_REVIEW.md` review_clock2 22:16; `input_saves/F5_ROUTING_REVIEW.md` review_f5b 21:20).
Cherry-picked `-x` (Zeno first, then Bernoulli): `git cherry` 14 × `-` (Zeno) and 4 × `-` + 1 context-only `+` (Bernoulli's `1b9245dc`,
`allon_clock/range-diff-1b9245dc.txt`: one context line Zeno added above the hunk). **Keys: 27 on, 0 off, unchanged**
(`gen_settings_table --check` → `reproducible (101 rows) -> PASS`; the one new key is `harness.inject_key`, `--set` only). Every line below
quotes a result that was read.
- **Clock contract (Zeno, `port-graphics-r2` `60c03cf3 adff262f 8219c774 7322a435 0e58341c 16b6dfe3 4c0f9600 32645b27 804a79a8 6afa4f1f
  274acf43 e27ed635 4226d457 1210aef2` → the same subjects on `main`):** rule 1 — `TickThread` edges at exactly 1/60 s (rational), the
  wall-locked `BoundarySequencer` fires every noted tick 1:1, no surplus tick (`pace_surplus` row RETIRED → `pace_late_fields=`,
  `pace_slept_ms=`); rule 2 — frame skip (ONE pending frame, the older dropped: `frames_dropped=`), no `WaitFrame` under the wall lock, the
  bridge drain returns without `Sync()` in play (`graphics_bridge_drain=stream_ordered=,waited=`), owned deliveries async on the consumer
  (`graphics_owned_async=`), the local→local VRAM source served EE-side (`gs_queue_l2l=`); rule 3 — a late boundary's extra fields reach the
  title's T0 (pinned); rule 4 — `AudioPullRing` pad-with-tail (`padded=,relatches=,trimmed=,pad_with_tail=1`); rule 5 — `game_hz=`, `field_hz=`,
  `render_fps=` series per 1 s wall window (attributed by the first field's wall edge), the F1 line `game N Hz | render M fps`; the harness
  mem lane OFF in play (`mem_lane=off`; it was a 34 MB FNV on the game thread every 60 boundaries), `pace_late_trace=ordinal:work_us:fields`
  names the EE's own long frames. Pins `RootClockContract` (+Selftest), `PacerReleaseSequencer`, `DrawIrThreadQueueFrameSkip`,
  `DrawIrThreadQueueLocalToLocalSource`, `AudioPullRing`, `ClockCensus`. Integrator fix `6a3a3bb4`: `SetClockReadout` body out of
  `session_runtime.h` (`LintOneClass` was red on the pick: `session_runtime.h:91: non-trivial function definition in a header`).
- **F5 routing (Bernoulli, `port-product` `4ae95ea2 1b9245dc 2bdbdb8a 303c75a5 5c2de09b`):** the s5 "F5 does nothing" — a press SDL stamped
  `windowID=0` (no keyboard-focus window when macOS does not activate the harness-launched process) was discarded by `SessionGui::ProcessEvent`
  before `HandleFunctionKey`. Now a press is ours unless SDL names a window that is not ours; `SessionWindow::Open` requests focus; an
  unconsumed hotkey prints `nativeee: F<n> discarded: SDL stamped window id U (session window W)`; run.meta `hotkeys=key_events=,function_keys=,
  discarded=`; `harness.inject_key=F5@N` pushes a REAL `SDL_KEYDOWN/KEYUP` into SDL's queue. Pins `SessionHotkeyRoute` (deterministic red on the
  old router), `RootPlayHotkeyF5` (+Selftest). Integrator fix `dc1a8a3a`: the `[sdl-trace]` attribution trace is armed by `--set
  dev.log_level=narrate`, not a second getenv (`CoreSingleGetenvSite` was red: `app/host/session_runtime.cpp:194: getenv` / `getenv sites: 2 ->
  FAIL`); `trace_probe/run.log`: 12 `[sdl-trace]` lines, `savestate F5 save Completed … (frame 101…)`, `hotkeys=key_events=2,function_keys=1,discarded=0`.

**Binary (frozen):** `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.dc1a8a3a`, `PS2RECOMP-BUILD-ID 2026-10-10 23:21:02`, sha256
`ee57499e0dd80e608ab1b891eb56171b9170a3258a317e278ac02984a15acc4a` (`cmp` clean, `BIN_FROZEN_OK`; `.ff12dca8` kept; `.96c8525c` deleted for disk,
`bin/DELETED.txt`). Release: per-step builds `FAILED=0 warning:=0 ld_warning=0` (865 / 29 / 503 / 29 steps) and the full rebuild with every TU
touched (`build-release-final.log`: **1833 steps, 0/0/0**, 6m34).

**Suite (binary `a9664e6f…` = the frozen code before the build-id relink):** component suite ALONE `ctest --test-dir build-release -j8 --timeout 900
-E '^(Root|GsCorpus|Render_AttractReplay|Render_TitleFrame|GsReplay)'` → **`100% tests passed, 0 tests failed out of 610`** (121.43 s; 22 skips =
the previous set, `SKIPSET_SAME`). Game-process group `-j1` (Debug `nativeee` rebuilt first, `build-debug-nativeee.log` 0/0/0, 237 steps) →
**`81% tests passed, 18 tests failed out of 95`** (1355.80 s; +1 disabled `GsCorpus_slowspot`). The 18 reds (`allon_clock/red_names.txt`) = the
frozen-anchor GCLK set of `ff12dca8` BY NAME (`RED_SET_SAME_AS_ff12dca8`): RootRegistersVuVifGifSections, RootBindsDmacSyncSeams,
RootRegistersIopmSection, RootRestoresStreamWorker, RootRestoresStreamHeartbeat, RootRoutesGsPageAndDrawIr, RootVideoStackPresents,
RootVu1LibraryNone, RootFramesKeyConvention, RootAnchorGsRestored, RootAnchorPadReady, RootAnchorF57292, RootAnchorF59991, RootAnchorHeartbeat,
RootAnchorHeartbeatAtRestore, RootReleaseLanesIdentical, RootGsQueueThreadIdentical, RootColdbootProbeResumes — each class re-run by hand
(`root_fail_logs/anchor_refusals.txt`): `nativeee: --load-state …/{artdet_f37152, ps2state_slot0_f57292, ps2state_slot0_f59991,
slowspot_f40298_s1789693059}.ps2st: Restore needs owned logical clock state.` RECORDED, not weakened (GCLK). **0 other reds.** Passed:
`RootClockContract 47.11`, `RootPlayHotkeyF5 28.33`, `RootGraphicsBridgeAllOn 15.94`, `RootPlayModeSmoke 6.22`, `RootShadowLevelEntry 581.25`,
`RootPlayWallLock 8.22`, `RootMovieLifecyclePlay 24.72`, `RootGsQueuePipelinedIdentical 10.40`, `RootPlaySaveAtTitle 18.83`, `RootPlaySaveRestore
5.39`, `RootPlayRelaunchRestore 39.94`, `RootTitleMusicPlays`, `RootMenuSfxAudible`, `RootPlayFmvPresents`, `RootMtvuPlaySmoke`, `RootCdvdPollVisibility`.
Re-test `-j1` on `main` after the merge AND again on the frozen bytes (`ctest-retest.log`, `ctest-retest-frozen.log`; `STILL_IDENTICAL_TO_FROZEN`):
RootClockContract, RootPlayHotkeyF5, RootPlayModeSmoke, RootGraphicsBridgeAllOn, RootPlaySaveAtTitle, RootPlayRelaunchRestore,
RootGsQueuePipelinedIdentical → **`100% tests passed, 0 tests failed out of 7`** twice (168.29 / 168.13 s); RootClockContract prints `PASS DONE band:
game_hz 59.5..60.5 in every in-loop window without an EE-own-time frame (out of band: none)`, `EE_OWN_TIME windows … = 1 <= 3: ['game_hz=59
field_hz=60']`, `RootClockContract: ALL PASS`.

**DT-14 acceptance on the integrated Release (idle machine `load_1m` 1.62 / 2.42, `other_game_procs=0`; `verify/bench/run_s3.py --play --frames 3000`,
`--harness-window --inputs play_sessions/s{3,5}/session.padlog`; `allon_clock/s{3,5}_play/leg/run.meta`, summaries `s{3,5}_play.summary.txt` with the
pin's in-loop definition = the interior of a ≥30-boundary stretch):**

| replay | frame_waits / syncs / ordered_waits / sync_wait_ms | bridge waited | frames_dropped | audio dropped / underruns | tick == pacer | game_hz ALL windows min/max | in-loop windows min/max (histogram) | field_hz in-loop | render_fps in-loop | pace_late (trace) |
|---|---|---|---|---|---|---|---|---|---|---|
| **s3** (3000 f, wall 107.3 s) | **0 / 0 / 0 / 0** | 0 (`stream_ordered=10671`) | 3 | **0 / 0** (`pushed=5142400 pulled=5138432 padded=6912 relatches=6 trimmed=8576`) | 6419 == 6419, `resyncs=0` | **0 / 60** (106 windows; zeros = `fmv_window=399-2972` + the level load) | **59 / 60** (44 windows: 60→42, 59→2) | 60 / 60 | 55–61 | 31 (`1248:29972:2, 1885:43965:2`, the FMV start/playback `2934–2999` in the trailing partial window) |
| **s5** (3000 f, wall 70.1 s) | **0 / 0 / 0 / 0** | 0 (`stream_ordered=8161`) | 2 | **0 / 0** (`pushed=3351200 pulled=3349504 padded=5280 relatches=4 trimmed=4288`) | 4182 == 4182, `resyncs=0` | **0 / 60** (69 windows) | **58 / 60** (43 windows: 60→41, 59→1, 58→1) | 60 / 60 | 57–60 | 10 (`1248:30298:2, 1352:56026:3`) |
| s5 on `ff12dca8` (the user's session) | 4963 frame_waits | — | 0 | 344896 / 1060 | — | — | — | — | — | 2366 |

Rules 1, 2, 4, 5 hold whole on both replays (every renderer-wait row 0; `dropped=0 underruns=0`; `domain tick == pacer ticks`, `absorbed=0`;
`field_hz` exactly 60 in every in-loop window — the game's clock advanced at wall speed in every second: no slow motion, no pitch change).
The literal "59.5–60.5 in EVERY window" is met in 42/44 (s3) and 41/43 (s5) in-loop windows; each miss is one boundary whose EE/host OWN frame
work exceeded a period with `sync_wait_ms=0` (not the renderer): the systematic **boundary 1248** (~30 ms on s3, s5 and the pin; `footprint_series`
+49 MB b1200→b1260, no loader request; owner unknown) and one sporadic 44 / 56 ms frame per replay (s3 1885; s5 1352, 3 fields). These are perf
attribution items (the carve-out `RootClockContract` tolerates, ≤3 own-time windows per fixture replay) — recorded in DT-14 for the user's
decision. **Deterministic profile unchanged:** 1300-frame deterministic coldboot, integrated binary vs the frozen `ff12dca8` (main's code), same
cwd → `frames.fhash: identical by key (1300)`, `audio.ahash: identical (217 lines)`, `mem.mhash: identical (22 lines)`, **`diff_legs: IDENTICAL`**
(`allon_clock/diff_legs_main_vs_integrated.txt`; the deterministic leg keeps every wait: `frame_waits=1300 frame_skip=0`, `mem_lane=on`). Note for
anyone diffing against the owners' `det_*` legs: they were run from the graphics worktree, whose untracked `memcards/` card differs from the main
repo's → the mem lane differs at b60 by card content, not code (`allon_clock/mem_attrib/`: Zeno's own binary prints both hashes depending on cwd;
frames/audio identical either way).

**Play command:** INTEGRATION_STATUS.md "All-on build, no exceptions" with `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.dc1a8a3a`
in place of `nativeee_release` — unchanged otherwise. New in the session: F1 shows `game N Hz | render M fps`; F5 saves from the game window
(`nativeee: savestate F5 save Queued/Completed` on stderr; a lost press prints `F5 discarded: SDL stamped window id …`); the exit census carries
`game_hz=`, `field_hz=`, `render_fps=` (one value per wall second), `frames_dropped=`, `pace_late_trace=`, `hotkeys=`, `mem_lane=off`,
`graphics_bridge_drain=`, `graphics_owned_async=`, `gs_queue … frame_skip=1,sync_wait_ms=`. Expected on the first level: `frame_waits=0`,
`sync_wait_ms=0`, `audio_ring … dropped=0,underruns=0`, `game_hz` 60 in the in-loop windows (58–59 only where `pace_late_trace` names a long EE
frame), `render_fps` whatever the machine does. Attribution trace for a key: `--set dev.log_level=narrate` (`[sdl-trace]` lines).

**Still open (named):** the LIVE play run (the user's; DONE's third leg) and a physical-key F5/F8/F1 spot check; the boundary-1248 own-time frame
and the sporadic 44–61 ms EE frames (loader bursts on the EE thread, movie decode on the EE at FMV start) — perf attribution, owner to be named;
the reviewer's non-blocking notes (the `EE_OWN_TIME` exemption's width — suggest `game_hz ≥ 57`; the presenter-roll census straddle; the
`ServeLocalToLocalSource`/`PublishSnapshot` mutex over a 4 MB memcpy, never armed on this title); then M8 slice A (the kick state-packet lowering).

## Standing charter (2026-10-11 →)

Integrator record `int_work/m_series/cutover/allon_w1/SECTION.md` (every number below is read from a file there). Rules: CUTOVER.md
"STANDING CHARTER" — INVARIANT per stage = Release 0/0/0 + ctest green + the DT-14 clock pins; a stage that regresses is REVERTED,
not tuned; deterministic lanes identical unless attributed. Wave 1: five stages reviewed independently; the four APPROVED ones
integrated via `integration-20261011-w1` from `main 16da6dd1`, `cherry-pick -x` in the order services → product → math (→ graphics skipped
→ textures reverted), `git merge --ff-only` → `main` code HEAD **`1bb648b5`** (12 commits = 3 Herschel + 5 Bernoulli + 4 Socrates; `git cherry`
12 × `-` by patch-id; no integrator fix on the merged set). **Keys: 28 `hle.*` on** (`gen_settings_table --check` → `reproducible (102 rows) ->
PASS`): the one new key `hle.startup=native` (the deterministic profile pins `retained` — its fingerprint grew that entry), `graphics_strips=all`.

### 2026-10-11 03:5x — wave 1: service-only startup, frame-keyed recorder, native kick lowering (unsupported → 0); screen effects REVERTED; 1c not approved

| track item | owner / branch (commits on `main`) | status | evidence (read) |
|---|---|---|---|
| **3a** M11 service-only startup — STREAM.IRX/CDVDSTM/LIBSD/SDRDRV + IOP heap not loaded, the IOP never boots in play | Herschel, `port-services-r3` `29a0a9d7 56235334 07f44f9f` → `b07c419d be7138d3 f21cec4f` | **INTEGRATED** (`stream/TRACK3A_REVIEW.md` APPROVED) | s3/s5 play replays `startup=native iop_boot=0 iop_modules= iop_hb_beats=0 stream_rpc_calls=0/0 libsd_calls= iop_heap_calls=allocs=0,…`, `service_startup=stream=native,sound=native,retained=none,consumers=0`, `stream_transport=…unowned=0,malformed=0,server_calls=12311` (s3) / `12310` (s5) `== batches`; `RootServiceOnlyStartup: ALL PASS` (28.54 s); deterministic 1300 f pins `retained` and is IDENTICAL to `dc1a8a3a` (below); **peak_mb s5 609 → 481** |
| **Bernoulli** product rebase + recorder determinism (s4/s5 hazard) + F8-live design | Bernoulli, `port-product` `78ec3cee 1218b91b 49351450 b3cadc85 330cbc75` → `e64d78f7 da635408 3abd797d 7f8070e0 dac37eda` | **INTEGRATED** (`input_saves/TRACK_PRODUCT_1_REVIEW.md` APPROVED) | the recording keyed by the game frame (`# padlog … clock=frame`, run.meta `pad_record_clock=frame`; absent header = tick clock, every earlier padlog replays as before); `RootPlayRecordReplay: ALL PASS` (43.16 s: `frames.fhash: identical by key (600 keys paired)`, `audio.ahash: identical (100 lines)`); `docs/design/F8_LIVE_RESTORE.md` (design only). s4/s5 themselves stay tick-clock recordings (title route under deterministic) — a frame-clock session is the next workload |
| **1a** Socrates: native `vs_skinned_lit_strip` (7 hashes) + `vs_poscolor_strip` (4) via the VU1 replay oracle; **1b** `vs_textured_tristrip`, `vs_lit_tristrip`, `vs_zero_kick_cont/_a_cont` | Socrates, `port-math` `790331aa 29d805b4 0129537a 7441b7c0` → `754e0190 1a37a495 3671104f 1bb648b5` | **INTEGRATED** (`math/TRACK1A_REVIEW.md` APPROVED) | `KickLowering: 510 checks, 0 failures`; `LOWERING-PARITY cases=125 kicks=552 lowered=552 … unsupported=0 parity_failures=0`; `GIF PARSER EQUIVALENCE PASS`; s3 play `graphics_kick_lowering_worker=submitted=1033454,queued=1033454,lowered=1030930,…,raw=0,unsupported=0` (was `raw=209833,unsupported=209833` on `dc1a8a3a`), s5 `…submitted=1266257,…,raw=0,unsupported=0` (was 215674); `_unsupported_by_program=none` both; deterministic lanes IDENTICAL |
| **1c** Zeno: retire the DMAC→VIF→GIF parse in play, `gs_sink` → 0 | Zeno, `port-graphics-r2` `a2354b83 1c1d3a20 e31ce15d b676d3bd 65efedee` | **NOT INTEGRATED — review NEEDS_CHANGES** (`graphics/TRACK1C_REVIEW.md`): code sound, charter criterion unmet | the stage retires the PATH1 byte parse (`gif_parse path1` 211668 → 92 receives) but every play replay reads `gif_parse_executed=1`, `gs_sink=tags=1362275,…,kicks=30198173,image_qw=56864448` (s5), PATH2 943.6 MB/3000 f (DIRECT texture uploads, sky/sprite model draws, per-bucket state). Parent decision pending: re-scope 1c to "PATH1 byte parse retired + census" and integrate as-is, or hold on the PATH2 owners (textures-at-load, 2a sky, 2b sprites, M8 buckets). On `main` now: s3 `gs_sink=tags=1266676,…,kicks=27937250`, s5 `tags=1551221,…,kicks=34519218` |
| **2c** Lagrange: owned screen effects (blur/filter/wave) with a presented-frame pin | Lagrange, `port-textures` `70dd8d91 0e2421a8 2b4d3fac b2842309 1be888e7 429fbe76 0dfdb622` (review `graphics/LAGRANGE_TRACK2C_REVIEW.md` APPROVED) | **REVERTED from the branch — `RootPlaySaveAtTitle` red** (the invariant F5 capture); state preserved at `backup/integration-20261011-w1-with-2c` (`90192a88`, incl. the integrator's `CoreSingleGetenvSite` fix for `tests/title/effects/test_screen_pixels.cpp:154: getenv`) | game group with 2c in (`ctest-root-with-2c.log`): `81% tests passed, 19 tests failed out of 101` = the 18 GCLK + **`RootPlaySaveAtTitle`**: `savestate=refused,No admissible boundary in 120 consecutive boundaries; the last owner to refuse: graphics_screen: screen effects boundary is executing or a reserved frame is undelivered`; `graphics_owned_output=…pending=5`, `graphics_screen_effects=calls=1350,…,replaced=1350,filter_off=1176`. Cause (read): `ScreenPass::CanCheckpoint() { return active_ == 0 && pending_->load() == 0; }` lacks the `unlinked_recording_open` arm the sibling `ShadowPass`/`DistortionPass::CanCheckpoint(bool)` carry (a Ready-but-undelivered reserved batch is admissible under the capture settle); in play with the pipelined queue a reserved frame is always pending at the boundary, so F5 never admits. Without 2c the same pin reads `RootPlaySaveAtTitle: PASS (trigger save_at_frame, boundary 1300, unlinked items 4)` 19.84 s. Also for the owner: the 2c pin set did not include `RootPlaySaveAtTitle`/`RootPlayRelaunchRestore`; the config pick collided with 3a on `test_config.cpp` (`count_on` 27→29, soft-backend notes 14, `notes[8]` strips) and the generated settings table — the resolution is in the backup branch (`range-diff-b2842309.txt`). `graphics_screen_selection=model` stays (s3 `model_calls=2926`) |

**Build / suite (binary `0b1681b3…`, BUILD-ID `2026-10-11 03:08:39` = the frozen bytes):** per-step Release builds `FAILED=0 warning:=0
ld_warning=0` (526 / 369 / 148 / 615 steps; the 615 and the 6-step getenv fix were the reverted 2c) and the full `--clean-first` rebuild of the
merged set **1889 steps, 0/0/0** (6m07). Component suite ALONE `ctest --test-dir build-release -j8 --timeout 900 -E '^(Root|GsCorpus|
Render_AttractReplay|Render_TitleFrame|GsReplay)'` → **`100% tests passed, 0 tests failed out of 611`** (127.44 s; 22 skips `SKIPSET_SAME`). Game
group `-j1` (Debug `nativeee` rebuilt first, 0/0/0) → **`82% tests passed, 18 tests failed out of 99`** (1463.36 s; +1 disabled
`GsCorpus_slowspot`); the 18 = the frozen-anchor GCLK set of `dc1a8a3a` BY NAME (`RED_SET_SAME_AS_dc1a8a3a`; `root_fail_logs/anchor_refusals.txt`:
`nativeee: --load-state …/ps2state_slot0_f57292.ps2st: Restore needs owned logical clock state.`) — RECORDED, not weakened; **0 other reds**.
Passed: `RootClockContract 47.08` (`ALL PASS`: `PASS DONE band … (out of band: none)`, `EE_OWN_TIME windows … = 2 <= 3`, `frame_waits=0`,
`dropped=0`, `underruns=0`), `RootGraphicsBridgeAllOn 15.79`, `RootShadowLevelEntry 585.86` (`ALL PASS`, `PASS presented frame p7000 dark footprint
under Crash: sand between the feet 105.8 <= ring 144.5 - 22`), `RootPlaySaveAtTitle 19.84`, `RootPlayRelaunchRestore 40.04`, `RootPlayHotkeyF5 28.37`,
`RootGsQueuePipelinedIdentical 10.40`, the new `RootServiceOnlyStartup 28.54` and `RootPlayRecordReplay 43.16`, `RootPlayModeSmoke 6.24`,
`RootMovieLifecyclePlay 24.77`, `RootPlayWallLock 6.19`, `RootTitleMusicPlays`, `RootMenuSfxAudible`, `RootPlayFmvPresents`, `RootMtvuPlaySmoke`,
`RootPlaySaveRestore`. All on an idle machine, one game process (`other_game_procs=0`).

**DT-14 on the integrated Release (`verify/bench/run_s3.py --play --frames 3000`, `--harness-window --inputs play_sessions/s{3,5}/session.padlog`,
`load_1m` 3.67 / 3.16, `other_game_procs=0`; `allon_w1/s{3,5}_play/leg/run.meta`, `s{3,5}_play.summary.txt`, `s{3,5}_play.fps_windows.txt`):**

| replay | frame_waits / syncs / ordered_waits / sync_wait_ms | bridge waited | frames_dropped | audio dropped / underruns | tick == pacer | in-loop game_hz min/max (histogram) | field_hz | render_fps in-loop p50 / p95 (min/max) | pace_late (trace) | unsupported kicks | peak_mb | service_startup |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **s3** (3000 f, wall 107.5 s) | **0 / 0 / 0 / 0** | 0 (`stream_ordered=10675`) | 3 | **0 / 0** (`padded=11712 relatches=8 trimmed=10016`) | domain 6423 / pacer 6424 (`absorbed=0 resyncs=0`; one edge after the last boundary at the frames cap) | **57 / 60** (44 w: 60→43, 57→1 at w39 = own-time, `1178:53289:3, 1179:37121:2`) | 60 / 60 | **60.0 / 60.2** (55–61) | 38 | **0** (was 209833) | 602 (was 597) | `stream=native,sound=native,retained=none,consumers=0` |
| **s5** (3000 f, wall 70.2 s) | **0 / 0 / 0 / 0** | 0 (`stream_ordered=8162`) | 3 | **0 / 0** (`padded=9248 relatches=4 trimmed=4800`) | 4185 == 4185 (`absorbed=0 resyncs=0`) | **57 / 60** (44 w: 60→42, 58→1, 57→1; w39/w43 own-time, `1178:45510:2, 1179:39846:3, 1423:55355:3`) | 60 / 60 | **60.0 / 60.0** (53–60) | 14 | **0** (was 215674) | **481** (was 609) | same |

Rules 1, 2, 4, 5 hold whole (every renderer-wait row 0, `dropped=0 underruns=0`, `domain tick == pacer`, `field_hz` 60 in every in-loop
window). The literal 59.5–60.5 band is met in 43/44 (s3) and 42/44 (s5) in-loop windows; each miss is the EE's own frame time with
`sync_wait_ms=0` — boundaries **1178/1179** (two consecutive 37–53 ms EE frames, both replays; the former systematic 1248 has moved with the
route) and s5's sporadic 1423 (55 ms) — the DT-14 carve-out `RootClockContract` tolerates (≤ 3 own-time windows), still a perf-attribution
item with no owner. **Deterministic profile unchanged:** 1300-frame coldboot, integrated vs the frozen `dc1a8a3a`, same cwd → `frames.fhash:
identical by key (1300 keys paired)`, `audio.ahash: identical (217 lines)`, `mem.mhash: identical (22 lines)`, **`diff_legs: IDENTICAL`**
(`--allow-set-difference hle.startup --allow-build-diff`: the env_hash differs by the new deterministic pin only); `drawir.ohash`/`gsops.ohash`
byte-identical by `cmp` (`allon_w1/diff_legs_main_vs_integrated.txt`).

**Binary (frozen):** `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.1bb648b5`, `PS2RECOMP-BUILD-ID 2026-10-11 03:08:39`, sha256
`0b1681b36f7e9cfd314a193c05edfee1bd3c026133a071face122c74df002828` (`cmp` clean, `BIN_FROZEN_OK`; `.dc1a8a3a` kept; `.ff12dca8` deleted for disk,
`bin/DELETED.txt`). **Play command:** "All-on build, no exceptions" with `nativeee_release.1bb648b5` in place of `nativeee_release` — unchanged
otherwise. New in the exit census: `startup=native iop_boot=0 iop_modules= iop_hb_beats=0`, `service_startup=stream=native,sound=native,…`,
`stream_transport=…unowned=0`, `graphics_kick_lowering_worker … lowered_state= lowered_packed= raw=0 unsupported=0`, `pad_record_clock=frame`
(and `# padlog … clock=frame` in a recorded session). `--set hle.startup=retained` boots the IOP as before (the oracle arm).

**Still open (named):** 1c's parent decision (re-scope or hold; the code is integration-safe — `gs_sink` still executes in play); 2c's F5
admissibility (`ScreenPass::CanCheckpoint` needs the settle's open-recording arm, then `RootPlaySaveAtTitle`/`RootPlayRelaunchRestore` in its pin
set) before it re-enters; 1d (`video.scale` 1 in play, texres trims, the level-route profile — boundaries 1178/1179 are the first site);
2a sky, 2b sprites, 2d distortion/cloth/blend-shape; 3a's F8 LIVE restore (design only) and 3b M12 deletion (peak_mb 602/481 now); a
frame-clock recording of the first level (s3/s5 are tick-clock); the LIVE play run (the user's).

### 2026-10-11 13:2x — wave 2: M12 D1, F8 LIVE restore, owned sprites, 1c census + 1d (scale 1, 60 fps on the beach), owned screen effects; sky REVERTED

Integrator record `int_work/m_series/cutover/allon_w2/SECTION.md` (every number below is read from a file there). Branch
`integration-20261011-w2` from `main de48cfb4`, `cherry-pick -x` in the order services → product → math → graphics → textures, `git merge
--ff-only` → `main` code HEAD **`3c47bd96`** (49 commits = 4 Herschel + 8 Bernoulli + 4 Zeno(2b, port-math) + 20 Zeno (1c rebased + 1d) + 10
Lagrange (2c) + 1 partial pick + 2 integrator test fixes; `git cherry main <owner>` 4/8/4/18/8 × `-`, the `+` are the collision-resolved
picks and the five 2a commits). **Keys: 29 `hle.*` rows** (`gen_settings_table --check` → `reproducible (104 rows) -> PASS`): new
`hle.graphics_screen=on`, `save.restore_mode=live`, and **`video.scale=1`** (the deterministic fingerprint no longer carries it).

| track item | owner / branch (commits on `main`) | status | evidence (read) |
|---|---|---|---|
| **3b** M12 D1: delete the SPU2 register window on the IOP bus; the M12 deletion ledger | Herschel, `port-services-r3` `cf09dd81 e3b5927c 2157a011 2093c350` → `fc390831 690b9fcb 13b8764b 1b937ed0` | **INTEGRATED** (`stream/TRACK3B_REVIEW.md` APPROVED) | `nm` 0 window symbols in the play binary; deterministic lanes identical; `docs/design/M12_DELETION_LEDGER.md` §3 asks the parent's decision on the IOP move; peak_mb unchanged by D1 (632→635 noise under load) |
| **3a part 2** F8 restores LIVE in the running session (default `save.restore_mode=live`; relaunch by setting or as the named fallback) | Bernoulli, `port-product` `de038417 … ce8bbcd7` → `11d84ef5 … 07acd7f3` | **INTEGRATED** (`input_saves/TRACK3A_F8LIVE_REVIEW.md` APPROVED) | `RootPlayLiveRestore 110.95 s` Passed (`restore_route=live`, one process / one window bind, `F8 restore Queued` → `Completed`), `RootPlayRelaunchRestore 41.00`, `RootPlaySaveAtTitle 13.55` (`written at boundary 1300 (refusal: none)`, unlinked `buckets=4:1/21:1/24:2/26:3`), `RootPlayHotkeyF5 28.43`; collision with 1d on `draw_ir_queue_consumer.{h,cpp}` resolved with both blocks (`ForgetLastFrame` beside `LastFrameHeaderCopy`) |
| **2b** Sprites: every sprite-only UI texture owned at load; the untextured flat Sprite at its slot | Zeno, `port-math` `f5dde195 1468daa0 7509765c b9d4a2db` → `7427e141 7373ad98 4a756b38 811e2fd9` | **INTEGRATED** (`math/TRACK2B_REVIEW.md` APPROVED) | s3/s5/level play `graphics_sprites=replaced=11597/12247/12171,model_calls=0,refused=0`, `graphics_sprites_model_by_texture total=0`, `graphics_textures loaded=61`; `RootSpriteTitleLogo: ALL PASS` (`15807 yellow pixels`, `mean (234,209,18)`, grab and PRESENTED frame); **peak_mb s5 481 → 549** (2b's own A/B +76 for the owned UI textures) |
| **1c** retire the DMAC→VIF→GIF parse in play (rebased: the census + docs; the lowering is main's `3671104f`) | Zeno, `port-graphics-r2` `ea1af4cb 0b47b914 52f5eafd db8775de` → `336bdce8 454b0eca 5d940729 e96650ed` | **INTEGRATED — criterion `gs_sink → 0` OPEN** (`graphics/TRACK1C_REVIEW.md` NEEDS_CHANGES; integrated with 1d on the parent's instruction) | s3 play `gif_parse_executed=1`, `gif_parse=receives=123427,path1=92/78608,path2=123335/535273488` (PATH1 byte parse retired: 92 receives; PATH2 535 MB / 3000 f = the DIRECT uploads + the model's remaining buckets), `gs_sink=tags=1177310,…,kicks=27151398` (was 1266676 / 27937250); s5 `tags=1480017,…,kicks=34088509` (was 1551221 / 34519218) |
| **1d** `video.scale` default 1 in play; staged IMAGE store; hashed grab; footprint decode; owned draw into the queue record; sleeper-only signal; interactive QoS; band-stride mirror import (+ review F1 fix) | Zeno, `port-graphics-r2` `0c8ecf55 … c68dbfa7` (15) → `60439f39 … 37e319a3` | **INTEGRATED** (`graphics/TRACK1D_REVIEW.md` APPROVED) | **the level route (RootShadowLevelEntry's padlog, 7600 f, play, `--harness-window`, scale 1): beach render_fps p50 60.0 / p95 60.3 / min 59, game_hz 60 in 118/119 in-loop windows (one 59 own-time at the level load), EE cpu 14.3 ms/frame median** — the charter's DONE-1; `RootShadowLevelEntry 331 s` (scale 1) and `RootShadowLevelEntryScale2 530 s` ALL PASS; s3/s5 `pace_late_trace` no longer carries the 1178/1179 own-time boundaries (`EE_OWN_TIME windows=0`) |
| **2c** Lagrange: owned screen effects (blur/filter/wave) + the checkpoint arm (the wave-1 revert's fix) | Lagrange, `port-textures` `9793e6fb … ce137dc7 28ea620a 0244fe63 dce80478` → `60f7b324 … 7d15afcd` + **`6bcc1a9a`** (the `screen_application.cpp` hunk of `5d5297b1` alone: the reservation guard built in place) | **INTEGRATED** (`graphics/LAGRANGE_TRACK2C_2A_REVIEW.md` APPROVED) | s3 play `graphics_screen_selection=owned`, `graphics_screen_effects=calls=2926,…,model_calls=0,replaced=2926,filter_off=1107,refused=0,faults=0`, `graphics_screen_scene=passes=1816,filter_draws=1816,failures=0`; `RootScreenFilterLevel 529.90 s` ALL PASS at `video.scale=2` (its measured composition — the pin now states it: integrator `53d36a49`); `RootPlaySaveAtTitle` written at 1300 with the screen slot (bucket 4) among the unlinked items — the wave-1 refusal is gone; `CoreSingleGetenvSite` green |
| **2a** Lagrange: the sky owned (the META-arena DiskManager nodes) | Lagrange, `port-textures` `dcf6fd3e 861eb5ec 18699cda 5d5297b1(sky half) 996c6bc6` | **REVERTED from the branch — memory runaway on the level route in PLAY**; state preserved at `backup/integration-20261011-w2-with-2a` (`33b09065`) | with 2a in, every pin passed (12/12 incl. `RootSkyAttractFrame`), s3/s5 read sky `model_calls=0` — but the level route (7600 f, play, scale 1) read **`footprint … peak_mb=1654`** (`b3600:503 → b4200:921 → b7200:1039 → b7560:1600`, `graphics_owned_output … pending=317`) against 1d's own 672 flat; `--set hle.graphics_sky=off` → 550 flat, completed; `--set hle.graphics_screen=off` / `hle.graphics_sprites=off` → **`park=exception park_detail=Shadow pass limit`** at frames 5094 / 5391 with 3.2 / 3.5 GB (`shadow_pass.cpp:89`, the 64-reservation limit). The owner's deterministic beach leg (492) does not show it: PLAY (mtvu on, pipelined queue, wall-paced) does. Sky stays on the model: s3 `graphics_sky_start=replaced=0,model_calls=1779`, `graphics_refusals_sky total=1779 missing_resource … producer:0x1b9cf8/detail:4` |

**Integrator fixes (tests only, checks unchanged):** `53d36a49` RootScreenFilterLevel states `video.scale=2` (its selftest read `FAIL
video_scale=1` once 1d made the shadow pin's scale a default-1 variable); `3c47bd96` RootTriageBundleOnFault injects at frame 1300 — with 2b
+ 2c + graphics_frame owned, the drawlist ring (the GS model's DrawIR stream) is EMPTY for the first 400+ frames (`drawir=draws=0`; the frozen
main read a model Sprite `draw seq=558` at frame 100), at the title the model still draws (`vs_textured_tristrip seq=61603`) and every check
passes (9.4 s). Owner follow-up: the ring does not record owned deliveries.

**Build / suite (binary `9e1a7a44…`, BUILD-ID `2026-10-11 12:02:23` = the frozen bytes):** per-step Release builds `FAILED=0 warning:=0
ld_warning=0` (567 / 510 / 222 / 801 / 627 steps) and the full `--clean-first` rebuild of the merged set **1905 steps, 0/0/0**. Component suite
ALONE `ctest --test-dir build-release -E '^Root' -j8 --timeout 900` → **`100% tests passed, 0 tests failed out of 644`** (142 s; 24 skips =
wave 1's 22 + 2 Disabled). Game group `-j1` (Debug `nativeee` rebuilt first, 0/0/0) → **`76% tests passed, 19 tests failed out of 78`**
(2209 s): the 18 frozen-anchor GCLK reds BY NAME (`Restore needs owned logical clock state.`) — RECORDED, not weakened — plus
`RootSpriteTitleLogo` **exit 137 at frame 1856** with no `[run_bounded]` kill line and no JetsamEvent (a SIGKILL from outside the leg);
**re-run alone: `Passed 67.64 sec`, `ALL PASS`**. 0 other reds. All on an idle machine, one game process.

**DT-14 on the integrated Release (`run_s3.py --play --frames 3000`, `--harness-window`, gate `pgrep -x nativeee` empty + `load_1m < 4.5`,
foreign pids sampled: 0; `allon_w2/s{3,5}_play/leg/run.meta`, `level_play/leg/run.meta`):**

| replay | frame_waits / syncs / ordered_waits / sync_wait_ms | bridge waited | frames_dropped | audio dropped / underruns | in-loop game_hz (histogram) | field_hz | render_fps in-loop p50 / p95 | pace_late (trace) | unsupported kicks | peak_mb | gs_sink tags / kicks | gif_parse path1 / path2 receives |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **s3** (3000 f, wall 107.2 s) | **0 / 0 / 0 / 0** | 0 | 0 | **0 / 0** | **60 / 60** (44 w: 60→44; own-time windows 0) | 60 / 60 | **60.0 / 60.0** | 12 (`2:61:270085:3, 2983:34710:2`) | **0** | 564 (602) | 1177310 / 27151398 (1266676 / 27937250) | 92 / 123335 |
| **s5** (3000 f, wall 69.9 s) | **0 / 0 / 0 / 0** | 0 | 0 | **0 / 0** | **60 / 60** (44 w: 60→44) | 60 / 60 | **60.0 / 60.0** | 7 (`1:61:268625:3`) | **0** | 549 (481) | 1480017 / 34088509 (1551221 / 34519218) | 92 / 164407 |
| **level** (7600 f, wall 183.9 s, `video_scale=1`) | **0 / 0 / 0 / 0** | 0 | 0 | **0 / 0** | 59 / 60 (119 w: 60→118, 59→1 own-time at the level load) | 60 / 60 | **beach 60.0 / 60.3** (min 59; whole run 60.0 / 60.0) | 117 | **0** (19.35 M lowered) | **553** (1d's r6: 672) | 21.3 M / 470 M (7600 f) | — |

Deterministic 1300-frame coldboot vs wave 1's (`diff_legs_w1_vs_w2.txt`, `--allow-set-difference video.scale`): `audio.ahash: identical (217
lines)`; `frames.fhash` first divergent key **1251**, 50 keys (1251–1300 = the attract shot under the owned colour filter; 2c's attributed
0.4/channel delta, `LAGRANGE_TRACK2C_2A_REVIEW.md` §4); `mem.mhash` from key 60, `drawir`/`gsops.ohash` from key 62 (owned sprites and
screen effects leave the DrawIR stream and the mem lane — attributed in `TRACK2B_REVIEW.md` §2 and the 2c record).

**Binary (frozen):** `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.3c47bd96`, `PS2RECOMP-BUILD-ID 2026-10-11 12:02:23`, sha256
`9e1a7a446afa21b4b2aa4689ec5fcf57d83f820ab3393d516fe9bd54b3244964` (`cmp` clean, `BIN_FROZEN_OK`; `.1bb648b5` kept; `.dc1a8a3a` deleted for
disk, `bin/DELETED.txt`). **Play command:** "All-on build, no exceptions" with `nativeee_release.3c47bd96` in place of `nativeee_release`.
New in the exit census: `video_scale=1` (scale 2 = `--set video.scale=2`), `graphics_screen_selection=owned`, `graphics_screen_effects`/
`graphics_screen_scene`, `graphics_sprites … model_calls=0`, `graphics_sprites_model_by_texture`, `ee_cpu_ms`/`ee_cpu_total_ms`,
`gif_parse`/`gif_parse_executed`/`gs_sink_by_path`, `f8=live` on an F8 press (`save.restore_mode=relaunch` for the former route), no
`iop_bus_window_hits` row.

**Still open (named):** 2a (the owner reproduces the play-mode leak on the level route; the five commits in the backup branch); 1c's
`gs_sink → 0` (PATH2 is the DIRECT upload + the model's remaining buckets: textures-at-load, sky, M8 buckets); 2d distortion/cloth/blend-shape;
3b D2/D3 and the IOP move decision (ledger §3); peak_mb 549–564 on s3/s5 (2b +76), 553 on the level; the triage ring blind to owned
deliveries; `RootScreenFilterLevel` thresholds measured at scale 2 only; the LIVE play run (the user's).

### 2026-10-12 02:3x — wave 3: M12 D2/D3 (the IOP arm out of the play binary), textures at load (1c-2), 2d skin/blend-shape pins + the shadow-limit refusal; sky REVERTED again

Integrator record `int_work/m_series/cutover/allon_w3/SECTION.md` (every number below is read from a file there). Branch
`integration-20261012-w3` from `main af82bb9d`, `cherry-pick -x` in the order services → graphics → textures, `git merge --ff-only` → `main`
code HEAD **`e1247812`** (22 commits = 5 Herschel + 1 integrator test pin + 11 Zeno + 5 Lagrange 2d picks (3 whole, 2 partial); `git cherry
HEAD <owner>` 5/10/2 × `-`, the `+` are one context-shifted census pick, the two partial picks and the seven sky commits NOT integrated).
**Keys: 30 `hle.*` rows** (`gen_settings_table --check` → `reproducible (105 rows) -> PASS`): new **`hle.graphics_textures=on`** (every texture
record converts at the title's TextureRead; degrades with the Soft backend). The 3b review was NEEDS_CHANGES on ONE test-only item (a
component pin for the service-only refusals) — written by the integrator (`53e99d9e`), everything else in that stage "verified and ff-ready".

**Charter table — every item, DONE / OPEN with the evidence read on THIS binary (`allon_w3/s{3,5}_play/leg/run.meta`, `level_play/leg/run.meta`):**

| item | owner | status | evidence (read) |
|---|---|---|---|
| **1a** native `vs_skinned_lit_strip` (7 hashes) + `vs_poscolor_strip` | Socrates (w1) | **DONE** | s3 `graphics_kick_lowering_worker=submitted=1026670,…,lowered=1024467,…,raw=0,unsupported=0`; s5 `…1271988,…,raw=0,unsupported=0`; level `…19453842,…,lowered=19452247,…,raw=0,unsupported=0`; `vu1_hle_worker_misses_by_variant=none` (s3/s5) |
| **1b** `vs_textured_tristrip`, `vs_lit_tristrip`, `vs_zero_kick_cont/_a_cont` → unsupported 0 | Socrates (w1) | **DONE** | the same rows: `unsupported=0` on every replay (was 209833 / 215674 on `dc1a8a3a`) |
| **1c** retire the DMAC→VIF→GIF parse in play (`gs_sink` → 0) | Zeno (w1 lowering, w2 census, **w3 1c-2 textures-at-load**) | **INTEGRATED — criterion `gs_sink → 0` OPEN** (`graphics/TRACK1C_2_REVIEW.md` APPROVED on the re-scoped track) | PATH2 IMAGE transport retired: s3 `gif_parse=receives=53563,path1=92/78512,path2=53471/7169120` (was `123335/535273488` on `3c47bd96`), `gif_path2_by_class=image:0/0,texregs:18961/3538368,…,state:18961/2645040,geometry:15792/1010688`, `gs_sink=tags=1078030,…,kicks=27294491,image_qw=0` (was tags 1177310, image_qw>0), `gs_xfer=trxdir=0,…,owned=35280`, `texres=…decodes=0,prep_bytes=0,…,owned_serves=669408,owned_misses=0`; s5 `path2=76358/9707168`, `gs_sink tags=1347974 kicks=34089704 image_qw=0`; level `path2=1682926/176303248` (was 7.77 GB), `image_qw=0`. `gif_parse_executed=1` everywhere; the remainder is named per class (texregs/state → the M8 bucket owner, geometry → 2b/text) and per PATH1 bucket (`gs_sink_path1_by_bucket=exact=1`, buckets 2/3 = M8 slice B Socrates, 0 = sky Lagrange; INTERFACES.md §15) |
| **1d** `video.scale` 1 in play; texres trims; 60 render_fps p50 in the first level | Zeno (w2) | **DONE** (re-measured here) | level route (7600 f, play, scale 1): **beach render_fps p50 60.0 / p95 60.3 / min 59, game_hz 60 in 119/119 in-loop windows, EE cpu 12.68 ms/frame median** (w2: 14.3); s3/s5 render_fps p50 60.0 / p95 60.3–60.4 |
| **2a** the sky owned | Lagrange | **OPEN — REVERTED from the branch (second time)**; state at `backup/integration-20261012-w3-with-2a` (`7ca727b5`) | with 2a in: `RootSkyAttractFrame` red on the integrated composition (`replaced=0,model_calls=121`, `graphics_refusals_sky total=121 missing_resource … detail:6`, `graphics_sky_assets … textures_loaded=0,declines_memory=319`) — 1c-2's `TextureBindings::Read` writes the ownership word `0xffffffff` at object+0x60 for every record; 2a's `SkyAssets::TextureLoaded` reads that word as the DiskManager handle (`allon_w3/REVERTED_2a.txt`). The sky stays on the model: s3 `graphics_sky_start=replaced=0,model_calls=1779`, `graphics_refusals_sky total=1779 missing_resource … producer:0x1b9cf8/detail:4`. Owner interface: `TextureAssets::Find` (§15 bucket 0) |
| **2b** sprites owned | Zeno (w2) | **DONE** | s3/s5/level `graphics_sprites=replaced=11585/12247/12167,model_calls=0,…,refused=0`; `RootSpriteTitleLogo 67.41 s` Passed |
| **2c** screen effects owned | Lagrange (w2) | **DONE** | `graphics_screen_selection=owned` on every replay; `RootScreenFilterLevel 519.12 s` Passed (scale 2) |
| **2d** distortion / cloth / blend-shape / special materials | Lagrange (+ Socrates) (**w3 part**) | **PARTIAL: skin + blend-shape DONE with a presented-frame pin; distortion, cloth OPEN** | level `classes=…skinned=2473045,…,indexed-displace=2859340` lowered native (`raw=0`); `RootShadowLevelEntry` now pins `classes skinned > 0`, `indexed-displace > 0`, Crash's fur over his footprint in the grab AND the presented frame (`(124.8, 45.4, 4.1)` f/p7000), `peak_mb <= 1000` (450), `vu1_hle misses <= runs/1000` (0) — ALL PASS 313.90 s; the shadow pass limit refuses instead of faulting (`graphics_shadow … limit_refusals=0,faults=0` here; `ShadowPass PendingLimit` pin); census `vu1_hle(_worker)_misses_by_variant` (level: `0xb4c16758d3c6564c:0x1ac8:113`). Distortion: `draws=0` on every route in hand (not exercised); cloth: not on the route (0 library misses) — both named, not claimed |
| **3a** M11 service-only startup; F8 live restore | Herschel (w1), Bernoulli (w2) | **DONE** | every replay `startup=native iop_boot=0 service_startup=stream=native,sound=native,retained=none,consumers=0`; `RootServiceOnlyStartup 16.38`, `RootPlayLiveRestore 110.11`, `RootPlayRelaunchRestore 40.60`, `RootPlaySaveAtTitle 13.69`, `RootPlayHotkeyF5 27.63` Passed |
| **3b** M12 deletion of the models at zero in play; peak_mb must drop materially | Herschel (D1 w2, **D2/D3 w3**), Bernoulli | **IOP/STREAM/libsd/SIF-IOP/cdvd-IOP DONE (out of the play binary); ledger rows L5–L9 (DMAC/VIF/GIF parse, IPU, VRAM mirrors) OPEN** | `iop_linked=0` on every play replay (`LintPlayBinaryNoIop`: 0 IOP symbols in `nativeee`; the oracle `nativeee_oracle` carries the arm — 33 ctest rows re-pointed to it, `RootPlayBinaryServiceOnly` on play: `--set hle.startup=retained` exit 2 naming the key and the oracle); `--profile deterministic` on play → `[config] hle.startup=native (the profile's retained pin is the oracle binary's…)`; **peak_mb s3 564 → 532, s5 549 → 514, level 553 → 509** (w2 → w3; the charter's 580–690 band at dispatch) |

**Integrator fixes (tests only, checks unchanged):** `53e99d9e` the 3b review's unit pin (above); the two partial picks `597713d3` / `e1247812`
carry only the shadow-side hunks of Lagrange's `adf38155` / `c47d1c6f` (the sky halves stay with 2a). Collision resolved with both blocks:
`fd915433` on `app/host/graphics_application.cpp` + `graphics_runtime.cpp` (Zeno's `graphics_textures` option / `OwnsTextures()` /
`texture_release` / `detach_uploads` beside Lagrange's META `arena` window) — moot after the revert (the tree is the graphics tip + the 2d picks).

**Build / suite (binary `10a74019…`, BUILD-ID `2026-10-12 01:14:46` = the frozen bytes):** per-step Release builds `FAILED=0 warning:=0
ld_warning=0` (948 / 8 / 726 / 328 / 325 steps) and the full `--clean-first` rebuild of the merged set **1978 steps, 0/0/0** (6m27).
Component suite ALONE `ctest --test-dir build-release -E '^Root' -j8 --timeout 900` → **`100% tests passed, 0 tests failed out of 652`**
(146 s; 24 skips = wave 2's set). Game group `-j1` (Debug `nativeee` + `nativeee_oracle` rebuilt first, 0/0/0; the 31 re-pointed Root rows
+ `RootReleaseLanesIdentical` run the oracle binary) → **`78% tests passed, 18 tests failed out of 80`** (2160 s): EXACTLY the 18 frozen-anchor
GCLK reds BY NAME (`root_fail_logs/anchor_refusals.txt`: `Restore needs owned logical clock state.` on the oracle) — RECORDED, not weakened;
**0 other reds**. Passed: `RootClockContract 47.39` (`ALL PASS`: `DONE band … (out of band: none)`, `EE_OWN_TIME windows = 1 <= 3`,
`frame_waits=0`, `dropped=0`, `underruns=0`), `RootGraphicsBridgeAllOn 10.82`, `RootShadowLevelEntry 313.90` + `Scale2 517.07`,
`RootSpriteTitleLogo 67.41`, `RootScreenFilterLevel 519.12`, `RootPlaySaveAtTitle 13.69`, `RootPlayRelaunchRestore 40.60`, `RootPlayLiveRestore
110.11`, `RootPlayHotkeyF5 27.63`, `RootGsQueuePipelinedIdentical 10.48`, `RootServiceOnlyStartup 16.38`, `RootPlayBinaryServiceOnly 0.43`,
`RootPlayRecordReplay 39.32`, `RootTriageBundleOnFault 8.47`, `RootPlayModeSmoke 4.22`, `RootMovieLifecyclePlay 24.80`, and on the oracle
`RootTitleScreenEffects 76.23`, `RootTitleMusicPlays 14.35`, `RootMenuSfxAudible 20.50`, `RootAttractCutsceneFrames 69.64`. The with-2a gate
(`*-with-2a.*`: 1981 steps 0/0/0, 653/653, game group 19 red = GCLK-18 + `RootSkyAttractFrame`) is kept as the revert's evidence.

**DT-14 on the integrated Release (`run_s3.py --play --frames N`, `--harness-window`, gate `pgrep -x nativeee` empty + `load_1m < 4.5`,
foreign pids sampled: 0, all three `clean`; `allon_w3/s{3,5}_play/leg/run.meta`, `level_play/leg/run.meta`):**

| replay | frame_waits / syncs / ordered_waits / sync_wait_ms | bridge waited | frames_dropped | audio dropped / underruns | in-loop game_hz (histogram) | field_hz | render_fps in-loop p50 / p95 | pace_late | unsupported kicks | peak_mb (w2) | gs_sink tags / kicks / image_qw | gif_parse path1 / path2 (pkts/bytes) | iop_linked |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **s3** (3000 f, wall 107.3 s) | **0 / 0 / 0 / 0** | 0 | 0 | **0 / 0** | **59 / 60** (43 w: 60→42, 59→1 = one own-time window w39, `field_hz` 60) | 60 / 60 | **60.0 / 60.3** | 25 | **0** | **532** (564) | 1078030 / 27294491 / **0** | 92/78512 / 53471/7169120 | 0 |
| **s5** (3000 f, wall 70.0 s) | **0 / 0 / 0 / 0** | 0 | 0 | **0 / 0** | **59 / 60** (43 w: 60→42, 59→1 own-time w37) | 60 / 60 | **60.0 / 60.4** | 18 | **0** | **514** (549) | 1347974 / 34089704 / **0** | 92/78608 / 76358/9707168 | 0 |
| **level** (7600 f, wall 183.8 s, `video_scale=1`) | **0 / 0 / 0 / 0** | 0 | 1 | **0 / 0** | **60 / 60** (119 w: 60→119) | 60 / 60 | **beach 60.0 / 60.3** (min 59; whole run 60.0 / 60.3) | 21 | **0** (19.45 M lowered) | **509** (553) | 21135717 / 494892466 / **0** | 544/606544 / 1682926/176303248 | 0 |

Deterministic 1300-frame coldboot (`allon_w3/det*`): on the ORACLE binary vs wave 2's frozen main (`diff_legs_w2_vs_w3_oracle.txt`):
**`frames.fhash: identical by key (1300 keys paired)`, `audio.ahash: identical (217 lines)`**; `mem.mhash` from key 120, `drawir`/`gsops.ohash`
from line 1248 = 1c-2's attributed divergence (the ownership word written for every record; the IMAGE ops leave the GS op stream —
`TRACK1C_2_REVIEW.md` §3). On the PLAY binary the same profile runs `startup=native` (3b D2/D3: no retained arm to pin) — `frames.fhash` key
443, `audio.ahash` key 199, `mem.mhash` key 0 (`--allow-set-difference hle.startup`), the divergence `TRACK3B_2_REVIEW.md` §6 records.

**Binary (frozen):** `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.e1247812`, `PS2RECOMP-BUILD-ID 2026-10-12 01:14:46`, sha256
`10a74019a9998f01f17f715297932f2162b10b3e0c201646e2eab1ca604726d2` (`cmp` clean, `BIN_FROZEN_OK`; `.3c47bd96` kept; `.1bb648b5` deleted for
disk, `bin/DELETED.txt`). **Play command:** "All-on build, no exceptions" with `nativeee_release.e1247812` in place of `nativeee_release`.
This binary has NO IOP arm: `--set hle.startup=retained` and any `--set` that turns a native-startup consumer off (`hle.stream_files, files,
sound_bank_headers, sound_samples, music, music_buffers, audio_mix, movie_lifecycle, movie_start, input_service, save_storage`,
`iop.dispatch≠frame`) REFUSE with exit 2 naming the key — the oracle arm is `build-release/app/host/nativeee_oracle` (not frozen; tests only).
New in the exit census: `iop_linked=0`, `graphics_textures=loaded=319,destroyed=8,model_calls=0,…`, `gif_path2_by_class`, `gs_sink_path1_by_bucket`,
`texres_owned`, `gs_xfer … owned=`, `vu1_hle(_worker)_misses_by_variant`, `vu1_hle(_worker)_declines_by_program`, `graphics_shadow … limit_refusals=`.

**Still open (named):** 2a (the sky must consume the owned texture asset — `REVERTED_2a.txt`, Lagrange with Zeno's `TextureAssets`
interface); 1c's `gs_sink → 0` (PATH1 buckets 2/3 → M8 slice B, texregs/state/geometry PATH2 classes → the bucket owners; the MTVU worker's
113 raw misses on the level route, one variant); 2d distortion (no route in hand exercises it) and cloth; 3b ledger rows L5–L9 (DMAC/VIF/GIF
parse, IPU, VRAM mirrors) — peak_mb 509–532 now; `RootScreenFilterLevel` thresholds at scale 2 only; the triage ring blind to owned
deliveries; the LIVE play run (the user's).

## Standing charter 2 (2026-10-12 →)

### 2026-10-12 06:5x — wave 1: the in-game menu framework + C4 layout, A1 widescreen, A4 texture packs + the sky owned (2a), B1 Linux + the ONE host header, B2 Windows groundwork + C3 build scripts

Integrator record `int_work/m_series/cutover/allon_c2w1/SECTION.md` (every number below is read from a file there). Rules: CUTOVER.md
"STANDING CHARTER 2" — the DT-14 clock contract and the all-on invariants stay green; a stage that regresses is REVERTED. Branch
`integration-20261012-c2w1` from `main 636c797f`, `cherry-pick -x` in the order linux → windows → product → graphics → textures,
`git merge --ff-only` → `main` code HEAD **`729a321e`** (35 commits = 5 Herschel + 4 Socrates + 5 Bernoulli + 4 Zeno + 14 Lagrange + 3
integrator; `git cherry HEAD <owner>` 25 × `-`, 7 × `+` = the conflict-resolved picks, both owners' blocks kept). Five reviews: four APPROVED;
`stream/CHARTER2_B1_REVIEW.md` NEEDS_CHANGES on GCC-lane-only items with "nothing blocks the parent … merging port-linux for the macOS/Clang
scope" → integrated, its items 1–2 written by the integrator (`a78c226a`), item 3 as a parent note in `CHARTER2_B1.md`. **Keys: 110 rows**
(`gen_settings_table --check` → `reproducible (110 rows) -> PASS`; 30 `hle.*` unchanged): new `paths.user_data` (the C4 root: macOS
`~/Library/Application Support/NativeEE`, SDL's pref path), `paths.texture_packs` (`<root>/textures`), `ui.menu_pages=on`,
`ui.perf_overlay=off`, `video.texture_pack=none`; `paths.mc_dir` / `game_files` / `log_dir` defaults `""` (resolved under the root);
`video.aspect` enum `auto|4:3|16:9|16:10|21:9`. **No 2a revert this time:** `RootSkyAttractFrame` is GREEN on the integrated composition
(Lagrange's re-expression on textures-at-load); the sky is owned in play.

**Charter 2 table — every item, DONE / OPEN with the evidence read on THIS binary (`allon_c2w1/s3_play/leg/run.meta`, `level_play/leg/run.meta`, `root_pass_logs/`):**

| item | owner | status | evidence (read) |
|---|---|---|---|
| **A menu framework** (Display + Controls pages in the title's options menu via its page table; strings per language bank; settings in `ps2r.ini`, never the card) | Bernoulli | **DONE (foundation)** — `ui.perf_overlay` is the template option end to end | s3 `menu_ext=state=registered,constructs=1,pages=2,items=3,texts=5,banks=1,text_base=228`, `menu_ext_pages=page0=…,ui.perf_overlay=0,video.fullscreen=0;page1=…,input.kbm_pad=0`; `RootMenuPerfOverlay 157.70` ALL PASS (leg B: the overlay VISIBLE in the presented frame, `<user data>/ps2r.ini` carries `ui.perf_overlay = on`; leg A nothing); `MenuExtension` unit pin |
| **A1** widescreen (`video.aspect` 16:9/16:10/21:9/auto, 4:3 selectable): projection, culling, HUD fit, FMV letterbox | Zeno | **DONE** (behind `video.aspect`; the Display-page row is Bernoulli's next step — INTERFACES §16) | `RootWidescreenBeach 574.49` ALL PASS on the PRESENTED frame (`squeezed error 4.76 <= 0.6 x 16.16`; the SAVE dialog disc 0.75× as wide, centred; `fit_place=32817 fit_size=110184`); 4:3 arm = the frozen `e1247812` lanes byte-identical (the review's `diff_legs`); s3/level `video_aspect=ratio=4:3,… passed_projection=2926 … interior=0` (auto on a 4:3 window = identity) |
| **A2** display options (scale, filtering, vsync, fullscreen, overlay toggle) | Zeno + Lagrange + Bernoulli | **OPEN** (fullscreen live on the Display page, not persisted — no `video.fullscreen` key yet; the perf-overlay toggle DONE as A's template) | `menu_ext_pages … video.fullscreen=0` |
| **A3** high frame rate (render at display rate, logic 60 Hz) | Zeno | **OPEN** | — |
| **A4** texture packs by content hash + `--dump-textures` | Lagrange | **DONE** | `RootTexturePackTitle 43.43` ALL PASS (p900 logo band share `0.000` without / `0.383` with the pack); `TexturePack 106 checks`; s3 `graphics_texture_pack=pack=none,…` (no pack configured in the replay); the pack dir = `<paths.texture_packs>/<video.texture_pack>` (integrator `729a321e`: Lagrange's `paths.data_dir` folded into the C4 layout) |
| **2a** (charter 1) the sky owned | Lagrange | **DONE** (third attempt; re-expressed on textures-at-load) | `RootSkyAttractFrame 28.96` ALL PASS (`graphics_sky_start replaced=121,model_calls=0`, `graphics_refusals_sky total=0`, `textures_converted=237 programs_uploaded=121`); s3 `graphics_sky_start=replaced=1778,model_calls=0`, `graphics_sky_model=replaced=14224,model_calls=0`, `graphics_refusals_sky=total=0`; level `replaced=6379,model_calls=0`; `gs_sink_path1_by_bucket` no longer lists bucket 0 |
| **A5** QoL (skip FMV/attract, remap UI, slot manager, RETAIL_BUGS toggles) | Bernoulli | **OPEN** | — |
| **B1** Linux x86-64 + aarch64 (ONE platform header, presets, Dockerfile/CI stub, identity script) | Herschel | **DONE for the macOS/Clang scope; GCC lane + the Linux candidate leg OPEN** | one header `engine/platform/host/platform.h` (0 `__APPLE__`/`<OpenGL/gl3.h>`/`PlatformMacos::` code sites outside `engine/platform/`); 7 linux presets; `tools/ci/linux.Dockerfile` + `linux_build.sh` + `.github/workflows/linux.yml`; `verify/platform_identity.py` + `platform_ref/s3` (`VerifyPlatformIdentitySelftest` Passed; `VerifyPlatformIdentity` SKIP without a candidate). Linux Clang lane 716/717 in Herschel's container (`Render_Present` 163 = llvmpipe); the GCC lane blocked on the init-table split (B2) after the integrator's generator fix; the CI stub is red on all four lanes until `Render_Present` is gated per renderer |
| **B2** Windows groundwork (shard split knob + PCH, shims, presets, vcpkg, CI stub, target folder) | Socrates | **DONE (groundwork; the Windows compile is CI's first run)** | `RecompManifestIntact` (bodies hash split-independent), PCH object identity 32/32 (review), `PlatformHostShims` Passed (`arm=posix failures=0`); 3 windows presets; `.github/workflows/windows.yml` manual + continue-on-error; `host/platform.cpp` names the missing Windows forwarders with `#error`; `PS2R_GL_HEADER` undefined on `_WIN32` (gap 1 — the GL loader decision, Zeno) |
| **B3** CI matrix | Bernoulli | **OPEN** (the two stubs exist) | — |
| **C1** one self-contained artifact per platform, asset scan | Herschel | **macOS DONE by C3's `build.sh` (this wave's artifact); Linux/Windows OPEN** | `dist/macos/` 163 MB (`nativeee` + 14 dylibs + BUILD-ID + README + LICENSE placeholder + notices); `asset-scan.log` `ASSET_SCAN_CLEAN` (format constants only; 0 ELF byte-window hits) |
| **C2** first-run launcher GUI (ISO verify → extract → play) | Bernoulli | **OPEN** (`GameFilesStamp::Write` waits for it) | s3 `game_files_stamp=absent` (hand-supplied files) |
| **C3** repo-clone path (`build.sh` / `build.ps1`, README for users + developers) | Socrates | **DONE on macOS** (`build.ps1` unparsed here; the Linux branch unexercised) | `./build.sh --dist-only` → `dist/macos/`; README "You must own the game" + the preset table (Linux presets added by the integrator) |
| **C4** user data layout + BUILD-ID check | Bernoulli | **DONE** | s3 `user_data_root=<out>/userdata` (`--set paths.user_data`), `ps2r.ini` written there by the menu (RootMenuPerfOverlay), `UserData::Migrate` copies the cwd cards once; `GameFilesStamp::Verify` → `game_files_stamp=absent` until C2 writes it |

**Build / suite (binary `79a7ee3e…`, BUILD-ID `2026-10-12 05:43:02` = the frozen bytes):** per-step Release builds `FAILED=0 warning:=0
ld_warning=0` (853 / 337 / 582 / 585 / 714 steps) and the full `--clean-first` rebuild of the merged set **2020 steps, 0/0/0** (6m51);
focused ctests per step 122 / 63 / 59 / 95 / 88, all 100 %. Component suite ALONE `ctest --test-dir build-release -E '^Root' -j8 --timeout 900`
→ **`100% tests passed, 0 tests failed out of 661`** (162 s; 25 skips = wave 3's 24 + `VerifyPlatformIdentity`). Game group `-j1` (Debug
`nativeee` + `nativeee_oracle` rebuilt first, 0/0/0) → **`80% tests passed, 18 tests failed out of 88`** (2981 s): EXACTLY the 18
frozen-anchor GCLK reds BY NAME (`root_fail_logs/anchor_refusals.txt`: `Restore needs owned logical clock state.` on the oracle) — RECORDED,
not weakened; **0 other reds**. Passed: `RootClockContract 47.26` (`ALL PASS`: `DONE band … (out of band: none)`, `frame_waits=0`,
`dropped=0`, `underruns=0`), the charter-1 set (`RootGraphicsBridgeAllOn 10.86`, `RootShadowLevelEntry 315.93` + `Scale2 521.21`,
`RootSpriteTitleLogo 67.39`, `RootScreenFilterLevel 519.12`, `RootPlaySaveAtTitle 13.69`, `RootPlayRelaunchRestore 40.65`, `RootPlayLiveRestore
110.28`, `RootPlayHotkeyF5 27.53`, `RootGsQueuePipelinedIdentical 10.48`, `RootServiceOnlyStartup 16.40`, `RootPlayBinaryServiceOnly 0.45`,
`RootPlayRecordReplay 39.38`, `RootTriageBundleOnFault 9.53`, `RootPlayModeSmoke 6.29`, `RootMovieLifecyclePlay 24.75`, the oracle rows) and
the four NEW pins `RootSkyAttractFrame 28.96`, `RootWidescreenBeach 574.49`, `RootTexturePackTitle 43.43`, `RootMenuPerfOverlay 157.70`.

**DT-14 on the integrated Release (`run_s3.py --play --frames N`, `--harness-window`, `--set paths.user_data=<isolated root>`, gate
`pgrep -x nativeee` empty + `load_1m < 4.5`, foreign pids sampled: 0, both `clean`; `allon_c2w1/s3_play/leg/run.meta`, `level_play/leg/run.meta`):**

| replay | frame_waits / syncs / ordered_waits / sync_wait_ms | bridge waited | frames_dropped | audio dropped / underruns | in-loop game_hz (histogram) | field_hz | render_fps in-loop p50 / p95 | pace_late | unsupported kicks | peak_mb (w3) | sky | gs_sink tags / kicks / image_qw | iop_linked |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **s3** (3000 f, wall 107.2 s) | **0 / 0 / 0 / 0** | 0 | 0 | **0 / 0** | **60 / 60** (44 w: 60→44) | 60 / 60 | **60.0 / 60.3** | 25 | **0** | **565** (532) | owned: `replaced=1778,model_calls=0`, refusals 0 | 765711 / 21770999 / **0** | 0 |
| **level** (7600 f, wall 183.9 s, `video_scale=1`) | **0 / 0 / 0 / 0** | 0 | 1 | **0 / 0** | **59 / 60** (118 w: 60→117, 59→1 = one own-time window w37, `EE_OWN_TIME=1`) | 60 / 60 | **beach 60.0 / 61.0** (min 59; whole run 60.0 / 61.0), EE cpu 13.27 ms/frame median | 42 | **0** (17.9 M lowered) | **531** (509) | owned: `replaced=6379,model_calls=0` | 19592436 / 470742978 / **0** | 0 |

peak_mb rose 33 / 22 MB with the owned sky (237 converted textures + program residency) and the menu extension resident — recorded for 3b, not a gate.

**Binary (frozen):** `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.729a321e`, `PS2RECOMP-BUILD-ID 2026-10-12 05:43:02`, sha256
`79a7ee3e994ed8290b9ad7adbbdff75a546ad5fe7e6a3646e017a2cb440514ed` (`cmp` clean, `BIN_FROZEN_OK`; `.e1247812` kept; `.3c47bd96` deleted for
disk, `bin/DELETED.txt`). **Artifact (macOS):** `int_work/m_series/cutover/allon_noexc/dist/nativeee-macos-729a321e.zip` (33.4 MB; `macos/`
= `nativeee` + `lib/` + `BUILD-ID` + `README.md` + `LICENSE` + `THIRD_PARTY_NOTICES.md`; the same bytes as the frozen binary, ad-hoc
re-signed), sha256 `735a4b0e21a1168d7ab1fec4a69463394d3b975b39f6a929e3609835dc23359d`. **Play command:** "All-on build, no exceptions"
with `nativeee_release.729a321e` in place of `nativeee_release`; the settings file is now `~/Library/Application Support/NativeEE/ps2r.ini`
(the C4 root; `--set paths.user_data=<dir>` pins another; the repo's `memcards/` is COPIED into `<root>/memcards` once, never moved);
in-game: START → Options → the added **Display** / **Controls** pages (pad-navigable; `fps overlay` persists); `--set video.aspect=16:9`
(or `display.ini`) for widescreen until the Display row lands; `--set video.texture_pack=<name>` reads `<root>/textures/<name>/<hash>.png`,
`--dump-textures <dir>` writes every texture + `index.tsv` + `contact_sheet.png`. New census rows: `user_data_root=`, `game_files_stamp=`,
`menu_ext=`, `menu_ext_pages=`, `video_aspect=`, `graphics_texture_pack=`, `graphics_texture_dump=`, `graphics_sky_assets … textures_converted=`.

**Still open (named):** A2 (keys + rows), A3, A5, B1's GCC lane (init-table split, Socrates) + the Linux candidate leg + the `Render_Present`
renderer gate (the CI stub red on four lanes), B2's Windows compile (CI's first run; `host/platform.cpp` forwarders, the GL loader decision),
B3, C1 Linux/Windows artifacts, C2 launcher, the Display-page rows for `video.aspect` / `video.texture_pack`; the project licence and the
64-way re-split (user decisions, carried); charter 1's 1c `gs_sink → 0`, 2d distortion/cloth, 3b L5–L9 (peak_mb 531–565 now).

### 2026-10-12 20:4x — wave 2: C1 Linux artifact + the GCC lane, B3 CI matrix, C2 first-run launcher + A5 QoL, A2 Display rows + the A3 scaffold, A2 texture filtering + 2d cloth census

Integrator record `int_work/m_series/cutover/allon_c2w2/SECTION.md` (every number below is read from a file there). Rules: CUTOVER.md
"STANDING CHARTER 2" — the DT-14 clock contract and the all-on invariants stay green; a stage that regresses is REVERTED. Branch
`integration-20261012-c2w2` from `main f8cb78cd`, `cherry-pick -x` in the order linux → windows → product → graphics → textures,
`git merge --ff-only` → `main` code HEAD **`d67e84e0`** (40 commits = 9 Herschel + 6 Socrates/Bernoulli B3 + 9 Bernoulli C2/A5 + 4 Zeno +
7 Lagrange + 5 integrator; `git cherry HEAD <owner> f8cb78cd`: 31 × `-`, 5 × `+` = the conflict-resolved picks (ci.yml, the Display-page rows,
the A2 config + filter rows) and Lagrange's `a99a6875` (its `TEXTS` fix is subsumed by the integrated pin — skipped as empty)). Five reviews:
`stream/CHARTER2_C1_REVIEW.md`, `math/CHARTER2_B3_REVIEW.md`, `input_saves/CHARTER2_C2_A5_REVIEW.md`, `graphics/LAGRANGE_CHARTER2_A2_REVIEW.md`
APPROVED; `graphics/CHARTER2_A2_A3_REVIEW.md` (the integrator's, the independent stage cancelled) APPROVED FOR INTEGRATION with
`video.render_rate=60` default and A3's pixels OPEN — conditional on the integrated-Release pins, which passed (below). **Nothing reverted.**
**Keys: 122 rows** (`gen_settings_table --check` → `reproducible (122 rows) -> PASS`; 30 `hle.*` unchanged): new `video.fullscreen`
(`windowed|fullscreen|borderless`), `video.render_rate` (`60|display`), `harness.display_hz`, `video.texture_filter`
(`title|point|bilinear|trilinear`), `video.anisotropy` (1|2|4|8|16), `launcher.show`, `launcher.elf_sha256`, `paths.iso`, `qol.skip_movies`,
`qol.fast_forward`, `fixes.look_rest_seconds`, `fixes.skid_mark_init`; `video.scale` range 0..4 (0 = native window; default 1).

**Charter 2 table — every item, DONE / OPEN with the evidence read on THIS binary (`allon_c2w2/s3_play/leg/run.meta`, `level_play/leg/run.meta`, `root_pass_logs/`, `ctest-root.log`):**

| item | owner | status | evidence (read) |
|---|---|---|---|
| **A menu framework** | Bernoulli | **DONE** (wave 1) | s3 `menu_ext=state=registered,constructs=1,pages=2,items=9,texts=35,banks=1,text_base=228`; `menu_ext_pages=page0=…,ui.perf_overlay=0,video.fullscreen=0,video.scale=1,video.vsync=0,video.aspect=0,video.render_rate=0,video.texture_filter=0,video.anisotropy=0;page1=…,input.kbm_pad=0` — the eight-row Display page in the title's font (`root_menu_perf_overlay_legB_w_final.png`) |
| **A1** widescreen | Zeno | **DONE** (wave 1; the Display row `aspect ratio` landed this wave) | `RootWidescreenBeach 562.69` Passed; s3 `video_aspect=ratio=4:3,… passed_projection=2926` |
| **A2** display options: internal scale (native/1–4×), display mode (windowed/fullscreen/borderless), vsync, aspect, texture filtering (original/point/bilinear/trilinear) + anisotropy (off–16x), fps overlay — all on the Display page, live, persisted in `ps2r.ini` | Zeno (rows) + Lagrange (filtering) + Bernoulli (framework) | **DONE** | `RootDisplayRowsLive 83.61` Passed (`menu_ext … items=9,texts=35 … changes=2`, `display_rows=scale_setting=2,scale=2,scale_requests=1,scale_applied=1,scale_async=1,scale_inline=0,grab=1024x896,vsync=1`, `gfx_consumer resizes=1`, `ps2r.ini: 'video.scale = 2\nvideo.vsync = on\n'`); `RootTextureFilterTitle 61.57` ALL PASS (`title: owned_draws=8628,enhanced_draws=0`; `point: enhanced_draws=8087`; `bilinear: enhanced_draws=541`; p900 logo band point vs bilinear share 0.192, mean 1.79, steep ratio 1.28); `RootTextureFilterBeach 642.73` Passed; `RootShadowLevelEntryScale2 518.21` / `Scale3 929.47` Passed; `RootMenuPerfOverlay` PASS (the pin's items box widened for the eight-row page — `root_fail_logs/RootMenuPerfOverlay.first-run.log`); s3 `video_texture_filter=filter=title,anisotropy=1,max_anisotropy=16,owned_draws=684824,enhanced_draws=0` (the identity arm in the replay) |
| **A3** high frame rate (render at display rate, logic 60 Hz) | Zeno | **SCAFFOLD DONE, PIXELS OPEN** — `video.render_rate=display` presents at the display's rate with the camera interpolated in `CameraTrack`'s trace; the presented pixels are the latest tick's frame repeated (the owned passes do not re-render at the interpolated camera yet) | `RootHighFrameRate 177.58` Passed (`display_rows … render_rate=display,present_hz=120,presents=21018`, `presents 21018 >= 1.8 x frames 7100`, `in-loop windows=111 game_hz min/max=59/60 render_fps min/max=119/121`, `frame_waits=0`, `audio dropped=0 underruns=0`, `EE_OWN_TIME windows = 1 <= 3`; `hfr_camera=samples=5919,presents=21018,interpolated=11010,duplicates=10397`); `RootClockContractDisplayRate 47.09` Passed; default `60` → s3/level unchanged (`render_rate=60,present_hz=60`) |
| **A4** texture packs | Lagrange | **DONE** (wave 1) | `RootTexturePackTitle 43.10` Passed |
| **A5** QoL: START/CROSS skips a movie (`qol.skip_movies`), cutscene fast-forward while R2 held (`qol.fast_forward` fields per boundary; the census/judge subtract the fields), save-slot delete confirmation in the launcher, two RETAIL_BUGS fixes behind `fixes.*` toggles (default off) | Bernoulli | **DONE for skip / fast-forward / slot manager / retail toggles; controller remap UI OPEN** (the review's standing item: no gamepad-button remap page) | s3 `retail_fixes=look_rest_seconds=off,applied=0;skid_mark_init=off,applied=0;installed=0`; `RootClockContract 47.13` Passed with the fast-forward-aware judge; review §5 `CutsceneFastForward`, `RetailFixes` unit pins; `RootPlayHotkeyF5 28.19` Passed |
| **2a** sky owned | Lagrange | **DONE** (wave 1) | `RootSkyAttractFrame 30.79` Passed (`graphics_sky_start=replaced=121,model_calls=0`, `graphics_refusals_sky=total=0`); s3 `replaced=1779,model_calls=0`; level `replaced=6379,model_calls=0` |
| **2d** distortion / cloth / blend-shape routes | Lagrange (+Socrates) | **OPEN** — this wave: the cloth hash join (`vs_indexed_displace` = the owned family, `TEXTURE_FILTERING.md` / the 2d record), the cloth producer + particle-system blend census rows; distortion untriggered on the routes | review §5 (the two VU1 image hashes reproduced from the ELF) |
| **B1** Linux x86-64 + aarch64 | Herschel | **DONE** — Clang lane `100% … out of 732` and the GCC 13 lane building (the compact init table `3fc3c00e`; the generated oracle TUs; `FiberGuardPage` fixed) with ONE open red (`MathLibraryTrigOracle` `SnapToYaw` one float code, GCC only, not the shipped compiler); identity leg **IDENTICAL** on the four CPU lanes (audio 512, drawir 3068, gsops 3068, mem 52; frames divergent 0.29 %, reported) | `stream/CHARTER2_C1_REVIEW.md` §1–3; here `ctest-step1-linux-focused.log` 171/171; `3f34605b` ONE host sin/cos (no libm in the libvu0 substitution) |
| **B2** Windows x86-64 | Socrates | **GROUNDWORK DONE (wave 1) + the clang-cl flag shim (`dc9c6c80`, `PlatformClangClShimContract` Passed: 17 flag sets, mapped failing 0); the compile itself is CI's first Windows run** | `ctest-step2-windows-focused.log` 57/57 |
| **B3** CI matrix (macOS arm64/x86-64, Windows x64, Linux x64/arm64) Release + component suite per push; tag release workflow; game files NEVER in CI | Bernoulli | **DONE (wired; the first push is its run)** — `ci.yml` five targets `guard → build×5 → identity`, `release.yml` five artifacts + SHA256SUMS, `asset_guard.py` three classes + pins in every job (`CiAssetGuardTree` / `CiAssetGuardSelftest` Passed), the C1 Linux artifact job (`726fd5e4`) | `math/CHARTER2_B3_REVIEW.md` (actionlint + shellcheck PASS; sparse-checkout `CLEAN (0 finding(s), 15 allowed)`) |
| **C1** one self-contained artifact per platform + asset scan | Herschel (+ C3) | **macOS + Linux DONE; Windows OPEN** — Linux `linux-dist` preset (static SDL2/FFmpeg/libstdc++; `NEEDED` = libGL/libm/libc/ld only), `tools/ci/asset_scan.py` (names, magic, ELF byte windows; the positive control on `SLUS_123.45` FAILs with 125 findings); here `./build.sh --dist-only` → `dist/macos/` 82 MB, `asset_scan: CLEAN -- 24 files, 85988121 bytes, 0 findings` | `build-sh-dist.log`; the binary is **53 MB** now (138 MB in wave 1: the compact init table) |
| **C2** first-run launcher GUI (drop ISO/BIN → verify → extract → memcards/settings → Play; afterwards the settings screen; the harness CLI bypasses) | Bernoulli | **DONE** | `RootLauncherScreens 5.99` Passed (`first-run and settings screens presented; harness CLI bypasses`); `RootFirstRunExtract 0.51` Passed (`accept (raw .bin -> plain .iso, ELF byte-equal, memcards, ps2r.ini, stamp == PS2RECOMP-BUILD-ID …); 3 refusals left nothing; a later-stage failure removed its ELF, no stamp`); `DiscVerify` (ISO9660 + 2352 raw, SLUS-12345 present, ELF sha256 = the build's; PAL/other regions reported); s3 `game_files_stamp=absent` (hand-supplied files) |
| **C3** repo-clone path (`build.sh` / `build.ps1`, README) | Socrates | **DONE on macOS; Linux `./build.sh` = the linux-dist preset (Herschel's container run)** | `build-sh-dist.log` `build.sh: done -- …/dist/macos/nativeee (PS2RECOMP-BUILD-ID 2026-10-12 18:46:39)` |
| **C4** user data layout + BUILD-ID stamp | Bernoulli | **DONE** (the stamp is written by C2's extraction now) | `RootFirstRunExtract` (stamp == BUILD-ID); s3 `user_data_root=<out>/userdata` |

**Build / suite (binary `b03df788…`, BUILD-ID `2026-10-12 18:46:39` = the frozen bytes):** per-step Release builds `FAILED=0 warning:=0
ld_warning=0` (996 / 77 / 720 / 726 / 578 steps); focused ctests per step 171 / 57 / 73 / 158 / 153, all 100 %; Debug `nativeee` +
`nativeee_oracle` 406 steps 0/0. Component suite ALONE `ctest --test-dir build-release -E '^Root' -j8 --timeout 900` → **`100% tests
passed, 0 tests failed out of 671`** (142 s; 25 skips = wave 1's skipset exactly). Game group `-j1` → **`81% tests passed, 19 tests failed out
of 102`** (4948 s): the 18 frozen-anchor GCLK reds BY NAME (`red_names.txt` = wave 1's 18) — RECORDED, not weakened — **plus
`RootMenuPerfOverlay`**, whose functional checks all passed (`items=9,texts=35`, the overlay visible, `ui.perf_overlay = on` persisted) and
whose one red was the pin's own six-row items box (237 orange pixels of the highlighted first row, which the eight-row page draws higher):
box widened (`d67e84e0`, no product code), re-run alone **Passed 157.06 s** (2013 / 1974 orange pixels). **0 other reds.** Passed:
`RootClockContract 47.13` (`PASS DONE band … (out of band: none)`, `frame_waits=0`, `dropped=0`, `underruns=0`), the charter-1 set
(`RootGraphicsBridgeAllOn 10.75`, `RootShadowLevelEntry 309.13`, `RootSpriteTitleLogo 79.91`, `RootScreenFilterLevel 524.22`,
`RootPlaySaveAtTitle 14.59`, `RootPlayRelaunchRestore 40.02`, `RootPlayLiveRestore 110.52`, `RootPlayHotkeyF5 28.19`, `RootGsQueuePipelinedIdentical
10.48`, `RootServiceOnlyStartup 16.36`, `RootPlayRecordReplay 39.10`, `RootPlayModeSmoke 6.27`, `RootMovieLifecyclePlay 24.74`,
`RootTriageBundleOnFault 8.48`), wave 1's pins (`RootSkyAttractFrame 30.79`, `RootWidescreenBeach 562.69`, `RootTexturePackTitle 43.10`) and the
NEW pins `RootDisplayRowsLive 83.61`, `RootHighFrameRate 177.58`, `RootClockContractDisplayRate 47.09`, `RootShadowLevelEntryScale3 929.47`,
`RootTextureFilterTitle 61.57`, `RootTextureFilterBeach 642.73`, `RootFirstRunExtract 0.51`, `RootLauncherScreens 5.99`.

**DT-14 on the integrated Release (`run_s3.py --play --frames N`, `--harness-window`, `--set paths.user_data=<isolated root>`, gate
`pgrep -x nativeee` empty + `load_1m < 5.0`, foreign pids sampled: 0, both `clean`; `allon_c2w2/s3_play/leg/run.meta`, `level_play/leg/run.meta`):**

| replay | frame_waits / syncs / ordered_waits / sync_wait_ms | bridge waited | frames_dropped | audio dropped / underruns | in-loop game_hz (histogram) | render_fps in-loop p50 / p95 | pace_late | unsupported kicks | peak_mb (c2w1) | sky | iop_linked |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **s3** (3000 f, wall 107.2 s) | **0 / 0 / 0 / 0** | 0 | 0 | **0 / 0** | **60 / 60** (44 w: 60→44) | **60.0 / 60.0** | 15 | **0** | **518** (565) | `replaced=1779,model_calls=0` | 0 |
| **level** (7600 f, wall 183.9 s, `video_scale=1`) | **0 / 0 / 0 / 0** | 0 | 0 | **0 / 0** | **58 / 60** (119 w: 60→117, 59→1, 58→1 = two EE-own-time windows w37/w160, `EE_OWN_TIME=2`, out-of-band 0) | **beach 60.0 / 61.0** (min 58), EE cpu 12.87 ms/frame median (13.27) | 111 | **0** (18.3 M lowered) | **493** (531) | `replaced=6379,model_calls=0` | 0 |

peak_mb fell 47 / 38 MB (the compact init table's smaller binary image); the two own-time windows on the level route are the EE's own frame
work exceeding one period (the contract's allowance, ≤ 3; wave 1 had one).

**Binary (frozen):** `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.d67e84e0`, `PS2RECOMP-BUILD-ID 2026-10-12 18:46:39`, sha256
`b03df788dd0f41dc866d2b90076a6c0915a307138dae7f5cbbcdae1d99fd3d6d` (`cmp` clean, `BIN_FROZEN_OK`; 53,144,472 B; `.729a321e` kept; `.e1247812`
deleted for disk, `bin/DELETED.txt`). **Artifact (macOS):** `int_work/m_series/cutover/allon_noexc/dist/nativeee-macos-d67e84e0.zip` (28.1 MB;
`macos/` = `nativeee` + `lib/` (14 dylibs) + `licenses/` + `title/` (sdk_bindings.tsv, post_load_patches.txt, restore_mhash_expected.tsv) +
`BUILD-ID` + `README.md` + `LICENSE` + `THIRD_PARTY_NOTICES.md`; 31 entries, 85,988,121 B unpacked; the binary ad-hoc re-signed), sha256
`7b4246bd99763951384646bf2d4a9e0407f013f7201c4be237079161ea153985`. **Play command:** unchanged ("All-on build, no exceptions" with
`nativeee_release.d67e84e0`); a FIRST RUN without game files opens the launcher (drop the ISO/BIN → verify → extract → Play; `--game-files`
bypasses); in-game START → Options → **Display** (fps overlay, display mode, internal scale, vsync, aspect ratio, render rate, texture filter,
anisotropic) / **Controls**; START or CROSS skips an FMV, R2 fast-forwards a cutscene; F1 = the dev panel (save-slot manager with delete
confirmation). New census rows: `display_rows=`, `hfr_camera=`, `video_texture_filter=`, `retail_fixes=`, `fast_forward=` /
`fast_forward_fields=`, `graphics_cloth_producer=`.

**Still open (named):** A3 pixels (the owned passes re-rendered at the interpolated camera), A5's controller remap page, B1's GCC-only
`SnapToYaw` red + the Linux candidate leg in CI, B2's Windows compile (CI's first run; `host/platform.cpp` forwarders, the GL loader decision),
B3's first push (the matrix is wired, unrun), C1 Windows artifact, the fresh-machine "zip + ISO → beach < 5 min" measurement; the project
licence and the 64-way re-split (user decisions, carried); charter 1's 1c `gs_sink → 0` (s3 `kicks=21517654`), 2d distortion/cloth routes,
3b L5–L9 (peak_mb 493–518 now).

### 2026-10-13 05:1x — wave 3: C1 licence attestation + ledger L5/L7, B2 Windows cross-compile verified, A5 remap page + the first-run drill, A3 re-view mechanism (pixels OPEN), M8 slice B owned meshes

Integrator record `int_work/m_series/cutover/allon_c2w3/SECTION.md` (every number below is read from a file there). Rules: CUTOVER.md
"STANDING CHARTER 2" — the DT-14 clock contract and the all-on invariants stay green; a stage that regresses is REVERTED. Branch
`integration-20261013-c2w3` from `main 8d52ece4`, `cherry-pick -x` in the order linux → windows → product → graphics → textures,
`git merge --ff-only` → `main` code HEAD **`91dfdbe6`** (37 commits = 9 Herschel + 5 Socrates + 3 Bernoulli + 9 Zeno + 9 Lagrange +
2 integrator; `git cherry HEAD <owner> 8d52ece4`: 34 × `-`, 1 × `+` = Bernoulli's README commit re-contextualised over Herschel's Licence
section (`range-diff`: blank-line shuffles only, both blocks present)). Five reviews, all APPROVED: `stream/CHARTER2_C1_LIC_3B_REVIEW.md`
(loop 2), `math/CHARTER2_B2_WIN_REVIEW.md`, `input_saves/CHARTER2_A5_DRILL_REVIEW.md`, `graphics/CHARTER2_A3_PIXELS_REVIEW.md` (APPROVED
as the A3 MECHANISM with `video.render_rate=60` the default and the PIXEL TIER OPEN — `RootHighFrameRate`'s blocking pixel check is red
and reported as such below), `graphics/LAGRANGE_M8B_REVIEW.md`. **Nothing reverted.** **Keys: 123 rows** (`gen_settings_table --check` →
`reproducible (123 rows) -> PASS`): new `hle.graphics_meshes` (on; the 31st `hle.*` key); 122 → 123.

**Integrator fixes (2 commits, no product behaviour):** `b3ceffbd` — `LintOneClass` read two findings in the A3 commits on the integrated
tree (`frame_review.h:135` a three-statement `Count()` body in a header; `display_rows.cpp:183` a local `struct PendingReset` with a
destructor = a second primary type): `Count()` moved to the .cpp, the RAII reset became a `unique_ptr` deleter (same semantics). `91dfdbe6` —
the plain `./build.sh` (tests on, `macos-dist` preset = the documented user command) had never run on macOS's `/bin/bash` 3.2: two EMPTY
arrays (`TARGET_ARGS`, `XVFB`) are "unbound" there under `set -u` (`${arr[@]+"${arr[@]}"}` now); and under the static preset seven test
executables named `SDL2::SDL2` / `FFmpeg::avutil` directly AND through a static library's PRIVATE dependency → `ld: warning: ignoring
duplicate libraries` × 7 (headers through `INTERFACE_INCLUDE_DIRECTORIES` now, the archive linked once). The CMake edit regenerated the
BUILD-ID: the game group ran on `4de0b714…` (BUILD-ID `02:20:36`); the frozen bytes are `5f609ab8…` (BUILD-ID `05:00:47`), whose only
recompiled objects are `build_id.cpp.o` and `input_mapper.cpp.o` (its configure-time header, content unchanged); the 17-pin invariant
set and both DT-14 replays were re-run on the frozen bytes (below).

**Charter 2 table — every item, DONE / OPEN with the evidence read on THIS binary (`allon_c2w3/s3_play/leg/run.meta`, `level_play/leg/run.meta`, `ctest-root.log`, `ctest-root-final-binary.log`):**

| item | owner | status | evidence (read) |
|---|---|---|---|
| **A menu framework**, **A1** widescreen, **A4** texture packs | Bernoulli / Zeno / Lagrange | **DONE** (waves 1–2) | `RootWidescreenBeach 594.11`, `RootTexturePackTitle 45.30`, `RootMenuPerfOverlay 157.70` Passed; s3 `menu_ext … items=9,texts=35` |
| **A2** display options | Zeno + Lagrange + Bernoulli | **DONE** (wave 2) | `RootDisplayRowsLive 83.95`, `RootTextureFilterTitle 70.21`, `RootTextureFilterBeach 655.20`, `RootShadowLevelEntryScale2 564.13` / `Scale3 974.56` Passed |
| **A3** high frame rate | Zeno | **MECHANISM DONE, PIXELS OPEN** — `video.render_rate=display` re-renders the tick's frame at the interpolated camera (`FrameReview`: the presenter's ordered slot replays the recorded draw IR; async PBO window readback; the re-view yields to the tick's frames while its cameras are stale); default `60` unchanged (lanes identical to `d67e84e0` on the owner's HEAD, review §3) | `RootClockContractDisplayRate 47.27` Passed (`render_rate=display`, ALL PASS); **`RootHighFrameRate` Failed 175.94** — 28 PASS / 2 FAIL: `game_hz 60 at the display rate … (out of band: none)`, `EE_OWN_TIME windows = 1 <= 3`, `render_fps >= 110 in every in-loop window`, `presents 21027 >= 1.8 x frames 7100`, `hfr_review replays > 0, faults=0, stale=0 … replay_presented=11496,replay_refused=0`, camera-track CV 0.183 (red-before 1.026) — and `FAIL no judder in the re-viewed PIXELS: the presented camera deltas' CV 0.507 <= 0.50` (both the strictly-all-re-viewed and the busiest pan read 0.507 here; the owner's idle run 0.587 / 0.471; floor ≈ 0.44 from the camera path + the stream-order clamps, review §4). Reported, not weakened; the next lever is the owner's (a stamped camera side channel read at `Replay`). s3/level unchanged at `render_rate=60,present_hz=60`; `hfr_review=recording=0 … replays=0` |
| **A5** QoL incl. the controller remap UI | Bernoulli | **DONE** — the launcher's Controls page: press-to-bind for the 16 pad buttons (`PadButtonMap`, `bindings.ini [pad]`) and the 16 keys, deadzone/sensitivity, rumble/kbm toggles; the first-run drill `tools/first_run_drill.py` (zip + ISO in a fresh HOME / pinned user-data root, only the unzipped binary runs) reached the beach in **177.5 s / 179.7 s** (< 300 s) on this host, `docs/design/FIRST_RUN_DRILL.md`; README "Users" rewritten (Gatekeeper step) | `ControlsBindingsPressToBind 32/32`, `InputMapper 226/226`, `RootLauncherScreens 11.40` Passed (Controls page, refused scripted drop, auto hand-off), `RootPlayHotkeyF5 27.60`; review §3 (the operator's real root untouched, the ISO unchanged). OPEN nits for the owner: no physical pad exercised; `run.meta game_files_stamp` is evaluated before the launcher extracts (reads `absent` on the one-launch first-run route — `main.cpp:209` vs `:231`) |
| **2a** sky owned | Lagrange | **DONE** (wave 1) | `RootSkyAttractFrame 30.96`; s3 `graphics_sky_start=replaced=1778,model_calls=0`; level `replaced=6378,model_calls=0` |
| **2d** distortion / cloth / blend-shape | Lagrange (+Socrates) | **OPEN** (unchanged this wave) | — |
| **B1** Linux x86-64 + aarch64 | Herschel | **DONE** — the GCC-only `SnapToYaw` red ATTRIBUTED and FIXED (`d79799fb`: GCC folds float ops as if round-to-nearest; `-ffp-contract=off -frounding-math` tree-wide on the GNU lane; `06fec63a` the record); the licence-clean Linux artifact re-staged `dist/nativeee-linux-aarch64.tar.gz` sha256 `ea460209…aaa76` (16,845,011 B; its binary's one embedded FFmpeg string == the recipe, `--disable-version3` present; `NEEDED` = libGL/libm/libc/ld only) | review §1–§4 (every row re-derived); here `MathLibraryTrigOracle Passed 28.63` (Clang) |
| **B2** Windows x86-64 | Socrates | **CROSS-COMPILE VERIFIED on this host; the run is CI's** — `tools/build/win_cross_smoke.py`: 36 TUs (all of `engine/core`, the Windows arm, the largest + smallest recompiled shard, the common TU) → x86-64 COFF against the real MSVC CRT/SDK (xwin splat), the arm links to a 45,568-B PE32+ with every import resolved; the tree sweep 660/692 TUs pass the Windows front end, the 32 unknowns classed (7 GL loader `expected "FILENAME"`, 16 SDL vcpkg headers, 6 POSIX callers in `app/host` + `tools/vu1_replay`, 3 POSIX-only TUs) with owners; `host::StrCaseCmp/GmTimeUtc/ThreadCreate/Fd*` moves; `build.ps1` parsed (0 errors) + dry-run; README/ci.yml say CI has never run (no remote) | `PlatformWindowsCrossSmoke` **PASS `36 / 36 TUs compiled`** (`ctest-step2-windows-crosssmoke.log`, `PS2R_XWIN_ROOT` set; SKIP 77 in the plain suite without it), `PlatformClangClShimContract 2.66`, `PlatformHostShims`, `VuMtvu*`, `ThreadManagerThbase` Passed |
| **B3** CI matrix | Bernoulli | **DONE (wired; the first push is its run)** | wave 2 |
| **C1** self-contained artifact + asset scan | Herschel | **macOS + Linux DONE and LICENCE-CLEAN; Windows OPEN** — the complete licence set committed (`tools/dist/licenses/`: SDL2 zlib, FFmpeg LGPL v2.1 + LICENSE.md, imgui MIT, imgl3w UNLICENSE; `THIRD_PARTY_NOTICES.md`); `tools/ci/ffmpeg_static.sh` (n6.1.2, mpeg1/mpeg2 decoders + swscale only, `--disable-gpl --disable-nonfree --disable-version3`, cache keyed on tag + configure args) + the `macos-dist` / `linux-dist` presets link it statically; `build.sh` attests from the STRING EMBEDDED IN THE LINKED BINARY (refuses ≠ 1 string, any GPL/non-free/version3 flag, any argument ≠ the recipe); `LintArtifactLicenses --dist` reads the binary too. The wave-2 zip `d67e84e0` bundled Homebrew's GPL FFmpeg → FAILS the lint → **deleted** (`allon_noexc/dist/DELETED.txt`) | here `./build.sh` → `ffmpeg: the linked libavcodec's configuration == the recipe (23 arguments; --disable-gpl --disable-nonfree --disable-version3)`, `lint_artifact_licenses: PASS (0 failures)`, `asset_scan: CLEAN -- 14 files, 56365387 bytes, 0 findings`; `dist/macos/` = `nativeee` (static SDL2 + FFmpeg; `otool -L` non-system entries: 0) + `licenses/` (6) + `title/` + `BUILD-ID` + `README.md` + `LICENSE` + `THIRD_PARTY_NOTICES.md`, NO `lib/`; `LintArtifactLicenses` / `Selftest` Passed |
| **C2** first-run launcher | Bernoulli | **DONE** (+ `--launcher-disc <image>` scripted drop, auto Play) | `RootFirstRunExtract 0.35`, `RootLauncherScreens 11.40` Passed |
| **C3** repo-clone path | Socrates (+ Herschel, integrator) | **DONE on macOS and Linux** — `./build.sh` now runs END TO END on macOS's bash 3.2 with tests on (`91dfdbe6`); `build.ps1` reviewed/parsed, unrun (no Windows host) | `build-sh-dist.log`: `build OK -- warning: 0, ld: warning 0`, `100% tests passed, 0 tests failed out of 592`, `build.sh: done -- …/dist/macos/nativeee (PS2RECOMP-BUILD-ID 2026-10-13 04:57:04)` |
| **C4** user data layout + stamp | Bernoulli | **DONE** (the stamp-ordering nit above) | `RootFirstRunExtract` (stamp == BUILD-ID) |
| **Charter 1 — 1c** `gs_sink → 0` | Zeno + Lagrange (M8 slice B) | **OPEN, reduced** — `hle.graphics_meshes=on` (M8 slice B): the type-01 rigid/placed/blended materials the certificate closes over are delivered as OWNED draws (`MeshProducer` → `GraphicsProducers`), no VIF/VU1/GIF transport; the residue is NAMED by the counters (the certificate has 0 rigid IDs — the HUD rule; the shared-consumer `unowned_consumer` refusals) and is the materials owner's next closure | level `graphics_meshes=selected=1 … placed_replaced=319162/1349974, flush_replaced=115625/737258, rigid_replaced=0/551791, draws=1028736`, `graphics_mesh_assets=loaded=643 invalid=0 unsupported=0`, `graphics_mesh_materials … certified=539926 unowned_consumer=584841 receipt_stale=0`, `graphics_refusals_meshes=total=584841 … unsupported_state=584841 … producer:0x1be7f0/detail:4, faults=0`; `gs_sink kicks=445796413` vs `474809595` with `--set hle.graphics_meshes=off` on the same route (**−6.1 %**; bucket 2 `288239377` vs `320018981`); `RootMeshLevelEntry 649.65` Passed (the owned bucket in the PRESENTED beach frame, pixel-identical to the model arm per review §3); s3 `kicks=21602742` (c2w2 21517654; the title route has few owned materials: `placed_replaced=1963`) |
| **Charter 1 — 3b** ledger L5–L9 | Herschel (+ owners) | **L5 + L7 DONE; L6/L8/L9 OPEN** — L7: the whole IPU command/DMA model (`IpuDevice`, window 0x10002000, FIFOs, `ipu_vlc`/`ipu_color`/`mpeg_decode`, ch3/ch4 faces, `LibipuApi`) → `ps2r_ipu_model`, linked into `nativeee_oracle` only; L5: the DMAC SIF0/SIF1 kick callback → `#if PS2R_IOP_ORACLE` (`SifState` + its MMIO window stay: EE syscalls 121/122 store through them). `peak_mb` unchanged by it (handoff §4) | `LintPlayBinaryNoIop 1.83` Passed; s3/level `iop_linked=0`; `IpuDevice`/`IpuCompletion`/`IpuDmaCoupling` Passed (oracle); `InstallDispatch` size budget 410 (`a5dd5624`) |

**Build / suite (binary `5f609ab8…`, BUILD-ID `2026-10-13 05:00:47` = the frozen bytes):** per-step Release builds `FAILED=0 warning:=0
ld_warning=0` (1003 / 581 / 413 / 573 + 57 / 737 / 57 steps); focused ctests per step 287 (2 reds = `RootBindsDmacSyncSeams`,
`RootRegistersIopmSection`, both GCLK) / 91 + the cross-smoke 1/1 / 84 (1 red = `RootAnchorPadReady`, GCLK) / 183 (3 GCLK +
`RootHighFrameRate` + `LintOneClass` → fixed, re-run 25 = 24 + `RootVideoStackPresents` GCLK) / 137 / 53, every non-GCLK row green.
Settings table 122 → **123 rows PASS**. Component suite ALONE `ctest --test-dir build-release -E '^Root' -j8 --timeout 900` → **`100% tests
passed, 0 tests failed out of 677`** (135 s; 26 skips = wave 2's 25 + `PlatformWindowsCrossSmoke`, env-gated). Game group `-j1` → **`82% tests
passed, 19 tests failed out of 104`** (5763 s): the 18 frozen-anchor GCLK reds BY NAME (`red_names.txt` = wave 2's minus `RootMenuPerfOverlay`,
which Passed 157.70) **plus `RootHighFrameRate`** (the A3 pixel tier, above, per the review's terms). **0 other reds → nothing reverted.** Passed:
`RootClockContract 47.32`, `RootClockContractDisplayRate 47.27`, `RootGraphicsBridgeAllOn 10.73`, `RootShadowLevelEntry 325.61` (+ `Scale2 564.13`,
`Scale3 974.56`), **`RootMeshLevelEntry 649.65`** (new), `RootSkyAttractFrame 30.96`, `RootSpriteTitleLogo 73.42`, `RootScreenFilterLevel 567.88`,
`RootWidescreenBeach 594.11`, `RootTexturePackTitle 45.30`, `RootTextureFilterTitle 70.21`, `RootTextureFilterBeach 655.20`, `RootDisplayRowsLive
83.95`, `RootMenuPerfOverlay 157.70`, `RootPlaySaveAtTitle 13.67`, `RootPlayRelaunchRestore 40.44`, `RootPlayLiveRestore 111.01`, `RootPlayHotkeyF5
27.60`, `RootGsQueuePipelinedIdentical 10.47`, `RootServiceOnlyStartup 16.34`, `RootPlayBinaryServiceOnly 0.40`, `RootPlayRecordReplay 39.32`,
`RootPlayModeSmoke 4.23`, `RootMovieLifecyclePlay 24.87`, `RootPlayFmvPresents 24.72`, `RootTriageBundleOnFault 8.46`, `RootFootprintBounded 4.24`,
`RootFirstRunExtract 0.35`, `RootLauncherScreens 11.40`. On the FROZEN bytes (`ctest-root-final-binary.log`, -j1): the 17-pin invariant set
(`RootClockContract 47.31`, `RootClockContractDisplayRate 47.31`, `RootGraphicsBridgeAllOn`, `RootPlayModeSmoke`, `RootMovieLifecyclePlay`,
`RootPlaySaveAtTitle`, `RootPlayHotkeyF5`, `RootLauncherScreens`, `RootFirstRunExtract`, `RootPlayBinaryServiceOnly`, `RootServiceOnlyStartup`,
`RootTriageBundleOnFault`, `RootGsQueuePipelinedIdentical`, `RootSkyAttractFrame`, `RootPlayRecordReplay`, two selftests) **17/17**.

**DT-14 on the frozen Release (`run_s3.py --play`, `--harness-window`, isolated `paths.user_data`, gate `pgrep -x nativeee` empty + `load_1m <
5.0`, foreign pids sampled: 0, both `clean`; `allon_c2w3/s3_play/leg/run.meta`, `level_play/leg/run.meta`; in-loop windows per
`RootClockContract`'s definition):**

| replay | frame_waits / syncs / ordered_waits / sync_wait_ms | bridge waited | frames_dropped | audio dropped / underruns | in-loop game_hz (histogram) | render_fps in-loop p50 / p95 | pace_late | unsupported kicks | peak_mb (c2w2) | gs_sink kicks (c2w2) | gif_parse receives | iop_linked |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **s3** (3000 f, wall 107.2 s) | **0 / 0 / 0 / 0** | 0 | 1 | **0 / 0** | **59 / 60** (43 w: 60→42, 59→1 = one EE-own-time window, out-of-band 0) | **60.0 / 60.0** | 26 | **0** | **523** (518) | 21,602,742 (21,517,654) | 44,955 (path2 44,901) | 0 |
| **level** (7600 f, wall 183.8 s, `video_scale=1`) | **0 / 0 / 0 / 0** | 0 | 0 | **0 / 0** | **60 / 60** (119 w: 60→119, own-time 0) | **beach 60.0 / 61.0** (min 59), EE cpu 13.00 ms/frame median (c2w2 12.87; the meshes-off arm 12.57 — the owned Build on the game thread, review §5) | 29 | **0** (17.18 M lowered) | **523** (493; the meshes-off arm 528 on the pre-relink binary, so not the meshes) | **445,796,413** (meshes off: 474,809,595) | 1,439,506 (path2 1,439,000) | 0 |

**Binary (frozen):** `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.91dfdbe6`, `PS2RECOMP-BUILD-ID 2026-10-13 05:00:47`, sha256
`5f609ab82a43f637a61c9a05b1840b5b43efc7db2197db7406c0c223c7743bad` (`cmp` clean, `BIN_FROZEN_OK`; 53,728,872 B; `.d67e84e0` kept; `.729a321e`
deleted for disk, `bin/DELETED.txt`). **Artifact (macOS, the `macos-dist` preset — a separate build tree from the acceptance lane by design):**
`int_work/m_series/cutover/allon_noexc/dist/nativeee-macos-91dfdbe6.zip` (15,327,457 B; 20 entries, 56,365,387 B unpacked; `macos/BUILD-ID` =
`2026-10-13 04:57:04`; the binary ad-hoc re-signed), sha256 `61cab83b5cda603d088dea029c7b34e2aeaed1e95aceaae1b014014167f5be51`;
`asset_scan: CLEAN -- 14 files, 56365387 bytes, 0 findings` on the zip; the shipped static binary vs the acceptance binary, deterministic coldboot
1300 f: `frames.fhash identical (1300)`, `audio.ahash identical (217)`, `mem.mhash identical (22)`, `drawir`/`gsops` identical
(`allon_c2w3/dist_identity/`). **Play command:** unchanged ("All-on build, no exceptions" with `nativeee_release.91dfdbe6`); a FIRST RUN
without game files opens the launcher (drop the ISO/BIN → verify → extract → Play; `--launcher-disc <image>` scripts the drop); the launcher's
**Controls** page rebinds pad buttons and keys (`bindings.ini`). New census rows: `hfr_review=`, `graphics_meshes=`, `graphics_mesh_assets=`,
`graphics_mesh_materials=`, `graphics_refusals_meshes=`, `gif_parse=`.

**Still open (named):** A3 pixels (`RootHighFrameRate` CV 0.507 vs 0.50 — the camera side channel at `Replay`, Zeno); B2's Windows RUN (the GL
loader decision, the six POSIX callers, SDL on vcpkg — CI's first push), C1/C3 Windows artifact + `build.ps1` unrun; B3's first push (no remote);
the fresh-MACHINE "zip + ISO → beach" (this host's fresh-HOME drill: 177.5 / 179.7 s); the project licence and the 64-way re-split (user
decisions, carried); charter 1's 1c residue (the certificate's rigid closure, `unowned_consumer`), 2d distortion/cloth routes, 3b L6/L8/L9
(peak_mb 523).

## Builds for play (user-reviewed cadence, 2026-10-13 →)

**2026-10-13 14:29 — `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.baf15a38`** (sha256 `35ef97fe578dedb8f677…`, BUILD-ID 2026-10-13 14:29:22; main `baf15a38`). What changed: screen-mode swaps (windowed/borderless/fullscreen, scale) run on the presenter thread — the game thread no longer stalls and audio continues (`RootScreenModeSwap`); the pad's boundary read never blocks the game thread; the A3 render-rate row is hidden (default 60); the tick census books host work per site and `RootNoLongTick` S3/S5/S8 exist. User review (s9/s10): swap OK, keyboard+mouse work, no visual regressions. Known: the level-entry FRAME SKIP is NOT fixed — tick 1178 = 32.5 ms with 26 ms unattributed (`other`), outside the load tail; it is the last gate before release and is being attributed now. Shutdown on window close and Quit from the in-game menu hit the 10 s watchdog (being fixed). What to check: nothing new beyond the above until the next build.

**2026-10-13 23:04 — `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.2b14dc5b`** (sha256 `558a0830bb4012123357a1b23cbeeb6a5229b7005ab2182de299edbd971c9bb6`, BUILD-ID 2026-10-13 23:01:49; main `2b14dc5b`; dist zip `allon_noexc/dist/nativeee-macos-2b14dc5b.zip` sha256 `e5dbb691…8de26`, 20 entries, `asset_scan: CLEAN -- 14 files, 0 findings`, `lint_artifact_licenses: PASS`). What changed (the Mecha-Bandicoot release blockers): the VU1 library keys programs by their LOADED LAYOUT, not the stale tail after them — all 40 missed boss images now run native (`vu1_hle_worker=2050487/0/0`, `misses_by_variant=none`; `Vu1HleBossMecha` 159/159); the MTVU backpressure never parks the game thread (play-mode kick drop, `engine.mtvu_kick_drop`) — the periodic 560 ms stall is gone; material receipts travel in the savestate (`MSHR` section) and a frame is admitted to the owned mesh pass WHOLE or not at all (`mixed_frames` census) — a restore no longer puts owned and model meshes in one frame; the chunk-state census names the level-entry cut as the last tick of its load window (`chunk_cuts`, `load_tail_ticks=0`); shutdown is bounded (`shutdown=` ladder census, the guest join abandoned-and-named after 500 ms, the watchdog names the stuck step); keyboard defaults bind select=Tab and the dpad to the arrow keys with an `input_sources` census (active = the last event's source, hot-plug counted); harness knobs `inject_key` hold / `close_at_frame` / `close_after_ms` / `virtual_pad` / `--vu1-miss-cases`. Acceptance: Release 0/0/0; settings table reproducible (130 rows); component suite alone 681/681; game group 86/121 with the known 18 GCLK/anchor + RootPlayPolicyInert reds and these PRE-EXISTING reds (same numbers on the baf15a38 candidate record): RootShadowLevelEntry(+Scale2/3) streak 204–211 px, RootSpriteTitleLogo 12615 < 15000, RootTitleScreenEffects, RootScreenFilterLevel 58.77 vs 59.1, RootAttractCutsceneFrames; RootNoLongTick S3/S5/S8/S9 are red under the tightened judge (tail 0) on ticks baf15a38 ALSO has (its s3 trace: 1178 35 ms, 1179 17 ms, 2982 32 ms under a 15-tick tail): the post-FMV texture conversion tick (S5 435 / S9 428: `tex=13.8–14.9 ms/8` — host work, Lagrange's TextureAssets), the first tick after the cut at 16.7–16.9 ms (S5 1180, S8 1172: the mesh pass's second-frame flushes `mesh=4.4–4.5 ms/931`), and S3's post-FMV 2978–2981 (title code `other=14 ms` + `mesh=8.8 ms`); the cut tick itself is inside its window on every arm. Required green: RootClockContract, RootMechaBandicootClock, RootMechaBandicootMeshes, RootScreenModeSwap, RootSkyAttractFrame, RootQuitGame, RootExitClean, RootKbmPadOnly, RootPadHotplug — all PASS on the frozen binary (the quit/exit pin now accepts frames = close or close+1, both the same boundary — the stage's own binary read close; the kbm pins hold the FMV-skip press 3 boundaries like a real key). Boss resume (slot0, 1200 f, windowed, the state's four `--set`s, `boss_int/boss_leg2`): `game_hz=59,60,60,…,60` (every window), `pace_late=2` (the restore tick only), `bp=0/0/0 kick_drop=on drop=0/0/0/0 kicks_dropped=0`, `vu1_hle_worker=2050487/0/0`, `graphics_refusals_meshes total=0`, `graphics_shadow limit_refusals=0`, `audio dropped=0 underruns=0`, `shutdown_ms` 16–35 across the pins; `render_fps` ~29–30 and `frames_dropped=594` are the renderer's (rule 2: every mesh of the OLD slot0 runs the model path — `frames_whole_model=1200`, the state carries no receipts). **What to check:** load slot0 (`--load-state` or F8) — the boss fight at 60 with the world geometry intact and no flicker as it spawns (F5 in the arena saves a state WITH receipts: a later F8 of that state is the first owned-mesh boss resume — report `mixed_frames` from the exit census); the level-entry cut (beach load → play: one visible jump at most, no second one); Quit from the pause menu returns to the title (intro movie restarts); Exit = the window close or F1 → Close session, < 1 s; play with NO controller connected (Space/Enter = cross, Tab = select, arrows = dpad, WASD = left stick, mouse = right stick). Not in this build: the native 'Exit' row on the title menu, the movie-skip `[mpeg2video] Invalid frame dimensions 0x0` lines, the texture conversion off the tick (S9 428), M8 slice B skins (port-math bbcf468e/9a9efcf7/53275bf5) and the rigid/lit family commits (port-textures 815ead06/5e1699bd) — held for their own stage.

**2026-10-14 02:55 — NO NEW BUILD; the play binary stays `nativeee_release.2b14dc5b`** (the judged integration, record `int_work/m_series/cutover/judged/RECORD.md`). Both frozen-judge stages (CUTOVER.md "TWO OPEN BUGS WITH FROZEN JUDGES") were SENT BACK by the independent integrator's own runs: **BUG A** (level-entry frame skip, port-graphics-r2 a62afdc2..65c35187; judge `tools/verify/judge_frame_skip.py` committed once, never edited) — the judge FAILs 2b14dc5b twice as required (cut tick 36886 / 29488 us; frames_dropped 0 / 1) but ALSO FAILs the candidate twice (cut tick 23744 / 21846 us at host load 3.0–3.8, frames_dropped=0 both, every other tick of the ±40 window ≤ 16700) — the owner's own report said the same (BLOCKED on the cut alone); the stagger / mesh workers / wall-aware headroom / aged frame drop stay on the owner's branch until the cut tick itself is ≤ 16.7 ms on the integrator's runs. **BUG B** (Mecha-Bandicoot arena flicker, port-textures 12c31d81..286163f8) — the judge SCRIPT `tools/verify/judge_mecha_arena.py` is byte-identical to its first commit, but the FIXTURE `tools/verify/fixtures/mecha/session.padlog` committed with it (v1, sha256 db7044b3…) was replaced in 06b8dd6b by a v2 recorded on the candidate binary: a fixture change after the judge's first commit = sent back by the contract (record, do not integrate); independently, the frozen v1 judge FAILs the owner's candidate 286163f8 twice (7 / 10 presents outside ±15 luminance: bright presents 340–355 over the floor region, and in one run Crash's death fade 410–435 + a census frame at 827 of 2103) while FAILing 2b14dc5b twice (31 / 35 grey presents, the flicker). The cause Lagrange named (the play-mode frame skip erasing the dropped frame's BOUNDARY SLICE, which holds the clear quad + world buckets of the NEXT presented image; fix = scope the drop to the presented image via an ImageEnd mark at the display copy) is coherent with its evidence and the v2 runs, but under the contract only the USER can re-freeze the fixture as v2 (the v1 fight is not reproducible even with 286163f8's deterministic restore); until then nothing of either stage is on main. main stays `884f680d`; the candidate branch is kept as `backup/integration-20261014-judged-sentback`. **What to check:** nothing new — keep playing 2b14dc5b; the level-entry skip and the arena flicker are still present in it.
