# Stage 15: cutover repairs integrated — texture streams on the scratchpad stack, play smokes fail on a crash

Integration branch `integration-20261008-fixes` from `main` 01f34b3a (flip 6 reverted; defaults
`hle.movie_lifecycle=on`, `hle.math_helpers=on` plus the earlier `hle.meta_arena`,
`hle.sound_bank_headers`), executed on 2026-10-08 19:20–19:42 EDT by the integrator. Two owners'
reviewed fixes were cherry-picked (`-x`) onto `main` in the order graphics → product; no merge
commit, no conflict, no hand edit. Full command/result ledger:
`int_work/m_series/integration/fixes/INTEGRATION_LOG.md` (logs alongside). Builds bounded
(`verify/run_bounded.sh`) at `-j4`; the full component suite ALONE at `-j8`; every game-process
class at `-j1`, one process. `main` was not committed to. No owner worktree was touched.

Branch HEAD (code) **f0d04255**; `git cherry main HEAD` → five `+`; `git diff --stat main..HEAD`:
23 files, +625/−121.

## What moved (serial order)

1. **Zeno `port-graphics-r2` ee7c36fe → 1dc1b80c** `fix(graphics): Read TextureRead streams on the
   scratchpad stack` — the flip-1 cause. Zeno's branch has since been rebased onto 01f34b3a as
   1fd03271; `git patch-id --stable` of ee7c36fe and 1fd03271 are identical
   (`1d0edc82…`), so the cherry-pick of ee7c36fe is content-equal to his current HEAD. The Stream a
   `TextureRead` receives is a stack object and the title's main thread stack is on the
   scratchpad (`TR:src/platform/ps2/entry.cpp:83`); `TextureBindings::Bytes` admitted RAM only, so
   all 40 selected icon reads refused `InvalidMemory` (detail 3) at frame 60. Now
   `TextureBindings::Memory` carries the 16 KB scratchpad window (texture OBJECTS stay RAM-only),
   `GraphicsProducers`/`GraphicsRuntime` take a `Window`, `GraphicsApplication` supplies it from
   the root's guest translation, `SpriteIntent` reads a placed draw's matrix from the window.
   No shared file. 13 files, +220/−36.
2. **Bernoulli `port-product` a86af99a and its three parents → 95660033, 96c3920b, 543092ee,
   f0d04255** (= his isolated patches `bernoulli-12…15`; each `git apply --check` clean on the
   branch before the cherry-picks, each `--check --reverse` clean after — content identical):
   - 4d5808a1 → 95660033 *Fail the play smokes on a crashed or unfinalized game process*:
     `root_play_mode_smoke.py` and `root_play_fmv_presents.py` (behind `RootPlayFmvPresents` and
     `RootMovieLifecyclePlay`) judge runner exit 0 → `finalized=1` → `video=gl` → the pins; SKIP 77
     only for the missing title ELF or a COMPLETED display-less run (exit 0, `finalized=1`,
     `video=stub`). `tests/app/smoke_fake_runner.py` + `--selftest` on each script, registered as
     `RootPlayModeSmokeSelftest` (10 cases) and `RootPlayFmvPresentsSelftest` (11 cases).
   - dad2757b → 96c3920b *Name --game-files in the nativeee usage text* — the one SHARED file
     (`engine/core/config/config_loader.{cpp,h}`): a usage-text line and a grammar comment only.
     `git diff main..HEAD -- engine/core/config/` is exactly those two hunks; no Config field, no
     parser change, `docs/filemap/settings_table.tsv` byte-identical to `main`
     (`gen_settings_table: … reproducible (96 rows) -> PASS`). `tests/core/test_config.cpp` pins
     that `ConfigLoader::Usage()` names every flag the parser accepts.
   - 319b64b8 → 543092ee docs (`M0_M12_PRODUCT_PHASE.md` round 4, `PLAY_SESSIONS.md`).
   - a86af99a → f0d04255 *Fail RootPlayWallLock on a crashed or unfinalized game process* (same
     verdict shape for `root_play_wall_lock.py`; `RootPlayWallLockSelftest`, 5 cases).

Shared files other than the usage text: root `CMakeLists.txt`, `engine/ee/cpu/dispatch_table.*`,
`games/<t>/hle/CMakeLists.txt`, `games/<t>/verify/retirement_counters.json`, `app/host/ee_bootstrap*`
— all **byte-identical** to `main` (checked with `git diff --quiet`).

## Defaults — unchanged vs `main`

`docs/filemap/settings_table.tsv` is identical to `main` 01f34b3a: the `hle.*` keys at `on` are
exactly `hle.math_helpers`, `hle.meta_arena`, `hle.movie_lifecycle`, `hle.sound_bank_headers` (the
latter two accepted before the cutover); every other replacement key stays `off`,
`hle.graphics_strips=plain`. No gate ran this stage; nothing flips.

## Validation on the integration branch (Release preset)

- Step 1 build `cmake --build --preset macos-release -j4 -- -k 0` → 0 FAILED, 0 `warning:`,
  0 `ld: warning` (`step1-build.log`). Zeno's focused set + lints →
  `100% tests passed, 0 tests failed out of 21` (`step1-ctest.log`): `TextureBindings: 840 checks,
  0 failures`, `SpriteService: 82 checks, 0 failures`, `GraphicsProducers: 319 checks, 0 failures`,
  `GraphicsRuntime: 88 checks, 0 failures` — the numbers his handoff quotes.
- Step 2 re-configure (three new tests) rc 0; build → 0/0/0 (`step2-build.log`). His focused set
  + lints → `100% tests passed, 0 tests failed out of 13` (`step2-ctest.log`: `Coretest_config`,
  `RootPlayModeSmokeSelftest`, `RootPlayFmvPresentsSelftest`, `RootPlayWallLockSelftest`,
  `RootRecordInputRoundTripSelftest`, `LintOneClass/EnginePurity/SizeBudget/HeaderGlobals/
  SessionTags/SdkBindings`, `FlagPhaseCountsReproducible`, `CoreSingleGetenvSite`,
  `SettingsTableReproducible`). Direct: `root_play_mode_smoke selftest: PASS`,
  `root_play_fmv_presents selftest: PASS`, `root_play_wall_lock selftest: PASS` (rc 0 each).
  **Fake runner, exit 134 with `frames=0 finalized=0`** (`step2-fake134.log`): the three scripts
  return rc 1 — `RootPlayModeSmoke: FAIL`, `FAIL: 17 check(s) failed (leg kept …)`,
  `RootPlayWallLock: FAIL`; exit 0 + `finalized=1` + `video=stub` → rc 77 (the documented
  display-less SKIP). `./build-release/app/host/nativeee --help` line 16:
  `--game-files <dir>    the title's ELF / disc-side files (paths.game_files; default game_files)`.
- Final tree (BUILD-ID `2026-10-08 19:22:54`, code = this branch's HEAD; the stage's last commit
  is docs-only): full component suite ALONE `-j8 --timeout 900 -E '^(Root|GsCorpus|
  Render_AttractReplay|Render_TitleFrame|GsReplay)'` → **`100% tests passed, 0 tests failed out of
  597`**, 136.79 s (`final-component-ctest.log`); 22 fixture skips = the Stage-13 classes
  (Cloth*/Shadow*/Distortion*/Wave*/Sky*/Mesh* asset oracles, Gs_Inv25/26/30, CoreDrawlistRecorder,
  VifArmsVsOld, Vu1Replay*/Vu1CatalogIdentity) + `Vu1CollectSweep (Disabled)`.
- `cmake --build --preset macos-dev --target nativeee -j4` → 0/0 (for `RootReleaseLanesIdentical`;
  `final-dev-nativeee-build.log`), then the game-process classes `-j1 --timeout 1800 -R '^(Root|
  GsCorpus|Render_AttractReplay|Render_TitleFrame|GsReplay)'` → **`100% tests passed, 0 tests
  failed out of 76`**, 730.97 s (`final-root-ctest.log`; `GsCorpus_slowspot` Disabled,
  pre-existing): `RootPlayModeSmoke Passed 6.24 sec`, `RootMovieLifecyclePlay Passed 24.51 sec`,
  `RootPlayWallLock Passed 24.51 sec`, `RootReleaseLanesIdentical Passed 76.16 sec`,
  `RootCdvdPollVisibility Passed 60.68 sec` (the Stage-14 pre-existing red is green on this
  tree), the three new `*Selftest`s Passed. The smokes now pass under the truthful verdict: a
  completed game process (exit 0, `finalized=1`, `video=gl`) with every pin held.

## Decisions and notes for the orchestrator

- The flip-1 commit **`1cd2a13e` does not re-apply as-is**: on this HEAD `git cherry-pick 1cd2a13e`
  (trial in a throwaway detached worktree, aborted) merges `config.h`, `settings_table.tsv`,
  `test_config.cpp`, `gen_settings_table.py` cleanly but conflicts in
  `tests/app/root_anchor_{frames,heartbeat,pad_ready}.py` and
  `tests/title/textures/test_texture_consumers.cpp` — flips 4–6 added their own `--set` old-default
  pins at the same lines (`flip1-reapply-trial.log`). Re-attempt flip 1 as a fresh one-line default
  change (`hle.graphics_sprites = true`, table regen, the explicit old-default pins added next to
  the flip-4/5 ones), not as a cherry-pick. Expected on the smokes/leg (Zeno's handoff 3 §1):
  `graphics_refusals_textures=total=0`, `graphics_textures=loaded=40`,
  `graphics_sprites=replaced>0,model_calls=0`. Caveat he names (§3c): the first OWNED sprite
  delivery passes the same `GraphicsScene` pre-draw guards that refused the first owned strip at
  frame 118 (flip 3); fixes 2/3/4 (frame first-Send completion, strip retained-at-slot, census of a
  faulted run + the `ee_bootstrap_leg.cpp` catch-around-`session->Run` patch) are in his worktree,
  not here.
- With patch 12/15 in, CUTOVER.md criterion "(d) `RootPlayModeSmoke` and `RootMovieLifecyclePlay`
  pass" is machine-read: a flip whose game process aborts is `***Failed`, not `***Skipped`.
- `docs/phases/INTEGRATION_STATUS.md` still says `--game-files` is "MISSING from the `--help`
  text — reported, not changed here"; it is now present (line 16 of `--help`). That file is the
  cutover record on `main`; the next flip's line should note the repair (not edited on this
  branch).
- Bernoulli's ask (not done here, single-writer slot): `EeBootstrap::InstallDispatch`
  (`app/host/ee_bootstrap_image.cpp:146`) is 313 lines, a `LintSizeBudget` WARN(hard); the lint
  passes (`109 warnings, 0 failures`).
- `TextIntent`/`TextService` (`hle.graphics_text`) still reject scratchpad addresses (Zeno §2); the
  same two-line rule applies when that key's stage comes.

## Next (orchestrator)

`git switch main && git merge --ff-only integration-20261008-fixes` (the branch is `main` 01f34b3a
+ five commits). Then CUTOVER.md: re-attempt flip 1 (`hle.graphics_sprites`) as above, then
`hle.graphics_movie` + `hle.movie_step`, then `hle.music` + `hle.sound_samples`, one stage each,
REFERENCE = `int_work/m_series/cutover/REFERENCE`.
