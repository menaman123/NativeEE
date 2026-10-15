# M2 movie format and caller audit

All 22 packaged PSS files were read completely: **958,234,712 bytes**.
The current PssDemux/FmvAudioCapture accepted MPEG2 sequence extensions and
PCM from every file. Observed sequence sizes are 512×288, 512×448 and
640×480; every observed frame-rate code is 5. Accepted audio is format 1,
48 kHz, two channels, 512-byte interleave, as checked by
`engine/ipu/hle/fmv_audio_capture.cpp::ParseHeader`.

The known NTSC player selects MPEG2 and RGB32. This inventory found no
additional format required by that caller. It is a container/format audit,
not a full-frame decoder or presentation comparison. Proprietary captured
IPU column/row formats remain unknown and retain their fallback.

## Complete asset inventory

Paths are relative to `game_files/example_title.rep/FMV`. The accompanying
[machine-readable inventory](movie_asset_formats.json) records each full-file
SHA256, packet and sequence-header counts, MPEG2/PCM validity, PCM staging,
and the observed container-end marker. It includes the four nested BONUS
files; an initial top-level-only scan was discarded and rerun recursively.

| Asset | File bytes | Width × height | Accepted PCM frames | Declared PCM body bytes left |
|---|---:|---|---:|---:|
| ATTRACT.PSS | 79216644 | 512×448 | 2867968 | 9029 |
| B01_A.PSS | 38584324 | 512×288 | 1949184 | 10808 |
| B01_B.PSS | 39878660 | 512×288 | 2015232 | 9567 |
| B04_A.PSS | 67993604 | 512×288 | 3437568 | 9757 |
| BONUS/BONUS1.PSS | 14565380 | 640×480 | 524800 | 12578 |
| BONUS/BONUS2.PSS | 15597572 | 640×480 | 562432 | 5503 |
| BONUS/BONUS3.PSS | 10584068 | 640×480 | 381440 | 9813 |
| BONUS/BONUS4.PSS | 15073284 | 640×480 | 543232 | 10088 |
| COMPLETE.PSS | 101351428 | 640×480 | 3669760 | 24525 |
| H01_A.PSS | 35454980 | 512×288 | 1790464 | 12930 |
| H01_F.PSS | 61194244 | 512×288 | 3092736 | 13016 |
| H02_A.PSS | 46710788 | 512×288 | 2360064 | 12396 |
| H02_B.PSS | 16449540 | 512×288 | 828928 | 11303 |
| H02_KA.PSS | 24739844 | 512×288 | 1248256 | 12065 |
| H02_N.PSS | 6029316 | 512×288 | 301056 | 18152 |
| H03_F.PSS | 74678276 | 512×288 | 3774208 | 13412 |
| H04_A.PSS | 67993604 | 512×288 | 3437568 | 9757 |
| H04_DA.PSS | 44335108 | 512×288 | 2240000 | 12404 |
| H04_DB.PSS | 65798148 | 512×288 | 3323648 | 68229 |
| SPYRO.PSS | 108937220 | 512×448 | 3944192 | 15091 |
| TTIDENT.PSS | 15941636 | 512×448 | 574720 | 4839 |
| VIVENDI.PSS | 7127044 | 640×480 | 322304 | 9503 |

The existing parser stops payload processing at the video end marker. All
22 files therefore retain declared PCM body bytes, up to 68,229, despite the
complete input file being fed to the parser. These are measurements of the
current behavior, not proof of lost audible output. No tail-drain workaround
or parser policy change was introduced by this audit.

## Source and NTSC ABI

Read-only specification: `the title-reversed/src/platform/ps2/movie/`
(`player.cpp`, `decoder.cpp`, `movie.h`, `mpegoutput.cpp`, `mpegmotion.cpp`)
and `src/game/movie.cpp`. No decomp implementation was copied.

The supplied `SLUS_123.45` ELF has SHA256
`8d114879fb704d328394771aec6276136bdb4ecf4c5f8de06139b7b42974038f`.
The player loads format 2 at `0x2ae388` and stores it at **`0x2ae39c`**,
the delay slot of the call at `0x2ae398`. It loads RGB32 `0x100` at
`0x2ae3b4` and passes that output argument at `0x2ae3e8`.

| Movie boundary | Observed generated direct caller |
|---|---|
| Initialise `0x2b0d30` | Prepare `0x2ae1c8` |
| Open `0x2b0dc8` | Player Start `0x2ae318` |
| Start `0x2afc00` | Open `0x2b0dc8` |
| DecodePicture `0x2b01e8` | DecodeAndUpload `0x2b0e50` |
| Finished `0x2b0f68` | Player Start `0x2ae318`, Step `0x2aea08` |
| Close `0x2b1000` | Player Stop `0x2ae658` |

No literal ELF address words reference these six lifecycle entries.
That establishes the observed direct-call closure, not all possible
computed indirect targets. RGB16/image variants have software boundary
tests, but the known player requires RGB32.

The real scratchpad starts described in
[subsystem_completion_dma.md](subsystem_completion_dma.md) are internal
software MPEG implementation details: CopyMacroblocks PAL `0x2b89a8` →
NTSC `0x2b8110` uses D9/D8 transfers, and FetchReferences NTSC `0x2bed28`
uses a D9 REF/REFE chain. Public software picture delivery bypasses both.
Startup register-reset tables are not real SPR starts. This closure needs
no DMA fidelity work and is not a claim about every non-movie SPR caller.
The positional PAL→NTSC proposal for FetchReferences currently says
`0x2bf140` with **low** confidence. The independently inspected NTSC body
is `0x2bed28`; the proposal is not used as authority for this closure.

## Public player, service and callback closure

[movie_caller_inventory.json](movie_caller_inventory.json) records every
generated player/decoder function from `0x2ae018` through `0x2b17b8`, the
public MPEG/IPU entries, both real SPR helpers and presentation boundaries.
Each record includes its body digest, direct call sites and return addresses,
declared continuations and candidate literal ELF address words. The inventory
also pins all nine MoviePlayer vtable methods. It is reproducible with:

```sh
python3 tools/movie_callers.py --root . \
  --elf $PS2R_OLD_ROOT/game_files/example_title.rep/SLUS_123.45 \
  --out docs/design/movie_caller_inventory.json --check
```

The script scans NEW generated code and the NTSC ELF, without copying decomp
implementation. Literal words and direct callers do not exclude computed
indirect calls. Vtable/source review and the actual-operation census remain
part of acceptance.

| Source intent | NTSC caller/ABI and current disposition |
|---|---|
| Construct and choose the player's format | `0x2af7d0` initializes the player, installs the table at `0x3068e0`, then calls `0x2af848`. These remain game data operations. Start explicitly reselects PSS/PCM/MPEG2/RGB32; constructor format alternatives do not establish an additional required live format. |
| BeginPresenting and Open | Inline in MoviePlayer Start `0x2ae318`, vtable word `0x3068ec`. It lends sound/music, builds the filename, calls Prepare `0x2ae1c8`, OpenStream `0x2ae860` and Open `0x2b0dc8`. Game decisions remain generated. Selected MovieStream owns both loans and lookup; MovieDecoder owns Start `0x2afc00`. |
| Two-picture priming | Start calls DecodeAndUpload `0x2b0e50` at return addresses `0x2ae458` and `0x2ae528`, with the unchanged first queue between them. Selected MovieGraphics supplies immutable picture IDs instead of packet addresses. The initial audio-fill loop exits at input exhaustion; playback EOF still waits for accepted decoder backlog. |
| StartSound and Close's audio stop | Checked `sceSdRemote` service `0x2c4cf0`, returns `0x2ae620` / `0x2ae6e4`, command `0x80e0`, core zero, loop mode `0x13` / stop mode `2`. MovieDecoder checks the complete caller/argument/ownership contract before acknowledging the already software-fed PCM path. Other callers retain their real service. |
| Step | `0x2aea08`, vtable word `0x306904`. It asks Finished, queues the decoded picture when space exists, calls DecodeAndUpload and FeedAudio, and increments the player's frame count. The game's algorithm and queue cadence remain unchanged. |
| WaitFrame | Vtable word `0x306914` points to **`0x2af8f0`**. The shard has a prelude/spin tail at `0x2af8f4`, but live NEW dispatch actually registers a separate bounded-refill/direct-present lambda in `recompiled.cpp`. Inventory v2 hashes it separately. Default-off `hle.movie_wait` replaces that complete service, keeping up to two ReadOn calls and one game presentation without adding another clock wait. Selected tail/partial entry refuses. `MovieWaitOracle` compares the actual registered wrapper, not an invented spin-loop control. |
| QueuePicture and Draw | `0x2af968` / `0x2aeb30`, vtable words `0x30691c` / `0x30690c`. MovieGraphics owns the immediate upload and bucket-27 draw; the three actual replaced producer calls are separately counted. Cropping, strip rounding and OLD sampled-edge extension are Movie-view-only. |
| Vertical blank / presentation | Start/Stop are the only NEW direct `sceGsSyncVCallback` callers. Default-off `hle.movie_stop`, requiring owned Step/Wait, removes that matched registration/unregistration and rejects old callback `0x2af568`; live Wait already calls complete game PresentMovieFrame `0x19fcb8`. The actual Libgraph count supplies its positive/zero control. GraphicsMovie selects completed owned SceneColor but **still runs retained presentation transport**. M7 counts those calls separately. |
| Stop, abort and reopen | `0x2ae658`, vtable word `0x3068f4`: selected MovieStop closes the stream, mutes/stops software audio, returns the ring, closes the decoder, and preserves complete game renderer recovery and ReclaimSound. RequestStop accepts only StatePlaying; failed Start returns idle. Selected closed/foreign/partial Stop refuses before any teardown or delegation. The source does not clear `player.opened`, so that word is not a checkpoint quiescence proof. EOF/error retains ownership until Close. See `hle/fmv/MOVIE_STOP.md` for source/ABI/provider boundaries. |
| Player destruction and sound lending | Table words `0x3068fc`, `0x306924`, `0x30692c` select `0x2af500`, `0x2af9b8`, `0x2afa00`. String destruction and gameplay lend/reclaim decisions remain generated. They are not counted as retired hardware operations. |
| Decoder refill callbacks | ReadOn `0x2b0fe0` is reached from Start, WaitFrame's loop and `0x2b17b8`. The latter is reached from generated DecodePicture and `0x2b1208`. Selected input owns Demultiplex `0x2b0ae0`; selected software picture delivery does not register/reenter the old MPEG callbacks. |
| MPEG context and IPU bookkeeping | Initialise is the sole observed direct caller of `sceMpegInit 0x2b7b50`; Close is the sole observed direct caller of `sceMpegDelete 0x2b7c00`. Selected lifecycle replaces both containing owners. Create/Reset and public RGB32/RAW8 software picture services bypass the old decode and conversion continuation. The selected provider constructor now requires no LibipuApi/DMAC references. Unexpected retained initialization rejects before software/PCM mutation. |
| Real scratchpad DMA | CopyMacroblocks `0x2b8110` callers are `0x2b83a0` / `0x2b8660`; FetchReferences `0x2bed28` caller is `0x2bca80`. These are internal to the replaced public software-picture implementation. Startup reset-table entries are not transfer starts. |
| Copied scratchpad stack | Selected `hle.movie_step` replaces the complete forwarding Step `0x2aea08`, preserving queue/decode/feed/frame behavior. Selected `hle.movie_start` separately preserves Start `0x2ae318` and its two primes through the complete owned service set. Both bypass the obsolete copied-stack/DI/EI/cache wrappers. `MovieStepOracle` / `MovieStartOracle` execute unchanged generated bodies and memcpy for their positive controls and compare picture/queue/EOF/ABI output. These CPU copies are **not** the D8/D9 starts above. The source BeginPresenting registration, Stop/WaitFrame and presentation transport remain separate dependencies. |

The retained non-MPEG branches in `DecodePicture 0x2b01e8` use FDEC/IDEC
and conversion helpers `0x2b0660`, `0x2b07d8`, `0x2b0990`. No packaged asset
or source player requires them in this inventory. RGB16 and image output
variants have boundary/writer tests; captured IPU row/column input is still
unknown. When owned buffers or pictures are selected, unsupported continuations
must refuse rather than pass an opaque ID to those retained consumers.

Selected MovieGraphics owns DecodeAndUpload/QueuePicture/Draw for its whole
installation lifetime. Calls before Start, after Close or at split
continuations refuse before decoder, output or captured-body execution.
Close leaves source queue words intact and accepted immutable render leases
alive; it does not make those words packet addresses again. An output refusal
remains visible to the root checkpoint predicate after cleanup. The actual
generated-output oracle pins all three closed boundaries, retained-call/GIF
zeros, unchanged queues and eventual delivery of already accepted leases.
This carries OLD `FMV2_IMPL`'s accepted backlog and `SOUND_P5_IMPL`'s explicit
Close cut, without treating file EOF as presentation teardown.

## Readiness and remaining ownership

M2 remains **implementing / awaiting composed integration and acceptance**.

The worktree's closed MOVI reconstruction now prepares MPEG contexts at the
saved GCLK before teardown and retains them through joined apply. Movie PCM
must already be closed; restoring that software clock neither flushes PCM nor
executes a volume intent. The root registers the resource preflight with a
paired discard, so failed/cancelled requests do not keep unpublished contexts.
Active decode, PCM, loans and presentation still refuse capture. See
`app/host/MOVIE_CHECKPOINT.md` for the M0/M7 integration and validation boundary.
The earlier integrated lifecycle/file/PCM selection preserved all 1,500 frame
and 250 audio keys, and the parent observed both movies with audible output.
That is bounded evidence, not acceptance of the later buffer/Graphics cohort
or the full 13×3 gate. `CanCheckpoint` holds ownership
from Start preparation through player Close, including provider EOF/error.
Ordinary Close preserves the delivery grid. The prepared application
`MovieCheckpoint` owner now persists closed decoder identity, shared file
cursor and MPEG delivery deadlines in REQUIRED-on-write `MOVI`. It validates
the incoming RAM and provider selection before teardown, restores after GCLK,
and permits only explicitly classified frozen legacy imports without MOVI.
An unconditional `ResetAfterRestore` **after** MOVI would erase that state and
must not be retained in root merges. Active decoder/PCM/Graphics queues and
either live buffer loan still cause a named checkpoint refusal.

Frozen OLD compatibility records were reviewed before each replacement:
`docs/loop_state/SOUND_P5_IMPL.md` (PCM format, first-picture gate, muted
consumption, no EOF flush, skip/reopen cut), plus `host_app/mpeg_hle.cpp`
(BUG-045 accepted backlog, two-picture priming, preserved delivery grid).
Tests cover those at the Movie service boundary. The inaudible deferred
PCM tail in deterministic mode is not reproduced as a metadata workaround.

Stream/Audio/Input and checked audio start/stop adapters are wired in the
integrated lifecycle selection. MovieStream and MPEG read-ahead share one
cursor. The later whole-phase worktree composes MovieStorage with the two
MusicBuffers consumers before exposing opaque handles, and GraphicsApplication
with immutable MovieOutput leases and shared source-frame snapshots.
MovieStream owns lookup/readiness without CD/IOP allocation. The software
picture owner bypasses upload builder `0x2b1448`.

Remaining provider contracts are concrete:

- **M5/M2:** the Movie worktree now installs `MovieAudio` through root
  `MovieSound` and the common mixer's `PcmSource`. It keeps one PCM queue,
  first-picture gate and voice→Movie→master/clamp order, while selected Movie
  gain and input consumption require no SpuCore parameter shadow. The actual
  retained-path oracle covers byte equality and positive/zero operation
  counters. Merge this provider alongside Audio's `VoiceSource`, then validate
  live output and the full gate. Never feed or consume PCM twice.
  The composed closed-Movie image must pass
  `ServiceState::ValidateMovie(service_state, {})`; no unlisted Movie loan
  may be renewed. Restore service state after the saved game clock and before
  Movie consumers.
- **M7:** finish SceneColor/depth/copy and source-frame persistence, keep
  deferred image leases alive through renderer completion, and retire the
  retained presentation transport before claiming MOV-3 complete. Completed
  source snapshots, not live guest/GS state, supply every deferred draw.
- **M0/M2:** register GCLK before service deadlines/MOVI, run the same
  incoming-image checks for boot and interactive restart before teardown, and
  remove blanket post-restore resets. The Movie worktree supplies the boot
  join and ServiceClock validation; MOVI v2 also owns closed PCM gain.
  Named active-Movie refusal remains.
- **Parent gates followed by M2:** same integrated binary with one selection
  changed at a time; actual retained-operation positive controls and selected
  zeros, positive owned pictures/PCM/loans, OLD pins, attributed frame/audio
  differences, then every leg of the 13×3 stability gate. Memory is advisory.

Neither the whole Movie subsystem nor all IPU formats are declared retired.

## Evidence

Worker evidence is under
`int_work/m_series/movie/worktree-stage5/`: `asset_formats.cpp`,
`asset-formats-2.tsv`, `asset-formats.json`, `asset-summary.log`,
`audit.py`, `audit.json`, `audit-2.log`, `unit.log`, and `services.log`.
The final inventory includes every file and complete input SHA256.
Stage 5 passed 2,884 sanitizer checks, 5,202 provider checks and ten NTSC
instruction pins/caller assertions. Stage 6 adds the optional input service
and actual captured-original counters: commit `0476432`, evidence
`worktree-stage6/HANDOFF.md`, 3,015 sanitizer checks, 5,255 provider checks,
149 SDK checks and 21 NTSC instruction pins.

The provider oracle compares 543 picture/clock keys and 1,179 field-audio
keys with 1,037 audible fields and 889,504 PCM frames consumed. It is not a
substitute for the formal BCLR-positive retained-NEW lifecycle artifact
oracle or the parent's 13-leg, three-repetition DT-12 acceptance.
No PCSX2 capture, pin, comparison or historical savestate is used here.

Whole-phase evidence is under `int_work/m_series/movie/whole-m2-stage20`
through `whole-m2-stage23`. Stage 20 adds closed software-clock persistence;
stage 21 composes required Movie state and incoming-image preflight, including
24 subsequent real pictures/PCM at three restored clock origins. Stage 22
removes required hardware interfaces from the selected software provider
(14 focused registered tests/lints pass, root syntax passes). Stage 23 exercises
both Movie consumers through actual ordered MusicBuffers providers, including
five close/reopen cycles and failure/teardown balancing; the composed root and
storage adapter compile. An independently found Graphics settings-schema
failure is preserved in its log and fixed separately. No worker game or shared
build was run for these stages.
