# MIGRATION_PLAN — from the current tree to the ARCHITECTURE.md target

> **SUPERSEDED by `CONSTRUCTION_PLAN.md` (2026-09-26); retained for per-file
> dispositions.** The original repo is frozen read-only (`../REFERENCE_FREEZE.md`),
> so the in-place phases below (purge/delete waves, moves inside the OLD tree, the
> Phase-2 engine retirement) are no longer executable in this shape. What remains
> authoritative here: the §1 disposition map (§1.1–§1.6: every file's destination,
> verb, and engine/game split row — consumed unchanged through
> `../filemap/INDEX.md`), §3's per-family purge PROCEDURE (applied at transplant
> time instead of in waves), §4's risks (merged into CONSTRUCTION_PLAN §6), and
> Appendix A (ADD A SECOND GAME). Phase numbering in other documents that says
> "MIGRATION Phase N" maps through CONSTRUCTION_PLAN §A.3.

Companion to `ARCHITECTURE.md` (the target) and `EQUIVALENCE_PLAN.md` (the proof).
Sources: A1 (inventory/dead code/duplication), A2 (gate dispositions), A3 (GS
invariants), A4 (CPU/HLE/timing), A5 (verification assets), plus the external
reference-architecture study (`REFERENCE_ARCHITECTURES.md`) whose lessons are
marked `Informed by:` where applied. Repo under migration:
`$PS2R_OLD_ROOT`.

[REV 2026-09-22 D1/D2/D3] Revised per the three user decisions: (D1) the plan
targets ONLY the v2 recompiler engine — a new **Phase 2** retires the legacy
engine, the Ghidra JSON, recomp_overrides.ini, and the manual-body layer under
its own proof gate, and all later phases renumber +1; (D2) `hle/game/` does not
exist — every former address-bound override follows its
`HOOK_RECLASSIFICATION.md` class; (D3) destinations across §1 now use the
subsystem-owned tree (each PS2 subsystem holds its state model and its own
`hle/`; `runtime/` and central `hle/` prefixes are gone; `render/backends/`
holds the API adapters).

[REV2 2026-09-23 D4-D8] Revised per five further binding decisions (ARCH §13-§17):
**(D4)** every phase's moved clusters must pass the one-class-per-file lint —
§1 dispositions gain a "lint clean" obligation and `tests/lint/{lint_one_class,
lint_engine_purity}.py` land in Phase 0 as advisory, hard from Phase 3; **(D5)**
all §1 destinations acquire the `engine/` prefix (e.g. `gs/format/` reads
`engine/gs/format/`), game-bound halves land under
`games/<title>/`, and **§1.6 (new) gives every A6 MIXED
file its explicit engine-part/game-part split row**; the split executes
inside the existing phases (each phase moves its clusters to their final D5
homes — no separate "big split" phase) with Appendix A defining the
ADD-A-SECOND-GAME procedure; **(D6)** a new late **Phase 11** builds the mod
host + per-title manifest (mods-off remains the only certified config; the 250
workaround rows are NOT mods — HOOK_RECLASSIFICATION §6); **(D7)** Phase 10
expands to per-target platform folders behind the ONE Platform interface with
feasibility-spike gates (ARCH §16); **(D8)** `docs/bugs/` is seeded now
(TEMPLATE/INDEX/TRIAGE + 30 entries) and every phase's root-cause work appends
entries in the same commit as the fix.

Governing rule: **no phase lands without its proof gate passing** (defined per
phase below; mechanics in EQUIVALENCE_PLAN.md). One phase = one reviewable train
of commits; behavior changes are forbidden except where a phase explicitly declares
an accepted difference (EQUIVALENCE_PLAN §6).

---

## 1. Disposition map — every current file/cluster → destination

Verbs: **verbatim** (move + rename + include fixes only), **split** (content
partitioned into named modules, logic unchanged), **rewrite** (same behavior, new
implementation, oracle-gated), **delete** (with evidence it is dead or concluded).

### 1.1 Deletions (Phase 1: ~30k lines + stale machinery; [REV 2026-09-22 D1] plus the Phase 2 engine-retirement rows at the end of the table)

| Current | Evidence | Action |
|---|---|---|
| 23 host_app-root test_*.cpp (~19.6k ln) | zero CMake refs (A1 §5.1) | delete |
| ps2_scheduler_old_fiber.{cpp,h} (2,169 ln) + platform/*/fiber_* | superseded by ps2_threading; 0 build refs (A1 §5.1, A4 §2.3/B5) | delete |
| main_old.cpp, recomp_test.cpp, memory_test.cpp | dead scaffolds | delete |
| graphics/vulkan/* (2,034 ln + shaders + CMake branch) | stale since 2026-03-17, demoted in CMake (A1 §5.4) | delete (rebuilt later per ARCHITECTURE §6) |
| VU-AOT tier: vu_aot_gen.cpp, 33 chunk TUs, vu_aot_registry consumers, 6 ctest gates, 233 MB inc + 351 MB lib | default-OFF per NOAOT decision record (A1 §5.4, A4 §3.2) | delete |
| EEAnalyze: unused_function.cpp, jump_table_analysis.cpp, analyze_test.cpp, DataFlowEngine.cpp | not compiled (A1 §5.1) | delete |
| host_app/cmake_install.cmake; ~60 build_*.log, tmp_*.py, .gs.zst at repo root; .claude/worktrees clone | build/source boundary violations (A1 §5.4/§6) | delete / relocate to corpus |
| One-off diagnostics: bullseye_trace, cuelag_probe, foglane_live, loop_hist, pacebrk, mipsrc_diag, SEQ54-class hooks | concluded sessions (A2 §4) | delete |
| tests/swizzle/test_swizzle_equivalence.cpp (host_app/tests) | dead copy; live equivalents in tests/ | delete |
| [REV 2026-09-22 D1] Legacy recompiler engine: recompiler_tool legacy CLI (main.cpp), the RECOMP_ENGINE cache var + CMake legacy branch, the body-time special-case chain (recompiler.cpp :8904-19660 `if (base_address==0x…)` early returns) incl. the ~1,490-line FUN_00177168 hand emitter (:10409-11901) and the 143 embedded raw-string manual bodies, alias/compat emission for the Ghidra set | v2 general emission covers all of it, oracle-verified: REFACTOR_COMPLETE.md ("the 143 … no longer required by the v2 path"), RECOMP_P3A_IMPL.md (FUN_00177168 general-path body, 23-target table @0x1772c0), RECOMP_P4 corpus audit GAPS=0 | delete in Phase 2 (proof-gated) |
| [REV 2026-09-22 D1] game_files/ghidra_functions/extracted_functions.json + the CMake configure-time requirement on it | P4 no-Ghidra build: "[RECOMP-P4] Ghidra JSON absent: v2 PURE-DISCOVERY build" configures, builds (8,819 fns, 0 aliases/compat/pins), boots (REFACTOR_COMPLETE.md) | delete in Phase 2 |
| [REV 2026-09-22 D1] recomp_overrides.ini + the pin/pin-closure machinery (recompiler_tool/legacy/recomp_overrides_config.h) + switch_manual_enumeration.json (sole remaining consumer = ALL_MANUAL pin expansion, A4 §1.1) + the RecompV2OverridePin*/RecompOverridesConfig ctest gates | the pin channel exists only to re-emit legacy bodies; with the legacy engine gone there is nothing to pin to | delete in Phase 2 |
| [REV 2026-09-22 D1/D2] The runtime manual-body layer: manual_switch_dispatchers.cpp (235 installs), the 222 fun_*_body.inc, anim_pose_hle.cpp + anim_pose_hle_bodies.inc ([REV-2 2026-09-23] 9 installs — "installed (ok/9)"; only 0x294480 overlaps the 235, so 243 distinct addresses across the two layers), inject_fun_1017c8_helpers.{h,cpp} | HOOK_RECLASSIFICATION §3.2: emulation-bug workarounds for the legacy translation defect (root-fixed by v2) and the emission perf defect (root-fixed by ARCH §5.2 flat dispatch + fastmem emission) | delete in Phase 2, per-address harness adjudication (v2 body vs manual body; ledger to zero) |
| Compute-raster experiment: graphics/opengl/compute_data_layouts.h + shaders/cs_rasterize_{tri,sprite,line}.glsl + shaders/cs_scanout.glsl + the m_cs_* compile/dispatch plumbing in opengl_backend.{h:8 include, cpp:4112-4130} | ARCHITECTURE §6 verdict: GL 4.1 target, "no compute required — the compute-raster experiment does not survive"; readback path already inactive on macOS (A3 §5 VRAM-writers table) | delete — explicit row so the §1.4 directory-fallback rule cannot migrate these files; tests/test_integration_render_queue.cpp:54-65 drops its mirrored ComputeVertex/BatchUniforms blocks in the same commit [COVERAGE-FIX 2026-09-22] |

### 1.2 Verbatim moves

[REV 2026-09-22 D1/D3] Destinations follow the subsystem-owned tree (ARCH §3).

| Current | Destination |
|---|---|
| cpu_state.h (+ common/Pcsx2Types.h) | ee/cpu/ |
| hle/vu1_hle.h (contract) | vu/hle/contract.h |
| vu_decode_slot.h (canonical decode) | vu/decode.h |
| hle/gs_draw_ir.h + hle/drawlist_record.{h,cpp} + drawlist_reader.h | gs/hle/ir/ + core/diag/recorder |
| time_domain.h (timedom) | ee/timing/emu_domain.* |
| savestate_format.{h,cpp} + SAVESTATE_SCHEMA.md | core/savestate/ (format frozen) |
| input/ (mapper, backends, system) | platform/sdl/input/ |
| sound/ (sound_core, mixer, voice, vag_decoder, ring) | sound/ (+ platform audio sink) |
| texture_cache.{h,cpp} key logic (texcache_key.h) | gs/hle/texsrc/content_key.* |
| gif_arbiter.{h,cpp} | gif/arbiter.* |
| iop_recompiler/, sce_lib_finder tooling | tools/ |
| corpus pair files (RecompCorpusAudit fixtures) | tools/recompiler/config/ — the ONLY per-game metadata input that survives. [REV 2026-09-22 D1] recomp_overrides.ini, game_files/ghidra_functions/extracted_functions.json, switch_manual_enumeration.json: **deleted in Phase 2** (§1.1), not moved |
| gs_corpus/, vutrace_*/, save_states/ | corpus/ |
| tests/ replay tools + suites | tests/ (paths updated; env pins retired with their gates) |
| 222 fun_*_body.inc + manual_switch_dispatchers bodies | [REV 2026-09-22 D1] **not moved — retired.** They stay in place through the Phase 2 per-address adjudication (each is the OLD arm of its own harness leg) and delete with the ledger's completion (§1.1 last row; HOOK_RECLASSIFICATION §3.2) |
| third_party/imgui | app/gui/third_party/ |

### 1.3 Splits (largest first — the five megafiles)

[REV 2026-09-22 D2/D3] Destinations updated to the subsystem-owned tree; former
`hle/game` targets follow their HOOK_RECLASSIFICATION classes.

| Current | Destinations (split) |
|---|---|
| **opengl_backend.cpp** (25.6k) | render/backends/gl/ (device, surfaces, draw, readback, scanout, shaders) + gs/hle/targets/ (FindTargetTexture → target cache) + gs/hle/texsrc/ (ResolveTextureSeam + serve tiers → ResolveRead) + gs/vram/ (SyncRT/RefreshFBO → Materialize/deposit paths) + gs/hle/present/; the 6 *_native.inc → gs/hle/classes/*.cpp; in-TU test main() → tests/ |
| **main.cpp** (14.7k) | app/host/main.cpp (≤500 ln) + core/config loader + boot sequence → core/boot.*; [REV 2026-09-22 D2] the hook installs follow HOOK_RECLASSIFICATION: the ~195 DIAG dual-hooks are deleted; the 4 behavioral hooks (0x181560/0x1b36b8/0x268440/0x2ad110) are quarantined-then-retired at their root fixes (§3.1 there); the gated budget hook (0x191bd0) folds into ee/timing budget_anchor; S8-ARGV → ee/hle/boot_env.*; the manual-body installer call dies in Phase 2 (hook-count reconciliation [COVERAGE-FIX 2026-09-22]: **200** = live `install_dual_hook(` call sites, the authoritative figure — grep 2026-09-22: 201 matching lines − the definition at main.cpp:464; **A4's 191** = `install_dual_hook` sites at A4's earlier snapshot (156 distinct addresses); **A2's 208** = the broader "`install_dual_hook` etc." forensic-install pattern incl. sibling helpers/non-call mentions; **A1 §7's 172** = lines in main.cpp mutating `recompiled_functions[` directly — a different pattern (map writes, not wrapper calls; the wrapper body at :464 holds the actual writes), re-verified 2026-09-22 at exactly 172. All four bracket the same population; the behavioral-5 classification is count-independent) |
| **recompiler.cpp** (25.8k) | tools/recompiler/{ir/lifter, emit/*}; translate_instruction switch → ir/lifter tables; write_cpp_tail → emit/{tables,shards,runtime_seams}; embedded probes deleted at generator; [REV 2026-09-22 D1] the legacy special-case chain + embedded manual-body strings + FUN_00177168 hand emitter delete in Phase 2 (§1.1), so the split moves only the general path |
| **vu.cpp** (12.5k) | vu/ (state, flags, XGKICK) + vu/tiers/{interp,cached} + core/diag/vutrace; duplicated decode/fast-exec bodies deleted in favor of the header canon (A1 §4.2) |
| **sif_rpc_handlers.cpp** (6.1k) | sif/hle/rpc_router + cdvd/hle + mc/hle + pad/hle + sound/hle (per-module RPC handlers) + iop/hle/stream_irx; [REV 2026-09-22 D2] HandleCustomLoaderRecompiled → cdvd/hle/loader.* as HARDWARE/TIMING HLE (completion visibility, HOOK_RECLASSIFICATION §3.6; legacy twin deleted) |
| memory.cpp write_slow (3.5k switch) | ee/mem/mmio_router + per-subsystem handlers registered by dmac/, gif/, vif/, gs/, ee/timing, vu/; [REV 2026-09-22 D2] [KEY-REDIRECT] → named workaround module quarantined beside ee/mem, retired at its root fix (HOOK_RECLASSIFICATION §3.8) |
| render.h (3.7k grab-bag) | gs/hle/ir/ types + gs/hle/queue/ + config fields; vendored xxhash → core/hash.h; 5 gate namespaces dissolved by purge |
| gs_state.cpp (5.6k) | gs/state/{registers, vertex_kick, flush} + gs/xfer/ (WriteHWReg swizzle via gs/format) |
| gif.cpp (5.5k) | gif/parse (ParseSpan canonical; legacy ProcessPacked deleted) + gs/state/flush (FlushBatch merged into the ONE flush) |
| graphics_thread.cpp (4.3k) | gs/hle/queue consumer + gs/hle/present + core/diag taps |
| ps2_threading.{h,cpp} | ee/hle/threads.* (map leaves the header, A1 §2.3) |
| hle/vu1_hle_programs.cpp (6.2k) | vu/hle/programs/{ttx,pcs,lit,crys,skn,idx,zk,spr}.cpp + bitmath.h + registry.cpp (A4 §3.5) |
| texture_decoder.{h,cpp} | gs/format/ (tables, addressing, decode) + gs/hle/texsrc/ (decode caches) |
| sce_hle/* | [REV 2026-09-22 D2] per-subsystem hle/ as SdkBinding rows (ARCH §4.9): libpad→pad/hle, libmc→mc/hle, libcdvd→cdvd/hle, libmpeg+libipu→ipu/hle, libsdr→sound/hle, libgraph→gs/hle/sce, libdma→dmac/hle, libkernl/libscf/libdev→ee/hle, netcnfif→sif/hle; bindings .inc generated into build dir, never source tree |
| syscalls.cpp | ee/hle/syscalls.* + ee/hle/dyn_interp.* (fallback interpreter out of syscalls) |
| ee_timers/ps2_vblank/quantum/turbo/warp/ee_pace/present_pace/budget_* | ee/timing/* + ee/intc/ + gs/hle/present/pacing.* per ARCHITECTURE §4.7 |

### 1.4 Remaining files — covering clusters (completes the map)

Every host_app file not named in §§1.1–1.3 (or covered by a §1.5 rewrite row) is
assigned here, grouped by
destination. Verbs as in §1's key; "purge-dissolve" = a session gate header whose
winning arm is inlined into the owning module during the Phase 4 purge and whose
file then dies.

**EE kernel & runtime glue → `ee/`** [REV 2026-09-22 D2/D3]

| Current | Destination / verb |
|---|---|
| runtime.{cpp,h} (dispatcher entry, update_function_impl seam) | ee/cpu/dispatch.* + ee/hle glue — split |
| recomp_seams.cpp (frozen A4 §1.3 seam surface) | ee/cpu/seams.cpp — verbatim (surface frozen until Phase 8) |
| ps2_syscall_wrappers.{cpp,h} | merged into ee/hle/syscalls.* — split |
| intc.{cpp,h} | ee/intc/ — verbatim + MMIO handler registered via ee/mem router |
| memory.h, sema.h, ps2_argv.h | ee/mem/ · ee/hle/ · ee/hle/boot_env.* (S8-ARGV = kernel boot environment, HOOK_RECLASSIFICATION §3.7) — verbatim |
| common/Pcsx2Defs.h | ee/cpu/ (with Pcsx2Types, §1.2) — verbatim |
| w10_eefeed.h, s8_ctxlatch.h, s8_inrt.h | purge-dissolve into ee/timing + ee/hle (EEFEED is an A1 §5.3 expired hatch) |

**DMA path → `dmac/` · `vif/` · `gif/`** [REV 2026-09-22 D3]

| Current | Destination / verb |
|---|---|
| vif.{cpp,h} | vif/ — verbatim + gate purge |
| dmac.{cpp,h} | dmac/ — verbatim + gate purge; MMIO via ee/mem router |
| gif_template.h | gif/ packet-template helpers — verbatim |

**GS device & VRAM → `gs/`** [REV 2026-09-22 D3]

| Current | Destination / verb |
|---|---|
| gs.{cpp,h} (privileged regs) | split: gs/state/privileged.* + CSR/vsync handshake → ee/timing boundary + scanout regs → gs/hle/present |
| gs_deswizzle.h | gs/format — one of the 4 swizzle families collapsing (Phase 3 rewrite row) |
| gs_page_tracker.h, gs_deposit_overlay.h | absorbed by the ONE PageRecord rewrite (§1.5) — delete after Phase 5 |
| gs_vram_census.h | core/diag census family — move-to-diagnostics-build |

**Native renderer & backend headers → `gs/hle/` + `render/backends/gl/`** [REV 2026-09-22 D3]

| Current | Destination / verb |
|---|---|
| graphics/graphics_backend.{h,cpp} | absorbed by render/backend.h + gs/hle/queue consumer — rewrite |
| gs_interp.h | gs/hle/interp/ — verbatim |
| gs_class_renderer.h | gs/hle/classes/ dispatch — verbatim |
| gs_crtc_class.h | gs/hle/present/ — verbatim |
| gs_glbatch.h (GL staging RenderBatch) | render/backends/gl/ internal — split (must NOT survive as the IR, A3 finding 7) |
| gs_mipmap.h | addressing tables → gs/format; sourcing → gs/hle/texsrc — split |
| alias_rect.h, rt_copy_tracker.h, rt_neverstale.h (graphics/opengl/) | gs/hle/targets/ (CoherenceState inputs, §1.5 TargetFBO split) — split |
| s9_composite.h, s9_shadow.h (graphics/opengl/) | gs/hle/classes/ (CSM1 copy at s9_composite.h:322 dies into gs/format) — split |
| gl_state_cache.h (65 header globals) | render/backends/gl/ device state members — rewrite |
| blend_ff_map.h (graphics/opengl/) | render/backends/gl/ blend mapping (REVERSE_SUBTRACT [R178]) — verbatim |
| graphics/opengl/gs_scale.h | gs/hle/targets/ scale policy — verbatim |
| graphics/vconv.h, graphics/viewport_math.h | gs/hle/present/ fit/aspect math — verbatim |
| graphics/gpu_vertex.h | split: DrawIR vertex → gs/hle/ir; GL layout → render/backends/gl |
| graphics/gs_c3_counters.h | core/diag — move-to-diagnostics-build |
| uv_bbox.{h,cpp} | gs/hle/texsrc/ read-footprint math ([S109]/[CX8]) — verbatim |
| shuf_snap.h, idx_epoch_guard.h, idx16_lane.h | purge-dissolve into render/backends/gl + gs/hle/targets (GL_SHUF_SNAP/GL_IDXEPOCH are A1 §5.3 expired hatches) |

**Timing/pacing additions (beyond the §1.3 row)** [REV 2026-09-22 D3]

| Current | Destination / verb |
|---|---|
| timer_hle.{cpp,h} | ee/hle/ (sce timer/alarm HLE) with hooks into ee/timing — verbatim |
| vu1_async.{cpp,h} (MTVU worker) | vu/tiers/mtvu_worker.* — split (VIF-submit equivalence gate stays a test) |
| runahead_guard.h | vu/tiers/ (mtvu_depth bound → engine.mtvu_depth config) — verbatim |

**SDK/OS HLE → per-subsystem `hle/`** [REV 2026-09-22 D2/D3: the central `hle/sdk` is gone; each row lands in the subsystem it emulates, as SdkBinding rows where address-bound]

| Current | Destination / verb |
|---|---|
| mpeg_hle.{cpp,h} + ipu.{cpp,h} | ipu/hle/mpeg_ipu.* (FMV pipeline; PACE_AUTHORED folds into time.mode) — split |
| libc_hle.{cpp,h} | ee/hle/libc.* — verbatim (guarded-fallback install path preserved; SdkWrap rows, HOOK_RECLASSIFICATION §3.4) |
| pad_input.{cpp,h} | split: scePad protocol → pad/hle/; device polling → platform/sdl/input |
| cdvd_stream.{cpp,h} | cdvd/ streaming reader over ISO — verbatim |
| gs_hle.{cpp,h} (1,141 ln; read 2026-09-22) [COVERAGE-FIX 2026-09-22] [REV 2026-09-22 D2/D3] | split per concept: **hle_sceGsSyncV** (gs_hle.cpp:294) is the de-facto frame-boundary owner — sce surface → gs/hle/sce/; [R170-PACE] runahead park + VSync push + CSR field toggle → ee/timing BoundaryProtocol (ARCH §4.7) + gs/hle/present pacing; the [E2-SAVE] savestate probe/pending-save/**load consumption** (`if (savestate::AtFrameBoundary(ctx)) return;` at :558 — a load consumes the boundary and early-returns) → core/savestate quiescence hook. **hle_sceGsSyncPath/hle_sceDmaSync/hle_sceGifSyncDma** (:579/:750/:780) → gs/hle/sce + dmac/hle sync surface — verbatim. **hle_sceGsResetGraph/hle_sceGsResetPath** (:636/:720) → gs/hle/sce, privileged-reg writes routed via gs/state/privileged + gs/hle/present scanout. [REV-2 2026-09-23] **hle_SetupDisplayRegisters** (:1000; = FUN_0019aff0, GAME code — reads the game's display config struct @0x3D4AE0 with a Crash-NTSC fallback) → EMULATION-BUG-WORKAROUND per HOOK_RECLASSIFICATION §3.5: quarantined, retired when the recompiled FUN_0019aff0 runs against gs/state/privileged + gs/hle/present. **hle_sceGsSendImage** (:832, one of A3's 4 gs_hle VRAM writers) → gs/hle/sce over gs/xfer host→local deposit (MTVU order fence stays at vu/tiers). [REV-2 2026-09-23] **hle_InitGraphics** (:128; hardcoded game-struct writes 0x0030a788/0x002f6498 for fn 0x0019fb18) → DELETE as dead code per HOOK_RECLASSIFICATION §3.5 — never installed (no live emission, no runtime install; main.cpp references it only in comments); the window request moves to platform/sdl regardless. **g_sceGsSyncV_count** (pad-cadence coupling, gs_hle.h) → frame counter published by ee/timing, consumed by pad/hle. **g_window/g_renderer** globals → platform/sdl. **DELETE hle_RenderLoop + Ps2RenderNode + header decl**: body commented out (gs_hle.cpp:57–125), its recompiler emission also commented (recompiler.cpp:2144–2156), zero refs in generated shards. These functions are recompiler-injected seams — live emission at recompiler.cpp:1904–1998 binds them at fixed EE addresses (e.g. SyncV at 0x002c1d78, all in the sce_lib_finder SDK band except SetupDisplayRegisters at 0x0019aff0 [REV-2 2026-09-23]); of the 8 SDK-band seam addresses, 5 are already owned by sce registry rows (the registry installs last and wins — HOOK §3.5 table) and the 3 seam-only ones (0x2c1030 sceDmaSync, 0x2c1ab0 sceGsResetPath, 0x2c1b50 sceGsSyncPath) become SdkBinding manifest rows (signature-keyed, ARCH §4.9) in Phase 8, where the sce_hle/libgraph.cpp:17–18 thin wrappers fold into the same rows |

**Former "HLE game" rows — redistributed per HOOK_RECLASSIFICATION** [REV 2026-09-22 D2: `hle/game/` does not exist]

| Current | Destination / verb |
|---|---|
| hle_heap.{cpp,h} | cdvd/ — the file's live content per A4 §4.1 is the ISO9660 walker, deferred async read engine, DVD seek/transfer latency model and FMV sector streaming (the "heap" name is historical); the latency model is HARDWARE/TIMING HLE consuming ee/timing clocks — split + gate purge |
| hle_meta_arena.{cpp,h} | part of the 0x181560 workaround family (HOOK_RECLASSIFICATION §3.1: the arena is a host invention that isolates allocator metadata) — quarantined beside ee/mem, **retired at the root fix**; its META savestate section follows it out under the accepted-difference protocol when the root fix lands |
| anim_pose_hle.cpp + anim_pose_hle_bodies.inc | retired in Phase 2 (§1.1 last row; HOOK_RECLASSIFICATION §3.2 — emission-perf workaround, root-fixed by ARCH §5.2 flat dispatch + fastmem emission) |
| hle/loader_oracle.h, hle/loader_sync_hle.h | cdvd/hle/loader.* as HARDWARE/TIMING HLE (completion visibility, HOOK_RECLASSIFICATION §3.6) + oracle check remains a tests/ tool — split |
| hle/gs_direct_ops.h, hle/gs_packet.h | gs/hle/sce packet builders — verbatim |
| loader_prof.h, loadspan.h | loader probes → core/diag — move-to-diagnostics-build |

**VU runtime → `vu/`** [REV 2026-09-22 D3]

| Current | Destination / verb |
|---|---|
| hle/vu1_hle.cpp | vu/hle/ dispatch glue — split (contract.h already §1.2) |
| hle/vu1_hle_runtime.{cpp,h} | vu/hle/runtime.* tier entry (PS2_VU1_HLE → engine.vu1_mode) — split |
| vu_function_registry.{cpp,h} | merged into vu/hle/registry.* (§1.3 row) — split |
| vu_fast_exec.h | vu/tiers/cached (RunUpper twins deduped, A1 §4.2) — split |
| vu_span_classify.h, vu_hash_fnv.h | vu/ · core/hash.h — verbatim |
| vu_trace_case.h (Vu1Case format) | core/diag/vutrace + shared with tests/ corpus tools — verbatim. [REV3 2026-09-23 D9] the Vu1Case struct/writer is ALSO the fallback-capture format of core/triage (FallbackDetector vu1 arm, TRIAGE_CAPTURE §5.2) — one format, two producers |
| vu_aot_config.h, vu_aot_status.h, vu_aot_registry.h | delete with the AOT tier (§1.1 row) |

**IOP runtime → `iop/` · `sif/`** [REV 2026-09-22 D3] [IOPK 2026-09-24: the
cooperative kernel LANDED in the OLD tree (host_app/iop_kernel/, hatch
PS2_IOP_KERNEL default ON) — these rows now move settled code] [R1B: shipped
default OFF, BUG-026 — realtime boot stall (PHASE0_IMPL §12.2); the
deterministic profile pins `=1` + `PS2_IOP_DISPATCH=frame`]

| Current | Destination / verb |
|---|---|
| sif.{cpp,h} + sif_hle.{cpp,h} | sif/ (mailbox regs) + sif/hle/ (sceSif RPC handler bodies — [REV-2 2026-09-23] invoked through the 291 registry rows, e.g. sceSifCallRpc 0x2d29a8 via libkernl.cpp:112; NOT independent address bindings) — split. The 0x181490 piracy-check bypass in sif_hle.cpp is EMULATION-BUG-WORKAROUND (HOOK §3.5): quarantined as a named workaround-ledger row beside cdvd//sif until the Phase 7 adjudication |
| host_app/iop_kernel/{iop_kernel,iop_coroutine,iop_host_thread,iop_clock,sif_rpc_bridge,iop_module_table}.* | engine/iop/ — verbatim (already one class per file; ARCHITECTURE §3 + §7.1). The FRAME-policy pump call sites (gs_hle/sif_hle boundary, syscall edge, loader poll-spin) become boundary_protocol steps |
| iop_stubs.{cpp,h} | iop/hle/modules.* SPLIT one dir per stock module (thbase/thsemap routing now delegates to engine/iop/iop_kernel; the legacy per-guest-thread std::thread bodies + GuestMonitorYield waits are the PS2_IOP_KERNEL=0 hatch — DELETE at the Phase-7 NORM adjudication) + games/…/iop/stream_irx/ glue (worker stack/callback conventions) |
| iop_loader.{cpp,h} | iop/loader.* (IRX load / reboot handshake + the kernel boot hook OnModuleLoaded) — verbatim |
| iop_context.h, iop_memory.h, iop_sysmem_heap.h | iop/ (recompiled STREAM.IRX exec env) — verbatim |
| sif_rpc_handlers.cpp EeStreamRpcRequest/ServiceEeStreamRpc + EeRpcServiceCtxScope | sif/hle/rpc_router — the IOP→EE marshal (Wait::Rpc park + EE service points); HandleCustomLoaderRecompiled's RunOnRpcThread wrapper → games/…/iop/stream_irx/ dispatch glue |

**Savestate → `core/savestate/`**

| Current | Destination / verb |
|---|---|
| savestate.{cpp,h} | split: envelope/orchestration → core/savestate; per-subsystem section collectors register from their owning modules |
| savestate_writer.{cpp,h} | core/savestate/writer.* (async writer thread) — verbatim |

**App & GUI → `app/`**

| Current | Destination / verb |
|---|---|
| gui/gui.{cpp,h}, gui/savestate_browser.*, gui/sound_section.*, gui/vu_microcode_section.*, gui/bug_report_section.* | app/gui/ panels — verbatim (read Config + diag registry instead of env/globals). [REV3 2026-09-23 D9] bug_report_section becomes the "Triage" panel over core/triage (per-class counts/toggles, manual button, last result — TRIAGE_CAPTURE §12) |
| display_settings.{cpp,h} | split: persisted keys → core/config [video]; live geometry publication → gs/hle/present view state; ini round-trip test retargets the Config loader |
| shutdown_policy.h | app/host/shutdown.* (config shutdown_wait_s) — verbatim |

**Audio → `sound/`** [REV 2026-09-22 D3]

| Current | Destination / verb |
|---|---|
| snd_bank_ledger.{cpp,h} | sound/bank_ledger.* (bank residency consumed by mixer; fed by STREAM.IRX wire capture on the SIF RPC dispatch path — zero address hooks, verified 2026-09-22, consistent with D2) — verbatim |
| snd_census.h | core/diag census family — move-to-diagnostics-build |

**Durable diagnostics keep-list → `core/diag/` (PS2R_DIAGNOSTICS builds)**

| Current | Destination / verb |
|---|---|
| perf_profiler.h, bench.h, bench_report.h | core/diag/perf.* (A5 PERF/bench anchors) — [REV3 2026-09-23 D9] the totals-snapshot API additionally feeds core/triage bundle files (perf_buckets.tsv, TRIAGE_CAPTURE §5.7) |
| artdet_flicker.{cpp,h} | [REV3 2026-09-23 D9] split: detector/metrics → core/diag/artdet.* (diag builds; flicker ctest suite unchanged); the FIRE/WriteBundle path (artdet_flicker.cpp:193-250 flat files) DELETES — fires report through core/triage/TriageHost as a spike-class bundle (TRIAGE_CAPTURE §12) |
| slowspot.{cpp,h}, slowspot_detector.h, slowspot_paths.h | [REV3 2026-09-23 D9] core/triage/perf_monitor.* — the Detector core moves verbatim (O(1) p50 window), the capture trio (state/snap/_logs naming, slowspot_paths.h) retires into TriageBundler bundle layout; dev.slowspot_capture Config field absorbed by triage.* (ARCH §4.8) |
| bug_report.{cpp,h} | [REV3 2026-09-23 D9] core/triage/{triage_bundler,bundle_manifest}.* — Builder/manifest/retention/crash-sentinel logic generalizes (crash+manual become trigger kinds); bundle root moves bug_reports/ → games/<title>/triage/ (TRIAGE_CAPTURE §7/§12); unit test test_bug_report.cpp retargets the pure helpers verbatim |
| call_census.{cpp,h}, mid_census.h | core/diag census family |
| timeline.h, stick_trace.h, debug_budget.h | core/diag/ (stick_trace hosts the HEADWATCH/EULERTRACE probes) |
| ring_log.{h,cpp}, ring_log_format.h, ring_log_decode.h, ring_tags.h | core/log/ring.* sink + decode tool — verbatim |
| hw_verbose.h | delete — dissolves into core/log categories (Phase 3) |

**Platform seams → `platform/`**

| Current | Destination / verb |
|---|---|
| platform/platform.h, win_compat.h, backtrace_compat.h, crash_handler.h, proc_stats.h | platform/ common + platform/sdl — verbatim |
| thread_qos.h | platform/threads.* — verbatim |

**In-tree tests → `tests/`**

| Current | Destination / verb |
|---|---|
| input/test_input_mapper.cpp, input/test_pad_buffer.cpp | tests/ (live, unlike the §1.1 dead root set) — verbatim |

**Offline tooling → `tools/` · `verify/` · `tests/` [COVERAGE-FIX 2026-09-22: full tools/ cluster — 74 code files / 9,019 ln (A1 census = .py/.sh/.cpp excl. `__pycache__`; re-verified today: exactly 74 / 9,019)]**

| Destination / verb | Files (all 74 accounted; counts per group) |
|---|---|
| tools/recompiler/ — verbatim (4) [REV 2026-09-22 D1: gen_merge.py moved to the delete group — it lifts-and-merges Ghidra-split functions for the hand-rewrite flow, whose products (fun_*_body.inc) and input (Ghidra JSON) both retire in Phase 2] | recomp_lint.py, disasm_func.py, decode_instr.py, read_jt.py |
| tools/sce_lib_finder/ — verbatim, extends the §1.2 row (8) | sce_lib_finder/{\_\_init\_\_,\_\_main\_\_,ar_reader,elf_reader,mips_mask,sig_gen,sig_match}.py + detect_sdk_version.py (SDK 3.0.1 fingerprinting companion) |
| tools/diagnostics/ — verbatim (21) | GS-dump family: gs_dump_parser.py, parse_gsdump.py, summarize_gsdump.py, gsdump_state.py, gif_diff.py, dump_transfers.py, fmv_draws.py; log/trace: host_log_parser.py (runtime-log → DrawBatch, pairs with gs_dump_parser), vu1_trace_diff.py, snap_query.py (PS2_SNAP RAM-dump query), dump_pcsx2_memory.py, ramread.py (PINE ground-truth capture), ppm_to_png.py, pss_census.py (offline PSS demux mirroring mpeg_hle ProcessBuffer); ring_decode.cpp (the core/log ring decode tool named in the diagnostics keep-list row); flicker/{bbdiff,chromaflip,greensig,pixflip,spatsig,tdiff}.py (+ README.md rides along) — pairs with core/diag/artdet |
| verify/ — verbatim, Phase 0 harness inputs (5) | coldboot_gate.py (already named in Phase 0), compare_frames.py, run_bounded.sh, recomp_gates/{fps_class_gate,run_leg_gate}.sh |
| tests/ — verbatim (1) | loader_oracle_check.py (the "oracle check remains a tests/ tool" of the loader_hle row above) |
| delete — concluded one-off sessions (35) [REV 2026-09-22 D1: +gen_merge.py, Phase 2] | gen_merge.py (hand-rewrite lift-and-merge; Ghidra-input, products retired — Phase 2); bullseye (concluded per A2 §4): build_bullseye_scene.py, dump_bullseye_slots.py, find_bullseye_calls.py, correlate.py; logo/scene probes: logo_area.py, logo_timeseries.py, check_skydomes.py; backbuffer-dump census: analyze_bb_dumps.py, sample_frame.py, scan_rows.py, color_map.py, scan_tbps.py; tile/atlas one-offs: extract_tiles.py, render_tiles.py, tile_raw.py; CLUT probes: tmp_clut_full.py, tmp_clut_replay.py, tmp_pcsx2_clut.py; game-asset RE one-offs: parse_bh.py, dump_bh_entries.py, find_in_bh.py, parse_rm2.py; hand-rewrite campaign (complete — retired outright under D1): gen_1017c8.py, swap_to_include.py, check_inc.py, brace_check.py, check_empty_switch.py, dump_rewrite_targets.py, warning_worklist.py; phase-B integration campaign (complete): phaseb_{integrate,remaining,status,triage}.py; log slicing: per_frame_batches.sh |

Non-code strays in tools/ (outside the 74): the 7 worklist .txt files
(phaseb_{all,clean,exclude,problem,remaining}.txt, REWRITE_TODO.txt,
WARNING_WORKLIST.txt) delete with their concluded campaigns; `__pycache__/` ×3 and
.DS_Store are §1.1-class hygiene deletions. 4+8+21+5+1+35 = 74. ✓

**EEAnalyze → `tools/EEAnalyze/` [COVERAGE-FIX 2026-09-22: the 6 survivors named]**

| Current | Destination / verb |
|---|---|
| analyze.cpp, analyze.h, Function.h, register_state.h, DataFlowEngine.h, main.cpp — the 6 files surviving the §1.1 deletion of {unused_function, jump_table_analysis, analyze_test, DataFlowEngine}.cpp | tools/EEAnalyze/ — verbatim. Verified 2026-09-22: only analyze.cpp is compiled into the `EEAnalyze` STATIC lib (root CMakeLists.txt:28-32, links rabbitizer+elfio); recompiler_tool and recompiler_tool_v2 additionally compile analyze.cpp directly (CMakeLists.txt:45/:87) and link the lib (:50/:90) — [REV 2026-09-22 D1] the legacy recompiler_tool target dies in Phase 2, leaving the v2 tool as the only consumer beside the CLI; header chain analyze.h → Function.h → {register_state.h, DataFlowEngine.h} keeps DataFlowEngine.h alive as a type dependency after its .cpp dies; main.cpp is the standalone disasm driver (the README `disasm.exe` workflow), built by no current target — kept as the library's CLI entry. host_app links EEAnalyze (host_app/CMakeLists.txt:599) but includes **zero** EEAnalyze headers (grep 2026-09-22) — the runtime keeps its direct rabbitizer link and the vestigial EEAnalyze link is dropped at migration, restoring the "tools/ never linked into the runtime" layering rule (ARCH §3) |

**Build system (CMake) [COVERAGE-FIX 2026-09-22: A1 census row "CMake 4 / 1,356" resolved to files]**

| Current | Destination / verb |
|---|---|
| Root CMakeLists.txt (647 ln) + CMakePresets.json (63) + host_app/CMakeLists.txt (624) + host_app/cmake/gen_build_id.cmake (22) — the exact 4-file / 1,356-line A1 census set (line counts verified 2026-09-22). tests/CMakeLists.txt (3,173 ln) is counted in A1's *tests* bucket and moves with tests/; host_app/tests/swizzle/CMakeLists.txt (18 ln) dies with its §1.1 dead-copy row | rewrite into the ARCHITECTURE §3 tree's layout [REV 2026-09-22 D1/D3]: one top-level CMakeLists.txt (project, toolchain, options incl. PS2R_DIAGNOSTICS + the §1.5 Config schema knobs) adding one CMakeLists.txt per top-level dir (tools/, core/, the subsystem dirs ee/ iop/ dmac/ gif/ vif/ sif/ vu/ gs/ cdvd/ ipu/ pad/ mc/ sound/, render/, platform/, app/, tests/, verify/) as subdirectories; CMakePresets.json kept, presets re-pointed at the new option schema; gen_build_id.cmake → a cmake/ helper invoked by app/, still emitting build_id.cpp into ${CMAKE_BINARY_DIR} (generated/ stays build-dir-only per the tree); dead branches die with their §1.1 rows (Vulkan shader-compile branch host_app/CMakeLists.txt:499-553, VU-AOT machinery, sce-bindings write into the source tree redirected to the build dir, and [REV D1] the RECOMP_ENGINE=legacy|v2 selector + legacy generation target + the configure-time Ghidra-JSON requirement — one engine, one generation path) |

Completeness rule: any file not named in §§1.1–1.5 follows its directory's cluster
row above; the per-file authority is A1 Appendix A (602 rows). No file moves
without a row or a cluster assignment.

### 1.5 Rewrites (oracle-gated; same behavior by proof)

| Current | Rewrite | Oracle |
|---|---|---|
| 5 VRAM page trackers | ONE PageRecord + single-writer commit (ARCH §4.3) | SNAPVERIFY mismatches=0, corpus byte-identity, harness |
| 17 texture-serve paths | ONE ResolveRead with owner table (ARCH §4.5) | per-route case tests + corpus + harness |
| TargetFBO god-struct | 4-way split (ARCH §4.4) | corpus + harness |
| Two flush paths | one flush, two entry points | G-C1 verifier + corpus |
| 4 swizzle families | PsmLayout table | swizzle/block-decode equivalence tests extended to write/encode sides |
| Log layers ×4 | core/log categories | byte-identical kept-log output on anchors (M4A property) |
| getenv ×625 | Config schema | A2 registry: each var mapped or purged |
| std::map dispatch | flat tables | recomp oracle + determinism gates |
| 1 GB stacks / recursion | tail-dispatch trampoline | savestate chain tests + full harness (R5 contract unchanged) |
| [REV 2026-09-22 D2] three hook layers ([REV-2 2026-09-23] 747 address-bound rows: 200 dual-hook sites + 243 manual/anim distinct addresses + 291 sce + 4 libc + 1 sif piracy bypass + 3 seam-only sce seams + 1 game-address seam (0x0019aff0) + argv + KEY-REDIRECT + the loader completion-visibility model + the no-code retail-behaviour row — HOOK §2; the 5 registry-owned seam addresses count inside the 291) | SdkBindingManifest (signature-keyed, ARCH §4.9) + per-class dispositions in HOOK_RECLASSIFICATION.md; workarounds retire at root fixes | per-address harness adjudication (HOOK_RECLASSIFICATION §4); SdkBind address-keyed row count == 298 (291+4+3, HOOK §2) [REV-2]; zero game-address bindings (lint) |

### 1.6 [REV2 2026-09-23 D4-D8] Engine/game split rows — every A6 MIXED file

One row per MIXED file/cluster named by audit A6 (§2-§7 there; classification
basis = comment-stripped code tokens). Columns: what stays in `engine/`
(mechanism) and what moves to `games/<title>/` (data/
plugin). Rows already RETIRED by D1/D2 are marked — their "split" is deletion.
GAME-classified companions that ride a MIXED cluster are listed with it.
Executed inside the phases named; every landed file must pass `LintOneClass`.

| A6 MIXED file (cluster) | Engine part → where | Game part → where | Phase |
|---|---|---|---|
| `hle/vu1_hle.h` + `vu1_hle.cpp` (A6 §2.2, row A) | Entry/Register/Lookup/Analyze registry + family-dispatcher walk MECHANISM → `engine/vu/hle/` (Analyze parameterized by `Vu1FamilySignature`) | the 5 family constants {kVariantBase 0x1720, dispatcher op 0x090E0003, exit stubs 0x70/0x90, TPC 0xA0, prefix_is_family} → `games/…/vu1_library/` signature | 9 |
| `hle/vu1_hle_runtime.{h,cpp}` (row A) | enable gates, per-image dispatch, interpreter fallback, DIRECT→DrawIR lowering ladder → `engine/vu/hle/runtime.*` | round-1 TPC exit contract note rides the family signature | 9 |
| `hle/vu1_hle_programs.cpp` 6,207 ln (row A; GAME content on MIXED cluster) | ~21 generic float helpers (~140 ln: CF/VuDiv/Ftoi0/ClipNf/Xform*/MacLane…) single-sourced with vu.cpp → `engine/vu/vu_float.{h,cpp}` | 15 native programs + 137 entries/120 hashes + ~40 game-convention helpers (ClipGateB0, Run*Call, VsIndexedDisplace, …) + [FP-CONTRACT] note → `games/…/vu1_library/` (one family per file, D4) | 9 |
| `gs_class_renderer.h` 585 ln (A6 §3.1, row B) | ClassRenderer interface, registry, hatch envs, counters, fallback ladder → `engine/gs/hle/classrdr/` (one class per file: class_renderer.{h,cpp}, class_registry.{h,cpp}) | `ClassifyFields()` strstr taxonomy (10 classes keyed on this game's VU1 program names) + S9 raw signature + blit threshold → `games/…/renderer_profile.cpp` {stamps, raw} | 6 |
| `world_tristrip_native.inc` 1,300 ln (row B) | tristrip idiom lowering → `engine/gs/hle/classrdr/world_tristrip_renderer.{h,cpp}` | AcceptStrip census tuple (CT16S/fbmsk/DATE/GEQUAL row) + `frame_fbp==0x230000` special-case + World2 T8 handoff trigger → profile accepts/vram | 6 |
| `s9_composite_native.inc` 2,273 ln + `s9_composite.h` 383 + `s9_shadow.h` 254 (row B; the .h pair is GAME) | stencil-winding/composite lowering + sequence-FSM MECHANISM → `engine/gs/hle/classrdr/stencil_composite_renderer.{h,cpp}` | the seven-pass S9 recipe (CLEAR 0x78787878 / SEED / ZCOPY→0x268000 / INVERT T8H / STRIPS / QUANT / COMPOSITE) as a declarative `SequenceRecipe` + 0x230000/0x280000 pins → profile sequences/vram | 6 |
| `sprite_hud_native.inc` 1,679 ln (row B) | sprite-quad idiom (incl. roundSprite convention CONSUMER) → `engine/gs/hle/classrdr/sprite_hud_renderer.{h,cpp}` | Accept tuples + direct-MSCAL family bounds (micro 0x1748) + 0x230000 pin → profile; round_sprite_mode=1 → TitleProfile.quirks | 6 |
| `skinned_native.inc` 1,680 ln (row B) | skinned-mesh STQ idiom → `engine/gs/hle/classrdr/skinned_renderer.{h,cpp}` | AcceptShape census tuple + 0x230000 pin → profile | 6 |
| `particles_native.inc` 1,667 ln (row B) | particle idiom → `engine/gs/hle/classrdr/particles_renderer.{h,cpp}` | AcceptShape + census note (class is textured T8+CLUT in every capture) → profile | 6 |
| `fmv_blit_native.inc` 1,633 ln (row B) | fullscreen-blit idiom → `engine/gs/hle/classrdr/fmv_blit_renderer.{h,cpp}` | family-F decomposition (6 semantic families, 820/6,831 draws) + 0x230000/0x150000 staging pins → profile sequences/vram | 6 |
| `rt_neverstale.h` 351 ln (row B) | the generic "RT-backed reads never consume stale mirror bytes" rule → `engine/gs/hle/texsrc/` (ResolveRead freshness ladder) | the `vs_crys_dual` 0x280000 env-lane trigger context → profile | 6 |
| `foglane_live.h` (row B) | generic live-attachment serve for PSMT8H composites → `engine/gs/hle/texsrc/` | fog-lane trigger data (lane signature, 0x280000/0x2F0000) → profile vram | 6 |
| `opengl_backend.cpp` 25.6k ln, 145 VRAM hits (row C) | device/surfaces/draw/readback/scanout → `engine/render/backends/gl/`; target cache → `engine/gs/hle/targets/`; ResolveRead → `engine/gs/hle/texsrc/`; deposit/materialize → `engine/gs/vram/` (per §1.3) | the 28 distinct VRAM base literals ×145 (0x230000×71, 0x280000×69, 0x150000×43, CLUT pairings :6772, SPLASH-COMPOSITE-SKIP :8559 …) → `VramLayout` queries; every `fbp==CONST` becomes `layout.is(…)` | 6 |
| `gs_state.cpp` 5.6k ln, 47 hits (row C) | registers/vertex_kick/flush → `engine/gs/state/` + `engine/gs/xfer/` (per §1.3) | kTbps[] {0x370000,0x3c0000,0x3f0000}, tile-atlas formula 0x380000+tile·0x10000(+0x8000), ktap {0x70000,0x230000} → VramLayout; the :2909 `fbp==0x230000 ⇒ g_cutscene_active` latch → a TitleProfile scene-hook | 6 |
| `graphics_thread.cpp` 4.3k ln, 6 hits (row C) | queue consumer + present → `engine/gs/hle/{queue,present}/` | loading-logo 0x370000 special-case, base-layer 0x230000 tracking → profile vram/scene-hooks | 6 |
| `gif.cpp` (4 hits) + `gif_arbiter.cpp` (2 hits) (row C) | ParseSpan + arbiter → `engine/gif/` | :1349 forced `zbuf[0].zbp=0x150000` + 3 other literals → VramLayout | 6 |
| `texture_decoder.{h,cpp}` (7 hits) (row C) | format tables/addressing/decode → `engine/gs/format/`; decode caches → `engine/gs/hle/texsrc/` | 7 VRAM literals → VramLayout | 3/6 |
| `main.cpp` 14.7k ln, 596 code hits (row D) | boot/composition → `app/host/` + `engine/core/boot.*` (per §1.3) | **retired**: 195 DIAG hooks delete; 4 behavioral + KEY-REDIRECT root-fixed (Phase 7); budget hook → engine ee/timing model with params in TitleProfile.quirks; S8-ARGV value → TitleProfile.boot (writer = engine ee/hle/boot_env) | 2/7 |
| `manual_switch_dispatchers.cpp` 2,303 ln + 222 `fun_*_body.inc` + `anim_pose_hle_bodies.inc` (row D) | dual-hook/registry mechanism + shadow stack → `engine/ee/cpu/` (already frozen seams) | **retired** by D1/D2 per-address adjudication (§1.1); survivors reclassify per HOOK — none become games/ code | 2 |
| `hle/loader_sync_hle.h` 224 + `hle/loader_oracle.h` 144 + main.cpp [CHUNK-APPLY-SCOPED] (row E; GAME) | completion-visibility MECHANISM (hold/decision core, drain-edge counting) → `engine/cdvd/hle/loader.*` | the FUN_0017d6b8/FUN_002ad110/FUN_002aa8d0-shaped model parameters + spin-site ra 0x17d724 + drain-edge fn set → `games/…/hle/loader_sync.cpp` (TitleHleModule) | 7 |
| `budget_math.h` + main.cpp:3226-3608 (row E; GAME) | `DerivedClocks.budget_anchor` emu-domain mechanism + boot-phase classifier → `engine/ee/timing/` | FUN_00191bd0 model params (bases 8400-9700, thresholds, ra sites) → TitleProfile.quirks.budget (`games/…/hle/budget.cpp`) | 7 |
| `ps2_argv.h` [S8-ARGV] 115 ln (row E; GAME) | kernel boot-environment writer → `engine/ee/hle/boot_env.*` | argv string `cdrom0:\SLUS_123.45;1` + args_base 0x30a980 → TitleProfile.boot | 7 |
| `sif_hle.cpp` piracy bypass 0x181490 (row E) | sceSif handler bodies → `engine/sif/hle/` (invoked via the 291 registry rows) | the bypass is a WORKAROUND row (HOOK §3.5) — adjudicated Phase 7; if the v2 body needs a patch it becomes a TitleProfile.patches row, never engine code | 7 |
| `sce_hle_bindings.inc` 307 ln generated (row F; GAME) | 13 sce lib TUs (API semantics) stay `engine/*/hle/`; sce_lib_finder stays `engine/tools/` | the generated address table regenerates per title → `games/<title>/gen/` (22 EE imports for this game) | 7/8 |
| `iop_loader.cpp` 451 ln (row G; GAME) | IRX-load/reboot MECHANISM → `engine/iop/loader.*` | `DATASYS/STREAM.IRX` location/discovery on THIS ISO → title data (path in TitleProfile.iop_modules.custom) | 7 |
| `iop_stubs.cpp` 2,422 ln (row G) | thbase/sifman/sysclib Sony-API kernel stubs → `engine/iop/hle/modules.*` | STREAM.IRX worker stack/callback conventions (:758,:1278,:1362,:1675) → `games/…/iop/stream_irx/` glue | 7 |
| `sif_rpc_handlers.cpp` 6,131 ln (row G) | Sony module RPC HLE (pad/mc/cdvd/libsd) → per-subsystem `engine/*/hle/` (per §1.3) | STREAM.IRX/`lti_` custom protocol handlers + EE recv buffer 0x3B3000 → `games/…/iop/stream_irx/` | 7 |
| `sound/` 5 TUs 2,097 ln (row G) | SPU2 voice/ADSR/VOLX/NAX core → `engine/sound/` | feed-protocol contract points pinned to the recompiled STREAM.IRX (sound_core.cpp:98/:105/:810/:1122, voice.h:88) → `games/…/iop/stream_irx/` feed contract | 7 |
| `iop_recompiler` driver main.cpp + `ExportIopFunctions.java` 672 ln (row G/J) | rabbitizer-based IOP MIPS→C++ emitter → `engine/tools/iop_recompiler/` | STREAM.IRX input wiring → per-title recomp config (TitleProfile.recomp) | 8 |
| `hle_heap.cpp` 3,995 ln (row C/G) | ISO9660 walker + async read engine + DVD latency model → `engine/cdvd/` (per §1.4) | game allocator re-entry sites (:719-768 `0x1e2db0/0x1e2958/0x1e74a8`) — retire with the 0x181560 workaround family (Phase 7); cdvd file-table → title data | 7 |
| `memory.cpp` 5,373 ln (row H) | 32 MB model, translate/read/write, mmio_router → `engine/ee/mem/` (per §1.3) | hardcoded ELF path (:140) + section table (:166-170 `.vutext 0x2D9A10…`) → derived from TitleProfile.elf at load; hardcode deleted | 7 |
| `savestate.cpp` 2,450 ln (row I) | envelope/section registry/quiescence → `engine/core/savestate/` | title-tied section PROVIDERS (IOPM incl. STREAM.IRX state, STREAM recv @0x3B3000, MCHL, SDRX) → `games/…/savestate/` registered at boot | 7 |
| `recompiler_tool/recompiler.cpp` 25.8k ln, 1,373 hits (row J) | v2 MIPS→C++ engine → `engine/tools/recompiler/` (per §1.3 split) | per-address emission special-cases (:903 0x2d1a50, :935 0x2044f8, :963 0x204740, :987 0x1ffdc0, :1008 0x194040 …) + `inject_fun_1017c8_helpers.{h,cpp}` — **retired by D1** (Phase 2); game inputs (ELF, corpus pairs) → TitleProfile.recomp | 2/8 |
| `ps2_scheduler_old_fiber.cpp` (row — DEAD) | — | — deleted Phase 1 (A1 §5.1); drops out of every count | 1 |
| Diagnostics probes ~54 files (row K) | ring log, perf profiler, census MECHANISMS → `engine/core/diag/` | scene-keyed probes (splash tiles, foglane triggers, s8_*) — mostly DELETED per D2/A2 gate purge; survivors become title-profile-scoped probe configs | 4 |

Completeness: rows above cover every file A6 names as MIXED plus their
GAME-classified riders; any A6-MIXED file not named here follows its cluster's
row (the per-file authority remains A6's classification TSV at
`/tmp/s114/final_classification.tsv`, archived with the audit).

---

## 2. Phases — order, entry criteria, proof gates

Phase 0 exists because A5's verdict is that pixels/audio are currently diagnostics,
not gates, and the old adjudication protocol (one binary × two envs) dies with the
gates. Build the net before walking the wire.

[REV 2026-09-22 D1] Phase 2 is new — the engine decision lands EARLY so every
later phase gate runs v2-vs-v2 instead of proving everything twice; former
Phases 2–9 renumber to 3–10.

| Phase | Content | Entry criteria | Proof gate (must pass to land) |
|---|---|---|---|
| **0. Verification harness** | verify/: frame-hash mode (per-present FNV at native scale), input recorder (PS2_PAD_SCRIPT grammar writer), audio ring hash, memory checkpoints, headless present, deterministic profile preset, escalation scripts (EQUIVALENCE §5); wire coldboot_gate + corpus sweep + ctest into one runner; record suite cost | audits accepted | Harness self-test: OLD vs OLD twice on all anchors ⇒ identical hash streams; corpus sweep green; black-boot class caught by construction (pixel gate on every anchor) — **[P0 STATUS 2026-09-24: IMPLEMENTED — emitters (default-off, `PS2_VERIFY_OUT`) + `verify/` scripts + profile landed; behaviour-neutrality proven (19/19 corpus byte-identical, ctest 412/408/408 green, coldboot_gate PASS); self-test: coldboot + anchors NONDET on both binaries — two clock leaks found & PINNED (`PS2_SND_TICKCLOCK`, `PS2_FIXC_TICKCLOCK`), residual root cause = free-running IOP guest worker threads, assigned Phase 1+; gate verdict + full table in `docs/phases/PHASE0_IMPL.md`] [P0 STATUS 2026-09-25: **GATE PASSED** — legacy binary 12/12 legs ADMISSIBLE-STABLE at 3 reps under the deterministic profile (`PS2_EE_RUNTOKEN` + `PS2_IOP_KERNEL`/frame + `PS2_TICK_SYNC` pins; §11.5), baselines frozen (§11.9); v2 10/10 non-defect legs STABLE, its 2 P4FIX-F1 legs are deterministic crashes = Phase-2 entry work (§12.3); default-ON run token play-neutral in fps/music/input/load/FMV/gates (§12.1) except the cold-boot title load (BUG-038, first Phase-1 fix item, play fallback `PS2_EE_RUNTOKEN=0`); `PS2_IOP_KERNEL` realtime boot stall signature recorded, play default OFF (BUG-026, §12.2). **Phase-1 entry criterion MET.**]** |
| **1. Dead-code + hygiene purge** | §1.1 Phase-1 deletions; generated-into-source fixes (sce bindings → build dir); repo-root cleanup; **[2026-09-25] first runtime fix items carried in: BUG-038 (cold-boot title load under the run token) + BUG-026 realtime kernel stall + `ChangeThreadPriority` routing with a battery re-freeze** | Phase 0 green baseline captured — **MET 2026-09-25 (PHASE0_IMPL §12.3)** | build + full ctest + harness identical to baseline (bit-for-bit — deletions must be no-ops) |
| **2. v2-only recompiler — legacy + Ghidra + overrides retirement [REV 2026-09-22 D1]** | The engine decision, proof-gated: delete the legacy CLI, the RECOMP_ENGINE selector + legacy generation target, the emission special-case chain incl. the 143 embedded raw-string bodies and the FUN_00177168 hand emitter; remove the Ghidra extracted_functions.json input + its configure-time requirement; delete recomp_overrides.ini, the pin/pin-closure machinery, switch_manual_enumeration.json and their ctest gates; retire the runtime manual-body layer (235 manual_switch_dispatchers installs + 9 anim-pose installs; 243 distinct addresses [REV-2 2026-09-23]) per-address; root-cause and fix the two v2-correlated open defects — P4-F1 (f8606-era stream-restart spin: crash-or-wedge through 1 GB stacks, FUN_001f9058 recursion) and P4FIX-F1 (post-title-music cold-boot `[FATAL-NOJUMP] pc=0x0` via `[IND-JUMP-BAD] @PC=0x1f4940`, FUN_001f4730 callback slot reads 0) — both "v2 loses a race" timing-family emulation bugs (REFACTOR_COMPLETE.md limitations 2–3), fixed at the timing/OS layer, never pinned or hand-bodied | Phase 1 (harness green on the legacy-engine baseline) | v2-only build configures with the Ghidra JSON absent ("v2 PURE-DISCOVERY build": 8,819 fns, 0 aliases/compat/pins), builds, **boots and passes the full Phase-0 harness** vs the frozen legacy baseline (all anchors + cold boot + long legs INCLUDING the P4-F1/P4FIX-F1 repro legs — f8606/slowspot-f9743 resumes and 3200+ vf cold boots — crash 0); **RecompCorpusAudit green (GAPS=0)** on a corpus recorded from the v2-only binary; manual-body adjudication ledger complete: all 243 distinct addresses ([REV-2 2026-09-23] 235 manual + 8 anim-mirror-only; the mirror wins at the overlapping 0x294480) proven v2-body ≡ hand-body on the harness, every divergence root-caused (v2 bug fixed, or hidden fix reclassified per HOOK_RECLASSIFICATION) — end state 0 hand bodies; regen-determinism (v2) green; savestate cross-load: all legacy-era anchor states load and run under the v2-only binary (R5/fn-id contract) |
| **3. gs/format** | Extract PsmLayout/clut/texa/expand; collapse 4 swizzle families + 3 CSM1 copies + bpp switches; extend equivalence tests to write-side and encode-side | Phase 2 | swizzle/block/rect equivalence suites green incl. NEW write/encode cross-proofs; corpus byte-identical; harness identical. CT16-TEXA stays bug-compatible (fix deferred to an accepted-difference, EQUIVALENCE §6) |
| **4. config + log + diag** | Config schema + loader; core/log categories replacing 4 layers; core/diag module; **gate-purge waves A2 §7 ride here** (family-by-family, §3 below); recompiler-injected gates removed at generator. [REV3 2026-09-23 D9] **core/triage lands here** (TRIAGE_CAPTURE.md): TriageHost/Bundler/Manifest absorb bug_report; PerfMonitor absorbs slowspot; FallbackDetector arms (VU1/EE/RPC/GS/syscall report sites), SpikeDetector + FrameAnnotator, SavestateRing + InputLog (the Phase-0 input recorder REHOMED to core/triage — one implementation for harness + capture), TriageDedup + TriageWriter; tools/triage/ CLI (list/repro/lift/perf); bundle root `games/<title>/triage/`; artdet FIRE path rerouted | Phase 3 | per-family: two-binaries×one-profile harness identical; env-pinned ctest arms retired in the same commit as their gate; final state: getenv count = config loader only. [REV3 2026-09-23 D9] **Triage gate:** (i) a scripted 30-minute play leg (recorded-input, deterministic profile) produces **exactly one bundle per distinct fallback dedup key observed** — counters for repeats, no duplicates, no misses (counter totals reconcile against the [VU1-HLE-RT]/classrdr-style census lines); (ii) `triage repro` reproduces **100% of the leg's bundles headless** (fallback keys re-fire; perf/spike windows re-breach — wall-clock-nondet spikes must report NONDET, count ≤ 10%); (iii) capture-cost audit: no `self_inflicted`-unmasked spike attributable to TriageWriter/SavestateRing in the leg's frame series; (iv) unit suites (bug_report/slowspot pure-logic tests) green retargeted |
| **5. gs/vram** | ONE PageRecord; single-writer commit; ReadView/MaterializePages; delete upload-epoch/deposit/gsresident/gsovl/write-bits quintuple | Phase 4 (gates gone ⇒ one code path to port) | SNAPVERIFY-equivalent oracle mismatches=0; [GB4] save-time zero-stale test; corpus + harness identical; invariants 4–8, 15–16, 22, 31 as named tests |
| **6. gs/hle split (native renderer)** | DrawIR promoted in-flight (+ dither/mip/CLUT-bytes fields); ONE flush; TargetFBO 4-way split; ResolveRead resolver; render/backend.h extracted; render/backends/gl decomposed; class renderers as TUs; present/pacing module — all under gs/hle per D3 | Phase 5 | G-C1 verifier green; corpus re-frozen ONLY via adjudication ledger (recorder format change is an intentional-change re-freeze, EQUIVALENCE §6); harness identical; invariants 1–3, 9–14, 17–21, 23–30, 32–38 as named tests |
| **7. ee/timing + bindings + main dissolution** | TickSource/EmuDomain/BoundaryProtocol consolidation (budget hook folds into DerivedClocks.budget_anchor); SdkBindingManifest + ONE installer (per-subsystem contribution tables, ARCH §4.9); app/host/main ≤500 ln; ee/mem mmio_router; [REV D2] [REV-2 2026-09-23] the 6 remaining workaround-ledger rows root-fixed and deleted in four fix families per HOOK_RECLASSIFICATION §4 (boot-path family 0x1b36b8+0x268440 together; allocator family 0x181560+KEY-REDIRECT, with meta-arena riding the 0x181560 row; loader delivery 0x2ad110 with the retail overflow reproduced; the 0x181490 piracy-check bypass adjudicated — let the v2-emitted body run against cdvd/+sif/, HOOK §3.5) | Phase 6 | test_time_domain + pace unit gates; harness identical incl. long-play leg; [REV-2 2026-09-23] SdkBind address-keyed row count == **298** (the 291-row sce_lib_finder unambiguous match set + 4 libc + 3 seam-only sce addresses 0x2c1030/0x2c1ab0/0x2c1b50; the 5 registry-owned seam addresses collapse into their registry rows — HOOK §2/§3.5) + the S8-ARGV boot-env writer (not address-keyed) and **zero game-address rows** (lint — satisfiable now that 0x181490/0x0019aff0 are workaround-class, not bindings); workaround ledger at zero or each residual row carrying its root-fix record + harness-identical proof; savestate round-trip on all anchors |
| **8. recompiler IR** | MIPS→IR→C++ with verifier over the v2 (sole) engine; bounded-stack tail dispatch; flat dispatch tables; explicit entry tables; GenProbe manifest channel replaces env-gated generator injections; determinism gates kept ([REV D1] the "v2-only CLI" and manual-body relocation deliverables moved to Phase 2; the link-time override lane is dropped — ARCH §5.2) | Phase 7 (runtime seams stable) | recompiler-v2 oracle (selftest, planted-bugs, 600 s sweep, corpus audit) green; regen determinism; **golden-emission snapshots** frozen for the curated function set and diffed (Informed by: OpenGOAL `_REF` files); savestate cross-load OLD-state→NEW-binary on all anchors (R5/fn-id contract); harness identical; OPTIONAL extra arm: external EE instruction-test corpus (ps2autotests-class) recompiled and executed vs interpreter (Informed by: XenonRecomp running Xenia's PPC test corpus) |
| **9. vu module** | vu/ + tiers + hle/programs split; delete vu.cpp duplicate decode/fast bodies; registry as data | Phase 8 | Vu1CachedCompare 126/126 bit-exact; Vu1Hle 126/126; full 3,293 sweep FAIL=0; harness identical |
| **10. platform + ports** | SDL isolation into platform/; soft backend (oracle); **pipeline/shader cache** per ARCHITECTURE §6 (corpus-driven permutation enumeration; boot/load precompile; miss-during-gameplay = counted diagnostic — Informed by: XenosRecomp/Unleashed Recompiled, required for GXM); Vulkan rebuild (optional) with persisted VkPipelineCache; Vita spike: compile the subsystem libraries against vitasdk headers, stack/memory audit vs B1–B12, **explicit memory plan** (32 MB guest RAM + 4 MB VRAM mirror + shadow + caches vs the ~256–330 MB app budget; newlib-heap sizing/custom allocator per the d3es-vita/rvgl-vita precedent — Informed by: Vita ports; [REV D2] the 2 MB meta-arena drops out of the plan when its workaround retires in Phase 7) | Phase 9 | soft-vs-gl corpus diff within adjudicated bounds; subsystem libraries build with no SDL/GL symbols (link test); pipeline-cache warm-boot run shows zero draw-path compiles on the anchor set; Vita spike report vs blocker list incl. the memory-plan table |
| **11. Mod host + per-title manifest [REV2 2026-09-23 D6]** | `engine/ee/mods/` ModHost (manifest loader, registry resolution, hard-fail on unresolved entries, detour install via the update_function_impl seam with safe re-entry, savestate mod-set stamping); `games/<title>/mods/manifest.toml` (empty) + `symbols/` export surface (adjudicated names only) + the three documented examples (widescreen / 60fps unlock / debug camera — hypothetical); `--mods` flag in app/host; window-title MODDED stamp | Phase 10 (platform seams stable; HOOK ledger at zero so no workaround can masquerade as a mod) | Mods-off build with ModHost compiled-in-but-empty is BYTE-IDENTICAL to the Phase-10 baseline on the full harness (the standing CI assertion); manifest resolution unit tests (unresolved symbol/address/hash ⇒ load fails); a detour-install/uninstall round-trip leaves dispatch tables byte-identical; the examples load and stamp MODDED; equivalence harness NEVER runs with --mods (asserted in the runner) |

[REV2 2026-09-23 D4-D8] Cross-phase gate additions: (a) from Phase 3 onward
every phase's proof gate includes `LintOneClass` + `LintEnginePurity` green on
the files that phase moved (Phase 0 lands both lints advisory; §1.6 rows carry
the obligation); (b) each phase moves its clusters to their FINAL D5 homes
(`engine/…` / `games/<title>/…`) — there is no separate
split phase, and the engine-purity token count for `engine/` must be
monotonically non-increasing per phase, reaching 0 at Phase 9's gate (the last
game constants leave with the VU1 library); (c) Phase 10's Vita spike executes
the ARCH §16 feasibility-spike protocol (code size / RAM plan / GPU-caps corpus
check / CPU throughput on the [GD1] empty-profile leg) and its report is the
psvita folder's required feasibility note; wii/gamecube folders land as
`_template` clones containing ONLY their ARCH §16 verdict READMEs + CI stub
builds — no port work is planned for either.

Rules of engagement: phases are sequential; hotfixes to the OLD tree during
migration must be mirrored as accepted differences or ported forward before the
next phase gate; every phase updates docs/INVARIANTS.md checklist coverage —
and [REV2 D8] every root-caused defect adds/updates a `docs/bugs/` entry in the
same commit as its fix.

---

## 3. Gate-purge procedure (Phase 4 waves [REV 2026-09-22 D1: renumbered]; per env var, from the A2 registry)

Input: A2's 496-row CSV (dispositions verified against source). Unit of work is a
**family** (master + sub-gates + mirrors together, A2 §2.2), never a lone var.

Per-family procedure:

1. **Confirm the winner** (for `delete-losing-arm-keep-winner`, 208 vars): the code
   default (`!(e && e[0]=='0')` ⇒ ON-arm wins), run_play.sh CANON, and the
   introducing docs/loop_state record must AGREE. Disagreement ⇒ stop, adjudicate,
   document.
2. **Inline the winner**: make the winning branch unconditional; delete the latch,
   the losing branch, and every mirror latch (38 vars have >1 latch site — all go
   in one commit so mirrors cannot drift mid-purge).
3. **delete-both** (136 vars): delete the whole gated block; if the block references
   a fix, verify via the record that the fix is already unconditional elsewhere.
4. **move-to-diagnostics-build** (109) / **move-to-test-only** (14): relocate behind
   `PS2R_DIAGNOSTICS` / into tests; the runtime getenv site still dies.
5. **keep-as-setting** (29 registry rows, 27 effective — the canonical base; see
   ARCHITECTURE §4.8 for the derivation to the 32-field target schema): map to its
   Config field (A2 §6 schema);
   delete the getenv site.
6. **Generator-injected gates** (PS2_SHADOW_DIAG, PS2_EE_RAWDISP): purge in
   tools/recompiler, regenerate, then verify (A2 §7.4).
7. **Retire coupled tests in the same commit**: 143 ctest entries pin 17 distinct
   PS2_* values (A5 §1.2) — a purged gate's pinned arms are deleted or converted to
   the unconditional behavior's test.
8. **Prove the deletion safe**: run the Phase-0 harness OLD-binary vs PURGED-binary
   under the deterministic profile on the anchor set + corpus sweep. Identical ⇒
   land. Different ⇒ the "default" was not the shipped truth — stop and adjudicate
   (this is exactly the class of discovery the harness exists to catch).
9. **64 undocumented vars**: code-comment read first (no record exists); default to
   delete-both unless the comment carries a verdict.

Order of waves: (a) diagnostics/one-off probes (no behavior risk), (b) concluded
experiments, (c) losing arms in leaf subsystems (VIF/GIF/DMAC), (d) render-side
arms, (e) timing family last (interacts with the deterministic profile itself —
pin regime before touching it).

Expired default-ON hatches from A1 §5.3 (EEFEED, MTVU_DEFER, VU_RUNFLAT,
VU_HASHEPOCH, VU_RUNRETIRE, LIBC_HLE, FMV_NATIVE, SLOWSPOT_ASYNC, MTVU_* quartet,
LOADER_HLE, GS_DRAWIR, INTERP_PHASEFIX, FMV_COMPFIX, GL_SHUF_SNAP, GL_IDXEPOCH)
are wave (c)/(d) leads — each ships with its record's acceptance evidence converted
to a test.

## 4. Risk register

[REV 2026-09-22 D1/D2] Phase numbers updated; R14 replaced (the link-override
lane it guarded was dropped); R15 added for the Phase-2 engine retirement.

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| R1 | Savestate incompatibility breaks the ~40 anchor states (the harness's scene selectors) | med | critical | PS2SAVE1 format frozen; SectionSpec registry preserves layouts (GSST/THRD/VU1R sizeof guards); cross-load OLD-state→NEW-binary is a standing phase gate (incl. legacy-era states onto the v2-only binary, Phase 2 — proven both directions in the P4 record) |
| R2 | A "diagnostic" is secretly behavioral ([KEY-REDIRECT] precedent, A4 §2.2) | med | high | KEY-REDIRECT is now an explicit workaround row (HOOK_RECLASSIFICATION §3.8): quarantined as a named module, root-fixed and deleted in Phase 7 under the §4 adjudication ladder; purge step 8 (differential run) catches any other; memory-slow-path purge done under MEMWATCH-off harness runs |
| R3 | Hatch-off adjudication dies with the gates (A5 §7.4) | certain | high | That is Phase 0's purpose: two-binaries×one-profile replaces one-binary×two-envs BEFORE any purge wave |
| R4 | GL driver / machine variance pollutes differential runs | med | med | same-machine discipline; GL renderer string + env fingerprint stamped and asserted (gsrep precedent); scale=1 native hashes only |
| R5 | Latent CT16-TEXA wrongness gets "fixed" accidentally during gs/format collapse | med | med | Phase 3 explicitly preserves bug-compatibility; the real fix is a separate accepted-difference with its own re-freeze |
| R6 | MTVU / live nondeterminism leaks into gates | high | med | deterministic profile pins MTVU=0 (equivalence to on-path already gated at VIF-submit level); MTVU stays a config option verified by its own ctest arms |
| R7 | Generated-code compile cost regresses (2,068 s/TU history) | low | med | sharding is a budgeted output parameter; shard size in CI |
| R8 | Bounded-stack dispatch subtly changes park/resume semantics | med | high | R5 rule means host stack never held PS2 state; shadow-chain + fn-id containment tests + savestate round-trips gate Phase 8; worst-chain measured from existing THRD chains first |
| R9 | Corpus re-freezes hide real regressions during Phase 6 (recorder format change) | med | high | re-freeze only via the BASELINE.md adjudication ledger protocol: delta isolation (old recorder vs new on same run), bounded magnitudes, N× determinism (EQUIVALENCE §6) |
| R10 | .gsdl fidelity holes (CLUT bytes, mips, dither, deposits) mask replay divergence | known | med | Phase 6 extends DrawIR/recorder to carry them (ARCH §4.6); until then corpus gates are census/order/hash-level as today, and the harness's live frame-hash is the pixel authority |
| R11 | Parallel fix-development on OLD tree during migration | high | med | freeze windows per phase gate; any OLD fix lands with a record and is ported/adjudicated before the next gate |
| R12 | main.cpp dissolution drops a load-bearing boot ordering | med | high | boot sequence extracted as an explicit phase list (SdkBinding `phase` field); CpuThreadFunc order documented before dissolution; cold-boot pixel gate + all anchors |
| R13 | Runtime pipeline/shader compilation stutters or fails on weak GPUs (Vulkan first-run, GXM especially) | med (Vulkan) / certain (GXM) | med | ARCHITECTURE §6 pipeline-cache contract: corpus-driven permutation enumeration + boot/load precompile; miss-during-gameplay is a counted diagnostic that feeds the enumeration list; Phase 10 gate requires zero draw-path compiles on anchors (Informed by: Unleashed Recompiled/XenosRecomp; vitaGL compiler-cost precedent) |
| R14 | [REV 2026-09-22 D2] A retired manual body or workaround patch was silently load-bearing (a "re-implementation" that actually fixed behavior, or a patch masking a second defect behind the first) | med | high | per-address adjudication is the Phase 2/7 gate itself: v2-body-vs-manual-body and patched-vs-root-fixed runs must be hash-identical on the motivating scenes; any divergence is root-caused before deletion (HOOK_RECLASSIFICATION §4); the P4-proven cross-engine savestates give bisection anchors |
| R15 | [REV 2026-09-22 D1] The two v2-correlated defects (P4-F1 stream-restart spin, P4FIX-F1 cold-boot [FATAL-NOJUMP]) resist root-causing and stall Phase 2 | med | high | both are characterized with repro legs and site-level evidence (RECOMP_P4_IMPL/REVIEW §5, RECOMP_P4_FIX §3: FUN_001f9058 spin, [IND-JUMP-BAD] @PC=0x1f4940); they are timing-family races also latent under legacy — the Phase-0 harness + savestate bisection localize them; the fallback is NOT the legacy engine (D1 forbids it) but holding Phase 2 open while the fix is developed — later phases are sequenced behind it precisely so no work builds on the unfixed engine |
| R16 | [REV2 2026-09-23 D5] A "mechanism" half secretly depends on its game data's SHAPE (e.g. an idiom detector that only terminates on this game's ring layout) — the split compiles but a second title crashes the engine | med | med | the [GD1] empty-profile leg is a standing engine test (interpreter + generic DrawBatch with NULL library/profile); `LintEnginePurity` catches token-level leakage; interface types (DrawShapeSpec, SequenceRecipe, VramLayout, Vu1FamilySignature) are total — every field tri-state/optional with a defined empty behavior; BUG-023's lesson codified: data-driven termination, never fixed guards |
| R17 | [REV2 2026-09-23 D6] The mod surface erodes into a patch channel (mods shipping to "fix" behavior, recreating the D2 hook scatter) | med | high | hard rules in ARCH §15 + HOOK §6: harness runs mods-off, CI asserts empty-modhost byte-identity, detour targets resolve only against the adjudicated symbol map, and a PR adding a games/<title>/mods entry that changes any harness hash is rejected by construction (the harness never loads mods) |

---
## Appendix A [REV2 2026-09-23 D4-D8] — ADD A SECOND GAME (checklist)

What is authored BY HAND vs what TOOLING GENERATES, then the nine ordered
steps. The engine never changes; everything below lands under
`games/<new_title>/`.

| Artifact | Authored by hand | Generated by tooling |
|---|---|---|
| `title_profile.cpp` identity/quirks/boot | yes (from boot log + ELF header) | serial/crc/elf ranges dumped by `tools/recompiler` loader |
| Recompiled EE output | no | **recompiler run** over the new ELF (v2 pure discovery — no per-title tool changes allowed) |
| SDK bindings | no | **sce_lib_finder** signature match against the title's SDK drop → `gen/sce_hle_bindings` |
| IOP module list | stock list by hand from the boot log | custom-module recomp via `tools/iop_recompiler` (one plugin per custom IRX) |
| VU1 library | program lifts by hand (oracle-gated) | **VU capture→lift workflow**: vutrace capture → `vu1_replay --catalog` hash census → per-entry `--hle` proof |
| Renderer profile | Accept tuples/recipes transcribed by hand from census output | **renderer census** over the title's GS corpus (classes, shapes, families, VRAM bases) |
| VramLayout | no (transcribed, not invented) | FRAME/TEX0 census emits the base-address table |
| Title HLE (loader/budget) | yes — only if adjudicated through the HOOK five-class protocol | profiler/attribution tooling identifies the candidates |
| Corpus/baselines/savestates | play sessions by hand | **corpus freeze** via the recorder + BASELINE ledger protocol |
| Mods manifest | n/a (empty) | — |

Steps (each gate must pass before the next):

1. Dump ELF identity → fill `TitleProfile{serial, crc, elf, sdk}`; verify `gp`
   and import count from the ELF itself (this game: 0 and 22 — do not assume).
2. Enumerate IOP modules from the boot log; map stock modules to engine
   `iop/hle`; list customs — each custom = one `IopPlugin` recomp project
   (this game had exactly one, STREAM.IRX).
3. Recompiler run (v2, pure discovery) → `games/<title>/gen/`; boot with
   EMPTY VU1 library + EMPTY RendererProfile: interpreter + generic DrawBatch
   must render correctly — the [GD1] mods-off/profile-off leg is the
   correctness baseline and THE bring-up milestone.
4. sce_lib_finder against the title's SDK version → bindings into `gen/`;
   adjudicate ambiguous/out-of-band matches (never bind game addresses).
5. Capture VU trace corpus → `vu1_replay --catalog` → lift hottest images into
   the title's library entry by entry, each oracle-gated (`oracle_case_ids`,
   FAIL=0) — the A6 §2.3 workflow unchanged.
6. Capture GS corpus → renderer census → write stamp map + Accept tuples +
   sequence recipes as DATA; enable class renderers one hatch at a time under
   the byte-identity harness; fill VramLayout from the FRAME/TEX0 census (not
   by copying 0x230000 et al.).
7. Adjudicate any needed title HLE (loader spins, budget predicates) through
   the HOOK_RECLASSIFICATION five-class protocol — signature/surface/timing
   bound, never raw addresses.
8. Register the title's savestate SectionProviders; record a fresh anchor set;
   corpus freeze via the ledger.
9. Equivalence: two-binary harness against a reference-emulator capture for
   the new title (EQUIVALENCE_PLAN applies verbatim; the ladder is
   title-agnostic). Only then consider mods (`mods/manifest.toml` stays empty
   until Phase-11-equivalent machinery exists for the title).

---
*Next: EQUIVALENCE_PLAN.md defines the harness, the definition of "identical", and
the accepted-difference protocol referenced throughout.*
