# `tests/core/`

**Phase:** 1  
**Purpose:** core/ suites: config round-trip + schema vs the generated settings table (`test_config_schema` over `docs/filemap/settings_table.tsv`: bijection with `Config::Keys()`, peak + deterministic columns, fingerprint single source), validator DAG, ring golden, FNV/XXH vectors, PS2SAVE1 envelope over the 12 anchor states, frame/audio/downsample vectors (ported test_verify_units), the shutdown ladder tables (ported test_shutdown_policy).

## Belongs here

- unit tests

## Does not belong here

- —

## Transplanted from (OLD, read-only)

- `tests/test_verify_units.cpp, test_ring_log.cpp, test_savestate_format.cpp, test_display_settings.cpp, test_shutdown_policy.cpp, test_savestate_writer.cpp`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
