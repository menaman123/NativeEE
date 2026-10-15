# Whole M2 integration and acceptance contract

M2 is implementing. Defaults remain unchanged. The previous 1,500-frame /
250-audio exact Movie comparison does not accept the later buffer, Graphics
and persistence composition. Run the commands below only in the parent's
serial integration window, using a frozen source tree and binaries.

## Required composition

- Closed Movie state: Movie `189aaf6` then `572332b`, with M0's
  `63e029d`/`48a6594` registry APIs. GCLK precedes service deadlines and MOVI.
  Preserve Movie's incoming-RAM validator and required-on-write section.
  Remove unconditional Movie resets after `RestoreFile`.
- Buffers: Movie `4f38b6a` and `227f73b`, Services `b1672b4`, and composed
  regression `a79a597`. Both Prepare/OpenStream consumers install before
  MusicBuffers exposes handles. Current MovieStorage closes consumers,
  returns both loans, then detaches the pool before its providers die.
- Pictures: Zeno `a1febb5` / `eb56dfd` provide MovieOutput and its application
  composition. Use the original dependency once; local Movie imports have
  different hashes. Preserve later M7 scene/source/resource persistence and
  all completion guards when merging. `GraphicsMovie::CanCheckpoint` also
  joins `MovieGraphics::CanCheckpoint`: an inactive owner with a refused
  output intent is not a valid checkpoint. Selected picture bindings never
  delegate before Start or after Close; accepted image leases still drain
  through Graphics after their Movie owner closes.
- Selected software constructor: Movie `39ecb32`. The default three-binding
  arm retains its actual initialization callback; lifecycle selection has
  no required IPU/DMAC service. Unexpected old initialization fails before
  changing software playback, PCM or RAM.
- Config schema: `bfaf108` adds the four Graphics selections to the generator
  and settings table. Reproduce that table after combining other owners' keys.
- Boot migration: `MovieBoot` composes Movie, InputSave and SessionClock over
  the original file. It requires M0's clock and Input/Save application APIs
  (`515c2dc`, `76c5dc4`, `a1ba1b9` and their prerequisites). Import those
  dependency commits once; do not replay the Movie branch's cherry-picked copies.
  The root uses `MovieBoot::PrepareFile`, installs its metadata frame, then
  `MovieBoot::Restore`. Keep this replacement when merging shared bootstrap
  changes: InputSave-only preparation adds ISCP before Movie can classify the
  frozen original and therefore makes every old restore fail Movie provenance.

Remaining provider obligations are in
[movie_format_audit.md](movie_format_audit.md). In particular, retained Movie
presentation transport and composed provider acceptance are still open.
The worktree's MovieSound composition supplies the PCM/gain handoff described
below. With closed-only MOVI, M5 must validate its incoming
state with `ServiceState::ValidateMovie(state, {})` before teardown. A native
owner missing from a new image never means “reset it to empty.”

### Movie/InputSave/clock boot join

`MovieCheckpoint::PrepareBoot` validates the unchanged original against the
approved basename/memory signature and frozen section schema before adding a
closed REQUIRED MOVI. That bounded import records no unrecorded MPEG contexts,
PCM, picture or buffer loans. It derives the selected stopped decoder identity
from incoming RAM without resetting live services. InputSave may then convert
its own sections. A file containing new owner tags with missing MOVI still
rejects; a complete MOVI image cannot use legacy permission to omit GCLK/ISCP.

New images restore their saved game clock before Movie's relative deadlines.
There is no later Movie initializer. Failed preparation cannot reuse a previous
prepared image, and successful preparation is consumed once. `movie_checkpoint`
counts actual MOVI applications in `restored`; an approved synthesized import
also increments `legacy_migrated` only after successful application. A recapture
of that imported session is a complete new image and does not migrate again.

The registered `MovieBoot` test uses its own NEW memory/file inputs and actual
production owners. It covers the original composition failure as a positive
control, lifecycle off/on, selected Input conversion, closed clock byte
continuation, PCM flush ownership, and CRC-valid incomplete/provider-mismatched
images rejected before teardown. `MovieCheckpoint` retains the independent
active-playback/EOF/loans refusal. These pins carry OLD `SOUND_P5_IMPL` and
`E2_SAVE_REVIEW`; they do not claim active Movie snapshot reconstruction.

### Closed Movie with file/Audio persistence

The ready M5 `SessionServices` owner (`b436027`, with its bank/control/music/
buffer state and service-time prerequisites) now joins M0's saved clock through
application `ServiceClock`. Root registers GCLK before SVCS and MOVI, installs
the pure join in both the registry and original-image preflight, and uses the
actual service capture predicate instead of unconditional selected-audio/file
refusal. The original-image check also rejects an optional SVCS when services
are deselected; it cannot disappear through StateRestorer's owner filtering.

The registered `MovieServiceCheckpoint` fixture composes the actual file,
Audio, music, buffer, Movie and clock providers. Its own audible ADPCM input
continues byte-exactly after restore while Movie preserves its relative
deadline. A structurally valid different GCLK tick, missing GCLK/SVCS, or
optional SVCS rejects before teardown, PCM flush or changes to live providers.
Either of the two actual Movie loans still refuses capture/restore; returning
only one is insufficient. Both returned permits exact repeated continuation
without manufacturing renewed loans.

This removes one application persistence blocker for closed movies. It does
not add active Movie reconstruction or selected-service OLD import.
Selected services require real SVCS; old hardware
sections cannot be reinterpreted as empty native service state. M0's interactive
preflight must retain the registered joins and its independent metadata/frame
and provider check, then restore the game clock before these owners. Do not
run Movie initializers or reset the restored Audio/file pools afterward.

### Movie PCM and gain ownership

`MovieSound` selects the input provider with the same `hle.movie_lifecycle`
flag. In the selected arm it installs `MovieAudio` in the common `Mixer`,
passes the checked player's quantized gain directly to that owner, and gives
`MovieCheckpoint` its `StateOwner()`. The retained arm installs the existing
`FmvPcmInput` in SpuCore and programs both original BVOL gains. There is one
FIFO in either arm. Demux, first-picture gating, field pumping, muted
consumption and Close remain the existing owners' operations. No sound-device
callback, second feeder, synthetic ring or new timing policy is introduced.

The PCM provider enters the existing common voice→Movie→master/clamp stage;
its float gain uses the original division by 32767. The adapter does not retire
the common mixer or the remaining SPU voice/control model. Herschel's
`VoiceSource` can coexist with this separate `PcmSource`. Keep both selections
when merging `Mixer`/`SpuCore`, and detach PCM only after game workers join and
before `MovieSound` dies. No bank/voice owner files are changed by this stage.

`MovieSound` is sink-agnostic: besides the `Mixer&` constructor it accepts a
`Sink` (`std::function<void(sound::PcmSource*)>`) that attaches/detaches the
owned `MovieAudio` on exactly one consumer. Since Stage 13 (Herschel
`aa92a328`, root `91295362`) that consumer is one seat: the root passes
`Sink{[&](PcmSource* s) { sound_output.BindMovie(s); }}` and
`SoundOutput::BindMovie` lands the owner on `AudioMixer::SetMovieSource`
when `hle.audio_mix` is selected, otherwise on the common
`Mixer::SetPcmSource`. Either seat reads `Gains()` from the owner and stores
no Movie gain and no PCM (SVCS v3 carries no Movie floats; MOVI v2 is the
only Movie-gain persistence). `MovieAudio` pins both seats on the same
input: byte-identical mixed output through the common Mixer and the owned
`AudioMixer`, one accepted gain intent visible as `Gains()` on the seat, a
refused intent reaching no seat, the retained core never pumped for Movie
PCM, first-picture gate respected, detach before owner death, and a selected
owner without a sink refuses rather than falling back. `MovieSound` is
non-copyable: the root constructs it exactly once.

MOVI v2 records owned PCM selection and closed gain. Either queued samples or
an empty open gate rejects capture/restore before teardown. Restoring a closed
gain executes no new volume intent or flush; source Close remains the cut.
The v1 reader permits only its original, unowned PCM composition. New selected
images cannot silently manufacture the missing owner.

The registered actual `MovieLifecycleOracle` now runs this same root adapter
in both arms. Its own input artifact includes gain intents. Input, picture,
mixed PCM and clock bytes must match; retained BCLR/init/stop and retained PCM
input operations must be positive, with selected zero and positive owned PCM.
`MovieAudio` separately exercises gain/mute, capacity/drop-newest, partial
chunks and final clamp using the production common mixer. These pins carry
OLD `SOUND_P5_IMPL:82–134`; they do not replace the real-play provider-clock
test, same-binary game pair or 39-run acceptance.

### Step and Graphics state composition

`hle.movie_step` is a separate default-off selection requiring both lifecycle
and Movie graphics. Root `GraphicsMovie` installs `MovieStep` after those
owners and includes its active/failed state in the capture predicate.
The complete forwarding Step preserves source queue/decode/feed/frame
behavior and bypasses only its obsolete scratchpad relocation wrapper.
`MovieStepOracle` executes the actual unchanged generated Step and memcpy
in its control; identical production picture providers compare uploaded
bytes, queue/EOF/frame state and callee ABI. The selected arm must report
actual Step/copy zero and positive picture work. Its source/ABI/OLD pins and
remaining Start wrappers are in `hle/fmv/MOVIE_STEP.md`.

The later `hle.movie_start` stage replaces those two priming wrappers with
direct owned-picture calls while preserving all surrounding NTSC source
decisions. It requires lifecycle, Movie graphics and music_buffers (and their
provider prerequisites). It rejects incomplete service installation instead of
delegating an opaque loan. Root includes its active/failed guard alongside Step.
`MovieStartOracle --start-paths` supplies the actual retained Start/memcpy
positive control; source/ABI/error and OLD pins are in `hle/fmv/MOVIE_START.md`.
The source presentation-registration calls remain explicit dependencies.

`hle.movie_wait` additionally requires `hle.movie_start` and replaces the
actual live WaitFrame registration in NEW, separately from the dormant shard
spin body. It preserves bounded refill and one complete game presentation
without another clock wait. `MovieWaitOracle --wait-paths` compiles that
unchanged registration and compares its observable picture/state/ABI output.
Inventory v2 records this distinction; see `hle/fmv/MOVIE_WAIT.md`.

The Movie worktree has imported Zeno's original `ef1a899`, `721c6ab`,
`6586f37`, `3f41765` and `30b4dd0` for source-frame, shared-scene and held-image
persistence. Merge those original dependencies once, retaining their guards.
Graphics uses `StateRestorer::AddImagePreflight` to stage incoming resources
before live publication. M0 `4143524` plus the `808a9a5` original-image
validation prerequisite provides the concrete preparation API (Movie's local
dependency import is `082bc91`; merge the originals once). Keep
`SessionRestart::Sources.validate_image` pure; bind `Hooks.prepare_image`
to `StateRestorer::PrepareImage`, pair its discard hook, and apply the exact
retained bytes with `RestorePrepared` after join. Registry validation alone
does not stage Graphics' incoming-RAM/resource join. Staging must finish
before teardown and must not repeat during apply.
Do not replace a missing Graphics/PCM owner with a reset. Movie boot already
uses the composed StateRestorer path; interactive restart needs the same
contract and its own accepted round trip.

`MovieServiceCheckpoint` composes that actual preparation/apply path with
Movie, PCM gain, files/Audio and held Graphics image state. It pins failed
resource allocation before teardown, unchanged live picture/sound on refusal,
immutable prepared bytes and exact restored image/Audio/deadline continuation.
The graphics backend is in memory and the source-frame fixture is explicit;
root interactive restart and GPU/live output remain composed gate work.

Root now also registers `MovieCheckpoint::RegisterPreparation` after MOVI and
GCLK. It prepares actual MPEG context allocations at the incoming clock before
teardown, and applies that same record once. The optional paired discard on
`StateRestorer::AddImagePreflight` releases unpublished Movie allocations on
failure/cancellation; preserve M0 `96d2d05` and later root changes when merging
this small extension. The composed fixture injects an actual factory-copy
allocation failure, a later Graphics allocation refusal, cancellation and a
new live PCM owner between prepare/apply. None may tear down or flush the old
owner. Successful apply allocates no second MPEG/Graphics resources.

## Parent same-binary pairs

Use the same explicit input/anchor in both arms; keep all unrelated selections
identical. Example coldboot commands use the existing read-only asset root.
Collect fresh directories and run serially:

```sh
M2_BIN=<repo>/build-release/app/host/nativeee
M2_OUT=<work>/int_work/m_wave/movie-whole-phase
for arm in off on; do
  python3 verify/run_leg.py --tree new --binary "$M2_BIN" \
    --anchor coldboot --frames 1500 --state-every 60 \
    --repo $PS2R_OLD_ROOT --timeout 900 --stall-s 300 \
    --out "$M2_OUT/lifecycle-$arm" --set "hle.movie_lifecycle=$arm"
done
for arm in off on; do
  python3 verify/run_leg.py --tree new --binary "$M2_BIN" \
    --anchor coldboot --frames 1500 --state-every 60 \
    --repo $PS2R_OLD_ROOT --timeout 900 --stall-s 300 \
    --out "$M2_OUT/buffers-$arm" --set hle.movie_lifecycle=on \
    --set hle.stream_files=on --set hle.music=on --set "hle.music_buffers=$arm"
done
for arm in off on; do
  python3 verify/run_leg.py --tree new --binary "$M2_BIN" \
    --anchor coldboot --frames 1500 --state-every 60 \
    --repo $PS2R_OLD_ROOT --timeout 900 --stall-s 300 \
    --out "$M2_OUT/pictures-$arm" --set hle.movie_lifecycle=on \
    --set hle.stream_files=on --set hle.music=on --set hle.music_buffers=on \
    --set "hle.graphics_movie=$arm"
done
```

The Step pair uses the same binary and owned picture path in both arms:

```sh
for arm in off on; do
  python3 verify/run_leg.py --tree new --binary "$M2_BIN" \
    --anchor coldboot --frames 1500 --state-every 60 \
    --repo $PS2R_OLD_ROOT --timeout 900 --stall-s 300 \
    --out "$M2_OUT/step-$arm" --set hle.movie_lifecycle=on \
    --set hle.graphics_movie=on --set "hle.movie_step=$arm"
done
```

These isolate four changes. A later combined pair cannot explain a newly
introduced frame/audio difference by itself. Repeat real play/audio and
abort/reopen checks with the actual provider clock before a default decision.
No inaudible deferred-PCM-tail workaround is permitted to force census equality.

The subsequent Start pair keeps the complete service composition identical.
The selected Start/WaitFrame/Stop owners require `MovieDecoder::Services()
== 127`, whose player-buffers bit is supplied by `MovieStorage::Install`
(`GameLibraries::InstallMovieBuffers` refuses without `hle.stream_files`),
so every Start/WaitFrame/Stop command carries `--set hle.stream_files=on`
(gate 3, round 2, ran the three loops without it and the selected owners
refused). The root installs the player owners after `MovieStorage`
(`GraphicsApplication::InstallMoviePlayer()`), and a refused owner prints
`nativeee: selected Movie Start requires the complete Movie service mask:
have 0x.., missing ...` before the root exits 2 — never a silent exit:

```sh
for arm in off on; do
  python3 verify/run_leg.py --tree new --binary "$M2_BIN" \
    --anchor coldboot --frames 1500 --state-every 60 \
    --repo $PS2R_OLD_ROOT --timeout 900 --stall-s 300 \
    --out "$M2_OUT/start-$arm" --set hle.movie_lifecycle=on --set hle.stream_files=on \
    --set hle.graphics_movie=on --set hle.movie_step=on \
    --set hle.music=on --set hle.music_buffers=on --set "hle.movie_start=$arm"
done
```

## Actual-operation and positive-work evidence

The WaitFrame pair keeps the complete provider composition identical:

```sh
for arm in off on; do
  python3 verify/run_leg.py --tree new --binary "$M2_BIN" \
    --anchor coldboot --frames 1500 --state-every 60 \
    --repo $PS2R_OLD_ROOT --timeout 900 --stall-s 300 \
    --out "$M2_OUT/wait-$arm" --set hle.movie_lifecycle=on --set hle.stream_files=on \
    --set hle.graphics_movie=on --set hle.movie_step=on \
    --set hle.music=on --set hle.music_buffers=on --set hle.movie_start=on \
    --set "hle.movie_wait=$arm"
done
```

The subsequent Stop pair keeps those selections fixed. Only the paired
Start/Stop presentation-registration ownership changes:

```sh
for arm in off on; do
  python3 verify/run_leg.py --tree new --binary "$M2_BIN" \
    --anchor coldboot --frames 1500 --state-every 60 \
    --repo $PS2R_OLD_ROOT --timeout 900 --stall-s 300 \
    --out "$M2_OUT/stop-$arm" --set hle.movie_lifecycle=on --set hle.stream_files=on \
    --set hle.graphics_movie=on --set hle.movie_step=on \
    --set hle.music=on --set hle.music_buffers=on --set hle.movie_start=on \
    --set hle.movie_wait=on --set "hle.movie_stop=$arm"
done
```

| Field | Meaning / requirement |
|---|---|
| `ipu_commands` | Actual IpuDevice command dispatch. All selected members zero. The retained lifecycle fixture executes BCLR four times. |
| `ipu_operations` | Actual LibipuApi entry, including direct libmpeg calls missed by SDK-wrapper counters. Selected init/sync/stop/restart zero. Retained lifecycle fixture measures init=2, stop=3; sync/restart are unexercised there and cannot be claimed via that fixture. Static attribution: the only NEW direct callers of `sceIpuSync 0x2c12e0` are DecodePicture `0x2b01e8`, `0x2b1208` and `0x2b72e0` (reached from `sceMpegReset 0x2b7cb0`); of `sceIpuRestartDMA 0x2c1190`, DecodePicture and `0x2b80e8`; of `sceIpuStopDMA 0x2c10a8`, Start `0x2afc00`, DecodePicture and `0x2b80c0`. DecodePicture/Reset/Create are the DT-11-accepted `MovieDecoder` boundaries whose retained bodies execute in neither arm, and `0x2b1208`/`0x2b80c0`/`0x2b80e8` have no direct caller or literal ELF address word. Both operations therefore stay identified as unexercised with a source-backed reason; the per-leg zero census remains the authority for computed reachability. |
| `libmpeg.retained_init_calls` | Actual retained initialization callback. Positive control / selected zero; diagnostic supplement to actual model counters. |
| `movie_decoder`, `fmv_pictures_served`, `fmv_pcm` | Positive selected pictures, two starts/closes for the bounded logo pair, consumed/audible PCM under the real provider clock, and no selected fallback/error. |
| `movie_audio` | `selected=1`, positive `gain_changes` and `pops`; actual retained input `model_pops=0`. The off arm must exercise `model_pops>0` and BVOL sets. Existing BVOL counters remain actual register writes; selected gain calls are not relabeled as them. `frames`/`audible_frames` are the seat's popped frames: in the deterministic `engine.pacing=freerun` coldboot leg they are **0 in both arms** (gate 3: `pushed=897024`, `pops=10500`, `frames=0`, `flushes=3`) because each movie decodes, pushes and is Closed — flushing the FIFO — between two boundary pumps; the live `audible_frames` requirement is therefore evaluated by `MovieLifecycleOracle` (`audible fields=23/23` under the real provider clock) and by a paced play-mode leg, not by this pair. With `hle.audio_mix=on` the owner sits on `AudioMixer::SetMovieSource`; the retained core is never pumped, so `model_pops=0` in both arms and the off arm's positive control is the BVOL sets plus `audio_mix.movie_frames>0` with `movie_audio.selected=0`. The Movie pairs above keep `hle.audio_mix` at its default. |
| `music_buffers`, `movie_stream` | Positive separate sound/disc loans, balanced returns, zero active loans after Close, positive reads/lookups, zero errors. Preserve exact real capacity and deferred free while either loan is held. |
| `movie_graphics` | Positive decoded/uploaded/drawn/presented work with zero original Movie upload/queue/draw producers. Accepted producer counts alone do not prove renderer completion or pixel equality. |
| `movie_step` | Positive selected steps/advances, `original_calls=0`, `failed=0`. The off arm calls the actual generated Step. `MovieStepOracle` separately measures its real memcpy copies; no whole-startup or DMA claim follows from this counter. |
| `movie_start` | Positive selected starts/primes, `original_calls=0`, `failed=0`, no unexpected `open_failed`. The off arm calls the actual generated Start; `MovieStartOracle` separately measures real priming copies. BeginPresenting/Stop/WaitFrame transport is not retired by this counter. |
| `movie_wait` | Positive waits/refills/present calls, `original_calls=0`, `tail_calls=0`, `failed=0`. Control executes the actual live NEW wrapper. This is not a backend completion counter and does not retire game presentation transport. |
| `movie_stop` / `libgraph_calls.syncvcb` | Positive selected stops/recoveries, actual `original_calls=0`, `callback_calls=0`, `failed=0`, and actual SDK registrations zero. `MovieStopOracle` supplies actual generated Stop and Libgraph positive controls. The complete source renderer recovery remains explicit M7 work. |
| `graphics_movie_transport` | Currently retained presentation calls. Keep them visible; positive calls mean presentation transport is not retired. |
| `movie_checkpoint` | `prepared` counts completed software context preparations; `restored` counts actual MOVI applications and `legacy_migrated` separately attributes approved imports. Cancellation may make prepared exceed restored. A synthesized boot import increments restored and migrated; a new reproduction round trip never adds a legacy migration. |

The registered real lifecycle oracle exports measured init/stop and BCLR
positive controls with exact input, picture, PCM and clock bytes. The supported
claims are `IPU.operation.init`, `IPU.operation.stop`, `IPU.command.bclr`
and narrow `SPU.movie_input.pop`. `MovieStepOracle` supplies the separate
narrow `Movie.player.step` proof and `MovieStartOracle` supplies
`Movie.player.start`; `MovieWaitOracle` supplies the separately attributed
`Movie.player.wait` live-registration proof. `MovieStopOracle` adds
`Movie.player.stop` and actual `GS.syncv_callback` evidence.
Broader model retirement requires its own actual operation proof.
`MovieDecoderServices`, `MovieBuffers`, `MovieCheckpoint`, `MovieBoot`,
`MovieServiceCheckpoint` and
`MovieGraphicsSelfOracle` retain the OLD EOF, PCM, pacing, abort, loan,
malformed-state and sampled-edge pins. The registered `GraphicsMovie` test
pins the root composition owner itself without generated code: the control
arm counts one actual captured execution per Step/Start/Wait(+tail)/Stop(+
callback)/picture producer and installs no presentation transport; Stop
without Start/Wait/Step, pictures without complete root services and Step
without owned pictures refuse at Install; with owned pictures and no active
movie both presentation boundaries run the retained transport once, release
the frame and never select; a split continuation or a failing retained
transport marks the owner failed before any fallback, visible to the
checkpoint predicate and in `graphics_movie_transport`; every refusal of
`GraphicsMovie::Install`/`InstallPlayer` carries a reason string and a
selected Start/WaitFrame/Stop over an incomplete service mask names the
missing services (`GraphicsMovie::MissingServices(95)` = the player buffers).
Link each accepted bug pin to an executed replacement oracle, not just this
document.

## Round-2 gate attributions (parent gate 3 on `bin_b39a543`, `GATES_LOG.md` L240–277)

Facts read from the parent's `run.meta`/`diff_*.txt`; no game ran on the
Movie side.

- **`hle.movie_lifecycle`, `hle.movie_step` — PASS** (frames/audio identical,
  census zero with positive owned work). The formal 13×3 is the parent's.
- **`hle.movie_start/wait/stop` — BLOCKED → repaired.** Exit 2 with no
  message at root install: the selected owners evaluated
  `MovieDecoder::Services()` = 95 (no player-buffers bit) because
  `GraphicsApplication::Install()` ran before `MovieStorage::Install()`; the
  documented loops also omitted `hle.stream_files=on`, which
  `InstallMovieBuffers` requires. Fixed on `port-movie-r3` (`InstallPlayer`
  after `MovieStorage`, named refusals, the loops above); `GraphicsMovie` pins
  the order and the reason text. Re-run by the parent on the next frozen
  binary.
- **`hle.music_buffers` — FAIL (audio silent from window 202), attributed to
  the music path, not the Movie loans.** Every Movie counter is identical in
  both arms (`fmv_pcm pushed=897024`, `movie_audio pops=10500 peak=32767`,
  `movie_decoder audio_started=2 audio_stopped=2`, `movie_stream
  sound_loans=2 disc_loans=2 loan_returns=4 buffer_errors=0`,
  `music_buffers errors=0 active_loans=0`). What differs is the game's
  streamed music: `music_library` goes from `starts=2 reads=2 delivered=2
  bytes=8388608 errors=0` to `starts=0 reads=0 delivered=0 bytes=0
  errors=2` (2 `Play`s, 2 `Child`s in both arms), `sound_controls.volume`
  248→0, `sound_output.owned_channel_samples` 1409600→704800, and the control
  arm's first non-silent window (202) is where that music starts (the Movie
  PCM is inaudible in both arms of this leg, see `movie_audio`). The only
  predicate in `MusicLibrary::Reserve` that depends on the buffers selection is
  `MusicBuffers::CanPlay(stream)` (`!g_buffers || g_buffers->CanPlay(stream)`;
  `Audio::ReserveMusicVoice` reads request parameters only), so the regression
  is `StreamBuffers::CanPlay` returning false for the music `Play`/`Child`
  stream after the two owned Movie loan cycles (slot not allocated through
  the owned path, storage not owned, or a loan bit still set). The loan
  clause is exonerated on the same inputs the Movie composition produces:
  `MovieBuffers` (1656 checks) pins that a lent music stream is unplayable
  only while a Movie view is held and playable again after both views return
  without any service Close, and that a no-lender scratch loan never blocks a
  music stream. The remaining clauses (`allocated`/`owned` for the stream the
  music `Play` chose through the game's free-stream search, or an allocation
  routed to the retained module) need the owner's same-input oracle (Herschel's `MusicSelfOracle`
  harness: the leg's five owned allocations `0x1e1ca8` — inferred from the
  control arm's RPC payloads and the on arm's `used=983040`: two at boot
  (streams 0/1, 0x20000/0x10000), three level streams of 0x40000 with SPU
  addresses and `0x1e1e10` resizes to 0x4000 — then a Movie
  `BorrowMovie(lent handle, SoundRing+DiscStream)`/return cycle, then the
  music `Play` + `Child` on the boot streams); Movie's side is the composition
  (`MovieStorage`) and the request is recorded in the M2 handoff.
- **`hle.graphics_movie` — unattributed → located at the presentation seam.**
  Frame keys 171 (the window's first picture), 184 and 185 (the two boundaries
  after the window) differ; 172–183 and all audio are identical.
  `MovieGraphicsSelfOracle` (304802 checks) proves owned and model picture
  content/draw IR identical, so the three keys are the presentation transport
  M7 still owns: the owned arm selects the picture frame
  (`VideoStack::SelectMovieFrame`) at the first `PresentFromInterrupt` and
  releases it (`ClearMovieFrame`) at the first boundary after the movie, while
  the model arm's scanout shows the retained frame buffer, which holds the
  last picture until the game redraws (the two trailing keys) and shows the
  pre-movie contents until its first picture scans out (the leading key).
  Pixel confirmation needs the parent's pair re-run with `--dump-frames`
  (`f171/f184/f185.ppm` per arm) — requested; until then the three keys stay
  attributed-by-mechanism, not accepted.

## Registered oracle exports and full gate

The parent writes the trusted `dt12-sources-v1` source manifest described in
`verify/README.md`. Configure both oracle arguments in the integration test
build before collecting the **unfiltered** CTest inventory:

```sh
cmake --preset macos-dev \
  -DPS2R_MOVIE_ORACLE_EXPORT_ROOT="$M2_OUT/oracles" \
  -DPS2R_MOVIE_ORACLE_SOURCE_MANIFEST="$M2_OUT/sources.json"
ctest --preset macos-dev --show-only=json-v1 > "$M2_OUT/ctest-inventory.json"
ctest --preset macos-dev --timeout 300 -j8 \
  -E "$(python3 verify/retirement_scope.py --regex)" > "$M2_OUT/ctest.log" 2>&1
```

Use fresh export directories. The registered MovieLifecycleOracle and
MovieStepOracle commands include `--both-paths`; MovieStartOracle uses
`--start-paths`, MovieWaitOracle `--wait-paths`, and MovieStopOracle
`--stop-paths`. All include both explicit export arguments; receipts must
match that exact command. Do not reuse exports from another build.

While defaults are still off, the composed **preview** battery is:

```sh
python3 verify/battery.py --binary "$M2_BIN" --reps 3 \
  --lanes frames,audio --advisory-mem --timeout 900 --out "$M2_OUT/preview-battery" \
  --set hle.movie_lifecycle=on --set hle.stream_files=on \
  --set hle.music=on --set hle.music_buffers=on --set hle.graphics_movie=on \
  --set hle.movie_step=on --set hle.movie_start=on \
  --set hle.movie_wait=on --set hle.movie_stop=on
```

All 13 legs and 39 completed runs are required. Explicit selections change
the fingerprint; a preview is not a default-retirement receipt. After paired
outputs are reviewed, provider migration is complete and defaults separately
approved/integrated, collect the fresh formal gate without overrides:

```sh
python3 verify/gate_run.py --retirement --release "$M2_BIN" \
  --out "$M2_OUT/accepted-candidate" \
  --ctest-log "$M2_OUT/ctest.log" --ctest-inventory "$M2_OUT/ctest-inventory.json" \
  --retirement-evidence "$M2_OUT/evidence.json"
```

Frame/audio first-divergent keys must be stable and nonregressing or have
reviewed attribution. Memory is advisory. All actual replaced-operation
counts must be zero on every leg with positive owned work somewhere, and
unexercised legs must stay identified. No PCSX2 input, missing historical
playtest save, fabricated receipt or narrow-test default flip is part of this
acceptance.
