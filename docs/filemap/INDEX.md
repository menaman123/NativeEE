# FILE MAP INDEX — every hand-written file of Ps2Recomp mapped into the refactor tree

Consolidated from the nine shard filemaps (`FILEMAP_host_app_0..5.md`,
`FILEMAP_graphics.md`, `FILEMAP_hle_sce_sound_input_gui_platform.md`,
`FILEMAP_toolchain_tests_tools_build.md`). Row detail (purpose, public surface, full
flag tables, hack lists, risks, proving tests) lives in those files; this INDEX is the
one-table view + the reverse map.

## Totals

- **386 mapped rows** covering **602 A1-inventory files** (rows for f0–f7 are one per
  file, with a few documented pair/group rows; shard 8 maps tests/tools/generated
  populations as reconciled groups: tests 158 files in 14 groups, tools 74 in 6 groups,
  222 `fun_*_body.inc` as one retired class, 28 recompiler_tool + 4 iop_recompiler +
  10 EEAnalyze files individually or in named groups).
- Coverage verified against `audit/A1_STRUCTURE.md` Appendix A (602 rows): **589/602
  covered by shard entries**; 13 without per-file entries, resolved below (10 exempt
  vendored imgui, 1 rider, 2 consolidator-added rows).
- Line totals per shard: f0 24,611 · f1 24,605 · f2 24,606 · f3 24,610 · f4 24,607 ·
  f5 24,607 · f6 47,030 · f7 ~21,474 · f8 ~101k (recompiler 31,381 + tests 55,340 +
  tools 9,019 + EEAnalyze 2,106 + iop 1,741 + build/scripts/config).

## Coverage verification vs A1 Appendix A (omissions)

Scripted diff of A1's 602 inventory paths against the union of shard coverage:

| A1 path | Status | Resolution |
|---|---|---|
| host_app/third_party/imgui/* (10 files, 56,339 ln) | exempt | A1 marks vendored imgui exempt from refactor; f7 records the destination: vendors to `app/gui/third_party/` |
| host_app/common/Pcsx2Types.h (114 ln) | rider-covered | mapped inside f0's cpu_state.h entry ("+ common/Pcsx2Types.h alongside — verbatim; EXEMPT" ) → `engine/ee/cpu/` beside cpu_state.h |
| host_app/common/Pcsx2Defs.h (165 ln) | **OMISSION** — consolidator-added row | PCSX2-vendored macro/defs header, includer of record for Pcsx2Types; same fate: verbatim → `engine/ee/cpu/` (vendor-exempt, lint-exempt like cpu_state.h) |
| host_app/cmake_install.cmake | **OMISSION** — consolidator-added row | build artifact leaked into the source tree (A1 finding #13) — DELETE/untrack Phase 1 (build hygiene row; no code content) |

Net: **2 true omissions**, both resolved with consolidator rows above; zero unmapped
hand-written code files remain.

## Destination collisions

Two sources mapped onto one new home with a shape/ownership disagreement. One hard
collision (decided), five soft ones (naming/coordination):

1. **HARD — `timer_hle.{h,cpp}` mechanism owner.** Shard 3 (.cpp) routes per MIGRATION
   §1.6 row E: mechanism → `ee/timing` (budget family), params → `games/…/hle/budget.cpp`;
   shard 5 (.h) proposes `engine/ee/hle/timer_wait` + a `TitleProfile.hle` row for
   0x191BD0. **Decision: follow §1.6 row E (shard 3)** — the 0x191bd0 wait model is the
   budget family's; shard 5's `TitleProfile.hle` row stands as the title-data half.
   The `ee/hle/timer_wait` node is NOT created (drop that TREE-GAP proposal; adjudicate
   against the main.cpp budget hook per shard 3's errata).
2. Soft — `engine/core/hash.h` receives BOTH render.h's vendored XXH64 (f3) and
   vu_hash_fnv.h's FNV64 (f5). Compatible (two functions, one module) but the merge
   must be coordinated: one header, both reference-vector test sets.
3. Soft — meta-arena quarantine module named `ee/mem/workarounds/meta_arena` (f0) vs
   `ee/mem/workaround_meta_arena` (f3). Same thing; pick `ee/mem/workarounds/meta_arena`
   (folder-per-quarantine matches HOOK §3.8's KEY-REDIRECT sibling).
4. Soft — `gs_direct_ops.h`: MIGRATION says gs/hle/sce, f7 argues gs/hle/ir (keeps the
   three converters of the lowering ladder together). Adopt **ir/** (f7's reasoning);
   doc delta recorded.
5. Soft — MIGRATION §1.3 says the six native renderers go to "gs/hle/classes/", §1.6 and
   the ARCH tree say `gs/hle/classrdr/`. Adopt **classrdr/** (f6); fix §1.3 wording.
6. Soft — `gif_template.h`: f4's gif.cpp row keeps `engine/gif/packet_template` (the
   TplTryServe machinery), f3's gif_template.h row says verbatim-or-delete (default-OFF,
   superseded by M5A). Not a shape conflict — one is the mechanism inside gif.cpp, one is
   the standalone cache header — but the Phase-4 wave must decide the cache's fate once
   (recorded on the purge checklist).

## TREE-GAPs (merged; nodes/sections the ARCH §3 tree or Config schema must add or rename)

New engine nodes: 1. `engine/gs/state/ir_apply.*` (typed DrawIR/DirectOps apply entry —
f4) · 2. `engine/cdvd/delivery_meter.*` (CdvdBudgetMeter — f4) · 3.
`engine/cdvd/delivery_budget.*` (budget_lift re-home; MIGRATION §1.3's ee/timing glob
corrected — f0) · 4. `engine/gs/hle/present/composite_guard.*` (cross-target
composite-freshness rule — f5; collapses into targets/render_target if §4.3/§4.4 epochs
subsume it) · 5. `engine/ee/mem/elf_loader.*` (fourth ee/mem member — f5) · 6.
`engine/gs/hle/classrdr/sequence_recipe.*` (engine executor for RendererProfile.sequences
— f6) · 7. `engine/gs/hle/targets/scale_policy.*` (f6) · 8. `engine/platform/sdl/`
shared-implementation folder composed by the per-target folders (f7) · 9.
`engine/gif/boundary_slot.*` (R168 machinery named — f2).

New games/-side homes: 10. `games/…/vu0_programs.cpp` (VU0 macro-program descriptor
table — f4) · 11. `games/…/data/archive_table.*` (DATA.BD/BH layout — f4) · 12.
`games/…/iop/stream_irx/recomp_seams.toml` (generator-side per-title seam input,
consumed via TitleProfile.recomp — f8 T-G3).

Schema/docs/convention gaps: 13. `Config.harness.*` section (warp_to_frame et al. —
f5) · 14. `verify/bench/` home for the bench launcher + report schema (f8 T-G2) · 15.
`docs/RECOMP_CONVENTIONS.md` home for the recompiler rule catalog (f8 T-G5) · 16. ONE
tests-tree convention (top-level tests/ mirroring module paths vs per-module tests/ —
f8 T-G4; the per-group destinations in this INDEX assume the top-level mirror) · 17.
`engine/tools/EEAnalyze/` → `engine/tools/ee_analyze/` snake_case rename (f8 T-G1).

Plan-row corrections carried from shards (not tree gaps): runtime.{cpp,h} MIGRATION
row is stale (file dead — f0/f1); s8_inrt.h → gs/hle/texsrc not ee/timing (f1);
s8_ctxlatch dissolve target is gif/ not ee/timing+ee/hle (f3); render.h has ~21 gate
namespaces not 5 (f3); ee_timers standardized to `engine/ee/intc/ee_timers.*` with
clock reads through ee/timing DerivedClocks (f1/f3); sema.h is dead payload not a
verbatim move (f0); opengl_backend "in-TU test main" claim is stale (f6); netcnfif
handler may be unreachable — verify boot BOUND census before porting (f7);
`engine/render/backends/gl/gl_draw_merge.*` only if consumer-side merge survives
profiling (recommended: it does not — f2).

---

## Master table, grouped by destination directory of the NEW tree

Grouping key = the row's PRIMARY destination (first-named). Split files appear once,
under their primary home, with the full destination string in the row; the reverse
index below shows every home a source feeds. Flags column is the shard's
N→NORM/SETTING/DIAG/TEST/DELETE tally; hacks column is the entry count in HACK_LEDGER
source entries.

### app/gui/  (6 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| gui/bug_report_section.cpp | 159 | G | app/gui/triage_section.cpp | rewritten (over core/triage) | 2 → 2 TEST | 1 | f7 |
| gui/bug_report_section.h | 16 | G | app/gui/triage_section.h | rewritten (over core/triage) | 0 | 0 | f7 |
| gui/gui.cpp | 753 | G | app/gui/gui_host.* + imgui_backend_gl.* + settings_window.* | split | 0 | 1 | f7 |
| gui/gui.h | 34 | G | app/gui/gui.h | verbatim | 0 | 0 | f7 |
| gui/savestate_browser.cpp | 506 | G | app/gui/savestate_browser.cpp | move | 2 → 2 TEST | 1 | f7 |
| gui/savestate_browser.h | 249 | G | app/gui/savestate_rows.* + savestate_browser.h | split | 0 | 0 | f7 |

### app/host/  (3 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| graphics_backend.cpp | 114 | GENERIC-PS2 | app/host composition root (backend selection from Config) | rewritten | 0 | 0 | f6 |
| main.cpp | 14,690 | MIXED | app/host/main.cpp + core/{config,boot} + app/host/shutdown; hooks per HOOK ledger (195 DIAG del / 4 workaround / 1 timing HLE) | split | 45→8/2/12/4/19 | 8 | f0 |
| shutdown_policy.h | 90 | GENERIC-PS2 | app/host/shutdown | verbatim | 1→0/1/0/0/0 | 1 | f0 |

### engine/cdvd/  (6 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| budget_lift.h | 223 | GENERIC-PS2 | cdvd/delivery_budget | rewritten as class (purge-dissolve) | 5→5/0/0/0/0 | 3 | f0 |
| cdvd_stream.cpp | 425 | GENERIC-PS2 | cdvd/stream_reader + cdvd/hle bindings | moves verbatim | 0 | 4 | f2 |
| cdvd_stream.h | 48 | GENERIC-PS2 | engine/cdvd/stream_reader.* + cdvd/hle SdkBinding rows | verbatim | 0 | 2 | f4 |
| hle_heap.h | 521 | MIXED | cdvd/{read_engine,iso_fs} + cdvd/hle/loader + ee/hle/heap + title loader_sync data | split | 0 | 4 | f0 |
| ps2_vblank.h | 380 | GENERIC-PS2 | engine/ee/timing/tick_source.* + time_domain + engine/cdvd/delivery_meter.* | split | 2 (1/1/0/0/0) (+3d) | 4 | f4 |
| sce_hle/libcdvd.cpp | 402 | G | engine/cdvd/hle/libcdvd_bindings.cpp | move | 0 | 0 | f7 |

### engine/core/  (1 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| vu_hash_fnv.h | 50 | GENERIC-PS2 | engine/core/hash | V | 0 | 0 | f5 |

### engine/core/config/  (3 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| display.ini (runtime artifact) | n/a | G | core/config [video] | R (fold) | 0/all S/0/0/0 | 0 | f8 |
| display_settings.cpp | 191 | GENERIC | core/config [video] + gs/hle/present/view_state | split | 1 → 0/0/0/0/1 | 1 | f3 |
| display_settings.h | 69 | GENERIC-PS2 | core/config [video] + gs/hle/present/view_state | split | 1→0/0/1/0/0 | 1 | f0 |

### engine/core/diag/  (16 source rows)

> **Status (L8, PHASE6_IMPL item 7): deferred to integration / Phase 8** — not written in Phase 6 stages 6A–6F″; pointers: the GS diag categories — `drawlist_recorder` (the `DrawIrSink` decorator writing `GsdlFormat`), `artdet` / `artdet_flicker`, `vram_census`, `gs_c3_counters`, every DIAG row of the Phase-6 tally (PHASE6_IMPL item 8); scope = PHASE6_IMPL INTEGRATION CHECKLIST item 8 ("Deferred modules"). [IDOC-A 2026-10-01]

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| artdet_flicker.cpp | 338 | GENERIC-PS2 | core/diag/artdet + triage spike path (bundle writer deletes) | split | 5→1/2/1/1/0 | 3 | f0 |
| artdet_flicker.h | 47 | GENERIC-PS2 | core/diag/artdet.* + core/triage (fire path) | split | 0 (+3d) | 2 | f4 |
| bench.h | 713 | GENERIC-PS2 | core/diag/perf_bench (feeds core/triage PERF) | verbatim → .h/.cpp pair | 0 (setenv PS2_PERF write dies) | 3 | f0 |
| bench_report.h | 781 | GENERIC-PS2 | core/diag/bench_report | moves + scrapers rewritten | 0 | 2 | f2 |
| call_census.cpp | 491 | GENERIC-PS2 | engine/core/diag/call_census (verbatim, diag builds) | V | 3 → 0/0/3/0/0 | 1 | f5 |
| call_census.h | 99 | GENERIC-PS2 | core/diag/call_census (GenProbe channel) | moves | 3→0N/0S/3D | 2 | f2 |
| gs_c3_counters.h | 79 | GENERIC-PS2 | core/diag (emitters) + counter points feed core/triage PerfMonitor | moves | 0 | 1 | f6 |
| hle/drawlist_record.cpp | 403 | G | core/diag/drawlist_recorder.cpp | move | 5 → 5 DIAG | 2 | f7 |
| hle/loader_oracle.h | 144 | M | core/diag/loader_trace.* (harness-armed) + tools checker | split | 2 → 1 TEST / 1 DELETE | 2 | f7 |
| input/input_latency.h | 55 | G | core/diag/input_latency.* | move | 1 → 1 DIAG | 1 | f7 |
| loadspan.h | 328 | GENERIC (diag) | core/diag/loadspan | move-to-diagnostics-build | 2 → 0/0/2/0/0 | 4 | f1 |
| mid_census.h | 425 | GENERIC-PS2 | engine/core/diag/mid_census.* | move-to-diag | 2 (0/0/2/0/0) | 3 | f4 |
| perf_profiler.h | 610 | GENERIC-PS2 | core/diag/perf_profiler + core/triage/perf_monitor | split | 1→0N/0S/1D/0T/0X | 3 | f2 |
| snd_census.h | 454 | MIXED (diag) | core/diag/snd_census (title cmd-table as data) | move-to-diagnostics-build | 1 → 0/0/1/0/0 | 3 | f1 |
| stick_trace.h | 414 | GENERIC-PS2 | engine/core/diag/stick_trace | V | 1 → 0/0/1/0/0 | 1 | f5 |
| vu_trace_case.h | 93 | GENERIC (diag fmt) | core/diag/vutrace_case (also triage producer, D9) | verbatim (format frozen) | 0 | 2 | f1 |

### engine/core/log/  (8 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| debug_budget.h | 69 | GENERIC-PS2 | core/log rate-limit policy (absorbed) | deleted | 0 | 1 | f2 |
| hw_verbose.h | 47 | GENERIC-PS2 | core/log categories | delete (dissolve) | 1→0/0/1/0/0 | 1 | f0 |
| ring_log.cpp | 595 | GENERIC-PS2 | engine/core/log/ring.* (verbatim) + bundle assembly → core/triage/triage_bundler | S | 4 → 1/2/1/0/0 | 2 | f5 |
| ring_log.h | 232 | GENERIC-PS2 | core/log/ring_sink + core/diag/mem_gauges + triage feed | split | 4→1N/2S/0D/0T/1X | 3 | f2 |
| ring_log_decode.h | 333 | GENERIC-PS2 | engine/core/log/ring_decode.* + tools/diagnostics | verbatim | 0 | 1 | f4 |
| ring_log_format.h | 75 | GENERIC | core/log/ring_format.h | verbatim | 0 | 0 | f1 |
| ring_tags.h | 125 | GENERIC | core/log/ring_tags.h | verbatim | 0 | 1 | f3 |
| tests/ 5.12 diag/triage/log (7) | ~1,800 | G | core/triage + core/log + core/diag tests | V→retarget; slowspot_paths D | 0/0/3/2/0 | 1 | f8 |

### engine/core/savestate/  (5 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| savestate.h | 140 | GENERIC | core/savestate/savestate_host (+ thread SectionProvider coupling) | split | 0 | 2 | f3 |
| savestate_format.cpp | 242 | GENERIC | core/savestate/format (frozen) | verbatim | 0 | 0 | f3 |
| savestate_format.h | 234 | GENERIC-PS2 | engine/core/savestate/savestate_format (format frozen) | V | 1 → 1/0/0/0/0 | 2 | f5 |
| savestate_writer.cpp | 185 | GENERIC-PS2 | core/savestate/capture_writer | moves verbatim | 0 | 2 | f2 |
| savestate_writer.h | 106 | GENERIC-PS2 | engine/core/savestate/writer.* | verbatim | 1 (1/0/0/0/0) | 1 | f4 |

### engine/core/threads/  (1 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| sound/ring.h | 88 | G | engine/core/threads/cmd_ring.h (.inl) | move | 0 | 0 | f7 |

### engine/core/triage/  (6 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| bug_report.cpp | 564 | GENERIC | core/triage/{triage_bundler,bundle_manifest} (D9) | split/rewrite | 3 → 0/3/0/0/0 | 3 | f3 |
| bug_report.h | 459 | GENERIC-PS2 | core/triage/{triage_bundler, bundle_manifest} | split | 0 | 0 | f2 |
| slowspot.cpp | 306 | GENERIC-PS2 | core/triage/perf_monitor (capture trio → TriageBundler) | split | 5→4/1/0/0/0 | 4 | f0 |
| slowspot.h | 61 | GENERIC | core/triage/perf_monitor (D9) | split | 0 | 1 | f3 |
| slowspot_detector.h | 129 | GENERIC-PS2 | core/triage/window_median (PerfMonitor core) | verbatim | 0 | 0 | f0 |
| slowspot_paths.h | 66 | GENERIC-PS2 | absorbed by TriageWriter layout | delete after absorption | 1→1/0/0/0/0 | 1 | f0 |

### engine/dmac/  (2 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| dmac.h | 178 | GENERIC-PS2 | engine/dmac/{dmac,dmac_channel,handlers}.h | verbatim, reshaped | 0 | 2 | f4 |
| sce_hle/libdma.cpp | 82 | G | engine/dmac/hle/libdma_bindings.cpp | move | 0 | 1 | f7 |

### engine/ee/cpu/  (1 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| recomp_seams.cpp | 27 | GENERIC | ee/cpu/seams.cpp | verbatim (frozen) | 0 | 0 | f1 |

### engine/ee/hle/  (13 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| libc_hle.cpp | 495 | GENERIC-PS2 | ee/hle/libc.cpp (SdkBinding rows) | verbatim | 1→1/0/0/0/0 | 3 | f0 |
| libc_hle.h | 31 | GENERIC-PS2 | ee/hle/libc.h | verbatim | 0 | 1 | f0 |
| ps2_argv.h | 115 | GAME (row E) | engine/ee/hle/boot_env + TitleProfile.boot | S | 0 | 1 | f5 |
| ps2_syscall_wrappers.cpp | 265 | GENERIC-PS2 | merged into engine/ee/hle/syscalls.* | split (dissolve) | 0 | 1 | f4 |
| ps2_syscall_wrappers.h | 62 | GENERIC-PS2 | dissolves into ee/hle/syscalls.* | split (dissolve) | 0 | 0 | f4 |
| ps2_threading.cpp | 1,481 | GENERIC-PS2 | ee/hle/{threads,semaphores,boot_env} + core/diag census | split | 1→1/0/0/0/0 | 4 | f0 |
| ps2_threading.h | 326 | GENERIC | ee/hle/threads (class ThreadManager; maps leave header) | split/rewrite | 0 | 2 | f3 |
| sce_hle/libdev.cpp | 19 | G | engine/ee/hle/libdev_bindings.cpp | move | 0 | 0 | f7 |
| sce_hle/libscf.cpp | 107 | G | engine/ee/hle/libscf_bindings.cpp | move | 0 | 2 | f7 |
| sce_hle/sce_hle.h | 44 | G | engine/ee/hle/sdk_binding.h | rewritten (manifest contract) | 0 | 1 | f7 |
| sce_hle/sce_hle_registry.cpp | 604 | G | engine/ee/hle/sdk_binding.cpp (+ per-subsystem row registration) | rewritten (same install seam) | 0 | 3 | f7 |
| syscalls.h | 86 | GENERIC-PS2 | ee/hle/{syscalls,dyn_interp} | split | 0 | 3 | f1 |
| timer_hle.h | 10 | GAME binding | engine/ee/hle/timer_wait + TitleProfile.hle row (0x191BD0) | S | 0 | 1 | f5 |

### engine/ee/intc/  (3 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| ee_timers.h | 177 | GENERIC-PS2 | ee/intc/{ee_timers,timer_codec} (+TitleProfile seed) | split | 1 → 1/0/0/0/0 | 4 | f1 |
| intc.cpp | 136 | GENERIC-PS2 | engine/ee/intc/intc.* | verbatim (+flat table) | 0 | 3 | f4 |
| intc.h | 77 | GENERIC-PS2 | engine/ee/intc/intc (globals fold into class) | V | 0 | 1 | f5 |

### engine/ee/mem/  (4 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| hle_meta_arena.cpp | 350 | GAME | quarantined ee/mem/workaround_meta_arena → retired at 0x181560 root fix (HOOK §3.1) | quarantine→delete | 0 | 3 | f3 |
| hle_meta_arena.h | 176 | GAME-SPECIFIC | ee/mem/workarounds/meta_arena (quarantine; retire at 0x181560 root fix) | quarantine-then-delete | 0 | 4 | f0 |
| memory.cpp | 5,373 | MIXED (row H) | engine/ee/mem/{address_map,mmio_router,fastmem,elf_loader} + TitleProfile.elf + core/diag probes + KEY-REDIRECT quarantine | S | 2 → 0/0/0/0/2 | 6 | f5 |
| memory.h | 284 | GENERIC-PS2 (+1 game hook) | ee/mem/{memory, fastmem} + quarantined key workaround | split | 2→1N/0S/1D | 5 | f2 |

### engine/ee/timing/  (5 audited source rows + 5 post-audit rows: ee_timing/ + ee_sched/ classes added 2026-09-25)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| ee_timers.cpp | 605 | MIXED | ee/intc/ee_timers + ee/timing/time_domain | split | 6 → 2/1/1/0/2 | 3 | f3 |
| time_domain.h | 319 | GENERIC-PS2 | ee/timing/{emu_domain,regime_detector} | split (logic verbatim) | 2 → 2/0/0/0/0 | 3 | f1 |
| timeline.h | 498 | GENERIC-PS2 | core/diag/{frame_timeline,ee_stall_census}; WAITFIX arm → ee/timing/boundary_protocol (NORM) | S | 3 → 1/0/2/0/0 | 3 | f5 |
| turbo.h | 502 | GENERIC-PS2 | engine/ee/timing/turbo_governor.* (+time_domain fold) | split | 8 (6/1/0/0/1) | 5 | f4 |
| warp.h | 124 | GENERIC-PS2 | engine/ee/timing/warp | V | 1 → 0/1/0/0/0 | 1 | f5 |
| ee_timing/tick_sync.{h,cpp} [TICKSYNC 2026-09-25] | 108+127 | GENERIC-PS2 | engine/ee/timing/tick_sync.* (one class, verbatim) | V | 2 → 0/1/0/1/0 (PS2_TICK_SYNC SETTING, PS2_FRAME_LOCKSTEP TEST) | 0 | post-audit |
| ee_timing/frame_rendezvous.{h,cpp} [TICKSYNC 2026-09-25] | 81+84 | GENERIC-PS2 | engine/ee/timing/frame_rendezvous.* (one class, verbatim) | V | 0 | 0 | post-audit |
| ee_timing/boundary_sequencer.{h,cpp} [TICKSYNC 2026-09-25] | 94+75 | GENERIC-PS2 | engine/ee/timing/boundary_sequencer.* — THE fixed frame-boundary order (ARCH §7.2); the step bodies stay with their owners (gs_hle boundary → ee/hle/frame_boundary) | V | 0 | 0 | post-audit |
| ee_sched/ee_run_token.{h,cpp} [P0-EETOK 2026-09-25] | 215+404 | GENERIC-PS2 | engine/ee/sched/ee_run_token.* (one class; thbase election, relaunch handoff, DiagPrintf) | V | 2 → 1/0/1/0/0 (PS2_EE_RUNTOKEN NORM, PS2_EESCHED_DIAG DIAG) | 0 | post-audit |
| ee_sched/ee_event_queue.{h,cpp} [P0-EETOK 2026-09-25] | 134+148 | GENERIC-PS2 | engine/ee/sched/ee_event_queue.* (one class; holder-only drain, BUG-035) | V | 0 | 0 | post-audit |

### engine/gif/  (3 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| gif_arbiter.cpp | 1,808 | MIXED | engine/gif/arbiter | split (experiments deleted) | 6→0N/0S/1D/0T/5X | 5 | f2 |
| gif_arbiter.h | 684 | GENERIC-PS2 | engine/gif/arbiter.* (+drain_thread.*) | verbatim, reshaped per §13 | 2 (1/1/0/0/0) | 5 | f4 |
| gif_template.h | 116 | GENERIC | gif/packet_template (MIGRATION row) — or delete at purge (default-OFF, superseded by M5A; errata) | verbatim-or-delete | 2 → 0/0/0/1/1 | 1 | f3 |

### engine/gs/format/  (4 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| gs_deswizzle.h | 196 | GENERIC-PS2 | gs/format/{psm_layout, clut} (collapsed) | rewritten (family merge) | 0 | 3 | f2 |
| gs_vram_census.h | 362 | GENERIC-PS2 | engine/core/diag/vram_census (page math via gs/format/PsmLayout) | V | 1 → 0/0/1/0/0 | 1 | f5 |
| tests/ 5.1 GS properties (9) | ~4,900 | G | tests/gs/ (gs/format+soft+queue) | V | 0 | 0 | f8 |
| tests/ 5.2 block-decode equiv (1) | 1,150 | G | tests/gs/format/ (extended Ph3) | V+extend | 0 | 0 | f8 |

### engine/gs/hle/classrdr/  (8 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| opengl/fmv_blit_native.inc | 1,633 | MIXED | engine/gs/hle/classrdr/fmv_blit_renderer.{h,cpp}; family-F spec + 0x150000/0x70000 → profile | split | 2→2/0/0/0/0 | 2 | f6 |
| opengl/particles_native.inc | 1,667 | MIXED | engine/gs/hle/classrdr/particles_renderer.{h,cpp}; tuples/pins → profile | split | 2→2/0/0/0/0 | 2 | f6 |
| opengl/s9_composite.h | 383 | GAME | recipe constants → renderer_profile; FSM enum/hold → stencil_composite_renderer.h | split | 3→2/0/1/0/0 | 1 | f6 |
| opengl/s9_composite_native.inc | 2,273 | MIXED(→GAME recipe) | engine/gs/hle/classrdr/stencil_composite_renderer.{h,cpp} (+ SequenceRecipe engine); 7-pass recipe + 0x230000/0x280000/0x268000 → renderer_profile | split | 4→0/0/1/0/3 | 4 | f6 |
| opengl/skinned_native.inc | 1,680 | MIXED | engine/gs/hle/classrdr/skinned_renderer.{h,cpp} (+ raw slot registration); tuples/pin → profile | split | 4→4/0/0/0/0 | 3 | f6 |
| opengl/sprite_hud_native.inc | 1,679 | MIXED | engine/gs/hle/classrdr/sprite_hud_renderer.{h,cpp}; tuples/family/0x230000 → profile; roundSprite → TitleProfile.quirks | split | 2→2/0/0/0/0 | 3 | f6 |
| opengl/world_tristrip_native.inc | 1,300 | MIXED | engine/gs/hle/classrdr/world_tristrip_renderer.{h,cpp}; Accept tuple + 0x230000 + World2 trigger → renderer_profile | split | 2→2/0/0/0/0 | 3 | f6 |
| shuf_snap.h | 149 | GENERIC-PS2 | gs/hle/classrdr/texture_shuffle_snap | purge-dissolve (snap stays) | 1 → 1/0/0/0/0 | 3 | f1 |

### engine/gs/hle/interp/  (1 source rows)

> **Status (L8, PHASE6_IMPL item 7): deferred to integration / Phase 8** — not written in Phase 6 stages 6A–6F″; pointers: P6-INTERP-DEFERRED (`FrameInterpolator`; OLD `gs_interp.h` + `opengl_backend.cpp:19576-19588` shadow swap + `graphics_thread.cpp:3396-3465`; tests `test_gs_interp.cpp` → the SKIP-77 stubs `Gs_Inv25/26`); scope = PHASE6_IMPL INTEGRATION CHECKLIST item 8 ("Deferred modules"). [IDOC-A 2026-10-01]

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| gs_interp.h | 666 | GENERIC | gs/hle/interp/frame_interp | verbatim + purge | 5 → 3/2/0/0/0 | 3 | f3 |

### engine/gs/hle/ir/  (6 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| gif.h | 216 | GENERIC-PS2 | gif/{gif_tag,gif_unit,parse} + gs/hle/ir apply seam | split | 0 | 4 | f1 |
| gpu_vertex.h | 40 | GENERIC-PS2 | split: VertexRec→gs/hle/ir; GL layout→backends/gl/gl_vertex_stream.h | split | 0 | 0 | f6 |
| hle/drawlist_record.h | 241 | G | engine/gs/hle/ir/gsdl_format.h + core/diag/drawlist_recorder.h | split | 0 | 0 | f7 |
| hle/gs_direct_ops.h | 151 | G | engine/gs/hle/ir/direct_ops.* | move (body → .cpp) | 0 | 1 | f7 |
| hle/gs_draw_ir.h | 638 | G | engine/gs/hle/ir/draw_ir.* + draw_ir_verifier.* | split | 0 | 1 | f7 |
| render.h | 3,734 | MIXED | gs/hle/ir/{draw_ir,render_batch} + gs/hle/queue/{render_queue,draw_run_merge} + core/hash.h; ~21 gate ns dissolve | split | 45 (+13 fam) → 24/1/3/1/16 | 7 | f3 |

### engine/gs/hle/present/  (9 source rows)

> **Status (L8, PHASE6_IMPL item 7): deferred to integration / Phase 8** — not written in Phase 6 stages 6A–6F″; pointers: the present_policy / pacer / quantum rows — `present_policy`, `present_decimator`, `pacer`, `sample_ring`, `band_selector`, `pace_plan`, `cap_stepper`, `load_grace`, `view_state`, `composite_guard` (pacing / `video.fps_cap`, NOT pixels; OLD `present_pace.h`, `quantum.h`; the present gate `present_now = fresh>0 || !has_new_draws || holds>=2`, INV §9 step 4). Written in Phase 6: `scanout_composer`, `presenter`, `crtc_classifier`, `viewport_math` (see PHASE6_IMPL 6D); scope = PHASE6_IMPL INTEGRATION CHECKLIST item 8 ("Deferred modules"). [IDOC-A 2026-10-01]

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| graphics_thread.cpp | 4,341 | MIXED (row C) | engine/gs/hle/{queue/queue_consumer,present/presenter,present/composite_guard} + renderer_profile vram + core/{diag,triage} | S | 29 → 3/0/14/3/9 | 6 | f5 |
| gs.cpp | 627 | GENERIC-PS2 | engine/gs/state/privileged.* + ee/timing/boundary_protocol.* + gs/hle/present/ | split | 1 (0/0/1/0/0) | 3 | f4 |
| gs_crtc_class.h | 112 | MIXED | gs/hle/present/crtc_classifier | moves + pin generalized | 0 | 1 | f2 |
| gs_hle.cpp | 1,113 | MIXED | gs/hle/sce + ee/timing/boundary_protocol + dmac/hle + gs/hle/present + platform | split | 0 (consumes others) | 5 | f2 |
| present_pace.h | 1,146 | GENERIC-PS2 | engine/gs/hle/present/{pacer,sample_ring,band_selector,pace_plan,cap_stepper,load_grace} | S | 9 → 4/1/1/0/3 | 3 | f5 |
| ps2_vblank.cpp | 1,167 | GENERIC-PS2 | ee/timing/{tick_source,boundary_protocol} + ee/intc + gs/hle/present/pacing | split (+delete probes) | 5→1/3/1/0/0 | 5 | f0 |
| quantum.h | 171 | GENERIC-PS2 | ee/timing/boundary_protocol + gs/hle/present/present_decimator | split | 2→1N/1S | 1 | f2 |
| tests/ 5.7 timing/pacing (11) | ~3,600 | G/M | tests/ee_timing/, tests/gs/present/ | V (Ph7 gates) | 0 | 0 | f8 |
| viewport_math.h | 154 | GENERIC-PS2 | engine/gs/hle/present/viewport_math.{h,cpp} | verbatim | 0 | 0 | f6 |

### engine/gs/hle/queue/  (1 source rows)

> **Status (L8, PHASE6_IMPL item 7): deferred to integration / Phase 8** — not written in Phase 6 stages 6A–6F″; pointers: 6B HANDOFF (`RenderQueue`, `QueueConsumer`, `DrawRunMerge` — the merge ACTION; OLD `render.h` queue half, `graphics_thread.cpp` consumer, `ee_pace.h`); scope = PHASE6_IMPL INTEGRATION CHECKLIST item 8 ("Deferred modules"). [IDOC-A 2026-10-01]

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| ee_pace.h | 367 | GENERIC-PS2 | ee/timing/time_domain + gs/hle/queue/render_queue + core/diag | split | 7→4N/1S/1D/0T/1X | 3 | f2 |

### engine/gs/hle/sce/  (4 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| gs_hle.h | 28 | MIXED | gs/hle/sce + ee/timing + platform/sdl (per §1.4 row) | split/dissolve | 0 | 3 | f1 |
| hle/gs_packet.h | 572 | G | engine/gs/hle/sce/gs_packet.* + packet_diff.* | split (bodies out of header) | 0 | 1 | f7 |
| sce_hle/libgraph.cpp | 123 | M | engine/gs/hle/sce/ (rows + gs_callbacks.*) | split | 0 | 2 | f7 |
| tests/ 5.4 DrawIR/classrdr/replay (9) | ~4,300 | G/M | tests/gs/hle/ + replay tools | V | 0/0/0/1/0 | 0 | f8 |

### engine/gs/hle/targets/  (2 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| opengl/gs_scale.h | 70 | MIXED | engine/gs/hle/targets/scale_policy.{h,cpp}; factor→Config.video.scale; 0x230000 pin→profile VramLayout | verbatim + de-game | 1→0/1/0/0/0 | 1 | f6 |
| opengl/rt_copy_tracker.h | 256 | GENERIC-PS2 | gs/hle/targets coherence_state/surface_views methods | split | 2→2/0/0/0/0 | 1 | f6 |

### engine/gs/hle/texsrc/  (13 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| foglane_live.h | 85 | MIXED | gs/hle/texsrc/texture_source (absorbed) | rewritten | 2→1N/0S/0D/0T/1X | 0 | f2 |
| gs_mipmap.h | 358 | GENERIC-PS2 | gs/format/mip_chain + gs/hle/texsrc/mip_sampler | split | 5 → 3/0/1/0/1 | 4 | f1 |
| idx16_lane.h | 118 | GENERIC-PS2 | gs/hle/targets/render_target + texsrc ResolveRead | purge-dissolve | 1→1/0/0/0/0 | 2 | f0 |
| opengl/opengl_backend.h | 2,380 | MIXED | dissolved with the .cpp: TargetFBO→targets/{target_identity,surface_views,readback_engine,coherence_state}; TexResolveOut+m_ftt_*→texsrc/texture_source.h; caches→decode_cache.h; GL members→backends/gl/*; compute members deleted | split | 0 | 4 | f6 |
| opengl/rt_neverstale.h | 351 | MIXED | predicates → gs/hle/texsrc ResolveRead freshness ladder; counters → diag; crys trigger context → profile | split | 3→2/0/1/0/0 | 0 | f6 |
| opengl/s9_shadow.h | 254 | GENERIC-PS2 | value logic → gs/format/{alpha,clut} + texsrc DepthToColor case + backend 16-bit store convention | split (gates deleted) | 6→6/0/0/0/0 | 0 | f6 |
| s8_inrt.h | 124 | GENERIC-PS2 | gs/hle/texsrc/window_in_target → ONE ResolveRead | split/fold (ROW CORRECTED) | 1 → 1/0/0/0/0 | 3 | f1 |
| tests/ 5.3 GL serve/target (27) | 12,582 | M | tests/gs/{texsrc,targets,vram}, tests/render/gl (Ph5-6 oracles) | V→retarget; off-twins D Ph4 | 0/0/0/6+/0 | fixtures carry VRAM pins | f8 |
| texcache_key.h | 235 | GENERIC-PS2 | engine/gs/hle/texsrc/content_key.* | verbatim | 0 (+1d) | 3 | f4 |
| texture_decoder.cpp | 3,049 | MIXED | gs/format/{psm_layout,clut,expand,texa,alpha,block_decode} + gs/hle/texsrc + gs/hle/present (scanout) | split | 0 | 3 | f3 |
| texture_decoder.h | 267 | GENERIC | gs/format + gs/hle/texsrc decls; r118 ns dissolves | split | 1 (+4 fam) → 5/0/0/0/0 | 2 | f3 |
| uv_bbox.cpp | 101 | GENERIC | gs/hle/texsrc/uv_bbox | verbatim | 0 | 1 | f3 |
| uv_bbox.h | 125 | GENERIC-PS2 | engine/gs/hle/texsrc/uv_bbox.* | verbatim (gates die) | 2 (0/0/0/0/2) | 3 | f4 |

### engine/gs/state/  (3 source rows)

> **Status (L8, PHASE6_IMPL item 7): deferred to integration / Phase 8** — not written in Phase 6 stages 6A–6F″; pointers: `games/<t>/hle/scene_hooks` (CONSTRUCTION §4.6 row: cutscene latch / loading-logo / base-layer tracking — OLD `gs_state.cpp:2909` FRAME latch NOT transplanted, 6B "`g_cutscene_active` FRAME latch not written"); re-adjudicate at integration if the frames lane needs it; scope = PHASE6_IMPL INTEGRATION CHECKLIST item 8 ("Deferred modules"). [IDOC-A 2026-10-01]

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| gif.cpp | 5,532 | MIXED | engine/gif/{gif_parser,gif_regs,packet_template}.* + engine/gs/state/{flush,ir_apply}.* | split | 5 (2/0/2/0/1) | 8 | f4 |
| gs.h | 45 | GENERIC-PS2 | engine/gs/state/gs_priv_regs (VideoState dissolves in) | S | 0 | 2 | f5 |
| gs_state.h | 473 | GENERIC-PS2 | engine/gs/state/{gs_registers,vertex_kick,flush}.* + gs/xfer/transfer_engine.* | split | 0 (+1d) | 4 | f4 |

### engine/gs/vram/  (5 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| gs_deposit_overlay.h | 236 | GENERIC-PS2 | gs/vram/{vram_image,page_record} | rewritten/absorbed (Phase 5) | 1 → 1/0/0/0/0 | 4 | f1 |
| gs_page_tracker.h | 517 | MIXED | gs/vram/{vram_image, page_record} (absorbed) | rewritten | 2→2N | 4 | f2 |
| gs_state.cpp | 5,623 | MIXED | gs/state/{gs_registers,vertex_kick,flush} + gs/xfer/transfer_engine + gs/vram; game consts → renderer_profile VramLayout + TitleProfile scene-hook | split | 13 → 6/0/5/0/2 | 7 | f3 |
| idx_epoch_guard.h | 115 | GENERIC-PS2 | gs/vram/page_record (the ONE epoch rule) | purge-dissolve | 1→1/0/0/0/0 | 1 | f0 |
| opengl/alias_rect.h | 216 | GENERIC-PS2 | band/coverage math → gs/vram/page_record (invariant #36) | split | 2→2/0/0/0/0 | 0 | f6 |

### engine/iop/  (5 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| iop_context.h | 19 | GENERIC-PS2 | engine/iop/iop_context.h (+.cpp for the global) | verbatim | 0 | 1 | f4 |
| iop_loader.h | 15 | GAME | rides iop_loader.cpp split | split | 0 | 0 | f1 |
| iop_memory.h | 57 | GENERIC-PS2 | iop/iop_memory | moves (storage out of header) | 0 | 1 | f2 |
| iop_sysmem_heap.h | 148 | MIXED lean-GENERIC | iop/sysmem_heap | verbatim (h/cpp split) | 0 | 3 | f1 |
| tests/ 5.9 EE differentials (7) | ~1,700 | G/GS | tests/ee_mem, tests/ee_hle, tests/iop; anim_pose D Ph2 | V / RET | 0/0/0/1/1 | 0 | f8 |

### engine/ipu/  (6 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| ipu.cpp | 600 | GENERIC-PS2 | ipu/{ipu,decode} + ipu/hle | split | 0 | 1 | f0 |
| ipu.h | 113 | GENERIC-PS2 | ipu/{ipu_device,mpeg_decode} | split | 0 | 0 | f1 |
| mpeg_hle.cpp | 2,520 | GENERIC-PS2 | engine/ipu/hle/{mpeg_context,mpeg_api,pss_demux,ffmpeg_video_decoder,frame_writer,fmv_pacer} (was `mpeg_ipu` — no such class; P7-MPEG-IPU-SPLIT: duties = `MpegContext` state + `MpegApi` entry points; PHASE7_IMPL checklist item 9) [IDOC-A 2026-10-01] | S | 10 → 2/0/2/0/6 | 4 | f5 |
| mpeg_hle.h | 318 | GENERIC-PS2 | engine/ipu/hle/{mpeg_context,fmv_pace}.* | split | 0 (+1d) | 2 | f4 |
| sce_hle/libipu.cpp | 266 | G | engine/ipu/hle/libipu_bindings.cpp | move | 0 | 1 | f7 |
| sce_hle/libmpeg.cpp | 815 | M | engine/ipu/hle/mpeg_api.* + bindings | split | 0 | 3 | f7 |

### engine/mc/  (1 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| sce_hle/libmc.cpp | 887 | G | engine/mc/hle/mc_host_store.* + mc_command_model.* + bindings | split | 2 → 1 NORM / 1 SETTING | 3 | f7 |

### engine/pad/  (6 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| input/input_backend_gamepad.cpp | 423 | G | engine/platform/sdl/input/gamepad_backend.cpp + gamepad_convert.cpp | split | 0 | 0 | f7 |
| input/input_backend_gamepad.h | 103 | G | engine/platform/sdl/input/gamepad_backend.h + gamepad_convert.h | split | 0 | 1 | f7 |
| input/test_pad_buffer.cpp | 698 | T | tests/pad/ | verbatim | 0 | 1 | f7 |
| pad_input.cpp | 1,007 | GENERIC-PS2 | engine/pad/hle/pad_protocol.* + core/diag/{input_script,stick_trace}.* | split | 5 (0/0/4/0/1) | 4 | f4 |
| pad_input.h | 81 | GENERIC-PS2 | engine/pad/hle/pad_protocol.h | rides pad split | 0 | 2 | f4 |
| sce_hle/libpad.cpp | 933 | M | engine/pad/hle/pad_protocol.* + bindings; probes → core/diag | split | 2 → 1 TEST / 1 DIAG | 3 | f7 |

### engine/platform/  (15 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| input/default_bindings.ini | 37 | G | engine/platform/sdl/input/ + Config defaults | move | 0 | 0 | f7 |
| input/input_backend.h | 48 | G | engine/platform/sdl/input/input_backend.h | verbatim | 0 | 0 | f7 |
| input/input_backend_kbm.cpp | 91 | G | engine/platform/sdl/input/kbm_backend.cpp | verbatim | 0 | 0 | f7 |
| input/input_backend_kbm.h | 34 | G | engine/platform/sdl/input/kbm_backend.h | verbatim | 0 | 1 | f7 |
| input/input_mapper.cpp | 318 | G | engine/platform/sdl/input/input_mapper.cpp | verbatim | 0 | 0 | f7 |
| input/input_mapper.h | 95 | G | engine/platform/sdl/input/input_mapper.h | verbatim | 0 | 0 | f7 |
| input/input_system.cpp | 393 | G | engine/platform/sdl/input/input_system.cpp (behind Platform.poll_input) | move | 2 → 1 NORM / 1 TEST | 2 | f7 |
| input/input_system.h | 55 | G | engine/platform/sdl/input/input_system.h | verbatim | 0 | 0 | f7 |
| platform/backtrace_compat.h | 31 | G | engine/platform/backtrace_compat.h | verbatim | 0 | 0 | f7 |
| platform/crash_handler.h | 23 | G | engine/platform/crash_handler.h | verbatim | 0 | 0 | f7 |
| platform/macos/crash_handler_macos.cpp | 175 | G | engine/platform/macos/crash_handler.cpp | move | 0 | 0 | f7 |
| platform/proc_stats.h | 16 | G | engine/platform/proc_stats.h (+ per-target .cpp) | split | 0 | 1 | f7 |
| platform/win32/crash_handler_win32.cpp | 189 | G | engine/platform/windows/crash_handler.cpp | move | 0 | 1 | f7 |
| platform/win_compat.h | 112 | G | engine/platform/file_compat.h (shrinks into Platform.open) | move | 0 | 0 | f7 |
| thread_qos.h | 19 | GENERIC | platform/threads (macos impl) | verbatim | 1 → 1/0/0/0/0 | 1 | f3 |

### engine/render/  (1 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| graphics_backend.h | 174 | GENERIC-PS2 | engine/render/backend.h (IRenderBackend, ARCH §6) | rewritten | 0 | 1 | f6 |

### engine/render/backends/gl/  (3 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| opengl/blend_ff_map.h | 249 | GENERIC-PS2 | engine/render/backends/gl/blend_ff_map.{h,cpp} | verbatim (gates dropped) | 2→2/0/0/0/0 | 1 | f6 |
| opengl/gl_state_cache.h | 917 | GENERIC-PS2 | engine/render/backends/gl/gl_state_cache.{h,cpp} as GlDevice member; lifetime census → core/diag | rewritten | 5→5/0/0/0/0 | 3 | f6 |
| vconv.h | 245 | GENERIC-PS2 | engine/render/backends/gl/vertex_convert.{h,cpp} | moves | 0 | 0 | f6 |

### engine/sif/  (7 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| budget_math.h | 156 | GAME (row E) | engine/ee/timing/{budget_clock,phase_classifier} + TitleProfile.quirks.budget | S | 0 | 1 | f5 |
| sce_hle/libkernl.cpp | 765 | G | engine/ee/hle/kernl_{file_io,sif_rpc,boot,timer}.* (sceSif bodies → sif/hle) | split | 0 | 3 | f7 |
| sce_hle/netcnfif.cpp | 23 | G | engine/sif/hle/netcnfif_bindings.cpp | move (verify reachability) | 0 | 0 | f7 |
| sif.cpp | 19 | GENERIC-PS2 | merged into engine/sif/hle/rpc_router::CompletePending | M | 0 | 1 | f5 |
| sif.h | 31 | GENERIC-PS2 | sif/sif_state | moves verbatim | 0 | 0 | f2 |
| sif_hle.cpp | 884 | MIXED | sif/hle/sceSif + ee/timing boundary_protocol + ee/hle/boot_env + workaround ledger (0x181490) | split | 0 | 5 | f0 |
| syscalls.cpp | 2,496 | GENERIC-PS2 | ee/hle/{syscalls, dyn_interp, rpc_callback_queue} + sif/hle/sceSif_regs | split | 0 | 5 | f2 |

### engine/sound/  (13 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| gui/sound_section.cpp | 51 | G | app/gui/sound_section.cpp | verbatim | 0 | 0 | f7 |
| gui/sound_section.h | 15 | G | app/gui/sound_section.h | verbatim | 0 | 0 | f7 |
| sce_hle/libsdr.cpp | 438 | G | engine/sound/hle/sdr_remote.* + sd_block_trans.* | split | 3 → 3 NORM | 2 | f7 |
| snd_bank_ledger.h | 49 | MIXED | with snd_bank_ledger.cpp | split | 0 | (with .cpp) | f3 |
| sound/mixer.cpp | 85 | G | engine/sound/mixer.cpp + platform/sdl/audio_sink.cpp | split | 0 | 0 | f7 |
| sound/mixer.h | 29 | G | engine/sound/mixer.h + platform/sdl/audio_sink.h | split | 0 | 0 | f7 |
| sound/sound_core.cpp | 1,168 | M | engine/sound/sound_core.cpp + fmv_stream.* + sound_state_codec.* | split | 5 → 1 NORM / 2 SETTING / 1 DIAG / 1 TEST | 4 | f7 |
| sound/sound_core.h | 171 | M | engine/sound/sound_core.h | move (D4 splits below) | 0 | 0 | f7 |
| sound/vag_decoder.cpp | 56 | G | engine/sound/vag_decoder.cpp | verbatim | 0 | 0 | f7 |
| sound/vag_decoder.h | 56 | G | engine/sound/vag_decoder.h | verbatim | 0 | 0 | f7 |
| sound/voice.cpp | 245 | M | engine/sound/voice.cpp | verbatim | 0 | 0 | f7 |
| sound/voice.h | 199 | M | engine/sound/voice.h | verbatim | 0 | 0 | f7 |
| tests/ 5.11 sound (5) | ~1,600 | G | tests/sound/ | V | 0 | 0 | f8 |

### engine/tools/diagnostics/  (1 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| tools/ 4.3 diagnostics (21) | ~3,600 | G | engine/tools/diagnostics/ | V | 0 | 0 | f8 |

### engine/tools/ee_analyze/  (5 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| EEAnalyze/DataFlowEngine.h | 31 | G | engine/tools/EEAnalyze/ (then prune) | V | 0 | 1 | f8 |
| EEAnalyze/Function.h | 47 | G | engine/tools/EEAnalyze/function.h | V | 0 | 0 | f8 |
| EEAnalyze/analyze.{h,cpp} | 332 | G | engine/tools/EEAnalyze/ | V (Ghidra parser D Phase 2) | 0 | 0 | f8 |
| EEAnalyze/main.cpp | 165 | G | engine/tools/EEAnalyze/ (+build target) | V | 0 | 1 | f8 |
| EEAnalyze/register_state.h | 82 | G | engine/tools/EEAnalyze/ | V | 0 | 0 | f8 |

### engine/tools/iop_recompiler/  (4 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| iop_recompiler/ExportIopFunctions.java | 73 | G | engine/tools/iop_recompiler/ | V | 0 | 0 | f8 |
| iop_recompiler/iop_recompiler.cpp | 1,010 | M | engine/tools/iop_recompiler/ (seams→title data) | V−2 injections | 0/0/0/0/1 (PS2_STRMQ) | 2 | f8 |
| iop_recompiler/iop_recompiler.h | 59 | G | engine/tools/iop_recompiler/ | V | 0 | 0 | f8 |
| iop_recompiler/main.cpp | 599 | M | engine/tools/iop_recompiler/ | V (stub range derived) | 0 | 1 | f8 |

### engine/tools/recompiler/  (16 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| corpus/audit.{h,cpp} | 253 | G | …/corpus/audit.* | V | 0 | 0 | f8 |
| corpus/recorder_gen.{h,cpp} | 114 | G | …/corpus/recorder_gen.* | V | 0/0/0/1/0 (PS2_RECOMP_CORPUS) | 0 | f8 |
| discovery/discovery_types.h | 133 | G | …/discovery/discovery_types.h | V | 0 | 0 | f8 |
| discovery/functions.{h,cpp} | 630 | G | …/discovery/functions.* | V | 0 | 0 | f8 |
| discovery/jumptables.{h,cpp} | 610 | G | …/discovery/jumptables.* | V | 0 | 0 | f8 |
| discovery/leaders.{h,cpp} | 122 | G | …/discovery/leaders.* | V | 0 | 0 | f8 |
| discovery/report.{h,cpp} | 111 | G | …/discovery/report.* | V | 0 | 0 | f8 |
| discovery/rv2_decode.h | 143 | G | …/discovery/rv2_decode.h | V | 0 | 0 | f8 |
| elf/elf_image.{h,cpp} | 78 | G | …/loader/elf_image.* | V | 0 | 0 | f8 |
| emit/driver_v2.{h,cpp} | 311 | G | …/emit/emission_model_builder.* | S | 0 | 0 | f8 |
| emit/v2_emission.h | 52 | G | …/emit/emission_config.h | S (pin/alias fields D) | 0 | 0 | f8 |
| recompiler_tool/main_v2.cpp | 572 | G | engine/tools/recompiler/main.cpp | S (compat branches D) | 0 | 1 | f8 |
| recompiler_tool/recompiler.cpp | 25,813 | M | engine/tools/recompiler/{ir,emit}/ (5 class pairs) | S (+D Phase 2 freight) | 0/0/5/0/0 (emitted gates) | 6 | f8 |
| recompiler_tool/recompiler.h | 155 | G | dissolves into the 5 new headers | S | 0 | 2 | f8 |
| tools/ 4.1 recompiler companions (4) | ~450 | G | engine/tools/recompiler/ | V | 0 | 0 | f8 |
| translate/control_flow_v2.cpp | 105 | G | …/emit/indirect_dispatch.* | S (alias emitter D) | 0 | 0 | f8 |

### engine/tools/sce_lib_finder/  (2 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| sce_hle/generate_bindings.py | 139 | G | engine/tools/sce_lib_finder/ | move | 0 | 0 | f7 |
| tools/ 4.2 sce_lib_finder (8) | ~1,800 | G | engine/tools/sce_lib_finder/ | V (output→games/gen) | 0 | 0 | f8 |

### engine/vif/  (4 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| dmac.cpp | 3,034 | GENERIC-PS2 | engine/dmac/{dmac,chain_walker,deferred_vif1,handlers}.* | verbatim+gate purge | 4 (0/0/2/0/2) | 5 | f4 |
| tests/ 5.6 VIF/GIF/MTVU (2+tsan) | ~4,000 | G | tests/vif/, tests/gif/ | V | 0/0/0/6/0 | 0 | f8 |
| vif.cpp | 3,609 | GENERIC-PS2 | engine/vif/{vif, vif_unpack, vif_fast_span, vif_mtvu_seam} | split | 6→1N/0S/3D/0T/2X | 5 | f2 |
| vif.h | 349 | GENERIC-PS2 | engine/vif/{vif,vif_regs,unpack} | S | 2 → 2/0/0/0/0 | 3 | f5 |

### engine/vu/  (4 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| cpu_state.h | 299 | GENERIC-PS2 | ee/cpu/cpu_state.h (lint-exempt); ps2-float → vu/vu_float | verbatim (+helper extraction) | 0 | 1 | f0 |
| tests/ 5.5 VU (16 incl. 3 .inc) | ~6,400 | G/M | tests/vu/ (+ title vu1_library gates); AOT 3 D Ph1 | V / D | 0/0/0/10+/AOT | .inc fixtures→.bin | f8 |
| vu_decode_slot.h | 648 | GENERIC-PS2 | vu/decode.h | verbatim | 0 | 1 | f0 |
| vu_span_classify.h | 195 | GENERIC-PS2 | vu/span_classify.h/.cpp | verbatim | 0 | 1 | f0 |

### engine/vu/hle/  (3 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| hle/vu1_hle.cpp | 91 | M | engine/vu/hle/vu1_registry.cpp + image_analyzer.cpp (signature-param) | split | 0 | 2 | f7 |
| hle/vu1_hle_runtime.cpp | 437 | G | engine/vu/hle/runtime.cpp + kick_lowering.* ; verifier → tests | split | 6 → 4 NORM / 1 TEST / 1 DIAG | 5 | f7 |
| hle/vu1_hle_runtime.h | 23 | G | engine/vu/hle/runtime.h | verbatim (gate dies) | 0 | 1 | f7 |

### engine/vu/tiers/  (6 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| runahead_guard.h | 180 | GENERIC-PS2 | vu/tiers/runahead_guard | verbatim → class | 3→2/1/0/0/0 | 2 | f0 |
| vu.cpp | 12,475 | GENERIC-PS2 | engine/vu/{vu_unit,flag_pipeline,stall_model,xgkick,vu_float} + vu/tiers/{interp,cached} + core/diag/vutrace | split | 57 → 11/2/11/1/32 | 10 | f1 |
| vu.h | 1,171 | GENERIC | vu/{vu1_state,xgkick,flags} + vu/tiers/{interp,cached}; AOT surfaces deleted | split | 0 direct (latched members; rows in vu.cpp shard) | 3 | f3 |
| vu1_async.cpp | 1,908 | GENERIC-PS2 | engine/vu/tiers/{mtvu_worker,mtvu_backpressure,mtvu_sync}.* | split | 12 (7/1/0/0/4) | 5 | f4 |
| vu1_async.h | 358 | GENERIC-PS2 | vu/tiers/{mtvu_worker,mtvu_sync} | split | 0 | 3 | f0 |
| vu_fast_exec.h | 1,833 | GENERIC-PS2 | vu/tiers/cached_tier.cpp + vu/vu_float | split (header dissolves post-NOAOT) | 4 → 1/1/0/0/2 | 3 | f1 |

### games/<title>/  (16 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| gs_class_renderer.h | 585 | MIXED | gs/hle/classrdr/{class_renderer, class_registry, class_classifier} + games/…/renderer_profile | split | 13→1N/0S/2D/1T/9X (10 per-class tokens→X, test hooks→T) | 4 | f2 |
| hle/loader_sync_hle.h | 224 | M | engine/cdvd/hle/loader.* + games/<t>/hle/loader_sync.cpp | split | 1 → 1 NORM | 3 | f7 |
| hle/vu1_hle.h | 108 | M | engine/vu/hle/contract.h + vu1_registry.h + image_analyzer.h; family consts → games/<t>/vu1_library | split | 0 | 1 | f7 |
| hle/vu1_hle_programs.cpp | 6,207 | GS-on-M | helpers → engine/vu/vu_float.*; 8 family files + registration table → games/<t>/vu1_library/ | split (one family per file) | 3 → 2 NORM / 1 DELETE | 6 | f7 |
| iop_loader.cpp | 451 | GAME | engine/iop/module_loader + games/…/iop/stream_irx plugin | split | 0 | 3 | f1 |
| iop_stubs.cpp | 2,422 | MIXED | iop/hle/{thbase, thsemap, timrman, libsd_stubs, cdvdman_stubs, deferred_events, guest_monitor} + games/…/iop/stream_irx/module_layout | split | 11→4N/0S/2D/0T/5X | 5 | f2 |
| iop_stubs.h | 192 | MIXED (row G) | engine/iop/hle/ per-module stubs + games/…/iop/stream_irx/ glue | S | 0 | 3 | f5 |
| opengl/opengl_backend.cpp | 25,598 | MIXED | split per REGION MAP: render/backends/gl/{gl_device,gl_shader_library,gl_uniform_cache,gl_draw_executor,gl_vertex_stream,gl_texture_pool} + gs/hle/texsrc/{texture_source,clut_source,decode_cache} + gs/hle/targets/{render_target,target_cache,readback_engine,target_refresher} + gs/vram/{vram_image,page_record} + gs/hle/present/{scanout_composer,present_policy} + gs/hle/interp/frame_interpolator; VRAM pins → games/…/renderer_profile.cpp; compute arm + probes deleted | split + rewritten | 72→14/0/20/3/35 | 11 | f6 |
| savestate.cpp | 2,450 | MIXED (row I) | engine/core/savestate/{savestate_registry,quiescence,save_request} + games/…/savestate/ providers | S | 5 → 0/1/3/0/1 | 3 | f5 |
| sce_hle/sce_hle_bindings.inc | 307 | GS (gen) | games/<t>/gen/ (build dir) | regenerate; checked-in copy deleted | 0 | 1 | f7 |
| sif_rpc_handlers.cpp | 6,131 | MIXED | sif/hle + pad/mc/cdvd/sound/iop hle + cdvd/hle/loader + games/…/iop/stream_irx | split | 3→1N/0S/1D/0T/1X | 6 | f2 |
| sif_rpc_handlers.h | 89 | MIXED | sif/hle/rpc_router + per-device hle + games/…/iop/stream_irx | split | 0 | 3 | f0 |
| snd_bank_ledger.cpp | 148 | MIXED | sound/bank_ledger (mechanism) + games/…/iop/stream_irx feed contract (IOP table data) | split | 0 | 3 | f3 |
| timer_hle.cpp | 67 | GAME | per §1.6 row E: mechanism ee/timing, params games/…/hle/budget.cpp (MIGRATION §1.4 row imprecise; errata) | rewritten | 0 | 2 | f3 |
| vu_function_registry.cpp | 214 | MIXED | engine/vu/hle/registry.* (mechanism) + games/…/vu0_programs.cpp (table) | split | 0 | 2 | f4 |
| vu_function_registry.h | 80 | MIXED | merged into engine/vu/hle/registry + games/…/vu1_library data | M | 0 | 2 | f5 |

### tests/  (8 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| hle/drawlist_reader.h | 115 | G/T | tests/lib/gsdl_reader.* | move | 0 | 0 | f7 |
| input/test_input_mapper.cpp | 645 | T | tests/input/ | verbatim | 0 | 0 | f7 |
| tests/ 5.10 savestate (4) | ~1,200 | G | tests/core_savestate/ | V | 0 | 0 | f8 |
| tests/ 5.13 app/platform/misc (6) | ~1,400 | G/M | tests/{config,mc,present,app} | V→retarget | 0 | 0 | f8 |
| tests/ 5.14 support stubs (4) | ~131 | G | tests/support/ | V (shrink as libs land) | 0 | 1 (monolith-link symptom) | f8 |
| tests/ 5.8 recompiler suites (50+txt) | ~13,600 | G | tests/recompiler/{verify,semantics}; legacy arms D Ph2 | V / D | 0/0/2/2/0 | 0 | f8 |
| tests/CMakeLists.txt | 3,173 | G | tests/ façade + per-module regs | R | ~17 distinct pins → T | 1 | f8 |
| tools/ 4.5 loader oracle (1) | ~150 | M | tests/ | V | 0 | 0 | f8 |

### verify/  (2 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| scripts/run_bench.sh | 124 | G | verify/bench/ | V→thin (--profile bench) | 0/1S/0/0/0 | 0 | f8 |
| tools/ 4.4 verify inputs (5) | ~700 | G/M | verify/ (+title anchor lists→games) | V | 0/0/0/all/0 | 1 | f8 |

### build (CMake / scripts / config)  (4 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| CMakeLists.txt (root) | 647 | M | per-directory CMake tree | R | 5N/3S/1D/0/4DEL (build opts) | 4 | f8 |
| CMakePresets.json | 63 | G | kept, re-pointed | V | 0 | 0 | f8 |
| host_app/CMakeLists.txt | 624 | M | app/ + backends/ + subsystem CMake | R | (in root's counts) | 2 | f8 |
| host_app/cmake/gen_build_id.cmake | 22 | G | cmake/ helper (app/) | V | 0 | 0 | f8 |

### DELETED / RETIRED (no surviving destination)  (112 source rows)

| source file | lines | class | destination (full) | shape | flags (N/S/D/T/X) | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| **recompiler_tool subtotal** | **31,381** |  |  |  |  |  | f8 |
| EEAnalyze/{unused_function,jump_table_analysis,analyze_test,DataFlowEngine}.cpp | 1,449 | G | — | D Phase 1 (dead) | 0 | 0 | f8 |
| anim_pose_hle.cpp | 308 | GAME-SPECIFIC | — (HOOK §3.2 Phase-2 retirement) | deleted | 1→0N/0S/0D/0T/1X | 1 | f2 |
| anim_pose_hle_bodies.inc | 2,027 | GAME | — retired Phase 2 (HOOK §3.2) | delete | 0 | 1 | f4 |
| bullseye_trace.cpp | 132 | GAME-SPECIFIC (diag) | — | deleted | 1→0N/0S/0D/0T/1X | 1 | f2 |
| bullseye_trace.h | 86 | GAME diag | — (concluded session) | deleted | 0 | 1 | f3 |
| cuelag_probe.h | 200 | GENERIC | — (concluded probe) | deleted | 2 → 0/0/0/0/2 | 1 | f3 |
| gs_glbatch.h | 121 | GENERIC-PS2 | — (producer RUNDRAW is the survivor) | deleted | 1→0N/0S/0D/0T/1X | 1 | f2 |
| gui/vu_microcode_section.cpp | 488 | G/D | — | **deleted** (AOT tier retired) | 4 → 4 DELETE | 1 | f7 |
| gui/vu_microcode_section.h | 22 | G/D | — | **deleted** (AOT tier retired) | 0 | 0 | f7 |
| hle_heap.cpp | 3,995 | MIXED | engine/cdvd/{iso_walker,read_engine,latency_model}.* + cdvd/hle/loader.* + mc/hle + games/…/{hle/loader_sync,iop/stream_irx,data/archive_table,savestate}; allocator HLE quarantined→retired | split | 7 (5/1/0/0/1) | 7 | f4 |
| host_app/fun_*_body.inc (222, grouped) | 159,811 | GS | — (adjudication OLD arms) | RET Phase 2 | 0 | 1 (is one class) | f8 |
| host_app/tests/swizzle/* (2) | 619 | G | — | D Phase 1 | 0 | 0 | f8 |
| inject_fun_1017c8_helpers.{h,cpp} | 233 | GS | — | D Phase 2 | 0 | 1 (is one) | f8 |
| inject_logging.sh (root) | 74 | GS | — | D Phase 1 | 0 | 1 (is one) | f8 |
| legacy/recomp_overrides_config.h | 151 | M | — | D Phase 2 | 0 | 1 (is one) | f8 |
| loader_prof.h | 347 | GAME-SPECIFIC (diag) | — | deleted | 1→0N/0S/0D/0T/1X | 1 | f2 |
| loop_hist.h | 149 | GENERIC-PS2 | — concluded one-off (§1.1) | delete | 1 (0/0/0/0/1) | 1 | f4 |
| main_old.cpp | 103 | dead scaffold | — | D | 0 | 1 | f5 |
| manual_switch_dispatchers.cpp | 2,303 | GAME | retired Phase 2 (per-address adjudication; mechanism already in ee/cpu seams) | deleted | 2 → 0/0/0/0/2 | 5 | f3 |
| memory_test.cpp | 45 | dead test | — | deleted | 0 | 0 | f3 |
| mipsrc_diag.cpp | 277 | GENERIC (one-off) | — (mechanism idea → core/diag probe kit) | D | 4 → 0/0/0/0/4 | 1 | f5 |
| mipsrc_diag.h | 52 | GENERIC (one-off diag) | — | delete (§1.1 named row) | 0 | 1 | f1 |
| opengl/compute_data_layouts.h | 127 | GENERIC (dead) | DELETE (MIGRATION §1.1 compute-raster row) | deleted | 0 | 1 | f6 |
| pacebrk.h | 196 | GENERIC (one-off) | — (wait-class taxonomy → threading.md) | D | 1 → 0/0/0/0/1 | 2 | f5 |
| platform/.DS_Store | — | junk | — | **deleted** (gitignore) | 0 | 0 | f7 |
| platform/macos/fiber_macos.cpp | 149 | D | — | **deleted** | 0 | 0 | f7 |
| platform/platform.h | 28 | D | — | **deleted** (old-fiber consumer never built) | 0 | 0 | f7 |
| platform/win32/fiber_win32.cpp | 58 | D | — | **deleted** | 0 | 0 | f7 |
| ps2_scheduler_old_fiber.cpp | 2,169 | MIXED (DEAD) | — | delete (Phase 1) | 0 | 0 | f1 |
| ps2_scheduler_old_fiber.h | 309 | GENERIC (dead) | — (superseded by ps2_threading; 0 refs) | D | 0 | 0 | f5 |
| recomp_overrides.ini | 33 | GS | — | D Phase 2 | 0/0/0/0/1 | 1 (is one) | f8 |
| recomp_test.cpp | 24 | DEAD | — | deleted | 0 | 0 | f2 |
| recomp_test.h | 9 | GENERIC-PS2 | — dead scaffold (§1.1) | delete | 0 | 0 | f4 |
| recompiler_tool/main.cpp | 325 | GS | — | D Phase 2 (snippet mode rehomed) | 0 | 2 | f8 |
| runtime.cpp | 0 | DEAD | — | delete (stale §1.4 row) | 0 | 0 | f1 |
| runtime.h | 7 | GENERIC-PS2 | — | delete (dead orphan) | 0 | 0 | f0 |
| s8_ctxlatch.h | 404 | MIXED | workaround retired (default OFF); rule → gif/arbiter order; trigger data → renderer_profile if relanded | deleted (purge) | 2 → 0/0/0/0/2 | 3 | f3 |
| scripts/check_vu_aot_optimized.sh | 74 | G | — | D Phase 1 (NOAOT) | 0 | 1 | f8 |
| scripts/run_play.ps1 | 72 | M | — | D with run_play.sh | (mirror) | 1 | f8 |
| scripts/run_play.sh | 189 | M | — (CANON→Config defaults) | D end of Phase 4 | 8N/8S/5D/4T/1DEL (set, not read) | 3 | f8 |
| scripts/w14_make_backstop_cases.py | 125 | G | — | D Phase 1 (NOAOT) | 0 | 0 | f8 |
| sema.h | 21 | GENERIC-PS2 | — | delete (dead payload) | 0 | 1 | f0 |
| shaders/blend.glsl | 289 | GENERIC (dead) | DELETE (dither reference first) | deleted | 0 | 0 | f6 |
| shaders/cs_rasterize_line.glsl | 466 | GENERIC (dead) | DELETE | deleted | 0 | 0 | f6 |
| shaders/cs_rasterize_sprite.glsl | 475 | GENERIC (dead) | DELETE | deleted | 0 | 0 | f6 |
| shaders/cs_rasterize_tri.glsl | 567 | GENERIC (dead) | DELETE (compute-raster row) | deleted | 0 | 0 | f6 |
| shaders/cs_scanout.glsl | 97 | GENERIC (dead) | DELETE | deleted | 0 | 0 | f6 |
| shaders/pixel_format.glsl | 262 | GENERIC (dead) | DELETE | deleted | 0 | 1 | f6 |
| shaders/ps2_frag.glsl | 184 | GENERIC-PS2 | superseded by single-source GlShaderLibrary (UNIFPACK form authored) | deleted/absorbed | 0 | 1 | f6 |
| shaders/ps2_vert.glsl | 62 | GENERIC-PS2 | superseded by single-source GlShaderLibrary (build-embed or delete) | deleted/absorbed | 0 | 1 | f6 |
| shaders/swizzle.glsl | 250 | GENERIC (dead) | DELETE (mirror of gs/format tables) | deleted | 0 | 1 | f6 |
| shaders/texture_sample.glsl | 317 | GENERIC (dead) | DELETE (TEXA reference first) | deleted | 0 | 0 | f6 |
| shaders/tfx.glsl | 57 | GENERIC (dead) | DELETE | deleted | 0 | 0 | f6 |
| sif_hle.h | 56 | MIXED | sif/hle/rpc_router + ee/hle/boot_env + HOOK §3.5 quarantine | split | 0 | 3 | f1 |
| test_alpha_blend_bug_condition.cpp | 453 | GENERIC-PS2 | — | delete (dead) | 0 | 0 | f0 |
| test_alpha_blend_preservation.cpp | 745 | dead test | — | deleted | 0 | — | f3 |
| test_black_screen_scanout.cpp | 918 | dead test | — | deleted | 0 | — | f3 |
| test_boot_bug_condition.cpp | 453 | dead test | — | deleted | 0 | — | f3 |
| test_boot_preservation.cpp | 862 | GENERIC (dead) | — (invariants → gs/state named tests) | D | 0 | 0 | f5 |
| test_bug_conditions.cpp | 789 | GENERIC test (dead) | — | delete (Phase 1) | 0 | 1 | f1 |
| test_comprehensive_rendering.cpp | 581 | GENERIC test (dead) | — | delete (Phase 1) | 0 | 1 | f1 |
| test_fbo_persistence_bug_condition.cpp | 637 | GENERIC test (dead) | — | delete (Phase 1) | 0 | 1 | f1 |
| test_fbo_persistence_preservation.cpp | 692 | GENERIC (dead) | — (→ gs/hle/targets tests) | D | 0 | 0 | f5 |
| test_preservation.cpp | 1,246 | dead test | — | deleted (§1.1, 0 CMake refs) | 0 | 1 | f3 |
| test_prim_change_bug_condition.cpp | 498 | dead test | — | deleted | 0 | — | f3 |
| test_prim_change_preservation.cpp | 845 | GENERIC-PS2 | — dead root test (§1.1) | delete | 0 | 0 | f4 |
| test_rendering_architecture.cpp | 1,614 | GENERIC (dead) | — (properties re-homed as gs/hle/present unit tests) | D | 0 | 1 | f5 |
| test_rendering_bug_condition.cpp | 638 | GAME (dead) | — (contract → SdkBindingManifest completeness check) | D | 0 | 1 | f5 |
| test_rendering_preservation.cpp | 572 | GENERIC-PS2 | — dead root test (§1.1) | delete | 0 | 0 | f4 |
| test_scanout_preservation.cpp | 1,058 | GENERIC test (dead) | — | delete (Phase 1) | 0 | 1 | f1 |
| test_texture_shuffle.cpp | 519 | GENERIC test (dead) | — | delete (Phase 1) | 0 | 1 | f1 |
| test_vblank_bug_condition.cpp | 676 | DEAD | — | deleted | 0 | 1 | f2 |
| test_vblank_preservation.cpp | 688 | GENERIC test (dead) | — | delete (Phase 1) | 0 | 1 | f1 |
| texture_cache.cpp | 116 | GENERIC-PS2 | — dead (zero external includers) | delete | 0 | 1 | f4 |
| texture_cache.h | 52 | DEAD | — | deleted | 0 | 0 | f2 |
| tools/ 4.6 concluded one-offs (35) | ~2,300 | GS-heavy | — | D Phase 1 (gen_merge Phase 2) | 0 | many (offline probe class) | f8 |
| vu_aot.ini | 21 | G | — (capture keys→Config) | D Phase 1 | 0/2S/0/0/1 | 0 | f8 |
| vu_aot_config.h | 266 | GENERIC tooling | — | delete (NOAOT tier) | 0 | 1 | f1 |
| vu_aot_gen.cpp | 1,470 | G | — | D Phase 1 (NOAOT) | 0 | 0 | f8 |
| vu_aot_programs.cpp | 135 | GENERIC over GAME payload | — (NOAOT: tier deleted) | D | 4 → 0/0/0/0/4 | 3 | f5 |
| vu_aot_programs_1/9/15/20/26/31.cpp | 6×17 | GENERIC-PS2 | — | delete (NOAOT tier) | 0 | 1 | f0 |
| vu_aot_programs_10.cpp | 17 | 〃 | 〃 | delete | 0 | (shared) | f4 |
| vu_aot_programs_11.cpp | 17 | scaffolding (AOT) | — (NOAOT) | D | 0 | 2 | f5 |
| vu_aot_programs_12.cpp | 17 | 〃 | — | deleted | 0 | 〃 | f3 |
| vu_aot_programs_13.cpp | 17 | GAME (generated shell) | — | delete (NOAOT) | 0 | 1 | f1 |
| vu_aot_programs_16.cpp | 17 | 〃 | 〃 | delete | 0 | (shared) | f4 |
| vu_aot_programs_17.cpp | 17 | scaffolding (AOT) | — (NOAOT) | D | 0 | 2 | f5 |
| vu_aot_programs_18.cpp | 17 | 〃 | — | deleted | 0 | 〃 | f3 |
| vu_aot_programs_19.cpp | 17 | GAME (generated shell) | — | delete (NOAOT) | 0 | 1 | f1 |
| vu_aot_programs_21.cpp | 17 | 〃 | 〃 | delete | 0 | (shared) | f4 |
| vu_aot_programs_22.cpp | 17 | scaffolding (AOT) | — (NOAOT) | D | 0 | 2 | f5 |
| vu_aot_programs_23.cpp | 17 | 〃 | — | deleted | 0 | 〃 | f3 |
| vu_aot_programs_24.cpp | 17 | GAME (generated shell) | — | delete (NOAOT) | 0 | 1 | f1 |
| vu_aot_programs_27.cpp | 17 | 〃 | 〃 | delete | 0 | (shared) | f4 |
| vu_aot_programs_28.cpp | 17 | scaffolding (AOT) | — (NOAOT) | D | 0 | 2 | f5 |
| vu_aot_programs_29.cpp | 17 | 〃 | — | deleted | 0 | 〃 | f3 |
| vu_aot_programs_3.cpp | 17 | GAME (generated shell) | — | delete (NOAOT) | 0 | 1 | f1 |
| vu_aot_programs_4.cpp | 17 | GENERIC shell/GAME payload | — deleted with AOT tier | delete | 0 | 1 | f4 |
| vu_aot_programs_5.cpp | 17 | scaffolding (AOT) | — (NOAOT) | D | 0 | 2 | f5 |
| vu_aot_programs_6.cpp | 17 | GENERIC (retired tier) | — | deleted (NOAOT) | 0 | 1 | f3 |
| vu_aot_programs_7.cpp | 17 | GAME (generated shell) | — | delete (NOAOT) | 0 | 1 | f1 |
| vu_aot_programs_{0,2,8,14,25,30}.cpp | 17×6 | GAME payload aggregators | — (NOAOT) | deleted | 0 (tier env rows die with the tier) | 0 | f2 |
| vu_aot_registry.h | 70 | GENERIC-PS2 | — deleted with AOT tier (NOAOT) | delete | 0 (+4d→DELETE) | 1 | f4 |
| vu_aot_status.h | 13 | GENERIC (retired tier) | — | deleted | 0 | 1 | f3 |
| vulkan/shaders/ps2_frag.glsl | 45 | GENERIC (stale) | DELETE | deleted | 0 | 0 | f6 |
| vulkan/shaders/ps2_vert.glsl | 43 | GENERIC (stale) | DELETE | deleted | 0 | 0 | f6 |
| vulkan/shaders/scanout_frag.glsl | 15 | GENERIC (stale) | DELETE | deleted | 0 | 0 | f6 |
| vulkan/shaders/scanout_vert.glsl | 28 | GENERIC (stale) | DELETE | deleted | 0 | 0 | f6 |
| vulkan/vulkan_backend.cpp | 1,836 | GENERIC (stale) | DELETE (MIGRATION §1.1; Vulkan rebuilt against IRenderBackend later) | deleted | 0 | 0 | f6 |
| vulkan/vulkan_backend.h | 198 | GENERIC (stale) | DELETE | deleted | 0 | 0 | f6 |
| w10_eefeed.h | 55 | GENERIC-PS2 | — (purge-dissolve: arms become dmac/gif code) | D | 1 → 1/0/0/0/0 | 1 | f5 |

### Consolidator-added rows (A1 coverage completion)

| source file | lines | class | destination (full) | shape | flags | hacks | shard |
|---|---:|---|---|---|---|---:|---|
| host_app/common/Pcsx2Defs.h | 165 | GENERIC (vendored PCSX2) | engine/ee/cpu/ beside cpu_state.h (vendor-exempt, lint-exempt) | verbatim | 0 | 0 | cons |
| host_app/common/Pcsx2Types.h | 114 | GENERIC (vendored PCSX2) | engine/ee/cpu/ (rider on f0 cpu_state.h entry) | verbatim | 0 | 0 | f0/cons |
| host_app/cmake_install.cmake | n/a | build artifact (leaked) | — untrack/DELETE Phase 1 (A1 finding #13) | delete | 0 | 0 | cons |
| host_app/third_party/imgui/* (10 files) | 56,339 | vendored third-party (A1-exempt) | app/gui/third_party/ (vendored as-is) | move | 0 | 0 | f7/cons |

---

## Reverse index — what each NEW module is assembled from

One row per new-tree directory (or named class file where a single class is fed by
many sources). "†" = source contributes only after purge/root-fix (content dissolves
in rather than moving). Sources are today's paths under host_app/ unless prefixed.

### engine/core/
- `core/config/` ← main.cpp (loader + CLI), display_settings.{h,cpp} + display.ini ([video]), run_play.sh CANON (defaults), vu_aot.ini capture keys†, bug_report.cpp parsing†
- `core/boot` (BootSequence) ← main.cpp CpuThreadFunc boot ladder
- `core/log/` ← ring_log.{h,cpp} (ring + sinks), ring_log_format.h, ring_tags.h, ring_log_decode.h; dissolving in†: main.cpp 4 log layers, hw_verbose.h, debug_budget.h, gs.cpp s_scanoutLog, gs_state m4a latches
- `core/hash.h` ← render.h XXH64 (vendored) + vu_hash_fnv.h FNV64 (collision #2: coordinated merge)
- `core/threads/cmd_ring.h` ← sound/ring.h
- `core/diag/` ← bench.h (perf_bench), bench_report.h, perf_profiler.h (profiler half), call_census.{h,cpp}, stick_trace.h, gs_vram_census.h, timeline.h (frame_timeline, ee_stall_census), loadspan.h, snd_census.h, mid_census.h, artdet_flicker.{h,cpp}, hle/drawlist_record.{h,cpp} (recorder), hle/loader_oracle.h (loader_trace), input/input_latency.h, vu_trace_case.h, graphics/gs_c3_counters.h, memory.cpp watch mechanism†, pad_input.cpp (input_script, stick_trace)
- `core/triage/` ← bug_report.{h,cpp} (TriageBundler, BundleManifest), slowspot.{h,cpp} + slowspot_detector.h (PerfMonitor, window_median), slowspot_paths.h†, perf_profiler.h (perf_monitor half), ring_log.cpp (bundle feed), artdet_flicker (spike path), savestate.cpp (save_request)
- `core/savestate/` ← savestate.{h,cpp} (registry, quiescence, save_request, savestate_host), savestate_format.{h,cpp} (frozen format), savestate_writer.{h,cpp} (capture_writer)

### engine/ee/
- `ee/cpu/` ← cpu_state.h (+ common/Pcsx2Types.h, Pcsx2Defs.h riders), recomp_seams.cpp, main.cpp init_cpu_context
- `ee/mem/` ← memory.{h,cpp} → address_map, mmio_router, fastmem, elf_loader (TREE-GAP #5); `ee/mem/workarounds/meta_arena` quarantine ← hle_meta_arena.{h,cpp} + memory.cpp KEY-REDIRECT (both retire at root fixes)
- `ee/hle/` ← syscalls.{h,cpp} (syscalls, dyn_interp, rpc_callback_queue), ps2_syscall_wrappers.{h,cpp}†, ps2_threading.{h,cpp} (threads, semaphores, ThreadManager), libc_hle.{h,cpp}, sce_hle/sce_hle_registry.cpp + sce_hle.h (sdk_binding), sce_hle/libkernl.cpp (kernl_*), sce_hle/libscf.cpp, sce_hle/libdev.cpp, ps2_argv.h (boot_env), main.cpp S8-ARGV writer, sif_hle (boot env half)
- `ee/timing/` ← time_domain.h (EmuDomain, regime_detector), ee_pace.h, quantum.h + gs_hle.cpp + ps2_vblank.cpp + sif_hle.cpp WaitForVblank (BoundaryProtocol), turbo.h (turbo_governor), warp.h, budget_math.h + main.cpp budget hook + timer_hle.cpp (budget_clock, phase_classifier, budget_anchor), ps2_vblank.{h,cpp} (tick_source), timeline.h WAITFIX arm†
- `ee/intc/` ← intc.{h,cpp}, ee_timers.{h,cpp} (ee_timers, timer_codec)

### engine/gs/
- `gs/state/` ← gs_state.{h,cpp} (gs_registers, vertex_kick, flush) + transfer_engine (gs/xfer), gs.{h,cpp} (gs_priv_regs/privileged), gif.cpp apply seams (ir_apply — TREE-GAP #1)
- `gs/format/` ← texture_decoder.{h,cpp} (psm_layout, clut, expand, texa, alpha, block_decode), gs_deswizzle.h†, gs_mipmap.h (mip_chain), s9_shadow.h (alpha/clut logic)
- `gs/vram/` ← gs_page_tracker.h†, gs_deposit_overlay.h†, opengl_backend.cpp VRAM regions (vram_image, page_record), alias_rect.h, idx_epoch_guard.h†
- `gs/hle/ir/` ← render.h (draw_ir, render_batch), hle/gs_draw_ir.h (+verifier), hle/gs_direct_ops.h, hle/drawlist_record.h (gsdl_format), graphics/gpu_vertex.h (VertexRec)
- `gs/hle/queue/` ← render.h (render_queue, draw_run_merge), graphics_thread.cpp (queue_consumer), ee_pace.h (queue depth inputs)
- `gs/hle/texsrc/` ← opengl_backend.cpp regions (texture_source, clut_source, decode_cache), rt_neverstale.h (ResolveRead freshness ladder), foglane_live.h†, texcache_key.h (content_key), uv_bbox.{h,cpp}, s8_inrt.h (window_in_target), idx16_lane.h†, gs_mipmap.h (mip_sampler), texture_decoder serve decls
- `gs/hle/targets/` ← opengl_backend.{h,cpp} TargetFBO decomposition (render_target, target_cache, readback_engine, target_refresher, target_identity, surface_views, coherence_state), rt_copy_tracker.h, gs_scale.h (scale_policy — TREE-GAP #7), idx16_lane.h†
- `gs/hle/present/` ← graphics_thread.cpp (presenter, composite_guard — TREE-GAP #4), present_pace.h (pacer, sample_ring, band_selector, pace_plan, cap_stepper, load_grace), quantum.h (present_decimator), viewport_math.h, gs_crtc_class.h (crtc_classifier), display_settings (view_state), opengl_backend.cpp (scanout_composer, present_policy), texture_decoder.cpp (scanout leaf), ps2_vblank.cpp (pacing seam)
- `gs/hle/interp/` ← gs_interp.h (frame_interp), opengl_backend.cpp (frame_interpolator region)
- `gs/hle/classrdr/` ← gs_class_renderer.h (class_renderer, class_registry, class_classifier), world_tristrip/sprite_hud/skinned/particles/fmv_blit/s9_composite native .inc → six *_renderer classes, s9_composite.h (FSM), shuf_snap.h (texture_shuffle_snap), sequence_recipe engine (TREE-GAP #6)
- `gs/hle/sce/` ← gs_hle.{h,cpp} (sce sync/reset HLE), hle/gs_packet.h (+packet_diff), sce_hle/libgraph.cpp (rows + gs_callbacks)

### engine/render/
- `render/backend.h` (IRenderBackend) ← graphics/graphics_backend.{h,cpp} (rewritten per ARCH §6)
- `render/backends/gl/` ← opengl_backend.cpp GL regions (gl_device, gl_shader_library, gl_uniform_cache, gl_draw_executor, gl_vertex_stream, gl_texture_pool), gl_state_cache.h, blend_ff_map.h, vconv.h (vertex_convert), gpu_vertex.h (GL layout)
- (software oracle backend: new code, replaces the deleted compute-raster arm)

### engine/vu/ · vif/ · gif/ · dmac/
- `vu/` ← vu.{h,cpp} (vu_unit/vu1_state, flag_pipeline, stall_model, xgkick, flags), vu_decode_slot.h (decode.h), vu_span_classify.h, vu_float ← cpu_state ps2-float helpers + vu1_hle_programs ChopGuard/float-bit helpers + vu_fast_exec clamp rules
- `vu/tiers/` ← vu1_async.{h,cpp} (mtvu_worker, mtvu_backpressure, mtvu_sync), vu_fast_exec.h (cached_tier), runahead_guard.h, vu.cpp tier dispatch
- `vu/hle/` ← hle/vu1_hle.{h,cpp} (contract, vu1_registry, image_analyzer), hle/vu1_hle_runtime.{h,cpp} (runtime, kick_lowering), vu_function_registry.{h,cpp} (registry merge)
- `vif/` ← vif.{h,cpp} (vif, vif_regs, vif_unpack, vif_fast_span, vif_mtvu_seam)
- `gif/` ← gif.{h,cpp} (gif_tag, gif_unit, gif_parser, gif_regs, packet_template), gif_arbiter.{h,cpp} (arbiter, drain_thread, boundary_slot — TREE-GAP #9), gif_template.h (packet_template, Phase-4 decision), s8_ctxlatch.h† (order rule only)
- `dmac/` ← dmac.{h,cpp} (dmac, dmac_channel, chain_walker, deferred_vif1, handlers), sce_hle/libdma.cpp (hle bindings)

### engine/cdvd/ · iop/ · sif/ · ipu/ · pad/ · mc/ · sound/
- `cdvd/` ← hle_heap.{h,cpp} (iso_walker/iso_fs, read_engine, latency_model, hle/loader), cdvd_stream.{h,cpp} (stream_reader + hle bindings), budget_lift.h (delivery_budget — TREE-GAP #3), ps2_vblank.h (delivery_meter — TREE-GAP #2), sce_hle/libcdvd.cpp (libcdvd_bindings), hle/loader_sync_hle.h (hle/loader half)
- `iop/` ← iop_stubs.{h,cpp} (per-module hle: thbase, thsemap, timrman, libsd_stubs, cdvdman_stubs, deferred_events, guest_monitor), iop_memory.h, iop_sysmem_heap.h, iop_context.h, iop_loader.{h,cpp} (module_loader)
- `sif/` ← sif.{h,cpp} (sif_state + CompletePending), sif_hle.{h,cpp} (sceSif + rpc_router), sif_rpc_handlers.{h,cpp} (rpc_router + per-device dispatch), syscalls.cpp sceSifSetDma decode, sce_hle/netcnfif.cpp (verify reachability)
- `ipu/` ← ipu.{h,cpp} (ipu_device, decode, hle), mpeg_hle.{h,cpp} (mpeg_context, mpeg_api, pss_demux, ffmpeg_video_decoder, frame_writer, fmv_pacer — `mpeg_ipu` renamed, P7-MPEG-IPU-SPLIT [IDOC-A 2026-10-01]), sce_hle/libipu.cpp (libipu_api), sce_hle/libmpeg.cpp (mpeg_api)
- `pad/` ← pad_input.{h,cpp} (pad_protocol), sce_hle/libpad.cpp (bindings)
- `mc/` ← sce_hle/libmc.cpp (mc_host_store, mc_command_model), hle_heap.cpp MC half
- `sound/` ← sound/sound_core.{h,cpp} (+fmv_stream, sound_state_codec), voice.{h,cpp}, mixer.{h,cpp}, vag_decoder.{h,cpp}, snd_bank_ledger.{h,cpp} (bank_ledger), sce_hle/libsdr.cpp (hle/sdr_remote, sd_block_trans)

### engine/platform/ · app/
- `platform/` ← platform/crash_handler.h + macos/win32 crash handlers, backtrace_compat.h, proc_stats.h, win_compat.h (file_compat), thread_qos.h (threads); `platform/sdl/` (TREE-GAP #8) ← input/* runtime files (input_backend, input_mapper, kbm/gamepad backends, input_system, default_bindings.ini), sound/mixer SDL half (audio_sink)
- `app/host/` ← main.cpp (≤500-ln composition root), shutdown_policy.h + main.cpp shutdown ladder, graphics_backend.cpp (backend selection)
- `app/gui/` ← gui/gui.{h,cpp} (gui_host, imgui_backend_gl, settings_window), savestate_browser.{h,cpp} (+savestate_rows), bug_report_section.{h,cpp} (triage_section over core/triage), sound_section.{h,cpp}; `app/gui/third_party/` ← host_app/third_party/imgui (vendored)

### engine/tools/ · verify/ · tests/ · games/
- `tools/recompiler/` ← recompiler_tool v2 set (main_v2, discovery/*, elf/*, emit/*, translate/control_flow_v2, corpus/*) verbatim; recompiler.cpp → 5 one-class pairs (MipsLifter, CppEmitter, ShardWriter, DispatchTables, SnippetEmitter); tools group 4.1 companions
- `tools/iop_recompiler/` ← iop_recompiler/* (4 files; two game seams extracted to title data)
- `tools/ee_analyze/` ← EEAnalyze 6 survivors (T-G1 rename)
- `tools/sce_lib_finder/` ← tools group 4.2 (8 files) + sce_hle/generate_bindings.py
- `tools/diagnostics/` ← tools group 4.3 (21 files), ring_log_decode.h consumers
- `verify/` ← tools group 4.4 (5 harness inputs), scripts/run_bench.sh (verify/bench — TREE-GAP #14), tools group 4.5 loader-oracle checker
- `tests/` ← tests/ 158 files in 14 reconciled groups (tests/gs/{format,texsrc,targets,vram,hle,present}, tests/render/gl, tests/vu, tests/vif, tests/gif, tests/ee_timing, tests/ee_mem, tests/ee_hle, tests/iop, tests/recompiler/{verify,semantics}, tests/core_savestate, tests/sound, tests/input, tests/pad, tests/{config,mc,present,app}, tests/support) + input/test_input_mapper.cpp, input/test_pad_buffer.cpp, hle/drawlist_reader.h (tests/lib/gsdl_reader), vu1_hle_runtime verifier
- `games/<title>/` ← vu1_hle_programs.cpp 8 family files + registration table (vu1_library/, one family per file), sce_hle_bindings.inc → gen/ (build-dir, regenerated), opengl VRAM pins + class tuples + S9/FMV recipes (renderer_profile.cpp), TitleProfile rows (boot argv, elf, quirks.budget, quirks.roundSprite, scene-hook), iop/stream_irx/ (iop_loader plugin descriptor, snd_bank/snd_census tables, RPC feed contracts, module_layout, recomp_seams.toml — TREE-GAP #12), hle/loader_sync.cpp + budget.cpp, data/archive_table.* (TREE-GAP #11), vu0_programs.cpp (TREE-GAP #10), savestate providers

### DELETED / RETIRED (no destination)
86 rows: 23 dead root tests + memory_test + main_old + recomp_test.{cpp,h} +
ps2_scheduler_old_fiber.{cpp,h} + platform fiber pair + platform.h + texture_cache.{h,cpp} +
runtime.{cpp,h} + sema.h + the AOT tier (vu_aot_registry.h, vu_aot_config.h,
vu_aot_status.h, vu_aot_programs.cpp + 32 chunk TUs, vu_aot_gen.cpp, vu_aot.ini, gui
vu_microcode_section, check_vu_aot_optimized.sh, w14_make_backstop_cases.py) + concluded
probes/experiments (cuelag_probe.h, pacebrk.h, mipsrc_diag.{h,cpp}, loop_hist.h,
bullseye_trace.{h,cpp}, loader_prof.h, w10_eefeed.h†, s8_ctxlatch.h†, gs_glbatch.h,
anim_pose_hle.cpp + bodies.inc, manual_switch_dispatchers.cpp + 222 fun_*_body.inc,
compute-raster shader family + compute_data_layouts.h, vulkan/** stale backend,
shaders/ps2_vert+frag reference copies, hle_meta_arena after root-fix, legacy
recompiler arms (main.cpp, legacy/recomp_overrides_config.h + .ini,
inject_fun_1017c8_helpers, 4 dead EEAnalyze TUs), tools group 4.6 (35 one-offs),
run_play.sh/.ps1 (end of Phase 4), inject_logging.sh, host_app/tests/swizzle/,
cmake_install.cmake. Each with evidence in its shard's dead-code section.
