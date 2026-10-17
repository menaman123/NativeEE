# `app/host/`

**Phase:** 1  
**Purpose:** The composition root (≤ 500 ln): construct engine, register ONE TitleProfile, install manifests, spawn threads; mods loaded ONLY under --mods (9.3). Also build id + shutdown ladder.

## Belongs here

- main.cpp, build_id, ShutdownLadder
- `EeBootstrap::Run` (ee_bootstrap.cpp) and its fourteen phase functions in construction order
  (ee_bootstrap_devices / _services / _play / _image / _sections / _census / _leg.cpp): a phase owns the
  objects it constructs on its own frame, hands later phases pointers through `EeBootstrap::LegState`
  (leg_state.h, private to these TUs) and tail-calls the next, so destruction runs in reverse construction
  order as one scope would; an early return ends the leg
- `Vu1AotCache` (vu1_aot_cache.h/.cpp): the VU1 AOT tier's persistent side -- `<user_data>/vu1_aot/corpus/` (the
  title's `Vu1Case` corpus, collected at first run through the tier's kick observer, quota per image) and
  `<user_data>/vu1_aot/<build token>/manifest.txt` (one verdict row per image: hash, status, cases, corpus hash,
  engine build, time, bundles, divergence); `Open` seeds the tier from the manifest and queues the unjudged
  corpus, a verifier thread runs `Vu1AotVerifier` and publishes the verdicts (the frame never waits), `Close`
  joins; `CensusRow` is the run.meta `vu1_aot=` row. A title composition constructs it on the LIVE VU1 unit when
  `engine.vu1_mode=aot`

## Does not belong here

- any PS2 semantics, hooks

## Transplanted from (OLD, read-only)

- `main.cpp shell, host_app/cmake/gen_build_id.cmake, shutdown_policy.h`

## Flags consumed (FLAG_LEDGER outcomes applied at transplant; Phase 1B)

| Flag | Outcome | Where |
|---|---|---|
| `PS2_SHUTDOWN_WAIT` | SETTING `engine.shutdown_wait_s` (10, clamp 0..600) — the 0/legacy infinite-join arm is not written | `ShutdownLadder` |
| `PS2_LOG` | SETTING `dev.log_level` → `Logger::Start` level | `main.cpp` |
| `PS2_LOG_CHANNELS`, `PS2_LOG_RING_EVENTS` | SETTING `log.channels`, `log.ring_events` → `RingLog::Configure` (verbose tier mask = 0 outside diagnostics builds) | `main.cpp` |
| `PS2_VERIFY_*`, `--bench-frames`, `--load-state` | CLI flags `--harness-out --headless --frames --load-state --inputs --state-every --profile` (`harness.*`) | `ConfigLoader` → `VerifyHost`/`FrameCap` |
| `PS2_WAITFIX` | NORM — the realtime pacing arm of the TickSource stub waits through `Park::WaitUntil` (no sleep/poll) | `main.cpp` `RunTickSourceStub` |
| chdir-to-project-root / argv ISO join | not transplanted — explicit `--iso` (`paths.iso`); the harness writes only under `--harness-out` | `ConfigLoader` |

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
