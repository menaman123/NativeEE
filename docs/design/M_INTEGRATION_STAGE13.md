# Stage 13: M-series round 2 integrated, every selection still off

Integration branch `integration-20261008` from `main` b39a543, executed per
`int_work/m_series/PARENT_REVIEW_2026-10-07_r2.md` §6 on 2026-10-08. Full command/
result ledger: `int_work/m_series/integration/INTEGRATION_LOG_r2.md` (logs
`r2-*.log` alongside). Every build and ctest ran bounded
(`verify/run_bounded.sh`) at `-j2`; the game-process `Root*` set at `-j1`, one
process. No step was skipped for red; no unexpected conflict occurred.

## What moved (serial order)

1. **Gate record** — `docs/design/M_GATES_2026-10-07.md` committed (9c99d28f).
2. **Bernoulli `port-product` 2d0f80a** (merge f084e3aa, clean). `EeBootstrap::Run`
   is split into seven phase TUs (`app/host/ee_bootstrap_{census,devices,image,
   leg,play,sections,services}.cpp` + `leg_state.h`; `ee_bootstrap.cpp` is 95
   lines) with its size-budget allowlist entries dropped; the play-session recorder
   round trip is pinned end to end (`RootRecordInputRoundTrip`,
   `tests/app/root_record_input_round_trip.py`). The six Root lane-identity checks
   of the split root all Passed (`RootReleaseLanesIdentical 70.90 s`).
3. **Herschel** — cherry-picks `2ccb470`→aa92a328 (one Movie PCM owner on a single
   `AudioMixer` sink seat: `AudioMixer::SetMovieSource(PcmSource*)`,
   `SoundOutput::BindMovie(PcmSource*)`, SVCS envelope v3; `SetMovieInput/
   SetMovieGain/MovieGain` removed, no shim), `1e012a91`→466d517f
   (`title::ServiceStartup::Inspect(Inputs)` → `Report::{Stream,Sound}StartupOwned()`,
   the M11 contract Bernoulli consumes), `d4b44ca`→fd37eb47 (M5 scope row). Then
   the root patch `stream/integration/r2-01-root-single-movie-pcm-sink.patch`
   (91295362): its `HLE_COVERAGE.md` hunk applied as-is (Movie PCM row → "resolved,
   round 2"); its two `app/host/ee_bootstrap.cpp` hunks were **relocated** into
   `app/host/ee_bootstrap_image.cpp` (the post-split home of that code) — the
   `MovieSound` ctor now takes `MovieSound::Sink{[&sound_output](PcmSource* s)
   { sound_output.BindMovie(s); }}` and the gain callback reaches
   `movie_sound.SetGain(gain)` alone; the `if (cfg.hle.audio_mix)
   sound_output.BindMovie(fmv_pcm, spu_core)` / `sound_output.MovieGain(...)` lines
   are gone. One mechanical consequence: the image TU's local `Mixer&` alias lost
   its only use in that TU and was removed (`-Werror=unused-variable`); `LegState::
   mixer` and every other TU are unchanged. `grep` for `MovieGain(`, `BindMovie(fmv_pcm`,
   `SetMovieInput`, `SetMovieGain` under `app tests engine games` finds only two
   documentation lines describing the removal.
4. **Zeno `port-graphics-r2` 2df1da8** (merge 659b6bf1, clean): `CaptureScene`
   admits every family bucket (incl. 4/21/23 — the first time the composed shadow arm
   can run), `TextureAssets::Lease`, DT-12 evidence tools `verify/dt12_sources.py`,
   `verify/dt12_assemble.py` (five `--selftest`s PASS), Sprite/Text self-oracle DT-12
   exports, M8/M9/M2/M0 interface answers in the Graphics evidence.
5. **Lagrange `port-textures` f65bf0d4** (merge 23c3a352, clean, no shared files):
   distortion settings version / DSTN state / `EffectsApplication` composition
   (title-side; root application deferred, see below), effects census and report,
   bucket-4 screen-effects boundary parameters and census, colour-filter palette
   ownership; the five effects/cloth pixel+asset oracles register independent of
   include order (`ctest -N` count 5).
6. **Socrates `port-math` 655bf6d3** (merge 2e2d4c17, `HLE_COVERAGE.md`
   auto-merged): the counted `AnimateJoint` adapter (`JointLibrary.generated.animate`
   counter; catalog 91→92, verifier `Ran 69 tests … OK`) and the `MeshPass::Hooks::
   capture_material` owner hook (Lagrange's `cloth_mesh.patch` content — the patch
   now reverse-applies and is not applied separately). Trig TUs byte-identical to
   b39a543; `-O2 -fno-strict-aliasing -fwrapv -ffp-contract=off` preserved (the
   `ffp-contract=off` count rose 10→11 only because the new
   `math_library_joint_animate_oracle.cmake` carries it).

`git diff --stat b39a543 2e2d4c17`: 124 files changed, 9502 insertions(+), 2709
deletions(-). Shared files `engine/core/config/*`, `engine/ee/cpu/dispatch_table.*`,
root `CMakeLists.txt`, `games/<t>/hle/CMakeLists.txt`, `docs/filemap/settings_table.tsv`:
**byte-identical to b39a543**. `games/<t>/verify/retirement_counters.json`: 91→92.

## Movie PCM sink decision (semantic conflict, §5.1)

Herschel's `2ccb470` and Bacon's stage-38 `MovieSound::FifoConsumer` (25a6569/608e2c0,
built on the removed `SetMovieInput/SetMovieGain/MovieGain`) are mutually exclusive on
the same two root hunks and the same `HLE_COVERAGE.md` row; Bacon's
`tests/recomp/test_movie_audio.cpp` would not compile after 2ccb470. **Herschel wins**
(engine/sound owner's decision that round 1 asked for; Bacon's own handoff planned the
"one-lambda swap"). `port-movie-r2` is **not merged** this stage; Bacon re-expresses on
the merged tree (drop FifoConsumer; keep the 33-check attribution and non-copyable
`MovieSound` if still true) and prepares the Start/Wait/Stop install-order patch against
`ee_bootstrap_image.cpp` (≈ lines 294/312) plus the MOVIE_ACCEPTANCE.md
`--set hle.stream_files=on` fix for his next single-writer slot. `MovieAudio` in this
tree is main's 89-check version.

## Defaults — all replacement selections remain OFF

Unchanged from `main` b39a543 (the diff of `engine/core/config/` and
`docs/filemap/settings_table.tsv` is empty; `gen_settings_table.py --check` →
`reproducible (95 rows) -> PASS`). The only `on` values are the ones accepted in
earlier stages: `hle.meta_arena=on`, `hle.sound_bank_headers=on`,
`hle.graphics_strips=plain`. Every replacement key stays **off**: `hle.audio_mix`,
`hle.culling`, `hle.files`, `hle.graphics_frame`, `hle.graphics_movie`,
`hle.graphics_shadows`, `hle.graphics_sky`, `hle.graphics_sprites`, `hle.graphics_text`,
`hle.input_service`, `hle.math_helpers`, `hle.movie_lifecycle`, `hle.movie_start`,
`hle.movie_step`, `hle.movie_stop`, `hle.movie_wait`, `hle.music`, `hle.music_buffers`,
`hle.save_storage`, `hle.service_time`, `hle.sound_samples`, `hle.stream_files`;
harness keys unchanged. The round-2 gate verdicts (`M_GATES_2026-10-07.md`,
review §5) flip **nothing**: no key satisfies all four DT-12 conditions
(pair clean + 13×3 + census zero with positive work + pins); the closest are
`hle.movie_lifecycle`/`hle.movie_step` (pair identical, census zero, 13×3 not run) and
`hle.culling` (pair identical, 13×3 impossible on legacy anchors).

## Validation on the integration branch (Release preset, `-j2`, bounded)

- Final build `cmake --build --preset macos-release -j2 -- -k 0` → **0 FAILED,
  0 `ignoring duplicate libraries` warnings**, 0 compiler warnings (`r2-final-build.log`;
  the per-step full builds of steps 3/4/5 likewise 0/0).
- Component suite `-E '^(Root|.*Smoke|GsCorpus|Render_AttractReplay|Render_TitleFrame|
  GsReplay)'` → **`100% tests passed, 0 tests failed out of 589`** (Stage 12: 577);
  22 asset/fixture skips (Cloth*/Distortion*/Wave*/Shadow*/Sky*/Mesh* self-oracles
  without configure-time assets, Gs_Inv25/26/30, CoreDrawlistRecorder, VifArmsVsOld,
  Vu1Replay*/Vu1CatalogIdentity) and `Vu1CollectSweep (Disabled)` — the Stage 12 skip
  classes plus the new owners' asset-gated oracles. New Passed tests include
  FrameEnvironment (`994 checks, 0 failures`), TextureAssets (`691 checks, 0 failures;
  SELF 3 assets / 11 levels / 45696 pixels`), Sprite/Text self-oracles, Distortion*,
  Screen*, ServiceStartup (`43 checks 0 failures`), MathLibraryJointAnimateOracle,
  RootRecordInputRoundTripSelftest.
- Per-step sets: step 1 10/10; step 2 38/38 (`AudioMixer 50562 checks 0 failures;
  frames=25287 Movie=10240`, `SoundOutput 9626 checks 0 failures; Movie frames=2400
  model_mix_calls=0`); step 3 29/29; step 4 53/53; step 5 26/26 (`MathLibraryTrigOracle
  Passed 26.25 sec`).
- Math three-preset evidence reproduced on `MathLibraryJointAnimateOracle`:
  macos-release, macos-dev and macos-diag (the latter re-configured with
  `PS2R_MATH_ORACLE_ELF`, a build-dir cache value) each
  `100% tests passed, 0 tests failed out of 1` with the identical line
  `checks=168130 failures=0 cases=12000 published=1940 blended=4060 actual_bodies=18100
  actual_decoder_calls=72064 actual_scale_calls=12252 actual_translate_calls=12256
  actual_euler_programs=18032 actual_turn_programs=9044 actual_interpolate_programs=12252`.
- Game-process tests (`-j1`, one process), on the final binary
  (`r2-final-root-ctest.log`): `RootRegistersVuVifGifSections Passed 2.16 sec`,
  `RootPlayWallLock Passed 26.43 sec`, `RootAnchorPadReady Passed 3.24 sec`,
  `RootReleaseLanesIdentical Passed 81.04 sec`, `RootColdbootProbeResumes Passed 3.33 sec`,
  `RootRecordInputRoundTrip Passed 2.34 sec` → **`100% tests passed, 0 tests failed out
  of 6`** (also 6/6 right after step 1).

## Skipped / deferred

- **Bacon `port-movie-r2` d47d6d3 (7 commits) — not merged** (above). Nothing lost:
  2079dce/0705177 are textually clean but document a FifoConsumer root that does not
  exist; 25a6569/608e2c0/25efb84/f47e491/d47d6d3 conflict on the chain.
- **Lagrange `distortion_application.patch` — deferred, kept atomic.** On the chain
  `git apply --check` fails only on `app/host/ee_bootstrap.cpp` (whole-file split); the
  other 8 files would apply (registry 95→96 rows, counters 92→96) but landing them
  without the root hunk leaves `EffectsApplication` uncomposed. Lagrange regenerates it
  against `ee_bootstrap_{devices,image,sections,census,leg}.cpp` + `leg_state.h`. Its
  default-off `hle.graphics_distortion` has no gate this round (`CaptureEffects` still
  owed by Zeno).
- `cloth_mesh.patch` and `joint-animate-5d33b656-shared.patch`: content already in the
  tree (both reverse-apply) — not applied, not deleted.
- Housekeeping named in §6 (deleting `bernoulli-01/02`, `stage36-*`/`stage37-*`,
  `PROPOSAL-m5-*` from the owners' patch dirs) was **not** performed by the integrator;
  the parent decides.
- Not run here (parent-only, need a game process beyond the Root set): the §7 gates —
  M10 control 13×3 re-baseline, `hle.graphics_shadows` cold-boot pair, Socrates cold-boot
  pair, `hle.audio_mix` pair, Bacon pairs, Zeno exports from the merged tree.

## Frozen Release binary for the round-3 gates

`int_work/m_series/gates/bin_2e2d4c17/nativeee_release`
**sha256 `8156b26defe55ce42e8b5f09d2bea5a045371c2c0257af8ec8a4f5d74674076d`**
(`nativeee_release.sha256`, `PROVENANCE.txt`). Built by `r2-final-build.log` from code
HEAD 2e2d4c17 (`PS2RECOMP-BUILD-ID 2026-10-08 09:06:53`); it is the exact binary the
589/589 suite and the 6/6 Root set ran against. The branch's later commits are docs
only (this file and the integration log), so the code tree at the branch HEAD equals
2e2d4c17. The b39a543 binary (`gates/bin_b39a543/`) is stale for Graphics shadows,
JointLibrary Animate, ServiceStartup and the split root.

## Next (orchestrator)

Review this branch, then `git switch main && git merge --ff-only integration-20261008`;
relaunch owners in the review's §8 order (Bacon first). Round-3 gates (§7) run on the
frozen binary above, starting with the Root lane identity and the M10 control-arm 13×3
re-baseline.
