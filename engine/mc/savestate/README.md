# `engine/mc/savestate/`

**Phase:** integration I4-A (plan item 4.8, MCHL half)  
**Purpose:** `MchlSection` -- the MCHL `.ps2st` section over `McCommandModel`: the undelivered libmc completion pair (i32 cmd, i32 result) and the per-(port, slot) card-seen flags (u8 x 4), 12 bytes, the reference writer's byte order. OPTIONAL (`required=false`; absent = boot state). Registered in `app/host/ee_bootstrap.cpp` after the sound sections (the reference file order `..., SDRX, SNDC, MCHL, ...`).

## Belongs here

- MchlSection

## Does not belong here

- the model itself (`hle/mc_command_model`); the PADS section (`engine/pad/savestate`, not landed: its reference layout carries the deleted press latch)

## Transplanted from (OLD, read-only)

- `host_app/sce_hle/libmc.cpp:865-887` (`LibMc_SerializeState` / `LibMc_RestoreState`), `host_app/savestate.cpp:1347-1354` (capture), `:1646-1652` (validate: `2 * 4 + 4` bytes), `:2035-2044` (restore; absent = keep live values)

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
