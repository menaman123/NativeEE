# `engine/core/diag/`

**Phase:** 1+  
**Purpose:** Diagnostics MECHANISM: probe/census registry, perf profiler, bench + report, frame cap, recorders. Whole directory compiles out of Release (PS2R_DIAGNOSTICS=OFF).

## Belongs here

- WordWatch (`word_watch.{h,cpp}`): the `--watch-word 0xADDR[,0xADDR...]` census (a CLI harness knob, never a Config key) -- installed by the composition root as `FastMem::SetWriteObserver` (Debug builds carry the one-load-and-branch check; Release compiles it out), it records every CPU store overlapping a watched guest word as `watch=<addr>:w<width>:b<boundary>:<FUN_xxxxxxxx+off>:<old>-><new>` to `<harness out dir>/watch.log` (first 20,000 lines) and `run.meta watch_words=<addr>:<hits>,...`; the writer is the first recompiled function on the host frame-record chain (dladdr). DMA/host-side stores are not seen -- a watched word that changes between boundaries without a line was written by one of those. Registry entry `watch_word`.
- DiagRegistry, PerfProfiler, Bench/BenchReport, FrameCap (--frames), DrawlistRecorder (`drawlist_recorder.{h,cpp}`: a `gs::DrawIrSink` decorator writing the DrawIR stream as a GsdlFormat v2 `.gsdl` — kName/kDraw+vertices/kJob/kFrame; upload pixels never cross the sink boundary, so the header says vertices only; the root opens `<harness out_dir>/drawlist.gsdl` whenever the harness is on — decision DP-10 (b) — registry entry `drawlist`, census `drawlist_recorded=`), later: vutrace collector, censuses, artdet detector

## Does not belong here

- game-address or scene-keyed probe SITES (HOOK §3.1: the 195 DIAGNOSTIC rows are not transplanted); anything behaviour-mutating

## Transplanted from (OLD, read-only)

- `perf_profiler.h, bench.h, bench_report.h, call_census.{h,cpp}, mid_census.h, stick_trace.h, timeline.h census half, loadspan.h, snd_census.h, gs_vram_census.h, artdet_flicker.{h,cpp}, hle/drawlist_record.{h,cpp}, vu_trace_case.h`

## Flags applied

Rows copied from `PHASE1_IMPL.md` §1A "Flags consumed" (`FLAG_LEDGER.md` outcomes; the DIAG list is
`PHASE1_PLAN.md` §3 "DIAG (19)" minus `PS2_LOG_VERBOSE`, which belongs to `core/log`):

| Flag | Outcome | Where |
|---|---|---|
| `PS2_PERF` | SETTING `diag.perf` (off) | `Config` → `PerfProfiler::SetEnabled` (replaces the 21-site announce idiom) |
| `PS2_VERBOSE_HW` | DIAG — `DiagRegistry` category; no getenv | `DiagRegistry` (body lands with the HW subsystems) |
| `PS2_CALL_CENSUS`, `PS2_CALL_CENSUS_ROOT`, `PS2_CALL_CENSUS_SLOWSEG` | DIAG — registry mechanism only | `DiagRegistry` (bodies: Phase 3 CPU) |
| `PS2_MID_CENSUS`, `PS2_MID_CENSUS_FROM` | DIAG — registry mechanism only | `DiagRegistry` (bodies: Phase 3) |
| `PS2_EESTALL_CENSUS`, `PS2_TICKWATCH` | DIAG — registry mechanism only | `DiagRegistry` (bodies: Phase 3 timeline) |
| `PS2_EULERTRACE`, `PS2_EDGEPROBE`, `PS2_PROJ_DIAG`, `PS2_SITEB_DIAG`, `PS2_POPUP_DIAG`, `PS2_SMWATCH` | DIAG — registry mechanism only | `DiagRegistry` (bodies: Phases 5–6 GS/render, if their FILEMAP row keeps them) |
| `PS2_SAVE_PROBE`, `PS2_SAVE_WATCH` | DIAG — registry mechanism only | `DiagRegistry` (bodies: `core/savestate` consumers, Phase 3) |
| `PS2_SND_BANKTRACE`, `PS2_SND_STATEHASH` | DIAG — registry mechanism only | `DiagRegistry` (bodies: Phase 7 sound) |
| `PS2_U3_VIEWPORT` | DELETE — `display::ProbeOn` not written | — |
| the 195 DIAGNOSTIC hook rows (HOOK §3.1) | not transplanted — address-keyed probe SITES stay out of `engine/` | — |

Every row above compiles only under `PS2R_DIAGNOSTICS=ON` (`ps2r_diag`); Release carries no symbol from here.

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
