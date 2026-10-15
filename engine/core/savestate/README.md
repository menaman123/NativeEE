# `engine/core/savestate/`

**Phase:** 1  
**Purpose:** PS2SAVE1 v2 envelope (FROZEN bit-for-bit), SectionSpec registry, async writer, quiescence protocol.

## Belongs here

- savestate_format (frozen), SectionRegistry, SavestateWriter, Quiescence, SaveRequest

## Does not belong here

- per-subsystem section PAYLOAD code (declared beside the module it saves); title-tied providers (games/<title>/savestate/)

## Transplanted from (OLD, read-only)

- `savestate_format.{h,cpp}, savestate_writer.{h,cpp}, savestate.{h,cpp} orchestration`

## Flags consumed (FLAG_LEDGER outcomes applied at transplant; Phase 1B)

| Flag | Outcome | Where |
|---|---|---|
| `PS2_SAVE_ATOMIC` | NORM — atomic tmp + fclose + rename always; tmp removed on any failure | `WriteStateFile` (`savestate_format.cpp`) |
| `PS2_SLOWSPOT_ASYNC` | NORM — the one-slot async writer thread is the only writer path | `SavestateWriter` |
| `PS2_W13R7_NORESEED` | DELETE — nothing written | — |
| `PS2_SAVE_SLOT` | SETTING `save.slot` (0) — consumer lands with the GUI savestate browser | `Config` |

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
