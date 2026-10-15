# M-series integration: bank handoff, UI ownership and axis math

The file-to-bank handoff now preserves the new caller's pending intent while
settling the previous file request. `audio/BANK_TIMING.md` records the source
contract, OLD SOUND_P6 pacing findings, pins and remaining acceptance/timing
defects. This is a file-service fix; no IOP/CD model was deepened.

The source-checked TextureOwners catalogue identifies three Crash font pages
and forty startup HUD icons for eventual activation with their complete
consumers. Arial and borrowed world HUD materials remain outside that initial
selection. Static archive/material and generated-caller audits support the
policy; this catalogue does not activate textures or migrate their state.

The two axis-rotation and two axis-matrix operations bring TrigMath to 25
entries. The reviewed catalogue now names 47 source operations across math
libraries, each tied to its actual generated-body increment and publisher.
These are narrow source-operation counters, not whole VU retirement.

`build-stage9-selected.log` builds the Release application and affected test
targets. All seven checks in `ctest-stage9-focused.log` pass: StreamLibrary,
StreamFileSelfOracle, MathLibraryTrigOracle, TextureBindings,
TextureOwnerBoundaries, source purity and recompiler provenance.

## Sound comparison still fails

The two runs in `int_work/m_wave/sound-output-comparison-stage9/` each finish
1,500 frames on one immutable executable with files/movie selected. Only
`hle.sound_samples` differs.

| Lane | Different keys | First different key |
|---|---:|---:|
| Frames, 1500 in each arm | 1282 | 212 |
| Audio, 250 in each arm | 74 | 176 |
| Memory, advisory, 25 in each arm | 25 | 0 |

Selected bank delivery/acknowledgement completes all 120 requests without
reported errors, but owned sample plays and owned channel samples remain zero.
Both arms still key eight retained voices. Movie outputs and counters agree.
Final domain ticks are 2662 / 2660 and file wait parks 91 / 89. The handoff fix
is not sufficient to attribute or clear the regression. Queued bank acceptance,
shared logical timing and a source-defined positive sample-play leg remain
necessary before selection can become default.

Binary SHA256:
`15092363cb94570b8ccc4d330567518f22bf2d87972608bfb103da3ad816039e`.
Source-manifest SHA256:
`244bcd8b6681cb2c11b407fb3e9ebbb9b9f2bd93f4e635bca5d57a7a5e64ac83`.
The directory retains commands, complete lanes, metadata and capture-cleanup
receipts. No default changed, no whole model retirement was accepted, no
PCSX2 work was performed, and no frame-time delta is claimed.
