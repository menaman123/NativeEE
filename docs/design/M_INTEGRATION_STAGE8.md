# M-series integration: movie cursor, music and completed boundaries

This stage composes reviewed agent work without changing replacement defaults.
It does not close M0–M9 or the full DT-12 retirement gate.

## Application composition

- Movie callbacks and decoder read-ahead share one owned MovieStream cursor
  when `hle.movie_lifecycle=on`. Prepare/Open/Close and PCM playback controls
  follow the same selection. Prepared file ownership refuses capture before
  decoder Start; closing the file does not flush queued PCM.
- `hle.music=on` installs MusicLibrary on the same Audio and mixer provider
  as sample output. It includes file/bank/control ownership. This remains
  experimental: source preload timing, the sample comparison regression and
  composed audio persistence are still open.
- The F5 handler now asks SessionCheckpoint for fresh threads at a completed
  game-boundary return, then publishes the asynchronous writer's real result.
  The game token, quiescence, exact continuation and section-owner checks
  remain mandatory. F8 orchestration and a demonstrated actual-game F5
  reproduction remain unfinished.
- Twenty-one trig/matrix operations now join the thirteen macro helpers,
  eight collision operations and animation decoder. Font producers and the
  complete static texture/UI corpus join the shared build. Their live
  graphics/resource activation remains separate work.

OLD pins come from the movie source/provider handoffs (FMV2_IMPL,
FMV3_REVIEW and SOUND_P5_IMPL), the music command/ramp handoff, the texture/
font inventories and the checkpoint E2_SAVE_IMPL/REVIEW audit. No reference
implementation was copied and no PCSX2 evidence was taken or required.

## Validation

Artifacts are under `int_work/m_wave/`.

- `build-stage8-release.log` built the application but exposed a generic
  boundary-to-dispatch link dependency in the standalone pacer test.
  `build-stage8-release-2.log` completes the full Release build after the
  boundary owner supplies deferred-return scheduling explicitly.
- `ctest-stage8-focused.log`: all 40 selected checks pass, including actual
  movie providers, music/sample commands, common mixing, the complete texture
  corpus, Sprite/font oracles, math, configuration, checkpoint continuations,
  generic boundary ordering, source purity and recompiler provenance.
- `ctest-stage8-restore-initial.log`: RootRegistersVuVifGifSections and
  RootColdbootProbeResumes both pass on the initial stage8 application.
  Installed-wrapper restore compatibility still needs the broader root suite;
  the source audit does not grant blanket continuation permission.
- The inventory selects 529 tests under DT-12, excluding the unchanged 20
  PCSX2 consumers. Two declarations are disabled and four are unavailable.
  This inventory is not a claim that the complete suite has run.

## Actual movie comparison

`movie-services-comparison-stage8/` retains the immutable application, exact
commands, source manifest, complete lanes, metadata and capture-cleanup receipt.
Both runs completed 1,500 frames; only `hle.movie_lifecycle` differed.

| Lane | Control / selected keys | Different keys |
|---|---:|---:|
| Frames | 1500 / 1500 | 0 |
| Audio | 250 / 250 | 0 |
| Memory, advisory | 25 / 25 | 22; first 180 |

The selected arm serves 547 pictures, 1,410 movie callback/demux windows,
1,953 PCM feeds, two playback starts and two stops. Its one shared file
cursor handles 1,440 reads / 11,760 sectors across two Prepare/Open/Close
lifetimes. Errors and replaced control calls are zero. Actual IPU BCLR
dispatch changes from four to zero; every selected IPU command counter is
zero. Completed-boundary return rejections are zero in both arms.

Binary SHA256:
`22604cfa90d72ec8d0ff4a7f8086fb5a7e30ed0931ccd79374ae8b566d645aad`.
Source-manifest SHA256:
`0e15989e7f17714e5c17a6bc6ed4b8c67d7002d8e21f318ed43f76e4bfc8d60c`.
This comparison predates the generic boundary linkage fix and settings-table
registration; its evidence keeps that exact identity.

No whole model is newly accepted as retired. The 13-leg ×3 battery, remaining
movie loans and picture presentation, sample timing regression, production
graphics and persistent service ownership remain open. Frame-time delta is
unmeasured; these concurrent-development runs are not performance measurements.
