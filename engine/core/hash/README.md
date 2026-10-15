# `engine/core/hash/`

**Phase:** 1  
**Purpose:** The two hashes every lane and key in the tree uses: FNV-1a 64 (the harness lane hash: frames/audio/mem/env fingerprint) and XXH64 (texture/content keys, Phase 6 consumer).

## Belongs here

- `hash.h/.cpp` — free functions `Fnv1a64` (offset `0xcbf29ce484222325`, prime `0x100000001b3`, seed-chaining overload) and `Xxh64` (own implementation of the published algorithm; no vendored code)

## Does not belong here

- CRC32 (savestate envelope) — `core/savestate/savestate_format`; any lane FORMAT (that is `core/verify`)

## Transplanted from (OLD, read-only)

- `host_app/verify/verify_env.h` Fnv1a64 (verbatim), `host_app/vu_hash_fnv.h`; `host_app/render.h` XXH64 **algorithm only**

## Flags applied

- none (no flag names this module)

## Tests

- `tests/core/test_hash.cpp` (`Coretest_hash`): FNV vectors from OLD `tests/test_verify_units.cpp` (`""`, `"a"`, `"foobar"`), seed chaining; XXH64 against the 9 reference vectors OLD `test_probe_bench` pins.

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
