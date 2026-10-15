# Contributing to NativeEE

Thanks for helping. This is short on purpose; the long form is in `README.md` (Developers) and
`docs/`.

## Build

```sh
cmake --preset macos-release          # or linux-release, windows-release (see CMakePresets.json)
cmake --build --preset macos-release -j8
```

Prerequisites per platform are in `README.md` → *Developers* → *Prerequisites*. One-command builds
that also stage the artifact folder are `./build.sh` (macOS / Linux) and `.\build.ps1` (Windows).

## Run the tests without a disc

No game file is needed for the engine, the recompilers, the tools and the component suites:

```sh
ctest --preset macos-release --timeout 900 -j8 -E '^Root|^Og'
```

Without a disc image or title ELF, cmake prints a `SKIPPED` status line per title target, and the
title binaries (`nativeee`, `nativeee_oracle`) and their tests are simply not registered. Tests that
need the frozen original tree (`Og*`, `PS2R_OLD_ROOT`) or the extracted game files (`Root*`) skip
with ctest's SKIP return code (77); they are never red because something is absent.

With your own disc image the full tree builds and every test exists:

```sh
cmake --preset macos-release -DNATIVEEE_DISC="/path/to/the title.iso"   # or -DNATIVEEE_TITLE_ELF=/path/to/SLUS_123.45
```

The ELF and `STREAM.IRX` are SHA-256 checked against `games/<title>/TITLE_INPUTS.sha256`; the
generated code must hash to the committed `MANIFEST.sha256` files (`README.md` → *The recompiled code*).

## Private fixtures (`NATIVEEE_PRIVATE_FIXTURES`)

Some tests replay bytes captured from the running game — RAM savestate probes, GS dumps, texture
atlas pages, VU0 microprogram captures, the title's `.data` word dump. Those files are game-derived
and are **not in this repository**. The tests that read them are registered through
`cmake/Ps2rPrivateFixtures.cmake` and resolve the files under

```
NATIVEEE_PRIVATE_FIXTURES=<dir>      # mirrors the repository layout, e.g.
                                     # $NATIVEEE_PRIVATE_FIXTURES/tests/vu/fixtures/vu0_shadow_code.bin
```

Unset, or file absent → the test SKIPs (77). A contributor without the captures loses nothing but
those rows; a reviewer with them runs the same suite with the variable set. Each fixture directory
carries a `README.md` / `PROVENANCE.md` saying what used to live there.

## The asset-scan gate

```sh
python3 tools/ci/asset_scan.py --tree .      # must print CLEAN, exit 0
python3 tools/ci/asset_scan.py --selftest
```

`asset_scan.py --tree` walks the whole tree: file names of disc / executable / savestate / dump
formats, format magic, byte windows of the title's ELF inside every file, raster images, files over
1 MB and long hex-literal arrays — each allowlist entry in the script carries its reason. It is the
first step of the CI `guard` job and every build / release job depends on it; `build.sh` /
`build.ps1` run the artifact mode over the staged folder before they finish. If your change adds a
large or binary file, the scan tells you which check fired; add the file to the matching allowlist
only with a justification in the same line.

## What may never be committed

- **No game data**: no textures, audio, models, movies, fonts, strings or disc bytes, in any
  encoding, in any file type (including hex arrays in source, test vectors and documentation).
- **No recompiled output**: `recompiled*.cpp`, `recompiled.h`, `iop_recompiled.*` and everything else
  the recompilers produce are generated into the build directory from your own disc and are
  `.gitignore`d. Only their manifests (`MANIFEST.sha256`, `MANIFEST.md`, `INVOCATION.md`) and the
  hand-written glue are tracked.
- **No disc-derived bytes**: savestates, memory-card images, GS / VIF / VU dumps, captured
  microprograms, data-section dumps, screenshots of the game. Hashes and offsets of them are fine
  (that is what the catalogues, evidence JSON and `*.fhash` / `*.ahash` lanes are).
- **No personal paths or credentials**: use repository-relative paths or placeholders
  (`<repo>`, `$PS2R_OLD_ROOT`, `$NATIVEEE_PRIVATE_FIXTURES`).

`.gitignore` blocks the usual extensions, and the asset scan is the gate — but the rule is the
rule, not the tooling.

## Code rules the lints enforce

Engine code never names the game (`LintEnginePurity`); one class per `.h` / `.cpp` pair
(`LintOneClass`); no mutable globals in headers (`LintHeaderGlobals`); size budgets
(`LintSizeBudget`); the play binary links no IOP (`LintPlayBinaryNoIop`). The `ps2r_` / `PS2R_` code
prefix stays. Run `ctest -R '^Lint'` before you open a pull request. The acceptance shape for any
change is a Release build with 0 failures, 0 `warning:`, 0 `ld: warning`, the component suite green,
and — for anything touching the engine — the deterministic lanes identical to the previous state
(`verify/`, `README.md` → *Harness and tests*).

## Commits and pull requests

Conventional Commits, imperative mood, subject ≤ 72 characters, type and scope as in the history:

```
feat(input): Active-source contract census + select=Tab, dpad=arrows
fix(config): Regenerate the settings table after the boss integration
test(root): RootQuitGame + RootExitClean pins
docs: Builds for play -- 2b14dc5b
```

Types: `feat`, `fix`, `test`, `docs`, `refactor`, `perf`, `chore`, `ci`. One logical change per
commit; the body says what and why. A pull request names the tests it added or changed and the
`ctest` / `asset_scan.py` output it was verified with. Bug reports: attach the triage bundle
(`<user data>/logs/triage/<time>/`) and `BUILD-ID` (`README.md` → *Something went wrong?*).
