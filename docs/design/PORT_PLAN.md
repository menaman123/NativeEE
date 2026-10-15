# Port plan

Date: 2026-10-07. Status: implementation roadmap, not a completion report.

This plan implements **DT-9: native game intent owns the operation and retires its
PS2 transport**. The authoritative policy is
[DT-9](../phases/DECISIONS_LOG.md#dt-9--retire-hardware-scaffolding-through-native-game-intents-user-2026-10-07);
the canonical retirement inventory and acceptance rules remain
[HLE_COVERAGE](HLE_COVERAGE.md#scaffolding-retirement). This document sequences
that work. It does not authorize new DMAC/VIF/GIF, IOP kernel/STREAM.IRX or IPU
command fidelity. DT-12 below defines the current regression gate.

**Current direction (user, DT-12):** finish M0–M9, with graphics
M6→M7→M8→M9 and services M3→M4→M5 in parallel worktrees. Input, storage,
startup, game-state saves and product obligations below complete the host
experience. Earlier MathLibrary/MovieDecoder acceptance remains historical,
bounded evidence; new cutovers use the gate below.

**Acceptance:** same-input NEW model/replacement self-oracles behind a
one-line toggle, differences attributed before default; 13 legs ×3 stable
with unchanged/improved frame/audio first-divergent keys; replaced-model
census zero on every leg with positive replacement work; relevant OLD-bug
pins carried onto the replacement. Memory is advisory. No PCSX2 use,
captures, dumps, pins or comparisons in this wave. Its existing pins remain
unchanged and excluded from acceptance. PCSX2 returns for user-reported bugs
only through supplied content-aligned states or NEW-pipeline GS replay.

The old user save is unavailable and is not a dependency. The user will play
Release at milestone reports. Before changing each subsystem, enumerate the
decomp specification and grep OLD's records; carry the applicable bug cases
as replacement tests. OpenGOAL is a read-only architectural reference:
fixed 60 Hz game tick, decoupled renderer, typed asset renderers, load-time
texture conversion, named render targets and host services; never copy code.
Commit each approved stage. Report model retirement/census, measured frame
time and open items at every milestone close.

After M7, schedule the idle-machine performance round against
`verify/bench/BASELINE.tsv` (L), then the separately measured OLD Release
target and 60 fps in-level. Complete the input recorder and exact commands
for two NEW-recorded user play sessions, plus hotkeys and GUI; track these
alongside the milestone work rather than silently deferring them.

The companion [library_boundaries.tsv](hle_coverage/library_boundaries.tsv) records
source contracts, NTSC candidates, dependencies and practical validation cases.
No row in that ledger is a claim of completed retirement.

### Current implementation status — 2026-10-07

The following is the current integration status, not a percentage estimate or
milestone acceptance. New replacements remain selectable while their complete
DT-12 gate and caller closure are pending.

| Milestone | Implemented evidence | Work still needed |
|---|---|---|
| M0 | Owned jobs, handles/generations, queue ordering and provider contracts; required-section ownership guard | Finish persistent pending work and coherent restore across the composed services |
| M1 | Thirteen macro helpers, eight collision entries, thirty trig/matrix entries and an animation-angle decoder; generated-body oracles; earlier macro/collision 1,500-frame comparison identical in all lanes | Remaining source-enumerated VU0 consumers, game checks for newly integrated trig/angle work, full retirement gate |
| M2 | Selected lifecycle and shared file/demux/audio provider wiring preserve all 1,500 frame/250 audio hashes; zero IPU commands and optional original-body calls; earlier windowed movie sound/presentation passes | Full battery; remaining player buffer loans/audio ownership and picture upload/presentation dependencies |
| M3 | Bank header reader, owned pending-read bytes/deadlines, file snapshot codec, and real STREAM file/sample-bank byte self-oracles | Legacy pending-read import, composed checkpoint migration, all streaming consumers and production bank activation |
| M4 | Ordered logical Scheduler, file completion/wait ownership and scheduling self-oracle | Replace remaining audio/service waits and heartbeat dependencies without IOP execution |
| M5 | Owned audio assets/voices, sample/ramp/command oracles, opt-in bank/control/common-mixer composition through `hle.sound_samples`, MusicLibrary through `hle.music`, and tested music storage/two-part Movie loans | Recheck the integrated frame-212/audio-176 divergence after typed bank acceptance; exercise actual owned playback; music preload timing, effects, composed Movie loans and restore |
| M6 | Ordered Graphics queue, shared renderer allocations, named scene targets and root delivery through the render consumer | Route remaining source producers and resource loading through it; close mixed-path dependencies |
| M7 | Four Strip variants; optional Sky; texture conversion/bindings and quiescent persistence; Sprite/Text ownership policy; immutable Movie image/edge and completed source-frame snapshots | Activate complete consumers in the root, remaining primitives and presentation; then the scheduled performance round |
| M8 | Type01 rigid/placed producer and geometry self-oracles, owned mesh assets/material capture and mip-aware rendering | Source mesh-bucket environment and whole-material consumer ownership before activation; skin/blend/particle/decal and remaining world families |
| M9 | Source material/pass inventory, font producer/original-body oracle and retained backend operations | Activate fonts with owned textures/targets; complete special materials, shadows and screen passes |

The large unfinished portions are production audio integration and world/special
graphics. Input/save providers, recorder and GUI components also exist, but
their application ownership, persistence and product wiring remain part of
M10–M12 and the host-experience obligations. Passing isolated tests does not
close those dependencies. See HLE_COVERAGE for selections and precise evidence.

The current reviewed integration is documented in
[M_INTEGRATION_STAGE11.md](M_INTEGRATION_STAGE11.md): all seven initial
complete-suite failures have passing repair checks, and 73 affected component/
application tests pass. The sound comparison still differs at frame 212 and
audio key 176 after bank acceptance changes. This remains an explicit open
item, with no new default or whole-model retirement accepted.

The interactive application now compiles with the main-thread window/input
loop and shared menu/hotkey queue. The four focused session tests pass,
including pause/one-boundary stepping and a bounded shutdown failure.
Actual game checks are still pending. The initial presenter copies completed
pixels between contexts; its cost is unmeasured. Reproduction capture reports
an explicit refusal until fresh live continuations are available, and runtime
restore remains disabled. This product stage is not a completed save feature
or a measured performance improvement.

Product components now include owned asynchronous reproduction writes,
owner-local thread capture, exact installed-body continuation metadata,
joined-worker restart guards, and completion reporting while paused or
shutting down. Cold boot enters through the observed dispatch surface and
releases its adopted context before destruction. Focused thread/capture tests
pass. The completed-boundary provider and root F5 capture/writer callbacks are
now connected; actual-game reproduction and F8 restore composition remain
open. These component checks do not prove a usable game reproduction.

The current integration checkpoint and its failed opt-in sound comparison
are recorded in [M_INTEGRATION_STAGE7.md](M_INTEGRATION_STAGE7.md). No whole
hardware model is newly retired and no performance improvement is claimed.
The subsequent full Release build, 40 passing focused checks, two historical
restore legs and passing movie cursor/audio comparison are recorded in
[M_INTEGRATION_STAGE8.md](M_INTEGRATION_STAGE8.md).
The bank-handoff correction and additional math/UI ownership checks are in
[M_INTEGRATION_STAGE9.md](M_INTEGRATION_STAGE9.md). Its sound rerun still first
diverges at frame 212/audio window 176; selection remains off.

### Whole-phase agent ownership — user correction, 2026-10-07

An agent's assignment is an entire M phase, including application integration
and acceptance follow-up. Component commits and handoff reports are progress
within that assignment. They do not complete the assignment or transfer its
unfinished implementation to the parent.

The user authorized dispatch on 2026-10-07. All six existing agents received
the whole-phase assignments below in their separate worktrees, superseding
the earlier instruction to hold dispatch. Earlier agent completion messages
refer only to their bounded assignments, not to accepted M phases. Dispatch
does not change the implementation evidence or acceptance status above.

| Phase owner | Whole phases | Responsibility through closure |
|---|---|---|
| Socrates | M1, then M8 | Complete the statically enumerated math consumers, then all rigid/placed/skin/blend/particle/decal geometry families, including their application integration and VU/transport retirement evidence. Reuse the existing graphics geometry handoffs. |
| Bacon | M2 | Complete Movie lifecycle, formats, buffer/audio ownership, presentation dependencies and persistence; coordinate provider work with M5/M7 owners through integrated acceptance. |
| Herschel | M3 → M4 → M5 | Complete files/stream rings, service scheduling, banks/voices/music/effects and their composed persistence. Attribute the sound regression and close each phase's STREAM/IOP/libsd dependencies. |
| Zeno | M6 → M7 | Complete ordered submission/resources, all primitives and presentation, including live root activation, texture ownership/persistence and the required performance round after M7. |
| Lagrange | M9 | Complete every source-enumerated special material, font, shadow and screen pass, including resource ownership and integration with M8 geometry. Existing texture/material work supplies dependencies to M6–M8 without narrowing this assignment to a component. |
| Bernoulli | M0; M10 → M11 → M12 and product obligations | Complete cross-service ownership/persistence contracts, input/game saves, service-only startup, checkpoint/restore and final deletion. Own recorder/play-session commands, hotkeys and GUI through application validation. Each service owner still implements its own state and migration. |

Graphics activation follows M6 → M7 → M8 → M9, while the services chain
progresses alongside it. Dependencies in section 3 still apply, including
M2/M5 for complete Movie presentation. Independent source analysis and
implementation may proceed before those activation dependencies close.
An M0 dependency requires the relevant ownership contract to be settled;
M0's final cross-service validation must not prevent its consumers from
being implemented and integrated.

Each phase owner supplies its full source/caller inventory, OLD-bug pins,
implementation, application wiring, build/config registration, persistence,
actual-execution counters and regression fixes. Shared-file edits use an
explicit single-writer assignment or an isolated integration patch; owners
must not concurrently edit the root bootstrap or shared build/config files.
The parent reviews and merges these changes, commits every reviewed stage,
and operates the shared builds, default changes and acceptance runs. Only one
game process runs at a time, with the tested source/binary frozen. Preparing
and repairing the integration remains the phase owner's responsibility.

A phase is **complete** only when its entire statically enumerated scope has
an accepted replacement or a source-backed unreachable disposition, all
required application/persistence paths are composed, and the applicable
DT-11/DT-12 gate passes: same-input self-oracles with attributed differences,
13 legs ×3 stable/non-regressing frame/audio keys, zero execution of replaced
models on every leg with positive replacement work, and carried OLD-bug pins.
Infrastructure phases must satisfy their composed ownership/order/restore
contracts; they cannot claim a model retirement merely by adding a counter.
Publish the Release build, retirement census, measured frame-time delta
(or explicitly unmeasured result), and remaining project dependencies at
each milestone close. M7's performance round remains a required follow-up.

Use **implementing**, **awaiting dependency**, **awaiting integration**,
**acceptance failing**, or **complete** for phase status. A worker reaching
the end of a response or returning a tested component does not change the
phase to complete. While waiting for a serialized gate, continue independent
work within the assigned phase; record the exact dependency when no such
work remains. Report agent execution status separately from phase status.

## 1. Provenance and evidence rules

`TR:` means the read-only `Smartkin/the title-reversed` repository at commit
**`38d533d0033b48b5d6b49880c49f5eeeade0f38e`**, locally
`<projects>/the title-reversed`. Local tracked HEAD was
checked against that hash and the source checkout was clean for this review.
The PAL specification is **SLES-52568**; the executable being bound here is
**NTSC-U SLUS-12345**. `TR:README.md:1` describes the C++ decompilation; `:9` explicitly
retains split data and VU microcode. A C++ wrapper containing `vcallms`, or a
`.vutext` declaration, does not supply the called program's portable arithmetic.
Use the source as a specification; do not copy its implementations into this tree.

`OG:` means the read-only architecture checkout at
`<projects>/jak-project`, commit
**`efb21c3e8c5a40a74f2e86510f915da55eaeba34`**, cloned on 2026-10-07.
No code from that checkout is included here. `OG:game/graphics/opengl_renderer/BucketRenderer.h`
provides the typed bucket/shared-resource separation; `OG:game/graphics/texture/TexturePool.h`
describes load-time GPU resources and lifetime across overlapping levels.
Our intended pipeline is typed game draw intent → owned resources and named-target
passes → backend → presenter. A fixed 60 Hz game clock owns service completion;
presentation may repeat or skip a completed image without advancing game logic.
These are our design requirements, not a claim that every OpenGOAL path already
satisfies them: its bucket interface still accepts `DmaFollower`, its texture pool
retains VRAM aliases, and `OG:game/system/iop_thread.h` still owns an `IOP_Kernel`.
Those compatibility mechanisms are not templates for this project's replacements.

Repository paths in the ledger are relative to this repository. `TR:` paths are
relative to that fixed source commit. Each address pair cites
`games/<title>/symbols/pal_ntsc_map.tsv`; an extent-alignment
score of `high` is a mapping candidate, not proof of the complete binding.
In the ledger, `MAP:<line>` abbreviates that map and `NTSC:<file>:<line>`
abbreviates `games/<title>/recomp/<file>:<line>`.
Semicolons separate references/dependencies; `-` means no entry asserted.

| Ledger evidence | Meaning |
|---|---|
| `body-checked` | Matching NTSC generated body was inspected for the stated entry and semantics. Caller closure, safe binding and integration still need acceptance. |
| `mapped-candidate` | PAL declaration and map identify an NTSC candidate. Check that NTSC body, ABI and callers before binding. |
| `source-contract` | PAL operation or layout is known; no standalone NTSC entry is asserted. |

Microcode requirements are recorded separately in the notes. In particular,
new `Platform::*` abstractions in the decomp need not exist as standalone retail
functions. Find their retail owner, vtable target or complete call region
statically; do not invent addresses by subtracting one regional offset.

For every binding, check function start, argument registers and stack slots,
N32/EABI float adaptation, return value, object offsets, globals, aliases, vtable
uses, indirect callers and observable side effects against NTSC. Generated
bodies are an index into target semantics; suspicious translation details must
be checked against the target instructions. Preserve ABI-visible VU register
effects while any generated consumer still reads them.

**Discovery is source driven.** Enumerate declarations, callers, material
construction, asset references and program registrations before implementation.
Static inspection of the user's executable/microcode and asset formats can
close missing semantics. Runtime captures are useful validation fixtures and
reach evidence; they are not a mandatory prerequisite to discovering a function.
An unobserved command or shader is not automatically unreachable.

## 2. Starting state and ownership

The preceding subsystem pass is transitional code. Its recorded result is 472
passing CTests and 39 admissible scenario runs, with a **DT-8 FAIL of five extra
raw memory words**: 20,084,452 against 20,084,447, attributed to saved IPU MP1 at
five attract checkpoints. DT-11 now treats that memory-only failure as advisory.
The unfinished decoder-to-IPU bridge was withdrawn. MOV-1 owns the native
lifecycle replacement; do not reintroduce that bridge, mask these words or
change the frozen baseline/attribution rules.

The first implementation pass produced these bounded replacements:

| Component | Implemented scope | Remaining dependencies |
|---|---|---|
| `MovieDecoder` | Active MPEG-2 Create/Reset and picture delivery | Initialise/Start/close, other formats, stream, audio handoff and upload/presentation |
| `MathLibrary` | Complete point-transform boundary at NTSC `0x0018e4f0` | Other VU0 entries, VU1 geometry and packet producers |
| `LoaderFileIo::LoadSoundBank` | `.mh` metadata reader enabled under DT-11; focused reader and cold-boot checks pass | Integrated retirement gate, `.mb` sample residency, streaming and voices |

The enabled header binding preserves all 130 frame and 22 audio fixture hashes.
Its frame-120 memory difference is retained as an advisory diagnostic under
DT-11. The reader publishes complete owned header storage without recreating
the removed file/RPC transport writes.
The final full suite passed (474 passed, four skipped, two disabled), including
the Debug/Release lane comparison. All 39 scenario runs are stable, but the
historical DT-8 memory report **fails by 92 raw words against S12**, entirely in
attract, with unchanged frame/audio first-divergent keys. The enabled library
replacements are accepted by DT-11 at their measured scope. Continue
integration at game/library boundaries under the retirement gate; do not add
hardware fidelity or dead transport writes to match memory. Evidence is in
[HLE_COVERAGE](HLE_COVERAGE.md#scaffolding-retirement) and the Phase 8 record.
None of these bounded replacements closes a whole hardware model.

Current implementation evidence is in
`<work>/int_work/host_libraries/`. GIF-1 now lowers
complete ST/RGBAQ/XYZ2 or XYZF2 geometry directly in the existing ordered slot.
The affected 6,000-frame attract preview preserves every frame and audio hash,
with 12,041,856 parser bypasses. Other packet shapes, packet construction and
GS state/assembly remain. The sound-bank reader passes the adjusted cold-boot
fixture. DMA-1/VIF-1 work now targets the source-checked flat UI Strip producer,
with typed geometry/material tests and ordered bucket delivery tests passing;
live composition and the combined full gate remain required.

## 3. Shared contracts and milestone dependencies

Title-specific layouts, bindings, material meanings and service policy belong
under the title. Reusable file, decode, sound, job and render mechanisms belong
in the engine. Keep the existing game algorithms and useful backend operations;
retirement concerns the executing dependency, not the directory name of reused
code. Recompiled gameplay can coexist during migration. Removing every generated
gameplay function is not implied by retiring its platform services.

Use simple component names, without `Native` or `Platform` prefixes. The current
implementation names are `MathLibrary` (`hle/math/math_library.*`), `MovieDecoder`
(`hle/fmv/movie_decoder.*`) and `GameLibraries` (`hle/game_libraries.*`), relative
to the title directory. The first stream implementation is
`LoaderFileIo::LoadSoundBank` in `hle/loader_sync/loader_file_io.*`.
Proposed owners in this roadmap are `StreamLibrary`,
`Audio`, `Graphics`, `Scheduler`, `Input` and `SaveStorage`. These names describe
responsibilities, not a claim that all components already exist. `Platform::*`
and other decomp names below identify source provenance only; they are not a
naming convention to adopt.

M0 defines the small contracts required by each branch: stable handle plus
generation, owned immutable job inputs, cancellation/reset, logical clock,
completion sequence, error result and checkpoint ownership. Workers return
results; the game execution owner publishes guest memory and invokes callbacks.
Publish data before completion, deliver once, and never invoke EE/IOP continuations
from an arbitrary host worker. A callback must be able to enqueue another request
without holding the service's internal lock. Pausing or waiting must not hold a
lock needed by the worker or renderer that completes the request.

Numbering expresses a useful implementation order, not a demand to serialize
independent work. Dependencies below are **cutover** dependencies; source analysis
and isolated native algorithms can proceed sooner.

| Milestone | Dependencies | Deliverable and done criteria |
|---|---|---|
| **M0 — boundary and service contracts** | None | Maintain this static ledger; verify an NTSC binding before enabling it. Define ownership, completion, errors and persistence across the complete service/graphics contracts. Done when composed ordering, pending work and coherent restore satisfy those contracts without introducing another hardware model. |
| **M1 — explicit macro math** | M0 | Extend `MathLibrary` from the implemented point transform at NTSC `0x0018e4f0` to every source-enumerated macro/VU0 consumer in scope. Check operation order, aliasing, all result lanes and live register effects. Individual helper fixtures are progress; phase completion requires complete caller accounting, no replaced VU execution by supported callers and the integrated retirement gate. |
| **M2 — Movie lifecycle and pictures** | M0 | Extend `MovieDecoder` from Create/Reset/picture delivery to the complete open/prime/start/end/close lifecycle and remove its remaining generated IPU bookkeeping. Reuse decoder/PCM algorithms behind that interface. Verify two-picture priming, output ownership, abort/reopen and all required asset formats. It may initially use the existing file/presentation provider; that dependency remains open. |
| **M3 — files, stream rings and bank metadata** | M0; M4 for asynchronous cutover | The proposed `StreamLibrary` would own open/size/close/ranged reads, channel buffers, ring/direct destinations and recovery. The first `.mh` implementation is `LoaderFileIo::LoadSoundBank`; it does not require creating that proposed library. Verify the NTSC `0x002ab160` binding and preserve `.mb` handle lifecycle. Done when file intents no longer register or pump STREAM. |
| **M4 — completion and scheduling** | M0; request contracts from M2/M3, not their completed cutovers | `Scheduler` replaces stream/audio heartbeat, kernel waits and service callbacks with ordered jobs on the existing game clock. M3/M4 can be developed together. Preserve poll visibility, deadlines, cancellation, pause/reset and callback context. Done when every service wait/callback in scope makes progress without IOP threads, semaphores or host-delay dependence and passes the composed gate. |
| **M5 — banks, voices, music and effects** | M3, M4 | `Audio` owns sample registry, voice allocation, ADPCM/PCM playback, release, pitch/volume, paired music channels and effects bus; integrate Movie ownership handoff. Done when required controls produce the right audio and status without STREAM/libsd execution, including release and restore. |
| **M6 — ordered graphics submission and resources** | M0 | `Graphics` joins bucket/insertion-ordered intents with legacy spans in one existing render queue. Own upload payloads, target/texture lifetime and completion fences. Done when mixed ordering, shared target coherence and frame reset tests pass; required before enabling Strip in a live mixed frame. |
| **M7 — native primitives and presentation** | M6; M2/M5 for full Movie presentation | Begin with verified untransformed/untextured Strip, then its other overloads, sprites, sky copy-out, frame conversion and movie upload/draw/present. Each slice removes its producer and transport. Done only after depth/alpha/filter/quantization and pacing tests, including legacy/native crossings. This can retire MOV-3 without rewriting every world draw. |
| **M8 — geometry and VU arithmetic** | M0, M1, M6; M3 for native asset ownership | Typed rigid/placed/skin/blend/particle/decal inputs replace packet production and program dispatch. Reuse established arithmetic, reconstruct missing program semantics statically. Done when every required source-enumerated family directly emits owned vertices/draws without VIF, VU interpretation or raw GIF kicks, its callers are accounted for, and the integrated retirement gate passes. |
| **M9 — special materials and screen passes** | M6; M8 for deformations/expansion | Retire cloth, distortion, wave, blur/depth/filter, font and shadow producers with explicit native render passes. CPU parameter generation is not completion of a microcode-dependent shader. Done when every supported material family has a native owner or a source-backed unreachable disposition. |
| **M10 — input and persistent game saves** | M0, M4 | `Input` supplies pad reports/rumble; `SaveStorage` retains game formats, errors and NTSC paths through asynchronous storage. Test disconnects, pressure/motors, busy states, missing/corrupt saves and failed writes. Done without PADMAN/MCMAN/MCSERV service execution for those intents. |
| **M11 — startup and service cutover** | M2–M5, M7–M10 | `GameLibraries` wires providers while preserving constructor/allocator/registry initialization, settings, media paths and game start order. Instantiate providers instead of rebooting/loading IOP modules. Audit all remaining module/heap/RPC users first. Done when boot, reset and shutdown have no required platform-module execution, with explicit failures for unsupported services. |
| **M12 — checkpoint closure and deletion** | M1–M11 | Finish service-owned runtime save/reproduction support and caller accounting; remove obsolete bindings, transports, models and serialization dependencies. Parent runs the available integrated gates under DT-11/DT-12 and records unavailable historical fixtures. Done only with no supported intent relying on the retired path; do not claim graphics playtest coverage before the user's later pass. |

Persistence is a dependency of **every** slice, starting at M0; M12 is the final
cross-service check, not permission to postpone save support. M1, M2, M3, the M6
queue work and M10 static binding work can proceed in parallel. M8 microcode
analysis also proceeds immediately, independently of runtime capture availability.

## 4. Movie, Stream, Audio and scheduling contracts

Movie semantics are explicit in `TR:include/platform/movie.h:25` and
`TR:src/platform/ps2/movie/player.cpp:479`: construct, begin presenting, open,
prime, start sound, step, wait, queue, draw and close. `Step`'s bounded queue and
the draw path's initial picture threshold are part of the behavior, as are
cropped rows, lent-memory return, callbacks and return to gameplay. Preserve
these while reusing `MpegApi`, `MpegContext`, `FfmpegVideoDecoder`, `FrameWriter`
and PCM/pacing algorithms behind the new owner. An internal adapter that still
calls RestartDMA/StopDMA is an open dependency. Determine required compressed
and output formats from actual asset/caller sets; do not implement unused IPU
commands to make the checklist look complete.

`TR:src/platform/ps2/stream.cpp:141` specifies file naming/handles; `:188` ranged
reads; `:197` sound-bank destinations. `TR:src/game/filestream.cpp:396` defines the
ring consumer, including its held prefix; `TR:src/game/readers.cpp:20` identifies
the second ring. Test offset/length overflow, zero and short reads, wrap across
both rings, held-prefix straddles, overlapping requests, close while pending,
stale completions after reset, error/retry and direct destination bounds.
Do not turn the PAL disc check string into an NTSC path by assumption.

`TR:src/game/filestream.cpp:634` loads a `.mh` allocation, replaces its previous
header and separately closes/reopens `.mb`. The first implementation is
`LoaderFileIo::LoadSoundBank`, not a new `StreamLibrary`. Metadata completion must
preserve allocator ownership, missing-header behavior and the sample file
handle contract. Loading a header is not loading a sound bank into the mixer.
`LoaderFileIo` still registering/pumping STREAM is the specific dependency to
remove; reuse sector/file access through `ReadEngine` where useful.

`TR:include/platform/audio.h:20` and `TR:src/platform/ps2/audio.cpp:17` specify
reset/pause/resume, bank/voice controls, music and Movie lending. Native services
must preserve voice exhaustion/error results, loop/end positions, phase-aware
volumes, pitch units, envelopes and release, group gain and bank lifetime.
Reuse `engine/sound/vag_decoder.*`, `voice.*`, `mixer.*` and `fmv_pcm_input.*`
as algorithms; do not retain guest SPU addresses as native pointers. Music
read/prepare/ready/play/playing/stop and parent/child channel interleave are
distinct states (`TR:src/game/sound.cpp:656`). Reverb no-ops do not establish
effect completion. Test pauses at preload/play, paired-channel starvation,
bank unload during playback and Movie-to-game audio handoff.

RPC command meanings in the canonical inventory help account for remaining
consumers; they are not a reason to replace all SIF calls with success. Keep
reply adapters only while generated consumers require them. M4 preserves the
settled logical game clock: `TR:include/platform/time.h:9` describes the time
unit, but PAL timing is not an NTSC scheduling specification. Keep current
NTSC boundary/poll visibility and presenter pacing. Do not revive the deferred
IOP exact-clock conversion or introduce host-time deadlines for game logic.
Test simultaneous completions, callback reentrancy, pause/reset races and waits
whose producers need the same execution thread.

## 5. Concrete graphics route and smallest first slice

### Existing consumer path

The native destination already exists:

```text
title operation -> owned native intent -> ordered bucket merge (M6, proposed)
  -> DrawIrSink -> existing recorder / DrawIrThreadQueue
  -> DrawIrQueueConsumer -> DrawPipeline -> Backend::Draw
```

`engine/gs/hle/ir/draw_ir.h:25` is an explicit draw record, not a requirement to
run `GsState`. Supply its raster, texture, primitive, vertices, bounds and
identity fields deliberately. Reusing GS-named numeric formats does not require
emitting register packets. Shared targets must pass through the existing
`DrawPipeline::Prepare`/`Finish` ownership and alias handling
(`engine/gs/hle/classrdr/draw_pipeline.cpp:95`). Calling the backend directly
from the game thread skips that handling and can use the wrong graphics context.

`engine/render/backend.h:162` already supplies surface creation, upload/mips,
draw, stencil operations, copies and presentation. Native upload commands need
**owned payloads attached to their ordered queue item**. The existing
`DrawIrThreadQueue` obtains a legacy upload's payload after its event; inventing
a `JobRec` without replacing that dependency is insufficient. Frame target and
presentation metadata likewise need explicit snapshots.

### Verified NTSC candidates

| Intent | PAL -> NTSC | Target evidence and remaining restriction |
|---|---|---|
| `VuTransformPoint` | `0x0018ec80` -> **`0x0018e4f0`** | `recompiled_shard_009.cpp:16279`: a0/a1/a2 are matrix/point/output, all matrix rows loaded before output; translation uses VF0.w, not input.w. Agent owns implementation. |
| `Strip::Draw()` | `0x001a98b0` -> **`0x001a9120`** | `recompiled_shard_011.cpp:17249`: material +0, byte count +0x10, positions +0x14, colours +0x18; render target at NTSC `0x0030983c`. M6 blocks live cutover. |
| `LinkRenderBuckets` | `0x0017f460` -> **`0x0017ed50`** | `recompiled_shard_008.cpp:20948`: bucket linking and submission match the source. This is an ordering integration point, not permission to add a DMAC mode. |
| `FinishSky` | `0x001ba6c0` -> **`0x001b9f30`** | `recompiled_shard_012.cpp:20061`: 32-pixel copy strips and explicit state. Native copy-out is separable from sky geometry, but includes depth writes. |

Generated files above live under
`games/<title>/recomp/`. The ledger records map rows too.

The first graphics slice is the **untransformed, untinted, untextured case of
`Strip::Draw()`**, specified by `TR:src/game/shapes.cpp:275`,
`TR:include/game/shapes.h:79` and
`TR:src/platform/ps2/renderer/draw2d.cpp:163`. Its minimal proposed implementation:

```text
verify supported material and complete guest ranges before side effects
snapshot target + material/shader state + positions + colours
convert to the draw contract's coordinates and depth using source arithmetic
expand strip into owned triangles, retaining parity through degenerates
append an owned DrawIr at this bucket's exact insertion position
return through the checked NTSC ABI
```

Those are proposed title helpers, not existing API names or a new retail symbol
called `Platform::Graphics::DrawStrip`. No VU microcode, DMAC/VIF/GIF encoding or
`GsState` construction is needed for this slice. Unsupported material cases
must stay on the explicit existing path before any native side effect until
their semantics are implemented; they remain open retirement rows.

The backend draws **triangles**, even when `DrawIr::prim_type` describes a strip
(`engine/render/backends/gl/gl_draw_executor.cpp:91`). Expand alternating
`(v0,v1,v2)` / `(v1,v0,v2)` with the newest vertex last, preserving flat-colour
provocation. Degenerate positions still consume strip parity. The existing
`VertexKick::AssembleStrip` is useful reference behavior, not a service to call
from native rendering. Quantization matters:
`TR:src/platform/ps2/renderer/draw2d.cpp:96` sign-extends integer coordinate
packing; negative inputs cannot simply become unclamped host floats. Derive the
effective coordinates and depth statically, with fixtures for offsets and signs.

Required first-slice tests: counts 0/1/2/3/4 and maximum byte count; odd/even
triangles and degenerate restart/parity; distinct vertex colours; negative and
fractional coordinates; target offsets/scissor; blend/alpha/depth/masks; owned
inputs after guest mutation; two materials in one bucket; native/legacy/native
and legacy/native/legacy interleaving; bucket order differing from call order.
The tint/transform overloads have separate NTSC candidates in the ledger and
must not be claimed by the first binding's success.

### Ordered mixed queue is a blocker, not an optimization

`TR:src/platform/ps2/renderer/buckets.cpp:205` submits in bucket-chain order,
not game-call order. `BeginPacket`/insertion can also reorder within a bucket.
An immediate `OnDraw` at the Strip call, or appending native draws after the
whole legacy frame, is incorrect. `DrawIrSink` has no such merge operation today.

M6 must associate each owned native item with the frame, bucket and actual
insertion anchor, then traverse the resulting order. Existing legacy spans must
finish producing/flushing their draws before the following native item reaches
the same sink. Snapshot items before `StartFrameBuckets` reuses storage. Preserve
render-thread ownership, completion fences, shared targets and texture aliases.
Native resource operations must participate in this same order.

Use title-owned ordering metadata and a generic submission interface, not fake
GIF opcodes, title-specific DMAC modes or deeper hardware execution. Prove safe
legacy span boundaries using the existing path. If that cannot be done for a
bucket without new model fidelity, replace its complete producer family first;
do not enable the isolated Strip hook early. This allows a bounded bucket
migration without requiring a whole-frame rewrite.

`RenderBucket::last2DMaterial` caches **legacy emitted setup**
(`TR:src/platform/ps2/renderer/draw2d.cpp:48`). A native draw must not tell the
next legacy packet that GS setup happened when it did not. Crossings require a
consistent legacy cache and complete native state. A later reread of mutable
guest material data is not a snapshot.

## 6. Bucket, material and VU work breakdown

Bucket identities come from `TR:src/platform/ps2/renderer/renderer.h:431`.
The chain starts are 0, 5 and 21; indices 1, 5 and 25 have no named family in
that enum and must not acquire invented semantics.

| Buckets / family | Source-defined producer | Native operation and remaining semantic work |
|---|---|---|
| 0 sky | `sky.cpp:82,116,149`; `materials.cpp:280` | Half-size target, sky geometry and nearest copy-out. `FinishSky` also writes depth; plain colour `CopyRect` is not equivalent. Geometry depends on M8. |
| 2 opaque; 3 global opaque; 6–19 blended | `models.cpp:291,393,537`; `skins.cpp:91,151,313`; `materials.cpp:50` | Typed instances, lights, joints, blend shapes and ordered material passes -> native vertices -> DrawIr. Preserve material insertion, shader order, clipping, fog and alpha/depth tests. |
| 4 screen effects | `effects.cpp:268,296,446,507` | Explicit depth-copy, palette/filter, half-size blur and wave passes. Blur/depth/alpha and half-texel sampling preclude assuming every pass is a colour copy. Wave type 0x1C still needs its program semantics. |
| 20 decals | `particles.cpp:340`; `shaders.cpp:579` | Native view selection, clip/expansion and textured draws. Decal VU0 set and VU1 type 0x13 remain separate dependencies. |
| 21 shadows | `shadows.cpp:55,207,294` | Explicit shadow geometry and stencil/composite recipe; reuse `StencilCompositeRenderer` and backend stencil primitives where equivalent. Preserve half-size target, alpha and depth/mask behavior. |
| 22 particles | `particles.cpp:22`; `shaders.cpp:574` | Typed page/block inputs, view/culling and particle expansion; separate textured-particle and hexagon layouts and block limits. |
| 23 distortion | `shaders.cpp:904`; `particles.cpp:333` | Ordered framebuffer copy plus distortion geometry/material. Type 0x18's parameters alone do not define all expansion/texture behavior. |
| 24 UI | `draw2d.cpp:114,163`; `shapes.cpp:275` | First Strip slice, then tint/transform/sprites and other shape callers. Primitive arithmetic is available without microcode; ordering and material state still matter. |
| 26 fonts | `shaders.cpp:505` | Glyph/sprite intent and texture resource ownership. Type 0x0D consumer is identified; its raw program/output layout is not established merely by a family stamp. |
| 27 movies | `movie/player.cpp:589,605` | Ordered owned frame upload and cropped draw, followed by presentation/completion; M2/M5 supply playback and audio. |
| Frame head / scanout | `frame.cpp:81`; `graphics.cpp:548` | CT24-to-CT16 conversion, dither, masks and 32-pixel strips, then explicit present and pacing. `Backend::Present` does not by itself perform the frame-head conversion. |

Renderer paths in that table are `TR:src/platform/ps2/renderer/` unless prefixed
with `movie/` or `graphics.cpp`, which are under `TR:src/platform/ps2/`.
`shapes.cpp` is `TR:src/game/shapes.cpp`.

Material/shader contracts are in `renderer.h:518` and `:731`; target layout in
`TR:include/game/controllers.h:20`. Decode the guest fields explicitly, including
blend preset/custom factors, alpha test/reference/fail, destination/depth tests,
gouraud, texture/STQ, fog, filtering and depth-write flags. Do not cast guest
memory to a host compiler's bitfield struct. Implement semantic material
descriptors; do not obtain missing state by executing register writes.

Texture ownership starts at `textures.cpp:142` and its `Texture` layout. Decode
asset formats, palette/mips and upload data into stable native resources.
Preserve lifetimes across scene unload and outstanding draws. Legacy VRAM slot
identity can remain an adapter key while mixed consumers exist; it is not a
native resource allocator requirement.

VU work must track **program set/image plus entry**, never entry alone:

| Arithmetic family | Static source | Remaining action |
|---|---|---|
| Macro point/rotate/matrix helpers | `TR:src/platform/ps2/matrices.cpp:7,26,139` | Full instruction sequences are visible. Port independently with alias-safe input snapshots and explicit operation order; audit remaining live register consumers before dropping effects. |
| Standard VU0 math | `TR:include/platform/math.h:11`; `TR:src/platform/ps2/math.cpp:29` | Sin/cos 0xF0, slerp 0x1C0, Euler 0x818, turn 0xA70, joint 0x548/0x668 and parent 0x790. Wrappers specify operands/results; called microcode is still required for exact arithmetic. Existing polynomial is not blanket bit-identity proof. |
| Standard VU0 collision | `TR:include/platform/math.h:59`; `TR:src/platform/ps2/math.cpp:294` | Ray/triangle 0xAB8 has set/start/finish lifetime and nearest-hit state. Replace the complete operation, preserving result timing for its callers. |
| Culling VU0 set | `TR:include/platform/math.h:42`; `TR:src/platform/ps2/math.cpp:81` | View distance 0xCB0 and particle block view 0xA08/0xB90 depend on loaded view inputs. Recover the programs' exact culling/transform arithmetic statically. |
| Decal VU0 set | `TR:src/platform/ps2/renderer/particles.cpp:340`; `vu0programs.cpp:56` | Copier entry 0 plus same/other-chunk entries 0x1C0/0x360. These are decal views, not standard-set slerp. Replace staging with owned arrays and the view operation; historic census names are not authoritative semantics. |
| Shared VU1 and family programs | `TR:src/platform/ps2/renderer/vuprograms.cpp:117,187,541` | Resident/shared parameters, image registration and material-specific programs need typed inputs and direct outputs. Match NTSC code/data images; reuse existing family arithmetic only after layout/behavior checks. |
| Blend shapes | `vuprograms.cpp:190`; `skins.cpp:151,193` | Parameter/no-shape/first/next-shape programs and accumulation order remain an explicit source/microcode task. Do not declare skin completion from the non-blended case. |
| Cloth 0x17/0x1A | `shaders.cpp:261,612,618`; `shadertypes.cpp:612,659` | Phase/wave/amplitude parameter generation is visible; deformation program arithmetic is not supplied by those packets. Check both with-registers paths and documented retail packet defects against supported callers. |
| Distortion / special shaders | `shaders.cpp:466,505,585,904`; `vuprograms.cpp:541` | Account for 0x0B, 0x0D, 0x0E, 0x18, 0x1C and every registered/material-selected image. Known names or a shared stamp do not prove full semantics. Unknowns block family retirement, not unrelated slices. |

Bare renderer filenames in the VU table use the same renderer root. Program
addresses above use the source's entry notation; check instruction-address
conversion and loaded set at each NTSC call. `vu0programs.cpp:23` names three
split `.vutext` sets; `vuprograms.cpp:187` names VU1 code blobs and sizes.
Record asset/image identity and source-to-NTSC correspondence before replacing
them. Source limitations are concrete work items, not a demand for a new VU
interpreter or runtime discovery captures.

Existing native VU families still do not close graphics transport:
`engine/vu/hle/kick_lowering.cpp` lowers eligible ST/RGBAQ/XYZ geometry, with a raw
GIF fallback for other shapes. Refactor arithmetic outputs into explicit vertices/material
inputs at their game producer. Do not extend raw kick generation and call that
retirement. Classifier/stamp matches are routing evidence only; small sprites
can route to `RawSpritesHud`, and a batched strip bounding box can be fullscreen.
They are not a substitute for the material/producer ledger.

## 7. Input, saves, startup and persistence

Input uses the report/state contract in `TR:include/platform/pads.h:89` and game
callers `TR:src/game/pads.cpp:402,633`. Bind verified NTSC owners, retain active-low
buttons, sticks, pressure, disconnect/stable/request states and rumble sequencing,
and source reports from the existing host input/replay provider. Keep input
sampling on the game boundary; do not expose host event arrival timing to logic.

Game saves are ordinary persistent game data, distinct from runtime checkpoints.
`TR:include/platform/saves.h:61` and `TR:src/platform/ps2/saves.cpp:488` define the
asynchronous single-operation API and its result/error states. Keep current game
serialization, slot metadata and NTSC region/product naming. A host save provider
should commit complete writes atomically and preserve the preceding save on
failure; test failure injection and reload, not just successful byte output.
Completion visibility still follows M4. No new memory-card firmware is needed.

Startup is specified by `TR:src/main.cpp:37` and
`TR:src/platform/ps2/system.cpp:53,64`: constructors and game services must still
be initialized before the main loop. Replace module reboot/load only after
Stream, Audio, input, saves and Movie have native providers. Inventory remaining
IOP heap aliases, callbacks and module handles; retain narrow adapters with
explicit ownership until their last consumer is ported. Do not make unknown
module loads universally succeed or replace essential initialization with no-ops.
Keep NTSC timing, content lookup and startup settings distinct from PAL constants.

Every native service must define runtime checkpoint state:

| Owner | Persist/reconstruct |
|---|---|
| Clock/jobs | Logical time, ordered pending completions, deadlines, generations and delivery state; no host thread, lock or fibre pointers |
| Stream/banks | File/content identity, offsets, ring contents/cursors, outstanding read ownership and bank handles; reopen/requeue with once-only delivery |
| Audio | Voice/sample identity, decode/loop/envelope cursor, music channel relationship, pause and Movie ownership |
| Movie | Session/asset identity, consumed picture/audio position, queue and callback state; explicit deterministic decoder reconstruction or named quiescence refusal |
| Renderer | Resource/content identities, logical target state, pending ordering/fences at a supported boundary; recreate host surfaces instead of saving GL IDs |
| Input/saves | Pad/replay position and motors; pending storage operation and durable commit state, without replaying a completed write |

Use `engine/core/savestate/quiescence.h` and
`engine/core/savestate/section_registry.h` for named capture refusal, versioned
ownership and validation before mutation. Do not silently discard required
IOPM or graphics/decoder state when deleting models. Provide a tested migration
for supported old states or an explicit compatibility rejection, retaining any
compatibility needed by standing gates. A new refusal cannot silently reduce
the existing supported save points. Test fresh-process restore as well as
same-instance restore, corrupt/missing sections, stale handles and cross-service
pending completions. Preserve existing pad and memory-card section compatibility
until their replacement is accepted.

## 8. Product and performance schedule

The recorder publishes the same boundary sample supplied to the game. The host
owns one incremental `input.padlog` writer, flushes held ranges every 60 logical
ticks and at orderly exit, and records the first/last input tick and line count
in `run.meta`. The grammar uses logical input ticks, not present/frame-hash keys.
The two sets of keys can differ during movies. A diagnostic census flush does
not stop recording. A clock rewind requires a new recording segment; it must
not append ambiguous time ranges to the existing file.

The exact two user-session commands and output locations are in
`games/<title>/verify/pads/README.md`. Record on NEW, keep
the binary identity and settings with each leg, and retain in-game save data.
No synthetic input run is counted as one of the user's sessions. Their recordings
join the standing 13 legs only after the user has actually played them.

Product work continues alongside M6/M7: window-close handling and F1 settings,
F5 reproduction save, F8 restore, F9 pause, F10 single-step, with the GUI using
the same command queue as hotkeys. Apply commands on the game owner at a safe
boundary; show pending/refused/failed results. Reproduction saves must honor
service quiescence and remain distinct from persistent game saves. UI capture
must discard game input edges and clear camera motion. Resize belongs to the
presenter; it must not change logical render-target dimensions.

Immediately after M7 closes, freeze a Release binary and pause sibling builds
and game runs for the measured performance round. Run
`verify/bench/run_bench.py` against L's `verify/bench/BASELINE.tsv` using its
documented instrumentation and idle-load criteria. Record per-leg delta,
median/tail frame time, build/settings and actual model census. Then measure
OLD's existing Release binary with matching instrumentation; never build OLD.
L's headless GL/readback/hash numbers and interactive play numbers are separate
series. After attribution and optimization, measure 60 fps in-level on the
NEW-recorded sessions, including audio continuity and input responsiveness.
No performance delta is claimed before these measurements.

## 9. Closure and validation

A boundary is done only when its complete caller continuation, normal/error
results, lifetime, pause/reset and supported save behavior have native owners.
For each enabled slice, require independent arithmetic/layout/state fixtures and
a practical caller-level regression. Validate visual/audio output and guest
effects; a mocked counter increment is not enough. Compare old/native outputs
on controlled inputs without using the new implementation as its own oracle.
Microcode fixtures can come from static instruction analysis and existing
references; captures/replays can supplement them.

The parent owns builds and runtime validation. Start with focused tests for the
slice and representative affected scenarios; shared scheduling, resource or
submission changes require their dependent scenarios too. Final integration
requires the standing full suite and 13-scenario, three-repetition gate with
unchanged pairing, baseline, masks and attribution rules. Frame/audio
first-divergent keys, PCSX2 rendering pins and stability decide retirement
acceptance; memory hashes and word counts remain advisory under DT-11.
A smaller subset is feedback, not final gate clearance.

For every canonical retirement row, record native work performed and either
static unreachability of the replaced continuation or zero old-path calls in the
relevant validation scenario, alongside nonzero native work. Scenario absence
alone never proves whole-game absence. Extend static accounting to indirect
callers, recovery, optional assets, alternate materials and startup/restore paths.
Retained fallback paths stay named and open.

| Dependency to remove | Final condition |
|---|---|
| IPU/libipu/IPU DMA | All supported Movie formats, lifecycle, upload/draw and callbacks native; no generated decoder continuation or snapshot/status bridge |
| DMAC/VIF/GIF and `GsState` production | All supported bucket producers, uploads, frame copies and presentation native; ordered mixed queue has no legacy spans; no remaining buffer-copy intent starts these transports |
| VU interpreters/program dispatch | Every live macro/program-set/image intent has native arithmetic and direct output; no residual raw GIF lowering |
| STREAM/IOP kernel/libsd | Files, banks, voices, music/effects, recovery and lifecycle use native owners and completion; no module pumping, import, semaphore or heartbeat dependency |
| Pad/memory-card module services | Native reports/rumble and durable save operations serve every supported game caller |
| Legacy persistence and bindings | No supported state/caller still needs the removed owner; compatibility and fresh-process restore accepted before deleting its sections and registrations |

Scratchpad DMA's zero starts in the prior census are bounded evidence, not a
global closure claim. If static inventory finds a staging/copy user, retire
that buffer operation with owned native memory; do not reopen destination-chain,
interleave, MFIFO or other hardware mode work. Final deletion is justified by
consumer accounting and unchanged regression gates, not by the number of new
native helper functions.
