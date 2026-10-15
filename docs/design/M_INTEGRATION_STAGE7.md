# M-series integration, 2026-10-07

This checkpoint assembles completed agent stages and keeps their new selections
off by default. It is not M0–M9 completion or DT-12 retirement acceptance.

## Composed changes

- Eighteen TrigMath entries, including XYZ rotation, tangent and paired/XYZ
  matrices, join the existing math selection. The reviewed title counter
  catalog now describes forty source operations.
- Owned movie picture delivery and MovieGraphics producers, texture
  load/destruction bindings and Sprite producers compile in the shared title
  library; their original-body oracles are registered in the full project.
  Shared scene targets and live resource delivery remain unfinished.
- `hle.sound_samples=on` selects owned file delivery, SoundBanks, SoundControls
  and the common mixer's VoiceSource together. The provider runs before the
  retained voice's keyed/step check, including when the owned voice is silent.
  Core gains, Movie PCM, master gain and the final clamp retain their order.
  Pending audio ownership still refuses checkpoint/restore.
- Fresh thread capture, restart guards and exact installed-body continuation
  metadata are integrated. Cold boot now uses observed dispatch and releases
  its adopted context. Completed-boundary admission and root save/restore
  commands remain separate work.

OLD sound findings retained through the agents' command/ramp/sample pins are
SOUND_P7_IMPL/REVIEW (audible ramps), SOUND_P8_REVIEW6E (readiness),
SOUND_P9_IMPL FIX-F (first-block intent), SOUND_P6_REPORT (jingle identity) and
BUG-003 (pause/resume batches). The parent read these records before changing
the mixer composition. The shared mixer test also checks silent ownership,
explicit handoff, both core gains and the Movie/master/clamp order.

## Validation

Evidence lives outside the repository under `int_work/m_wave/`.

- `build-stage7-release-2.log`: combined Release application and focused
  test targets build successfully. The initial build overlapped the mixer
  signature change and linked stale objects; the dependency rebuild resolved
  it. No such mixed build was used for the game comparison.
- `ctest-stage7-release.log`: 18/19 focused checks passed. The new mixer
  handoff assertion incorrectly expected a looping sample to finish outside
  its original block. A shorter observation interval fixes that assertion;
  `ctest-stage7-mixer-pin.log` passes. The production common-mixer
  StreamFileSelfOracle, math, movie, texture, Sprite and thread/capture checks
  passed in the original run.
- `dt12-stage7.log`: 67 retirement-verifier checks passed.
- `purity-stage7.log`: 747 files, zero title-token violations.
- The initial provenance check found the new continuation generator missing
  its mandatory table row. `provenance-stage7-fixed.log` and the registered
  CTest both pass after adding the exact size/hash row.

## Actual game comparison: not accepted

`sound-output-comparison-stage7/` retains the immutable executable, source
manifest, exact commands, run metadata and complete frame/audio/memory hashes.
Disposable raw captures were bounded and removed; their cleanup receipt is
retained. Both serialized cold boots completed 1,500 frames on the same binary
with `hle.stream_files=on` and `hle.movie_lifecycle=on`; only
`hle.sound_samples` changed.

| Lane | Control / selected keys | First different key | Different keys |
|---|---:|---:|---:|
| Frames | 1500 / 1500 | 212 | 1286 |
| Audio | 250 / 250 | 176 | 74 |
| Memory, advisory | 25 / 25 | 0 | 25 |

Selected work delivered all 120 bank requests without a reported service error.
It applied 21 controls but played **zero owned sample voices**; both arms keyed
eight retained voices. Therefore this run proves neither sample playback
retirement nor audio equivalence. Movie delivery counters and its 170–183
boundary window match. Final domain ticks differ by one (2661 / 2660);
ordered file waits differ (90 / 89). These are observations, not yet a causal
attribution. The services agent is investigating the bank/control composition.

Binary SHA256:
`311bff671668d425d7bd4e33ad39a61c163a89ac740d5b23f08c13b4417b050b`.
Source-manifest SHA256:
`616416d6be6db57fd9d22b6e5a4e2fad5fb701ef76dffb191dbfd57ec2da4260`.

No defaults were flipped. Whole-model zero census, 13 legs ×3, owned playback
reachability and the M7 performance round remain open. PCSX2 was not used.
Frame-time delta is unmeasured.
