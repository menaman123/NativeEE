# Stage 12: whole-phase M-series owners integrated, every selection still off

This stage integrates the six whole-phase owners' reviewed work (PARENT_REVIEW
2026-10-07 §5–§6) onto the integration branch `integration-20261007`, created
from `main` 3f351c7. It is an integration record, not M0–M12 completion and not
an acceptance of any hardware-model retirement: **no default moved**, no
13-leg battery was run, no PCSX2 work was performed, and `main` is unchanged
until the orchestrator reviews this branch and fast-forwards it.

Per-step commands, conflicts, resolutions and quoted result lines are in
`<work>/int_work/m_series/integration/INTEGRATION_LOG.md`;
build and ctest logs sit beside it.

## What moved (serial order)

1. **Product trunk** — `port-product` 6893e1c merged clean (M0 contracts,
   M10 input/save services, product presentation, whole-tree build repair).
   530/530 component tests, 8/8 lints, 39/39 product focused list.
2. **Herschel (M3–M5)** — 8 NEW-only commits: stage-11 bank timing evidence,
   service clock dependency note, owned audio output (`AudioMixer`,
   `hle.audio_mix`), service test link repair, unsettled-bank attribution
   records. Product's M0 `SessionClock`/clock support and
   `StateRestorer` API were kept where the two sides differed (§5.3/§5.4).
3. **Bacon (M2)** — 8 of 9 NEW-only commits: complete Movie Stop
   (`hle.movie_stop`, `Movie.player.stop`), ledger rows, IPU attribution,
   single composable PCM sink seam, root composition pins. ee99e68 was
   content-subsumed by product and skipped.
4. **Zeno (M6/M7)** — 13 NEW-only commits: owned presentation crop/orientation,
   window filtering, declared-selection comparator (`--allow-set-difference`),
   bench selected-arm record, Graphics interfaces + M7 runbook, replaced-model
   census, DT-12 oracle artifact exports (Strip, Frame, FrameClear,
   DisplayLayout, Sky), M6/M7 evidence.
5. **Lagrange (M9)** — `port-textures` 0f188ed merged clean: effects
   (distortion, shadow, cloth, wave), material certificate/graph, texture
   consumer catalogue, shadow application sources.
6. **Socrates (M1)** — `port-math` e5cccb1 merged: trig/culling/bounds/clipping/
   joint source operations (`hle.math_helpers`, `hle.culling`), CULL checkpoint
   owner, DispatchTable.CountCalls provider on all math descriptors. Both trig
   TUs keep `-O2 -fno-strict-aliasing -fwrapv -ffp-contract=off` in every preset.
7. **Parent-applied isolated patches** — Zeno's Graphics replaced-model counter
   catalog (16 descriptors; catalog now 91) and Lagrange's default-off shadow
   application (`hle.graphics_shadows`), re-based onto the `GraphicsApplication`
   composition root.

Semantic decisions applied as written in PARENT_REVIEW §5; where §5 left a
choice the owner's newest code and the default-off toggle were preferred:

- §5.1 Movie PCM/gain owner: the root keeps Movie's `MovieSound` as the owner;
  Herschel's `SoundOutput::BindMovie`/`MovieGain` and the owned `fmv_pcm`
  record run only under `hle.audio_mix` (default off). With the default the
  root behaves exactly as the product trunk. The two-owner gain persistence
  under `hle.audio_mix=on` is recorded in `HLE_COVERAGE.md` ("Movie PCM sink
  and gain owner (open integration conflict)") for Herschel's single-writer slot.
- §5.2 `tests/services/CMakeLists.txt`: Herschel's structure plus product's
  `ps2r_ee`.
- §5.3/§5.4: product's `SessionClock`/clock CMake and `StateRestorer`
  (strict supersets of the services/movie versions).
- §5.5 registry quartet: union; `docs/filemap/settings_table.tsv` regenerated
  at every step (`reproducible (95 rows) -> PASS` at the end), never hand-merged.
- §5.6 `retirement_counters.json`: key union, every descriptor byte-equal to
  its owner's version; 91 descriptors; `tests_retirement_dt12` → 69 OK.
- §5.7 `ee_bootstrap.cpp` / `game_libraries.cpp`: union in source order,
  `-fsyntax-only` with the build's flags before each build.

Integration-only edits (no owner code changed in place):

- **Size-budget allowlist raised by the integrator** (`tests/lint/budget_allowlist.txt`):
  `app/host/ee_bootstrap.cpp` file-lines **2427 → 2493**, `EeBootstrap::Run`
  **2118 → 2154** (commits 6d255d0 → 0f3d6eb → b47ae99, dated reason in the
  file). The file was already allowlisted over the 2,000-line fail threshold
  on `main`; the growth is the union of the six owners' install lists (each
  owner's own tree stays ≤ 2427). This is an **integration-only measure**, a
  lint workaround made by the integrator and not by an owner, acknowledged by
  the orchestrator in the integration review (MAJOR 3). It is not an
  acceptance of the size: **the owner of the composition root's split is
  Bernoulli (M10 product wiring)** — refactor-queue item 1 (split `Run` into
  arming / restore / frame-loop / shutdown phases, each its own type) remains
  owed and the allowlist is to come back down with it.
- The two cloth `--assets` oracles register only when their manifest cache
  variables are set (they failed instead of skipping without one — flagged
  to Lagrange; 84e92a3).
- Review follow-ups (integration review 2026-10-07 23:36, `REVIEW.md`):
  - BLOCKER 1 — one approval row for the coldboot probe in
    `games/<t>/verify/restore_mhash_expected.tsv`
    (`old_b120.ps2st 0x74bacf01b589b5a6 33554432 16384 2097152 4 121`),
    **authorised by the orchestrator**, citing
    `verify/baseline/coldboot/probe/PROOF.txt` (old_b120: PROOF-OK) and
    `verify/baseline/coldboot/mem.mhash` line `120,0x74bacf01b589b5a6`
    (`verify/state_hash.py` over the probe reproduces the row). `old_b60` is
    not approved. Because the four OLD-anchor tests that iterate this table
    (`RestoreMhashAnchors`, `VuVifGifSectionsAnchors`, `PadsSectionAnchors`,
    `MchlSectionAnchors`) resolve rows against `PS2R_OG_ROOT/save_states`
    where the probe does not live, a row whose file is absent from the anchor
    directory is now reported and skipped by those tests (measured with the
    row alone: `50% tests passed, 2 tests failed out of 4`; after:
    `11/11 anchors match`, `11/11`, `11 of 11` ×2, `100% … out of 4`).
    Commit a22bca9.
  - MAJOR 2 — `hle.graphics_shadows` added to the `video.backend=gl`
    validator rule (`config_validator.cpp`) and to `test_config.cpp`'s
    graphics-key loop, with a shadows-only refuse/accept pair, like its
    sibling keys (Lagrange's patch lacked the rule; the Step 7 re-base added
    only the registry rows). Commit 6357e35.

## Defaults — all replacement selections remain OFF

Unchanged from `main` (the only `on` values, accepted in earlier stages):
`hle.meta_arena=on`, `hle.sound_bank_headers=on`, `hle.graphics_strips=plain`.

Every key this wave added or carries is **off**: `hle.audio_mix`,
`hle.culling`, `hle.files`, `hle.graphics_frame`, `hle.graphics_movie`,
`hle.graphics_shadows`, `hle.graphics_sky`, `hle.graphics_sprites`,
`hle.graphics_text`, `hle.input_service`, `hle.math_helpers`,
`hle.movie_lifecycle`, `hle.movie_start`, `hle.movie_step`, `hle.movie_stop`,
`hle.movie_wait`, `hle.music`, `hle.music_buffers`, `hle.save_storage`,
`hle.service_time`, `hle.sound_samples`, `hle.stream_files`; harness keys
`harness.legacy_input_save`, `harness.live_restore` off, `harness.record_input_path`
empty. (`diff` of key/type/peak/det columns against `main`'s table shows only
additions, no changed value.)

## Validation on the integration branch (Release preset, `-j2`, bounded)

- Final build: `cmake --build --preset macos-release -j2 -- -k 0` → 0 FAILED
  (`final-build.log`).
- Whole suite without game-process tests:
  `100% tests passed, 0 tests failed out of 577`; 19 fixture skips
  (Cloth/Shadow/Distortion/Wave/Sky/Mesh self-oracles without their
  configure-time assets, Gs_Inv25/26/30, CoreDrawlistRecorder, Vu1Replay*/
  Vu1CatalogIdentity, VifArmsVsOld) and `Vu1CollectSweep (Disabled)` —
  the same skip classes as Step 1 plus the new owners' asset-gated oracles
  (`final-ctest.log`).
- Math three-preset evidence reproduced: Release/Debug/ASan+UBSan each
  `100% tests passed, 0 tests failed out of 22` with the ELF-backed oracles
  registered (`PS2R_MATH_ORACLE_ELF`); TrigOracle `comparisons=992000 …
  differences=0`.
- Game-process tests (`-j1`, one process), first run at a3222a3:
  `RootRegistersVuVifGifSections`, `RootPlayWallLock`, `RootAnchorPadReady`,
  `RootReleaseLanesIdentical` **Passed**; `RootColdbootProbeResumes` **Failed**
  (`80% tests passed, 1 tests failed out of 5`, `final-root-ctest.log`) — see
  below. Re-run after the review fixes (a22bca9, `fix1-root-ctest.log`):
  `RootRegistersVuVifGifSections Passed 2.17 sec`, `RootPlayWallLock Passed
  26.45 sec`, `RootAnchorPadReady Passed 2.18 sec`, `RootReleaseLanesIdentical
  Passed 70.84 sec`, `RootColdbootProbeResumes Passed 2.16 sec` →
  **`100% tests passed, 0 tests failed out of 5`**; the probe leg reports
  `restore_mhash=0x74bacf01b589b5a6 == lane mem line 120` and the identity
  pin `resumed first frame (key 1) 0x91112b3860dce5fc == cold-boot lane frame
  121` (`fix1-root-coldboot-verbose.log`).
- After the MAJOR 2 validator change: full Release rebuild 0 FAILED
  (`fix2-build.log`); `Coretest_config`, `Coretest_config_schema`,
  `SettingsTableReproducible` and the 13 `Lint*` tests →
  `100% tests passed, 0 tests failed out of 16` (`fix2-ctest.log`).

## Skipped / not green

- **`RootColdbootProbeResumes` — was red at a3222a3, green since a22bca9.**
  The leg exited 2 with `--load-state …/coldboot/probe/old_b120.ps2st: Movie
  boot import has no approved memory signature`. The gate
  (`MovieCheckpoint::ValidateLegacy`, Movie commit 572332b, carried by both
  `port-movie` and `port-product`, absent from `main` — so relative to `main`
  an integration-introduced failure of an OLD-bug pin, as the review notes)
  requires the probe's basename in `games/<t>/verify/restore_mhash_expected.tsv`;
  no owner added the coldboot probes. Resolved by the orchestrator-authorised
  one-row approval described above (PROOF.txt + mem.mhash line 120 cited);
  Bernoulli's handoff listed this test as parent-run and never ran it.
- **ee99e68** (Bacon): skipped as content-subsumed by product (§5.4).
- **`cloth_mesh.patch`**: held until Socrates' M8 MeshPass review (Lagrange's
  instruction). **`PROPOSAL-m5-audio-mixer-pcm-source.patch`**: Herschel's §5.1
  decision, not a parent apply.
- Not registered in this configuration (owner-side): `DistortionPixelsSelfOracle`
  (root `CMakeLists.txt` includes the effects tests before `engine/render`, so
  its `if(TARGET ps2r_render_gl …)` guard is false — same on `port-textures`),
  `TextureConsumersCatalogue`, `ClothAssetsSelfOracle`,
  `ClothMaterialAssetsSelfOracle` (manifest cache variables unset).

## Parent-only gates still owed (owners are waiting)

Unchanged from PARENT_REVIEW §6 "Parent-only gates": Herschel's three-arm
`hle.service_time` run, Socrates' `hle.math_helpers`/`hle.culling` frozen-binary
pairs then 13×3, Bacon's same-binary Movie pairs and formal 13×3, Zeno's five
real-manifest oracle exports and toggle pairs, Bernoulli's two 13×3 arms and
play sessions. None was run here; no receipt is signed by this stage.
