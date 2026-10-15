# `tests/oracle/`

**Phase:** 2+  
**Purpose:** OG-oracle differential tests: OLD headers with std-only closure included READ-ONLY by absolute path (PS2R_OG_ROOT), python drivers that run OLD test binaries bounded (verify/run_bounded.sh). SKIP loudly when the OLD tree is absent; lifted fixtures are checked in with sha256 so proofs survive without it.

## Belongs here

- `og_*` prefix = the TU's content derives from the frozen OLD tree: a read-only OLD `#include`
  (`og_*_oracle.cpp`), a lifted OLD model or fixture, or a PORT of an OLD test whose contract is
  re-expressed against NEW (`og_page_invariance.cpp` — no OLD include, the OLD test is its source).
  Lint exemptions for OLD includes are keyed on this prefix; a ported test needs none.
- og_*_oracle.cpp, drivers, lifted fixtures
- `lift_og_tables.py` lifts the OLD `texture_decoder.cpp` block/column tables into `og_tables.bin`
  (sha in `og_tables.sha256`; `--check` needs no OLD tree, `--lift` re-lifts and must reproduce it).
- `og_gs_format_oracle.cpp` -- PsmLayout / Swizzle / ClutLayout vs the OLD address families
  (F1 lifted, F2 `#include`d read-only, F3/F4 rect models lifted incl. the local->local `FL` dispatch,
  CSM1 x3 + `csm1_unswizzle`), and the `TEXA` case: TexaExpand / PixelConvert vs the lifted OLD value
  leaves (Expand16to32, DecodePSMCT24 alpha, ScaleAlphaPS2ToGPU + inverse, GetPixelSize, pixels-per-write).
- `og_page_invariance.cpp` -- port of OLD `tests/test_page_invariance.cpp` (the VRAM-level co-page
  contract; NEW-only, no OLD include needed).

## Does not belong here

- any write into the OLD tree (never)

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
