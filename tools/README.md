# `tools/`

**Phase:** 0+  
**Purpose:** REPO-level utilities (not engine build-time programs — those are engine/tools/): regen_from_frozen.sh (bounded regeneration of title outputs from the frozen OLD tools + manifest check), fixture lifters, doc/ledger generators.

## Contents

- `disc_extract/` — `ps2r_disc_extract <image> <path-on-disc> <out> [--sha256 HEX]`: one file out of a
  PS2 disc image (plain .iso / raw .bin, through the engine's IsoFs), SHA-256 gated. The build uses it
  to pull the title ELF and `DATA/SYS/STREAM.IRX` out of the user's disc (cmake/TitleInputs.cmake);
  ctest `DiscExtractSynthetic` runs it over a synthetic volume.
- `iop_recompiler/` — the IOP (R3000A) recompiler `ps2r_iop_recompiler` (STREAM.IRX ->
  `iop_recompiled.{h,cpp}`): byte-identical copies of OLD's sources (its `PROVENANCE.md`), run by the
  build as a custom command; its output is manifest-checked, never committed.
- `recompiler_v2/` — the EE recompiler `ps2r_recompiler_v2` (DP-23); run by the build as a custom
  command over the user's ELF into `<build>/generated/games/<title>/recomp/`.
- `flag_phase_counts.py` — regenerates `docs/filemap/FLAG_PHASE_COUNTS.txt` from
  `docs/filemap/FLAG_LEDGER.md` (per-phase NORM/SETTING/DIAG/TEST/DELETE/NEW tally quoted
  by CONSTRUCTION_PLAN §3; `--check FILE` is the ctest `FlagPhaseCountsReproducible`).
  Its `PHASE_OF` table is the plan's phase assignment expressed as OLD-file patterns.
- `gen_settings_table.py` — regenerates `docs/filemap/settings_table.tsv` (CONSTRUCTION_PLAN
  R14): the Config schema in machine form (key / type / peak / det / source_var / ledger),
  verified before writing against `FLAG_LEDGER.md` (every SETTINGS-section var maps to a key
  or is listed as FOLDED/BUILD_ONLY; the ledger column is the master-table outcome) and
  `PHASE1_PLAN.md` §3 (every key named there and no other). Its
  `# deterministic_fingerprint=` header is derived from the det column and is the single
  source read by `tests/core/test_config.cpp`, `test_config_schema.cpp` and
  `verify/profile_deterministic.py`. `--check FILE` is the ctest `SettingsTableReproducible`.
- `recomp_symbol_census.py` — the generated-code seam roster (PHASE3_PLAN §3.4 / §3.4.4: 27
  rows, 101 entries + 11 include paths) as an executable check over a directory holding
  `recompiled.h`, `recompiled.cpp` and the shards: `--dir DIR [--check]` prints per-entry
  reference counts (common TU / header / shards) and exits 1 under `--check` if any entry is
  missing. The script's `ROSTER` is the source of truth the plan quotes; 3B runs it over
  `games/<t>/recomp/` and adds the unlisted-symbol scan.
- `gsdump/` — the PCSX2 GS-dump census (P8.20-B): `gsdump_census.py <dump.gs[.zst]>` reads the
  reference emulator's one-frame register stream (header, screenshot → `--screenshot out.ppm`, GSState
  freeze, the GIF packets: PACKED incl. A+D vertex kicks, REGLIST, IMAGE; PRMODECONT) and lists every
  draw grouped by its blend / test / texture-function state (PRIM, ALPHA A/B/C/D/FIX, PABE, COLCLAMP,
  TEST, FRAME, ZBUF, TEX0 TFX/TCC/PSM/CLUT, TEXA, kicks, vertex RGBA, bbox); `draw_census_compare.py
  A[:frame] B[:frame] …` censuses `.gsdl` recordings (OLD/NEW, with the HLE class name) and dumps with the
  SAME address-free key and prints the N-way present/missing table; `gsdl_slice.py in out first [last]`
  streams a frame range out of a multi-GB recording. Each has `--selftest` (ctests `ToolsGsdumpCensusSelftest`,
  `ToolsDrawCensusCompareSelftest`, `ToolsGsdlSliceSelftest`). Informed by OLD's read-only
  `tools/parse_gsdump.py`, `gsdump_state.py`, `dump_transfers.py` (format), own code.
- `footprint_watch.py` — the leg footprint sampler (P8.23-A, BUG-083): launches a leg (or `--pid`
  attaches to one) and every `--period` seconds writes `elapsed_s frames footprint_mb peak_mb
  resident_mb` to `--out` (`vmmap --summary` "Physical footprint" / peak; `frames` = the line
  count of `--frames-file`, the leg's `frames.fhash`). A footprint that keeps growing with the
  frame count after the boot movies names a host container that retains per-frame work;
  `run.meta footprint=`/`footprint_series=` carry the same census from inside the process.
  `--selftest` (ctest `ToolsFootprintWatchSelftest`).
- `ci/asset_scan.py` — the game-data gate (Charter 2 C1). Artifact mode (`<dist dir | archive>`) is what
  build.sh / build.ps1 run over `dist/`; `--tree <dir>` is the SOURCE-TREE gate (`python3 tools/ci/asset_scan.py
  --tree .` must print zero findings; `.github/workflows/ci.yml` runs it first): names, format magic, byte
  windows of the title's ELF, raster images / files > 1 MB / pasted hex arrays outside the in-source allowlists.
  `dist/asset_scan.py` is a thin forwarder at the path the release docs quote. The removed game-derived
  fixtures are read by tests from `$NATIVEEE_PRIVATE_FIXTURES` (`cmake/Ps2rPrivateFixtures.cmake`).
- `frame_diff.py` — the frames lane's pixel attribution: two presented frames (P6 PPM or
  24/32-bit BMP, e.g. a `--dump-frames` grab vs the original's display dump) → each image's
  lane hash (`FrameHasher::HashNativeScale`, so a dump is proved to be the presented image),
  differing-pixel count, bbox, max|delta| histogram (5-bit steps are multiples of 8),
  per-channel signed mean, and a 64-px grid of regions with per-region hashes; `--json`,
  `--flip-a/--flip-b`, `--crop`; `--quantize-a/-b N` truncates an input to N bits per channel
  before comparing (5 = the PSMCT16 store with DTHE 0: the DP-28 compare of OLD's 8-bit display),
  the residual split SUB-STEP (`--substep`, default two 5-bit steps) / OBJECT-level with the
  object regions listed, and `--draws <drawlist_tool dump> [--draws-fbp 0x70000]` joins those
  regions with the OLD draws' bounding boxes (by HLE class / primitive / target).
  `--selftest` is the ctest `ToolsFrameDiffSelftest`.
- planned (Phase 3): `regen_from_frozen.sh` — the CONSTRUCTION_PLAN §A.1 invocation in
  executable form (bounded, cwd pinned, inputs copied, manifest check).

## Belongs here

- shell/python repo utilities

## Does not belong here

- anything linked into a binary

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
- `regen_from_frozen.sh` — (3B) the exact CONSTRUCTION §A.1 invocation of the frozen `recompiler_tool_v2`:
  verifies tool/ELF/stub md5s, pins cwd to `build/regen/{a,b}/cwd`, runs twice bounded, requires `diff -r`
  empty and the PHASE3_PLAN §3.2 report lines, installs into `games/<t>/recomp/` with `MANIFEST.md`,
  `MANIFEST.sha256`, `INVOCATION.md`. `--check` re-hashes (ctest `RecompManifestIntact`); `--out DIR` = one run.
- `recomp_symbol_census.py` (3B addition) — the UNLISTED scan: every seam-family identifier the generated TUs
  reference that is neither in `ROSTER` nor defined by the generated TUs fails `--check` (found P3-SEAM-1).
  The common TU now explicitly includes generated `continuations.h`. That sidecar participates in the
  same definition scan: `recomp_continuations` is defined in its anonymous namespace by
  `tools/recompiler_v2/continuations.py`, not an external runtime seam. No roster/allowlist exemption is
  added. The metadata pins reject a missing header, declaration-only lookup, an unincluded sidecar and
  an additional unresolved extern. `ContinuationMetadata` separately checks the generated bytes.

- `gs_replay/` — the corpus replay oracle (replay / census / diff / classdiff) over the NEW GS HLE + GL backend, OLD-grammar output compared against `gs_corpus/baseline` by `tests/gs/replay` (Phase 6).
- `drawlist_tool/` — the `.gsdl` inspector (`stats | frames | dump | diff`), OLD-grammar output (Phase 6).
- `vu1_aot_pack/` — the pre-built VU1 library pack: `build --corpus <dir>... --out <pack dir>` emits, compiles, loads and re-verifies every image of a corpus through `Vu1AotBuilder` and writes `pack.txt` + one library per image (keyed by the engine's VU1 AOT ABI key, `vu1_aot_stage_prelude.py --key-header`); `check --corpus ... --pack <dir>` proves a compiler-less engine is served by it; `key` prints the engine's key. The pack is game-derived (never committed, never a public release asset); the engine loads it from `vu1_aot_pack/` next to the executable or `engine.vu1_aot_pack=<dir>`.
- `check_sizes.py` — PHASE6_PLAN §6 G10 size budgets over the Phase-6 write set (files ≤ 1,500 lines,
  functions ≤ 150; `engine/gs` minus `format`, `engine/render`, `engine/platform/sdl`, `tools/gs_replay`,
  `tools/drawlist_tool`, `tests/lib`, `games/<t>/renderer_profile`); ctest `Phase6SizeBudgets`.
