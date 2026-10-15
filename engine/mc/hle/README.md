# `engine/mc/hle/`

**Phase:** 4 (stage 4E)  
**Purpose:** McHostStore + McCommandModel (documented divergence: mc_latency_polls) + libmc bindings.

## Belongs here

- McHostStore, McCommandModel, libmc_bindings

## Does not belong here

- —

## Transplanted from (OLD, read-only)

- `sce_hle/libmc.cpp`

## Rules

- Argument reads follow the EE calling convention: the first eight integer arguments are `a0-a3, t0-t3` (`sceMcGetInfo`'s `*format` is t0, `sceMcGetDir`'s `max`/`table` are t0/t1); nothing of these calls is on the caller's stack. `sceMcGetInfo`'s `*format` is read from t0 only under Config `hle.libmc_format_in_t0` (BUG-089: the inherited `sp+0x10` read stored `format` into the title's MC-manager header; the stack arm stays the default until the MC model serves the title's 4-slot boot flow, DECISIONS_LOG DP-32).
- Measured against the hardware (PCSX2, `int_work/p833a/con_mcsync.out`) and NOT modelled here: the first `sceMcGetInfo` of a card completes after 30 one-frame polls, later probes the next frame (`mc_latency_polls` is a poll count, 0 under the deterministic profile); PCSX2's fresh card reports `0x1f3f` free clusters where the model reports `0x1f00`.
- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
