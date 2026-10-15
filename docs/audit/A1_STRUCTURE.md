# A1 — Structure, Dependencies, Dead Code, Duplication

Audit stage A1 of the the title PS2 static-recomp runtime at
`$PS2R_OLD_ROOT` (read-only; no builds, no runs).
Scope: `host_app/`, `recompiler_tool/`, `iop_recompiler/`, `EEAnalyze/`, `tests/`, `tools/`, CMake.
Date: 2026-09-22. Method: scripted census (scripts preserved under `/tmp/s113/a1/`), line-anchored evidence.

---

## 0. Summary table

| # | Finding | Numbers | Evidence anchor |
|---|---------|---------|-----------------|
| 1 | Five megafiles hold 84k lines — 31% of host_app | recompiler.cpp 25,813; opengl_backend.cpp 25,598; main.cpp 14,690; vu.cpp 12,475; sif_rpc_handlers.cpp 6,131 | §1 |
| 2 | Two functions >9k lines; 12 functions >1.9k lines | `Recompiler::recompile_function` 12,784 ln; `main()` 9,849 ln | §3 |
| 3 | 499 distinct env vars at 662 `getenv()` sites; 109 documented "=0 restores" kill-switch arms | 90 getenv sites in opengl_backend.cpp alone | §5.3 |
| 4 | ~19,600 lines of dead in-tree tests in host_app root (23 files, zero CMake references) | test_rendering_architecture.cpp 1,614 ln etc. | §5.1 |
| 5 | ps2_scheduler_old_fiber.cpp (2,169 ln) dead — superseded by ps2_threading.cpp, referenced only in a comment | ps2_threading.cpp:1137 | §5.1 |
| 6 | Vulkan backend stale 6+ months, officially demoted in CMake comment | vulkan_backend.cpp mtime 2026-03-17; CMakeLists.txt:506 "the Vulkan backend is stale" | §5.4 |
| 7 | CLUT CSM1 swizzle duplicated 3× byte-identical (+1 different-formula 4th); PSM→pixels-per-write switch duplicated ≥2× inside gs_state.cpp | texture_decoder.cpp:944, opengl_backend.cpp:21667, s9_composite.h:322; gs_state.cpp:5066+5332 | §4 |
| 8 | vu.cpp ↔ vu_decode_slot.h: 58 duplicate 30-line windows (~1,700 lines) — deliberate verbatim extraction, both copies live | vu_decode_slot.h:1 "extracted verbatim from vu.cpp" | §4 |
| 9 | HLE hook installation scattered over 7 TUs; main.cpp alone mutates `recompiled_functions[...]` 172 times | main.cpp, sce_hle_registry.cpp, manual_switch_dispatchers.cpp, sif_hle.cpp, memory.cpp, iop_loader.cpp, (+41 in dead scheduler) | §7.7 |
| 10 | 184 mutable globals live in 23 headers; render.h is a 3,734-line grab-bag with 49 getenv sites, structs, hash lib, and 6 gate namespaces | gl_state_cache.h 65 globals; render.h:447-871 | §2 |
| 11 | Zero header include cycles (clean layering is achievable) | 0 cycles across 224 in-scope headers | §2 |
| 12 | 222 `fun_*_body.inc` files are HAND-WRITTEN manual reimplementations (not generated), all funneled through manual_switch_dispatchers.cpp | fun_00100008_body.inc:1 "MANUAL re-implementation" | §6 |
| 13 | Generated code contaminates the source tree: sce_hle_bindings.inc written into host_app/sce_hle/ at configure time; cmake_install.cmake leaked into host_app/ | host_app/CMakeLists.txt:66-77 | §6 |
| 14 | Default-ON gates aged past their hatch windows: PS2_EEFEED (7 wk), PS2_MTVU_DEFER (7 wk), PS2_VU_RUNFLAT (5.5 wk), PS2_LOADER_HLE (15 d), PS2_GS_DRAWIR (14 d) | docs/loop_state dates vs 2026-09-22 | §5.3 |
| 15 | tests/ is healthy: 118 executables, 166 registered tests, zero unreferenced test .cpp files | tests/CMakeLists.txt (3,173 ln) | §5.1 |

Grand totals (hand-written, excluding `fun_*_body.inc`, third_party, build dirs):
**602 files, ~374,000 lines** — host_app 329 files / 272,855 ln (56,339 of that vendored imgui),
tests 156 / 56,940, recompiler_tool 28 / 31,381, tools 74 / 9,019, EEAnalyze 10 / 2,106,
iop_recompiler 3 / 1,668, CMake 4 / 1,356.

---

## 1. File inventory

### 1.1 Subsystem totals (classified)

| Subsystem | Files | Lines | Notes |
|---|---:|---:|---|
| tests | 178 | 73,368 | includes 23 DEAD host_app-root test files (~19.6k ln) |
| third-party | 10 | 56,339 | vendored imgui 1.92.8 — exempt |
| tools | 113 | 42,528 | recompiler_tool + EEAnalyze + tools/ scripts |
| backend-GL | 17 | 41,033 | 62% is one file (opengl_backend.cpp) |
| EE | 34 | 34,228 | includes main.cpp monolith (14.7k) |
| HLE-game | 20 | 20,991 | heap/libc/anim/mpeg/vu1-hle/manual dispatchers |
| VU | 47 | 20,053 | vu.cpp + AOT chunks + decode/fast-exec headers |
| diagnostics | 54 | 14,616 | probes, paces, censuses, ring log, slowspot, bench |
| render-IR | 16 | 12,232 | render.h, graphics_thread, draw-IR, drawlist |
| SIF | 15 | 10,987 | SIF + RPC + IOP stubs + CDVD |
| GIF | 4 | 8,240 | gif.cpp + gif_arbiter |
| HLE-sdk | 25 | 8,051 | sce_hle/* + sound/* |
| GS-state | 7 | 8,021 | gs.cpp, gs_state.cpp, gs_hle.cpp |
| GS-format | 18 | 7,368 | swizzle, texture decode, cache, page tracker |
| platform | 27 | 4,132 | input, fibers, crash handlers, win compat |
| VIF | 2 | 3,958 | |
| DMAC | 2 | 3,212 | |
| backend-Vulkan | 2 | 2,034 | STALE |
| IOP | 3 | 1,668 | iop_recompiler tool |
| dead scaffolds | 3 | 136 | main_old.cpp, recomp_test.cpp, memory_test.cpp |

Classification observations with evidence:

- **backend-GL is one file plus satellites.** `host_app/graphics/opengl/opengl_backend.cpp`
  (25,598 ln) plus a `main()`-shaped test at its line 1512 (see §3 #23) plus 5 hand-written
  `*_native.inc` class renderers **#included at its tail** (world_tristrip_native.inc:5:
  "Compiled INSIDE the opengl_backend.cpp TU (#include at its tail) so every file-local
  gate/helper namespace ... resolves"). The TU-tail-include pattern makes file-local state the
  API boundary.
- **diagnostics is 54 files** of single-purpose probe headers in host_app root
  (turbo.h, warp.h, pacebrk.h, quantum.h, loop_hist.h, stick_trace.h, cuelag_probe.h,
  snd_census.h, mid_census.h, foglane_live.h, w10_eefeed.h, s8_ctxlatch.h, s8_inrt.h ...).
  host_app root holds **111 .h files** total — no directory structure separates
  hardware model / render / diagnostics.
- **VU counts 47 files** because the 33 `vu_aot_programs*.cpp` chunk aggregators
  (17 ln each) are checked-in fixed TUs for generated `.inc` payloads.

### 1.2 Full inventory

The complete 602-row table (file, lines, subsystem, purpose) is in **Appendix A** at the
bottom of this document.

---

## 2. Include-dependency graph

224 in-scope headers, 602 TU/header files scanned. Resolver: quoted includes against
including dir, host_app/, project root.

### 2.1 Most-included headers (direct TU count)

| Direct TUs | Header | Comment |
|---:|---|---|
| 64 | host_app/memory.h | memory API + inline fast paths + watch gates |
| 47 | host_app/vu.h | 1,171 ln; VU1 class + registers |
| 45 | host_app/render.h | 3,734 ln GRAB-BAG — see §2.4 |
| 36 | host_app/vu_decode_slot.h | constexpr decode twins (duplicates vu.cpp logic, §4.2) |
| 35 | host_app/cpu_state.h | EmotionEngineState — the recompiled-code ABI |
| 35 | host_app/vu_fast_exec.h | 1,833 ln of always_inline VU exec member defs |
| 34 | host_app/vu_aot_registry.h | AOT program descriptors |
| 32 | host_app/gs_state.h | GS drawing state |
| 31 | host_app/platform/win_compat.h | platform shim |
| 24 | host_app/intc.h | interrupt controller |
| 21 | host_app/ring_log.h, perf_profiler.h | diagnostics reach |

By transitive reach: `cpu_state.h` and `common/Pcsx2Types.h` reach **149 TUs** (everything),
`memory.h` 99, ring-log trio ~70, `gs_state.h` 58, and notably diagnostics headers
`ee_pace.h`/`timeline.h`/`pacebrk.h`/`uv_bbox.h`/`idx16_lane.h` reach 54–56 TUs each —
diagnostics is wired into the global include fabric, not layered on top.

### 2.2 Cycles

**Zero include cycles detected.** The dependency structure is acyclic — a clean
extraction into modules is topologically possible today.

### 2.3 Headers containing mutable globals / non-inline code

**184 mutable-global candidates across 23 headers** (`inline` variables and non-const
statics; constexpr excluded). Top offenders:

| Header | Globals | Sample (file:line) |
|---|---:|---|
| graphics/opengl/gl_state_cache.h | 65 | `inline GLuint g_fbo_draw` L99, `inline std::atomic<uint64_t> g_tex_gen{0}` L59 |
| timeline.h | 18 | `inline StageAgg g_stage[kStageCount]` L88 |
| ps2_threading.h | 12 | `inline std::unordered_map<int, std::shared_ptr<ThreadInfo>> g_threads` L175, `inline std::mutex g_thread_map_mutex` L176 |
| present_pace.h | 12 | pacing state atomics |
| ee_pace.h / pacebrk.h / turbo.h / warp.h etc. | 5–10 each | gate latches + counters |

The **kernel thread table itself lives in a header** (ps2_threading.h:175) — any TU
including it can mutate scheduler state directly.

### 2.4 render.h — the grab-bag header

3,734 lines, 45 direct includers, containing (line anchors):
RenderBatch/RenderJob/BatchTextureInfo structs (L31–340), VRAM epoch constants (L342),
namespaces `r119dep` L376, `r119` L443, `r120` L749, `r121` L810, `r123` L856 (per-round
gate clusters with 49 `getenv` sites, e.g. L447 `PS2_GS_SNAPSHOT_FAST`, L753 `PS2_GL_FAST2`),
and a **vendored xxhash implementation** (namespace `xxh`, L886+). This one header couples
the render queue contract to five optimization-round gate sets and a hash library.

---

## 3. The 40 largest functions

3,977 function definitions scanned; brace-count heuristic. "Concerns" = distinct
responsibilities observed in the body.

| # | Lines | Function | Location | Concern mix |
|--:|---:|---|---|---|
| 1 | 12,784 | `Recompiler::recompile_function` | recompiler_tool/recompiler.cpp:8938-21721 | per-address special cases, jump-table dispatch emission, HLE-hook injection, probe/log injection, branch analysis — one switch to rule the whole game |
| 2 | 9,849 | `main` | host_app/main.cpp:4843-14691 | CLI parsing, SDL/window init, env-gate latching, 172 hook installs, savestate autoload, thread spawn, GUI wiring, bench/report plumbing, shutdown policy |
| 3 | 8,077 | `Recompiler::write_cpp_tail` | recompiler_tool/recompiler.cpp:747-8823 | C++ text emission: registration init, dispatch tables, shard writers, recorder runtime — string-building of the entire artifact tail |
| 4 | 6,090 | `OpenGLBackend::DrawBatch` | graphics/opengl/opengl_backend.cpp:8288-14377 | texture source resolve, RT/FBO cache, CLUT, blend/test state, scissor, native-class dispatch, ~dozens of env-gated variants |
| 5 | 4,242 | `CpuThreadFunc` | host_app/main.cpp:594-4835 | EE boot, ELF entry, per-frame pump, MPEG/loader hooks, pacing, savestate, diagnostics |
| 6 | 3,950 | `OpenGLBackend::ResolveTextureSeam` | opengl_backend.cpp:4337-8286 | texture/RT aliasing resolution, shuffle detection, seam repair, format reinterpretation |
| 7 | 3,766 | `VU1::Execute` | host_app/vu.cpp:4320-8085 | fetch/decode/stall/flags/XGKICK + trace + AOT dispatch + superblock logic |
| 8 | 3,525 | `write_slow` | host_app/memory.cpp:1199-4723 | every MMIO register write for every subsystem + watchpoints + game-specific KEY-REDIRECT hack |
| 9 | 2,429 | `Recompiler::translate_instruction` | recompiler.cpp:22936-25364 | per-opcode C++ emission switch |
| 10 | 2,329 | `GraphicsThreadFunc` | host_app/graphics_thread.cpp:1965-4293 | job-queue loop, vsync/present, scanout, FMV path, GUI frame, interp-phase fix |
| 11 | 1,999 | `OpenGLBackend::FindTargetTextureImpl` | opengl_backend.cpp:19605-21603 | RT-as-texture lookup: format match, epoch staleness, alias rects, shuffles |
| 12 | 1,915 | `HandleCustomLoaderRecompiled` | host_app/sif_rpc_handlers.cpp:2935-4849 | game loader RPC: file table, streaming, decompress orchestration |
| 13 | 1,809 | `VU1::RunLower` | host_app/vu.cpp:9476-11284 | entire lower-pipeline opcode switch |
| 14 | 1,248 | `main` | tests/test_resmemo_equivalence.cpp:591 | test harness (acceptable) |
| 15 | 1,221 | `main` | tests/test_vu_superblock.cpp:359 | test harness |
| 16 | 1,190 | `GIF::FlushBatch` | host_app/gif.cpp:1705-2894 | batch assembly, prim conversion, texture info capture, queue push |
| 17 | 1,186 | `VU1::ExecuteCachedLoop` | host_app/vu.cpp:2696-3881 | cached-tier decode loop + hash-epoch elision + runflat |
| 18 | 1,086 | `ProcessDrawBatchJob` | graphics_thread.cpp:622-1707 | render-job unpack, backend call, scanout decisions |
| 19 | 1,011 | `DMAC::ProcessVifDmaChain` | host_app/dmac.cpp:1664-2674 | tag walk + VIF feed + MFIFO-ish edge cases + async VU1 routing |
| 20 | 902 | `PS2Scheduler::RunSchedulerLoop` | ps2_scheduler_old_fiber.cpp:649-1550 | DEAD (whole file unreferenced) |
| 21 | 897 | `HandleCustomLoader` | sif_rpc_handlers.cpp:5094-5990 | near-twin of #12 (legacy pair, §4.4) |
| 22 | 876 | `GifArbiter::drain` | host_app/gif_arbiter.cpp:933-1808 | PATH1/2/3 arbitration + timing windows |
| 23 | 859 | `OpenGLBackend::DrawScanout` | opengl_backend.cpp:24208-25066 | CRTC composite, interlace, scaling |
| 24 | 857 | `Recompiler::translate_control_flow` | recompiler.cpp:22007-22863 | branch/jump emission |
| 25 | 848 | `Interp::execute` | tests/recompiler_verify/interpreter.cpp:132 | reference interpreter (test oracle — fine) |
| 26 | 829 | `OpenGLBackend::SyncRTToVRAM` | opengl_backend.cpp:16272-17100 | GPU→CPU VRAM mirror writeback + format swizzle |
| 27 | 818 | `VU1::RunSpecial2Upper` | vu.cpp:8618-9435 | upper special ops |
| 28 | 817 | `VIF::ExecuteUnpack` | host_app/vif.cpp:2680-3496 | all UNPACK formats × masks × cycle stealing |
| 29 | 802 | `main` | tests/test_block_decode_equivalence.cpp:349 | test harness |
| 30 | 779 | `main` | tests/test_present_pace.cpp:43 | test harness |
| 31 | 763 | `VU1::ExecUpperFastEl` | host_app/vu_fast_exec.h:664-1426 | header-inline upper fast path (twin of RunUpper, §4.2) |
| 32 | 738 | `GIF::ParseSpan` | gif.cpp:3585-4322 | fast-path GIF parse (twin of legacy ProcessPacked, §4.4) |
| 33 | 710 | `main` | opengl_backend.cpp:1512-2221 | a test main INSIDE the backend TU (gated) |
| 34 | 657 | `GSState::Flush` | gs_state.cpp:304-960 | vertex-queue flush + batch cut policy |
| 35 | 653 | `VU1::Op_XGKICK` | vu.cpp:11737-12389 | XGKICK defer/window + PATH1 handoff |
| 36 | 648 | `OpenGLBackend::RefreshFBOFromVRAM` | opengl_backend.cpp:17484-18131 | CPU→GPU RT upload + deswizzle |
| 37 | 638 | `VIF::ProcessFifo` | vif.cpp:1229-1866 | FIFO state machine |
| 38 | 623 | `VIF::ExecuteCommand` | vif.cpp:1906-2528 | VIF command switch |
| 39 | 613 | `OpenGLBackend::ScanoutFromVRAM` | opengl_backend.cpp:18796-19408 | direct-VRAM scanout path |
| 40 | 611 | `GSState::KickVertexImpl` | gs_state.cpp:1207-1817 | vertex kick + prim assembly + sprite expansion |

Pattern: the runtime's per-frame spine (DrawBatch → FindTargetTexture → SyncRT/Refresh →
Scanout; Execute → RunLower/Upper; write_slow) is all 600–6,000-line functions where
hardware semantics, caching strategy, game-specific fixes, and env-gated experiments are
interleaved in the same bodies.

---

## 4. Duplication census

Method: normalized token hashing of 30-line windows (identifiers→`id`, numbers→`N`,
strings→`STR`) across host_app/recompiler_tool/iop_recompiler/EEAnalyze, plus manual
verification of known clusters. **329 cross-file duplicate windows in 23 file-sets.**

### 4.1 CLUT CSM1 swizzle — 3 byte-identical copies + 1 variant

| Copy | Location | Status |
|---|---|---|
| `CSM1Swizzle(uint32_t)` | host_app/texture_decoder.cpp:944 | **canonical** (file-static) |
| `R164Csm1Swizzle(uint32_t)` | host_app/graphics/opengl/opengl_backend.cpp:21667 | self-declared copy: "Local transcription of texture_decoder.cpp CSM1Swizzle (static there)" |
| `Csm1Swizzle(uint32_t)` | host_app/graphics/opengl/s9_composite.h:322 | third copy, inline |
| `csm1_unswizzle(int)` | host_app/gs_deswizzle.h:140 | DIFFERENT formula (row_order {0,2,1,3} table form) computing the same permutation family — a trap for maintainers |

All three identical bodies exist because the canonical one is `static` in a .cpp.
Refactor target: one `gs/format/clut.h` owning the permutation (and the gs_deswizzle
variant folded in or deleted).

### 4.2 vu.cpp ↔ vu_decode_slot.h / vu_fast_exec.h — deliberate live duplication

- **58 duplicate 30-line windows (~1,700 lines)** between vu.cpp and vu_decode_slot.h.
- vu_decode_slot.h:1 admits it: "*Shared constexpr VU slot decode cluster, extracted
  verbatim from vu.cpp ... both TUs compile the same bytes, so decode semantics cannot
  diverge*" — i.e. the original was left in vu.cpp instead of being replaced by the
  extraction. `DecodeStallStatics` (vu_decode_slot.h:21) "MUST mirror the decode half of
  ApplyFmacReadStalls statement-for-statement" (enforced only by a replay gate).
- vu_fast_exec.h `ExecUpperFastEl` (664-1426) is the fast twin of vu.cpp `RunUpper`
  (8104-8613) / `RunSpecial2Upper` (8618-9435).
- Canonical for refactor: the constexpr header versions; vu.cpp's in-file copies get deleted.

### 4.3 PSM format→size switches — repeated ad hoc

- `switch (transfer.dpsm) ... pixels-per-write {2,4,8,16}`: gs_state.cpp:5066 **and**
  gs_state.cpp:5332 (two copies inside/adjacent to `WriteHWReg`, one per fast/legacy arm),
  with sibling dpsm block-layout switches at 5104, 5131, 5175, 5356, 5384, 5509.
- `GetBitsPerPixel(psm)` defined 3×: opengl_backend.cpp:4275 (live),
  test_texture_shuffle.cpp:46 and test_rendering_architecture.cpp:1243 (dead tests).
- Inline `bpp = (psm == 0x02 || psm == 0x0A) ? 2u : 4u` scattered:
  graphics/opengl/alias_rect.h:150, opengl_backend.cpp:3436, 4218-4237, 4729, 5134…
- Swizzled address math exists in **three parallel families**: `GetAddressPSM*`
  (texture_decoder.cpp:306,645,675,694,964,996 — canonical, declared in
  texture_decoder.h:196-252), `addr_psmct32/16/psmt8` (gs_deswizzle.h:57,78,101 —
  table-driven duplicate), and dead copies in host_app/tests/swizzle/test_swizzle_equivalence.cpp:131-395.
- Canonical: texture_decoder's family; refactor should collapse gs_deswizzle.h into it
  behind a single `PsmLayout` descriptor (bpp, page/block dims, addr fn).

### 4.4 Fast/legacy pairs (kept-both-arms pattern)

221 "fast path/legacy path" comment markers; 109 "=0 restores" kill-switch arms. Largest pairs:

| Fast | Legacy | Evidence |
|---|---|---|
| `GIF::ParseSpan` (gif.cpp:3585, 738 ln) | per-vertex `ProcessPacked` switch | gif.cpp:3204 "TRUTH shared by the legacy ProcessPacked switch and the bulk fast loop in ParseSpan. Semantics transcribed verbatim" |
| `memory::read/write` inline fast path (memory.h:192,207) | `read_slow/write_slow` (memory.cpp:1199, 4769) | memory.h:106 "every-256th-fast-write fallthrough to write_slow" |
| `HandleCustomLoaderRecompiled` (sif_rpc_handlers.cpp:2935, 1,915 ln) | `HandleCustomLoader` (5094, 897 ln) | two generations of the loader RPC in one TU |
| VU cached tier `ExecuteCachedLoop` + fast-exec headers | `VU1::Execute` interpreter | vu.cpp:2696 vs 4320 |
| main.cpp `main()` | host_app/main_old.cpp | dead remnant |

### 4.5 Dead-test self-duplication (compounding the §5.1 corpse pile)

The 23 dead host_app test files duplicate each other heavily — top file-set pairs:
test_alpha_blend_preservation ↔ test_prim_change_preservation (88 windows),
test_rendering_bug_condition ↔ test_vblank_bug_condition (37), the 3-way
bug_condition cluster (24), test_comprehensive_rendering ↔ test_fbo_persistence_preservation
(22). Deleting the pile removes ~330 duplicate windows in one stroke.

---

## 5. Dead / stale code

### 5.1 Files referenced by no CMake target

Checked every `.cpp` against all three CMakeLists (GLOB dirs `platform/*`,
`graphics/{opengl,vulkan}/*` and conditional `vu_aot_programs_*` treated as referenced).

**Dead in-tree test/scaffold files — host_app root, ~19,600 lines, 23 files:**
test_rendering_architecture.cpp (1,614), test_preservation.cpp (1,246),
test_scanout_preservation.cpp (1,058), test_black_screen_scanout.cpp (918),
test_boot_preservation.cpp (862), test_prim_change_preservation.cpp (845),
test_bug_conditions.cpp (789), test_alpha_blend_preservation.cpp (745),
test_fbo_persistence_preservation.cpp (692), test_vblank_preservation.cpp (688),
test_vblank_bug_condition.cpp (676), test_rendering_bug_condition.cpp (638),
test_fbo_persistence_bug_condition.cpp (637), test_comprehensive_rendering.cpp (581),
test_texture_shuffle.cpp (519), test_prim_change_bug_condition.cpp (498),
test_boot_bug_condition.cpp (453), test_alpha_blend_bug_condition.cpp (453),
test_rendering_preservation.cpp (572), host_app/tests/swizzle/test_swizzle_equivalence.cpp (601),
memory_test.cpp (45), recomp_test.cpp (24), main_old.cpp (103).

**Dead subsystem implementation:** ps2_scheduler_old_fiber.cpp (2,169 ln) + .h —
replaced by ps2_threading.cpp ("std::thread-per-PS2-thread implementation. Replaces the
fiber-based PS2Scheduler", ps2_threading.cpp:1-2); only remaining mention is the comment at
ps2_threading.cpp:1137. Its 41 `recompiled_functions[...]` hook installs are dead weight
that still shows up in greps (§7.7 noise).

**Dead in EEAnalyze:** unused_function.cpp (921), jump_table_analysis.cpp (260),
analyze_test.cpp (163), DataFlowEngine.cpp (105) — only analyze.cpp is compiled
(root CMakeLists.txt:28-31).

**tests/ directory: clean.** Every tests/*.cpp is referenced; 118 executables / 166 ctest
registrations.

### 5.2 Function-level dead code

Grep census (definition with zero call sites outside self/dead files) is dominated by the
dead files above. Inside live TUs the recurring pattern is not dead functions but dead
*arms*: 109 `=0 restores` legacy branches (§5.3) — each is a function-sized chunk reachable
only by flipping an env var.

### 5.3 Env-gate population and expired hatches

- **499 distinct env vars**, 662 `getenv()` sites (opengl_backend.cpp 90, vu.cpp 63,
  render.h 49, main.cpp 45, graphics_thread.cpp 30, gs_state.cpp 21…).
- Naming encodes archaeology, not function: gates carry round tags
  (PS2_R197_D2P, PS2_R198_QORDER, PS2_S106_STAGE, PS2_SH5_BIND, PS2_W12…) —
  meaningless without the matching docs/loop_state record (635 records).

Default-ON gates past a 2-week hatch window as of 2026-09-22 (gate, default site, record, age):

| Hatch | Default-ON evidence | Record (docs/loop_state) | Age |
|---|---|---|---|
| PS2_EEFEED | w10_eefeed.h:4 "default ON; =0 restores every gated branch" | W10_IMPL.md 2026-08-04 | ~7 wk |
| PS2_MTVU_DEFER | vu1_async.cpp:1650 "default ON since the W8 checker accepted [R168-DEFER]" | CRASH_TRIAGE.md 2026-08-04 | ~7 wk |
| PS2_VU_RUNFLAT | vu.cpp:2908 "[W13-F1] RUNFLAT flat path (PS2_VU_RUNFLAT, default ON)" | W13_IMPL.md 2026-08-15 | ~5.5 wk |
| PS2_VU_HASHEPOCH | vu.cpp:2465 "[R151] default ON" | R151 era (≤2026-08-08 build logs) | >6 wk |
| PS2_VU_RUNRETIRE | vu.cpp:895 "[PERF1-A] latch (default ON)" | PERF1 era | >4 wk |
| PS2_LIBC_HLE | libc_hle.h:8 "default ON" | R138 era (≤2026-08-08) | >6 wk |
| PS2_FMV_NATIVE | host_app/CMakeLists.txt anim_pose comment "gated by PS2_FMV_NATIVE (default ON)" | R129 era (≤2026-08-08) | >6 wk |
| PS2_SLOWSPOT_ASYNC | savestate_writer.h:49 "default ON; =0 restores sync path" | W13R7 era | >5 wk |
| PS2_MTVU_SPIN / EAGER_DRAIN / VBSYNC / DYNDEPTH | vu1_async.cpp:606,1532,1616,244 | R123/R166/R168 era | >6 wk |
| PS2_LOADER_HLE | hle/loader_sync_hle.h:37,65 "default ON" | LOADER_HLE_IMPL.md 2026-09-07 | 15 d |
| PS2_GS_DRAWIR | hle/gs_draw_ir.h:2 "default ON since [GC3]" | GS_PHASEC_IMPL.md 2026-09-08 | 14 d |
| PS2_INTERP_PHASEFIX | graphics_thread.cpp:3348 "[S85] default ON" | S85 era | >3 wk |
| PS2_FMV_COMPFIX | graphics_thread.cpp:1451 "[R196-FMV4] default ON" | R196 era | >2 wk |
| PS2_GL_SHUF_SNAP | shuf_snap.h:45 "default ON" | SHUF era | >2 wk |
| PS2_GL_IDXEPOCH | idx_epoch_guard.h:37 "default ON" | IDX era | >2 wk |
| PS2_LOAD_BUDGET_LIFT | budget_lift.h:37 "default ON" | CAVERN_BUDGET_IMPL.md 2026-09-19 | 3 d (not yet purgeable) |

All rows except the last qualify for gate-purge (legacy arm deletion) under a
2-week policy.

### 5.4 Stale backends and build-system remnants

- **Vulkan backend**: graphics/vulkan/vulkan_backend.cpp mtime **2026-03-17**,
  .h 2026-03-16 (6 months untouched). Root CMakeLists.txt:503-508 comment: "the Vulkan
  backend is stale (Mar snapshot, GUI stubs) and stays an explicit opt-in".
  2,034 lines + 4 GLSL shaders + a whole CMake shader-compile branch
  (host_app/CMakeLists.txt:499-553) kept for a backend nobody configures.
- **PS2_VU_AOT=OFF is the default** (root CMakeLists.txt:97-116 "[NOAOT] ... the AOT tier is
  no longer needed"): vu_aot_gen.cpp (1,470 ln), 33 chunk TUs, scripts/check_vu_aot_optimized.sh
  and 6 ctest gates are retained-but-off machinery. NOAOT record documents the user
  decision — candidates for physical removal at refactor time.
- **host_app/cmake_install.cmake** (34 ln) — build artifact leaked into the source tree.
- Repo-root clutter outside scope but notable: ~60 build_*.log files, tmp_*.py scripts,
  .gs.zst dumps, and a `.claude/worktrees/...` clone carrying a full second copy of the
  222 fun_*_body.inc set.

---

## 6. Generated vs hand-written boundary

| Artifact | Generator | Lands in | Hand-owned neighbors |
|---|---|---|---|
| recompiled.h / recompiled.cpp / recompiled_shard_000..031.cpp | recompiler_tool (legacy) or recompiler_tool_v2 (RECOMP_ENGINE=v2) | `${CMAKE_BINARY_DIR}` (build-v2/, build-v2-nog/ present at root) | recomp_seams.cpp (v2 runtime seams); main.cpp hook installs mutate the generated map at boot |
| v2gen/, legacygen/ reference trees | recompiler_tool_v2 / recompiler_tool | `${CMAKE_BINARY_DIR}/v2gen`, `/legacygen` | consumed by tests/recompiler_v2 gates |
| iop_recompiled.h/.cpp | iop_recompiler_tool from STREAM.IRX (or CMake-written placeholder if absent) | `${CMAKE_BINARY_DIR}` | iop_loader.cpp, iop_stubs.cpp |
| vu_aot_generated.inc + _0.._31.inc + manifest | vu_aot_gen (only when PS2_VU_AOT=ON) | `${CMAKE_BINARY_DIR}` | **checked-in** host_app/vu_aot_programs.cpp + 32 chunk aggregator TUs that #include them |
| sce_hle/sce_hle_bindings.inc | host_app/sce_hle/generate_bindings.py at **configure time** | **source tree** host_app/sce_hle/ (host_app/CMakeLists.txt:66-77, input tmp_analysis/crash_matches_full.json) | sce_hle_registry.cpp #includes it at :541 |
| generated/input_default_bindings.h | CMake file(WRITE) from input/default_bindings.ini | `${CMAKE_BINARY_DIR}/generated` | input/input_mapper.cpp |
| build_id.cpp | cmake/gen_build_id.cmake every build | `${CMAKE_BINARY_DIR}` | build_id.h |
| Vulkan .spv shaders | glslc custom commands | `${CMAKE_BINARY_DIR}/shaders` | vulkan/shaders/*.glsl |

**Corrections to project folklore:**
1. The 222 `host_app/fun_*_body.inc` files (excluded from inventory per instructions as
   "generated") are in fact **hand-written manual reimplementations** — e.g.
   fun_00100008_body.inc:1: "MANUAL re-implementation — TRUE function `entry` (CRT0) ...
   Ground truth: Ghidra disasm ... confirmed byte-for-byte against PCSX2". All 222 are
   #included by manual_switch_dispatchers.cpp (+ anim_pose_hle.cpp via
   anim_pose_hle_bodies.inc); 0 orphans. They are game-HLE payload and must be
   treated as source, not artifacts.
2. The 5 `graphics/opengl/*_native.inc` files are hand-written class renderers compiled
   by #include at the opengl_backend.cpp tail (world_tristrip_native.inc:5) —
   hand-written code shaped like generated code because the backend's file-local
   gate namespaces are its only API.
3. One generated file (sce_hle_bindings.inc) is written into the source tree at configure
   time, from an input (`tmp_analysis/crash_matches_full.json`) living in a scratch
   directory — both violate the build/source boundary.

---

## 7. Concept-to-location map

For each concept: where it lives today (file count from grep census, full lists in
/tmp/s113/a1/concepts.txt) and the ONE place it should live in the refactor.

| # | Concept | Today (files touching it) | Canonical today | Should live in (refactor) |
|---|---|---|---|---|
| 1 | Swizzle / PSM address math | 18 files: texture_decoder.{h,cpp}, gs_deswizzle.h, gs_page_tracker.h, gs_vram_census.h, gs_state.cpp, gif.cpp, graphics_thread.cpp, opengl_backend.cpp, render.h, texcache_key.h, + 4 dead tests + 4 live tests | texture_decoder.cpp:306-1022 (GetAddressPSM*) | `gs/format/psm.{h,cpp}` — single PsmLayout table + addr fns |
| 2 | CLUT decode + CSM1 | 8 files: texture_decoder.cpp, opengl_backend.{h,cpp}, s9_composite.h, gs_deswizzle.h, gs_vram_census.h, render.h, test_s9_composite.cpp | texture_decoder.cpp:944 (CSM1Swizzle) + opengl_backend.cpp:21675 (EnsureClutTex) | `gs/format/clut.{h,cpp}` |
| 3 | Texture cache key | 2 files: texture_cache.{h,cpp} (+texcache_key.h) | texture_cache.h | already coherent → `gs/texcache/` |
| 4 | Render-target tracking | 11 files: opengl_backend.{h,cpp} (FindTargetTextureImpl :19605), rt_neverstale.h, rt_copy_tracker.h (header-impl), gl_state_cache.h, s9_composite.h, s9_shadow.h, graphics_thread.cpp, render.h + 3 tests | opengl_backend.cpp | `render/rt_cache.{h,cpp}` — backend-agnostic, IR-keyed |
| 5 | VRAM mirror sync (GPU↔CPU) | 26 files — the most smeared concept: SyncRTToVRAM (opengl_backend.cpp:16272), RefreshFBOFromVRAM (:17484), ScanoutFromVRAM (:18796), gs_page_tracker.h, gs_vram_census.h, alias_rect.h, idx_epoch_guard.h, idx16_lane.h, foglane_live.h, gs_deposit_overlay.h, gs_class_renderer.h, render.h epochs, gs_state.{h,cpp}, texture_decoder.{h,cpp} + 6 tests | opengl_backend.cpp | `render/vram_mirror.{h,cpp}` — one page-epoch tracker, one sync policy |
| 6 | VU1 program dispatch | 23 files: vu.cpp (ExecuteCachedLoop/Execute), vu_function_registry.{h,cpp}, vu_aot_registry.h, vu_aot_programs*.cpp, hle/vu1_hle*.{h,cpp}, vu1_async.{h,cpp}, vif.{h,cpp} (MSCAL), gif_arbiter.cpp, vu_fast_exec.h, vu_trace_case.h, recompiler_tool/vu_aot_gen.cpp | vu.cpp:2445 (tier dispatch: HLE→AOT→cached→interp) | `vu/dispatch.{h,cpp}` — one tier table |
| 7 | HLE hook table | 26 files; installs live in main.cpp (172 sites!), sce_hle/sce_hle_registry.cpp:474-599 (bindings.inc walk), manual_switch_dispatchers.cpp (5), sif_hle.cpp, memory.cpp, iop_loader.cpp, + dead scheduler (41) | none — main.cpp is the de-facto registry | `hle/hook_registry.{h,cpp}` — single install API + ordered pass list, main.cpp reduced to one call |
| 8 | Syscall table | 12 files: syscalls.cpp:2310 (runtime_syscall_dispatcher), syscalls.h, ps2_syscall_wrappers.{h,cpp}, sce_hle/libkernl.cpp (re-declares + calls ::runtime_syscall_dispatcher(34,...)), runtime.h + tests | syscalls.cpp | `ee/kernel/syscalls.{h,cpp}` |
| 9 | Timers / vblank | 59 files touch the strings; owners: ee_timers.{h,cpp}, timer_hle.{h,cpp}, ps2_vblank.{h,cpp} (VBlankWorkerFunc :563), intc.h, + pacing diagnostics (ee_pace.h, present_pace.h, time_domain.h, timeline.h…) that read them everywhere | ee_timers.cpp + ps2_vblank.cpp | `ee/timing/{timers,vblank}.{h,cpp}`; diagnostics consume via one clock API |
| 10 | Savestate serialization | 85 files reference it; core: savestate.{h,cpp} (2,450), savestate_format.{h,cpp}, savestate_writer.{h,cpp}, savestate_browser (GUI); every subsystem hand-registers its chunks | savestate.cpp + SAVESTATE_SCHEMA.md | `core/savestate/` with per-module serialize fns declared beside module state |
| 11 | Input | 20 files: input/input_{backend,backend_kbm,backend_gamepad,mapper,system}.{h,cpp} (L1/L2), pad_input.{h,cpp} (L3), sce_hle/libpad.cpp (L4 = SDK surface), main.cpp wiring, stick_trace.h | input/ (already layered per INPUT_DESIGN.md) | `platform/input/` + `hle/sdk/libpad.cpp` — keep, relocate |
| 12 | Audio | 25 files: sound/{sound_core,mixer,voice,vag_decoder,ring}.{h,cpp}, snd_bank_ledger.{h,cpp}, snd_census.h, gui/sound_section, sce_hle/libsdr.cpp, iop_stubs.cpp (libsd RPC), sif_rpc_handlers.cpp | sound/sound_core.cpp | `hle/sdk/sound/` — SPU2 boundary stays libsd-level |

Reading of the map: concepts 3, 11, 12 are already well-factored (small file sets, one
owner). Concepts 1, 2, 5, 7 are the smeared ones — and all four smear *into*
opengl_backend.cpp or main.cpp, confirming the two megafiles as the primary
decomposition targets.

---

## Appendix A — Full file inventory

Legend: subsystem classification is by observed content/role. Purposes for satellite
files are pattern-derived one-liners. Vendored third_party rows retained for
completeness. Full machine-readable copy: /tmp/s113/a1/inventory_table.md.

| backend-GL | 17 | 41,033 |
| EE | 34 | 34,228 |
| HLE-game | 20 | 20,991 |
| VU | 47 | 20,053 |
| diagnostics | 54 | 14,616 |
| render-IR | 16 | 12,232 |
| SIF | 15 | 10,987 |
| GIF | 4 | 8,240 |
| HLE-sdk | 25 | 8,051 |
| GS-state | 7 | 8,021 |
| GS-format | 18 | 7,368 |
| platform | 27 | 4,132 |
| VIF | 2 | 3,958 |
| DMAC | 2 | 3,212 |
| backend-Vulkan | 2 | 2,034 |
| IOP | 3 | 1,668 |
| unknown | 5 | 1,484 |
| dead | 3 | 136 |

### Full inventory (descending lines)

| File | Lines | Subsystem | Purpose |
|---|---:|---|---|
| recompiler_tool/recompiler.cpp | 25,813 | tools | EE MIPS->C++ recompiler |
| host_app/graphics/opengl/opengl_backend.cpp | 25,598 | backend-GL | OpenGL backend megafile: draw, texture, RT cache, scanout |
| host_app/third_party/imgui/imgui.cpp | 18,378 | third-party | Vendored Dear ImGui (exempt from refactor) |
| host_app/main.cpp | 14,690 | EE | MONOLITH: boot, hooks, threads, HLE, diagnostics (14.7k lines) |
| host_app/vu.cpp | 12,475 | VU | VU1/VU0 execution |
| host_app/third_party/imgui/imgui_widgets.cpp | 10,994 | third-party | Vendored Dear ImGui (exempt from refactor) |
| host_app/third_party/imgui/imgui_draw.cpp | 6,778 | third-party | Vendored Dear ImGui (exempt from refactor) |
| host_app/hle/vu1_hle_programs.cpp | 6,207 | HLE-game | VU1 microprogram HLE (native reimplementation) |
| host_app/sif_rpc_handlers.cpp | 6,131 | SIF | EE<->IOP bridge, RPC, CDVD, IOP stubs |
| host_app/gs_state.cpp | 5,623 | GS-state | GS register/vertex/transfer state |
| host_app/gif.cpp | 5,532 | GIF | GIF path arbitration / GIFtag parsing |
| host_app/memory.cpp | 5,373 | EE | EE kernel/CPU/memory/interrupts/timers |
| host_app/third_party/imgui/imstb_truetype.h | 5,085 | third-party | Vendored Dear ImGui (exempt from refactor) |
| host_app/third_party/imgui/imgui_tables.cpp | 4,665 | third-party | Vendored Dear ImGui (exempt from refactor) |
| host_app/graphics_thread.cpp | 4,341 | render-IR | Render queue/jobs and graphics thread |
| host_app/third_party/imgui/imgui.h | 4,246 | third-party | Vendored Dear ImGui (exempt from refactor) |
| host_app/third_party/imgui/imgui_internal.h | 4,024 | third-party | Vendored Dear ImGui (exempt from refactor) |
| host_app/hle_heap.cpp | 3,995 | HLE-game | Game/SDK function HLE (heap, libc, anim, mpeg) |
| host_app/render.h | 3,734 | render-IR | Render queue/jobs and graphics thread |
| host_app/vif.cpp | 3,609 | VIF | VIF command/UNPACK processing |
| tests/CMakeLists.txt | 3,173 | tests | Test/replay harness |
| host_app/texture_decoder.cpp | 3,049 | GS-format | PSM formats, swizzle, texture decode/cache, VRAM census |
| host_app/dmac.cpp | 3,034 | DMAC | DMA controller channels/chains |
| host_app/mpeg_hle.cpp | 2,520 | HLE-game | Game/SDK function HLE (heap, libc, anim, mpeg) |
| host_app/syscalls.cpp | 2,496 | EE | EE kernel/CPU/memory/interrupts/timers |
| host_app/savestate.cpp | 2,450 | EE | Savestate serialization |
| host_app/iop_stubs.cpp | 2,422 | SIF | EE<->IOP bridge, RPC, CDVD, IOP stubs |
| host_app/graphics/opengl/opengl_backend.h | 2,380 | backend-GL | OpenGL backend megafile: draw, texture, RT cache, scanout |
| host_app/manual_switch_dispatchers.cpp | 2,303 | HLE-game | Game/SDK function HLE (heap, libc, anim, mpeg) |
| host_app/graphics/opengl/s9_composite_native.inc | 2,273 | backend-GL | S9 shadow/composite native pass helpers (header-impl) |
| host_app/ps2_scheduler_old_fiber.cpp | 2,169 | EE | EE kernel/CPU/memory/interrupts/timers |
| host_app/anim_pose_hle_bodies.inc | 2,027 | HLE-game | Game/SDK function HLE (heap, libc, anim, mpeg) |
| tests/vif_replay.cpp | 1,950 | tests | Test/replay harness |
| host_app/vu1_async.cpp | 1,908 | VU | VU1/VU0 execution |
| tests/test_resmemo_equivalence.cpp | 1,838 | tests | Test/replay harness |
| host_app/graphics/vulkan/vulkan_backend.cpp | 1,836 | backend-Vulkan | Vulkan backend (STALE since 2026-03) |
| host_app/vu_fast_exec.h | 1,833 | VU | VU1/VU0 execution |
| host_app/gif_arbiter.cpp | 1,808 | GIF | GIF path arbitration / GIFtag parsing |
| host_app/graphics/opengl/skinned_native.inc | 1,680 | backend-GL | GL backend helper (header-implemented pass/cache logic) |
| host_app/graphics/opengl/sprite_hud_native.inc | 1,679 | backend-GL | GL backend helper (header-implemented pass/cache logic) |
| host_app/graphics/opengl/particles_native.inc | 1,667 | backend-GL | GL backend helper (header-implemented pass/cache logic) |
| host_app/graphics/opengl/fmv_blit_native.inc | 1,633 | backend-GL | GL backend helper (header-implemented pass/cache logic) |
| host_app/test_rendering_architecture.cpp | 1,614 | tests | DEAD in-tree test harness (not in any CMake target) |
| tests/test_vu_superblock.cpp | 1,579 | tests | Test/replay harness |
| host_app/third_party/imgui/imstb_textedit.h | 1,527 | third-party | Vendored Dear ImGui (exempt from refactor) |
| host_app/ps2_threading.cpp | 1,481 | EE | EE kernel/CPU/memory/interrupts/timers |
| recompiler_tool/vu_aot_gen.cpp | 1,470 | tools | VU1 AOT C++ generator (corpus->C++) |
| tests/test_idxtex_equivalence.cpp | 1,423 | tests | Test/replay harness |
| tests/recompiler_verify/interpreter.cpp | 1,307 | tests | Test/replay harness |
| host_app/graphics/opengl/world_tristrip_native.inc | 1,300 | backend-GL | GL backend helper (header-implemented pass/cache logic) |
| host_app/test_preservation.cpp | 1,246 | tests | DEAD in-tree test harness (not in any CMake target) |
| tests/vu1_replay.cpp | 1,201 | tests | Test/replay harness |
| host_app/vu.h | 1,171 | VU | VU1/VU0 execution |
| host_app/sound/sound_core.cpp | 1,168 | HLE-sdk | Host sound core (libsd boundary HLE: VAG, voices, mixer) |
| host_app/ps2_vblank.cpp | 1,167 | EE | EE kernel/CPU/memory/interrupts/timers |
| tests/test_block_decode_equivalence.cpp | 1,150 | tests | Test/replay harness |
| host_app/present_pace.h | 1,146 | render-IR | Render queue/jobs and graphics thread |
| host_app/gs_hle.cpp | 1,113 | GS-state | GS register/vertex/transfer state |
| tests/gs_replay.cpp | 1,103 | tests | Test/replay harness |
| host_app/test_scanout_preservation.cpp | 1,058 | tests | DEAD in-tree test harness (not in any CMake target) |
| iop_recompiler/iop_recompiler.cpp | 1,010 | IOP | IOP MIPS->C++ recompiler (STREAM.IRX) |
| host_app/pad_input.cpp | 1,007 | platform | Host integration (pad facade, display, QoS) |
| host_app/sce_hle/libpad.cpp | 933 | HLE-sdk | sce SDK library HLE handler |
| EEAnalyze/unused_function.cpp | 921 | tools | Static ELF analysis library |
| host_app/test_black_screen_scanout.cpp | 918 | tests | DEAD in-tree test harness (not in any CMake target) |
| host_app/graphics/opengl/gl_state_cache.h | 917 | backend-GL | GL state/bind cache + inline globals |
| host_app/sce_hle/libmc.cpp | 887 | HLE-sdk | sce SDK library HLE handler |
| host_app/sif_hle.cpp | 884 | SIF | EE<->IOP bridge, RPC, CDVD, IOP stubs |
| tests/recompiler_v2/test_convention_enforcers.cpp | 871 | tests | Test/replay harness |
| tests/test_ztex_equivalence.cpp | 869 | tests | Test/replay harness |
| host_app/test_boot_preservation.cpp | 862 | tests | DEAD in-tree test harness (not in any CMake target) |
| host_app/test_prim_change_preservation.cpp | 845 | tests | DEAD in-tree test harness (not in any CMake target) |
| tests/recompiler_v2/test_oracle_selftest.cpp | 835 | tests | Test/replay harness |
| tests/test_present_pace.cpp | 821 | tests | Test/replay harness |
| tools/gs_dump_parser.py | 816 | tools | Offline analysis/debug script |
| host_app/sce_hle/libmpeg.cpp | 815 | HLE-sdk | sce SDK library HLE handler |
| host_app/test_bug_conditions.cpp | 789 | tests | DEAD in-tree test harness (not in any CMake target) |
| host_app/bench_report.h | 781 | diagnostics | Diagnostics / perf probe / gate plumbing |
| host_app/sce_hle/libkernl.cpp | 765 | HLE-sdk | sce SDK library HLE handler |
| host_app/gui/gui.cpp | 753 | diagnostics | ImGui settings/diagnostics overlay section |
| host_app/test_alpha_blend_preservation.cpp | 745 | tests | DEAD in-tree test harness (not in any CMake target) |
| host_app/bench.h | 713 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tests/recompiler_verify/test_interp_core.cpp | 711 | tests | Test/replay harness |
| tests/test_integration_render_queue.cpp | 709 | tests | Test/replay harness |
| host_app/input/test_pad_buffer.cpp | 698 | tests | In-tree input unit test (built by tests/) |
| host_app/test_fbo_persistence_preservation.cpp | 692 | tests | DEAD in-tree test harness (not in any CMake target) |
| host_app/test_vblank_preservation.cpp | 688 | tests | DEAD in-tree test harness (not in any CMake target) |
| tests/test_multipass_pipeline.cpp | 686 | tests | Test/replay harness |
| host_app/gif_arbiter.h | 684 | GIF | GIF path arbitration / GIFtag parsing |
| host_app/test_vblank_bug_condition.cpp | 676 | tests | DEAD in-tree test harness (not in any CMake target) |
| tests/recompiler_v2/test_oracle_diff_v2.cpp | 675 | tests | Test/replay harness |
| host_app/gs_interp.h | 666 | GS-format | PSM formats, swizzle, texture decode/cache, VRAM census |
| tests/test_gif_fast_equivalence.cpp | 662 | tests | Test/replay harness |
| host_app/vu_decode_slot.h | 648 | VU | VU1/VU0 execution |
| CMakeLists.txt | 647 | unknown | ? |
| host_app/input/test_input_mapper.cpp | 645 | tests | In-tree input unit test (built by tests/) |
| host_app/test_rendering_bug_condition.cpp | 638 | tests | DEAD in-tree test harness (not in any CMake target) |
| host_app/hle/gs_draw_ir.h | 638 | render-IR | GS draw-IR / direct ops (HLE draw path IR) |
| host_app/test_fbo_persistence_bug_condition.cpp | 637 | tests | DEAD in-tree test harness (not in any CMake target) |
| tests/recompiler_v2/test_recomp_gates.cpp | 636 | tests | Test/replay harness |
| host_app/third_party/imgui/imstb_rectpack.h | 627 | third-party | Vendored Dear ImGui (exempt from refactor) |
| host_app/gs.cpp | 627 | GS-state | GS register/vertex/transfer state |
| host_app/CMakeLists.txt | 624 | unknown | ? |
| tests/test_vsuv_equivalence.cpp | 618 | tests | Test/replay harness |
| host_app/perf_profiler.h | 610 | diagnostics | Diagnostics / perf probe / gate plumbing |
| host_app/ee_timers.cpp | 605 | EE | EE kernel/CPU/memory/interrupts/timers |
| tests/test_alias_rect.cpp | 604 | tests | Test/replay harness |
| host_app/sce_hle/sce_hle_registry.cpp | 604 | HLE-sdk | sce SDK library HLE handler |
| host_app/tests/swizzle/test_swizzle_equivalence.cpp | 601 | tests | In-tree test (host_app/tests/) |
| host_app/ipu.cpp | 600 | HLE-game | Game/SDK function HLE (heap, libc, anim, mpeg) |
| iop_recompiler/main.cpp | 599 | IOP | IOP MIPS->C++ recompiler (STREAM.IRX) |
| host_app/ring_log.cpp | 595 | diagnostics | Diagnostics / perf probe / gate plumbing |
| recompiler_tool/discovery/functions.cpp | 589 | tools | v2 discovery engine (leaders/jumptables/functions) |
| host_app/gs_class_renderer.h | 585 | GS-format | PSM formats, swizzle, texture decode/cache, VRAM census |
| host_app/test_comprehensive_rendering.cpp | 581 | tests | DEAD in-tree test harness (not in any CMake target) |
| recompiler_tool/main_v2.cpp | 572 | tools | EE MIPS->C++ recompiler |
| host_app/test_rendering_preservation.cpp | 572 | tests | DEAD in-tree test harness (not in any CMake target) |
| host_app/hle/gs_packet.h | 572 | render-IR | GS draw-IR / direct ops (HLE draw path IR) |
| tests/libc_hle_diff/test_libc_hle_diff.cpp | 570 | tests | Test/replay harness |
| tools/detect_sdk_version.py | 567 | tools | Offline analysis/debug script |
| host_app/bug_report.cpp | 564 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tests/recompiler_v2/fixtures_switch.h | 563 | tests | Test/replay harness |
| tests/test_savestate_format.cpp | 561 | tests | Test/replay harness |
| tests/test_gl_state_cache_equivalence.cpp | 534 | tests | Test/replay harness |
| tests/test_unifpack_equivalence.cpp | 524 | tests | Test/replay harness |
| tests/test_libmc.cpp | 521 | tests | Test/replay harness |
| recompiler_tool/discovery/jumptables.cpp | 521 | tools | v2 discovery engine (leaders/jumptables/functions) |
| host_app/hle_heap.h | 521 | HLE-game | Game/SDK function HLE (heap, libc, anim, mpeg) |
| host_app/test_texture_shuffle.cpp | 519 | tests | DEAD in-tree test harness (not in any CMake target) |
| host_app/gs_page_tracker.h | 517 | GS-format | PSM formats, swizzle, texture decode/cache, VRAM census |
| tests/test_ee_pace.cpp | 507 | tests | Test/replay harness |
| host_app/gui/savestate_browser.cpp | 506 | diagnostics | ImGui settings/diagnostics overlay section |
| tests/test_blend_properties.cpp | 505 | tests | Test/replay harness |
| tests/anim_pose_diff/test_anim_pose_diff.cpp | 503 | tests | Test/replay harness |
| host_app/turbo.h | 502 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tests/test_fmv_pace.cpp | 498 | tests | Test/replay harness |
| host_app/timeline.h | 498 | diagnostics | Diagnostics / perf probe / gate plumbing |
| host_app/test_prim_change_bug_condition.cpp | 498 | tests | DEAD in-tree test harness (not in any CMake target) |
| host_app/libc_hle.cpp | 495 | HLE-game | Game/SDK function HLE (heap, libc, anim, mpeg) |
| tests/recompiler_verify/savestate.cpp | 493 | tests | Test/replay harness |
| host_app/call_census.cpp | 491 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tests/test_lazyflags_algebra.cpp | 490 | tests | Test/replay harness |
| host_app/gui/vu_microcode_section.cpp | 488 | diagnostics | ImGui settings/diagnostics overlay section |
| tests/recompiler_v2/test_oracle_diff_legacy.cpp | 486 | tests | Test/replay harness |
| host_app/gs_state.h | 473 | GS-state | GS register/vertex/transfer state |
| tests/fastmem_diff/test_fastmem_diff.cpp | 472 | tests | Test/replay harness |
| tests/test_s8_ctxlatch.cpp | 468 | tests | Test/replay harness |
| tests/recomp_semantics/test_recomp_semantics.cpp | 466 | tests | Test/replay harness |
| tests/test_pixel_format_properties.cpp | 464 | tests | Test/replay harness |
| tests/test_w14r6_units.cpp | 463 | tests | Test/replay harness |
| host_app/bug_report.h | 459 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tests/test_page_invariance.cpp | 457 | tests | Test/replay harness |
| tests/recompiler_verify/test_sut_runner.cpp | 457 | tests | Test/replay harness |
| host_app/snd_census.h | 454 | diagnostics | Diagnostics / perf probe / gate plumbing |
| host_app/test_boot_bug_condition.cpp | 453 | tests | DEAD in-tree test harness (not in any CMake target) |
| host_app/test_alpha_blend_bug_condition.cpp | 453 | tests | DEAD in-tree test harness (not in any CMake target) |
| host_app/iop_loader.cpp | 451 | SIF | EE<->IOP bridge, RPC, CDVD, IOP stubs |
| tests/test_blend_ff_map.cpp | 450 | tests | Test/replay harness |
| host_app/sce_hle/libsdr.cpp | 438 | HLE-sdk | sce SDK library HLE handler |
| host_app/hle/vu1_hle_runtime.cpp | 437 | HLE-game | VU1 microprogram HLE (native reimplementation) |
| tests/recompiler_verify/test_interp_controlflow.cpp | 431 | tests | Test/replay harness |
| host_app/mid_census.h | 425 | diagnostics | Diagnostics / perf probe / gate plumbing |
| host_app/cdvd_stream.cpp | 425 | SIF | EE<->IOP bridge, RPC, CDVD, IOP stubs |
| host_app/input/input_backend_gamepad.cpp | 423 | platform | Host input pipeline (backends, mapper, facade) |
| tests/test_bench_report.cpp | 421 | tests | Test/replay harness |
| tests/recompiler_v2/test_discovery_real_elf.cpp | 417 | tests | Test/replay harness |
| tests/test_texture_sampling_properties.cpp | 414 | tests | Test/replay harness |
| host_app/stick_trace.h | 414 | diagnostics | Diagnostics / perf probe / gate plumbing |
| host_app/s8_ctxlatch.h | 404 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tests/test_defer_boundary_order.cpp | 403 | tests | Test/replay harness |
| host_app/hle/drawlist_record.cpp | 403 | render-IR | Draw-list recording for HLE draw path |
| host_app/sce_hle/libcdvd.cpp | 402 | HLE-sdk | sce SDK library HLE handler |
| host_app/input/input_system.cpp | 393 | platform | Host input pipeline (backends, mapper, facade) |
| tests/test_gs_replay.cpp | 389 | tests | Test/replay harness |
| tests/test_time_domain.cpp | 385 | tests | Test/replay harness |
| tools/gen_merge.py | 384 | tools | Offline analysis/debug script |
| host_app/graphics/opengl/s9_composite.h | 383 | backend-GL | S9 shadow/composite native pass helpers (header-impl) |
| tests/test_idx16_lane.cpp | 382 | tests | Test/replay harness |
| tests/r141_call_census/test_r141_call_census.cpp | 382 | tests | Test/replay harness |
| tests/test_sound_state.cpp | 381 | tests | Test/replay harness |
| tests/recompiler_v2/fixtures_structure.h | 381 | tests | Test/replay harness |
| host_app/ps2_vblank.h | 380 | EE | EE kernel/CPU/memory/interrupts/timers |
| tests/test_gs_draw_ir.cpp | 379 | tests | Test/replay harness |
| host_app/ee_pace.h | 367 | diagnostics | Diagnostics / perf probe / gate plumbing |
| host_app/gs_vram_census.h | 362 | GS-format | PSM formats, swizzle, texture decode/cache, VRAM census |
| tests/test_sound_voice.cpp | 359 | tests | Test/replay harness |
| host_app/vu1_async.h | 358 | VU | VU1/VU0 execution |
| host_app/gs_mipmap.h | 358 | GS-format | PSM formats, swizzle, texture decode/cache, VRAM census |
| tests/test_w14r5_units.cpp | 351 | tests | Test/replay harness |
| host_app/graphics/opengl/rt_neverstale.h | 351 | backend-GL | GL backend helper (header-implemented pass/cache logic) |
| host_app/hle_meta_arena.cpp | 350 | HLE-game | Game/SDK function HLE (heap, libc, anim, mpeg) |
| host_app/vif.h | 349 | VIF | VIF command/UNPACK processing |
| tests/test_vu0_shadow_replay.cpp | 347 | tests | Test/replay harness |
| host_app/loader_prof.h | 347 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tests/r139_threads/test_r139_threads.cpp | 342 | tests | Test/replay harness |
| host_app/artdet_flicker.cpp | 338 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tests/test_bug_report.cpp | 337 | tests | Test/replay harness |
| host_app/ring_log_decode.h | 333 | diagnostics | Diagnostics / perf probe / gate plumbing |
| host_app/loadspan.h | 328 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tests/drawlist_tool.cpp | 326 | tests | Test/replay harness |
| host_app/ps2_threading.h | 326 | EE | EE kernel/CPU/memory/interrupts/timers |
| tests/test_tfx_properties.cpp | 325 | tests | Test/replay harness |
| recompiler_tool/main.cpp | 325 | tools | EE MIPS->C++ recompiler |
| tests/test_gs_classrdr.cpp | 323 | tests | Test/replay harness |
| tests/test_s9_composite.cpp | 322 | tests | Test/replay harness |
| host_app/time_domain.h | 319 | diagnostics | Diagnostics / perf probe / gate plumbing |
| host_app/mpeg_hle.h | 318 | HLE-game | Game/SDK function HLE (heap, libc, anim, mpeg) |
| host_app/input/input_mapper.cpp | 318 | platform | Host input pipeline (backends, mapper, facade) |
| tests/recompiler_verify/sut_link_shims.cpp | 316 | tests | Test/replay harness |
| tests/recompiler_verify/test_oracle_anchor.cpp | 315 | tests | Test/replay harness |
| tests/test_vconv_equivalence.cpp | 313 | tests | Test/replay harness |
| host_app/ps2_scheduler_old_fiber.h | 309 | EE | EE kernel/CPU/memory/interrupts/timers |
| host_app/anim_pose_hle.cpp | 308 | HLE-game | Game/SDK function HLE (heap, libc, anim, mpeg) |
| host_app/sce_hle/sce_hle_bindings.inc | 307 | HLE-sdk | sce SDK library HLE handler |
| host_app/slowspot.cpp | 306 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tests/test_gs_interp.cpp | 302 | tests | Test/replay harness |
| host_app/cpu_state.h | 299 | EE | EE kernel/CPU/memory/interrupts/timers |
| EEAnalyze/analyze.cpp | 294 | tools | Static ELF analysis library |
| tests/test_rasterization_properties.cpp | 292 | tests | Test/replay harness |
| tests/recompiler_v2/diff_harness.cpp | 291 | tests | Test/replay harness |
| tests/test_gif_drain_thread.cpp | 285 | tests | Test/replay harness |
| host_app/memory.h | 284 | EE | EE kernel/CPU/memory/interrupts/timers |
| tests/test_newgame_switch.cpp | 282 | tests | Test/replay harness |
| tests/test_r160_draw_merge.cpp | 280 | tests | Test/replay harness |
| host_app/mipsrc_diag.cpp | 277 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tests/test_snd_wire_governor.cpp | 274 | tests | Test/replay harness |
| recompiler_tool/emit/driver_v2.cpp | 273 | tools | EE MIPS->C++ recompiler |
| tests/test_gs_glbatch.cpp | 270 | tests | Test/replay harness |
| host_app/texture_decoder.h | 267 | GS-format | PSM formats, swizzle, texture decode/cache, VRAM census |
| tests/test_s9_shadow.cpp | 266 | tests | Test/replay harness |
| tests/test_idx_epoch_guard.cpp | 266 | tests | Test/replay harness |
| host_app/vu_aot_config.h | 266 | VU | VU1/VU0 execution |
| host_app/sce_hle/libipu.cpp | 266 | HLE-sdk | sce SDK library HLE handler |
| host_app/ps2_syscall_wrappers.cpp | 265 | EE | EE kernel/CPU/memory/interrupts/timers |
| tests/test_ring_log.cpp | 264 | tests | Test/replay harness |
| tests/r144_list_leak/test_r144_list_leak.cpp | 262 | tests | Test/replay harness |
| tests/test_shadow_offset.cpp | 261 | tests | Test/replay harness |
| tests/test_dirty_tracking_properties.cpp | 260 | tests | Test/replay harness |
| EEAnalyze/jump_table_analysis.cpp | 260 | tools | Static ELF analysis library |
| tests/test_vu0_vis_micro.cpp | 259 | tests | Test/replay harness |
| tests/recompiler_v2/fixtures_delayslot.h | 257 | tests | Test/replay harness |
| host_app/graphics/opengl/rt_copy_tracker.h | 256 | backend-GL | GL backend helper (header-implemented pass/cache logic) |
| tests/test_savestate_browser.cpp | 254 | tests | Test/replay harness |
| host_app/graphics/opengl/s9_shadow.h | 254 | backend-GL | S9 shadow/composite native pass helpers (header-impl) |
| tools/tmp_clut_replay.py | 252 | tools | Offline analysis/debug script |
| tests/r140_budget/test_r140_budget.cpp | 252 | tests | Test/replay harness |
| host_app/gui/savestate_browser.h | 249 | diagnostics | ImGui settings/diagnostics overlay section |
| host_app/graphics/opengl/blend_ff_map.h | 249 | backend-GL | GL backend helper (header-implemented pass/cache logic) |
| tests/test_probe_bench.cpp | 248 | tests | Test/replay harness |
| host_app/sound/voice.cpp | 245 | HLE-sdk | Host sound core (libsd boundary HLE: VAG, voices, mixer) |
| host_app/graphics/vconv.h | 245 | render-IR | Backend-agnostic graphics interface |
| host_app/savestate_format.cpp | 242 | EE | Savestate serialization |
| host_app/hle/drawlist_record.h | 241 | render-IR | Draw-list recording for HLE draw path |
| tests/test_shuf_snap.cpp | 240 | tests | Test/replay harness |
| tests/test_e1_dispatch.cpp | 240 | tests | Test/replay harness |
| tests/test_vu_aot_config.cpp | 239 | tests | Test/replay harness |
| tools/render_tiles.py | 238 | tools | Offline analysis/debug script |
| tools/sce_lib_finder/sig_gen.py | 236 | tools | Offline analysis/debug script |
| tests/test_vag_decoder.cpp | 236 | tests | Test/replay harness |
| host_app/gs_deposit_overlay.h | 236 | GS-format | PSM formats, swizzle, texture decode/cache, VRAM census |
| host_app/texcache_key.h | 235 | GS-format | PSM formats, swizzle, texture decode/cache, VRAM census |
| host_app/savestate_format.h | 234 | EE | Savestate serialization |
| tests/test_drawlist_record.cpp | 232 | tests | Test/replay harness |
| tests/recompiler_v2/test_oracle_planted_bugs.cpp | 232 | tests | Test/replay harness |
| host_app/ring_log.h | 232 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tests/anim_pose_diff/gen_pose_natives.py | 230 | tests | Test/replay harness |
| tests/recompiler_verify/interpreter.h | 229 | tests | Test/replay harness |
| host_app/hle/loader_sync_hle.h | 224 | HLE-game | Game-specific HLE |
| tests/recompiler_verify/sut_runner.cpp | 223 | tests | Test/replay harness |
| host_app/budget_lift.h | 223 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tools/gen_1017c8.py | 222 | tools | Offline analysis/debug script |
| tests/test_slowspot.cpp | 221 | tests | Test/replay harness |
| tools/logo_timeseries.py | 220 | tools | Offline analysis/debug script |
| tools/disasm_func.py | 218 | tools | Offline analysis/debug script |
| host_app/graphics/opengl/alias_rect.h | 216 | backend-GL | GL backend helper (header-implemented pass/cache logic) |
| host_app/gif.h | 216 | GIF | GIF path arbitration / GIFtag parsing |
| host_app/vu_function_registry.cpp | 214 | VU | VU1/VU0 execution |
| tests/test_savestate_writer.cpp | 212 | tests | Test/replay harness |
| tools/gif_diff.py | 204 | tools | Offline analysis/debug script |
| tests/recompiler_v2/test_mips_asm.cpp | 203 | tests | Test/replay harness |
| host_app/cuelag_probe.h | 200 | diagnostics | Diagnostics / perf probe / gate plumbing |
| host_app/sound/voice.h | 199 | HLE-sdk | Host sound core (libsd boundary HLE: VAG, voices, mixer) |
| host_app/graphics/vulkan/vulkan_backend.h | 198 | backend-Vulkan | Vulkan backend (STALE since 2026-03) |
| tests/recompiler_v2/test_discovery_synthetic.cpp | 197 | tests | Test/replay harness |
| tests/test_viewport_math.cpp | 196 | tests | Test/replay harness |
| host_app/pacebrk.h | 196 | diagnostics | Diagnostics / perf probe / gate plumbing |
| host_app/gs_deswizzle.h | 196 | GS-format | PSM formats, swizzle, texture decode/cache, VRAM census |
| host_app/vu_span_classify.h | 195 | VU | VU1/VU0 execution |
| tools/sce_lib_finder/elf_reader.py | 194 | tools | Offline analysis/debug script |
| tests/test_iop_sysmem_heap.cpp | 194 | tests | Test/replay harness |
| tests/test_gl_l2_entries.cpp | 192 | tests | Test/replay harness |
| host_app/iop_stubs.h | 192 | SIF | EE<->IOP bridge, RPC, CDVD, IOP stubs |
| host_app/display_settings.cpp | 191 | platform | Host integration (pad facade, display, QoS) |
| host_app/platform/win32/crash_handler_win32.cpp | 189 | platform | Platform shim (fibers, crash handler, win compat) |
| tools/tmp_pcsx2_clut.py | 188 | tools | Offline analysis/debug script |
| recompiler_tool/corpus/audit.cpp | 188 | tools | EE MIPS->C++ recompiler |
| tools/gsdump_state.py | 187 | tools | Offline analysis/debug script |
| recompiler_tool/inject_fun_1017c8_helpers.cpp | 186 | tools | EE MIPS->C++ recompiler |
| tools/loader_oracle_check.py | 185 | tools | Offline analysis/debug script |
| host_app/savestate_writer.cpp | 185 | EE | Savestate serialization |
| tests/test_recomp_overrides_config.cpp | 183 | tests | Test/replay harness |
| tests/test_display_magnification_properties.cpp | 182 | tests | Test/replay harness |
| tests/r150_present_edge/test_r150_present_edge.cpp | 181 | tests | Test/replay harness |
| host_app/runahead_guard.h | 180 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tools/fmv_draws.py | 178 | tools | Offline analysis/debug script |
| tests/recompiler_v2/oracle_tiers.h | 178 | tests | Test/replay harness |
| host_app/dmac.h | 178 | DMAC | DMA controller channels/chains |
| host_app/ee_timers.h | 177 | EE | EE kernel/CPU/memory/interrupts/timers |
| tools/tmp_clut_full.py | 176 | tools | Offline analysis/debug script |
| host_app/hle_meta_arena.h | 176 | HLE-game | Game/SDK function HLE (heap, libc, anim, mpeg) |
| tests/test_vu1_transform_gt.cpp | 175 | tests | Test/replay harness |
| tests/test_s8_inrt.cpp | 175 | tests | Test/replay harness |
| host_app/platform/macos/crash_handler_macos.cpp | 175 | platform | Platform shim (fibers, crash handler, win compat) |
| tests/test_savestate_atomic.cpp | 174 | tests | Test/replay harness |
| host_app/graphics/graphics_backend.h | 174 | render-IR | Backend-agnostic graphics interface |
| tests/test_pause_gate_codegen.cpp | 172 | tests | Test/replay harness |
| host_app/sound/sound_core.h | 171 | HLE-sdk | Host sound core (libsd boundary HLE: VAG, voices, mixer) |
| host_app/quantum.h | 171 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tools/warning_worklist.py | 169 | tools | Offline analysis/debug script |
| tools/dump_transfers.py | 168 | tools | Offline analysis/debug script |
| tests/recomp_semantics/cases.h | 167 | tests | Test/replay harness |
| host_app/common/Pcsx2Defs.h | 165 | platform | Shared type defs (Pcsx2Types) |
| EEAnalyze/main.cpp | 165 | tools | Static ELF analysis library |
| tools/extract_tiles.py | 164 | tools | Offline analysis/debug script |
| tests/test_gs_packet.cpp | 163 | tests | Test/replay harness |
| EEAnalyze/analyze_test.cpp | 163 | tools | Static ELF analysis library |
| tools/flicker/tdiff.py | 160 | tools | Offline analysis/debug script |
| tools/coldboot_gate.py | 159 | tools | Offline analysis/debug script |
| tests/test_vu_clampelide.cpp | 159 | tests | Test/replay harness |
| host_app/gui/bug_report_section.cpp | 159 | diagnostics | ImGui settings/diagnostics overlay section |
| tests/test_sound_ring.cpp | 157 | tests | Test/replay harness |
| tests/test_ps2_argv.cpp | 156 | tests | Test/replay harness |
| host_app/budget_math.h | 156 | diagnostics | Diagnostics / perf probe / gate plumbing |
| recompiler_tool/recompiler.h | 155 | tools | EE MIPS->C++ recompiler |
| tools/sce_lib_finder/sig_match.py | 154 | tools | Offline analysis/debug script |
| host_app/graphics/viewport_math.h | 154 | render-IR | Pure viewport/scaling rect math |
| tools/dump_pcsx2_memory.py | 152 | tools | Offline analysis/debug script |
| tests/test_runahead_guard.cpp | 152 | tests | Test/replay harness |
| recompiler_tool/legacy/recomp_overrides_config.h | 151 | tools | EE MIPS->C++ recompiler |
| host_app/hle/gs_direct_ops.h | 151 | render-IR | GS draw-IR / direct ops (HLE draw path IR) |
| host_app/shuf_snap.h | 149 | GS-format | PSM formats, swizzle, texture decode/cache, VRAM census |
| host_app/platform/macos/fiber_macos.cpp | 149 | platform | Platform shim (fibers, crash handler, win compat) |
| host_app/loop_hist.h | 149 | diagnostics | Diagnostics / perf probe / gate plumbing |
| host_app/snd_bank_ledger.cpp | 148 | EE | Savestate serialization |
| host_app/iop_sysmem_heap.h | 148 | SIF | EE<->IOP bridge, RPC, CDVD, IOP stubs |
| tests/recompiler_verify/test_savestate.cpp | 146 | tests | Test/replay harness |
| tests/test_blendcp_lazy.cpp | 144 | tests | Test/replay harness |
| host_app/hle/loader_oracle.h | 144 | HLE-game | Game-specific HLE |
| recompiler_tool/discovery/rv2_decode.h | 143 | tools | v2 discovery engine (leaders/jumptables/functions) |
| tools/pss_census.py | 140 | tools | Offline analysis/debug script |
| host_app/savestate.h | 140 | EE | Savestate serialization |
| tools/summarize_gsdump.py | 139 | tools | Offline analysis/debug script |
| host_app/sce_hle/generate_bindings.py | 139 | HLE-sdk | sce SDK library HLE handler |
| tools/recomp_lint.py | 138 | tools | Offline analysis/debug script |
| tools/host_log_parser.py | 137 | tools | Offline analysis/debug script |
| tests/test_display_settings.cpp | 137 | tests | Test/replay harness |
| tests/recompiler_verify/state_delta.cpp | 136 | tests | Test/replay harness |
| host_app/intc.cpp | 136 | EE | EE kernel/CPU/memory/interrupts/timers |
| host_app/vu_aot_programs.cpp | 135 | VU | VU1/VU0 execution |
| tests/recompiler_v2/oracle_asm_ext.h | 134 | tests | Test/replay harness |
| recompiler_tool/discovery/discovery_types.h | 133 | tools | v2 discovery engine (leaders/jumptables/functions) |
| tests/test_rt_dirty_tiles.cpp | 132 | tests | Test/replay harness |
| host_app/bullseye_trace.cpp | 132 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tests/test_s8_wrapblend.cpp | 131 | tests | Test/replay harness |
| tests/recompiler_v2/mips_asm.h | 131 | tests | Test/replay harness |
| host_app/slowspot_detector.h | 129 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tests/recompiler_verify/mem_image.h | 128 | tests | Test/replay harness |
| tests/recompiler_v2/diff_harness.h | 128 | tests | Test/replay harness |
| host_app/graphics/opengl/compute_data_layouts.h | 127 | backend-GL | GL backend helper (header-implemented pass/cache logic) |
| tools/analyze_bb_dumps.py | 126 | tools | Offline analysis/debug script |
| host_app/uv_bbox.h | 125 | GS-format | PSM formats, swizzle, texture decode/cache, VRAM census |
| host_app/ring_tags.h | 125 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tests/test_vu0_decal_eval.cpp | 124 | tests | Test/replay harness |
| host_app/warp.h | 124 | diagnostics | Diagnostics / perf probe / gate plumbing |
| host_app/s8_inrt.h | 124 | diagnostics | Diagnostics / perf probe / gate plumbing |
| host_app/sce_hle/libgraph.cpp | 123 | HLE-sdk | sce SDK library HLE handler |
| host_app/gs_glbatch.h | 121 | GS-format | PSM formats, swizzle, texture decode/cache, VRAM census |
| tools/sce_lib_finder/mips_mask.py | 119 | tools | Offline analysis/debug script |
| host_app/idx16_lane.h | 118 | GS-format | PSM formats, swizzle, texture decode/cache, VRAM census |
| tools/dump_bullseye_slots.py | 117 | tools | Offline analysis/debug script |
| host_app/texture_cache.cpp | 116 | GS-format | PSM formats, swizzle, texture decode/cache, VRAM census |
| host_app/gif_template.h | 116 | unknown | ? |
| tests/test_shutdown_policy.cpp | 115 | tests | Test/replay harness |
| host_app/ps2_argv.h | 115 | EE | EE kernel/CPU/memory/interrupts/timers |
| host_app/idx_epoch_guard.h | 115 | GS-format | PSM formats, swizzle, texture decode/cache, VRAM census |
| host_app/hle/drawlist_reader.h | 115 | render-IR | Draw-list recording for HLE draw path |
| host_app/graphics/graphics_backend.cpp | 114 | render-IR | Backend-agnostic graphics interface |
| host_app/common/Pcsx2Types.h | 114 | platform | Shared type defs (Pcsx2Types) |
| tests/test_slowspot_paths.cpp | 113 | tests | Test/replay harness |
| host_app/ipu.h | 113 | HLE-game | Game/SDK function HLE (heap, libc, anim, mpeg) |
| tools/flicker/bbdiff.py | 112 | tools | Offline analysis/debug script |
| host_app/platform/win_compat.h | 112 | platform | Platform shim (fibers, crash handler, win compat) |
| host_app/gs_crtc_class.h | 112 | GS-state | GS register/vertex/transfer state |
| tools/vu1_trace_diff.py | 110 | tools | Offline analysis/debug script |
| host_app/hle/vu1_hle.h | 108 | HLE-game | VU1 microprogram HLE (native reimplementation) |
| tests/test_iop_sysclib_map.cpp | 107 | tests | Test/replay harness |
| host_app/sce_hle/libscf.cpp | 107 | HLE-sdk | sce SDK library HLE handler |
| tools/check_skydomes.py | 106 | tools | Offline analysis/debug script |
| host_app/savestate_writer.h | 106 | EE | Savestate serialization |
| recompiler_tool/translate/control_flow_v2.cpp | 105 | tools | EE MIPS->C++ recompiler |
| EEAnalyze/DataFlowEngine.cpp | 105 | tools | Static ELF analysis library |
| tools/logo_area.py | 103 | tools | Offline analysis/debug script |
| host_app/main_old.cpp | 103 | dead | Dead old main |
| host_app/input/input_backend_gamepad.h | 103 | platform | Host input pipeline (backends, mapper, facade) |
| tests/recompiler_v2/fixture.h | 101 | tests | Test/replay harness |
| host_app/uv_bbox.cpp | 101 | GS-format | PSM formats, swizzle, texture decode/cache, VRAM census |
| tests/recompiler_v2/oracle_executor.cpp | 99 | tests | Test/replay harness |
| host_app/call_census.h | 99 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tests/r139_threads/r139_shims.cpp | 96 | tests | Test/replay harness |
| tests/test_ee_timers_timr.cpp | 95 | tests | Test/replay harness |
| host_app/input/input_mapper.h | 95 | platform | Host input pipeline (backends, mapper, facade) |
| tests/fastmem_diff/fastmem_stubs.cpp | 94 | tests | Test/replay harness |
| host_app/vu_trace_case.h | 93 | VU | VU1/VU0 execution |
| host_app/input/input_backend_kbm.cpp | 91 | platform | Host input pipeline (backends, mapper, facade) |
| host_app/hle/vu1_hle.cpp | 91 | HLE-game | VU1 microprogram HLE (native reimplementation) |
| host_app/shutdown_policy.h | 90 | platform | Host integration (pad facade, display, QoS) |
| tools/sample_frame.py | 89 | tools | Offline analysis/debug script |
| recompiler_tool/discovery/jumptables.h | 89 | tools | v2 discovery engine (leaders/jumptables/functions) |
| host_app/sif_rpc_handlers.h | 89 | SIF | EE<->IOP bridge, RPC, CDVD, IOP stubs |
| tools/parse_rm2.py | 88 | tools | Offline analysis/debug script |
| host_app/sound/ring.h | 88 | HLE-sdk | Host sound core (libsd boundary HLE: VAG, voices, mixer) |
| tools/color_map.py | 87 | tools | Offline analysis/debug script |
| tools/parse_gsdump.py | 86 | tools | Offline analysis/debug script |
| tests/test_vu_spanfold2.cpp | 86 | tests | Test/replay harness |
| host_app/syscalls.h | 86 | EE | EE kernel/CPU/memory/interrupts/timers |
| host_app/bullseye_trace.h | 86 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tests/test_vu_aot_spanfold.cpp | 85 | tests | Test/replay harness |
| host_app/sound/mixer.cpp | 85 | HLE-sdk | Host sound core (libsd boundary HLE: VAG, voices, mixer) |
| host_app/foglane_live.h | 85 | render-IR | Render queue/jobs and graphics thread |
| tools/parse_bh.py | 84 | tools | Offline analysis/debug script |
| tools/build_bullseye_scene.py | 84 | tools | Offline analysis/debug script |
| tools/snap_query.py | 83 | tools | Offline analysis/debug script |
| tests/recompiler_v2/oracle_executor.h | 83 | tests | Test/replay harness |
| tests/recomp_semantics/mem_shim.cpp | 83 | tests | Test/replay harness |
| tools/tile_raw.py | 82 | tools | Offline analysis/debug script |
| tools/sce_lib_finder/ar_reader.py | 82 | tools | Offline analysis/debug script |
| host_app/sce_hle/libdma.cpp | 82 | HLE-sdk | sce SDK library HLE handler |
| EEAnalyze/register_state.h | 82 | tools | Static ELF analysis library |
| recompiler_tool/discovery/leaders.cpp | 81 | tools | v2 discovery engine (leaders/jumptables/functions) |
| host_app/pad_input.h | 81 | platform | Host integration (pad facade, display, QoS) |
| tests/recompiler_verify/sut_runner.h | 80 | tests | Test/replay harness |
| host_app/vu_function_registry.h | 80 | VU | VU1/VU0 execution |
| tools/sce_lib_finder/__main__.py | 79 | tools | Offline analysis/debug script |
| recompiler_tool/discovery/report.cpp | 79 | tools | v2 discovery engine (leaders/jumptables/functions) |
| host_app/graphics/gs_c3_counters.h | 79 | render-IR | Backend-agnostic graphics interface |
| tests/recompiler_verify/savestate.h | 77 | tests | Test/replay harness |
| host_app/intc.h | 77 | EE | EE kernel/CPU/memory/interrupts/timers |
| tests/vu1_test_shims.cpp | 75 | tests | Test/replay harness |
| tests/recompiler_verify/main.cpp | 75 | tests | Test/replay harness |
| host_app/ring_log_format.h | 75 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tools/flicker/greensig.py | 74 | tools | Offline analysis/debug script |
| tools/flicker/chromaflip.py | 71 | tools | Offline analysis/debug script |
| host_app/vu_aot_registry.h | 70 | VU | VU1/VU0 execution |
| host_app/graphics/opengl/gs_scale.h | 70 | backend-GL | GL backend helper (header-implemented pass/cache logic) |
| tools/phaseb_integrate.py | 69 | tools | Offline analysis/debug script |
| recompiler_tool/corpus/recorder_gen.cpp | 69 | tools | EE MIPS->C++ recompiler |
| host_app/display_settings.h | 69 | platform | Host integration (pad facade, display, QoS) |
| host_app/debug_budget.h | 69 | diagnostics | Diagnostics / perf probe / gate plumbing |
| host_app/timer_hle.cpp | 67 | EE | EE kernel/CPU/memory/interrupts/timers |
| host_app/slowspot_paths.h | 66 | diagnostics | Diagnostics / perf probe / gate plumbing |
| recompiler_tool/corpus/audit.h | 65 | tools | EE MIPS->C++ recompiler |
| CMakePresets.json | 63 | unknown | ? |
| tools/compare_frames.py | 62 | tools | Offline analysis/debug script |
| host_app/ps2_syscall_wrappers.h | 62 | EE | EE kernel/CPU/memory/interrupts/timers |
| host_app/slowspot.h | 61 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tests/recompiler_verify/state_delta.h | 60 | tests | Test/replay harness |
| iop_recompiler/iop_recompiler.h | 59 | IOP | IOP MIPS->C++ recompiler (STREAM.IRX) |
| tools/swap_to_include.py | 58 | tools | Offline analysis/debug script |
| host_app/platform/win32/fiber_win32.cpp | 58 | platform | Platform shim (fibers, crash handler, win compat) |
| tools/scan_rows.py | 57 | tools | Offline analysis/debug script |
| host_app/iop_memory.h | 57 | SIF | EE<->IOP bridge, RPC, CDVD, IOP stubs |
| tests/ring_log_stub.cpp | 56 | tests | Test/replay harness |
| host_app/sound/vag_decoder.h | 56 | HLE-sdk | Host sound core (libsd boundary HLE: VAG, voices, mixer) |
| host_app/sound/vag_decoder.cpp | 56 | HLE-sdk | Host sound core (libsd boundary HLE: VAG, voices, mixer) |
| host_app/sif_hle.h | 56 | SIF | EE<->IOP bridge, RPC, CDVD, IOP stubs |
| host_app/w10_eefeed.h | 55 | diagnostics | Diagnostics / perf probe / gate plumbing |
| host_app/input/input_system.h | 55 | platform | Host input pipeline (backends, mapper, facade) |
| host_app/input/input_latency.h | 55 | platform | Host input pipeline (backends, mapper, facade) |
| tools/ring_decode.cpp | 53 | tools | Offline analysis/debug script |
| tools/recomp_gates/run_leg_gate.sh | 53 | tools | Offline analysis/debug script |
| recompiler_tool/emit/v2_emission.h | 52 | tools | EE MIPS->C++ recompiler |
| host_app/texture_cache.h | 52 | GS-format | PSM formats, swizzle, texture decode/cache, VRAM census |
| host_app/mipsrc_diag.h | 52 | diagnostics | Diagnostics / perf probe / gate plumbing |
| tools/flicker/pixflip.py | 51 | tools | Offline analysis/debug script |
| tools/check_empty_switch.py | 51 | tools | Offline analysis/debug script |
| host_app/gui/sound_section.cpp | 51 | diagnostics | ImGui settings/diagnostics overlay section |
| tools/per_frame_batches.sh | 50 | tools | Offline analysis/debug script |
| host_app/vu_hash_fnv.h | 50 | VU | VU1/VU0 execution |
| tests/recompiler_verify/verify_assert.h | 49 | tests | Test/replay harness |
| host_app/snd_bank_ledger.h | 49 | EE | Savestate serialization |
| host_app/input/input_backend.h | 48 | platform | Host input pipeline (backends, mapper, facade) |
| host_app/cdvd_stream.h | 48 | SIF | EE<->IOP bridge, RPC, CDVD, IOP stubs |
| tools/phaseb_triage.py | 47 | tools | Offline analysis/debug script |
| recompiler_tool/inject_fun_1017c8_helpers.h | 47 | tools | EE MIPS->C++ recompiler |
| host_app/hw_verbose.h | 47 | diagnostics | Diagnostics / perf probe / gate plumbing |
| host_app/artdet_flicker.h | 47 | diagnostics | Diagnostics / perf probe / gate plumbing |
| EEAnalyze/Function.h | 47 | tools | Static ELF analysis library |
| recompiler_tool/corpus/recorder_gen.h | 45 | tools | EE MIPS->C++ recompiler |
| host_app/memory_test.cpp | 45 | EE | EE kernel/CPU/memory/interrupts/timers |
| host_app/gs.h | 45 | GS-state | GS register/vertex/transfer state |
| host_app/sce_hle/sce_hle.h | 44 | HLE-sdk | sce SDK library HLE handler |
| tools/phaseb_remaining.py | 41 | tools | Offline analysis/debug script |
| recompiler_tool/discovery/leaders.h | 41 | tools | v2 discovery engine (leaders/jumptables/functions) |
| recompiler_tool/discovery/functions.h | 41 | tools | v2 discovery engine (leaders/jumptables/functions) |
| tools/flicker/spatsig.py | 40 | tools | Offline analysis/debug script |
| recompiler_tool/elf/elf_image.h | 40 | tools | EE MIPS->C++ recompiler |
| host_app/graphics/gpu_vertex.h | 40 | render-IR | GPU vertex struct shared EE->render |
| tools/scan_tbps.py | 39 | tools | Offline analysis/debug script |
| recompiler_tool/emit/driver_v2.h | 38 | tools | EE MIPS->C++ recompiler |
| recompiler_tool/elf/elf_image.cpp | 38 | tools | EE MIPS->C++ recompiler |
| EEAnalyze/analyze.h | 38 | tools | Static ELF analysis library |
| tools/read_jt.py | 36 | tools | Offline analysis/debug script |
| tools/ramread.py | 36 | tools | Offline analysis/debug script |
| tools/find_bullseye_calls.py | 35 | tools | Offline analysis/debug script |
| tools/dump_bh_entries.py | 35 | tools | Offline analysis/debug script |
| tools/dump_rewrite_targets.py | 34 | tools | Offline analysis/debug script |
| host_app/input/input_backend_kbm.h | 34 | platform | Host input pipeline (backends, mapper, facade) |
| host_app/gui/gui.h | 34 | diagnostics | ImGui settings/diagnostics overlay section |
| host_app/cmake_install.cmake | 34 | unknown | ? |
| tools/recomp_gates/fps_class_gate.sh | 32 | tools | Offline analysis/debug script |
| tools/ppm_to_png.py | 32 | tools | Offline analysis/debug script |
| tests/recomp_semantics/recomp_semantics_prelude.h | 32 | tests | Test/replay harness |
| tests/gui_stub.cpp | 32 | tests | Test/replay harness |
| recompiler_tool/discovery/report.h | 32 | tools | v2 discovery engine (leaders/jumptables/functions) |
| host_app/sif.h | 31 | SIF | EE<->IOP bridge, RPC, CDVD, IOP stubs |
| host_app/platform/backtrace_compat.h | 31 | platform | Platform shim (fibers, crash handler, win compat) |
| host_app/libc_hle.h | 31 | HLE-game | Game/SDK function HLE (heap, libc, anim, mpeg) |
| EEAnalyze/DataFlowEngine.h | 31 | tools | Static ELF analysis library |
| host_app/sound/mixer.h | 29 | HLE-sdk | Host sound core (libsd boundary HLE: VAG, voices, mixer) |
| tools/decode_instr.py | 28 | tools | Offline analysis/debug script |
| host_app/platform/platform.h | 28 | platform | Platform shim (fibers, crash handler, win compat) |
| host_app/gs_hle.h | 28 | GS-state | GS register/vertex/transfer state |
| tools/phaseb_status.py | 27 | tools | Offline analysis/debug script |
| host_app/recomp_seams.cpp | 27 | EE | v2-engine runtime seams |
| tools/correlate.py | 26 | tools | Offline analysis/debug script |
| tests/recomp_semantics/gen_words.cpp | 25 | tests | Test/replay harness |
| tools/run_bounded.sh | 24 | tools | Offline analysis/debug script |
| tests/ee_timers_stub.cpp | 24 | tests | Test/replay harness |
| host_app/recomp_test.cpp | 24 | dead | Dead phase-1 test scaffold |
| tools/find_in_bh.py | 23 | tools | Offline analysis/debug script |
| host_app/sce_hle/netcnfif.cpp | 23 | HLE-sdk | sce SDK library HLE handler |
| host_app/platform/crash_handler.h | 23 | platform | Platform shim (fibers, crash handler, win compat) |
| host_app/hle/vu1_hle_runtime.h | 23 | HLE-game | VU1 microprogram HLE (native reimplementation) |
| tools/brace_check.py | 22 | tools | Offline analysis/debug script |
| tests/recompiler_verify/mem_image.cpp | 22 | tests | Test/replay harness |
| host_app/gui/vu_microcode_section.h | 22 | diagnostics | ImGui settings/diagnostics overlay section |
| host_app/cmake/gen_build_id.cmake | 22 | tools | CMake build-id generator |
| host_app/sema.h | 21 | EE | EE kernel/CPU/memory/interrupts/timers |
| tests/savestate_stub.cpp | 19 | tests | Test/replay harness |
| host_app/thread_qos.h | 19 | platform | Host integration (pad facade, display, QoS) |
| host_app/sif.cpp | 19 | SIF | EE<->IOP bridge, RPC, CDVD, IOP stubs |
| host_app/sce_hle/libdev.cpp | 19 | HLE-sdk | sce SDK library HLE handler |
| host_app/iop_context.h | 19 | SIF | EE<->IOP bridge, RPC, CDVD, IOP stubs |
| tools/check_inc.py | 17 | tools | Offline analysis/debug script |
| host_app/vu_aot_programs_9.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_8.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_7.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_6.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_5.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_4.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_31.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_30.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_3.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_29.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_28.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_27.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_26.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_25.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_24.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_23.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_22.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_21.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_20.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_2.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_19.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_18.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_17.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_16.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_15.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_14.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_13.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_12.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_11.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_10.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_1.cpp | 17 | VU | VU1/VU0 execution |
| host_app/vu_aot_programs_0.cpp | 17 | VU | VU1/VU0 execution |
| host_app/platform/proc_stats.h | 16 | platform | Platform shim (fibers, crash handler, win compat) |
| host_app/gui/bug_report_section.h | 16 | diagnostics | ImGui settings/diagnostics overlay section |
| tests/recompiler_verify/savestate_tests.h | 15 | tests | Test/replay harness |
| host_app/third_party/imgui/imconfig.h | 15 | third-party | Vendored Dear ImGui (exempt from refactor) |
| host_app/iop_loader.h | 15 | SIF | EE<->IOP bridge, RPC, CDVD, IOP stubs |
| host_app/gui/sound_section.h | 15 | diagnostics | ImGui settings/diagnostics overlay section |
| tools/sce_lib_finder/__init__.py | 14 | tools | Offline analysis/debug script |
| host_app/vu_aot_status.h | 13 | VU | VU1/VU0 execution |
| tests/recompiler_verify/controlflow_tests.h | 12 | tests | Test/replay harness |
| tests/recompiler_verify/oracle_anchor_tests.h | 11 | tests | Test/replay harness |
| tests/recompiler_verify/interp_core_tests.h | 11 | tests | Test/replay harness |
| host_app/timer_hle.h | 10 | EE | EE kernel/CPU/memory/interrupts/timers |
| host_app/recomp_test.h | 9 | dead | Dead phase-1 test scaffold |
| tests/vu1_xgk_capture.h | 8 | tests | Test/replay harness |
| host_app/runtime.h | 7 | EE | EE kernel/CPU/memory/interrupts/timers |
| host_app/runtime.cpp | 0 | EE | EE kernel/CPU/memory/interrupts/timers |
