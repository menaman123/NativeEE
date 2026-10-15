# Stage 14: M-series round 3 integrated, pre-flip reference established, every selection still off

Integration branch `integration-20261008-r3` from `main` 041f47fc (Stage 13), executed on
2026-10-08 10:45–12:05 EDT by the round-3 integrator following the Stage-13 serial method
(`int_work/m_series/PARENT_REVIEW_2026-10-07_r2.md` §6) in the order product → services →
movie → graphics → textures → math. Full command/result ledger:
`int_work/m_series/integration/INTEGRATION_LOG_r3.md` (logs `r3-*.log` alongside). Builds
bounded (`verify/run_bounded.sh`) at `-j4` (no owner build ran concurrently); the full
component suite ALONE at `-j8`; every game-process class (`Root*`, `GsCorpus*`,
`Render_*Replay*`, `GsReplay*`) at `-j1`, one process. `main` was not committed to.

## What moved (serial order)

1. **Bernoulli `port-product` 8a236f9f** (merge 5f71b97d, clean; 5 commits = his patches
   07–11): `--set` route for the two root acceptance opt-ins `harness.legacy_input_save` /
   `harness.live_restore` (files still refuse them); scratchpad admission in the owned
   Input/Save entries (the title's main thread stack is on the scratchpad — the
   `hle.save_storage=on` frame-374 abort); `pad_ready=` / `input_service=…opened,stable,setup`
   attribution keys for the frame-120 pair difference; `app/host/startup_ownership.{h,cpp}`
   consuming Herschel's `ServiceStartup` contract (`run.meta service_startup=…`, the named
   live-restore refusal). 34/34.
2. **Herschel `port-services-r3` 8f0bd7bf** (merge 6786efa7, clean; no shared file):
   `FileSchedule` charges ordinary Memory reads at the frame start under owned service time
   (the gate-1 lag; `LoaderChainSelfOracle … owned_parks=48 owned_sequence=1
   last_wait_tick=179/179 failures=0`), `StreamState::ImportPending` re-issues legacy pending
   ring windows. His 42 names 42/42.
3. **Bacon `port-movie-r3` 523dd66b** (merge 5c5ecb09, clean): re-expressed on Herschel's
   single Movie PCM sink seat — `MovieAudio` pins the owner on `AudioMixer::SetMovieSource`
   (117 checks), non-copyable `MovieSound`; `GraphicsMovie::InstallPlayer` /
   `GraphicsApplication::InstallMoviePlayer` install Start/WaitFrame/Stop AFTER
   `movie_storage.Install()` (the gate-3 silent refusal), every root install refusal names
   its owner; the `MovieBuffers` CanPlay pin (1656 checks). **Semantic rule held:** after the
   merge no code reference to `SetMovieInput`, `SetMovieGain`, `MovieGain(`,
   `BindMovie(fmv_pcm` or `FifoConsumer` exists under `app tests engine games`. 43/43.
4. **Zeno `port-graphics-r2` 284610a4** (merge db16dc1f — **one conflict**, resolved):
   refuse-not-abort for textures/sprites/text/sky/strips/frame (`title::GraphicsRefusals`,
   `run.meta graphics_refusals_<family>=…first=frame:F/source_frame:S/…`), `--dump-frames`,
   `verify/frame_pair_attribute.py`, `GraphicsOutput::CaptureEffects(bucket, frame, env)`
   for buckets 4/21/23. The conflict was `app/host/graphics_application.{h,cpp}`: Bacon's
   named-refusal blocks and Zeno's `SetFrameClock` propagation touch the same two sites;
   both were kept (the `diff-cc` of db16dc1f shows exactly that). Then Zeno's
   `refusal_frame_clock.patch` (ce452b29): its context no longer matched (Bacon's `refused`
   lambda), so the three-line `graphics_application.SetFrameClock([&host] { return
   host.Boundaries(); });` was placed by hand at the site the patch names. Seven verify
   `--selftest`s PASS; 45/45; verifier 69 OK.
5. **Lagrange `port-textures` a33b76c4** (merge d7d9ec03, clean; no shared file): the
   regenerated split-root `distortion_application.patch`, `TextureConsumers::Selected`
   (Config→family texture selection map), bucket-4 screen-effects census row through the
   effects root, CSM1 palette storage order, effect-arm refusals instead of faults. Then the
   patch itself (2342a5bd), applied `--3way` (13/14 files clean; the `ee_bootstrap_image.cpp`
   Install site conflicted with Bacon's named refusals → kept the refusals, call
   `effects_application.Install(…, (address, body, selected))`, label `"effects
   application"`): `ShadowApplication` → `EffectsApplication` across `leg_state.h` and the
   five phase TUs, `hle.graphics_distortion` (default **off**, GL-backend rule, test_config
   pair, PHASE1_PLAN row), `settings_table.tsv` 95 → 96 rows (`reproducible (96 rows) ->
   PASS`), `retirement_counters.json` 92 → **97** (`Graphics.shadow.{begin,mesh,end}`,
   `Graphics.distortion.draw`, `Graphics.screen.effects` `.model_calls`). The agreed one-lambda
   root wiring (Lagrange's `capture_effects.md`; Zeno's SUPERSEDED note: "valid as is")
   replaced the empty bucket-23 environment service in `ee_bootstrap_devices.cpp` with
   `graphics_output.CaptureEffects(23, frame, environment)`. `capture_effects.patch` NOT
   applied (superseded by Zeno 5ade0183). 61/61 (9 asset-gated skips).
6. **Socrates `port-math` c1113b04** (merge 30677403, clean; no shared file): quad-wise
   `GuestSpan` validation (the scratchpad-mirror seam behind the 11/13 restored-leg aborts),
   joint adapters refuse-never-abort (`refused_*`/`refusal_*` census members), the culling
   legacy import contract (`MathLibraryCullingCheckpoint legacy anchors admitted=11`). Trig
   TUs byte-identical to 041f47fc; `-ffp-contract=off` in the 11 oracle cmake files. 31/31.
7. **Integrator repair e0eeb855**: the full suite's only red was `CoreSingleGetenvSite`
   (`tests/recomp/test_math_library_culling_checkpoint.cpp:311: getenv` — Socrates' 8e4d1b07
   read `PS2R_LEGACY_ANCHORS` via `std::getenv`, a second getenv site; his lint set omitted
   this check). The override is now the test command's first argument
   (`"${PS2R_OG_ROOT}/save_states"`), the hard-coded fallback root kept, pins unchanged
   (`admitted=11`, `total checks=15672 failures=0`). Socrates carries it.

`git diff --stat main..e0eeb855`: 173 files, +5663/−1082. `app/host/ee_bootstrap*.cpp` total
3011 lines (seven phase TUs; `ee_bootstrap.cpp` untouched). Shared files: root `CMakeLists.txt`,
`engine/ee/cpu/dispatch_table.*`, `games/<t>/hle/CMakeLists.txt` **byte-identical** to 041f47fc.

## Defaults — all replacement selections remain OFF

`git diff main.. -- engine/core/config/` adds exactly one Config field, `hle.graphics_distortion`
(bool, `= false`, validator rule `video.backend=gl` like `graphics_shadows`), and the `--set`-only
acceptance route for `harness.legacy_input_save`/`harness.live_restore` (a route, not a default:
both stay `off`; files refuse them). The deterministic fingerprint is unchanged. Every `hle.*`
replacement key stays **off** and `hle.graphics_strips=plain`; the earlier accepted `on`s
(`hle.meta_arena`, `hle.sound_bank_headers`) stand. No gate ran this stage; nothing flips.

## Validation on the integration branch (Release preset)

- Final build `cmake --build --preset macos-release -j4 -- -k 0` → **0 FAILED, 0 `warning:`,
  0 `ld: warning`, 0 `ignoring duplicate libraries`** (`r3-final-build.log`; every per-step
  build likewise 0/0/0).
- Full component suite ALONE, `-j8 --timeout 900 -E '^(Root|GsCorpus|Render_AttractReplay|
  Render_TitleFrame|GsReplay)'` → **`100% tests passed, 0 tests failed out of 597`** (Stage 13:
  589 under a narrower exclusion), 22 fixture skips = the Stage-13 classes, `Vu1CollectSweep
  (Disabled)` (`r3-final-ctest-2.log`; the first run was 596/597 — the getenv site above).
- Game-process classes `-j1`: **72 of 73 Passed** (`r3-final-root-ctest.log`): all 45 `Root*`
  but one (incl. `RootReleaseLanesIdentical Passed 71.00 sec`, `RootColdbootProbeResumes 2.13`,
  `RootAnchorPadReady 3.24`, `RootPlayWallLock 26.41`, `RootRegistersVuVifGifSections 2.14`,
  `RootRecordInputRoundTrip 2.25`, `RootPlayModeSmoke 6.18`, `RootMovieLifecyclePlay 24.46`,
  `RootTitleScreenEffects 98.97`, `RootAttractCutsceneFrames 89.60`), 19 `GsCorpus_*`
  (`GsCorpus_slowspot` Disabled, pre-existing), 7 `Render_*Replay*`, 2 `GsReplay*`.
  **`RootCdvdPollVisibility` Failed — pre-existing on `main`:** the identical script and
  arguments against the frozen `gates/bin_2e2d4c17` (Stage 13) and `gates/bin_b39a543` binaries
  fail the same way (`FAIL [poll] boot case 0 >= 6 at the final image (b60)`): from frame 120
  every `--state-every 60` image is refused `movie_software: Movie software input/pictures/PCM
  still require Close` (Bacon 189aaf6c, already in `main`), so only b0/b60 exist. Not round 3's
  and not repaired here — Bacon (M2) owns it; it blocks nothing in CUTOVER.md's acceptance list
  but is a red in "the full suite".
- Per-step focused sets: 34/34, 42/42, 43/43, 45/45, 61/61, 31/31 (result lines in the log).

## The PRE-FLIP REFERENCE (`int_work/m_series/cutover/ref0/`, `REFERENCE -> ref0/leg`)

Binary `build-release/app/host/nativeee` `PS2RECOMP-BUILD-ID 2026-10-08 11:30:07` (code = this
branch's HEAD e0eeb855), frozen as `ref0/bin/nativeee_release` sha256
`f9699cb3b0d9b7167259eb1c9604aa7bdfe5eb0b25d37804d44d0230812d9d5f`.

```
python3 verify/run_leg.py --binary build-release/app/host/nativeee --tree new --anchor coldboot \
  --frames 4600 --profile deterministic --timeout 3000 --stall-s 600 \
  --repo $PS2R_OLD_ROOT --out int_work/m_series/cutover/ref0/leg
-> run_leg: OK            frames.fhash 4600 lines, last 4600,0xcdb1ff109c38971c
                          audio.ahash   767 lines, last  766,0xfd605d29418b3536
                          mem.mhash      77 lines, last 4560,0xfcc35c8f67bdd7b0 (advisory)
run.meta: frames=4600 finalized=1 park=none profile=deterministic
```
Census (`ref0/census.txt`, 97 catalog counters): 73 nonzero, 19 zero, 5 absent — every
replaced model executes on this control arm, e.g. `graphics_frame_{setup,send,interrupt,clear,
layout}.model_calls = 1/4536/2323/3310/6861`, `graphics_strip_plain.model_calls = 16` (the 16
Plain fallbacks, `graphics_refusals_strips total=16 unsupported_state=16 detail:259
producer:0x1a9120 frames 135–136`), `graphics_screen_effects.model_calls = 4526`,
`graphics_shadow_{begin,end}.model_calls = 3547`, `movie_{start,stop}.original_calls = 3`,
`movie_{step,wait}.original_calls = 2323`. The five absent rows (`Graphics.sky.*`,
`Graphics.sprites`, `Graphics.text`) are published only by a constructed `GraphicsRuntime`
(a selected sprites/text/sky/movie/frame or `strips=all`) — on the all-defaults arm they do
not exist; a flip of one of those keys creates them. Smoke reference: `RootPlayModeSmoke
Passed 6.22 sec`, `RootMovieLifecyclePlay Passed 24.36 sec` (`-j1`), their kept `run.meta`
and census under `ref0/smoke/`. Details and every row quoted: `ref0/NOTE.md`.

## Decisions and deferrals

- Zeno/Bacon textual conflict and the two drifted root patches were resolved/placed by the
  integrator with both owners' content intact (above); each is one commit with the reason.
- Lagrange's bucket-23 environment lambda was wired because both owners stated it in
  writing; `hle.graphics_distortion` stays off and has no gate yet.
- Housekeeping named by the owners (delete `bernoulli-01…06`, keep or drop
  `capture_effects.patch`) was not performed; the parent decides.
- Not run here (parent-only): the owners' requested pair re-runs (gate 2/3/4/5 on the new
  binary), the Movie pictures pair with `--dump-frames`, the slowspot bench.

## Next (orchestrator) — the cutover flips, one stage each, on `main`

`git switch main && git merge --ff-only integration-20261008-r3`; then CUTOVER.md stage by
stage with `REFERENCE` as the lane oracle. Facts the first flips need, read this stage:
1. **"textures-at-load" is not a single key.** `TEXTURE_OWNERSHIP.md` §"Selection map":
   texture ownership follows the consumer family keys — `hle.graphics_sprites` owns the forty
   startup icons (the most consumers), `hle.graphics_text` the Crash font pages, sky owns none
   alone, the control arm owns zero of the 4,048 IDs. The stage therefore flips
   `hle.graphics_sprites` (and records the rest). Caveat from Zeno's round-3 handoff: the
   round-2 sprites-on leg aborted at frame 60 in `TextureBindings` (`InvalidMemory`); it now
   REFUSES to the original and counts `graphics_textures.refused` / `graphics_refusals_textures`
   — census (c) will not read zero until that refusal is retired. Expect a REVERT with the
   `first=frame:…/detail:3` line unless the re-run shows zero refusals.
2. `hle.graphics_frame`: the five `graphics_frame_*.model_calls` rows above must read 0 with
   `graphics_frame_output.{completed,setups,layouts}` positive; the frame family refuses on all
   five boundaries (Zeno d997dbec), so `graphics_refusals_frame` must also stay `total=0`.
3. `hle.graphics_strips=all`: `graphics_strip_plain.model_calls` 16 → 0 requires the sixteen
   `unsupported_state` strip refusals (frames 135–136, `detail:259`) to be retired first;
   with them present the census is 16 on both arms and the flip is a REVERT by rule.
Freeze the merged `main` binary for the owners' round-4 gates (`ref0/bin/` is that binary).
