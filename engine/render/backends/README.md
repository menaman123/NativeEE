# `engine/render/backends/`

**Phase:** 6 (gl), 9.4 (soft)  
**Purpose:** API adapters: gl/ (first target, GL 4.1 core), soft/ (scalar oracle, 9.4). vulkan/ and gxm/ arrive only with ARCH §16 notes.

## Belongs here

- one folder per API

## Does not belong here

- gs state internals (only gs/hle/ir types + backend.h)

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
