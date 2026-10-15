# Reviewed component integration and remaining sound divergence

This stage integrates the reviewed graphics, math, service, texture and
checkpoint work. Experimental selections remain off; this is not M0–M9
completion or acceptance of a whole hardware retirement.

## Integrated changes

- Completed source-frame snapshots for UI/Movie buckets, immutable Movie
  images through deferred delivery, and Movie-specific sampled-edge guards.
- Type01 rigid/placed mesh assets, geometry and deferred material producers,
  with owned mip-aware rendering. Live selection still requires the mesh
  bucket's source environment and whole-material consumer ownership.
- Texture state encoding/preflight and one shared UI texture selection policy.
  Root migration and required-section publication remain unfinished.
- Bank requests accepted at send/wait boundaries, preserved pending intent,
  ordered host delivery and real common-Mixer comparison fixtures.
- Music storage and two independent, balanced Movie buffer loans. Both Movie
  consumers and root ownership must be composed before installing this path.
- Thirty trig/matrix operations, including quaternion interpolation, angle
  conversion and Euler extraction. The split source files retain the same
  arithmetic compilation flags. The reviewed catalogue now contains 52
  narrow source-operation counters, not a whole-VU retirement counter.
- Completed-boundary F5 component proof and strict continuation metadata.
  Actual-game capture closure, coordinated provider persistence and F8
  orchestration remain open.

OLD-bug evidence and pins are recorded with the subsystem handoffs:
graphics `FRAME_SOURCE.md` / `MESH.md`, textures `PERSISTENCE.md`, audio
`BANK_TIMING.md` / `BUFFER_HANDOFF.md`, and the checkpoint wrapper audit.
No reference implementation was copied and no PCSX2 work was performed.

## Validation

Artifacts are under `<work>/int_work/m_wave/`.

- The first Release build reached its 300-second limit while linking tests.
  `build-stage11-release-2.log` completed the full build. After the final math
  and fixture changes, `build-stage11-release-3.log` completes the full Release
  build and `build-stage11-debug.log` completes the matching Debug application.
- `stage11-lints.log`: **17/17 pass**, including repaired structural/session
  lints, continuation metadata, symbol census, guard-page and RPC fixtures.
- `stage11-final-source-gates.log`: **4/4 pass**, including the repaired
  single-environment-reader check after explicit Movie export arguments.
- `stage11-dt12-verifier.log`: **67/67 pass** at the 51-operation catalogue
  revision, before the final Euler catalogue addition.
- `stage11-focused/`: **73/73 pass**, including new mesh, texture state/
  selection, Movie and buffer oracles, common mixing, completed-boundary
  capture, restored gameplay, windowed Movie playback, and real Debug/Release
  and inline/threaded rendering comparisons. The application binary identities
  remain unchanged during the run.

These checks repair all seven failures from the complete stage10 suite and
exercise the affected components. They are not a claim that the complete suite
was rerun at this revision, nor the 13-leg ×3 retirement battery.

## Sound acceptance remains open

`sound-output-comparison-stage11/` retains the immutable executable, complete
lanes, exact commands, source manifest and raw-capture cleanup receipt.
Only `hle.sound_samples` differs; files and Movie lifecycle are selected in
both arms. Both runs complete 1,500 frames.

| Lane | Different keys | First different key |
| --- | ---: | ---: |
| Frames, 1500 per arm | 1282 | 212 |
| Audio, 250 per arm | 74 | 176 |
| Memory, advisory, 25 per arm | 25 | 0 |

All 120 selected banks deliver and acknowledge without errors. The run still
has zero owned sample plays/channel samples; both arms key eight retained
voices. The bank repairs fix their pinned cases but do not clear this
integrated divergence.

Model/selected final domain ticks are 2662/2660, file wait parks 91/89, and
first non-silent audio windows 177/176. Shared service-time ownership, remaining
control/Movie handoffs and a source-defined positive playback leg still need
attribution. No arbitrary timing adjustment or model fidelity is authorized
by this evidence.

Release SHA256:
`1b8c2b6838111c4722f8ca1a00fe03e89ad9326f7cb72c7e2626d1aba69d904e`.
Comparison source-manifest SHA256:
`7f72af549e70878f1bb07e7f57a59d49eb8b3e436fadabf02b64b1cfab7c1dda`.

No new whole-model zero census is accepted. No default changed. Frame-time
delta is unmeasured; the isolated performance round remains scheduled after
M7 activation and acceptance.
