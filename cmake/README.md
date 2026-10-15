# `cmake/`

**Phase:** 0  
**Purpose:** CMake helpers: gen_build_id.cmake (build-time build id into ${CMAKE_BINARY_DIR}/generated/), later toolchain files per platform folder.

## Belongs here

- *.cmake helpers
- `TitleInputs.cmake` — the title's game-derived inputs come from the USER'S disc: resolves
  `NATIVEEE_DISC` / `NATIVEEE_TITLE_ELF` / `NATIVEEE_STREAM_IRX` (cache entries or environment), SHA-256
  gates them against `games/<title>/TITLE_INPUTS.sha256`, defines the extraction custom commands
  (`tools/disc_extract`) and the generated-directory variables (`PS2R_RECOMP_GEN_DIR`,
  `PS2R_IOP_GEN_DIR`, `PS2R_RECOMP_ENV`, `PS2R_RECOMP_TEST_ENV`), `PS2R_TITLE_RECOMP_AVAILABLE` /
  `PS2R_TITLE_IRX_AVAILABLE`; without an input the title and its tests are skipped with STATUS lines.
- `ps2r_run_tool.cmake` (-P) — runs a generator with its (huge) output captured to a log file.
- `ps2r_verify_manifest.cmake` (-P) — every generated file must hash to the committed `MANIFEST.sha256`
  (STRICT: remove the output and fail; `-DPS2R_RECOMP_CHECK_MANIFEST=OFF`: warn).
- `ps2r_verify_input.cmake` (-P) — a loose ELF / IRX re-hashed at build time (a stamp the generation depends on).
- `Ps2rPrivateFixtures.cmake` — the private game-derived fixture contract (`$NATIVEEE_PRIVATE_FIXTURES`;
  `ps2r_private_fixture_path()`, `ps2r_add_private_fixture_test()`, the `ps2r_private_fixtures` library's
  home is `tests/lib/`): tests that need a removed fixture SKIP 77, never fail.

## Does not belong here

- —

## Transplanted from (OLD, read-only)

- `host_app/cmake/gen_build_id.cmake`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
