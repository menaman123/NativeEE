# `tests/lib/`

**Phase:** 6  
**Purpose:** support libraries shared by ctest suites and `tools/` (test-side only; the engine and `app/` never link them).

## Contents

- `gsdl_reader.h/.cpp` — `GsdlReader`: loads a `.gsdl` recording (versions 1 and 2) into `Recording { frames[] { draws, jobs, order, vsync }, names, truncated }`; target `ps2r_gsdl_reader`.

## Belongs here

- Readers / fixtures / oracles that several test suites or tools share.

## Does not belong here

- Anything the engine needs at run time (that is `engine/`); test bodies (those are `tests/<module>/`).

## Transplanted from (OLD, read-only)

- `host_app/hle/drawlist_reader.h`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- No game constants or VRAM addresses.

Stage 6B adds `capture_sink.h` — `CaptureSink`, the recording `DrawIrSink` the state / transfer tests share (header-only).

## Private fixtures (the public tree carries no game bytes)

- `private_fixtures.h/.cpp` — `PrivateFixtures::Root() / Path(rel) / Exists(path) / Skip(path)`: a test that
  needs a removed game-derived file asks for it by its ORIGINAL repo-relative path under
  `$NATIVEEE_PRIVATE_FIXTURES` (else the source tree) and exits 77 when it is absent; target
  `ps2r_private_fixtures` (compile definition `PS2R_SOURCE_ROOT`).
- `private_fixtures.py` — the Python twin (`private_fixture(rel)`, `require_private_fixture(rel)` -> exit 77).
- `private_fixture_runner.py` — the ctest wrapper `ps2r_add_private_fixture_test()` registers
  (`cmake/Ps2rPrivateFixtures.cmake`): exits 77 unless every `--fixture` exists under the root, substitutes
  `{PRIVATE_FIXTURES}` in the command, forwards the exit code.

