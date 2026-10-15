# `engine/core/log/`

**Phase:** 1  
**Purpose:** Compile-time category logger (LOG(cat, level, ...)), sinks file/stderr/ring, the always-on RingLog + decoder, rate-budget helpers.

## Belongs here

- Logger, RingLog (crashring extern-C boundary preserved verbatim — frozen generated-code seam), RingDecoder, LogBudget, ring_format.h, ring_tags.h

## Does not belong here

- the OLD 4-layer filter (TagFilterBuf / FastOn / elision / PS2_LOG master) — dissolved, not transplanted

## Transplanted from (OLD, read-only)

- `ring_log.{h,cpp}, ring_log_format.h, ring_tags.h, ring_log_decode.h, hw_verbose.h†, debug_budget.h†`

## Flags applied

Rows copied from `PHASE1_IMPL.md` §1A "Flags consumed" (`FLAG_LEDGER.md` outcomes):

| Flag | Outcome | Where |
|---|---|---|
| `PS2_CRASHRING` | NORM — ring always on, alias gone (no EE-channel clear arm) | `RingLog` |
| `PS2_LOG` | SETTING `dev.log_level` (quiet) | `Config` → `Logger` |
| `PS2_LOG_CHANNELS` | SETTING `log.channels` ("all") | `Config` → `RingLog::Configure` |
| `PS2_LOG_RING_EVENTS` | SETTING `log.ring_events` (65536) | `Config` → `RingLog::Configure` |
| `PS2_LOG_VERBOSE` | DIAG — verbose tier mask is a `RingLog::Configure` argument; the app passes 0 outside `PS2R_DIAGNOSTICS` builds | `RingLog` |
| `PS2_LOG_FASTFILTER` | DELETE — the OLD FastOn filter layer does not exist (compile-time categories) | — |
| `PS2_LOG_ELIDE_DROPPED` | DELETE — the OLD elision layer does not exist | — |
| `CT_FULL_LOG` | DELETE — the OLD master log switch does not exist | — |
| `PS2_RING_TEST_{CRASH,HOTKEY,BUNDLE}` | TEST — not written (`tests/core/test_ring_log.cpp` takes arguments) | — |

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
