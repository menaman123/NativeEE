# HLE coverage audit vs the the title-reversed decompilation (P8.37-E, 2026-10-06)

**Current acceptance (user, DT-12):** graphics and services proceed in
parallel. Every replacement needs a same-input NEW self-oracle/toggle,
13-leg ×3 frame/audio stability with non-regressing first-divergent keys,
zero execution of its replaced model and OLD-bug pins on the replacement.
Memory is advisory. PCSX2 is not used or required in this wave; existing pins
remain unchanged and excluded from acceptance. It returns only for
user-reported play bugs through content-aligned states or NEW-pipeline replay.
Historical PCSX2 evidence below is a record, not the current gate. The
missing user save does not block implementation. Commit each approved stage;
report census, measured frame time and open items for user Release playtests.

**Current design rule (user, 2026-10-07; DT-9):** the goal is an OpenGOAL-style
native the title port. DMAC/VIF/GIF, the IOP kernel and STREAM.IRX,
and the IPU command interface are **scaffolding to retire, not deepen**.
When a gap or defect touches one, HLE the game's intent using the decomp's
platform layer, bucket-chain meanings and RPC-command meanings so that the
model's code path stops executing. The [retirement table](#scaffolding-retirement)
below is the active worklist. The SDK/VU/loader census remains evidence of the
current implementation, not a mandate to complete PS2 hardware.

**Source-driven implementation plan:** [PORT_PLAN.md](PORT_PLAN.md) sequences
the work; [library_boundaries.tsv](hle_coverage/library_boundaries.tsv) maps
the source contracts and verified/candidate NTSC boundaries. The reviewed
decomp commit is `38d533d0033b48b5d6b49880c49f5eeeade0f38e`, checked against
the linked repository's HEAD. Discover operations from source, callers,
registrations and the shipped microcode; use captures to validate results.
Some C++ math wrappers still call VU microcode and do not contain its algorithm.
Names in our code describe the job (`MathLibrary`, `MovieDecoder`, `Graphics`);
decomp `Platform::*` names are specification citations only (DT-10).

**What this is.** The whole-game gap list of our HLE layer, measured against the complete C++ decompilation of the PAL build
(`<projects>/the title-reversed`, SLES-52568; READ-ONLY; a SPECIFICATION — no code copied, names used
as names, every rule cited as `TR:<path>:<line>`). Our title is NTSC-U (SLUS-12345): the same codebase, a different build, so
every PAL address is mapped through the new `games/<title>/symbols/pal_ntsc_map.tsv` (section 6) before use.
The original audit was an evidence stage: no engine change, no rename, no commit.
Its data tables live in `docs/design/hle_coverage/*.tsv`; the active implementation
direction is now the retirement table and the revised order in section 7.

**Original audit headline (before the library replacements below).** `sdk_surface.tsv`: 298 title manifest rows → 96 implemented + 6 wrap + 13 recompiled + 33 stub + 11 park +
**139 trap rows — 131 installable + 8 at inner labels the binder cannot hook** (the engine's own count: `tests/ee/test_syscall_table.cpp:290` with builtins only is 77 handlers; the live run with
every family bound installs 286 rows, 139 trap, 12 not-function-starts — `int_work/p834a/leg_on700/run.meta`
`sdk_rows_installed=286 sdk_rows_trap=139 sdk_rows_not_fn_start=12`). **Zero trap rows were reached in 700 frames** (the hit
list names 76 functions, every one an engine row). Of the 131 installable traps, 70 are libmpeg internals shadowed by the HLE'd outer
entry points (`sceMpegGetPicture`/`DemuxPssRing` run; the retail decoder's inner functions never do), 53 are libkernl (the T2
timer library, the SIF-RPC internals shadowed by the HLE'd `sceSifBindRpc`/`CallRpc`, deci2/TTY, `DelayThread`), 2 libscf
date helpers (`addhour`/`subhour`), 4 libdev VU pauses TR marks dead, `PowerOffCB` (libcdvd), `setD3_CHCR` (libipu) = 70+53+4+2+1+1 = 131; the 8 remaining "trap" rows (`iWakeupThread`, `iSetTimerAlarm`, `adddate`/`subdate`, `_sceMpegWaitIpuIdle`-class inner labels) sit at inner labels of a discovered function and are never hooked — the generated body runs (§1). The **gameplay-relevant
gaps include fileio parks/traps** (`sceOpen/Close` park; `sceRead/Write/Lseek` trap → TR's shadow tables fall back to a fileio `File::Open`,
TR:src/game/shadows.cpp:806-812) and the **timer library + `DelayThread` traps**. VU1: 12 program names / 176 variant hashes
cover the strip, skin, displace, particle, sprite and crystal families; **the cloth (0x17/0x1A), distortion (0x18) and the
blend-shape programs have no family** (an unknown image runs on the VU interpreter tier, not a park). VU0: 14 of 15 census
rows are `Kind::Unimplemented` (interpreted, unvalidated); TR names two of them differently (0xAB8 = ray/triangle, 0x1C0 =
same-chunk particle view) and the **other-chunk particle view 0x360 is absent**. GS: of TR's 28 buckets, the opaque/blended
material strips, skins, stencil shadows (bucket 21), fonts and the FMV route to named classes; **the screen effects (bucket
4), decals (20), textured particles (22), distortion (23), the sky copy-out (0) and the UI strips (24) draw through the generic
fallback**. Loader: every rule TR documents for the file streams, the readers, the disk manager and the STREAM client is
modelled or is the title's own recompiled code; TR **confirms** our ring-cache model (`FileStreamRead`, TR:filestream.cpp:396-520),
names the "second ring" (the FILE readers' 0x10000 stream, TR:readers.cpp:20) and gives the headroom predicate's exact
arithmetic (TR:clock.cpp:209-232: a T0 time-left query, slack 0x4E — DP-8 (1) stands). PAL→NTSC map: **8,472 PAL functions,
7,723 high (91.2%) + 233 seed (2.8%) + 122 medium (1.4%) + 345 low (4.1%) + 49 none (0.6%)**; all 30 plausibility samples
(our curated names) land on the semantically matching TR function.

## Scaffolding retirement

The per-model deletion ledger (source files, tests, the oracles that still need
each model, owner, measured resident bytes) is
[M12_DELETION_LEDGER.md](M12_DELETION_LEDGER.md) (track 3b, 2026-10-11).

This table records **remaining dependencies and partial replacements**,
not whole-model retirement. `Observed` means the cited run reached the path;
`static` means a source caller exists; `unobserved` is limited to the captured
scenarios. A partial HLE that still calls a model has not retired that model.
`TR:` paths are relative to the read-only decomp root named above; other source
paths are relative to this repository. PAL source establishes semantics, not
NTSC hook addresses.

| ID / scaffolding | Game intent still reaching it | Current path and evidence | Native replacement and removal condition |
|---|---|---|---|
| MOV-1 — IPU command interface; IPU DMA | Open/reset a movie, prime playback, deliver pictures and close | **Accepted active path plus default-off complete lifecycle.** `hle/fmv/movie_decoder.cpp` owns Create (`0x2b70a8`), Reset (`0x2b7cb0`) and DecodePicture (`0x2b01e8`) in the control arm (DT-11). Default-off `hle.movie_lifecycle` additionally owns Initialise `0x2b0d30`, Start `0x2afc00`, Finished `0x2b0f68`, Close `0x2b1000`, FeedAudio/SetVolume, stream seek/read, demux and the two audited ring start/stop calls; the selected software constructor takes no `LibipuApi`/DMAC reference (`39ecb32`). `MovieLifecycleOracle` compares the retained NEW bodies (init=2, stop=3, BCLR=4) with the selected arm (all zero) on identical input/picture/PCM/clock bytes. `ipu_operations.sync/restart` have no live caller in either arm (`MOVIE_ACCEPTANCE.md`). | Parent same-binary pair, composed preview battery and 13×3 gate per `MOVIE_ACCEPTANCE.md`; then flip `hle.movie_lifecycle` and delete the libipu/libmpeg model after RETIRE-ALL. Keep software callback/EOF ABI data only while generated consumers need it; no decoder-to-IPU status mirroring. |
| MOV-2 — remaining IPU decode/conversion branches | Decode compressed pictures and provide the requested output format | **Source-disposed format set.** `movie_format_audit.md` read all 22 packaged PSS files (958,234,712 bytes): MPEG-2 at 512×288 / 512×448 / 640×480, frame-rate code 5, PCM 48 kHz stereo 512-byte interleave. The NTSC player hard-selects MPEG-2 (`0x2ae39c`) and RGB32 (`0x2ae3e8`). RGB16 has adapter and writer tests with no live asset. Non-MPEG-2 FDEC/IDEC conversion helpers `0x2b0660`/`0x2b07d8`/`0x2b0990` have no packaged asset or source caller; selected owners refuse rather than delegate an opaque ID to them (`TR:src/platform/ps2/movie/decoder.cpp:746`). | No further format work is required for this title. Retire the unused branches with the libmpeg model after RETIRE-ALL. Do not implement BDEC, full IDEC, FIFO scheduling or IPU interrupts to close this row. |
| MOV-3 — GIF/DMAC/VIF movie transport | Advance a picture queue, upload the picture, draw its display area and wait for presentation | **Default-off owned producers; presentation transport retained.** `hle.graphics_movie` (`MovieGraphics` + M7 `MovieOutput`) owns DecodeAndUpload `0x2b0e50`, QueuePicture `0x2af968` and Draw `0x2aeb30` as immutable image leases and bucket-27 draw IR; `hle.movie_step/start/wait/stop` own Step `0x2aea08`, Start `0x2ae318`, the live WaitFrame wrapper `0x2af8f0` and Stop `0x2ae658`, removing the Movie `sceGsSyncVCallback` registration. `GraphicsMovie` still executes the retained PresentFromInterrupt `0x1a00d0` / Present `0x1a0150` transport and publishes `graphics_movie_transport` (`TR:src/platform/ps2/movie/player.cpp:553,589`). | M7 native presenter and WaitIdle/ResetBuckets owners (interface requested in `hle/fmv/evidence/PHASE.md`); then the composed pair and 13×3 gate. Preserve queue depth, cropped rows, aspect ratio, audio alignment and game callbacks; validate intro, attract, abort, reopen and return-to-game behavior. |
| DMA-1 — DMAC frame submission | Submit linked render buckets and present a completed frame | **Partial producer cutover; integrated acceptance pending.** `GraphicsService` bypasses the checked flat UI Strip body at `0x1a9120`, creating owned draws without its chain nodes. `GraphicsBridge` joins them at verified existing predecessors; the 6,000-frame preview completes 152 bucket-24 draws in 19 groups without ordering errors. The shared Link body at `0x17ed50`, chain walker used to merge retained producers, and presentation remain. Source: `TR:src/platform/ps2/renderer/buckets.cpp:205`, `draw2d.cpp:163`, `graphics.cpp:548`. | Broaden typed bucket producers and remove the bridge's chain-anchor dependency when each bucket's remaining producers migrate. Preserve resource ownership, completion fences and frame boundaries. No complete DMAC retirement is claimed by this mixed submission path. |
| DMA-2 — scratchpad DMA | Copy or stage game buffers, if a remaining caller starts SPR DMA | **Unobserved in the standing 39-run census.** The copier and unsupported modes remain registered; zero starts do not prove whole-game absence. See `subsystem_completion_dma.md` for source inventory. | Identify the actual buffer owner and copy/transform intent before implementing anything. Use native owned buffers and direct copy/transform operations at that boundary; retire its SPR start and polling path. Do not add destination chains, interleave, MFIFO, stall or global-control fidelity. |
| VIF-1 — VIF decoder/unpacker | Supply geometry, material parameters, joint data and program selection to a draw | **Partial producer cutover; integrated acceptance pending.** Supported flat UI Strips now use `StripDraw`/`MaterialState` and ordered owned DrawIr, bypassing their DIRECT payload construction and decoding. Empty/unsupported/interior entries retain the original body. World geometry still executes `engine/vif/vif.cpp`; `TR:src/platform/ps2/renderer/vuprograms.cpp:117`, `models.cpp:537`, `skins.cpp:313` specify shared-input, rigid-model and skin producers. Recognized VU routines alone bypass program execution only. | Broaden typed geometry/material/joint inputs and retire each packet producer, UNPACK and program-dispatch path. Reuse arithmetic where helpful; the retained ordered bridge and other VIF producers keep the model open. |
| GIF-1 — GIF parser/arbiter | Deliver draw state, texture uploads, copies and rendered vertices | **Partial lowering enabled.** `KickLowering` owns complete PACKED ST/RGBAQ/XYZ2 or XYZF2 geometry and bypasses the parser at an eligible ordered slot. The 6,000-frame attract preview preserves all frame/audio hashes with 12,041,856 lowered deliveries; 1,563,255 other submissions retain raw parsing. Packet construction, arbitration, GS state/assembly and fallback remain. Upload and display-copy specifications: `TR:src/platform/ps2/renderer/textures.cpp:219`, `frame.cpp:81`. Full integrated acceptance remains pending. | Emit typed draws, resource uploads and copy jobs to the existing render queue/backend. Continue eliminating packet producers and state/assembly dependencies per intent; this bounded parser bypass does not retire the entire GIF model. |
| STR-1 — STREAM.IRX; IOP file worker | Open/size/close files, issue ranged reads, poll/wait and publish progress | **Header reader enabled; integrated acceptance pending.** `LoaderFileIo::LoadSoundBank` at `0x2ab160` reads `.mh` headers through `ReadEngine` and publishes complete owned storage, retaining `.mb` open/close. Reader tests and the enabled 130-frame/22-audio-hash cold-boot fixture pass under DT-11; the frame-120 memory difference is advisory. Other file calls still register with STREAM and module-owned waits still pump it. Source: `TR:src/platform/ps2/stream.cpp:141,188`; multistream `streams.cpp:344`, `transport.cpp:459`. File RPC meanings include hex `1F,20,29,31,01(mode 7E),09,49,51`. | File handles, jobs and completion over `ReadEngine`, with owned ring/direct destinations and data visible before completion. Retire each endpoint/worker path after acceptance; do not recreate dead transport writes or bypass every SIF/Ship call indiscriminately. |
| AUD-1 — STREAM.IRX; IOP/libsd calls | Load/register/release sound banks; play/loop voices; change volume, pitch and envelopes | **Observed:** bank load/registration/compaction, play and volume. Other controls are **static**, not all measured. `TR:src/platform/ps2/stream.cpp:197`, `multistream/commands.cpp:743`, `multistream/streams.cpp:166`, `audio.cpp:49`. Bank RPC meanings include hex `25,1C,0E,01(mode 7F),103,28,104`; voices include `39,02,60,03,5C,04`. | Native bank/sample registry and voice service with stable resource handles, ready/availability state and explicit release. Reuse existing VAG decoding and mixing algorithms. Preserve resource lifetime, voice allocation and audible results; remove corresponding module execution and kernel waits. |
| AUD-2 — STREAM.IRX; IOP music/effect service | Preload/play/stop music, loop paired channels, control effects and hand audio ownership to movies | Music path is **static**, per-command execution unverified in retained records (`TR:src/game/sound.cpp:656`); effect-depth and PCM work are **observed**, other controls/handoffs remain static (`TR:src/platform/ps2/audio.cpp:32,64`). Music meanings include hex `01(mode 0/1),2B,2C,2E,2F,05,63`; effects/handoff `13–15,18,19,3B,3C`. Current reverb handlers include no-ops (`engine/iop/hle/libsd/libsd_module.cpp:43`). | Native streaming audio player, effects bus and movie-audio ownership, with ready/playing state, loop/end positions and paired-channel ordering. Validate music transitions, pause/resume, movie handoff and effect behavior; no extra IOP/libsd register fidelity. |
| IOP-1 — IOP kernel, semaphores, interrupts and timer heartbeat | Initialize services/buffers, run stream/audio jobs, pause/resume/reset and deliver completion | **Observed:** initialization/reset/pause and kernel delays in the standing census; resume and some callback/error paths are **static/unverified**. `TR:src/platform/ps2/stream.cpp:34`, `audio.cpp:17`; lifecycle meanings include hex `0C,10,0D,0F,52,07,0A,0B`. | Native service ownership, ordered job queues, game-clock deadlines, buffer lifetimes and completion events. Preserve game-visible ordering and supported restore behavior. Retire worker threads/imports, semaphore waits, heartbeat and IOPM dependencies as STR/AUD ownership moves; do not refine kernel priority, interrupt or clock fidelity. |
| STR-2 — STREAM.IRX/IOP recovery | Report and recover from read/disc errors | **Static, unobserved recovery path.** `TR:src/platform/ps2/stream.cpp:74` and command meanings hex `35,37`; fileio fallback reaches the current park rows. | Native file-service error/recovery state and the required `Platform::Files` operations. Test failure, retry and cancellation results without adding firmware or disc-controller behavior. |
| MATH-1 — VU macro arithmetic and VU0 programs | Transform points, compose joint/pose matrices, test collision and select particle views | **Partial replacement.** `MathLibrary::TransformPoint` replaces the complete NTSC `0x18e4f0` body using ordered CPU arithmetic; VF/ACC writes are a temporary generated-caller ABI. Other macro helpers and VCALLMS programs remain. `TR:src/platform/ps2/matrices.cpp:26`, `include/platform/math.h`; exact program-set distinctions are in `library_boundaries.tsv`. | `MathLibrary` functions with typed operands/results; statically examine microcode where wrappers omit the algorithm. Replace complete game math boundaries and then remove their register adapters when generated callers are gone. A shared entry number alone does not identify a program across uploads. |
| GEOM-1 — VU1 execution and dispatch | Produce rigid/skinned/blended geometry, particles, cloth and distortion vertices | **Partial HLE.** Existing recognized routines still take packet/program data and emit GIF; other uploaded images execute interpreter tiers. `TR:src/platform/ps2/renderer/vuprograms.cpp`, `models.cpp`, `skins.cpp`, `particles.cpp`, `shaders.cpp`. | `Graphics` geometry/material functions that take owned inputs and emit vertices/draws. Close every material family, including statically identified microcode gaps; remove VIF program selection, VU dispatch and raw GIF output from each replaced producer. |
| GS-1 — GS register state, VRAM addressing and display registers | Set render targets/material state, sample textures, blend/test pixels, copy frames and present | **Active.** The current `DrawIr`/backend still receives state derived from the GS model. `TR:src/platform/ps2/renderer/buckets.cpp`, `textures.cpp`, `frame.cpp`, `draw2d.cpp` describe the operations. | `Graphics` owns textures/targets and explicit render state. First implement ordered merging of library draws with retained bucket spans; immediate submission at a draw call is incorrect. Then remove register packet/state construction and GS-memory ownership as their final consumers move. |
| AUD-3 — SPU2 register/voice/RAM model | Decode/play samples, mix voices/music/movie PCM and apply effects | **Active**, independently of IOP retirement. `engine/sound/spu_core.*` still represents chip voices and state; `TR:src/game/sound.cpp`, `src/platform/ps2/audio.cpp` specify game controls. | `Audio` sample handles, voices, streams and mixer/effect buses; reuse VAG decoding and mixing algorithms without SPU addresses/register operations. Validate voice limits, loops, envelope/pitch/volume, handoff and restore. |
| DISC-1 — CDVD controller/delivery model | Locate and read game assets with game-visible completion and errors | **Partial HLE.** ISO parsing and sector reads already serve bytes; controller, delivery and polling state remain callers' dependencies. `TR:src/game/filestream.cpp`, `readers.cpp`, `disk.cpp`. | File/archive providers plus `StreamLibrary` jobs and explicit completion on the game clock. Retain the requested asset formats and read/error ordering; remove sector-controller and disc-timing dependencies from supported callers. |
| RPC-1 — SIF RPC/DMA and IOP heap | Request file/audio/input/save work, lend buffers and receive results | **Active.** Service adapters coexist with module execution and generated command clients. The multistream command meanings identify request ownership. | Direct service calls and ordered completion events with owned payloads/handles. Remove each RPC endpoint, bridge, IOP allocation and DMA copy once its last consumer moves. |
| SCHED-1 — EE kernel, timers and interrupts | Run game/service work, wait for frames/deadlines, invoke callbacks and shut down | **Active.** Generated consumers still use guest thread and interrupt context; IOP retirement alone does not close this row. `TR:src/game/clock.cpp`, platform time/graphics/movie entry points. | `Scheduler` and presenter use the settled game clock, explicit waits and completion ordering. Replace callers before deleting kernel/syscall/INTC/timer state; preserve pause, reset and callbacks without copying hardware timing machinery. |
| INPUT-1 — PADMAN and pad protocol | Read controllers, report disconnects and drive rumble | **Default retained; selected root prepared in port-product.** `hle.input_service` installs 17 complete input entries, uses accepted SDL/main-owner availability, and omits PADMAN registration, pad stamps and the frontend readiness hook. All 65,536 report words match the retained component oracle; 214 startup connection differences explicitly reflect host availability instead of the synthetic timer. Actual per-leg zero census remains unmeasured. `TR:src/platform/ps2/pads.cpp`, `TR:src/game/pads.cpp`. | `Input` owns reports/rumble, coherent connection, pressure, next-boundary setup and INPT v2 state, with bounded v1/PADS import. Parent integration, disconnect/rumble play coverage, frame/audio attribution and 13×3 acceptance precede a default flip or deletion. |
| SAVE-1 — memory-card services | Initialize storage, list/read/write/delete and report save/storage status | **Default retained; selected root prepared in port-product.** `hle.save_storage` installs nine complete save-manager entries plus the verified `mcInit 0x2c5080` startup interface. Startup calls real `SaveStorage::Start`, reports preparation failure, and leaves Main running. MCSERV registration is omitted in that arm. `TR:src/platform/ps2/{saves,memorycard,system}.cpp`. | `SaveStorage` owns durable game files/errors/busy results and SVST; ISCP rejects missing/mixed selected state before mutation. Pending durable mutations defer reproduction capture; restore never rewrites game progress. Actual startup/play, positive save work, zero model census and 13×3 acceptance remain open. |
| BOOT-1 — PS2 startup and subsystem savestates | Initialize the game, own resources, restore supported checkpoints and exit | **Active.** The host still composes hardware models and loads guest/module state; the new libraries coexist with this bootstrap. | `GameLibraries` wires game services directly. Preserve game constructors/data/allocator initialization; move persistence to each service's owned state. Delete model construction, MMIO maps, module images and serialization sections only after all rows above close. Account separately for remaining generated-code/guest-address adapters before claiming a runtime independent of PS2 state. |

**DT-12 comparison selections (2026-10-07).** The table above describes the
default path. New implementation slices are available for measured cutover:

The shared-scene integration and complete-suite findings are recorded in
[stage10](M_INTEGRATION_STAGE10.md); the subsequent component integration,
repaired checks and still-failing sound comparison are in
[stage11](M_INTEGRATION_STAGE11.md). Scene snapshots, immutable Movie output,
type01 mesh producers, texture persistence and music-buffer owners now compile
together. Their remaining root activation/persistence dependencies do not
remove any intent from the retirement table above. The sound replacement
remains opt-in after the repeated frame-212/audio-176 divergence.

| Slice | One-line selection | Replaced work and current acceptance |
|---|---|---|
| MATH-1 macro helpers and collision | `--set hle.math_helpers=on` (default off) | Thirteen MathLibrary and eight CollisionMath complete entries emit CPU results and preserve the temporary generated-caller ABI. Permanent NEW generated-body oracles compare 52,000 macro and 48,000 collision cases with zero context/memory differences or original delegations; the macro rounding oracle adds 208,000 cases. OLD pins are in `hle_coverage/math_bug_pins.tsv` and `collision_bug_pins.tsv`. A 1,500-frame Release comparison preserves every frame, audio and memory hash; seven of eight collision entries execute in that leg. Unsupported collision loop counts and interior continuations remain original paths. The 39-run gate and the remaining statically enumerated consumers are open. |
| MOV-1 lifecycle | `--set hle.movie_lifecycle=on` (default off) | Seven MovieDecoder bindings own initialise/start/finished/close plus the existing picture entries. This arm omits the scene abort-hold wrapper; `fmv_abort_hold_ticks` counts actual old-wrapper execution. Initial audio-fill EOF and queued-playback EOF are distinct. Close releases playback while preserving the delivery clock reused by the next movie. The same Release binary now produces all 1,500 identical frame and 250 identical audio hashes with selection off/on; the earlier frame-212 divergence is fixed. Selected IPU commands and abort-wrapper executions are zero; the control executes four BCLR commands and 1,975 wrapper ticks. A selected play-mode run passes movie sound/presentation checks with zero lifecycle fallback. The 39-run battery, remaining player services and graphics transport are still open. |
| STR-1 bank metadata | `--set hle.sound_bank_headers=off` selects the retained reader (default on) | The previously enabled `.mh` reader is now directly switchable in one binary. This allows the DT-12 recheck without editing/rebuilding a constant. Sample bank/voice/music and STREAM/IOP retirement remain open. |
| STR-1 ordinary file completion | `--set hle.stream_files=on` (default off) | StreamLibrary uses generation/sequence jobs, owned delivery and ordered waits. FileSchedule now owns pending bytes, destinations, deadlines and completion phase; shared ReadTiming retains the existing logical arithmetic without LoaderModel queue ownership for selected ordinary reads. The root supplies the same boundary/park clock. Scheduler, file-byte, pending-restore and scheduling self-oracles pass. Audio/sample transfers, legacy pending-read import and module startup remain. The composition guard still refuses selected-service capture until all dependent owners migrate. |
| STR-2 synchronous file SDK | `--set hle.files=on` (default off) | Files owns six reset/open/close/seek/read/write SDK boundaries used by disc recovery and shadow-file fallback. Actual mounted bytes, independent descriptors/cursors, explicit errors and required FILS snapshots replace fileio parks/traps without iob/RPC/IOP execution. NEW ReadEngine versus selected Files compares 71,625 bytes; production dispatch and snapshot tests pass. See `hle/stream/FILES.md` for source/OLD pins. Actual game acceptance and STREAM error/status retirement remain open. |
| DMA-1/VIF-1 flat Strip variants | `--set hle.graphics_strips=all` (default plain; model selects original producers) | Four source-checked variants have a generated-NEW self-oracle with 2,024 inputs and 2,785,032 compared vertices. Zero active output differences; 4,064 inactive untextured tex.fst differences are attributed. Actual per-variant generated-body calls are reported. Textured/unsupported/interior entries, chain anchors, Link and GS publication remain dependencies. |

These switches do not establish model retirement by themselves. They are
included in Config provenance, so ON/OFF comparisons must name the intentional
selection difference. Existing default behavior is preserved.

**M6 resource provider stage (2026-10-07).** `GraphicsResources` owns converted
upload bytes, allocation versions and fence-held leases. `GraphicsTargets`
resolves SceneColor/SceneDepth/SceneCopy/SkyColor when recording;
`GraphicsRenderer` submits supported draws through explicit backend surfaces
without GS registers, address lookup or a VRAM mirror. The four focused
integration tests pass. OLD SH1/SH7/SH8 and BUG-058/061 requirements are carried
as upload ordering, alpha lifetime, complete-state and destination-copy pins.
These are resource/parameter tests, not raster comparisons. Production source
producers still need to use this provider; this stage retires no additional
model and supplies no frame-time measurement.

**M1 additional source operations.** The existing opt-in selection now also
installs twelve TrigMath/axis-matrix entries and the source ReadJointAngle decoder.
The generated-NEW oracles compare 144,000 trig cases and 32,000 angle cases;
all context and fixture memory bytes match. Historical requirements are in
`hle_coverage/trig_bug_pins.tsv`, `trig_pairs_bug_pins.tsv` and
`animation_bug_pins.tsv`. The initial integrated Debug trig test exposed a
one-bit difference from separate sinf/cosf lowering. Compiling that replacement
with the same production arithmetic options in every preset fixes it without
changing the oracle; both trig and angle tests pass afterward.
The macro/collision/trig/angle control and selected arms now count actual captured
generated-body executions immediately before each call, including exceptions
and continuations. The macro/collision expanded oracles compare 104,000 and
96,000 cases respectively. The reviewed gate catalog names those individual operations,
not the entire VU model. The earlier 1,500-frame game comparison covered the
13 macro plus eight collision entries, not these newly added trig/angle entries.
The three later axis-matrix boundaries bring the selected math scope to 34
complete entries. Their actual generated-body counters are reviewed in the
title's `verify/retirement_counters.json`. The generic verifier loads reviewed
repository catalogs and requires their build fingerprints; a candidate cannot
supply an alternative zero-counter catalog. The source-path purity failure
found by the full `be56bee` suite is fixed by that ownership separation.

**Further implementation handoffs (2026-10-07).** These committed slices
extend the replacements above; their production default and full retirement
gates remain open.

| Scaffolding still reached in play | Implemented replacement and bounded evidence | Remaining production closure |
|---|---|---|
| Hull/triangle macro bounds | `BoundsMath` replaces TriangleBounds, PointsBox and GetBbox under opt-in `hle.math_helpers`. Complete context/memory oracle passes 96,000 cases in all four rounding modes across both selections; actual exported control/selected inputs/results match. OLD raw-payload and ABI pins are retained. | Parent application build/comparison and full 13×3 retirement gate. Other source math families remain open. |
| Box/corner macro clipping | `ClippingMath` owns ClipBoxAt, ClipBox and the source ClipCorners interior under opt-in `hle.math_helpers`. Pure corner transforms and guarded/tight plane masks preserve the complete generated ABI; 96,000 same-input cases pass in Release, Debug and ASan/UBSan. Two actual generated-body counters include the control tail call. | Parent application build/pairing, accepted restored-entry policy and full 13×3 gate. Selected source-entry support does not grant opaque host code generated checkpoint replay proof. |
| Standard joint interpolation VU0 operation | `JointMath::Interpolate` computes the source quaternion and weights directly; 16,512 varied/warm-feedback comparisons pass in three presets. `JointLibrary` prepares complete RotateJoint297b40 replacement under `hle.math_helpers`, with scalar/stack/source-object ABI, three-caller static audit, actual original/interior counters and atomic installation. Optional artifacts retain full raw context differences plus exact separate source-visible selected/control streams. OLD authored-key, rounding, continuation and decoder-separation pins remain explicit. | Parent application integration, supported restore and full 13×3 acceptance remain. AnimateJoint293ec0's four vector operations (818 Euler frame blend, a70 turn, inline macro scale Lerp, 1c0 interpolation) are verified pure `JointMath` operations (16,000/16,000/32,000/16,512 same-input NEW cases in three presets); its complete adapter `JointLibrary::Animate` (track reader, three counted ReadJointAngle delegations through the installed binding, the four vector operations, the source scale/translation blends and frame publication) passes 168,130 checks over 12,000 installed-arm cases in three presets against the NEW body and its callees, with a verified artifact export (6,000 pairs, 0 source differences); catalog `JointLibrary.generated.animate`. DoAnimation's vector staging and the parent application gate remain open. Discarded microprogram temporaries are not reconstructed; no complete-context equivalence or entire VU0/M1 retirement claim. |
| Standard joint basis / parent composition | `JointMath::RotationBasis` and `MultiplyByParent` implement source 548/668/790 arithmetic from the statically identified standard image. The actual NEW oracle covers ordered fused/chop math, parent/own scales, raw zero and boundary words, input aliases, affine translation w and authored-key hierarchies. | `JointLibrary::Transform` now replaces the complete CreateJointTransform294300 boundary under opt-in `hle.math_helpers` (two-entry atomic install with RotateJoint): done/early return, no-rotation identity with published own scale, parent-scale compensation independent of the no-parent flag, raw scale copies in source write order, alias-sensitive flag reload, parent composition with translation.w forced to one, exact scalar/stack ABI and span validation before mutation. 180,101 checks over 16,000 installed-arm cases pass in Release, Debug and ASan/UBSan; exported artifacts pair 8,000 identical source-visible cases per arm. Catalog `JointLibrary.generated.transform`. DrawInstanceModel1fdb00/DrawOgi296228 geometry consumers, artifacts receipt and all application acceptance remain open; no new runtime retirement. |
| Chunk-view culling VU0 arithmetic and plane/view uploads | `hle.culling` (default off) selects eleven complete source producers/readers using `CullingMath`, `CullingPlanes` and typed owned state. CULL persists active view/link/planes and pending results; counted originals preserve continuation proof in the control arm. Generated-source and fresh-owner checkpoint tests pass in isolation. | Application build/game comparison, actual oracle artifact receipts, approved legacy checkpoint migration and full 13×3 acceptance remain. Particle/decal/joint/ray and other VU0 consumers remain separate open source families. |
| IPU lifecycle and movie refill transport | Software input/demux services; retained-NEW lifecycle oracle compares 38 pictures, mixed PCM and clock, with actual BCLR dispatch 4 → 0; root selected arm now binds its shared file cursor, demux and PCM gain providers | Replace player buffer loans, upload and presentation; complete integrated acceptance |
| STREAM sample transfer and IOP audio work | `hle.sound_samples` composes SoundBanks, SoundControls and the common mixer; `hle.music` adds MusicLibrary; generated command/sample/ramp oracles pass | Opt-in game comparison diverges at frame 212/audio window 176 and exercises no owned playback; attribute/fix before default. Music preload timing/effects, restore and module cutover remain |
| Sky DMAC/VIF/VU/GIF/GS path | `SkyPass` owns setup, authored geometry/materials, decoded textures and copy-out through named targets; actual generated-producer oracle and OLD alpha/order/scale pins | Connect source ordering and shared scene targets in the application; live acceptance; other 28 program registrations remain open |
| Asset texture uploads and VRAM lookup | `TextureCodec`/`TextureAssets`, verified load/destruction bindings, owned mips and Sprite producer oracles | Select only assets whose every consumer is converted; connect shared targets and ordered resource delivery before enabling |
| Movie GIF upload and GS draw production | Owned decoder RGBA delivery and MovieGraphics upload/draw producers; unchanged generated-body oracle compares actual upload bytes and active draw/vertex output. Selected inactive/continuation calls refuse before transport; Close preserves accepted render leases and cannot erase a failed output from checkpoint admission | Shared SceneColor/Depth delivery and existing-boundary presentation; live acceptance |
| Movie SPU external-input and BVOL gain path | Movie worktree: `MovieSound` selects `MovieAudio` over the existing common mixer/FIFO with `hle.movie_lifecycle`; the actual retained lifecycle oracle checks equal picture/mixed-PCM/clock bytes, actual retained input-pop positive control and selected zero. MOVI v2 persists the closed gain and rejects any unclosed PCM gate | Merge root/provider/state composition; real-play and same-binary output pairs, then 13×3 acceptance. General SPU voices/control and common mixer remain separate M5 dependencies |
| Movie PCM sink and gain owner (resolved, round 2) | One owner, one seat: `MovieSound`/`title::MovieAudio` (Movie, `hle.movie_lifecycle`, MOVI v2) holds the Movie gain and the decoder FIFO lifetime; `SoundOutput::BindMovie(sound::PcmSource*)` is the single sink seam — the owner lands on `AudioMixer::SetMovieSource` when `hle.audio_mix` is selected, otherwise on the common `Mixer::SetPcmSource`. `AudioMixer` pops and scales only; it stores no Movie gain and no PCM; SVCS output envelope is version 3 (master + two stereo groups, no Movie floats; v2 images refuse). The root passes `MovieSound` a `Sink` that calls `BindMovie`; the gain callback reaches `MovieSound::SetGain` alone. `SoundOutput` three-arm oracle (retained BVOL core / owner on common Mixer / owner on owned output) compares exact output and PCM clock with zero model mix calls and zero BVOL writes on both owner arms | `hle.audio_mix` default stays off until the parent's same-binary pair (`hle/audio/OUTPUT.md`) and the 13×3 gate; Movie's MOVI restore of a closed gain is the only Movie-gain persistence |
| Movie Step copied scratchpad stack | Default-off `hle.movie_step` in the Movie worktree preserves the complete forwarding Step's queue/decode/feed/frame algorithm through owned picture/PCM services. The actual generated Step and memcpy oracle compares equal picture, queue, EOF and callee ABI outputs with positive retained calls versus selected zero | Merge the root/config/counter stage; run composed same-binary pairs and 13×3 gate. Start's separate selection is below; Movie presentation transport remains retained. These CPU copies are not D8/D9 DMA |
| Movie Start copied scratchpad stack | Default-off `hle.movie_start` in the Movie worktree preserves the complete NTSC Start service sequence with two owned primes and both buffer consumers. `MovieStartOracle` compares actual unchanged Start/Open/LendSound/ReadOn/memcpy against this service adapter, including failed file/loan/header/read paths and repeated Close/reopen | Merge the root/config/counter stage after the complete PCM/buffer/picture composition; composed pair and 13×3 gate remain. BeginPresenting registration, Stop/WaitFrame and presentation transport are retained. This does not retire those consumers |
| Movie WaitFrame generated dispatch wrapper | Default-off `hle.movie_wait` requires the complete Start service set and owns bounded refill plus the complete game presentation call. Inventory v2 corrects the shard-only audit: live NEW dispatch uses an explicit wrapper in `recompiled.cpp`. The retained-output oracle compiles that unchanged wrapper and compares actual picture-upload/state output against the service owner | Merge and run the composed same-binary pair and full gate. No new field wait, IRQ completion or PCM operation is introduced. BeginPresenting/Stop callback registration and M7 presentation transport remain explicit dependencies |
| Movie VBlank registration and Stop | Default-off `hle.movie_stop` pairs direct Start presentation with the complete source Stop, requiring selected Step/Wait and all Movie services. The unchanged generated Stop and actual Libgraph registration oracle compare picture/state/renderer-service inputs and balanced loans; selected callback entry refuses | Root cohort and composed pair/39-run acceptance remain. Stop preserves its two cleared frames, sound reclamation and WaitIdle/ResetBuckets order; those M7 transports are not hidden or claimed retired |

**Services branch continuation (2026-10-07, default off).** Composed
`ServiceState` now joins pending files, ordered controls, voices, music and
buffer loans. The application `MovieStorage` composition supplies both Movie
loans from that buffer owner. Root registration, legacy pending-state migration
and complete Movie state restore remain open; component snapshots alone do not
remove the checkpoint guards.

`hle.service_time` is an integrated M4 repair candidate. `FileSchedule` owns
the settled logical position used by subsequent reads and loader headroom;
`ServiceTimeline` observes the remaining service clock without advancing it.
Selected bank oracles compare six same-input scenarios, preserve complete
bytes/deadlines and execute zero model CD/sample-transfer work in those
scenarios. The actual fifteen-bank stage11 prefix still differs in fourteen
deadlines after removed transport overhead; it is recorded as a failing
diagnostic. The game comparison remains first divergent at frame 212/audio
key 176 until the parent reruns the integrated candidate. See
`hle/scheduler/SERVICE_TIME.md` for the three-arm gate and evidence. No IOP,
STREAM, CDVD or SPU model is declared retired; no frame-time delta is yet
established.

The source family ledger is `hle/graphics/evidence/FAMILIES.md` under the
title. Movie, audio and texture source/OLD-bug contracts accompany their
modules. No whole hardware model is newly declared retired by these handoffs;
frame-time delta remains unmeasured.
The combined Release build, focused validation and rejected sound comparison
are detailed in [M_INTEGRATION_STAGE7.md](M_INTEGRATION_STAGE7.md).
The later bank pending-intent fix, 25-entry TrigMath scope and source-checked
UI ownership catalogue are recorded in
[M_INTEGRATION_STAGE9.md](M_INTEGRATION_STAGE9.md). The repeated sound comparison
still fails at the same first frame/audio keys; no sound default was changed.

**Selected movie service composition.** The same `hle.movie_lifecycle` switch
now selects the two stream, one demux and two audio boundaries with the
existing seven lifecycle/picture bindings. It also selects MovieStream's
Prepare/Open/Close and the PCM playback controls. Both callbacks and decoder
read-ahead use that one owned cursor; the PCM input/gain route stays shared.
No second feeder, end-of-file drain or new timing source is introduced.
OLD `SOUND_P5_IMPL` first-picture gating, muted consumption, EOF backlog and
paired-gain requirements remain pinned by the movie service/lifecycle tests.

The Release comparison in
`int_work/m_wave/movie-services-comparison-bounded/comparison.json` uses one
immutable binary and an unchanged source fingerprint for both arms. All 1,500
frame and 250 audio hashes match; memory differs at 22 of 25 checkpoints,
first at 180 (advisory). Selection serves 547 pictures, performs 1,410 shared
cursor reads/demux windows, 1,953 audio feeds and four volume changes. Actual
IPU BCLR is 4 → 0; all selected IPU command counters, optional original-body
calls, fallbacks and stream/input/decode errors are zero. This is one bounded
coldboot comparison, not the full 13×3 retirement gate or a performance test.
The first attempt exhausted disk space writing an automatic boundary snapshot;
the completed rerun retains hashes/logs and bounds disposable capture storage.
The subsequent shared-cursor/PCM-control comparison in
`int_work/m_wave/movie-services-comparison-stage8/` also preserves all 1,500
frame and 250 audio hashes, with zero selected IPU commands and replaced
control calls. The complete Release build and 40 focused checks pass; see
[M_INTEGRATION_STAGE8.md](M_INTEGRATION_STAGE8.md) for exact identity and scope.

Focused integration: fourteen Debug checks passed; the full original texture
corpus exceeded the old 120-second unit deadline. With the explicit corpus
deadline, the Release texture test and both post-padding movie provider tests
pass. The generic purity lint and all 67 DT-12 verifier tests pass. The earlier
complete `be56bee` suite remains recorded with its one purity failure; these
focused results do not rewrite that historical full-suite result.

**M7 FinishSky and M3/M5 provider checks (2026-10-07).** The optional `SkyService`
lowers the complete source `FinishSky` entry through owned Graphics targets.
The generated-NEW self-oracle compares 897 cases and 94,272 vertices, with zero
active draw/vertex differences; inactive blend/mip state is attributed. Actual
sky geometry and its half-size color resource remain required before gameplay
selection. No blank target or VRAM copy substitutes for that work.
`StreamFileSelfOracle` compares 6,408 file bytes against actual NEW STREAM
execution. `AudioSampleSelfOracle` compares 480,000 mixed stereo frames against
NEW voices; the 5,372 checks include bank generations and sample lifetimes.
The ADPCM reader uses defined signed expansion, pinned for all 208 nibble/shift
pairs. The integration branch's eight focused tests passed, including the movie
provider clock and Sky tests (`int_work/m_wave/ctest-integration-sky-services.log`).
These providers do not yet replace pending-read scheduling or production audio
controls; no additional model is retired.

**Checkpoint ownership.** The application now rejects an unowned REQUIRED
section before teardown or RAM mutation. This prevents a checkpoint made with a
new service from silently loading under a selection without that service.
The explicit historical exceptions are FILE, CNTR, MISC, STRM and SIFS, which
the preceding root already omitted; their migration remains M3/M4/M12 work.
HEAP is not an exception (BUG-085). Partial-owner inspection tools retain their
historical reader mode. This is an ownership guard, not completion of service
snapshot migration.

The product worktree composes Input/Save, original-image ServiceRestore,
closed Movie and Graphics owner preparation with the actual F8 supervisor.
`harness.live_restore` remains off. A retained complete NEW image is validated
and staged before unwind, then published once after workers/writers join;
clock, recording/output epochs, audio device and pause state follow that
transaction. The composed fixture performs two restores and preserves newer
durable game saves through actual INPT/SVST/ISCP providers: missing selected
sections or an active write refuse before joins; the newer progress file
survives both rewinds with no replayed mutation. GCLK rewind now preserves
accepted input keys and save polling times instead of delaying pending work.
Closed Movie decoder construction is staged before unwind with paired
candidate cleanup; prepared apply does not allocate the decoder again or
flush the restored common PCM owner. These are composed contract pins;
they do not execute the title or prove model retirement.
Root live restore explicitly refuses after STREAM boot: its boot-era IOPM
contract cannot rewind a running module. Service-only startup remains M11
work before F8 activation; no blanket service reset substitutes for it.

The pure Input/Save preflight now checks the original section list, an explicit
selection marker, selected service payloads and approved legacy provenance
before publishing a restore plan. The Input importer validates coherent legacy
reports and their original clock; ambiguous pending work is rejected. File
handle migration now reads the incoming RAM image, not unrelated live memory.
The five focused integration tests pass (`InputLegacyImport`,
`InputSaveCheckpoint`, `StreamLibrary`, `MathLibraryTrigOracle`,
`MathLibraryAnimationOracle`; `int_work/m_wave/ctest-preflight-counters.log`).
Application selection and persistence wiring are still pending; these checks
do not claim live Input/Save retirement or general legacy save support.

The bounded Save importer additionally requires original RAM0, MCHL, SIFS and
THRD proving idle ownership. It preserves identity and retained results while
rejecting pending effects, ambiguous card observations and retired call chains.
The audio provider now treats the reserved sound count as table capacity,
allowing the title jingle's independent `0x8000` label, and retains ownership of
finished silent channels until explicit handoff. The seven focused checks in
`int_work/m_wave/ctest-audio-save-artifacts.log` pass. Macro/collision oracles
can now optionally export their actual consumed bytes, both selected/control
results and execution counters through the registered CTest command; normal
test runs do not write those artifacts. Full formal receipts and the 39-run
retirement gate remain pending.

**Preceding subsystem snapshot.** Artifacts under
`<work>/int_work/subsystem_completion/` include:

- `reachability.json`: 39 completed runs, no reported unsupported operations.
  All SPR start counters are zero. This is a bounded coverage result, not retirement.
- `gate/battery/coldboot_attract.rep1/run.meta`: the movie/libipu counts above;
  all IPU commands other than BCLR are zero and served RAW8 is zero.
- `gate/battery/coldboot.rep1/run.meta` and
  `gate/battery/artdet_f37152.rep1/run.meta`: bounded RPC command head/tail
  records supporting the observed stream/audio rows. Missing entries in a
  bounded head/tail capture do not prove a command never ran.

**Retirement acceptance.** For each row, record the verified NTSC boundary,
decomp specification, native owner and relevant game-visible results. Test
normal, error, lifecycle and save/restore cases required by that intent.
Exercise the corresponding gameplay scenario and show the replaced model
entry points are unreachable or have zero calls while the native path does
the work. Remove the obsolete binding/callback/serialization dependency when
its last supported consumer is gone. A wrapper which forwards into the same
model, a successful no-op, or zero unsupported-command counts does not close
the row. Whole-model removal waits until every supported intent is accounted
for; retained fallback paths remain explicit in this table.

**Accepted library validation (2026-10-07, DT-11).** Debug and Release build with
zero warnings. The full suite has **474 passing tests**, four skipped and two
disabled, including `RootReleaseLanesIdentical` and the PCSX2 rendering pins.
The 13-scenario, three-repetition battery is stable. The historical DT-8 memory
report **FAILS**: **20,084,539 raw words**, **92 above** the S12 reference of
20,084,447. All of that increase is in the attract scenario; the other twelve
scenarios have unchanged raw totals. Unbooked words improve to
**20,042,931** from 20,064,909, and no frame/audio first-divergent key or pin
regresses. Under DT-11, memory is advisory and these libraries are **accepted**.
This is **87 words above**
the preceding subsystem candidate's 20,084,452; it is not the old five-word
failure alone. No new residual-word attribution, mask, directive or frozen
baseline change was introduced. The added libraries remain enabled in the
working tree at their measured scope. Retirement acceptance requires frame/audio
first-divergent keys, PCSX2 rendering pins and the 13-leg stability battery;
memory-only differences cannot veto a replacement.

Across all **39 completed runs**, `MathLibrary` handled
**13,049,751 point transforms** and `MovieDecoder` served
**8,619 pictures** with **zero IPU RestartDMA calls**, zero
movie fallbacks and zero invalid work buffers. The first attract run preserved
all 6,000 frame hashes and 1,000 audio hashes from the preceding candidate.
Its 2,326 pictures now require zero restarts (previously 2,326); StopDMA fell
from 2,329 to three and BCLR from 2,330 to four. Remaining startup calls,
presentation transport and other formats keep MOV-1/MOV-2/MOV-3 open.
That accepted pass kept the header binding disabled. The subsequent DT-11
recheck enables it; its current validation status is recorded in STR-1.

Evidence: `<work>/int_work/library_retirement/`
contains `ctest-final.log`, `bin/SHA256SUMS`, `gate/SUMMARY.md`,
`regression.log`, `reachability.json`, `libraries.json` and `attract-lanes.json`.
The gate reused the full suite's passing Debug/Release identity check after
byte-comparing its binary snapshots with the tested build outputs.

## 0. Method and provenance

| Input | Lines / rows | Used for |
|---|---|---|
| TR `ps2sdk.txt` | 447 lines, 372 names (+ `name = target` aliases) | the SDK surface the retail executable links (TR:ps2sdk.txt:1-3) |
| TR `include/platform/*.h` (14 interfaces) | 229 method declarations | the platform layer the game's C++ calls; each method's PS2 body (`src/platform/ps2/**`) grepped for its SDK calls |
| TR `src/game/**` (242 files) | callers | which game file calls which `Platform::X::Method` |
| TR `symbol_addrs.txt` | 10,004 lines, 8,472 `type:func` | the PAL function starts (section 6) |
| TR `RETAIL(...)` declarations | 10,205 references, 7,780 `C++ name ↔ retail name` pairs | TR's names for retail functions (`tr_cpp_name` column of the map) |
| TR `docs/RETAIL_BUGS.md` | 519 lines | section 5 |
| ours `games/<t>/hle/sdk_bindings.tsv` | 298 data rows | the title manifest (addresses) |
| ours engine rows (every `Register`/`RegisterWrap`/`RegisterRecompiled`/`Rebind` site and `{"name", &Handler}` table under `engine/`, `games/`, `app/`) | 166 rows | implemented / park / stub / recompiled / wrap |
| ours `engine/ee/hle/syscall_table.cpp:118-174` | 119 labels | the libkernl syscall wrappers' service (served / logged no-op) |
| ours `games/<t>/symbols/symbols.tsv` | 8,825 rows | NTSC function starts; the `recomp_seam` rows (seam bodies) |
| ours `int_work/p834a/leg_on700/run.meta` | `sdk_rows_hit_list` | reach evidence (hits per row in a 700-frame NEW leg) |

Helpers (files, `python3 -u`, `-W error` clean): `int_work/p837e/helpers/inventory_sdk.py` (writes `sdk_surface.tsv` and
`sdk_gaps_by_system.tsv`), `tools/symbols/match_pal_ntsc.py` (the matcher; `--selftest` synthetic). The other five TSVs are
hand-written from the cited TR lines (breadth first; each row names its depth).

## 1. SDK surface (`hle_coverage/sdk_surface.tsv`, 722 data rows; `sdk_gaps_by_system.tsv`, 5)

Columns: `function library tr_source ours our_handler callers(game files) system priority hits(leg_on700)`. `ours` ∈
implemented | wrap | recompiled | seam(engine body) | seam(pass-through) | stub | park | trap | `syscall:N served` |
`syscall:N no-op` | n/a. Status by title row (298): implemented 96, wrap 6, recompiled 13, stub 33, park 11, trap 131 (+ 8 manifest rows at inner labels of a discovered function: never hooked, the generated body runs — booked `recompiled(inner label …)`; the binder counts them in its 139).

Reading of the 372 `ps2sdk.txt` names:

| Class | Count | What |
|---|---|---|
| done (implemented / wrap / recompiled / seam / syscall served) | 399 of 722 rows | incl. 49 libkernl syscall wrappers whose generated body runs `syscall N` and `SyscallTable` serves N (TR:ps2sdk.txt:155-243 ↔ `syscall_table.cpp:118-166`) |
| P4-dead (TR: "nothing reaches it") | 137 | TR:ps2sdk.txt:245-280, 300-335, 337-348, 375-447 — SystemInit's payloads, libdev VU pauses, scanf, vfprintf/dtoa, libgcc soft doubles; our trap rows there are harmless |
| P1-trap (TR names it, ours traps) | 41 | the libkernl timer library, `DelayThread`, SIF-RPC internals (`_request_*`, `_alarm_*`, `_search_svdata` — shadowed by HLE'd `sceSifBindRpc`/`CallRpc`), `sceSifExitRpc/ExitCmd/RemoveCmdHandler/ResetIop`, deci2/TTY, `PowerOffCB`, `new_iob`, `sceRead/Write/Lseek`, `addhour/subhour`. **8 of the 139 "trap" rows cannot trap at all**: `++traps` counts every row with no engine body (`sdk_binding_manifest.cpp:141-153`) before the install, and these 8 are the run's `not_fn_start` rows (`run.meta` `sdk_rows_not_fn_start_list`: `_sceMpegWaitIpuIdle`, `_sceMpegFlushBuf`, `_copyAddRefImage`, `_copyRefImage`, `adddate`, `subdate`, `iWakeupThread`, `iSetTimerAlarm`) — inner labels of a discovered function (`symbols.tsv` `sdk_inner_label`), never hooked, run as part of the enclosing generated body (`ee_bootstrap.cpp:1406-1408`). So the installable trap rows are 131. **`iWakeupThread`** (manifest 0x2cffd0 = `FUN_002cfee0+0xf0`, TR:ps2sdk.txt:206) is such a row: its body is the libkernl wrapper for syscall 52, which `SyscallTable` serves (`syscall_table.cpp:32` `{52, "iWakeupThread"}` → `Threads`) — effectively `recompiled; syscall:52 served`, 0 hits in the leg; the TSV books it so. The same reading covers `adddate`/`subdate` (served by the recompiled libscf block inside `FUN_002c61c0`) and the four libmpeg inner labels (shadowed by the HLE'd outer entry points) |
| P1-park (reaching it parks) | 9 | `sceOpen`, `sceClose`, `get_iob`, `write` (`KernlFileIo`), `SetT2`, `SetNextComp`, `iGetTimerSystemTime` (`KernlSifRpc::Park` rows), `InitExecPS2`, `PatchIsNeeded`, `kCopy`, `kFindAddress` (boot-only) |
| P2-trap (TR does not name it; our manifest only) | 78 | libmpeg's inner decoder labels (`_decMB0`, `_motionVectors`, `_ri0_*`, …) — shadowed by `sceMpegGetPicture`'s HLE |
| P2-stub | 25 | `Ret0`/`Internal` bodies: `sceDev*Reset`, `cmd_sem_init`, `_sceCd_*prechk`, `mce*`, `_sceFs*`, `GetRomName`/`IsT10K`, `InitTimer`, `InitThread`, `sceScfGetTimeZone/SummerTime`, `DIntr/EIntr` |
| P3-check (TR names it; not in our manifest) | 21 | `SIF0_InterruptHandler`, `_sceRpcGetPacket/FreePacket`, `_lf_version`, `_fs_version`, `scePadInit`, `scePadInfoPressMode`, `AlarmHandler`, `Copy`, `convertfrombcd/tobcd`, `SetT2_COMP/COUNT/MODE`, `StartTimerCounter`, `TimerUSec2BusClock`, `FUN_0017d1e8`, `FUN_002d39d8`, `DoConcat`(=strcat), `DebugPrint_`(=printf), `FUN_002d7fa0`(=__muldi3) — the sce-lib finder did not match them in this ELF; for 18 of them run the matcher's row (`pal_ntsc_map.tsv`) to find the NTSC address and decide trap/recompiled. **Not for the 3 libscf rows** (`convertfrombcd`, `converttobcd`, `FUN_0017d1e8`): PAL links libscf INSIDE the game band (0x17cee8-0x17d430) while NTSC links it in the SDK band (manifest 0x2c5ff0-0x2c64d8), so the in-order map yields `none` for them (§6); their NTSC addresses follow from the block's offsets from `frombcd`, identical on both sides (PAL +0x20/+0x88/+0xf0/+0x1a8 = `convertfrombcd`/`converttobcd`/`FUN_0017d1e8`/`subdate`, TR:symbol_addrs.txt:2148-2152; NTSC `frombcd 0x2c61a0`, `subdate 0x2c6348` = +0x1a8): `convertfrombcd` = 0x2c61c0 (`symbols.tsv` `FUN_002c61c0`, recompiled), `converttobcd` = 0x2c6228 (no discovered start — inside `FUN_002c61c0` at +0x68), and TR's `FUN_0017d1e8` = 0x2c6290 = the manifest's own `adddate` (TR leaves it unnamed); `adddate`/`subdate` are `sdk_inner_label` rows of that merged function, two of the run's 12 `not_fn_start` manifest rows |
| ours=implemented, no manifest address | 2 | `sceCdSearchFile` (engine row `libcdvd_bindings.cpp:33`) and `sceSifFreeIopHeap` (engine row `sce_sif.cpp`) have a handler but are NOT `sdk_bindings.tsv` rows (only `sceCdLayerSearchFile 0x2c3778` / `sceSifAllocIopHeap 0x2d45e0` are), so the handler is never installed and the recompiled body runs — for `sceCdSearchFile` that body calls the bound `sceCdLayerSearchFile`, so the observable is the same. The TSV marks them `implemented(engine row; no manifest address -- recompiled body runs)`; every other trap/stub/park/wrap/recompiled row IS a manifest row |
| P2-syscall-noop | 2 | `iFlushCache` (104) and `SetVSyncFlag` (49) are logged no-ops in `SyscallTable` (`:167-174`); TR's `Platform::Graphics::PresentFromInterrupt` uses `iFlushCache` (TR:src/platform/ps2/graphics.cpp) — harmless on a coherent host |

Platform interface methods (229): 182 make no SDK call (pure EE code / hardware registers through libgs/libdma structs);
28 implemented; **5 `trap/park`** (`Platform::Files::Open/Close/Read/Write/Seek` → `sceOpen/Close/Read/Write/Lseek`; callers
`game/stream.cpp` = the `File` class used by the shadow tables and the disc-check); **3 `missing-row`** (`Graphics::WaitIdle` /
`PresentFromInterrupt` / `WaitSent` use `sceGsSyncPath` — ours is a SEAM body `GsSync::SyncPath`, so these are done; the helper
reports the manifest-row absence); `Movie::BeginPresenting/Close`, `Time::Initialise` use `DisableIntc/EnableIntc` (syscall 20/21
served); `Pads::Initialise` → `scePadInit` and `Saves::Initialise` → `sceMcInit` are P3 rows (`sceMcInit` is a pass-through seam).

**Gap table by system** (`sdk_gaps_by_system.tsv`; "first gameplay moment" from TR where it documents one):

| System | missing / trap / park | First moment that needs it (TR) |
|---|---|---|
| movie (libmpeg internals) | 1 / 71 / 0 | none: the outer entry points are HLE'd and the leg plays the FMV (hits: `sceMpegGetPicture` 547, `sceMpegDemuxPssRing`, `sceMpegIsEnd`…); `sceMpegDelete` (= PAL `FUN_002b8498`, TR:include `RETAIL`) is not a manifest row — the player's `Close` calls it (TR:include/platform/movie.h:57) → verify it is a plain recompiled body |
| kernel (libkernl) | 19 / 45 / 9 | fileio (`sceOpen` family): the shadow table fallback (TR:shadows.cpp:806-812) and the disc-error recovery (TR:src/platform/ps2/stream.cpp:99-105); timer library / `DelayThread`: TR leaves them to PS2SDK (TR:ps2sdk.txt:155-243), no game caller found in `src/game` (the game's clock reads T0 itself: TR:clock.cpp:225) |
| system/clock (libscf) | 0 / 2 / 0 | `addhour/subhour` trap, `adddate/subdate` are inner labels of the recompiled `FUN_002c61c0`; all four are `sceScfGetLocalTimefromRTC`'s helpers (TR:ps2sdk.txt:283-297); ours implements `sceScfGetLocalTimefromRTC` directly, so they are shadowed |
| renderer/boot (libdev) | 0 / 4 / 0 | `sceDevVu0/1Pause/Sync`: TR "Sony SDK code nothing reaches" (TR:ps2sdk.txt:300-303) |
| pads | 2 / 0 / 0 | `scePadInit` (TR:src/platform/ps2/pads.cpp `Initialise`) and `scePadInfoPressMode` — not manifest rows; the leg's pads work (`scePadInit2` is bound), so `scePadInit` is a recompiled body calling it |
| loader/disc | 0 / 1 / 0 | `PowerOffCB` (the power-off callback; never in a leg) |
| saves | 1 / 0 / 0 | `sceMcInit` — a pass-through seam (`libc_heap_glue.cpp:6` `HLE_002c5080`), done |

## 2. VU1 / VU0 programs (`hle_coverage/vu_programs.tsv`, 45 data rows)

**Source-review correction:** VU0 entry numbers must be qualified by the
uploaded program set. In the standard set, `0x1C0` performs rotation
interpolation; in the decal set it is the same-chunk particle transform,
with `0x360` handling another chunk's view. Standard `0xAB8` is ray/triangle,
and `0x548`/`0x668` build joint matrices from staged rotations. The original
census names below are not a universal semantic key. The set-specific
contracts and remaining microcode work are recorded in
`hle_coverage/library_boundaries.tsv` and `PORT_PLAN.md`.

TR registers 43 slots (TR:renderer.h:985 `VuProgramCount = 43`): 5 renderer programs (1, 2, 0xA, 0xD, 0xB:
TR:vuprograms.cpp:302), 5 parameter/blend programs (TR:vuprograms.cpp:365-393), 29 shader-type programs (TR:vuprograms.cpp:395-549;
type 0x1B shares type 1's code, :515-519; type 0x03 is a class nothing makes, TR:shaders.cpp:809; types 5-9 and 0x14 use the
resident programs, :809,848,880). The resident set is loaded with the first bucket every frame in the order 1, 2, 10, 13, 3, 11,
5, 6, 7, 8 (TR:vuprograms.cpp:60-66); programs from 0xE/0xF on are loaded when a material needs them, into one of two regions of
micro memory per bucket (TR:vuprograms.cpp:14, 592-598; materials.cpp:16,49-50).

Ours is keyed on the **uploaded image hash** (176 variants, `vu1_library/README.md`), not on the shader type, and the reference
carries no `.vutext` bytes, so the join is semantic (the `join_confidence` column). Programs **without a family**:
cloth 0x17 / 0x1A (TR:shaders.cpp:258-261), distortion 0x18 (TR:shaders.cpp:894-903), the blend-shape programs 6/7/8 and the
blend parameter (TR:skins.cpp:13-19 — our `vs_crys_displace`/`indexed_displace` are guesses; the 0x15a0 float displace has no
corpus capture), type 0x0E (its program index is set by another registration: TR:shaders.cpp:305,314), 0x0B/0x0D/0x1C
("shaders of their own kinds", TR:shaders.cpp:328-333). An unknown image is not a park: `engine/vu/tiers` runs it on the
interpreter/cached tier (`engine/vu/README.md:35-36`), so these gaps cost performance and pin coverage, not correctness —
their pixels are the replay gate's.

VU0 (`vu0_programs.cpp:17-31`): 15 rows, 1 HLE (sincos 0xF0 = TR `Platform::Math::SinCos`, the `vcallms 0xF0` TR keeps on
VU0: TR:DEVELOPMENT.md "VU0 arithmetic in C++"), 14 `Kind::Unimplemented` = interpreted, unvalidated. TR's names for them
differ from ours in two places (0xAB8 ray/triangle, 0x1C0 same-chunk particle view: TR:particles.cpp:339-340) and TR's
`OtherChunkProgram = 0x360` has no census row.

**Proposal (P8.37-E-VU-HASH-JOIN).** A later stage with the PAL executable can hash the PAL `.vutext` programs by TR's
`D_002xxxxx` code addresses + `.data` sizes (TR:vuprograms.cpp:177-294 lists every code/size pair) and join them to our
176 variant hashes exactly; until then the semantic join above stands.

## 3. GS draw classes (`hle_coverage/draw_classes.tsv`, 16 data rows)

TR's 28 buckets (TR:renderer.h:431-449: sky 0, opaque 2, global opaque 3, screen effects 4, blended 6-19 (TR:renderer.h:433; the second chain is buckets 5-20, bucket 5 unnamed), decals 20, shadows 21,
particles 22, distortion 23, UI 24, fonts 26, movies 27; three DMA chains 0-4 / 5-20 / 21-27) vs our ten classes
(`class_classifier.h:30-41`) and the seven stamp substrings (`renderer_profile.cpp:51-58`). Routed: the world strips, skins,
indexed displace, poscolor particles, sprite text, the stencil composite (bucket 21: TR's exact tuples ALPHA `{b=1,c=2,d=2,FIX 0x80}` /
`{b=2,c=2,d=1}` / `{a=2,c=2,d=1,FIX 0x20}`, TEST DATE=1 ZTE=1 GEQUAL, TR:shadows.cpp:74-79,216 — the pin values for
`SequenceRecipe`), and the FMV strips. **Generic fallback**: bucket 4 screen effects (16 × 1/16-width frame-copy sprites,
`OverSource` ALPHA `{a=1}`, `DepthGreater`, TR:effects.cpp:51-61; the type 0x10/0x11 copies at page 0x140, TR:shaders.cpp:149-157),
bucket 20 decals, bucket 22 textured particles (four page blend modes), bucket 23 distortion, the sky's 32-px copy-out strips
(TR:sky.cpp:12-13), the UI's untextured strips (TR:draw2d.cpp:85,112), the frame set-up strips (TR:frame.cpp:11-28 — the CT24→CT16
display copy with DTHE + Sony's DIMX: the subject of §7.1 row P8-CT16-DISPLAY-8BIT). One decode rule to carry: TR writes TEX0 and
MIPTBP fields **ORed unmasked** ("one too wide runs into the next", TR:textures.cpp:32-33) — a retail quirk our TEX0 reader must
tolerate, not normalise.

## 4. Streaming / loader (`hle_coverage/loader_rules.tsv`, 27 data rows)

Every rule TR documents in `filestream.cpp`, `readers.cpp`, `archive.cpp`, `disk.cpp`, `chunkloading.cpp`, `src/platform/ps2/stream.cpp`,
`multistream/*`, `clock.cpp` is listed with its status. **Modelled**: the STREAM client transport (server 0x12345 / EE server
0x12344321 agree with our `ModuleLayout`), open/size/read/wait/poll (the five seams), the ring cache (TR:filestream.cpp:396-520 is
`FileStreamRead` = our `FUN_002aac80`: the one-window reader, Skip/Rewind by position, `rest < reader size` refills from `next`
with `min(left, reader size)`, the oversized case copies the held bytes in whole 0x40 blocks then reads straight to the destination —
our `loader.ring_cache` and `LoaderCompletionOversized`), completion visibility at the poll (TR's test mode completes a read at the
first poll: TR:filestream.cpp:596-597 — the title tolerates it), the headroom predicate (TR:clock.cpp:209-232, quoted in the TSV;
its divisor is the controller FIELD `controller->framesPerSecond`, TR:clock.cpp:224, stored by `GameTimeConstruct` (:144) from
`main.cpp`'s `g_Pal ? PalFramesPerSecond : NtscFramesPerSecond` = 50 / 60 (TR:src/main.cpp:19-20,55-56) → the frame quantum is
576000/60 = **9600 ticks on NTSC** — ours — and 11520 on PAL; `include/game/clock.h:44`'s `FramesPerSecond = 60.0f` is the rate the
game's per-frame VALUES are authored for, not the predicate's), the FMV
stream (`sceCdSt*`), disc readiness. **n/a (recompiled title code)**: the readers' step order ("the file readers first, the main
ones once they have none", `DiskCompactAll` inside the step: TR:readers.cpp:239-245), the disk manager (64-B nodes, 32 KB per
move step, 16 releases a round: TR:disk.cpp:13-16), the chunk delays (1 s load / 4 s unload / 0.1 s unreached / 2 empty frames:
TR:chunkloading.cpp:33-39), the archive tables. **Not modelled**: the disc-error recovery (TR:stream.cpp:94-110 — opens the
executable through fileio → our `sceOpen` park; never reached with the ISO present). **CHECK**: SIF DMA's low-4-bit destination
masking (TR:DEVELOPMENT.md "Buffers the IOP writes"); whether the FILE-readers storage's 0x10000 ring (TR:readers.cpp:20) is the
DP-8 class-4 "second ring" and already covered by the per-stream ring model.

## 5. RETAIL_BUGS vs EQUIVALENCE_PLAN §7.1 (`hle_coverage/retail_bugs_vs_7_1.tsv`, 19 data rows)

No §7.1 row is contradicted. TR **corroborates NEW** on 13 rows (the loader/clock/path/allocator/pad/save/heartbeat rows: the
retail predicate reads the timer, the path is built in title memory, the allocators run unmodified, libmc is polled through the
non-blocking sync, the per-frame batch is the heartbeat). **One reword**: P8-CT16-DISPLAY-8BIT — the shown buffer is PSMCT16 drawn
from a PSMCT24 frame **with DTHE dithering by Sony's default DIMX** (TR:frame.cpp:14,28); the row should name the dither as part
of the expected picture. **Two strengthenings**: P4-CKPT60-LOADWAIT-ISSUE-SLICE (cite TR:clock.cpp:222-231), P8-ATTRACT-OLD-
COMPACTOR-STARVED (cite TR:readers.cpp:243-245). **One nuance**: P4-LOADER-WINDOW-ROUTING — retail copies the held 0x40-block
prefix before the direct read (TR:filestream.cpp:481-484); prove NEW does (a pin). The GS-format rows are undecidable from TR (no
software GS). RETAIL_BUGS entries that touch OUR rows (TR:RETAIL_BUGS.md:467-492: `sceMpegAddStrCallback`'s count bug, newlib's
`strncpy` 0x7E fast path, `snprintf` '0' padding, `deci2Putchar`'s stale byte) are **preserved** by our Recompiled / pass-through
rows — never replace them with a correct libc.

## 6. Symbol map (`games/<t>/symbols/pal_ntsc_map.tsv`; `tools/symbols/match_pal_ntsc.py`)

Method: the two sorted function-start lists are aligned IN ORDER on the only shape feature both sides have without the PAL
executable — the function's extent (next start − start) — pinned by 233 name anchors (our manifest's SDK names ↔ the PAL symbol
table through `ps2sdk.txt`'s aliases, extent-verified, monotone by LIS, delta-outliers dropped), `difflib` block matching inside
each inter-anchor segment, positional pairing of replaced runs, confidence from the neighbours' deltas. Opcode histograms (the
prompt's second feature) need the PAL bytes the reference does not carry — recorded as the next refinement.

Result (`int_work/p837e/match_run.log`): `pal 8472 functions, ntsc 8819, seeds 233`; `high=7723 (91.2%), low=345 (4.1%),
medium=122 (1.4%), none=49 (0.6%), seed=233 (2.8%)`; by band: game `high 94%`, sdk `seed 50% + high 45%`, tail `high 92%`. The
delta is piecewise constant: +0x8A0 across libpad/libcdvd/libgraph, +0x898 in libmpeg, +0x370/0x380 in libc/libkernl, negative
through most of the game code (PAL `FUN_001844d8` → NTSC `0x183d48`). Selftest (synthetic 3,000 functions, 2% inserted / 2%
deleted / 3% resized, 12 anchors): `2934 true pairs, 2930 right, 3 wrong (99.9%); high/seed wrong = 0; PASS` — `python3 -W error`
clean, 0.08 s; the real run 0.56 s. The run also prints `rejected anchors: 23` — the manifest names known on both sides that the
extent evidence refused to pin (read below); the map file is byte-identical before and after that report was added (`cmp` → identical).

Plausibility check — the 30 curated/seam names of `symbols.tsv` and where the map puts them (every one lands on the
semantically matching TR function):

| ours (NTSC) | TR (PAL) | TR C++ name |
|---|---|---|
| `stream_ring_fill` 0x2aac80 | `FUN_002ab500` | `FileStreamRead` (TR:include/game/filestream.h:129) |
| `wait_for_read_complete` 0x2ad1a0 | `FUN_002ada20` | `FileStreamWait` |
| `async_read_issue` 0x2ad110 | `FUN_002ad990` | `FileStreamPoll` |
| `loader_open` 0x2ad8f0 | `StartLoadingFileFromDisk_` | — |
| `loader_close_handle` 0x2adb40 | `FUN_002ae3c0` | — |
| `stream_mgr_poll` 0x2aa8d0 | `FUN_002ab150` | `UpdateChunkLoading` |
| `stream_record_stage` 0x1e1500 | `FUN_001e1c90` | `MsPlayStream` |
| `stream_reply_processor` 0x1e1f48 | `FUN_001e26d8` | `MsReadReply` |
| `stream_slot_state_read` 0x1e3308 | `FUN_001e3a98` | `MsGetStreamStatus` |
| `headroom_query` 0x191bd0 | `FUN_00192360` | `GameTimeHasTimeLeft` (TR:src/game/clock.cpp:26,209) |
| `load_poll_spin` 0x17d6b8 | `FUN_0017ddc8` | `BackgroundWork` |
| `t0_count_read` 0x191920 | `GetCPU_Timer0_Count` | — |
| `setup_display_registers` 0x19aff0 | `SetupPCRTC` | — |
| `piracy_check` 0x181490 | `SimpleCopyrightChecksum_` | `SimpleCopyrightChecksum` |
| `pad_ctrl_state_machine` 0x177168 | `FUN_00177278` | `Update` |
| `meta_alloc_small` / `meta_free_small` 0x181560 / 0x181590 | `MemoryAllocate` / `MemoryDeallocate2_` | — |
| `slab_alloc` / `slab_free` / `slab_init` 0x202458 / 0x204c38 / 0x204d48 | `CacheAlloc_` / `FreeCache_` / `AllocateMemory` | `SmallAllocate` / `SmallFree` / `InitFixedAllocator` |
| `slab_list_op_a..d` | `GetAvailableAddress` / `FreeAddress` / `StoreInCacheChain` / `FUN_00205f70` | `TakeEntry` / `FreeEntry` / `PushOnChain` / `UnlinkFromChain` |
| `fmv_prime_pictures` / `fmv_opener` / `fmv_load_loop` 0x2ae318 / 0x2ae860 / 0x2b0ae0 | `FUN_002aeb98` / `FUN_002af0e0` / `FUN_002b1360` | `Start` / — / — |
| `fmv_frame_advance` 0x2af8f0 | `FUN_002b0170` (**low**: the extents differ — a 50/60 Hz constant) | `WaitFrame` |
| `scene_updater` 0x2af0d8 | `FUN_002af958` | `Update` |
| `entry` 0x100008 | `entry` | — |
| `memcpy` / `memmove` / `memset` / `strlen` | `CopyBytesIntoMemory` / `FUN_002c70fc` / `memset` / `GetStringLength` (seeds) | — |
| `strcmp` / `strcpy` / `strncpy` | `StringDifference` / `CopyString` / `StringCopy` | — |
| `memclr` / `cmd_sem_init` / `cdvd_exit` | seeds (same names) | — |

**Proposal (P8.37-E-NAMES).** Adopt names into `symbols.tsv` from the `high`/`seed` rows (7,956) through `build_symbols.py`'s
curated path in a later stage: TR's *symbol* name when it is not a `FUN_`/`func_` (e.g. `SetupPCRTC`, `Cmd600_…_Dtor`), else the
`tr_cpp_name` when it is qualified or unique (never a bare `Destroy`/`Execute`/`Update`/`Read` — about half of the sampled C++ names are
such class-method names; the next matcher version should capture the enclosing class). The `low`/`none` rows (394) are the
functions whose bodies changed between builds — the 50/60 Hz constants (e.g. `GameTimeConstruct`'s `framesPerSecond` argument,
TR:src/main.cpp:19-20,55-56 — the PAL build passes 50; §4's headroom row), the region strings — and are where a PAL-rule must be
re-read against our NTSC disassembly before use. No renames now.

**Known non-monotone block and manifest conflicts (the matcher's `rejected anchors` report).** The run prints the manifest names whose
PAL namesake the extent evidence refused to pin (`int_work/p837e/match_run_iter1.log`: `rejected anchors: 23`); they are exactly the
finder-miss / relink suspects:

| Class | Rows | Reading |
|---|---|---|
| libscf relinked | 11 (`GetRomName` … `sceScfGetLocalTimefromRTC`) | PAL links libscf INSIDE the game band (0x17cee8-0x17d430, TR:symbol_addrs.txt:2142-2156); NTSC links it in the SDK band (manifest 0x2c5ff0-0x2c64d8). The in-order aligner cannot cross that: 9 rows are honestly `none`, but **`sceScfGetSummerTime → 0x17c950 medium` and `tobcd → 0x17c9b8 low` are WRONG** (the game-band functions at those PAL positions) — the manifest's addresses are right (the handlers run: `sceScfGetLocalTimefromRTC` is implemented; `AdjustTime` stub). A later matcher version should seed the libscf block by name regardless of order (a second, block-local alignment). |
| `kCopy` | 1 | PAL `kCopy 0x2d5210` maps `high` to NTSC `0x2d4e90` (delta −0x380, the same as the `EIntr 0x2d4e10` / `kFindAddress 0x2d4ec8` seeds around it); the manifest's `kCopy = 0x2d5208` is a different function, which no PAL row maps to, and TR's second copy `kCopy_002D5610` maps to `0x2d5290`. **The manifest's `kCopy` address is a finder-naming suspect** (boot-only park row; 0 hits) — verify against the ELF before the row is relied on. |
| extent differs, map agrees | 8 (`_sceMpegCscStoreRefImage`, `_sceMpegError1`, `_quantMatrixExtension`, `sceMcClose`, `sceMcSync`, `_sceSnprintf`, `InitExecPS2`, `InitTLB32MB`) | the function's size differs between builds (body changed or a discovery boundary differs) but the positional pairing lands on the manifest's address anyway (`low`): the manifest stands. |
| extent differs, map disagrees | 3 | `dmaRefImage` (manifest `0x2bed28` = PAL −0x8A0, the libmpeg band's delta; the map's `0x2bf140 low` is the wrong one — the manifest stands); `StartThread` / `InitTLB` are TR duplicate names (`StartThread_002D04B8`, `InitTLB_002D6958` map `high` to the manifest's `0x2d0138` / `0x2d65d8`) — fine. |

Rule for consumers: a `low` row that contradicts a manifest row loses; a `high` row that contradicts one (only `kCopy`) is a question for the ELF.

## 7. The order to close the gaps (`hle_coverage/gap_order.tsv`, 15 audit rows)

1. **Movie lifecycle:** the active saved-IPU-control defect belongs to the native
   Movie boundary. Reuse demux/decode/PCM/pacing components, own create/reset,
   picture delivery and close, and remove StopDMA/RestartDMA and CTRL bookkeeping
   from that path. Keep the five-word regression failure open until the native
   replacement passes the unchanged checks; do not add decoder-to-IPU mirroring.
2. **Files and streams:** implement `Platform::Files`/`Platform::Stream` requests
   through native file access and completion state. Cover the shadow fallback,
   ring readers and straddling reads; remove the corresponding STREAM/RPC/IOP
   path. TR:src/game/shadows.cpp:806, TR:src/game/filestream.cpp:396,
   TR:src/game/readers.cpp:20, TR:src/platform/ps2/stream.cpp:99.
3. **Audio and stream scheduling:** implement the shipped RPC command meanings
   as native bank, voice and stream operations with game-clock deadlines,
   explicit completion and save/restore state. Retire the corresponding
   STREAM.IRX worker, kernel waits, timer and interrupt dependencies.
4. **Graphics submission:** replace bucket chains with ordered native resource
   and draw submissions. Start at a specified copy, movie, upload or bucket
   boundary and eliminate its DMAC/VIF/GIF traversal. Use the game's material,
   particle, skin and effect semantics with existing native rendering pieces;
   more transport fidelity is not progress.
5. **Remaining conditional gaps:** trace any new park, trap or unsupported hit
   back to a game caller. Extend the relevant native service with a behavioral
   test and a path-retirement check. Unobserved SDK rows remain an inventory;
   their presence alone does not justify implementing a hardware service.
6. **Coverage and names:** extend scenarios to gameplay transitions, uncommon
   effects, abort/reopen, errors and saves. Improve symbol evidence as needed
   for safe NTSC bindings. A native boundary requires verified identity; PAL
   addresses and low-confidence matches are not installable hooks by themselves.

## Original audit findings (implementation dispositions subject to DT-9 and DT-10)

| Id | Where | What | Disposition |
|---|---|---|---|
| INT-P8.37-E-FILEIO-PARK | `engine/ee/hle/kernl_file_io.cpp:17-20` | `sceOpen/Close/write/get_iob` park; TR's shadow tables (TR:shadows.cpp:806-812) and disc recovery (TR:stream.cpp:99-105) reach fileio | gap rank 1; a `LibfsBindings` stage with a File-over-ISO pin |
| INT-P8.37-E-TIMER-TRAPS | `sdk_surface.tsv` rows `SetTimerAlarm`… `DelayThread` | 10 libkernl timer rows trap/park (+ `iSetTimerAlarm` at an inner label); TR leaves them to PS2SDK | gap rank 2 |
| INT-P8.37-E-INNER-LABEL-TRAPS | `sdk_binding_manifest.cpp:141-153`; `run.meta` `sdk_rows_not_fn_start_list` | 8 of the binder's 139 "trap" rows are inner labels of a discovered function (`iWakeupThread` 0x2cffd0 = `FUN_002cfee0+0xf0`, syscall 52 served; `iSetTimerAlarm`; `adddate`/`subdate`; 4 libmpeg) — never hooked, never park; the real installable trap count is 131 | gap rank 2 sub-item: `RegisterRecompiled` or exclude `not_fn_start` rows from `++traps` |
| INT-P8.37-E-HEADROOM-CONFIRMED | DECISIONS_LOG DP-8 (1) | TR:src/game/clock.cpp:209-232 `GameTimeHasTimeLeft` (= our `headroom_query` 0x191bd0 per the map) is a T0 time-left query with slack 0x4E whose divisor `controller->framesPerSecond` is 60 on NTSC / 50 on PAL (TR:src/main.cpp:19-20,55-56; clock.cpp:144,224) → quantum 9600 NTSC (ours) / 11520 PAL; `clock.h:44`'s `FramesPerSecond = 60.0f` is the game-values rate | DP-8 (1) stands for NTSC; cite the NTSC figure only |
| INT-P8.37-E-VU0-0x360 | `games/<t>/vu1_library/vu0_programs.cpp:17-31` | TR's `OtherChunkProgram = 0x360` (TR:particles.cpp:340) has no census row; 0x1C0/0xAB8 named differently from TR | gap rank 5; rename proposal |
| INT-P8.37-E-VU1-NO-FAMILY | `vu_programs.tsv` | cloth 0x17/0x1A, distortion 0x18, blend-shape 6/7/8, 0x0E/0x0B/0x0D/0x1C have no family (interpreter tier) | gap rank 6 |
| INT-P8.37-E-FALLBACK-BUCKETS | `draw_classes.tsv` | buckets 0 (copy-out), 4, 20, 22, 23, 24 (strips) draw through the generic fallback | gap rank 7 |
| INT-P8.37-E-RING2-NAMED | DECISIONS_LOG DP-8 class 4 | the "second ring" is the FILE readers storage's 0x10000 stream (TR:readers.cpp:20-21) | name it in the next DP-8 entry |
| INT-P8.37-E-CT16-DITHER | EQUIVALENCE_PLAN §7.1 P8-CT16-DISPLAY-8BIT | retail's display copy dithers (DTHE + Sony DIMX, TR:frame.cpp:14,28) | reword proposal |
| INT-P8.37-E-TEX0-ORED | `engine/gs` TEX0/MIPTBP decode | TR writes the fields ORed unmasked (TR:textures.cpp:32-33) | a decode-tolerance check |
| INT-P8.37-E-P3-CHECK-21 | `sdk_surface.tsv` P3 rows | 21 TR-named SDK functions are not manifest rows (`scePadInit`, `TimerUSec2BusClock`, …): finder misses; 2 engine rows (`sceCdSearchFile`, `sceSifFreeIopHeap`) have no manifest address either | map 18 through `pal_ntsc_map.tsv`; the 3 libscf rows (`convertfrombcd` 0x2c61c0, `converttobcd` 0x2c6228, `FUN_0017d1e8` = `adddate` 0x2c6290) by block offset (§1); add rows (Recompiled or handler) |
| INT-P8.37-E-LIBSCF-RELINK | `pal_ntsc_map.tsv` rows 0x17cee8-0x17d430 | PAL links libscf in the game band, NTSC in the SDK band: 9 rows `none` (honest), `sceScfGetSummerTime → 0x17c950 medium` and `tobcd → 0x17c9b8 low` WRONG (the manifest's 0x2c6108 / 0x2c6170 are right) | consumers: the manifest wins; matcher v2: a block-local, order-free seed for libscf |
| INT-P8.37-E-KCOPY-SUSPECT | `sdk_bindings.tsv:281` `kCopy 0x2d5208` | the map puts PAL `kCopy 0x2d5210` at NTSC 0x2d4e90 `high` (delta −0x380 = the neighbouring `EIntr`/`kFindAddress` seeds); no PAL row maps to 0x2d5208; TR's `kCopy_002D5610` → 0x2d5290 | verify the manifest's `kCopy` against the ELF (boot-only park row, 0 hits) before relying on it |
| INT-P8.37-E-NAMES | `symbols/pal_ntsc_map.tsv` | 7,956 high/seed source-name proposals | retain verified source aliases as provenance; DT-10 requires independently chosen descriptive implementation names |
| INT-P8.37-E-VU-HASH-JOIN | `vu_programs.tsv` | the semantic VU1 join could be exact with the PAL `.vutext` bytes | needs the PAL executable |
