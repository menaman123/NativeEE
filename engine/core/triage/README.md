# `engine/core/triage/`

**Phase:** 1 (InputLog), 9.2 (rest)  
**Purpose:** Play-time capture subsystem (TRIAGE_CAPTURE.md, D9): compiled into ALL builds.

## Belongs here

- InputLog (Phase 1: the ONE PS2_PAD_SCRIPT-grammar recorder/replayer shared by harness + capture), TriageHost, TriageBundler, BundleManifest, FallbackDetector, PerfMonitor, SpikeDetector, SavestateRing, TriageDedup, TriageWriter, FrameAnnotator
- TriageBundle (`triage_bundle.{h,cpp}`, ALL-ON NO EXCEPTIONS 2026-10-09): the crash path's bundle writer -- `<harness.out_dir | paths.log_dir>/triage/<UTC>/{census.txt, drawlist_last200.txt, fault.txt, backtrace.txt, run.meta}` on any uncaught exception / fatal park (play and harness mode), the throw-site backtrace noted on the throwing thread (`NoteThrowSite`, `backtrace_symbols_fd`), and the `std::terminate` hook for exceptions no handler caught. The drawlist text comes from `engine/gs/hle/ir/DrawlistRing` (a fixed-size ring, every build); the root (`app/host/ee_bootstrap_leg.cpp`, `LegCloser`) composes the files.

## Does not belong here

- hash-lane emitters (core/verify), diagnostics-build-only tooling (core/diag)

## Transplanted from (OLD, read-only)

- `verify/input_recorder.{h,cpp} + pad_input.cpp grammar, bug_report.{h,cpp}, slowspot.{h,cpp}, slowspot_detector.h, perf_profiler.h monitor half, ring_log.cpp bundle assembly, artdet FIRE path`

## Flags consumed (FLAG_LEDGER outcomes applied at transplant; Phase 1)

| Flag | Outcome | Where |
|---|---|---|
| `PS2_PAD_SCRIPT`, `PS2_PAD_SCRIPT_FILE` | TEST channel → `harness.inputs` (`--inputs FILE`); the grammar itself is product code | `InputLog::ParseSpec/LoadFile/Apply` (consumer: pad/, Phase 7) |
| `PS2_VERIFY_RECORD_INPUT` | SETTING `harness.record_input` | `InputLog` recorder via `VerifyHost::OnPadServe` |
| `PS2_SLOWSPOT`, `PS2_BUGREPORT`, `PS2_BUGREPORT_SLOWSPOT`, `PS2_BUGREPORT_SLOWSPOT_MAX` | SETTING `triage.enabled` / `triage.perf` (+ `fallbacks`, `spikes`, `fps_target`, `disk_budget_mb`, `ring_period_s` per TRIAGE_CAPTURE §3); the per-run cap is a §3 cooldown constant, no field | `Config` (consumers land with the 9.2 mechanism) |
| `PS2_SLOWSPOT_MS`, `PS2_SLOWSPOT_GF_MS`, `PS2_SLOWSPOT_COOLDOWN_S`, `PS2_SLOWSPOT_DEDUPE`, `PS2_SLOWSPOT_SUFFIX` | NORM — fixed constants of the 9.2 detectors | (9.2) |
| `PS2_SLOWSPOT_ASYNC` | NORM — see `core/savestate` (`SavestateWriter`) | — |

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
