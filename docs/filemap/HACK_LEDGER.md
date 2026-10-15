# HACK LEDGER — consolidated across all nine filemap shards

Every hack/workaround inventoried by the FILEMAP_* shards, merged and grouped by kind.
Each entry: source (file:line where the shards cited one) — what it is — the PS2 rule /
correct form — the owning class or module in the new tree. Entries marked **KEEP** are
documented divergences or real rules that survive (with a `// diverges:` or invariant
comment), not deletions. Full prose per entry lives in the shard FILEMAP docs; this is
the merged catalog the purge/migration phases check off.

## Counts per kind

| # | Kind | Entries | Dominant fate |
|---|---|---:|---|
| 1 | Scene constants (frame windows, scene latches) | 20 | DELETE with probes; scene identity → TitleProfile/RendererProfile |
| 2 | Game addresses in engine code | 43 | → TitleProfile / RendererProfile.VramLayout / SdkBinding manifest data |
| 3 | Dual paths (fast/legacy, dual engines, dual bodies) | 22 | winner NORM'd, losing arm deleted (Phase 2/4) |
| 4 | Latches (env-latch idioms, inheritance cascades, sentinels) | 25 | die into one Config read (core/config) |
| 5 | Probes (hot-path, behavior-mutating, RAM scans) | 40 | DELETE; mechanism-only survivors → core/diag (PS2R_DIAGNOSTICS) |
| 6 | Mirrors (duplicate latches/declarations that must not drift) | 17 | one owner, zero copies |
| 7 | .inc textual includes | 6 | retired (Phase 1/2) — banned by ARCH §13/D4 |
| 8 | Code copies (statement-level logic/table duplication) | 15 | one implementation (PsmLayout, vu_float, DrawIR, v2 emission) |
| 9 | Timing fudge (invented waits, wall/emu mixing, latency shims) | 24 | real mechanism in ee/timing + cdvd models; 8 KEEP as documented models |
| 10 | Structural (header mutable globals, link seams, layering) | 30 | classes own state; constructor injection; platform/ owns #ifdefs |
| 11 | Build/config/path conventions & logging retrofits | 15 | core/config + core/log + triage tree |
| 12 | Narrative / session history in code | 12 | one-line invariants stay; history → docs/loop_state |
| | **Total** | **269** | |

Counting note: repeated instances of one pattern are merged into one entry with all
cited sites (e.g. "195 diagnostic dual-hooks" is one entry; the 21 PS2_PERF announce
mirrors are one entry). The per-file FILEMAPs carry every individual site.

---

## 1. Scene constants (frame windows, scene-keyed latches and probes)

PS2 rule for the whole kind: **none** — the GS/EE have no concept of "scene" or "frame
1180"; these encode THIS game's timeline. Scene identity that must survive becomes
TitleProfile scene-hooks / RendererProfile data; frame windows become triage-tool
arguments, never code.

- main.cpp:1530-1650 [SEQ54] sail-node probes, frames ≥1180, nodes 0xd8c5d0/0xd8c1a0/0xd8c130/0xd8c050 — DELETE; probe mechanism → core/diag.
- vu.cpp:840-869 VuFlagTrace fires only frames 1100–1180 — DELETE.
- vu.cpp:4520 SHADOW8-GATE arms at sgf≥2990 — DELETE.
- vu.cpp:11445 r198 window defaults to beach RT 0x2f0000 — DELETE with probe; layout → VramLayout.
- vif.cpp:351 w10unpkt::On() hardcodes frame ≥1990 (namespace at :341) — DELETE.
- gs_state.cpp:170-224 [ATLAS-WINDOW] frames 1677–1685 + kTbps {0x370000,0x3c0000,0x3f0000} — DELETE; page math is PsmLayout's.
- gs_state.cpp:2909 `fbp==0x230000 ⇒ g_cutscene_active` scene latch — TitleProfile scene-hook via `RendererProfile.VramLayout.is(scene_rt, fbp)`; engine keeps no scene notion.
- gs_state.cpp:346/:550/:1498-1785/:2025-2204 splash/ocean/FMV/font-atlas scene probes — DELETE.
- graphics_thread.cpp / pacebrk.h arm-at-frame-1990 constants (gfxloop, pacebrk, AtlasWindowFrame) — DELETE (savestate-specific magic).
- graphics_thread.cpp:1216-1260 loading-logo probe TBP==0x370000 + LOGO_GREENBOX force-paint — DELETE; adjudication → gs_replay + frame-hash oracle.
- graphics_thread.cpp:1339-1360 cloud-freshness counters on literal 0x230000 — DELETE with the CompositeGuard root fix.
- gif.cpp:1737-2900 [CUTSCENE]/[COMPOSITE-DIAG] region: ff≥1845, fbp 0x70000, tbp 0x150000, HTML/SVG dumper — DELETE; surviving address tests → VramLayout queries.
- dmac.cpp:2735-2760 [R101-SLOTW] guest slots 0x584240/0x58f9c0 + expected quads (cutscene window) — DELETE.
- input_system.cpp:130 selftest default "300-500:Y" boot window — moves to the title's verify scripts.
- bullseye_trace.cpp / bullseye_trace.h scene gate reads game RAM 0x309718 per call (caused the "held stick goes slow" bug) — DELETE; lesson = cheap-gate-first rule.
- manual_switch_dispatchers.cpp:770-812 [LOGO-MEMSNAP] 32 MB/frame RAM snapshots during logo — DELETE with the file.
- loader_prof.h (whole file) scene-specific probe rig with guest struct layouts — DELETE; `triage perf` attribution replaces it.
- tools/ group 4.6 (35 one-off scripts) offline faces of the same scene probes (bullseye slots, logo rects, tile bases) — DELETE together to keep the ledger symmetric.
- tests group 5.3 fixtures encode 0x230000/0x280000/0x150000 — on retarget the constants come from the title RendererProfile fixture (tests stay, constants move).
- render.h r197::D3RtReadOn 0x2f0000-family "RENDERED footprint" heuristic — durable rule is TextureSource.ResolveRead freshness (§4.5) + VramLayout data; env refusal deletes.

## 2. Game addresses in engine code

PS2 rule for the kind: the engine models hardware; **game RAM/VRAM/module addresses are
title data** (D2/A6). Owners: `RendererProfile.VramLayout` (VRAM), `TitleProfile`
(boot/quirks/hle rows), SdkBindingManifest (SDK data symbols), IopPlugin/module_layout
(IOP), games/…/data (archive tables).

- opengl_backend.cpp ~145 VRAM literals (:941 0x280000‖0x2f0000, :6605, :6772-6775 CLUT pairs, :6938, :8559-8589 SPLASH-COMPOSITE-SKIP, FBMSK=0x3fff fog sites ×6) — `VramLayout.is(role, fbp)` queries; pins → games/…/renderer_profile.cpp.
- Native-renderer .inc pins: world_tristrip:323, sprite_hud:407, skinned:517 (0x230000), fmv_blit 0x150000/0x70000, s9_composite 0x230000/0x280000/0x268000, gs_scale.h 0x230000 — RendererProfile data.
- vu1_hle.h:36 kVariantBase=0x1720; vu1_hle.cpp:50 dispatcher op 0x090E0003 + exit stubs 0x70/0x90 — `Vu1FamilySignature` fields registered by the title (ARCH §14.2).
- libkernl.cpp:335 "SLUS_123.45" tail-match; :613 sceSifFreeIopHeap 0x3B4480 + STREAM.IRX server notes — TitleProfile.boot data + stream_irx feed-contract table.
- libmc.cpp:51 kMcRpcBoundFlag=0x003c85a4 — SDK 3.0.1 libmc internal layout; named constant with SDK cite in mc/hle (binding-row documented layout).
- libmpeg.cpp:497-510 stream-key tables 0x2e7de0/0x2e7de8; :24-29 `extern FUN_002b70a8` link couplings — manifest data rows; calls via dispatch table by manifest address. (:412 kSlotOffsets = SDK struct layout — **KEEP** documented.)
- libgraph.cpp:60 kGsGParamAddr=0x002e7ff0 — manifest data row.
- libdma.cpp:25 kBaseAddr=0x01CFF000 invented descriptor arena in guest RAM — **RETIRED at integration (I5.1-B)**: `engine/dmac/hle/libdma_bindings.*` hands back the channel register blocks (`0x10008000` … `0x1000D400`; the SDK's `sceDmaChan*` table IS the register page) and writes no descriptor; the Phase 5 `kChannelArenaBase` mirror is deleted. Governing record: accepted-difference row P4-CKPT0-LIBDMA-TABLE (`docs/phases/PHASE4_IMPL.md:937, :1107, :1115`; the `0x323288-0x3232f8` / `0x1cff000-0x1cff300` mask ranges mask OLD, not NEW).
- sound_core.cpp:38-45 16 MB FMV FIFO sized by offline scan of all 22 disc files — TitleProfile capacity hint; engine default smaller.
- loader_sync_hle.h:96/:186 guest ABI facts (ra site, GPR.r[16] s0 check) in an engine-included header — TitleHleModule data.
- sif_hle.cpp:60-115 sceSifInitRpc writes a dozen absolute game addresses — offsets derive from the signature-matched binding (SDK struct layout) or the recompiled body runs.
- sif_hle.cpp:702-724 0x181490 piracy-check bypass (writes 0x30a458=0) — HOOK §3.5 workaround-ledger row; Phase-7 adjudication (retail-clean under faithful cdvd, or explicit TitleProfile patch).
- sif_rpc_handlers.cpp:2986-3000 EE game-state peeks 0x309720/0x309508/0x309BF4 — DELETE with log purge.
- hle_heap.cpp:719-768 recompiled-map re-entry at 0x1e2db0/0x1e2958/0x1e74a8 — title glue in games/…/iop/stream_irx driven by SIF RPC dispatch.
- hle_heap.cpp:456-542 DATA.BD/BH archive insert-order knowledge — games/…/data/archive_table (TREE-GAP row).
- iop_loader.cpp:24-26 hardcoded LBA/size for STREAM.IRX (rule: LoadModule via ISO9660 walk); :31-37/:356-362 module-internal addresses — cdvd iso_walker owns lookup; IopPlugin descriptor owns addresses.
- iop_sysmem_heap.h:38-40 kFloor=0x28000 (STREAM.IRX image ceiling) — computed from loaded IopPlugin image end.
- iop_stubs.cpp:1660-1700 module flags 0x12f44/0x12f1c (legacy arm); per-thread stack formula 0x100000−tid*0x4000 + scratch tops — title module_layout data with documented reservation map.
- iop_stubs.h `hle_stream_cd_read_rpc` bound at pc 0x128a8 — IopPlugin_StreamIrx patch row (engine never binds a game address).
- snd_bank_ledger.cpp:102-107 IOP addresses 0x133e0/0x13388/0x12e60 — stream_irx feed-contract data.
- snd_census.h STREAM.IRX cmd-type table in an engine header — registered title data.
- memory.cpp:140/:166-170 hardcoded ELF path + section table — TitleProfile.elf drives generic ElfLoader.
- memory.cpp watch families (0x9819a0, 0x309728, 0x309508, 0x309718, 0x1a88698, 0x420BC8, 0x01997D78…) — DELETE bodies; multi-range watch mechanism → core/diag probe kit.
- memory.h [KEY-REDIRECT]/[KEY-OVERLAP] level-key 0xbf2c00 + "Levels\Earth\Hub\Beach" — HOOK §3.8 quarantine; retire at boot/loader root fix.
- cdvd_stream.cpp:238-243 [CDVD-STREAM-WATCH] DMA tag 0x450180 — DELETE.
- ee_timers.h/.cpp game clock family: accumulator 0x3097D0, config struct 0x30972C, ISR 0x191980, T0_MODE=0xe82 seed (also memory.cpp:620-850 host-writes DAT_003097d0) — real rule: T0 is 16-bit with compare/overflow IRQ; EeTimers + Intc raise INTC_TIM0 so the game's own handler maintains its clock; layout knowledge → title data while quarantined.
- timer_hle.h/.cpp + main.cpp:3254-3611 0x191BD0 budget/wait binding, tuned bases 8400..9700 (8800 shipped) — ee/timing budget_anchor mechanism + TitleProfile.quirks.budget data (HOOK §3.6 row E).
- gs_hle.cpp:900-960 sceGsSendImage fallback assumes PSMCT32/512-wide/0x70000 — make it a loud triage decline; constants die. :1000-1113 re-implements game fn 0x19aff0 + Crash-NTSC fallback — HOOK §3.5 row; retire at gs/state/privileged root fix.
- gs_crtc_class.h census-pinned scanout config (0x70000/512/CT32) — classify generically; pin → VramLayout if it survives.
- gs_page_tracker.h:171-180 SeedReaderPages band table — RendererProfile/VramLayout seed consumed by VramImage.
- texture_decoder.cpp:1194/:1299/:2016 + gs_state.cpp:726 tile-atlas index formula `((cbp&0xFFFF0000)-0x380000)>>16` — VramLayout query.
- vif.cpp:2568-2575 s8viftap VU1-data header windows {0x110,0x130},{0x173,0x193} — DELETE; would be Vu1Library data if ever relanded.
- gif_arbiter.cpp:545 s8arb FBP matcher 0x038/0x118 pages — DELETE.
- gs_class_renderer.h strstr on hle_name + S9 signature constants (CT16S/T8H/DATE, 256×128) — RendererProfile.stamps + RawClassifierSpec data.
- mipsrc_diag.h VRAM pool band 0x300000-0x3fffff — dies with file.
- libc_hle.cpp vtable-protect band [0x2f6930,0x2f6a00) — rides the write-watch move to core/diag; band = title data if still needed.
- sif_rpc_handlers.h RE anchors in comments (thread body 0x1E7F80 …) — title module protocol doc.
- main_old.cpp smflag=0x20000 "bypass check at 2d1c60" — dead; superseded.
- vu_function_registry.cpp FUN_-labeled rows — table physically moves to games/ (LintEnginePurity).
- recompiler.cpp:903-2156 265-site `base_address ==` chain; :10464-11901 FUN_00177168 hand emitter (23-target table @0x1772c0); recompiler.h 0x1cd768 probe note — v2 general emission + SdkBindingManifest rows; DELETE Phase 2.
- iop_recompiler.cpp:373 `pc==0x000128a8` injection; iop_recompiler/main.cpp import-stub range 0x127E0–0x12B70 "known from analysis" — import-table-driven stub HLE (ordinal → binding rows); range derived by table walk.
- hle_meta_arena.h/.cpp tombstone bits ≥0x40000 + descriptor layout (+0x1c nibble, 10-bit lock counter) — game-image RE as API; dies with the arena at the 0x181560 root fix.
- intc.cpp `ra=0x80001000` kernel return-stub — **KEEP** as named invariant kIntcReturnStub (PS2 rule: INTC handlers return through a BIOS stub), trapped explicitly by the dispatch seam.

## 3. Dual paths (fast/legacy pairs, dual engines, dual bodies)

PS2 rule for the kind: one behavior has one implementation. The winning (peak-config)
arm becomes the only code path; the loser and its selection gate delete (MIGRATION §3
gate purge; equivalence pinned by corpus/replay gates before each deletion).

- vu.cpp:364-615/:738-916 superblock/stallspec/stallfold/spanfold/runflat/fastpack/hashepoch/lazyflags/runretire/pace gates — each keeps a verbatim legacy arm; winner inlined, arm+latch deleted (vu/tiers).
- gif.cpp:3601-3660/:3894-4150 ParseSpan bulk lane vs legacy decode + s_drift_* checker — GIF_FAST NORM deletes twin and checker; byte proof → gif corpus replay tests.
- gs_state.cpp TryKickRunNative + TryKickRunBulk + legacy AssembleVertex (3 assembly paths; equivalence argument at :2327-2363) — one path in VertexKick.
- gs_state.h three-tier kick decline ladder — native+bulk pair with one documented decline reason.
- sif_rpc_handlers.cpp:33/:5094 USE_IOP_RECOMPILER switch + 897-line HandleCustomLoader legacy twin (also sif_rpc_handlers.h twin decls) — v2-only; twin + macro delete.
- sif_hle.h:13-19/.cpp:710-750 dual-engine install seam (PS2_RECOMP_ENGINE_V2 vs update_function_impl reinstall) — one SdkBinding/patch channel after Phase 2.
- idx16_lane.h legacy/fast dual around the channel-shuffle read rule — RenderTarget/TextureSource implement the rule directly; byte-identity fallback arm deletes.
- vu_microcode_section.cpp `#if defined(PS2_VU_AOT_DISABLED)` dual-body TU — resolved by deletion (AOT retired).
- ee_timers dual maintenance (gate-ON module vs memory.cpp host-maintained accumulator) — NORM purge deletes legacy path + mirror.
- iop_stubs.cpp:1660-1700 RaiseStreamCdCompletion documented-WRONG legacy arm kept as bisect hatch — deletes with PS2_SNDREG; SignalSema-only path is the implementation.
- opengl_backend compute-raster dual pipeline (m_use_compute_vram branches) — second renderer behind a hardware latch; DELETE; software oracle backend replaces the role (ARCH §6).
- gif.h:174-190 ApplyHleDirectOps vs ApplyHleDrawIR generations — DIRECT→DrawIR ladder collapses to DrawIR-only (GS_DRAWIR NORM).
- vu.h `kEnableXgkickPace=true` with "=false restores snapshot semantics" — snapshot arm deletes (root cause of the spike-triangle family).
- w10_eefeed.h one gate steering two subsystems (A2 §2.2 master shape) — purge-dissolves; arms become dmac/gif code.
- gif_template.h proven-neutral cache alive behind a default-OFF gate for two sessions — Phase-4 wave decides delete-both vs keep; "decide, don't carry".
- mpeg_hle.h wall-vs-tick pacer duality — **KEEP both arithmetic cores** (unit-tested); domain selection moves to TimeDomain (one explicit `time.mode`).
  - NARROWED at Phase 7 (PHASE7_IMPL checklist item 9; PHASE7_PLAN §5.5 P7-LEDGER-BURST): only the TICK core survives — NEW `FmvPacer` (`engine/ipu/hle/fmv_pacer.cpp`) is ONE arithmetic core on an `int64` tick domain (`TickPace::NextBurst` semantics as the sole path); the wall core `PaceDeadline` (`mpeg_hle.h:56-105`) has no domain in NEW and was not written. [IDOC-A 2026-10-01]
- gs_hle.cpp SyncV four-deep pacing-era branch nest (PaceOn/WaitFix/Defer/legacy-poll) — ONE BoundaryProtocol after NORM collapse.
- rt_copy_tracker.h dual dirty-tracking (union rect AND tile grid in parallel) — pick one, delete the other, keep the test.
- legacy/recomp_overrides_config.h per-address rollback channel to hand bodies — DELETE Phase 2; emitter regressions are git bisect + golden-emission diffs, not pins.
- anim_pose_hle.cpp + anim_pose_hle_bodies.inc hand-mirror layer vs recompiled bodies — Phase-2 adjudication (tests/anim_pose_diff), then delete file + .inc + installer.
- manual_switch_dispatchers.cpp per-address hand bodies vs v2 emission — retired Phase 2 (HOOK §3.2 per-address adjudication).
- artdet_flicker.h synthetic-toggle draws to the live backbuffer — TEST-ONLY harness-injected fake frame source; never in release.

- host_app/graphics/opengl/{skinned,sprite_hud,particles,fmv_blit}_native.inc **Class-native generic arms inherit register-program uniforms** (L4, PHASE6_IMPL 6F′, carried by INTEGRATION CHECKLIST item 7; docs/bugs BUG-058): OLD generic arms (skinned `:1107-1170`/`:1375`) omit `SetU*` for `u_ct16q`, `u_dthe`, `u_dimx_lo/hi`, `u_draw_scale`, `u_uv_native` (+ blend a..d / fix_alpha, zsrc_num/den per shape) that `DrawBatch` sets on every draw (`opengl_backend.cpp:10817-10823`, `:10660-10671`, `:11136-11140`, `:11288-11292`, `:11457-11459`); the generic program keeps the previous DrawBatch draw's values. PS2 rule violated: a draw's frame format alone decides the 16-bit storage quantisation. Symptom: load_transition vf855 — CT32 accumulator draws after a CT16 frame draw land 5-bit quantised (blue 181 → 176, white → 248). NEW: `render::InheritedUniform` + `ClassRenderer::kInherit*Arm` (named, one-line retirement: masks → 0). Expiry gate: the corpus sweep (18 captures) + the frames lane; retiring it is an accepted difference against the frozen baselines. Live-mode caveat (PHASE6_IMPL INTEGRATION CHECKLIST item 5, quoted): "OLD's live path routes textured VRAM-decode skinned / sprite-HUD / particles / FMV draws to the SPECIALISED programs (`skinned_native.inc:935-937`, `[GS-SKINRDR] native=24158 spec=24158 generic=0` in OLD's live logs) which carry no `ct16q / dthe / dimx` uniforms at all — so once `has_snapshot` is true, a class-routed draw that OLD would send to `spec` must NOT inherit (`DrawPipeline::PrepareOptions::inherited_uniforms = 0` for `batch_tex_id != 0`, or the spec program modelled), or the frames lane diverges from OLD live output." [IDOC-A 2026-10-01]

## 4. Latches (env-latch idioms, inheritance cascades, config smuggling, sentinels)

PS2 rule for the kind: configuration is not a hardware concept — **one Config read at
startup in core/config; zero getenv outside it (ARCH §8)**. Every entry below dies into
the Config schema or into constants.

- render.h:767 `GateOn(const char*)` generic env helper (a config system smuggled into a header) + texture_decoder.h twin copy — die with the Config schema.
- memory.h g_fastmem_mode (-1/0/1) latch-on-first-use inside a hot helper — Config load before threads.
- budget_lift.h whole policy as header-inline function-local statics + mutex (TU-latch config style) — one class, config resolved once.
- turbo.h tri-state K encoding (1=off, 0=freerun-engaged, ≥2=finite) + PS2_TURBO=0 "unbounded freerun" semantics — explicit enum {Off, Freerun, Finite{k}} in TurboGovernor; env dies.
- ps2_vblank.h/.cpp:408-486 NOVSYNC→FREERUN→CDVD_INSTANT default-inheritance cascade (A2 finding 3; one bench var silently flips three behaviors) — one explicit pacing enum (`time.mode`) + dependent-field validation in core/config.
- mpeg_hle.cpp:459 PACE_AUTHORED default-by-inversion (`!VBlankFreerunOn()`) — same fix: explicit time.mode; no gate reads another gate.
- present_pace.h bypass keyed on other subsystems' gates (freerun/warp/`DecodeActiveRecently(250)`) + 9-var surface for one feature — 1 Config field (video.fps_cap) + constants + explicit FMV-active signal.
- vu1_async.cpp:1010-1035 s_gate_latched_at_startup static-init side effect (announce ordering) — dies with the announce idiom.
- manual_switch_dispatchers.cpp:778-790 sentinel-file gate `logs/MEMSNAP_ON` (config via filesystem probe) — dies.
- vu_aot_config.h ini that `setenv()`s vutrace vars at boot (env as internal IPC) — dies with tier; Config replaces env plumbing.
- vu_trace_case.h:82-92 VuTraceForce mutates process env before first Execute — vutrace Config gains a programmatic injection point; setenv shim deletes.
- gif.cpp:87-133 GifFastOverrideForTest/GskSimdOverrideForTest per-process env overrides (workaround for un-reflippable static latches) — Config injection; tests construct GifParser with explicit options.
- gs_class_renderer.h ClassOverrideForTest int[10] + env cache struct (11 gate mirrors in one header) — Config/profile + one test hook.
- vif.h OverrideForTest() mutable-int hooks in a production header — equivalence suite instantiates the reference kernel directly; TEST-ONLY code lives in tests/.
- gif_arbiter.h DrainThreadOverrideForTest per-process latch override — Config injection.
- s8_inrt.h gate-follows-gate default (`On(bool fmt6_class_on)`) — dissolves with the NORM.
- quantum.h PACE_SMOOTH/CAP_STEPDOWN "inert under Mode Q" but still latched in present_pace.h — cross-file dead-gate coupling; both delete at quantum NORM.
- savestate_format.h conditional version stamping by which gates were on — version = max required by present sections (data-driven); reader keeps accepting 1..4.
- shutdown_policy.h legacy infinite-join arm kept for bisect — deletes at purge; bounded unwind unconditional.
- s8_ctxlatch.h two-env alias resolution + single-site default lever — config archaeology; dies.
- ring_log.cpp [MTVU-BP-FIX] INDIRECT_JUMP demoted to verbose tier by mask default (rate-class fix by comment) — per-event-class rate budgets in the ring schema.
- skinned_native.inc hatch-stack (master→class→sub→SPEC four-level gating) — collapses to registry data.
- bug_report.cpp three variants of `v[0]=='0'` bool parsing in one file — Config loader owns parsing.
- display_settings.h/.cpp settings-as-atomics cross-thread bus + private ini dialect — Config (read-once) + PresentViewState (single-writer); display.ini folds into the one Config file.
- savestate_writer.h inline env gate + test-hold knob in a header — .cpp/Config + ctor injection.

## 5. Probes (hot-path probes, behavior-mutating probes, RAM scans, telemetry in dispatch)

PS2 rule for the kind: none — probes are session archaeology. The mechanism (registered
probe points, multi-range write watch, call-stack capture) survives ONCE in `core/diag`
(PS2R_DIAGNOSTICS builds); play-time capture belongs to `core/triage`; every in-line
probe body deletes.

- main.cpp ~195 diagnostic dual-hooks ([SEQ54], [CTRL-SM]/BullseyeDiag, [S8-REG], [S16-CHAIN], [S26-DECAL], [ADAF0-DIAG]/[DESER], memcpy/alloc watchers, 0x191bd0 companion, 4 DISABLED) — DELETE; `install_dual_hook` mechanism → core/diag probe API (HOOK §3.1).
- main.cpp BullseyeDiag shared-state namespace + s8cnt exit-report counters in the composition root — core/diag registry.
- vu.cpp:4330-5100 probe forest wrapping Execute() (S26-VU0, SHADOW8-GATE, S8-VISCAP, S13-TBLCAP/TBLWATCH, S8-MTXCAP, S15-VUENT/HDRCAP, VU0-XCHECK; several re-enter Execute recursively) — DELETE.
- vu.cpp:4381-4400 [S26-FORCEPASS] zeroes vf31.w after the decal micro; :10993-11003 [S8-FORCEVIS] forces VI6=0 at a pinned pc/reg signature — **behavior-mutating diagnostics**; DELETE (questions closed).
- vu.cpp ungated hot logging (qw2 WriteDataMem "UNGATED from process start" :1697+, [VU1-MTIR] first-40 :11005, [VUFLAG] :840) — compile-time log categories.
- vu.cpp:12400-12413 cutscene globals (g_cut_vu1_runs/xgkick/entry, g_pose_wb_count for the 0x194040 hook) — DELETE; hook is a HOOK ledger row.
- ps2_vblank.cpp:41-386 [BULLSEYE-SLOT-DUMP]/[BULLSEYE-TEMPLATE-PERIODIC]/[TEXT-SCAN] RAM scans at game addresses — DELETE.
- ps2_threading.cpp sema census + perfCensusEmitIfDue in the hot wait path — core/diag counters, off in release.
- memory.cpp 295 `g_logFile<<` sites (35 gated) incl. per-MMIO-read lines — core/log compile-time categories; dead categories compile to nothing.
- opengl_backend.cpp:19412-19604 FindTargetTexture wrapper = ~190 lines of probe around a tail call — core/diag GenProbe-style instrumentation only.
- opengl_backend.cpp:24211 [SCANOUT-STATE] stderr heartbeat every 60 calls + :24222 [BULLSEYE-FBODUMP] — core/log category, compile-time off.
- gif.cpp:382-540 SHADOW8_KTAP A+D-scan tap inside ReceiveData (hottest GIF entry) — core/diag probe at the gif/queue seam, compiled out of release.
- gif.cpp:134-183/:4461-4540 r158/ObjStrip/KickSrcTally census + EmitForBench `std::_Exit` riders — core/diag census registry; explicit flush owned by the harness.
- dmac.cpp:42-90 DmacPerfScope outermost-only booking + R120-WALK per-walk log latch + midc:: hooks in the walkers — core/diag perf scopes (compile-time).
- pad_input.cpp ~450/1,007 lines of script/watch/poke probes — input_script/stick_trace in core/diag; triage replay supersedes for repro bundles.
- libpad.cpp:373-440 probe stack in the serve path (VISREG-TMP "remove after investigation", sticktrace, INLAT analog) — DELETE / core/diag one latched test.
- vu1_hle_runtime.cpp:70-420 raw stderr fprintf in the hot dispatch path (rate-limited but unconditional formatting on trigger) — core/log + core/triage counters.
- vu1_hle_programs.cpp:212-244 WhyNote getenv latches + stderr in decline paths — FallbackDetector counters (core/triage).
- sce_hle.h/registry.cpp:45-64 "FAKED" banner + per-name unordered_map counters in log_call — core/log once-per-site category.
- drawlist_record.cpp:96-170 static-init env configure + atexit(Shutdown) + g_on_push single global fn-pointer — explicit lifecycle owned by the diag registry; observer list on RenderQueue.
- loader_oracle.h:70-118 header FILE*/mutex/seq singletons + `(s_seq&0x3ff)==0 ⇒ fflush` harness-lifecycle leakage — LoaderTrace class owned by the harness.
- loader_sync_hle.h:150-170 stderr telemetry every 2 s — core/log.
- gs_state.cpp:85-105 R119ProbesOn/S8KtapKickOn hot-path probe latches — DELETE.
- texture_decoder.cpp:1185-1203 [READ-CLUT32-DIAG]; :1419-1447 [CLUT-RED-SCAN]; :1662-1700 T8 tile dumps — DELETE.
- gif_arbiter.cpp:590-650 [ARB-SUBMIT] 200-line PATH2 dump in the hot submit path — DELETE; replay tooling covers it.
- ee_timers.cpp:552-566 [R195-TIM0] stderr dispatch-cost lines; :482-520 T0TRACE/T0_NOISR probe arms — core/diag; probe arms delete.
- syscalls.cpp dynamic-interpreter g_logFile narration per branch — EE fallback arm of FallbackDetector producing EeFunctionCase bundles.
- syscalls.cpp:89-93 NotImplemented_Syscall `return; exit(1);` dead exit — triage-visible unimplemented-syscall counter.
- iop_stubs.cpp IOPQ dump (dozen raw module addresses) + SPU peeks — DELETE; triage owns future capture.
- sif_rpc_handlers.cpp:291 VerifyRamContent + [STREAM EE-STATE] diag reads — DELETE.
- cdvd_stream.cpp:245 `memory::key_overlap_check` call in the stream path — deletes with HOOK §3.8 family.
- iop_loader.cpp:347-420 "VERIFICATION: Task 7.1/7.2" printf blocks — DELETE; load failure becomes a loud error path.
- bench.h:150-250 funopen stderr shim parsing its own process's census lines (counters live in a frozen TU's anonymous namespace; macOS-only) + bench_report.h:30-44 same scraping — counters exposed through the core/diag registry; shims delete.
- artdet_flicker.cpp raw glReadPixels + GL calls in a detector TU (backend bypass) — backend-provided readback tap feeding pure pixel buffers.
- slowspot.cpp [FIX-SLOWSTICK] opt-in + [W13R7-A7] bench suppression (capture-cost patches) — TRIAGE §9 cost budget makes capture safe-by-construction; both dissolve.
- libc_hle.cpp [S100-MIPWR] write-watch hooks inline in hot copy paths — core/diag builds only.
- cuelag_probe.h inline atomic device-clock counter consumed cross-TU by sound_core — untangle before deletion (counter → sound/, probe → gone).
- loadspan.h:51-90 cross-TU extern counter redeclarations + hook tendrils in four TUs ("cuelag pattern") — registered probe points on the diag module.
- snd_census.h hook tendrils into three TUs — registered diag probe points.
- mid_census.h `std::atexit` registered inside the On() latch — diag registry owns lifecycle/flush.
- gs_class_renderer.h CurDrawClass/CurS9Path relaxed atomics written per draw for an env-gated counter — PS2R_DIAGNOSTICS-only GenProbe.
- memory.h g_current_pc [IO-TRACE] (non-functional per steering) — DELETE.
- recompiler.cpp emitted probe blocks (PS2_SHADOW_DIAG ×4 bodies, PS2_EE_RAWDISP, crashring consults) + iop_recompiler.cpp:320-342 STRMQ injection — purge at generator; GenProbe manifest is the Phase-8 channel.
- run_play.sh HEADWATCH=0xd07640 + EULERTRACE probes riding the play canon — core/diag (stick_trace) hosts; presence-gating dies.

## 6. Mirrors (duplicate latches / declarations / tables that must not drift)

PS2 rule for the kind: one fact, one owner. Mirror latches die with the gates (one
Config read); mirror declarations become owned interfaces; mirror data tables collapse
into the format layer.

- gl_state_cache.h:75-89 documented intentional mirror of render.h `r120::BindCacheOn` ("to avoid an include chain") — A2 finding-4 worst case; one Config-derived constant.
- uv_bbox.h documented latch mirror ("render.h cannot include gl_state_cache.h", parity asserted by a test) — dies with the gates.
- vif.cpp 3 independent latches of PS2_VIF_ALIGN_LOG + 3 of PS2_SHADOW8_KTAP — single Config read; here simply DIAG/delete.
- graphics_thread.cpp:1990-2070 gate-mirror block re-reading ~25 other TUs' env gates to stamp bench JSON — gates die; core/config dumps the Config once at startup.
- ee_pace.h:151-160 env-level mirror of vu1_async DeferOn's latch inputs ("without linking vu1_async") — Config dependency validation (mtvu_depth needs mtvu).
- perf_profiler.h Enabled() re-latched per TU + the 21-site PS2_PERF announce-mirror family (A2 finding 2) — one startup config dump; compile-time category.
- vu.cpp:418-433 VuModeResolvedName re-derives vumode::Resolve minus the announce — one resolution, one owner (Config).
- s8_inrt.h:100-104 CPU/GLSL twin remap "must stay bit-identical to the GLSL" — golden-vector tests generated from ONE table (PsmLayout descriptors) driving both sides.
- compute_data_layouts.h "must stay in sync with the GLSL" comment-enforced mirroring — removed by the one-source design (and the compute arm deletes).
- shaders/swizzle.glsl + pixel_format.glsl GPU mirrors of the CPU swizzle/format tables — DELETE (reference extracted first).
- call_census.h symbol re-declaration pact with generated code ("the generated header cannot include this file") — GenProbe emission owns the declaration.
- ring_log.h:96 + timeline.h + mid_census.h `extern std::atomic<uint32_t> g_vsync_frame` re-declared across headers (include-order splice) — one frame-stamp provider interface (ee/timing frame counter API).
- gs_deposit_overlay.h:52-55 unsized `extern` array declarations against graphics_thread definitions — PageRecord owns its arrays.
- bench_report.h GL Census struct mirroring ~90 loose counters by name — one census registry (core/diag) both sides reference.
- run_play.ps1 duplicated CANON env set (mirror-latch at script level) — one Config, zero launchers.
- savestate.cpp park/unpark bookkeeping shadowing scheduler thread-state — quiescence consumes the scheduler's own table (one writer).
- vu_function_registry.h PC-keyed registry beside vu1_hle's image-hash registry (two lookup systems for one question) — merge into vu/hle/registry; PC-keying retires to start_pc metadata.

## 7. .inc textual includes

PS2 rule for the kind: none — a build-shape workaround. ARCH §13/D4 ban textual
includes for hand-written code; generated code emits real TUs.

- manual_switch_dispatchers.cpp:1300-1982 — 222 textual `fun_*_body.inc` includes (the direct counterexample of ARCH §13) — retired Phase 2 with the layer.
- host_app/fun_*_body.inc (222 files, 159,811 lines, ONE hack class) — hand reimplementations working around indirect `jr rs` dispatch through in-ELF jump tables; owned in the refactor by discovery jump-table engines + emit/indirect_dispatch (generation) and flat dispatch tables (runtime); per-address adjudication then 0 hand bodies.
- anim_pose_hle_bodies.inc (2,027 lines) — 9 hand-maintained shadow copies of recompiled functions (addr-switch prologue mirrors label dispatch) — v2 emits fast bodies; no mirrors.
- opengl_backend.cpp:25291-25316 TU-tail includes of the six native-renderer .inc files (so they see file-local gate namespaces) — renderers become classes registered via RendererProfile (gs/hle/classrdr).
- vu_aot chunk TUs (32 × 17 lines + vu_aot_programs.cpp's `#include "vu_aot_generated.inc"`) "include dance" for constexpr/always_inline in-TU visibility + fixed empty-TU placeholders to freeze CMake's source list — moot; NOAOT deletes the tier.
- opengl_backend.cpp:3994-4105 hand-rolled GLSL `#include` preprocessor — dies with the compute experiment.

## 8. Code copies (statement-level logic/table duplication)

PS2 rule for the kind: one implementation per rule. Owners named per entry.

- CLUT CSM1 swizzle duplicated 3× byte-identical + 1 variant (texture_decoder.cpp:944, opengl_backend.cpp:21667, s9_composite.h:322; A1 finding 7) — ONE `gs/format/clut` implementation.
- PSM→pixels-per-write switches duplicated ≥2× inside gs_state.cpp (:5066,:5332) + gs_vram_census.h fifth PSM page-geometry copy + gs_deswizzle.h swizzle family #3 of 4 — ONE `gs/format/psm_layout` (constexpr descriptors), everyone consumes it.
- vu.cpp ↔ vu_decode_slot.h 58 duplicate 30-line windows (~1,700 lines, "extracted verbatim") — after the move there is ONE decode consumed by interp + cached tiers; vu1_replay --compare stays as the proof.
- vu.cpp:616-663 ApplyFmacPrunePush hand-copied VERBATIM from vu_fast_exec.h:337-412 — one StallModel class with a decode-time specialized entry.
- vu1_hle_programs.cpp ~9 duplicated ChopGuard FE_TOWARDZERO RAII definitions + repeated float-bit helpers per namespace — ONE copy in engine/vu/vu_float.h.
- world_tristrip/sprite_hud/skinned/particles .inc DrawBatch statement-for-statement replication (particles is a third replica template-copied from skinned) — dissolves when both sides consume DrawIR + TextureSource; each renderer keeps only its specialization + DECLINE path (ARCH §5.3c).
- Dead root tests replicate production logic by copy (test_scanout_preservation, test_bug_conditions, test_vblank_preservation, test_comprehensive_rendering, test_rendering_architecture; test_texture_shuffle.cpp:7-9 admits copying a TU-static verbatim) — oracle-gated tests linking the owning class replace the pattern.
- gs_glbatch.h second copy of the producer merge predicate (header argues its own equivalence) — one merge, one place (producer RUNDRAW survives).
- idx_epoch_guard.h duplicates the [VRAM-EPOCH] comparison at opengl_backend.cpp:~9409 — PageRecord owns the epoch comparison once.
- opengl_backend.cpp:2500/:2541 embedded shader + runtime source transforms (R169TransformFragSrc, S83InjectIdxScale — string surgery on GLSL at init) + shaders/ps2_vert/frag.glsl "reference copies" (comment admits the embedded copy is authoritative) — the transformed source IS the only authored source in GlShaderLibrary; reference copies delete.
- recompiler.cpp 84 × `R"code(` raw-string hand-translated bodies (e.g. :13233 FUN_002a0250, :18043 FUN_00255188) — v2 auto-emission; DELETE Phase 2 with per-address adjudication.
- recompiler.cpp:25582 rewrite_manual_dispatch_file + :25696 inject_gen_hook_list — W16 textual POST-PASSES regex-rewriting emitted C++ after the fact — emit the right form the first time (DispatchTables owns hook/dispatch emission).
- inject_logging.sh — edits generated output by absolute line number (the purest pre-probe-channel artifact) — GenProbe manifest / core/diag address-hook probe kit.
- opengl_backend.h per-renderer uniform-shadow structs duplicated six times (Wsl/Shl/Skl/Prt/Fvl) — one GlUniformCache serves all programs.
- manual_FUN_00255f30 folds VU0 macro sin/cos into std::sin/std::cos (silent precision divergence) — v2 body + vu_float helpers; adjudicated on the harness.

## 9. Timing fudge (invented waits, wall/emu mixing, latency shims, pacing carve-outs)

PS2 rule for the kind: EE time is cycle/tick-denominated; ordering contracts must never
be wall-denominated (S57 freeze class; BUG-014). Owners: ee/timing (TimeDomain,
EmuDomain, DerivedClocks, BoundaryProtocol), cdvd latency/delivery models, protocol
classes. Entries marked KEEP are honest documented models.

- timer_hle.cpp unconditional `return 1` ("we don't need to replicate the exact math") + fixed 1 ms host sleep as guest time — compute elapsed emu-domain ticks against tick_rate/divisor (DerivedClocks) and return the real predicate; ee/hle/timer_wait + budget family.
- memory.cpp:620-850 T0 monotonic-clock reconstruction host-writing the game's wrap counter — real EeTimers 16-bit COUNT + INTC_TIM0 IRQ so the game's handler runs (see kind 2 for the addresses).
- iop_stubs.cpp:718 heartbeat clamp `usec∈[1000,100000]` silently rewriting the module's timer request — honor exact period or **KEEP** as documented timrman `// diverges:`.
- libmc.cpp:196-210 [R200-MC] instant-done-is-wrong latency shim — **KEEP** as McCommandModel with the PS2 rule stated (MCSERV ops span frames; first-poll completion breaks poll loops).
- pad_input.cpp:152-206 staging constants (disconnect 8 vsyncs, config 214, ready-delay 2) — **KEEP**: libpad 3.0.1 connection-negotiation protocol data on PadProtocol (TitleProfile quirk only if a second title disagrees).
- pad_input.cpp:239-333 press latch (kLatchMinReads=2; ~880 Hz host stamping vs ≤5 reads/s game sampling) — **KEEP** as documented HLE semantics; R183 measurement reduced to one invariant comment.
- pad_input.cpp g_pad_area_mutex as DIntr/EIntr bracketing — **KEEP** with `// diverges:` (real PADMAN DMA lands once per vsync).
- hle_heap.h/.cpp chunk_gate ARM-A/E/C/V hold model (valve 90 ticks, order 1 frame) binding 9 drain-ring descriptors + FUN_00264948/FUN_001a1bb8 observations — LoaderCompletionModel (HOOK §3.6): completion latency is the only observable; anchors → games/…/hle/loader_sync data; constants NORM'd.
- hle_heap.cpp:2765-2830 DVD model freerun default inversion (scale 0 under VBlankFreerunOn) + turbo::WallDivisor coupling — `time.mode`-scoped behavior of DvdLatencyModel; no cross-header env inheritance.
- cdvd_stream.cpp:268-276 sceCdStStat always-full ring (`available=16` fallback at :276) — **KEEP** today (load-bearing, correct for this game) as a `// diverges:` libcdvd note; HOOK §3.6 completion-visibility model is the future owner.
- cdvd_stream.h PumpSectors blocking-decode divergence — **KEEP** with `// diverges:` line (ARCH §11 rule).
- sif_rpc_handlers.cpp:3010-3170 S46-BANKGOV batch rewrite (holds bank-chain records so the single-job engine sees one chain per round; host-side carry lost by mid-carry savestate) — PS2 rule: RPC batches arrive at CD-latency spacing; pace at the completion model (cdvd/hle latency + title StreamDelivery), not by rewriting payload bytes.
- sif_rpc_handlers.cpp:118-140 EE-RPC 120 ms timeout → synthesized reply — BoundaryProtocol guarantees the service point; timeout becomes a loud triage event.
- sif_rpc_handlers.h S38-era synthesized-reply fallback on stream RPC timeout — STREAM.IRX recomp plugin serves all replies; synth path deletes with proof.
- gs_hle.cpp 100 ms park valves (parks fall through unpaced) — **KEEP** as the documented BoundaryProtocol safety valve; releases counted.
- ps2_vblank.cpp [S93-TILEPATCH] stall-debt bookkeeping feeding T0 — EmuDomain owns stall accounting (ARCH §4.7).
- ps2_vblank.cpp:563+ warp/turbo dynamic OR into the freerun branch (regime switching by scattered predicates) — TickSource takes one PacingRegime input owned by ee/timing.
- ps2_vblank.h CdvdBudgetEpoch delivery-fired tick exclusion (documented transient-dip tolerance) — **KEEP** as a named rule inside CdvdDeliveryMeter with its unit test.
- present_pace.h / slowspot.cpp / graphics_thread.cpp `mpeg_hle::DecodeActiveRecently(250/500)` wall-clock FMV sniffs — explicit FMV-active signal published by FmvPacer through the frame boundary (FrameAnnotator input). **SUPERSEDED in part (I7.6-A, 2026-10-01):** the signal is `MpegApi::FmvActive()` (context-level, not on the pacer), read at `Step::Verify` by the composition root -> run.meta `fmv_active_frames=` and the `Quiescence` savestate refusal (`engine/core/savestate/quiescence.*`; `savestate_refused_fmv=`), replacing the `savestate.cpp:1040` `DecodeActiveRecently(250)` sniff. The present-policy consumer (present_pace.h / slowspot.cpp sniffs) waits for plan row 6.7's module.
- turbo.h stale-note fail-safe (>1 s pump silence ⇒ 1×) — **KEEP**: real liveness rule; TurboGovernor invariant.
- vif.cpp:79-86 UNPACK shadow refresh = full 16 KB memcpy per exec-idle point (0.14 ms/f, self-documented) — **KEEP** semantics as the MTVU seam contract in vif_mtvu_seam (dirty-range refresh later).
- mpeg_hle.h debt clamp (~2 frames) + "first delivery arms the clock" — **KEEP** as FmvPacer invariants.
  - SUPERSEDED at Phase 7 (PHASE7_IMPL checklist item 9; PHASE7_PLAN §5.5): the debt clamp was never live under the defaults — `PS2_FMV_OPEN_BURST` defaults ON (`mpeg_hle.cpp:531-547`) so `NextBurst` (no clamp) is the runtime path of every baseline leg; NEW `FmvPacer::Next` carries "first delivery arms" + late → `late=true, wait=0`, NO debt clamp (`test_fmv_pacer` pins "no floor"). FLAG_LEDGER row `PS2_FMV_OPEN_BURST` → NORM. [IDOC-A 2026-10-01]
- libsdr.cpp wall-clock virtual-block model (addresses unobservable on real HW) — **KEEP** documented invention with divergence comment; `#define kVirtBlockDuration` macro-as-constant dies with the env read.
- libscf.cpp:70-80 std::localtime host-time read (nondeterminism leak) — TimeDomain-provided RTC (fixed in the deterministic profile). Fake MC timestamp table dt[8] year 2026 (libmc.cpp:143-146) — derive from host mtime (documented divergence either way).
- ee_timers.h ISR dispatch "on a guest EE thread at safe boundaries" (host scheduling approximation of an interrupt) — **KEEP** as `// diverges:` per the R195 adjudication (real INTC preemption not modeled).

- opengl_backend.cpp:19236-19320 (+ :19332-19378) **`ScanoutFromVRAM` FMV source-gated hold — dormant at peak, NOT written** (L2, PHASE6_IMPL 6D‴ / Review 6D″ fix 2, carried by INTEGRATION CHECKLIST item 7): inside the mirror-decode present, when `psm == 0x02 ∧ fbp == 0 ∧ width == 512 ∧ m_scanout_texture`, a 32×28 luma grid over the decoded scanout classifies the frame black (`< 5 %` samples above 16), a brightness probe of the FMV stage at `gs_vram[0x150000]` computes `fmv_source_bright` (SESSION 12; diagnostics only since SESSION 14), and the display black-run (`s_fmv_have_good ∧ g_vsync_frame ≥ 400 ∧ s_fmv_blackrun ≤ HOLD_CAP = 8`) sets `fmv_hold_prev`, under which `glTexImage2D` is skipped and the previous (bright) picture is re-presented. At peak the RT present arm (`r184::ScanoutRtOn()` default ON, `:25030-25036`) shows the FMV from the target's colour view; `ScanoutFromVRAM` is reached only by the stale-display round trip (L1, HOOK_RECLASSIFICATION) whose picked target is the CT32 main buffer, never the `psm == 0x02 ∧ fbp == 0` display — the predicate is unreachable at peak. Decision: NOT transplanted (`ScanoutComposer::PresentMirror` uploads every decoded frame); its PS2 rule would be "the CRTC re-displays the last complete field until a new picture is written", a host cadence artifact of the non-frame-locked CRTC fallback present. If integration ever routes an FMV through the mirror present, this is the arm to re-adjudicate (accepted-difference protocol), not a gap to rediscover. (FILEMAP_graphics R15/R26 context.) [IDOC-A 2026-10-01]

## 10. Structural (header mutable globals, link seams, layering violations)

PS2 rule for the kind: none — host-architecture debt. ARCH §11 bans mutable globals in
headers (A1: 184 across 23 headers); ARCH bans session-named namespaces; platform/
owns #ifdefs; collaborators are constructor-injected, not function-pointer slots.

- Header mutable-global population (A1 §2.3, one entry for the pattern): gl_state_cache.h (65), timeline.h (18), ps2_threading.h:86-112 (12 registries/mutexes + thread_local), present_pace.h (12), ee_pace.h/pacebrk.h/turbo.h/warp.h (5-10 each), gs_page_tracker.h g_author[512], gs_c3_counters.h, time_domain.h:219-231 (atomics + s_m mutex + s_det), stick_trace.h, loadspan.h ctr:: statics, snd_census.h, budget_lift.h, mid_census.h State struct, intc.h queues map + id counter, iop_context.h/ps2_vblank.h globals (g_vblank_cdvd_fired, g_vblank_tick_ns), vif.h census atomics, input_latency.h, gs_deposit_overlay.h 4 MiB static shadow, ring_log.h t_ring/registry, ee_timers.h public raw members — all become owned class members in .cpp TUs (ThreadManager, EmuDomain, TickSource, VramImage, PageRecord, GlDevice, PerfMonitor, RingSink, Intc…).
- vu_fast_exec.h 1,800 lines of always_inline function bodies in a header (forced by the AOT TU split) + g_vu_ocean_active probe gate mutated from the LQ hot path — bodies into cached-tier .cpp post-NOAOT; probe global dies.
- gif_arbiter.h function definitions in a header (DrainThreadLoop + pool) — .cpp split on move (ARCH §13).
- Function-pointer link seams (one entry for the family): vu1_async SetEagerDrainHook + W16DmacDepthHook, gif_arbiter.h r192gif::MmioFenceHook + s8tail::DropHook, graphics_thread/call_census weak-symbol `ps2_census_current_frame` frame hook, mipsrc_diag `__attribute__((weak))` fallbacks, drawlist g_on_push, savestate.cpp:42-64 cross-TU forward-declared serialize hooks, vu1_hle_runtime.cpp:430-437 static-init `s_install` wiring, vu1_hle_programs static-init `s_reg_*` registration booleans — constructor injection / explicit registries (MtvuSync holds GifArbiter&; SectionProvider registration; declarative Vu1ProgramLibrary.entries; injected FrameClock).
- sce_hle_registry.cpp std::function wrapper per binding + map mutation + fresh-insert "Ghidra missed it" branch + dual sceSifFreeIopHeap registration papering over a manifest mislabel — const manifest array of plain function pointers; install becomes update-only; fix the signature adjudication in sce_lib_finder.
- syscalls.h SDL.h included by a kernel header + rabbitizer `#undef CONST/PURE` include-order dance + grab-bag include fan-out — no SDL below app/platform; rabbitizer quarantined inside dyn_interp.cpp; D4 split.
- gs_hle.h SDL types in an HLE header + g_sceGsSyncV_count frame counter as cross-subsystem pad-cadence coupling (libpad.cpp:15 includes it) — platform handles leave with platform/sdl; frame counter published by ee/timing.
- recompiler.cpp includes host_app/cpu_state.h + memory.h (tool depends on runtime) — emitted code includes runtime headers; the tool keeps only emission templates.
- gui.cpp `#ifdef RENDERER_OPENGL` compile-fork + thread_qos.h `#ifdef __APPLE__` in a shared header — backend capability flag (has_gui_overlay) + platform/ folders.
- libgraph.cpp:75-118 direct g_intc_queues container surgery (another subsystem's internals) — ee/intc registration API.
- Session-named namespaces as API (~21 in render.h: r119…sh5 + aquad; r120gl, r192gif, s8tail, w10unpkb, s83fognative, g3/g7c/g8par/g9/w14r5g10, depcopy2…) — banned by ARCH §11; concepts get named homes; gates dissolve.
- opengl_backend.h 75-field god-struct + m_ftt_* loose members as implicit out-params (reset-per-entry by discipline) — fields of the returned TextureSource (ARCH §4.5 "the m_ftt_* idiom is abolished"); TargetFBO decomposes into targets/ classes.
- kOpSrcName carries `const char*` as uint64 (same-process pointer smuggling; gs_direct_ops.h + vu1_hle_runtime.cpp:345) — handler-id → name table owned by the DrawIR stream (kName record).
- vu1_hle_runtime.cpp:196 `s_memo.size()>512 ⇒ clear()` paranoia bound (silent cache flush) — sized LRU or assert, owned by Vu1HleRuntime.
- memory.h g_watched_functions unordered_set at header scope + every-256th-fast-write fallthrough kept ONLY for KEY-REDIRECT sampling cadence — DIAG registry; fastmem unconditional after the root fix.
- hle_heap.cpp file name lies ("heap"; 90% cdvd/loader) — rename per concept at split.
- gs.cpp direct RenderJob pushes from register writes (bypassing the arbiter; the reason its fence exists) + per-site idempotence guards duplicated across five cases — scanout register changes flow through gs/hle/present via the queue; one guarded-write helper in GsPrivilegedRegs.
- gs.h GetWidth() `==0x03 ? 640 : 640` pointless ternary + duplicate `extern VideoState` declaration + NTSC default in a struct initializer — real CRTC rule (width from DISPLAY magh/DW) in scanout math; SMODE1/TitleProfile owns the default.
- syscalls.cpp KERNEL_SYSCALL_TABLE_BASE + custom_syscall_addresses[256] file-scope globals — SyscallDispatcher state. read_string_from_guest unbounded strlen on a guest pointer — bounded ee/mem accessor.
- iop_stubs.cpp:770 handler lookup fallback `handler & 0xFFFF` module-relative-then-absolute (masks a binding ambiguity) — iop_recompiler emits one canonical keying.
- iop_stubs.h old wrong-name stubs kept beside [S44]-corrected ones (iop_memcmp/prnt/strchr/index/strrchr) — delete dead aliases; libsd 16 numbered no-op stubs named by export ordinal for the divergence census.
- iop_memory.h out-of-range accesses printf-and-zero silently — count via triage (IOP fallback class), keep semantics.
- iop_context.h `iop_fix_zero` call-after-write $0 discipline — generator emits writes that skip $0; helper stays for stubs only.
- intc.cpp full-GPR copy save/restore per dispatch — heavyweight but correct; revisit under R5 when moving.
- ps2_threading.cpp ThreadExitException thrown through recompiled frames — legitimate but scattered; documented ThreadManager contract.
- input_backend_gamepad static-mutable class-level selection atoms (GUI coupling) — one GamepadBackend instance owned by the platform layer.
- blend_ff_map.h `#ifndef GL_*` ABI self-definition for off-GL tests — acceptable; unnecessary once the pair has a .cpp in the backend.
- dmac.h preprocessor bit macros (CHCR_*/DSTAT_*) + global handler map in a header — constexpr + registry class. (MFIFO/interleave/stall regs unimplemented in .cpp — **KEEP** fields; they are the dmac to-do list, not dead.)
- gs_page_tracker.h stale over-mark + save-time ReconcileStaleAtSave (repairing its own over-approximation) + linear-span vs rect-walk duality — PageRecord precise write_bits ([CX8]) makes reconcile unnecessary; ONE PsmLayout page walk.
- vu1_async.h boundary census feeders declared to spare gif_arbiter.h a perf include + hook-slot inversions so test binaries link — proper module layering + constructor injection.
- gs_state.h m_batch_texture_enable [BATCH-STATE-SNAPSHOT] (TME captured at first kick so a later register write can't repaint the batch) — PS2 rule: draw state is per-primitive at kick time; DrawIR carries full per-draw state so flush never reads live registers; field dies. StampTransferDestEpoch ad-hoc epoch system — absorbed by the ONE PageRecord (§1.5 rewrites all five trackers).

## 11. Build / config / path conventions & logging retrofits

- Four retrofitted log layers (main.cpp TagFilterBuf :125, [M3A] FastOn :174, [M4A] elision :331 + gs_state m4a::Elide* latches, PS2_LOG master :4905) + gs.cpp s_scanoutLog private fourth log + hw_verbose.h layer + debug_budget.h string-keyed mutex budget — ALL die into the core/log compile-time category logger (A2 finding 5).
- vif.cpp:100 [KICKCNT] prefix chosen to dodge main.cpp's TagFilterBuf denylist (logging system fighting itself) — core/log category VIF.
- ring_log.cpp env.txt "every PS2_* var" capture — becomes the effective Config dump (TRIAGE §7).
- sce_hle_bindings.inc regenerated into the SOURCE tree at configure time (host_app/CMakeLists.txt:69-82) + Python3-absent path silently shipping the stale checked-in copy — emit to ${CMAKE_BINARY_DIR}/games/<title>/gen/; absence = hard configure error.
- Root CMakeLists vu_aot.ini self-healing configure-write + CONFIGURE_DEPENDS glob self-heal (:150-168) — dies with AOT.
- Root CMakeLists Ghidra-absent placeholder logic + tests-skipped-when-no-JSON (:585) + IOP placeholder file(WRITE) fallback — v2-only world makes it unconditional; graceful absence via a proper stub target ([GD1]-style).
- `-Wno-error`/`-Wno-unused-*` blankets over generated code — scope to generated targets only.
- tests/CMakeLists.txt test targets recompiling whole host_app TUs with stub link partners (the "monolith link" symptom) — dissolves as subsystems become libraries.
- main.cpp:4874-4884 ISO path by "join every remaining argv" + chdir-to-project-root by `../../..` from the exe — Config + TitleProfile.elf/save paths; app/host resolves explicitly.
- main.cpp:14271 F8 slot path `save_states/ps2state_slot%d.ps2st` + slowspot_paths.h fixed `save_states/` flat dir + bug_report.cpp kRoot="bug_reports" + `logs/bundle_*` scatter + snd_bank_ledger `logs/*.tsv` sidecar — unified games/<title>/triage/ + save layout (TRIAGE §7/§8), Config-owned paths.
- savestate_writer.cpp:118 hardcoded "save_states" dir in ServicePpmCopy — destination passed by caller (triage bundle dir).
- run_play.sh build-v2-nog hardcoded binary path + 100-line flag manual in comments — dies with the launcher end of Phase 4; Config docs supersede.
- scripts/check_vu_aot_optimized.sh DWARF DW_AT_APPLE_optimized sniffing calibrated to Apple clang — moot after AOT deletion; general lesson (verify built artifacts' optimization class) → docs/bugs.
- Group 4.4 run_leg_gate `bg + sleep + pkill` crude bounding — replaced by the Phase-0 harness runner's process control.
- artdet_flicker.cpp bundle env echo of PS2_GS_SCALE/INTERP into txt — triage bundle's canonical config.json.

## 12. Narrative / session history in code

One entry per notable concentration (A2 counted 797 >15-line narration blocks tree-wide).
Rule: code keeps one-line hardware-invariant comments; history lives in docs/loop_state
(and docs/bugs for adjudicated defects).

- vu.cpp session-tag strata ([R31], [W13-*], [S8-*], loop-N; hundreds of lines) — strip to invariant one-liners.
- turbo.h 100+ lines of roadmap quotes/measurement history — one invariant paragraph stays ("counts-per-tick never change").
- ps2_vblank.h 190/380 lines of R139/R140 narration — compress to the two invariants (refill epoch source; >0-admit liveness).
- texcache_key.h 70 lines of R124/R125 defect history — compress to the cancellation-proof invariant.
- s8_ctxlatch.h 90 lines of session narration (qi numbers, /tmp paths) — history to loop_state; file dies anyway.
- gs_deswizzle.h:118-137 in-code derivation argument ("Wait - … Actually, looking at the PCSX2 visualizer…") with a dead variable — delete narrative; format layer carries one invariant + a golden test.
- vu1_hle_programs.cpp:6067-6130 /tmp/s75-s81 analysis narrations + vu1_hle_runtime "s84 dv2 evidence" comments — RecordId + one-liners.
- vu1_async.cpp:1627-1641 PATHSYNC_RELAX post-mortem — docs/bugs (BUG-002-adjacent).
- hle_heap.cpp archive stream-position semantics narrated in comments — title archive-table README.
- iop_loader.cpp:330-341 [S42] session post-mortem — delete.
- ring_tags.h session-born tags (FMV_PEND_EDGE, BOOT_SM_EDGE document game state words) — tag PRUNE at purge for tags whose emitters die (X-macro registry itself stays).
- recompiler.cpp theory notes (:3613 dispatcher analysis, :20753) + recompiler_tool/main.cpp ~150-line commented-out ancient host main + gif.cpp:2904-3180 / dmac.cpp:252-263 commented-out old code blocks — delete dead text; analyses to docs.

---

## Cross-references

- HOOK_RECLASSIFICATION.md §3 owns the per-hook dispositions (195 DIAG / 4 workaround /
  1 timing-HLE in main.cpp; §3.2 the 222+9 hand-body adjudication; §3.5-§3.8 the named
  workaround rows: 0x181490, 0x181560 meta-arena, KEY-REDIRECT, RENDINIT/SESSION-14/15).
- FLAG_LEDGER.md owns every env gate named above.
- A1 §4 (duplication census), A2 §2/§4/§5 (latch/mirror/probe populations), A6 §3
  (game-token census) are the audit ground truth these entries were checked against.
