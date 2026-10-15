# FILE MAP — host_app shard 5 (37 files, 24,607 lines)

Session s116, shard f5. Source tree: `$PS2R_OLD_ROOT` (read-only).
Plan references: ARCHITECTURE.md §3 tree / §13 one-class-per-file / §14 interfaces,
MIGRATION_PLAN.md §1.1/§1.3 + row tables, HOOK_RECLASSIFICATION.md, TRIAGE_CAPTURE.md,
A2_CONFIG_DIAG.md (env registry + dispositions), A6_GAME_SPECIFICITY.md (§7 decoupling map).

Peak configuration (run_play.sh: `PS2_GS_SCALE=2 PS2_GS_INTERP=1 PS2_FPS_CAP=60 PS2_MTVU=1
PS2_GL_FAST=1 PS2_GL_FAST2=1 PS2_VU1_HLE=1 PS2_VU1_HLE_DIRECT=1`) is NORM; SCALE/INTERP/FPS_CAP
become Config SETTINGs with those defaults.

---

### host_app/memory.cpp (5,373 lines)

- **Purpose**: The EE memory system: owns every guest memory arena as `std::vector<uint8_t>`
  (32 MB main RAM, 16 KB scratchpad, 2 MB IOP RAM, 2 MB meta-arena at EE 0x02000000, BIOS,
  VU0/VU1 code+data, EE I/O, GS privileged, IOP I/O, SPU2, SIF regs, 4 MB GS VRAM),
  `translate_address()` (the chokepoint mapping PS2 virtual addresses to host pointers),
  templated `read_slow`/`write_slow`/quad variants (the fastmem fallback path), the MMIO
  dispatch for reads/writes in 0x10000000–0x1000FFFF (timers, GIF, INTC, DMAC, VIF, SIF), and
  `LoadElfSections()` which side-loads `.vutext` from the game ELF. The write gauntlet is also
  the host for ~15 game-address write-watch probe families and one live behavioral workaround.
- **Public surface**: `memory::` namespace (`initialize`, `translate_address`, `read_slow<T>`,
  `write_slow<T>`, `read_quad_slow`/`write_quad_slow`, `get_pointer`, `is_io_register`,
  `write_io_register`); the arena vectors (`main_memory`, `gs_vram`, `iop_memory`, …) consumed
  by DMA/GS/VU/savestate; `giftag_register_alloc`; globals `g_current_pc` (dead — never
  updated, per steering), watch atomics (`g_cam_rot_watch`, `g_ring_watch_addr`, …).
  Included via `memory.h` by virtually every runtime TU and all recompiled code.
- **Classification**: MIXED (A6 §7 row H) — generic 32 MB RAM model + translate/read/write,
  with 11 game-address hits (ELF path, section table, T0 clock-fix addresses, watch-probe
  addresses). Subsystem: EE memory.
- **Refactor destination**: `engine/ee/mem/` — SPLIT per MIGRATION §1.3:105/§3 row H:
  - `engine/ee/mem/address_map.h/.cpp` (`AddressMap`): arena ownership + `translate_address`.
  - `engine/ee/mem/mmio_router.h/.cpp` (`MmioRouter`): the read/write MMIO dispatch; the 3.5k
    write switch dissolves into per-subsystem handlers *registered* by dmac/, gif/, vif/, gs/,
    ee/timing, ee/intc, sif at boot.
  - `engine/ee/mem/fastmem.h/.cpp` (`Fastmem`): slow-path templates + `g_fastmem_force_slow`
    arbitration (pairs with memory.h fast path — other shard).
  - ELF loading (`LoadElfSections`) → `engine/ee/mem/elf_loader.h/.cpp` (`ElfLoader`) driven by
    `TitleProfile.elf` / `TitleProfile.recomp.elf_sections`; the hardcoded path
    (`memory.cpp:140` `game_files/example_title.rep/SLUS_123.45`) and section table
    (`:166-170`, `.vutext 0x2D9A10/0x1DAA10/0xD140`) DELETE — data comes from the profile.
  - The T0-COUNT clock HLE embedded in `read_io` (see hacks) → `engine/ee/timing/` +
    `engine/ee/intc/` real overflow-IRQ mechanism; game-struct knowledge → title data.
  - [KEY-REDIRECT] → quarantined named module beside ee/mem (HOOK_RECLASSIFICATION §3.8),
    retired at its root fix.
  - All watch-probe families → `core/diag/` generic address-hook kit or DELETE (per A2).
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | `PS2_S8_MTXWATCH` (:1166) | unset=off | hex addr: log every EE store into a 64 B window (shadow display-list matrix hunt); forces all stores through write_slow | diagnostic(one-off), delete-both | DELETE (solved S8 session; generic watch mechanism survives in core/diag probe kit) |
  | `PS2_S15_WATCH` (:1176) | unset=off | hex addr:len write-watch with EE call stack (freeze/chunkload hunt) | diagnostic(one-off), delete-both | DELETE (same: mechanism → core/diag, this instance dies) |

  Also *referenced but latched elsewhere*: `PS2_MIPWR` (armed via `mipsrc::MipwrInit()`,
  getenv lives in mipsrc_diag.cpp), `r192gif::DrainThreadOn()` (gif_arbiter),
  `PS2_B57Q` (comment-referenced latch in another TU). Not double-counted here.
- **Hacks / workarounds** (each with the correct refactor implementation):
  1. **Hardcoded ELF path + section table** (:140, :166-170): title data baked into engine
     code. Correct: `TitleProfile.elf` drives a generic `ElfLoader` (A6 row H seam).
  2. **T0 monotonic-clock fix** (:~620-850): the MMIO read of T0_COUNT (0x10000000)
     reconstructs a 64-bit clock and *host-writes the game variable* `DAT_003097d0` (the
     wrap count the INTC_TIM0 overflow IRQ handler would advance on hardware), reading the
     game's timer-config struct through the pointer at `0x0030972C` (+0x68/+0x6C/+0x70
     fields) to derive ticks-per-frame. PS2 rule: T0 COUNT is 16-bit with a compare/overflow
     interrupt; owning class: `EeTimers` (engine/ee/timing) raising INTC_TIM0 through `Intc`
     so the game's own handler maintains its clock. The 0x30972C/0x3097D0 layout knowledge is
     title data (`games/<title>/hle/` clock model) if the interim
     emulation survives; target state is the real timer+IRQ mechanism with no game writes.
  3. **[KEY-REDIRECT]** (:3385-3466): every-256-writes scavenger that *repairs* the level-load
     object's name key (obj+0x500, `"Levels\Earth\Hub\Beach"`) clobbered by host-side
     corruption; deliberately outside all diagnostic gates (load-bearing mutator inside the
     write path). Correct: root-fix the corrupting writes (boot double-init / loader delivery
     divergence); until then quarantine as a named workaround module beside `ee/mem` with a
     ledger entry (HOOK_RECLASSIFICATION §3.8); deletion harness-gated.
  4. **Game-address watch-probe families** in read/write gauntlets — [W10-MEMWATCH] region
     A/B brackets, [NAME-WRITE] (0x9819a0 scene+0x500), [RING-WATCH] (FMV ring counter),
     [VTABLE-WATCH] (0x309728 et al.), [BULLSEYE-SCENE-WP] (scene struct fields),
     [STALE-CHAIN-WATCH] (VIF1 chain ptr), [STREAM-STATE-WATCH] (STREAM.IRX vars),
     [S100-MIPWR] hooks, hardcoded addrs 0x309508/0x309718/0x1a88698/0x420BC8/0x01997D78…:
     concluded-session forensics in the hottest path. Correct: DELETE bodies; the *mechanism*
     (multi-range write watch with call-stack capture) becomes the config-driven probe kit in
     `core/diag` (PS2R_DIAGNOSTICS builds only).
  5. **Ungated hot-path logging** (295 `g_logFile<<` sites, 35 gated — A2 §3.3): per-MMIO-read
     lines (`VIF0_FBRST Read` :557-566, `[VBLANK-FIX]`/`[STALL WATCH]` every-1000 INTC reads).
     Correct: compile-time category logger (`core/log`), dead categories compile to nothing.
  6. **GIF MMIO read fence** (`r192gif::MmioFence()` under DrainThreadOn): a cross-thread
     ownership patch. Correct: MmioRouter's GIF handler is registered by gif/ and owns its
     fencing internally per the §7 single-writer table.
- **Risks**: memory.cpp is included-by-everything; moving arenas into classes changes linkage
  of `extern std::vector` consumers (savestate, DMA, graphics_thread, tests). The T0 clock
  rework is behavior-affecting (loading-logo animation, budget predicate feed) — prove with
  the boot-to-gameplay corpus replay + `tests/test_time_domain.cpp` pins + coldboot_gate
  (luma) harness; KEY-REDIRECT removal needs the level-load progression harness leg
  (chunk-load freeze corpus, BUG-001 family). MMIO router split proven by gs_replay/vif_replay
  corpus legs (byte-identical DrawIR) and savestate roundtrip (RAM section CRC).

---

### host_app/graphics_thread.cpp (4,341 lines; ~4,390 after [TICKSYNC 2026-09-25])
- **[TICKSYNC 2026-09-25 addendum]**: the VSync case acks its lockstep ticket
  (`eetiming::FrameRendezvous::Ack`) and brackets the harness settle
  (`verify::OnVsyncCaseBegin/End`) at its entry and BOTH exits (coalesce-skip,
  present/hold); under `PS2_TICK_SYNC=1` it raises no INTC VBON and sets no
  GS_CSR bit (the EE boundary tick does both); the `\r[FRAME N]` progress print
  is suppressed under `verify::Armed()` (it spliced onto diag lines);
  `FrameRendezvous::Wake()` at loop exit. Destination unchanged (gs/present
  consumer); the ack is the gs/present ↔ ee/timing seam.

- **Purpose**: The graphics-thread main loop (`GraphicsThreadFunc`): pops `RenderJob`s from
  `RenderQueue`, dispatches DrawBatch jobs to the backend (`ProcessDrawBatchJob`), runs the
  VSync case (scanout/present, `g_vsync_frame` bump, VBLANK raise, present pacing, warp skip,
  frame interpolation, bench/slowspot/artdet/spike hooks) and window/clear/resize jobs. Also
  hosts a decade of present-side diagnostics (snapshots, censuses, draw dumps) and two
  behavioral FMV/composite workarounds.
- **Public surface**: `GraphicsThreadFunc()` (spawned by main.cpp); `g_vsync_frame`,
  `g_texflush_pending`, `g_vram_pages_mutex`/`g_vram_written_pages`, `g_s32_artdiag_until`;
  bench emit helpers (`GsClassRdrEmitForBench`, `PresentPaceEmitForBench`,
  `GsInterpEmitForBench`, `FttCensusEmitForBench`, `GsS105SkipEmit`);
  `ps2_census_current_frame()` (C hook for call_census); `R168LiveDrawCount()`.
- **Classification**: MIXED (A6 §7 row C — 6 game-VRAM-constant hits: 0x370000 loading logo,
  0x230000 scene RT, 0x70000 work buffer, 0x3c0000/0x3f0000 atlas windows, 0x150000 FMV).
  Subsystem: GS HLE (queue consumer + present).
- **Refactor destination**: SPLIT per MIGRATION §1.3:109 (`gs/hle/queue consumer + gs/hle/present
  + core/diag taps`):
  - `engine/gs/hle/queue/queue_consumer.h/.cpp` (`QueueConsumer`): the job loop + DrawBatch
    dispatch (thin: pop → backend call; policy lives below it).
  - `engine/gs/hle/present/presenter.h/.cpp` (`Presenter`): VSync case — scanout composition,
    present, `g_vsync_frame`, VBLANK raise, warp/pace/interp hooks in one boundary sequence.
  - `engine/gs/hle/present/composite_guard.h/.cpp` (`CompositeGuard`): the base-layer
    freshness rule (see hack 1), parameterized by `RendererProfile.vram` — layout queries
    (`layout.is(scene_rt, fbp)`), never literal `0x230000`.
  - VRAM constants + the loading-logo special case → `games/<title>/
    renderer_profile.cpp` (`VramLayout` rows + scene hooks).
  - Diagnostics blocks (snap, artdet trigger, spike, BB windows, R169 run census, R197/R198
    dumps, FBP census, gfxloop census) → `core/diag/` (diag builds) or `core/triage/`
    (artdet fire, spike detection, snapshot request — TRIAGE_CAPTURE M5/E5/E13/E14) or DELETE
    (concluded experiments), per the flag table.
  - Bench gate-mirror registration block (:1990-2070) → dies with the env-gate era; the
    replacement is the Config startup dump (A2 finding 2).
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | `PS2_SNAP` (:218) | unset=off | frame-spec'd RAM/PPM snapshot capture | tuning-knob, delete-losing-arm | DIAG → core/triage snapshot writer (config-driven; env dies). Divergence from A2 auto-row justified by TRIAGE_CAPTURE E13/E15 keeping snap as capture primitive |
  | `PS2_SNAP_DIR` (:232) | `logs/` | snapshot output dir | tuning-knob | DIAG (TriageBundler owns paths) |
  | `PS2_SNAP_DISP_ONLY` (:292) | off | restrict snap to display FB | tuning-knob | DIAG |
  | `PS2_SNAP_RT` (:320) | off | include RT FBOs in snap | tuning-knob | DIAG |
  | `PS2_GFXLOOP_CENSUS` (:365,:372 — 2 latches, mirror) | off | gfx-loop iteration census | diagnostic | DIAG (core/diag; single latch) |
  | `PS2_GFXLOOP_ARM` (:382) | off | arm census at frame N | experiment, delete-both | DELETE |
  | `PS2_GFX_SLOWJOB` (:496) | off | slow-job (>N ms) reporter | tuning-knob, delete-losing-arm | DIAG (folds into core/triage PerfMonitor annotators) |
  | `PS2_R198_QORDER` (:569) | off | queue-order experiment (3-file mirror with gs_state/render) | experiment, delete-both | DELETE (both arms) |
  | `PS2_R198_QORDER_BASE` (:576) | — | experiment param | experiment | DELETE |
  | `PS2_R198_QORDER_ZCEN` (:738) | — | experiment param | experiment | DELETE |
  | `PS2_FMV3_BLIT` (:1366) | off | FMV blit extent probe | tuning-knob, delete-losing-arm | DELETE (concluded FMV3 session probe) |
  | `PS2_FMV_COMPFIX` (:1467) | **ON** | FMV bypass of the composite-drop guard (behavioral!) | tuning-knob, delete-losing-arm | NORM — winning arm becomes the only path inside CompositeGuard; hatch deleted |
  | `PS2_FMV3_COMPGATE` (:1486) | off | composite-gate decision probe | experiment, delete-both | DELETE |
  | `PS2_VIF_FAST` (:2021) | ON | *mirror* of vif.cpp latch for bench JSON | shipping-setting (A2) | NORM (peak config; mirror dies — Config dump replaces gate reporting) |
  | `PS2_GIF_FAST` (:2035) | ON | *mirror* of gif.cpp latch for bench JSON | tuning-knob | NORM (mirror dies) |
  | `PS2_GFXLOG` (:2076) | OFF | opens logs/gfx_diag.txt per-batch stanza (measured 15-22 ms/f when on) | diagnostic | DIAG (compile-time GFX log category) |
  | `PS2_STALL_INJECT` (:2211) | off | inject artificial gfx stalls | tuning-knob | TEST-ONLY (harness fault injection) or DELETE; prefer TEST-ONLY in verify/ |
  | `PS2_W16_SPIKE` (:2443) | off | spike experiment probe | experiment, delete-both | DELETE (SpikeDetector in core/triage supersedes — TRIAGE M5) |
  | `PS2_ARTDET` (:2547; mirror with artdet_flicker.cpp) | off | left-zone flicker sampler arm | diagnostic | DIAG→TRIAGE: detector becomes diag-build annotator firing through TriageHost (TRIAGE §12 row E5) |
  | `PS2_ARTDET_EVERY` (:2551) | numeric | sample cadence | tuning-knob | DIAG (Config triage field) |
  | `PS2_ARTDET_GREEN` (:2556) | numeric | green-band threshold | tuning-knob | DIAG |
  | `PS2_ARTDET_WEIRD` (:2560) | numeric | weirdness threshold | tuning-knob | DIAG |
  | `PS2_ARTDET_MIN` (:2564) | numeric | min-pixels threshold | tuning-knob | DIAG |
  | `PS2_ARTDET_DIAG` (:2654; mirror with opengl_backend) | off | force per-draw census after trigger | diagnostic | DIAG |
  | `PS2_BB_WINDOWS` (:3576) | off | boot-black frame-luma windows | tuning-knob | TEST-ONLY (coldboot_gate.py harness input — TRIAGE E14) |
  | `PS2_BB_SNAP` (:3600) | off | per-frame luma PPM sampling | tuning-knob | TEST-ONLY (same harness) |
  | `PS2_SPIKE_DUMP` (:3716) | off | spike frame dump | diagnostic | DELETE (superseded by core/triage SpikeDetector bundles) |
  | `PS2_R197_DRAWDUMP` (:4121) | off | per-draw dump (one-off) | diagnostic(one-off), delete-both | DELETE |
  | `PS2_GFX_FBPCENSUS` (:4183) | off | FBP usage census | diagnostic | DIAG (core/diag census family) |
- **Hacks / workarounds**:
  1. **Composite-drop guard** (`s_work70000_base`/`s_base_scene`, :1323-1460): drops
     0x70000→0x0 composites whose rebuild lacked the 0x230000 base layer (texture-popping
     fix), with a 240-composite decay and the [R196-COMPFIX] FMV un-drop. Behavioral policy
     keyed on literal VRAM addresses and prim/psm shape sniffing, living in the *thread loop*.
     Correct: a `CompositeGuard` class in gs/hle/present implementing the real rule — a
     composite may only sample a source RT whose content epoch is complete (VramImage/
     RenderTarget epoch arbitration, ARCHITECTURE §4.3/§4.4) — with scene_rt/work/display
     identified via `RendererProfile.vram`. FMV-active bypass becomes "the movie stage IS the
     base layer" expressed as target-class metadata, not a wall-clock `DecodeActiveRecently(500)`.
  2. **Loading-logo probe + LOGO_GREENBOX sentinel** (:1216-1260): logs state for
     TBP==0x370000 and force-paints the quad green when a sentinel file exists. Session
     forensics + filesystem-sentinel control channel. DELETE; the stamp/VRAM knowledge is
     RendererProfile data; visual adjudication belongs to gs_replay + frame-hash oracle.
  3. **Cloud-freshness counters** (`s_clouds_since_wipe`, :1339-1360): PSMT8-tristrip
     heuristics on literal 0x230000. DELETE with 1's real fix.
  4. **Gate-mirror block** (:1990-2070): re-reads ~25 other TUs' env gates (r118/r119/r120/
     r121/r123/r124/r125/r154/r159/r162/r164, VIF_FAST, GIF_FAST, VU_MODE) to stamp bench
     JSON — the A2 §2.3 mirror hazard. Correct: gates die; Config is dumped once by
     core/config at startup.
  5. **Hardcoded arm frames** (gfxloop/pacebrk arm at frame 1990; AtlasWindowFrame windows;
     R169 run caps): savestate-specific magic numbers. DELETE with their probes.
  6. **Weak-symbol frame hook** (`ps2_census_current_frame`): linker-level layering dodge so
     call_census works in the SUT runner. Correct: an injected `FrameClock` interface (the
     census moves to core/diag with an explicit ctor dependency).
- **Risks**: the present/boundary sequence ordering (vsync bump → interp → pace → VBLANK
  raise → warp check) is the frame-boundary protocol — any reorder changes pacing semantics
  (BUG-002/BUG-026/BUG-027 territory). Prove with: frame-hash-at-present oracle across the
  19-capture gs_corpus, present-cadence census A/B (old vs new p50/p95), boot luma gate, and
  the FMV corpus leg (composite guard byte-identity on the movie scenes).

---

### host_app/mpeg_hle.cpp (2,520 lines)

- **Purpose**: The sceMpeg/IPU HLE — the whole FMV pipeline with the PS2 IPU replaced by a
  host FFmpeg decoder: PSS demuxer (video/audio stream-id split), per-SceMpeg-context
  `PlaybackState` (AVCodecContext + parser + SwsContext + PSS ring + decoded-frame deque),
  `sceMpegGetPicture`-style delivery writing RGBA/RGB16 into EE RAM in IPU macroblock layout,
  authored-rate delivery pacing ([R136-FMVPACE], tick-domain under warp), FMV completion-gate
  hold, and FMV audio capture into the sound core ([S45-FMVAUD]).
- **Public surface**: `namespace mpeg_hle` — the sceMpeg HLE entry points (bound via
  sce_hle bindings), `DecodeActiveRecently()` (read by present pacer + graphics thread),
  playback state accessors for the graphics thread (width/height), FMV PCM source for
  sound_core. Consumers: sce_hle dispatch, present_pace.h, graphics_thread.cpp, ps2_vblank.
- **Classification**: GENERIC-PS2 (mechanism: libmpeg/IPU semantics per SDK 3.0.1) with
  title-tuned pacing constants; A6 does not list it as MIXED. Subsystem: IPU HLE.
- **Refactor destination**: `engine/ipu/hle/mpeg_ipu.h/.cpp` — SPLIT per MIGRATION §1.3:187
  (`mpeg_hle.{cpp,h} + ipu.{cpp,h} → ipu/hle/mpeg_ipu.*`; PACE_AUTHORED folds into
  `time.mode`). One-class-per-file shape:
  - `engine/ipu/hle/mpeg_context.h/.cpp` (`MpegContext` — per-ctx PlaybackState: decoder,
    ring, frame queue).
  - `engine/ipu/hle/pss_demux.h/.cpp` (`PssDemux` — stream-id walk, length-prefixed headers;
    pairs with the offline `pss_census.py` mirror kept in tools/diagnostics).
  - `engine/ipu/hle/ffmpeg_decoder.h/.cpp` (`FfmpegDecoder` — already a class).
  - `engine/ipu/hle/frame_writer.h/.cpp` (`FrameWriter` — WriteFrameToGuest RGBA/RGB16
    macroblock layout; this is the IPU output-format contract).
  - `engine/ipu/hle/fmv_pacer.h/.cpp` (`FmvPacer` — PaceAuthoredDelivery + census; mode comes
    from `Config.time.mode`, not env inversion).
  - FMV completion-gate hold (`HoldFmvCompletionGate`) stays with the sceMpeg surface
    (`mpeg_ipu.cpp`) — it is the libmpeg completion-visibility semantic.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | `PS2_MPEG_PACE_AUTHORED` (:459) | **inverse of `VBlankFreerunOn()`** (paced in play, free in bench) | FMV delivery paced to authored stream rate vs present-coupled legacy | experiment, delete-both (A2 row) — but the *behavior* is the shipped play path | NORM via `Config.time.mode`: paced delivery is the only play-mode path; freerun/bench mode disables it structurally (one pacing enum, no env inheritance — A2 finding 3) |
  | `PS2_PERF` (:462,:534,:598,:1200,:1365 + 2 more) | off | gate self-announce + census enable | shipping-setting | DIAG — announces die with gates; censuses ride core/diag PERF buckets |
  | `PS2_FMV_OPEN_BURST` (:532) | off | open-time delivery burst experiment | experiment, delete-both | DELETE |
  | `PS2_MPEG_PACE_OFF` (:566) | off | kill FMV pacer | tuning-knob, delete-losing-arm | DELETE (losing arm; mode enum covers it) |
  | `PS2_FMV_DECODE_CAP` (:1197) | off | cap decode-ahead depth | experiment, delete-both | DELETE |
  | `PS2_SND_FMV` (:1282) | ON | FMV audio capture into sound core | tuning-knob, delete-losing-arm | NORM (audio capture unconditional; =0 arm deleted) |
  | `PS2_MPEG_SEQCENSUS` (:1404) | off | sequence-header census | diagnostic | DELETE (one-off FMV3 probe) |
  | `PS2_MPEG_PDIFF` (:2056) | off | picture-diff probe | tuning-knob | DELETE |
  | `PS2_MPEG_WM` (:2192) | off | watermark/write-mode experiment | tuning-knob | DELETE |
  | `PS2_MPEG_DUMP_N` (:2248) | 0 | dump N decoded pictures | diagnostic | DIAG (core/diag dump family) |
- **Hacks / workarounds**:
  1. **Default-by-inversion pacing gate** (:459): `PACE_AUTHORED` defaults to
     `!VBlankFreerunOn()` — the A2 finding-3 hidden cascade (NOVSYNC→FREERUN→pace flip).
     Correct: one explicit `time.mode` enum in Config (deterministic/play/bench), validated,
     consumed by `FmvPacer`; no gate reads another gate.
  2. **Wall-clock↔tick pacing dual path** (warp::Active() switches the pacer to tick
     domain): correct home is the TimeDomain family (ARCHITECTURE §4.7) — the pacer asks the
     `EmuDomain` for its deadline source instead of consulting warp directly.
  3. **PERF-announce latches** (5 sites): the A2 finding-2 idiom. DELETE; Config dump.
  4. `[MPEG ERROR] _bstag:48` alignment warning is authentic hardware behavior (per
     runtime-environment steering) — keep, as a one-line divergence-free log.
- **Risks**: FMV pacing interacts with present pacing and STREAM.IRX audio (BUG-002/BUG-004
  family). Prove with: FMV corpus leg (picture-count-per-tick trajectory vs baseline),
  audio-hash at mixer ring for an FMV scene, and the boot-FMV coldboot gate. FFmpeg is the
  one third-party decode dependency — platform ports (Vita) need the caps note in §16.

---

### host_app/savestate.cpp (2,450 lines)

- **Purpose**: Savestate capture/restore orchestration: the section registry (30 tags —
  RAM/SPAD/META/VRAM/VU*/THRD/SEMA/EVFL/INTC/DMAC/TIMR/GSPR/GSST/VIFS/GIFS/PADS/FILE/CNTR/
  MISC/HEAP/STRM/SIFS/VU1R/SDRX/MCHL/IOPM/SNDC), quiescent-boundary capture (park/unpark
  records, per-thread ctx registration, boundary probe), load/restore with thread-resume
  orchestration, named-save requests (`RequestSaveNamed` — the primitive slowspot/triage
  ride), and the async writer handoff.
- **Public surface**: `savestate::` — `RequestSaveNamed`/`RequestSaveNamedUser`,
  `LastResult`, capture/restore entry points called at the frame boundary,
  `LoadCriticalActive()`, `RegisterThreadCtx`; consumes Serialize/Restore/Validate hooks from
  cdvd_stream, sif_rpc, libsdr, libmc, iop_stubs (forward-declared at :42-64).
- **Classification**: MIXED (A6 §7 row I): generic envelope/registry/quiescence + title-tied
  section *semantics* (IOPM carries STREAM.IRX state, STRM the CD streaming HLE, MCHL/SDRX
  lib statics). Subsystem: core/savestate.
- **Refactor destination**: SPLIT per MIGRATION §3 row I (:362):
  - `engine/core/savestate/savestate_registry.h/.cpp` (`SavestateRegistry` — SectionSpec
    registration, capture/restore walk).
  - `engine/core/savestate/quiescence.h/.cpp` (`QuiescencePoint` — park/boundary protocol;
    couples to ee/timing boundary_protocol).
  - `engine/core/savestate/save_request.h/.cpp` (`SaveRequest` — named-request surface
    consumed by core/triage per TRIAGE E15).
  - Title-tied section providers (IOPM incl. STREAM.IRX payload, STRM recv-buffer semantics
    @0x3B3000, MCHL, SDRX) → `games/<title>/savestate/` as
    `SectionProvider`s registered at boot (`TitleProfile.savestate_sections`).
  - The forward-declared serialize hooks become the `SectionProvider` interface — no more
    cross-TU forward declarations.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | `PS2_W13R7_NORESEED` (:106) | off | skip frame reseed on load (W13 experiment) | experiment, delete-both | DELETE |
  | `PS2_SAVE_PROBE` (:181) | off | boundary chain-probe dump | diagnostic | DIAG (core/diag; boundary probe useful for savestate debugging) |
  | `PS2_SAVE_SLOT` (:338; mirror with main.cpp:14271) | 0 | default save slot | tuning-knob, delete-losing-arm | SETTING → `Config.save.slot` (default 0); single latch in config loader |
  | `PS2_SND_STATEHASH` (:369) | off | hash sound core into result line | tuning-knob | DIAG (verification aid) |
  | `PS2_SAVE_WATCH` (:2159) | off | watch a section across save/load | diagnostic | DIAG |
- **Hacks / workarounds**:
  1. **Cross-TU forward-declared serialize hooks** (:42-64): implicit interface by linker.
     Correct: `SectionProvider` (name/tag/version/serialize/restore/validate) registered like
     everything else.
  2. **Park/unpark bookkeeping duplicated with scheduler state**: quiescence should consume
     the scheduler's own thread-state table (one writer), not shadow ParkRecords.
  3. Session-tagged gates ([W13R7], [W16R1], [FIX-S1]) and narrative comments — strip per
     §11 naming rules.
- **Risks**: savestate format/semantics are FROZEN (goal: anchors keep loading). Prove with:
  roundtrip determinism test (save→load→save byte-identical), the ~40 anchor savestates all
  loading, and section-CRC stability across the move. Restore-order dependencies (VRAM before
  GS state, IOPM before STREAM) must be captured in the registry ordering contract.

---

### host_app/savestate_format.h (234 lines)

- **Purpose**: The pure on-disk `.ps2st` format module: `PS2SAVE1` magic, file versions 1–4
  (2=GSST vertex latches, 3=TIMR, 4=SNDC — versions stamp only when the section is present so
  gate-off saves stay byte-identical), FileHeader/SectionHeader PODs, CRC32, 30 FourCC section
  tags, Write/ReadStateFile, PutPod/PayloadReader helpers, packed `GsVertexRec` mirror, and
  the [FIX-C1] atomic-write gate (tmp+rename).
- **Public surface**: `savestate::` format constants + `WriteStateFile`/`ReadStateFile` +
  `Crc32` + `PayloadReader`/`PutPod`/`PutBytes` + `GsVertexRec` codec. Included by
  savestate.cpp, savestate_writer, triage/slowspot capture, tests (13 includers).
- **Classification**: GENERIC-PS2 (pure container format; documented in SAVESTATE_SCHEMA.md).
  Subsystem: core/savestate.
- **Refactor destination**: `engine/core/savestate/savestate_format.h/.cpp` — VERBATIM per
  MIGRATION §1.3:81 (format frozen; PS2SAVE1 envelope named in ARCHITECTURE §4.11). D4 shape:
  the free-function group keeps a concept-named pair; `PayloadReader` is an adjacent
  ≤30-line POD-with-behavior allowed beside its owner (or gets `payload_reader.h` if the lint
  objects — minor).
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | `PS2_SAVE_ATOMIC` (:39) | **ON** | atomic tmp+rename write vs direct-write legacy | tuning-knob, delete-losing-arm | NORM — atomic write becomes the only path; `SaveAtomicOverrideForTest` and the legacy arm delete; the unit test asserts atomicity instead of exercising both gate states |
- **Hacks / workarounds**:
  1. **Conditional version stamping** (kFileVersion/Timr/Snd chosen by which gates were on):
     once TIMR/SNDC producers are NORM'd, the writer always emits the full section set and
     stamps kMaxFileVersion; the *reader* keeps accepting 1..4 (old anchors must load). The
     "stamp only if included" dance simplifies to data-driven (version = max required by
     present sections) — keep that rule, delete the gate coupling.
  2. Test override hook `SaveAtomicOverrideForTest` — TEST-ONLY, deleted with the gate.
- **Risks**: byte-format freeze. Prove with: format unit test matrix (roundtrip, truncation,
  CRC, unknown-required-section) surviving verbatim + all anchor .ps2st files loading.

---

### host_app/test_rendering_architecture.cpp (1,614 lines)

- **Purpose**: Standalone spec-era property-test program ("PCSX2-Inspired Rendering
  Architecture — Phase 1 Scanout Magnification"): replicates DISPLAY-register parsing and
  DrawScanout UV math as pure copies and property-tests them with mt19937 random inputs.
  Self-contained `int main()` (:1554); no SDL/GL/generated code.
- **Public surface**: none (own main; duplicates logic *copied* from gs.cpp /
  opengl_backend.cpp rather than linking it).
- **Classification**: GENERIC-PS2 test scaffold (scanout math). Subsystem: GS present/tests.
- **Refactor destination**: DELETE — dead code (zero CMake references anywhere; never built).
  The property ideas (DISPLAY parse round-trips, magnification UV invariants) are re-homed as
  real unit tests beside `gs/hle/present/` that link the actual class instead of a copy.
- **Flags consumed here**: none.
- **Hacks / workarounds**: the file itself is the hack — logic copied from production TUs so
  the "test" can drift from the tested code. Correct: tests link the owning class.
- **Risks**: none in deletion; the replacement tests are written against the moved classes
  (proof: present-module unit suite covers the same properties).

### host_app/test_boot_preservation.cpp (862 lines)

- **Purpose**: Spec-era preservation property tests for the boot-screen rendering fix
  (P2a-P2d: non-sprite assembly, FlushBatch snapshot, GS register parsing, XYOFFSET
  subtraction). Links gs_state.cpp + gif.cpp for real behavior; own `int main()` (:815).
- **Public surface**: none.
- **Classification**: GENERIC-PS2 test scaffold (GS state semantics). Subsystem: GS tests.
- **Refactor destination**: DELETE as a file (dead: no CMake reference). Salvage note: P2a-P2d
  are exactly the vertex-kick/flush/register-parse invariants that become named tests in
  `engine/gs/state/` per the A3 invariant checklist (§10) — port the assertions, not the file.
- **Flags consumed here**: none.
- **Hacks / workarounds**: none beyond being unbuilt.
- **Risks**: none; covered by gs/state unit tests + gs_replay corpus.

### host_app/test_fbo_persistence_preservation.cpp (692 lines)

- **Purpose**: Spec-era preservation tests for FBO-persistence rendering (links gs_state.cpp,
  gif.cpp, texture_decoder.cpp; observation-first baseline capture). Own `int main()` (:652).
- **Public surface**: none.
- **Classification**: GENERIC-PS2 test scaffold. Subsystem: GS targets/tests.
- **Refactor destination**: DELETE (dead: no CMake reference). Persistence semantics are owned
  by `gs/hle/targets/RenderTarget` tests + the byte-identity harness in the new tree.
- **Flags consumed here**: none.
- **Hacks / workarounds**: none.
- **Risks**: none.

### host_app/test_rendering_bug_condition.cpp (638 lines)

- **Purpose**: Spec-era bug-condition exploration test: encodes the sync-function stall bug
  (busy-wait loops at game addresses 0x2C1B50/0x2C1BD8/0x2C1AB0/0x2C1030/0x2C12E0 without HLE
  stubs → infinite loop, detected by fork+timeout) and the viewport-width-mismatch condition.
  Own `int main()` (:593).
- **Public surface**: none.
- **Classification**: GAME-SPECIFIC test scaffold (hardcoded EE addresses of the sceGsSync*
  family). Subsystem: GS HLE/tests.
- **Refactor destination**: DELETE (dead: no CMake reference). The underlying contract — every
  hardware-polling sce function has an HLE binding — is enforced structurally by the
  SdkBindingManifest completeness check (the sce_lib_finder route), not by address-keyed
  fork tests. The address list itself is title data already captured by the bindings.
- **Flags consumed here**: none.
- **Hacks / workarounds**: fork-based subprocess timeout harness — fine for its era, not a
  pattern to migrate.
- **Risks**: none.

### host_app/main_old.cpp (103 lines)

- **Purpose**: The Phase-1 "hello world" host main: init SDL timer, zero a CpuContext, seed
  SIF smflag 0x20000, loop `entry(ctx)` until pc==0. Historical scaffold predating the
  scheduler/threading runtime.
- **Public surface**: `int main` (conflicts with main.cpp — cannot even link into host_app).
- **Classification**: GAME-adjacent dead scaffold. Subsystem: app.
- **Refactor destination**: DELETE — MIGRATION §1.1:57 lists it verbatim as "dead scaffolds |
  delete" (zero CMake references; A2 excluded it from scans).
- **Flags consumed here**: none.
- **Hacks / workarounds**: hardcoded `smflag = 0x20000` "bypass check at 2d1c60" — an ancient
  ancestor of the SIF handshake HLE; already superseded.
- **Risks**: none.

### host_app/ps2_scheduler_old_fiber.h (309 lines)

- **Purpose**: The superseded fiber-based PS2 scheduler's header: THS_* thread states,
  `PS2Thread` TCB (full CpuContext copy + platform::FiberHandle + ready/sema wait links),
  `PS2Semaphore`, MAX_THREADS/SEMAPHORES/PRIORITIES constants. Replaced by ps2_threading.
- **Public surface**: none live — 0 includers, 0 CMake references (verified; MIGRATION
  §1.1:56 row: "superseded by ps2_threading; 0 build refs (A1 §5.1, A4 §2.3/B5) | delete").
- **Classification**: GENERIC-PS2 (dead). Subsystem: EE kernel HLE.
- **Refactor destination**: DELETE, Phase 1 (with ps2_scheduler_old_fiber.cpp — another
  shard's file; A6 notes its 66 EE-address hits drop out of every count).
- **Flags consumed here**: none.
- **Hacks / workarounds**: n/a (dead).
- **Risks**: none — no references.

---

### host_app/present_pace.h (1,146 lines)

- **Purpose**: [R193-PACE] Present-side pacing at the graphics thread's genuine-present site:
  a fixed FPS cap on an absolute-deadline slot grid (`PS2_FPS_CAP`, wins over smooth), an
  adaptive steady-cadence band selector (60/40/30 Hz with hysteresis, default OFF since
  R193b), cap-mode graceful step-down to a 30 Hz plateau under sustained overload, interval-
  fit-gated trips with bounded ratchet, state-load grace windows, FMV-decode bypass rules,
  and an interval census. Header-only: `SampleRing`, `BandSelector`, `PacePlan`, `CapStepper`,
  `LoadGrace`, the env latches, and the absolute-deadline sleep.
- **Public surface**: `namespace presentpace` — the Pacer entry called per present by
  graphics_thread.cpp; bench census emitters; 4 includers (graphics_thread, bench, main,
  quantum).
- **Classification**: GENERIC-PS2 host pacing (no game constants; band constants are display
  math). Subsystem: GS HLE present.
- **Refactor destination**: `engine/gs/hle/present/pacing.h/.cpp` per MIGRATION §1.3:115 —
  SPLIT into one-class-per-file pairs: `sample_ring.h/.cpp` (`SampleRing`),
  `band_selector.h/.cpp` (`BandSelector`), `pace_plan.h/.cpp` (`PacePlan`),
  `cap_stepper.h/.cpp` (`CapStepper`), `load_grace.h/.cpp` (`LoadGrace`), and
  `pacer.h/.cpp` (`Pacer` façade). Header-only inline state moves into the .cpp (zero
  mutable globals in headers). Deadline sleeps go through `platform/` timer primitives.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | `PS2_FPS_CAP` (:629) | 0=off (peak config sets 60) | fixed present cap in Hz, clamp [10,120] | shipping-setting, keep-as-setting | SETTING → `Config.display.fps_cap` default **60** (peak run value); 0 = uncapped |
  | `PS2_PACE_SMOOTH` (:641,:652 — 2 latches) | OFF (user rejected band quantization 2026-08-17) | adaptive band cadence | tuning-knob, delete-losing-arm | DELETE the smooth selector arm entirely (losing arm by explicit user verdict); BandSelector class survives only if CapStepper needs its hysteresis core — otherwise fold |
  | `PS2_PACE_FMV_BYPASS` (:662) | OFF | restore pre-R193c blanket FMV bypass in cap mode | tuning-knob, delete-losing-arm | NORM (R193c rule — cap holds grid during FMV unless cap < authored rate — becomes the only path; hatch deleted) |
  | `PS2_CAP_STEPDOWN` (:671) | ON | cap-mode 30 Hz plateau step-down | tuning-knob, delete-losing-arm | NORM (stepper unconditional) |
  | `PS2_PACE_LOADGRACE` (:684) | ON | load-grace window (no evidence feed during load warmup) | tuning-knob, delete-losing-arm | NORM |
  | `PS2_CAP_TRIPFIT` (:696) | ON | interval-fit-gated trips + bounded ratchet | tuning-knob, delete-losing-arm | NORM |
  | `PS2_CAP_TRIPFIT_PCT` (:707) | 110, clamp [100,200] | fit threshold percent | tuning-knob (A/B knob) | DELETE — constant `kTripFitPctDefault=110` becomes the code |
  | `PS2_CAP_UPHOLD_MS` (:722) | 2000, clamp [250,8000] | cap promotion hold | tuning-knob (A/B knob) | DELETE — constant kUpHoldNs |
  | `PS2_PERF` (:733) | off | census enable | shipping-setting | DIAG (census rides core/diag PERF) |
- **Hacks / workarounds**:
  1. **Bypass logic keyed on other subsystems' gates** (VBlankFreerunOn / warp::Active /
     `mpeg_hle::DecodeActiveRecently(250)` wall-clock sniff): pacing mode should be a single
     `time.mode` + explicit FMV-active signal published by the FmvPacer through the frame
     boundary, not cross-module gate reads (A2 finding 3).
  2. **9-env-var surface for one feature** (A2 §4 note: "needs the 9-var surface collapsed"):
     collapses to 1 Config field (fps_cap) + constants.
  3. Header-only inline atomics/state (violates zero-mutable-globals-in-headers) — fixed by
     the .h/.cpp split.
- **Risks**: pacing is the felt-experience core (BUG-002 sound desync, BUG-027 input lag);
  the depth-1 runahead audit in the header is a real contract. Prove with: present-interval
  census A/B on the slowspot anchor states (p50/p95 must match old cap-mode), input-latency
  probe (R183-INLAT) unchanged, FMV scene cadence leg.

---

### host_app/ring_log.cpp (595 lines)

- **Purpose**: [E2-LOG] the always-on binary ring log: per-thread rings with channel masks
  (normal + verbose tiers), snapshot request/done handshake, the absorbed [R187-NEWGAME]
  crash-ring C boundary for generated code (`crashring_enabled` /
  `crashring_record_indirect` / `crashring_fatal_no_jump`), and `WriteBundleTo` — the
  evidence-bundle primitive (ring.rlog + decoded ring.txt + env.txt with every PS2_* var +
  perf.txt) that slowspot and the F9 hotkey ride (TRIAGE_CAPTURE E13).
- **Public surface**: `ringlog::` (rings, masks, RegisterThread, Emit, WriteBundleTo,
  snapshot handshake) + the extern-C crashring boundary consumed by recompiled code.
- **Classification**: GENERIC-PS2 host tooling. Subsystem: core/log + core/triage.
- **Refactor destination**: `engine/core/log/ring.h/.cpp` (`RingLog` sink) — VERBATIM per
  MIGRATION §1.3:257 (ring_log family → core/log/ring.* sink + decode tool). SPLIT one piece:
  `WriteBundleTo`'s env.txt/perf.txt bundle assembly generalizes into
  `core/triage/triage_bundler.*` (TRIAGE §7: config.ini + env.txt = "the ring_log.cpp:373-385
  env capture generalized"); the crashring extern-C boundary is preserved verbatim (frozen
  generated-code seam). Channel masks become compile-time log categories where static, config
  fields where runtime.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | `PS2_LOG_CHANNELS` (:98) | all channels on | ring channel mask | diagnostic | SETTING → `Config.log.channels` (triage rings are ALL-builds product capture; a user-facing filter, not a diag build flag) |
  | `PS2_LOG_VERBOSE` (:101) | empty | verbose-tier mask (EmitV needs both masks) | diagnostic | DIAG (verbose tier = diagnostics builds; note: gates `crashring_enabled` for INDIRECT_JUMP events) |
  | `PS2_CRASHRING` (:104) | ON (=0 clears EE channel) | legacy alias killing the crash ring | diagnostic | NORM — crash ring always on (it is the core/triage flight recorder); alias deleted |
  | `PS2_LOG_RING_EVENTS` (:109) | default capacity | ring capacity (1024..4M, pow2) | diagnostic | SETTING → `Config.log.ring_events` with today's default |
- **Hacks / workarounds**:
  1. [MTVU-BP-FIX] INDIRECT_JUMP demoted to the verbose tier because 4.7K events/frame
     flushed the ring to ~8 frames depth — a rate-class fix expressed as a mask default.
     Correct: per-event-class rate budgets in the ring schema (core/log), not tier demotion
     by comment.
  2. env.txt "every PS2_* var" capture — transition-period crutch; becomes the effective
     38-field Config dump (TRIAGE §7).
- **Risks**: the crashring C boundary is a frozen seam consumed by *generated* code — the
  symbol names must survive or the recompiler emit must be regenerated in the same phase.
  Prove with: ring decode tool golden test + a triage bundle produced on the new tree
  matching the documented manifest.

### host_app/timeline.h (498 lines)

- **Purpose**: [R131-TL] frame-stage timeline census (ee2feed/feed2idle/idle2sync/push2gfx/
  present/dead/tickwait/bnd2coll stage aggregation, 1 Hz emitter, rides PS2_PERF) +
  **[R131-WAITCHAIN] `WaitFixOn()`** — the master gate for the condvar/absolute-deadline wait
  mechanisms (vs legacy poll+sleep) + [W10-EESTALL] stall-class attribution census
  (wall/cpu/pause triad per long EE episode).
- **Public surface**: `tl::` — `Enabled()`, `WaitFixOn()` (consumed by ps2_vblank/gs_hle wait
  sites — 7 includers), stage Sample hooks called from EE/VU1-worker/gfx threads,
  `tl::eestall::` hooks.
- **Classification**: GENERIC-PS2 host timing. Subsystem: core/diag + ee/timing.
- **Refactor destination**: SPLIT per MIGRATION §1.3:256 (timeline.h → core/diag):
  - Census + eestall → `engine/core/diag/frame_timeline.h/.cpp` (`FrameTimeline`) +
    `engine/core/diag/ee_stall_census.h/.cpp` (`EeStallCensus`), diag builds only.
  - `WaitFixOn()` is NOT diagnostics — it gates shipped wait mechanics. It NORMs: the
    condvar/deadline mechanisms become the only implementation inside
    `engine/ee/timing/boundary_protocol.*` and `core/threads/`; the gate and its legacy
    poll+sleep arms delete. (This is the one behavioral latch hiding in a "diagnostics"
    header — flagged so the gate purge does not sweep it as diag.)
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | `PS2_WAITFIX` (:54) | **ON** | condvar/absolute-deadline waits vs legacy sleep-quantum polls | tuning-knob, delete-losing-arm | NORM — winning mechanisms unconditional; legacy poll arms deleted at their sites (ps2_vblank, gs_hle, render queue) |
  | `PS2_PERF` (:56) | off | announce + census enable | shipping-setting | DIAG |
  | `PS2_EESTALL_CENSUS` (:133) | OFF | EE stall attribution census | diagnostic | DIAG (core/diag; feeds triage PERF evidence) |
- **Hacks / workarounds**:
  1. A behavioral master gate (`WaitFixOn`) living in a census header — home mismatch;
     corrected by the split above.
  2. `extern std::atomic<uint32_t> g_vsync_frame` re-declared here (mirror of render.h decl)
     — one declaration home after the move.
  3. Header-only inline atomic state — .h/.cpp split fixes.
- **Risks**: WAITFIX NORM touches every park/wake path (freeze-class bugs). Prove with: the
  boundary-protocol determinism test, slowspot-anchor cadence A/B, and the chunk-load freeze
  corpus leg (BUG-001).

### host_app/call_census.cpp (491 lines)

- **Purpose**: [R141-CALLCENSUS] per-recompiled-function call counters + root-segment
  wall-time attribution (dense-id tables from generated code, per-thread root-slot claim
  pattern, 2 s window dumps, top-40 report, slow-segment burst lines). The recomp coverage
  census — dependency-free so it links into host_app AND the recompiler-verify SUT runner.
- **Public surface**: `call_census.h` API (enter/exit hooks called by generated dispatch,
  init/dump); reads `g_recomp_fn_count`/`g_recomp_fn_addrs` from generated code; weak-imports
  `ps2_census_current_frame`.
- **Classification**: GENERIC-PS2 tooling (mechanism generic; ids come from whatever title's
  generated tables). Subsystem: core/diag.
- **Refactor destination**: `engine/core/diag/call_census.h/.cpp` (`CallCensus`) — VERBATIM
  per MIGRATION §1.3:255 (core/diag census family; A2 §4 keep-list "call census"). Compiled
  out of Release (PS2R_DIAGNOSTICS=OFF); the generated-code hook sites are emitted only in
  diagnostics builds (recompiler emit flag, same phase as the probe-silent gate).
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | `PS2_CALL_CENSUS` (:232) | off | enable census | diagnostic | DIAG (Config diag field in diag builds) |
  | `PS2_CALL_CENSUS_ROOT` (:235) | off | root-time attribution mode | diagnostic | DIAG |
  | `PS2_CALL_CENSUS_SLOWSEG` (:242) | off | slow-segment burst threshold (ms) | diagnostic | DIAG |
- **Hacks / workarounds**: weak-symbol frame hook (pairs with graphics_thread's strong def) —
  becomes an injected FrameClock dependency; kCap=16384 static arrays fine for a diag tool.
- **Risks**: low (diag-build only). Prove with: SUT-runner build still linking (the census is
  part of the recompiler-verify harness) + a census smoke run on one corpus case.

### host_app/stick_trace.h (414 lines)

- **Purpose**: [R2-STICK] layered stick-input trace for the "held stick degrades after 3-4 s"
  investigation: RAW/MAP/STAMP/SERVE/RATE 1 Hz lines + anomaly one-shot dump with a fixed
  sample ring, adjudicating game-rate slowdown vs served-analog degradation. stderr-only,
  default OFF, header-only by design (links into pad unit tests). Per MIGRATION §1.3:256 it
  also hosts the HEADWATCH/EULERTRACE probe family homes.
- **Public surface**: `sticktrace::` hooks — RecordRaw/RecordMap/RecordStamp (main input
  pump), ServeTick/NoteStateCall/NoteModeCall (CPU thread, libpad HLE); 6 includers
  (pad_input, input backends, tests).
- **Classification**: GENERIC-PS2 input diagnostics. Subsystem: core/diag (pad).
- **Refactor destination**: `engine/core/diag/stick_trace.h/.cpp` (`StickTrace`) — moves
  per MIGRATION §1.3:256, diag builds only. Pure cores (AnomalyCore, SampleRing, ByteMag)
  keep their unit tests. Hooks in pad/hle become diag-build-conditional probe points of the
  core/diag probe registry.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | `PS2_STICKTRACE` (:69) | OFF | the whole trace | diagnostic | DIAG |
- **Hacks / workarounds**: header-only inline statics ODR-shared across TUs (deliberate, to
  dodge CMake changes) — the .h/.cpp split + explicit test linkage replaces it.
- **Risks**: none behavioral. Prove with: pad unit tests (test_pad_buffer Phase J) passing
  against the moved pure cores.

### host_app/gs_vram_census.h (362 lines)

- **Purpose**: [GB1] exact CPU-reader census of gs_vram: every mirror consumer calls
  NoteReadSpan/NoteReadRect, every author calls AuthorCPUSpan/AuthorGPURect, pages classified
  by last author (CPU vs GPU) to prove which 8 KiB pages ever need CPU visibility — the
  Phase-B residency evidence tool. Header-only, gate-off = one branch per site event.
- **Public surface**: `vramcensus::` note/author hooks (6 includers: gs_state, backend,
  savestate, page tracker…), Site enum, shutdown dump.
- **Classification**: GENERIC-PS2 GS tooling. Subsystem: core/diag (gs/vram).
- **Refactor destination**: `engine/core/diag/vram_census.h/.cpp` (`VramCensus`) — per
  MIGRATION §1.3:152 ("gs_vram_census.h | core/diag census family — move-to-diagnostics-
  build"). Its page-geometry math (page dims per PSM) must call `gs/format/PsmLayout` instead
  of carrying its own copy once the format layer exists.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | `PS2_GS_READER_CENSUS` (:120) | OFF | off / stderr-at-exit ("1") / file path (rewritten every 120 frames) | diagnostic | DIAG |
- **Hacks / workarounds**: duplicated PSM page-geometry table (the A3 §2 four-family problem,
  fifth copy) — correct: consume PsmLayout. The header's own note that EE-visible VRAM reads
  are structurally impossible (no TRXDIR=1, no translate mapping) is a documented scope cut —
  keep as a docs/INVARIANTS entry.
- **Risks**: none (diag). Prove with: census smoke on a gs_corpus replay, page counts stable
  vs baseline.

### host_app/mipsrc_diag.cpp (277 lines)

- **Purpose**: [S100-MIPSRC] mip-source diagnostics for the CAVERN_MIPSRC session: multi-range
  write-watch ([S100-MIPWR] — MipwrInit parses PS2_MIPWR ranges, per-(who,ra,fn) dedup, EE
  call-stack lines, 2500-line cap) + mip-source table dump/RAM-dump helpers. Own TU so vif.cpp
  test binaries get symbols without dmac.cpp; weak fallbacks for `g_vsync_frame` and
  `ee_get_call_stack_top`.
- **Public surface**: `mipsrc::` (MipwrInit, MipwrNote write hook called from memory.cpp,
  g_vif_feed_ee thread-local, dump entry points).
- **Classification**: GENERIC mechanism, session-specific purpose (concluded CAVERN mip hunt).
  Subsystem: diagnostics.
- **Refactor destination**: DELETE — MIGRATION §1.1:62 lists mipsrc_diag among "One-off
  diagnostics … concluded sessions (A2 §4) | delete". The reusable idea (multi-range write
  watch + call-stack capture + dedup) survives as the generic address-hook probe kit in
  `core/diag` (A2 §4 keep-list "generic address-hook probe kit"); this instance and its
  weak-symbol shims die.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | `PS2_MIPWR` (:77) | off | hex addr:len[,...] write-watch ranges | experiment, delete-both | DELETE |
  | `PS2_MIPSRC_DIAG` (:145) | off | mip-source census | diagnostic | DELETE (one-off; probe kit is the survivor) |
  | `PS2_MIPSRC_RAMDUMP` (:234) | off | RAM dump on event | diagnostic | DELETE |
  | `PS2_MIPSRC_DUMP` (:250) | off | table dump | diagnostic | DELETE |
- **Hacks / workarounds**: weak `__attribute__((weak))` fallbacks (link-graph dodge, with an
  honest note about the 32 MB double-init landmine it avoids) — the probe kit gets real
  interfaces instead.
- **Risks**: memory.cpp's [S100-MIPWR] arm latch calls MipwrInit — remove both together.
  Prove with: full build + vif_replay link check after deletion.

### host_app/pacebrk.h (196 lines)

- **Purpose**: [W10-PACEBRK] pacing-attribution census brackets: RAII wait brackets around
  every designed wait class on the EE thread (ee_tick/ee_rq/ee_park/ee_slp) and gfx thread
  (gfx_pop/gfx_swap/gfx_cap), per-frame max-episode register, one stderr line per >33.4 ms
  frame naming the carrier wait class. Armed at frame 1990 by the gfx thread; default OFF.
- **Public surface**: `pacebrk::Scope` used at 2 includer sites (wait sites in ps2_vblank /
  render/graphics), `OnFrameBoundary`, `armed` atomic.
- **Classification**: GENERIC host diagnostics (concluded W10 hunt). Subsystem: diagnostics.
- **Refactor destination**: DELETE — MIGRATION §1.1:62 (one-off diagnostics, concluded
  sessions). Its taxonomy of designed wait classes is documentation-grade: fold the class
  list into the §7 threading ownership table / threading.md, and let core/triage PerfMonitor
  carry per-class wait attribution if ever needed again.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | `PS2_PACEBRK` (:101) | OFF | the census | experiment, delete-both | DELETE |
- **Hacks / workarounds**: hardcoded arm frame 1990 (savestate-specific window); header-only
  inline atomics. Both die with the file.
- **Risks**: none. Prove with: build after removing the two Scope sites.

---

### host_app/vif.h (349 lines)

- **Purpose**: The VIF0/VIF1 interface: state-machine enums (IDLE/WAIT_DATA/DIRECT_STREAM/
  STALL_VU), the full VIFCommand set, VIF_STAT/ERR/FBRST bit namespaces, mask actions,
  `VIFRegs`, and `class VIF` (both units: command decode, UNPACK with all formats/masks/
  cycle-stealing, DIRECT to GIF PATH2, MSCAL family) + two UNPACK fast-lane gates
  ([W10-UNPKB] bulk NEON lane for CL==WL forms, [W10-UNPKBX] strided-store lane for
  CL>WL/WL==1 skip forms) with must-execute census atomics.
- **Public surface**: `class VIF`, `VIFRegs`, `g_vif`, `w10unpkb::On`/`w10unpkbx::On` (+
  OverrideForTest hooks), census atomics. 11 includers (dmac, gif, memory, savestate, vu1
  async, tests).
- **Classification**: GENERIC-PS2 (VIF hardware semantics; no game constants). Subsystem: vif.
- **Refactor destination**: `engine/vif/vif.h/.cpp` (`Vif` — the register/state model per the
  §3 tree "vif/ VIF0/1 + UNPACK (+ MTVU submit seam)"). D4 split: `vif_regs.h` (POD +
  bitfield namespaces), `vif.h/.cpp` (the unit class), `unpack.h/.cpp` (`VifUnpack` — the
  UNPACK engine including the bulk/skip kernels as its only implementation). The gate
  namespaces and census atomics delete with NORM. `g_vif` global becomes a constructed
  module member (composition root).
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | `PS2_VIF_UNPACK_SIMD` (:40) | **ON** | bulk NEON/memcpy UNPACK lane vs per-vector scalar reference loop | tuning-knob, delete-losing-arm | NORM — bulk lane is the code; scalar reference loop survives ONLY inside tests/vif_replay as the equivalence oracle (the in-process A/B idiom, A5 §1.2) |
  | `PS2_VIF_UNPKBX` (:91) | **ON** | strided-store skip-cycle lane (82-94% of unpack QW mass) | tuning-knob, delete-losing-arm | NORM — same treatment |
- **Hacks / workarounds**:
  1. `OverrideForTest()` mutable-int test hooks in a production header — replaced by the
     equivalence suite instantiating the reference kernel directly (TEST-ONLY code lives in
     tests/).
  2. Census atomics (`g_w10_unpkb_cmds`…) as must-execute proof — the proof moves into
     vif_replay assertions (lane coverage counters scoped to the test build).
  3. Session-tag namespaces (`w10unpkb`) — naming rule violation; dissolved by NORM.
- **Risks**: UNPACK byte-identity is corpus-gated already — prove with
  `tests/vif_replay.cpp --unpack-bulk` (ctest VifUnpackBulkEquivalence) + the MTVU boundary
  interleave pass; NEON kernels need the platform/ caps note for non-ARM targets (SSE or
  scalar fallback chosen at compile time by platform, not env).

### host_app/intc.h (77 lines)

- **Purpose**: The EE interrupt controller model: IntcCause enum (15 sources), `IntcHandler`
  record, `class INTC` (atomic STAT/MASK @0x1000F000/F010, Read/Write, Enable/Disable,
  RaiseInterrupt, CheckInterrupt, DispatchInterrupt, [R195-TIM0] DispatchCause single-cause
  dispatch shared with the EE-timer boundary pump).
- **Public surface**: `INTC`, `g_intc`, `g_intc_queues` (cause→handler-list map),
  `g_nextIntcId`. 25 includers (memory MMIO, graphics thread VBLANK raise, scheduler,
  syscalls, timers, savestate…).
- **Classification**: GENERIC-PS2. Subsystem: ee/intc.
- **Refactor destination**: `engine/ee/intc/intc.h/.cpp` (`Intc`) — moves near-verbatim per
  the §3 tree ("intc/ INTC + EE timers T0-T3"). `IntcHandler` is an adjacent POD.
  `g_intc_queues`/`g_nextIntcId` extern mutable globals fold INTO the class (the A1
  zero-mutable-globals-in-headers rule); handler registration becomes a member API used by
  the syscall HLE.
- **Flags consumed here**: none.
- **Hacks / workarounds**: mutable globals in the header (queues map + id counter) — the
  exact A1 §pattern; session tag [R195-TIM0] in the comment — strip. DispatchCause's
  "dispatch iff (STAT&MASK), then ack" is correct PS2 INTC semantics — keep as the documented
  invariant.
- **Risks**: low; interrupt delivery order is savestate-visible (INTC section). Prove with:
  savestate roundtrip + the timer-interrupt unit pins (test_time_domain) + boot corpus.

### host_app/gs.h (45 lines)

- **Purpose**: GS privileged-register file (`GsRegs`: PMODE/SMODE1/2/SRFSH/SYNCH/SYNCV/
  DISPFB1/2/DISPLAY1/2/EXTBUF/EXTDATA/EXTWRITE/BGCOLOR/CSR/IMR/BUSDIR/SIGLBLID), `VideoState`
  (display mode, interlace, dimensions, refresh), and the privileged read/write API
  (Read/WritePrivilegedRegister + Upper/Lower halves, GS_Reset).
- **Public surface**: `GsRegs g_gs_regs`, `VideoState g_video_state`, the 5 free functions.
  18 includers (memory MMIO @0x12000000, graphics_thread, savestate, gs_state, syscalls
  SetGsCrt/PutIMR).
- **Classification**: GENERIC-PS2. Subsystem: gs/state (priv).
- **Refactor destination**: `engine/gs/state/gs_priv_regs.h/.cpp` (`GsPrivRegs`) — the §4.1
  `priv:` block of GsRegisters ("privileged regs, separate write path"). `VideoState`
  dissolves into it (display-mode decode belongs beside DISPFB/DISPLAY; present/scanout reads
  it via the DrawIR/present snapshot). Registered as an MmioRouter handler for
  0x12000000-0x12001FFF by gs/ at boot.
- **Flags consumed here**: none.
- **Hacks / workarounds**: `GetWidth()`'s pointless ternary (`==0x03 ? 640 : 640`) — PAL/NTSC
  width identical by accident of this game; encode the real CRTC rule (width from DISPLAY
  magh/DW) in the scanout math instead of a constant. Duplicate `extern VideoState
  g_video_state;` declaration (:42 and :45) — trivial cleanup. NTSC default hardcoded (fine:
  SLUS-12345 is NTSC, but the default should come from SMODE1 writes / TitleProfile, not a
  struct initializer).
- **Risks**: GSPR savestate section shape. Prove with: savestate roundtrip + gs_replay
  scanout comparisons (DISPFB/DISPLAY parsing pinned by the present tests).

### host_app/sif.cpp (19 lines)

- **Purpose**: One helper: `SifCompletePendingRpcs()` — walks `g_sif.pending_rpcs`, signals
  each active completion semaphore via `ps2_threading::iSignalSema`, clears the list. The
  SIF RPC completion pump (rest of SIF lives in sif.h/sif_hle/sif_rpc_handlers — other
  shards).
- **Public surface**: `SifCompletePendingRpcs()` (called from the scheduler/boundary pump).
- **Classification**: GENERIC-PS2 (sceSif RPC completion semantics). Subsystem: sif.
- **Refactor destination**: `engine/sif/hle/rpc_router.cpp` — MERGED into the RPC router
  class (§3 tree: "sif/ mailbox regs + RPC router; hle/ = sceSif surface + module routing").
  A 19-line orphan TU should not survive as a file; the function becomes
  `RpcRouter::CompletePending()`.
- **Flags consumed here**: none.
- **Hacks / workarounds**: unconditional `g_logFile <<` per completion (hot-ish path) —
  becomes a SIF log category. "Auto-signal every pending RPC" is an HLE simplification of
  real SIF DMA completion — document as `// diverges:` per §11 if kept.
- **Risks**: none beyond RPC ordering; proven by the STREAM.IRX/loader corpus leg (RPC
  completion drives stream state advance).

### host_app/iop_stubs.h (192 lines)

- **Purpose**: Declarations for the HLE stubs of the 12 IOP libraries STREAM.IRX imports
  (sysmem/intrman/ioman/sifcmd/sifman/stdio/sysclib/thbase/thsemap/timrman/libsd/cdvdman),
  each `void f(IopContext&)` per the real export-table ordinals ([S43]/[S44] corrections),
  plus the guest-monitor protocol (Acquire/Release/SwapCurrentIopThreadId,
  `GuestMonitorYieldScope` RAII), deferred CD-completion helpers (RaiseStreamCdCompletion,
  CdEvtPending, StopCdIrqThread), [STREAM-RPC-HLE] `hle_stream_cd_read_rpc` (the synthesized
  CD-read RPC reply at IOP 0x128a8), and [S29-SHUTDOWN] StopAllThreads.
- **Public surface**: `iop_stubs::` — consumed by the IOP recompiler output (stub thunks),
  sif_rpc_handlers, the scheduler shutdown path. 5 includers.
- **Classification**: MIXED (A6 §7 row G): Sony-API kernel stubs = GENERIC; the STREAM.IRX
  worker/callback conventions and `hle_stream_cd_read_rpc` (hard IOP addresses 0x128a8 /
  0x15310 / 0x14a00, FUN_0000e278 coupling) = GAME. Subsystem: iop/hle.
- **Refactor destination**: SPLIT per MIGRATION §3 row G (:356):
  - Sony stubs + guest monitor + timer/CD worker machinery → `engine/iop/hle/modules.h/.cpp`
    (MIGRATION says verbatim; under D4 pressure, one file per module —
    `engine/iop/hle/thbase.h/.cpp`, `thsemap`, `sifcmd`, `sysclib`, `timrman`, `cdvdman`,
    `libsd_stub`, … — propose the per-module shape as the D4-conformant refinement).
  - `hle_stream_cd_read_rpc` + STREAM.IRX stack/callback conventions →
    `games/<title>/iop/stream_irx/` glue (registered via
    `TitleProfile.iop_modules.custom`).
- **Flags consumed here**: none read in this header. Comments reference `PS2_SNDREG`
  (default ON — bare SignalSema CD nudge vs legacy S41/S42 flag-planting; NORM: legacy arm
  deletes) and `PS2_SHUTDOWN_WAIT` (bounded-shutdown unwind) — both latched in iop_stubs.cpp
  (that shard's ledger rows).
- **Hacks / workarounds**:
  1. **Address-bound RPC body** (`hle_stream_cd_read_rpc` "emitted as a call at pc 0x128a8 by
     the IOP recompiler"): a game-address hook in engine-side declarations. Correct: the IOP
     recompiler consumes it as a *title plugin* patch row (IopPlugin_StreamIrx), keeping the
     D2 rule (engine never binds a game address).
  2. Old wrong-name stubs kept "harmless, unit-tested" beside the [S44]-corrected ones
     (iop_memcmp/prnt/strchr/index/strrchr) — delete the dead aliases once the corrected
     ordinal map is the only map.
  3. libsd as 16 numbered no-op stubs — fine as stubs; name them by export ordinal for the
     divergence census.
- **Risks**: STREAM.IRX audio is the one custom IOP module (runtime-environment ground
  truth); the monitor/yield protocol is deadlock-prone territory (BUG-005 livelock family).
  Prove with: stream corpus leg (cutscene audio advance, done_flag chain), shutdown
  clean-exit test, savestate IOPM section roundtrip.

### host_app/timer_hle.h (10 lines)

- **Purpose**: Declares `hle_TimerWaitMicroseconds(CpuContext&)` — the HLE replacement for
  the game function at 0x191BD0 ("TimerWaitMicroseconds"): the original busy-waits on
  T0_COUNT and deadlocks under fiber scheduling; the HLE yields to the scheduler then
  returns 1.
- **Public surface**: one function; installed against the game address by main.cpp's hook
  machinery; 1 includer.
- **Classification**: GAME-SPECIFIC binding of a GENERIC idea (spin-wait → scheduler yield).
  Subsystem: ee/hle (timing). NOTE: 0x191BD0 is the same function the budget predicate
  emulates (A6 row E — FUN_00191bd0 is the frame-budget/timer-wait predicate); this header is
  the older HLE face of that same site, now paired with the [S96-BUDGETEMU] emu-domain hook.
- **Refactor destination**: MIGRATION §1.3:179 says `ee/hle/` (sce timer/alarm HLE) verbatim —
  but under D2 the *binding* cannot stay address-pinned in engine code: the yield-on-timer-
  wait mechanism goes to `engine/ee/hle/timer_wait.h/.cpp` (`TimerWaitHle`), and the 0x191BD0
  attachment becomes a `TitleProfile.hle` row (`games/…/hle/budget.cpp` family, same
  registration as loader_sync/budget per A6 row E). If the ee/timing rework makes T0 real
  (memory.cpp hack 2 root fix), this HLE may retire entirely — record as a
  HOOK_RECLASSIFICATION-adjudicated row.
- **Flags consumed here**: none.
- **Hacks / workarounds**: the file IS a workaround declaration (busy-wait deadlock dodge).
  The PS2 rule: T0 counts and its compare/overflow IRQ fires, so the spin terminates
  naturally; owning classes `EeTimers` + `Intc` + scheduler preemption at the boundary.
- **Risks**: removing it before the timer rework re-introduces the boot deadlock. Prove
  with: boot-to-menu corpus leg + test_time_domain pins.

### host_app/budget_math.h (156 lines)

- **Purpose**: [S96-BUDGETEMU] the pure arithmetic of the game's FUN_00191bd0 frame-budget
  predicate, byte-for-byte from the shard_009 emission: `ThreshCounts` (PS2-clamped float
  mul + saturating cvt.w.s), `PeriodCounts` (divu semantics incl. rt==0 → LO=-1),
  `FoldLiteral` (the decomp's fold loop verbatim) + `FoldClosed` (O(1) equivalent with proven
  domain), `HeadroomEmu` (the full sltu predicate incl. the 78-count boundary-guard sliver),
  and [S112-BOOTPHASE] `PhaseClassifier` (low-draw frame detection selecting base_low vs
  base_hi for the mode-1 synthetic budget base — the cold-boot black-screen fix).
- **Public surface**: `budgetmath::` pure functions + PhaseClassifier; consumed by the
  main.cpp 0x191bd0 dual hook (emu-domain path) and `tests/test_time_domain.cpp`. 2 includers.
- **Classification**: GAME-SPECIFIC (A6 §4 row: "Budget predicate … GAME, 156 ln"): it IS a
  game function's arithmetic; the fold/classifier mechanisms are generic. Subsystem:
  ee/timing (emu domain).
- **Refactor destination**: SPLIT per MIGRATION §3 row E (:351):
  - Mechanism — `DerivedClocks.budget_anchor` arithmetic + boot-phase classifier →
    `engine/ee/timing/budget_clock.h/.cpp` (`BudgetClock`) and
    `engine/ee/timing/phase_classifier.h/.cpp` (`PhaseClassifier`), parameterized.
  - Model params (rate=0x8CA00, fps=60, phase=0x4E, bases 8400–9700/8800, lowdraw thresholds,
    the FUN_00191bd0 shape itself) → `TitleProfile.quirks.budget` via
    `games/<title>/hle/budget.cpp`.
  - `FoldLiteral` (the verbatim reference) → the unit test as oracle; `FoldClosed` ships.
- **Flags consumed here**: none (PS2_BUDGET_EMU/PS2_BUDGET_BASE latch in main.cpp — that
  shard's rows; per the peak-config rule BUDGET_EMU is NORM and the base values become
  TitleProfile data).
- **Hacks / workarounds**: none illegitimate — this is deliberate guest-arithmetic emulation
  with documented PS2 float/divu semantics (ps2_float_clamp, LO=-1). The tuned magic bases
  (8800 phantom render cost, [BH13] operating point) are title data, not engine constants —
  that is the move. The "PS2 single mul then saturating truncate" belongs in the shared
  vu_float/ps2-float helper family (engine/vu/vu_float per A6 row A) — consume, don't copy.
- **Risks**: budget behavior gates loading pumps (BUG-019 black boot, BUG-013 budget phase).
  Prove with: tests/test_time_domain.cpp pins (they exercise the shipped code by design) +
  coldboot luma gate + cavern slowspot cadence leg.

### host_app/ps2_argv.h (115 lines)

- **Purpose**: [S8-ARGV] retail-faithful PS2 boot argc/argv builder: retail token
  (`cdrom0:\SLUS_123.45;1`, PINE-captured ground truth), the legacy 2-token host argv
  (pre-fix hybrid boot that broke character shadows), spec resolver, and the byte-exact
  `Layout` write plan for the guest argv block (argc word, 7-slot pointer array, packed
  strings) shared by both writers (0x30a980 pre-seed + InitMainThread args).
- **Public surface**: `ps2_argv::` — kRetailArgv0, resolve_tokens, build_layout, layout
  constants. Pure logic (no memory::, no globals), unit-pinned by tests/test_ps2_argv.cpp.
  3 includers (main.cpp, ps2_threading, test).
- **Classification**: GAME-SPECIFIC data + GENERIC layout builder (A6 §4: S8-ARGV row, GAME,
  115 ln). Subsystem: ee/hle boot environment.
- **Refactor destination**: SPLIT per MIGRATION §1.3:133 + §3 row E (:352):
  - `engine/ee/hle/boot_env.h/.cpp` (`BootEnv`) — the kernel boot-environment writer
    (layout builder, HOOK_RECLASSIFICATION §3.7 "S8-ARGV = kernel boot environment").
  - argv string + args_base 0x30a980 → `TitleProfile.boot` ({argv, args_base}).
  - `legacy_tokens()` and the free-form experiment splitter → DELETE (losing arm; the
    resolver collapses to "write the profile's argv").
- **Flags consumed here**: none directly (`PS2_ARGV` is read by main.cpp and passed in as
  `spec`; its A2 disposition delete-losing-arm → NORM retail; noted here because this header
  owns both arms).
- **Hacks / workarounds**: the 6-arg cap silently truncates (fine — fixed guest layout, keep
  the documented 7-slot rule). The retail-vs-legacy dual vocabulary dies with the gate.
- **Risks**: boot-path fidelity (the batch token drives FUN_00203f88 character loading —
  the S8 shadow defect). Prove with: tests/test_ps2_argv.cpp retail vector pin + boot corpus
  leg reaching gameplay with shadows bound.

### host_app/warp.h (124 lines)

- **Purpose**: [R156-WARP] jump-to-window warp: frames [0,N) run freerun-tick with presents
  skipped (GS/FBO/VRAM state still builds exactly), FMV pacing switches to tick domain, then
  at frame N presents re-enable and wall-clock pacing resumes. CLI `--warp-to-frame N` (wins)
  or env `PS2_WARP_TO=N`. Bench-window validation (`--bench-window-start >= N`).
- **Public surface**: `warp::` — Active() (hot: vblank ticker, gfx VSync case, FMV pacer),
  TargetFrame(), OnVsyncFrame(), ParseArgs(). 5 includers.
- **Classification**: GENERIC-PS2 harness facility. Subsystem: ee/timing (pacing family) +
  verify harness.
- **Refactor destination**: `engine/ee/timing/warp.h/.cpp` (`Warp`) per MIGRATION §1.3:115
  (the ee_timers/vblank/quantum/turbo/warp/ee_pace/present_pace family → ee/timing +
  gs/hle/present). Consumed by the TimeDomain (freerun selection) and Presenter (skip) —
  its cross-module reach becomes two explicit queries on the boundary protocol. Arg parsing
  moves to app/host CLI handling; the value lands in Config.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | `PS2_WARP_TO` (:102) | unset=off | warp target frame (CLI flag wins) | shipping-setting, keep-as-setting | SETTING → `Config.harness.warp_to_frame` (default 0=off); primarily a verify/bench input — keep in the dev/harness section of the schema |
- **Hacks / workarounds**: stderr announces with session tags — strip. The "OnVsyncFrame
  BEFORE the present-skip read so frame N presents" ordering contract must be stated on the
  boundary protocol, not in a header comment.
- **Risks**: none in play mode (latched no-op). Prove with: a warp-to-N harness run
  frame-hash-equal at frame N vs a paced run (the existing warp equivalence claim).

### host_app/w10_eefeed.h (55 lines)

- **Purpose**: [W10-EEFEED] one gate (`PS2_EEFEED`, default ON) covering two EE serial-feed
  optimizations implemented elsewhere: B1 — dmac VIF1 chain-walk submit *batching* into one
  MTVU ring segment per flush (span boundaries + TTE brackets preserved, order-exactness
  proven by vif_replay --mtvu); B2 — gif_parse whole-row consume loop for all-known no-A+D
  PACKED layouts (validated once at tag accept). Plus `OverrideForTest`.
- **Public surface**: `w10eefeed::On()` — consumed by dmac.cpp + gif.cpp (4 includers).
- **Classification**: GENERIC-PS2 perf gate. Subsystem: dmac/gif (mechanically); the header
  itself is pure gate.
- **Refactor destination**: DELETE via purge-dissolve per MIGRATION §1.3:135 ("EEFEED is an
  A1 §5.3 expired hatch"): the batched submit and row-loop arms become the ONLY code in
  `engine/dmac/chain_walker.*` and `engine/gif/giftag_parser.*`; the header, gate, and the
  legacy per-span/per-QW arms delete.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | `PS2_EEFEED` (:50) | **ON** | batched feed + row loop vs verbatim legacy | tuning-knob, delete-losing-arm | NORM (winning arms unconditional; header deleted) |
- **Hacks / workarounds**: one gate steering two unrelated subsystems (a "master → sub-gate"
  A2 §2.2 shape) — dissolves with NORM. The honest rejected-candidate memo (chain-tag memo
  rejection math) is history — already recorded in W10_IMPL.md; do not migrate the comment.
- **Risks**: MTVU marker ordering. Prove with: vif_replay --mtvu boundary interleave pass +
  gif corpus byte-identity.

---

### host_app/vu_aot_programs.cpp (135 lines)

- **Purpose**: The VU1 AOT tier's runtime anchor TU: gate accessors (`VuAotEnabled` —
  PS2_VU_AOT default ON per [W12R2]; `VuAotSkelFullOn` — default OFF; `VuAotSpanFoldOn` —
  default ON per [PERF1-S]), the optional profile-counter registry, the textual include of
  the generated `vu_aot_generated.inc` master registry, `VuAotLookupByHash`, and the GUI
  status accessor `VuAotRegistryCount`.
- **Public surface**: the three gate functions + lookup + count; consumed by vu.cpp dispatch
  and the GUI status panel. Built into the `vu_aot_lib` static lib (CMake: sources moved to
  the shared VU_AOT_TU_SOURCES list).
- **Classification**: GENERIC-PS2 mechanism over GAME-generated content (the .inc payloads
  are generated from SLUS-12345 microcode — A6 §1.2 note). Subsystem: vu/tiers.
- **Refactor destination**: **DELETE** — the entire AOT tier is retired per the NOAOT
  decision record (ARCHITECTURE §3 tree: "tiers/ … AOT deleted (NOAOT)"; §8: schema minus
  `recomp.vu_aot`; MIGRATION §1.1:59: "VU-AOT tier: vu_aot_gen.cpp, 33 chunk TUs,
  vu_aot_registry consumers, 6 ctest gates, 233 MB inc + 351 MB lib | default-OFF per NOAOT
  decision record | delete"). The shipping VU1 path is HLE-DIRECT (peak config) over the
  cached interpreter; AOT's job is gone.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | `PS2_VU_AOT` (:37) | ON (in this TU) | AOT tier dispatch vs cached-only | proven-fix-hatch-ON, delete-losing-arm (A2 baseline) | DELETE — superseded by the NOAOT decision record (A1 §5.4/A4 §3.2): both arms go because the *tier* goes; the cached interpreter + VU1-HLE ladder is the norm |
  | `PS2_VU_AOT_SKELFULL` (:47) | OFF | restore inert per-bundle VF0/VI0 stores | experiment, delete-both | DELETE |
  | `PS2_VU_AOT_SPANFOLD` (:70) | ON | span-folded bodies vs per-bundle emission | proven-fix-hatch-ON | DELETE (tier gone) |
  | `PS2_PERF` (:72) | off | gate announce | shipping-setting | DELETE with the TU |
- **Hacks / workarounds**: the [W12R2] comment narrating the -O0 build-artifact autopsy is
  session history (docs/loop_state keeps it); `#include "vu_aot_generated.inc"` is the
  textual-include pattern D4 bans (moot — deleted); `VuAotRegistryCount` living here because
  of internal-linkage generated statics is the tail wagging the dog.
- **Risks**: deleting the tier removes a perf floor for any image the HLE library declines
  AND the cached tier runs slowly on — the NOAOT record's accepted trade. Prove with:
  vu1_replay full-catalog FAIL=0 on HLE entries, cached-tier corpus ns/bundle within the
  NOAOT-record envelope, and the [GD1] empty-profile interpreter correctness leg.

### host_app/vu_aot_programs_5.cpp / _11 / _17 / _22 / _28 (.cpp ×5, 17 lines each)

- **Purpose**: [R158-TUSPLIT]/[W12R2] fixed checked-in chunk aggregator TUs (5, 11, 17, 22,
  28 of K=24+ naming): each does the same include dance (vu.h, vu_aot_registry.h,
  vu_decode_slot.h, vu_fast_exec.h) and textually includes its generated
  `vu_aot_generated_<k>.inc` chunk (emitted as a valid empty TU when programs run out, so
  CMake's source list stays static — the R126-SHARD principle). Exposes
  `g_vu_aot_chunk_descs_<k>` aggregated by the master registry.
- **Public surface**: the chunk desc array (external linkage), consumed by
  vu_aot_programs.cpp static init.
- **Classification**: GENERIC scaffolding for GAME-generated payloads. Subsystem: vu/tiers.
- **Refactor destination**: **DELETE** — same NOAOT row as vu_aot_programs.cpp (MIGRATION
  §1.1:59 explicitly counts the chunk TUs). No replacement: generated code in the new tree
  lives under `games/<title>/gen/` and the only generated VU payloads that remain are the
  VU1-HLE library's (which are hand-curated source, not AOT emission).
- **Flags consumed here**: none.
- **Hacks / workarounds**: the `.inc` textual-include pattern (D4's named counterexample —
  "the 222 fun_*_body.inc … both populations already retire under D1/D3/D5"); fixed empty-TU
  placeholders to freeze a CMake list. Both die with the tier.
- **Risks**: none beyond the tier-level risk above; CMake VU_AOT_TU_SOURCES and the
  [VU-AOT-GATE] POST_BUILD + VuAotLibOptimized ctest gates delete in the same commit.

### host_app/vu_function_registry.h (80 lines)

- **Purpose**: `vu_registry` — names and classifies VU0/VU1 microprograms by entry PC:
  `Descriptor` {vu_id, entry_pc, name, Kind (HLE/Interpreted/Unimplemented), 16-byte
  fingerprint checked at runtime}, two tables (top-level VCALLMS/MSCAL entries vs internal
  JALR/JR subroutine targets), lookup APIs, and iteration for boot diagnostics. Serves
  human-readable traces, HLE fast-path flagging (vu.cpp checks Kind::HLE before the
  interpreter), and unknown-program triage lines.
- **Public surface**: `vu_registry::{lookup, lookup_subroutine, all_entries,
  all_subroutines, Descriptor, Kind}`; tables live in the .cpp (other shard). 2 includers
  (vu.cpp, boot diag).
- **Classification**: MIXED — generic registry mechanism; the .cpp's entry tables (names,
  PCs, fingerprints) are this game's microprograms. Subsystem: vu/hle.
- **Refactor destination**: MERGE-SPLIT per MIGRATION §1.3:210 ("merged into
  vu/hle/registry.*"): the mechanism folds into `engine/vu/hle/registry.h/.cpp`
  (`Vu1HleRegistry` — which already owns hash-keyed entries per §14.2); the PC/fingerprint
  naming tables become part of `games/<title>/vu1_library/` registration
  data (names double as the RendererProfile stamp vocabulary). The `Kind` triage taxonomy
  survives as registry states; fingerprint checks fold into the variant-hash tiering
  (§14.2's image_hash/variant_hash already subsumes "did the game upload a different
  program here").
- **Flags consumed here**: none.
- **Hacks / workarounds**: PC-keyed identity ("every MSCAL technically starts at 0x0") is the
  weaker predecessor of the image/variant-hash keying — the merge retires PC-keying except
  as start_pc metadata. Duplicate registry concept beside vu1_hle's (two lookup systems for
  one question) — the merge is the fix.
- **Risks**: trace/HLE dispatch parity during the merge. Prove with: vu1_replay --catalog
  over the merged registry (137 entries / 120 hashes intact) + a boot trace diff.

### host_app/vu_hash_fnv.h (50 lines)

- **Purpose**: [R152-VUAOT] `VuHashImageFastRaw` — the 4-way interleaved FNV-1a content hash
  over VU micro-memory images (4 independent lanes for multiplier ILP, 8-byte tail loop,
  lane-fold finish). Shared verbatim by the build-time AOT generator and the runtime cached
  tier so registry keys can never diverge.
- **Public surface**: one inline function; 3 includers (vu.cpp, vu_aot_gen, tests).
- **Classification**: GENERIC-PS2 (pure hash). Subsystem: core.
- **Refactor destination**: `engine/core/hash.h/.cpp` — VERBATIM per MIGRATION §1.3:212
  ("vu_span_classify.h, vu_hash_fnv.h | vu/ · core/hash.h — verbatim"). Rename to a concept
  name (`Fnv1aInterleaved` / `HashVuImage`); the AOT consumer disappears (NOAOT) but the
  cached tier and the VU1-HLE variant-hash tier keep it as THE image key. The serial-chain
  `VuHashImage` in vu.cpp (keying the FASTPACK=0 verbatim path) collapses to this one hash
  when that gate NORMs — one image hash, one home.
- **Flags consumed here**: none.
- **Hacks / workarounds**: none — clean, documented. The "two hash algorithms keyed to two
  gate arms" situation it sits inside is the hack; resolved by the FASTPACK NORM (other
  shard's ledger row).
- **Risks**: hash-key stability = savestate/cache compatibility for the VU1 registry. Prove
  with: vu1_replay catalog hit-rate unchanged (120/120 variant hashes resolve) after the
  rename.

---

## SHARD SUMMARY

Shape legend: V=verbatim move, S=split, M=merged into existing target, D=deleted.
Flags column: total vars read here → NORM/SETTING/DIAG/TEST/DELETE.

| file | lines | class | destination | shape | flags (N→No/Se/Di/Te/De) | hacks |
|---|---:|---|---|---|---|---:|
| memory.cpp | 5,373 | MIXED (row H) | engine/ee/mem/{address_map,mmio_router,fastmem,elf_loader} + TitleProfile.elf + core/diag probes + KEY-REDIRECT quarantine | S | 2 → 0/0/0/0/2 | 6 |
| graphics_thread.cpp | 4,341 | MIXED (row C) | engine/gs/hle/{queue/queue_consumer,present/presenter,present/composite_guard} + renderer_profile vram + core/{diag,triage} | S | 29 → 3/0/14/3/9 | 6 |
| mpeg_hle.cpp | 2,520 | GENERIC-PS2 | engine/ipu/hle/{mpeg_ipu,mpeg_context,pss_demux,ffmpeg_decoder,frame_writer,fmv_pacer} | S | 10 → 2/0/2/0/6 | 4 |
| savestate.cpp | 2,450 | MIXED (row I) | engine/core/savestate/{savestate_registry,quiescence,save_request} + games/…/savestate/ providers | S | 5 → 0/1/3/0/1 | 3 |
| test_rendering_architecture.cpp | 1,614 | GENERIC (dead) | — (properties re-homed as gs/hle/present unit tests) | D | 0 | 1 |
| present_pace.h | 1,146 | GENERIC-PS2 | engine/gs/hle/present/{pacer,sample_ring,band_selector,pace_plan,cap_stepper,load_grace} | S | 9 → 4/1/1/0/3 | 3 |
| test_boot_preservation.cpp | 862 | GENERIC (dead) | — (invariants → gs/state named tests) | D | 0 | 0 |
| test_fbo_persistence_preservation.cpp | 692 | GENERIC (dead) | — (→ gs/hle/targets tests) | D | 0 | 0 |
| test_rendering_bug_condition.cpp | 638 | GAME (dead) | — (contract → SdkBindingManifest completeness check) | D | 0 | 1 |
| ring_log.cpp | 595 | GENERIC-PS2 | engine/core/log/ring.* (verbatim) + bundle assembly → core/triage/triage_bundler | S | 4 → 1/2/1/0/0 | 2 |
| timeline.h | 498 | GENERIC-PS2 | core/diag/{frame_timeline,ee_stall_census}; WAITFIX arm → ee/timing/boundary_protocol (NORM) | S | 3 → 1/0/2/0/0 | 3 |
| call_census.cpp | 491 | GENERIC-PS2 | engine/core/diag/call_census (verbatim, diag builds) | V | 3 → 0/0/3/0/0 | 1 |
| stick_trace.h | 414 | GENERIC-PS2 | engine/core/diag/stick_trace | V | 1 → 0/0/1/0/0 | 1 |
| gs_vram_census.h | 362 | GENERIC-PS2 | engine/core/diag/vram_census (page math via gs/format/PsmLayout) | V | 1 → 0/0/1/0/0 | 1 |
| vif.h | 349 | GENERIC-PS2 | engine/vif/{vif,vif_regs,unpack} | S | 2 → 2/0/0/0/0 | 3 |
| ps2_scheduler_old_fiber.h | 309 | GENERIC (dead) | — (superseded by ps2_threading; 0 refs) | D | 0 | 0 |
| mipsrc_diag.cpp | 277 | GENERIC (one-off) | — (mechanism idea → core/diag probe kit) | D | 4 → 0/0/0/0/4 | 1 |
| savestate_format.h | 234 | GENERIC-PS2 | engine/core/savestate/savestate_format (format frozen) | V | 1 → 1/0/0/0/0 | 2 |
| pacebrk.h | 196 | GENERIC (one-off) | — (wait-class taxonomy → threading.md) | D | 1 → 0/0/0/0/1 | 2 |
| iop_stubs.h | 192 | MIXED (row G) | engine/iop/hle/ per-module stubs + games/…/iop/stream_irx/ glue | S | 0 | 3 |
| budget_math.h | 156 | GAME (row E) | engine/ee/timing/{budget_clock,phase_classifier} + TitleProfile.quirks.budget | S | 0 | 1 |
| vu_aot_programs.cpp | 135 | GENERIC over GAME payload | — (NOAOT: tier deleted) | D | 4 → 0/0/0/0/4 | 3 |
| warp.h | 124 | GENERIC-PS2 | engine/ee/timing/warp | V | 1 → 0/1/0/0/0 | 1 |
| ps2_argv.h | 115 | GAME (row E) | engine/ee/hle/boot_env + TitleProfile.boot | S | 0 | 1 |
| main_old.cpp | 103 | dead scaffold | — | D | 0 | 1 |
| vu_function_registry.h | 80 | MIXED | merged into engine/vu/hle/registry + games/…/vu1_library data | M | 0 | 2 |
| intc.h | 77 | GENERIC-PS2 | engine/ee/intc/intc (globals fold into class) | V | 0 | 1 |
| w10_eefeed.h | 55 | GENERIC-PS2 | — (purge-dissolve: arms become dmac/gif code) | D | 1 → 1/0/0/0/0 | 1 |
| vu_hash_fnv.h | 50 | GENERIC-PS2 | engine/core/hash | V | 0 | 0 |
| gs.h | 45 | GENERIC-PS2 | engine/gs/state/gs_priv_regs (VideoState dissolves in) | S | 0 | 2 |
| sif.cpp | 19 | GENERIC-PS2 | merged into engine/sif/hle/rpc_router::CompletePending | M | 0 | 1 |
| vu_aot_programs_5.cpp | 17 | scaffolding (AOT) | — (NOAOT) | D | 0 | 2 |
| vu_aot_programs_11.cpp | 17 | scaffolding (AOT) | — (NOAOT) | D | 0 | 2 |
| vu_aot_programs_17.cpp | 17 | scaffolding (AOT) | — (NOAOT) | D | 0 | 2 |
| vu_aot_programs_22.cpp | 17 | scaffolding (AOT) | — (NOAOT) | D | 0 | 2 |
| vu_aot_programs_28.cpp | 17 | scaffolding (AOT) | — (NOAOT) | D | 0 | 2 |
| timer_hle.h | 10 | GAME binding | engine/ee/hle/timer_wait + TitleProfile.hle row (0x191BD0) | S | 0 | 1 |
| **Total** | **24,607** | | | | **81 vars → 15 NORM / 5 SETTING / 28 DIAG / 3 TEST / 30 DELETE** | **68** |

Shape counts: 10 split, 7 verbatim, 2 merged, 12 deleted (incl. 5 AOT chunks + 6 dead
scaffolds + 1 purge-dissolve header + 3 one-off diagnostics), 6 dead-code files.

## TREE-GAPs

1. **`engine/gs/hle/present/composite_guard`** — the ARCHITECTURE §3 tree names
   `gs/hle/present/` (scanout composition, present policy + pacing) but no class owns the
   cross-target composite-freshness rule graphics_thread.cpp implements (drop composites
   sampling an incomplete source RT). Propose `composite_guard.h/.cpp` under
   `engine/gs/hle/present/`, driven by RenderTarget/VramImage epochs + RendererProfile.vram.
   If review decides the epoch machinery in §4.3/§4.4 already subsumes it, the entry
   collapses into `targets/render_target.*` — either way the tree should name the home.
2. **`engine/ee/mem/elf_loader`** — ARCHITECTURE §14.1 says the "memory ELF loader …
   deletes memory.cpp's hardcoded path + section table" but the tree's `ee/mem/` line lists
   only "address map, fastmem, MMIO router". Propose `elf_loader.h/.cpp` as the fourth
   ee/mem member (consumes TitleProfile.elf/recomp).
3. **`engine/ee/hle/timer_wait`** — MIGRATION §1.3:179 sends timer_hle to `ee/hle/`
   verbatim, but the tree does not name a timer/alarm HLE file and the 0x191BD0 binding must
   become a TitleProfile.hle row under D2 (A6 row E names only loader_sync and budget as
   title HLE modules — the timer-wait face of 0x191bd0 should be added to that row's list or
   explicitly retired by the ee/timing root fix).
4. Minor: `Config.harness.*` section (warp_to_frame) — §4.8's 27-field schema should state
   where harness-only settings live (schema field vs verify-harness-only override), since
   PS2_WARP_TO is A2 keep-as-setting but is meaningless in a player build.

## Dead code (evidence)

| file | evidence |
|---|---|
| test_rendering_architecture.cpp | zero references in any CMakeLists.txt/*.cmake (repo-wide grep); own `int main()`; never built |
| test_boot_preservation.cpp | same — zero CMake references |
| test_fbo_persistence_preservation.cpp | same — zero CMake references |
| test_rendering_bug_condition.cpp | same — zero CMake references |
| main_old.cpp | zero CMake references; second `int main`; MIGRATION §1.1:57 "dead scaffolds — delete"; A2 scan excluded it |
| ps2_scheduler_old_fiber.h | 0 includers (repo grep), 0 CMake references; MIGRATION §1.1:56 "superseded by ps2_threading; 0 build refs (A1 §5.1)" |

Additionally retired-by-decision (built today, deleted by plan): vu_aot_programs.cpp + the 5
chunk TUs (NOAOT record; CMake vu_aot_lib + [VU-AOT-GATE] ctest gates delete with them),
w10_eefeed.h (expired hatch — purge-dissolve), pacebrk.h + mipsrc_diag.cpp (concluded
one-off diagnostics, MIGRATION §1.1:62).
