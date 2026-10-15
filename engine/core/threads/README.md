# `engine/core/threads/`

**Phase:** 1  
**Purpose:** Thread primitives: SPSC command ring, Park (condvar + absolute-deadline waits — the PS2_WAITFIX winning arm), queue helpers.

## Belongs here

- cmd_ring.h/.inl, Park, small lock-free helpers

## Does not belong here

- cpu_relax / thread QoS (platform/), any PS2 semantics

## Transplanted from (OLD, read-only)

- `sound/ring.h, timeline.h WaitFixOn consumers (condvar arms only)`

## Flags applied

Rows copied from `PHASE1_IMPL.md` §1A "Flags consumed" (`FLAG_LEDGER.md` outcomes):

| Flag | Outcome | Where |
|---|---|---|
| `PS2_WAITFIX` | NORM — condvar/absolute-deadline arm only; the OLD spin/`sleep_for` arm is not written (`grep -rn sleep_for engine app` → 0) | `Park` |
| `PS2_NO_QOS` | NORM — belongs to `engine/platform` (§1B), not here | — |

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
