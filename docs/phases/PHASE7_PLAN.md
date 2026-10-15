# PHASE7_PLAN — `engine/ipu` (FMV) + `platform/sdl/{audio_sink, input}`: CONSTRUCTION_PLAN §4.7 to the MODULE GATE

Status: planning complete. §1–§9 are written; §10 is the self-review. Stage 7A starts from the HANDOFF template in §9.

Binding inputs, read in this order and nothing else for §1–§3: `/tmp/p7/DECISIONS.md` (orchestrator decisions D1–D8,
followed literally below), `/tmp/p7/INVESTIGATION.md` (facts; cited as INV §X with the OLD `file:line` it carries),
`docs/phases/STAGE_RULES.md`, `docs/phases/PHASE4_PLAN.md` §1/§2.7/§2.8/§7 + HANDOFF (the format exemplar),
`docs/design/CONSTRUCTION_PLAN.md` §4.7 (`:665-703`, the gate wording). OLD = `$PS2R_OLD_ROOT`,
FROZEN READ-ONLY (sources read, binaries run bounded as oracles only, never built/edited/`git`-touched). NEW = this worktree,
branch `phase7`, HEAD `78b5655 Phase 4 WIP (4A-4B'')` (INV header).

Every OLD anchor below is `file:line` from INV, re-checked against OLD on 2026-09-27. OLD identifiers are quoted only to name
what is transplanted, never adopted as NEW names.

## 1. Goal and scope — §4.7 to the module gate, no lane claim (DECISIONS scope)

### 1.1 Start state — Phase-4 modules ABSENT on this branch (INV D2, RISK 1)

- `engine/sound/`, `engine/pad/`, `engine/ipu/`, `engine/cdvd/`, `engine/platform/sdl/` contain ONLY `README.md` files
  (INV D1 inventory, D2). There is no `Mixer`, no `FmvStream`, no `PadProtocol`, no `IsoFs`, no `cdvd_stream::PumpSectors`
  equivalent. PHASE4_IMPL's module checklist reads "§2.4–§2.9, §2.11: – (4C onwards)" (INV E3).
- What DOES exist and is consumed read-only: `platform::InputState` (wire active-low word + 4 axis bytes, `platform.h:21`),
  `platform::AudioSpec{48000, 2, 800}` (`:22`), `TickSync` (`engine/ee/timing/tick_sync.h:22-56` — `NotePending(Cause::Demand)`
  / `FirePending()`, read for §3 only), `MmioHandler` (`mmio_handler.h:7-11`), `MmioRouter` (`mmio_router.h:16-57`),
  `MainMemory::Pointer(phys, size)` (`main_memory.h:21-22`), `InputLog` (`core/triage/input_log.h:22-71`),
  `AudioHasher` (`audio_hasher.h:18-42`), `CmdRing` (a command ring — NOT reusable as a PCM ring, INV D1),
  `SdkBindingManifest::Register/RegisterWrap` (`sdk_binding_manifest.h:59-61`), `PS2R_GENERATED_DIR`
  (root `CMakeLists.txt:28-30`), `ps2r_platform` (`engine/platform/CMakeLists.txt:2-9`).
- Pre-flight (the last green stop): `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev` → 88/88 green incl.
  `LintOneClass`, `LintEnginePurity`, `CoreSingleGetenvSite`, `SettingsTableReproducible`, `FlagPhaseCountsReproducible`
  (INV E3 last gate).

### 1.2 End state — three self-contained modules, unit- and oracle-proven, wired to nothing

- `engine/ipu/` + `engine/ipu/hle/`: the IPU register model, the pure MPEG intra decoder, the PSS demuxer, the FFmpeg-backed
  video decoder behind `VideoDecoder`, the two frame writers, the tick-domain `FmvPacer`, the FMV audio capture, the
  per-context state and `MpegApi` — all over the SMALL INTERFACES §1.4 names (D1), none of which is implemented by a Phase-4
  class on this branch. The pipeline decodes `VIVENDI.PSS`/`TTIDENT.PSS` end to end in a host-only test and produces the
  per-picture golden sequence of §4.3.
- `engine/platform/sdl/`: `AudioPullRing` (NEW mechanism, §3.1) + `SdlAudioSink` (the transplanted device half), and
  `engine/platform/sdl/input/` (the transplanted mapper/backends/system + `default_bindings.ini` as data).
- `tests/{ipu,input,platform_sdl}/` green in `ctest --preset macos-dev`; both lints 0; `CoreSingleGetenvSite` still 1;
  0 warnings under `-Wall -Wextra -Werror`; `PHASE7_IMPL.md` carries one section per stage + the INTEGRATION CHECKLIST (§8).
- NOTHING is wired: no `MmioRouter::Register`, no `SdkBindingManifest` row, no `BoundarySequencer` step, no `app/host`
  edit, no `Config` key, no `settings_table.tsv`/`FLAG_LEDGER`/README edit outside our owned paths (DECISIONS FORBIDDEN list).

### 1.3 Lanes claimable in this phase: NONE (module gate only)

- `frames`, `audio`, `mem` — none is claimed. The CONSTRUCTION §4.7 proof gate ("all lanes, all legs … 1,494 frame lines,
  162 audio windows, 17 memory checkpoints", `:686-690`) is the INTEGRATION GATE and is explicitly NOT this phase's (§6.2).
  It needs `Mixer`, `FmvStream`, `PadProtocol`, `IsoFs`/stream cursor, the sequencer slots and the title-side rows of §8,
  none of which exists here (§1.1).
- What IS proven: byte-level module contracts against OLD-derived goldens — the per-picture FMV sequence (§4.3), the strip
  layout (§4.2), the pacer arithmetic (OLD `test_fmv_pace` cases, INV A7), the PSS packet census (OLD `pss_census.py`,
  INV A8), the input mapper (OLD `test_input_mapper` cases, INV C3), the padlog→wire round trip against Phase 1's
  `InputLog` (INV C4), and the pull ring's arithmetic/ordering/underrun/overrun contract (§4.4).

### 1.4 In scope (DECISIONS OWNED + D1 interfaces)

- OWNED paths: `engine/ipu/**` (incl. `ipu/hle`), `engine/platform/sdl/audio_sink*`, `engine/platform/sdl/audio_pull_ring*`,
  `engine/platform/sdl/input/**` (+ `default_bindings.ini` as data), `tests/ipu/**`, `tests/input/**`, `tests/platform_sdl/**`
  (NEW test dir; the "tests mirror module paths" rule is bent for the platform folder — recorded as P7-TESTS-PLATFORM-SDL-DIR, §7.1),
  `cmake/FindFFmpeg.cmake`, `docs/phases/PHASE7_PLAN.md`, `docs/phases/PHASE7_IMPL.md`.
- SHARED, append-only, one `# Phase 7` block each: root `CMakeLists.txt`, `tests/CMakeLists.txt`,
  `engine/platform/CMakeLists.txt`.
- The interfaces Phase 7 DEFINES and codes against (D1) — each a declarations-only header in the module that consumes it:
  - `ps2r::platform::sdl::PcmSource` = the `AudioPullRing` itself (the sink pulls from the ring; the ring is the source).
  - `PadSample` (= OLD `PS2PadState`: `uint16` active-high buttons in wire order, `lx/ly/rx/ry` bytes, neutral `0x80`,
    `input_mapper.h:17-23`) → `InputSystem::Frame()` → `platform::InputState` (wire active-low) via `ToInputState()`.
  - `FmvPcmSink { virtual void Push(const int16_t* stereo, uint32_t frames)=0; virtual void GateOpen()=0;
    virtual void Flush()=0; }` — mirrors OLD `sound::FmvPushPcm/FmvGateOpen/FmvFlush` (`sound_core.h:168-170`, INV A2).
  - `FmvSectorSource { virtual uint32_t PumpSectors(uint8_t* dst, uint32_t max_sectors)=0; }` — OLD
    `cdvd_stream::PumpSectors` (`cdvd_stream.h:47`, INV A5).
  - `GuestMemory { uint8_t* Translate(uint32_t addr, size_t len); uint32_t Read32(uint32_t); void Write32(uint32_t, uint32_t); }`
    — `engine/ipu` never includes `ee/mem` for the HLE bodies (host-only tests). Checked: `engine/ee/mem` has
    `MainMemory::Pointer(phys, size)` (`main_memory.h:21`) but no virtual interface → `GuestMemory` is declared in
    `engine/ipu/hle/guest_memory.h`; the integration adapter over `MainMemory` is §8.2 item 5.
  - `FmvPacer` computes `wait_ticks` from a caller-supplied `now_tick`; the park on `TickSync` is integration. No sleeping,
    no clock read, no `TickSync` include anywhere in `engine/ipu`.
  - `DmacChannelFace` (ch3/ch4 CHCR/QWC/MADR/TADR word access, `engine/ipu/hle/dmac_channel_face.h`) — consumed only by
    `LibipuApi::StopDma/RestartDma`; the OLD access at `libipu.cpp:139-172`, `:212-264` becomes Phase 5 dmac's adapter (§8.2 item 6).
  - `VideoDecoder` (`engine/ipu/hle/video_decoder.h`) — the libav-free face `MpegContext` owns; `FfmpegVideoDecoder` is its only
    implementation here (§2.2).
- The FFmpeg dependency (`cmake/FindFFmpeg.cmake`, `find_path`/`find_library` under `/opt/homebrew`, `FATAL_ERROR` when absent —
  OLD's fallback pattern `host_app/CMakeLists.txt:16-56`, INV D3) and `find_package(SDL2 CONFIG)`
  (`/opt/homebrew/lib/cmake/SDL2/sdl2-config.cmake` present; `pkg-config` NOT on PATH, INV D3).

### 1.5 Out of scope (and where it goes)

- SDK binding rows (`libmpeg_bindings.cpp`, `libipu_bindings.cpp`), `MmioRouter` registration of `IpuDevice`, sequencer steps
  (the GetPicture `EeTimers` pump, the pacer park, the pad stamp, the mixer pump), `app/host` wiring, the DMAC ch3/ch4 CHCR/QWC
  coupling (`dmac.cpp:1500-1550`, INV A6) → INTEGRATION (§8, documented, not coded).
- `games/<t>/hle/workarounds/fmv_abort_hold` (the unledgered `0x2af0d8` hook, INV OPEN Q7) → title side, integration; only
  the engine mechanism `MpegApi::ShouldHoldAbort(ctx)` (OLD `mpeg_hle.cpp:2537-2547`) is ours (D3, P7-ABORT-HOLD).
- `FmvContextLayout` VALUES (the ctx offsets `0x40/0x50/0x58/0x5c/0x94/0xf0-0x100/0x148/0x154/0x158/0x15c`,
  `kPlayToEofTotal 0x40000000`, `*0x30a700`) → title data; the engine declares the POD with NO defaults (D4; INV RISK 6, D4 lint).
- `Config` keys (`fmv.*`, `audio.device`, ring depth), `settings_table.tsv`, `FLAG_LEDGER` rows `:343/:345` correction,
  `FLAG_PHASE_COUNTS.txt` → integration doc items (§8.2). Module-local settings are PODs (`IpuSettings`, `InputSettings`).
- The `fmv_dump` diag category (`PS2_MPEG_DUMP_N` DIAG): `core/diag` is read-only for us → integration item (D3).
- `sceMpegIsEnd`/`sceMpegDelete` handlers: never bound in OLD (`sce_hle_bindings.inc` has no row; INV D1 finding) → not rows.
- The legacy present-coupled wait (`mpeg_hle.cpp:2202-2222`), the debt-clamp `Next()` arm (`mpeg_hle.h:68-88` wall,
  `:158-178` tick), the `cap == 0` eager decode (`:1255`, `:1586`), the 250 ms single `wait_for` (`:680-687`), every `[R196-*]` probe, the
  `sticktrace`/`inlat` probes, `PS2_INPUT_SELFTEST`'s grammar, `input_latency.h`, `DefaultConfigPathImpl` (`mixer.cpp:86-92`),
  `SDL_GetBasePath` in engine code → not written (§5).
- `PS2_FMV_NATIVE` arm (`engine/ipu/hle/README.md` "Does not belong") → not written.

## 2. Module table

Conventions (STAGE_RULES; ARCHITECTURE §11/§13): one primary type per `.h/.cpp` pair, file = snake_case(type); headers
declarations-only (≤ 3-line inline bodies); no `.inc`; no session tags; comments state the PS2/SDK rule. "OLD source" is the
INV-verified `file:line`; "Flags applied" names the §5 rows whose OLD `getenv` site lies in that row's source (applied at
transplant time: NORM winning arm only; DIAG/DELETE/TEST not written). Every row below is CREATED by this phase; none edits a
file outside §1.4.

### 2.1 `engine/ipu/` — 3 rows (OLD `host_app/ipu.{h,cpp}`, 113+600 ln, INV A6)

| New file | Class | OLD source (file:line) | Flags applied |
|---|---|---|---|
| `engine/ipu/ipu_device.h/.cpp` | `IpuDevice` — CMD/CTRL/BP/TOP register model (`0x10002000/10/20/30`), `WriteCtrl` honours RST (bit 30) only, `ReadCtrl` recomputes `ifc/ofc = min(size/16, 8)`, BUSY (bit 31) reads 0 after every command; input/output FIFO `std::vector<uint8_t>` + bit cursor; `FeedInputFifo`/`DrainOutputFifo` (the DMAC ch3/ch4 face — coupling is integration); command dispatch BCLR / SETIQ (64 bytes via zigzag) / SETTH / FDEC / CSC (RGB32 only, as OLD) / IDEC; BDEC, VDEC, SETVQ, PACK are `// diverges:` stubs (OLD logs "(stub)"/unhandled). Implements `MmioHandler` so integration can `Register` it over `[0x10002000, 0x10002040)` — the registration itself is NOT here. The FIFO port `0x10007010` is NOT modelled (OLD omission kept for `unhandled_mmio` byte-identity; INV OPEN Q8 → recorded in §8.2) | `ipu.h:10-21` (`IPUCommand`), `:24-37` (`IPUCtrl`), `:41-66` (API), `:96-98` (FIFO fields); `ipu.cpp:102-123` (switch), `:138-148` (RST), `:161-162` (ifc/ofc), `:191/:203`, `:208-216` (BCLR), `:220-236` (SETIQ), `:240-245` (SETTH), `:250-261` (FDEC), `:267-270` (BDEC stub), `:275-318` (CSC), `:323-371` (IDEC); MMIO map `memory.cpp:618-627`, `:1059-1068` | — |
| `engine/ipu/mpeg_decode.h/.cpp` | `MpegDecode` — pure: VLC/DC/AC tables, zigzag, IQ, float IDCT, YCbCr→RGB32 (`DecodeIntraMacroblock`, `DecodeBlock`, `DecodeDcCoeff`, `DecodeAcCoeffs`, `InverseDct`, `YCbCrToRgb32`); no FIFO, no registers — `IpuDevice` calls into it for IDEC/CSC | `ipu.cpp:376-600` (`:376`, `:394`, `:423`, `:465`, `:516`, `:554`) | — |
| `engine/ipu/ipu_settings.h` | `IpuSettings` POD — `decode_ahead_pictures = 64` (`kDecodeAheadPictures`, D3), `sectors_per_pump = 16`, `max_sector_budget = 4096`; module-local, no `Config` edit (a header-only POD; no `.cpp`) | `mpeg_hle.cpp:1228-1245` (cap default 64), `:1679-1680` (`kSectorsPerPump`, `kMaxSectorBudget`) | `PS2_FMV_DECODE_CAP` (ledgered DELETE; the ON-arm constant is written — P7-LEDGER-CAP, §5.5) |

### 2.2 `engine/ipu/hle/` — 14 rows (OLD `host_app/mpeg_hle.{h,cpp}` 318+2559 ln, INV A1–A5; `sce_hle/libipu.cpp` 266 ln)

Five of the rows are declarations-only interface headers (`guest_memory.h`, `fmv_pcm_sink.h`, `fmv_sector_source.h`,
`dmac_channel_face.h`, `video_decoder.h`): each declares one primary type with pure-virtual member functions, so under
`LintOneClass` each needs its own header (no `.cpp`). One row is the layout POD.

| New file | Class | OLD source (file:line) | Flags applied |
|---|---|---|---|
| `engine/ipu/hle/guest_memory.h` | `GuestMemory` — pure interface (`Translate(addr,len)`, `Read32`, `Write32`); declared here because `MpegApi`/`FrameWriter` consume it and `engine/ee/mem` exposes no virtual face (§1.4) | (NEW interface; replaces `memory::translate_address` / `memory::read<u32>` at `mpeg_hle.cpp:1729-1766`, `:1785`, `:1613`) | — |
| `engine/ipu/hle/fmv_pcm_sink.h` | `FmvPcmSink` — pure interface (`Push(const int16_t* stereo, uint32_t frames)`, `GateOpen()`, `Flush()`); consumed by `FmvAudioCapture`/`MpegApi`; implemented at integration by the Phase-4 `FmvStream` (§8.2 item 2) | (NEW interface mirroring OLD `sound::FmvPushPcm/FmvGateOpen/FmvFlush`, `sound_core.h:168-170`; INV A2) | — |
| `engine/ipu/hle/fmv_sector_source.h` | `FmvSectorSource` — pure interface (`uint32_t PumpSectors(uint8_t* dst, uint32_t max_sectors)`); consumed by `MpegApi::PumpUntilFrame`; implemented at integration over the Phase-4 stream reader (§8.2 item 3) | (NEW interface mirroring OLD `cdvd_stream::PumpSectors`, `cdvd_stream.h:47`; INV A5) | — |
| `engine/ipu/hle/dmac_channel_face.h` | `DmacChannelFace` — pure interface (`ReadChcr(ch)`, `WriteChcr(ch, v)`, `ReadQwc(ch)`, `WriteQwc(ch, v)` + the MADR/TADR words the 9-word snapshot needs, for `ch ∈ {3, 4}`); consumed ONLY by `LibipuApi::StopDma/RestartDma`; implemented at integration by Phase 5's dmac (§8.2 item 6) | (NEW interface over the ch3/ch4 CHCR/QWC access at `libipu.cpp:139-172`, `:212-264`) | — |
| `engine/ipu/hle/fmv_context_layout.h` | `FmvContextLayout` POD — the ctx offsets the title fills at integration (`inner_status_ptr`, `play_total`, `plane_w`, `plane_h`, `raw8_mode`, `plane_table`, `plane_select`, `ring_base/size/write/read/anchor`, `scene_state`, `abort_flag`, `play_to_eof_total`) — declared as a PARAMETER type with NO defaults; nothing in `engine/` names a value | `mpeg_hle.cpp:873-877` (ring offsets), `:1013-1035` (inner status), `:1175-1200` (completion gate), `libmpeg.cpp:706-714` (RAW8 plane select) — offsets NOT copied | — |
| `engine/ipu/hle/pss_demux.h/.cpp` | `PssDemux` — the `ProcessBuffer` packet walk: start-code scan, `0xB9` program end (→ `OnProgramEnd`), `0xBA` pack header (12 / `14 + (buf[13]&7)`), length-prefixed system headers `{BB,BC,BE,BF,F0,F1,F2,F8,FF}`, PES `pkt_len` / zero-length search, video `E0..EF` (→ `OnVideoEs(bytes)` after `ParsePesPayloadOffset`; sequence-end `00 00 01 B7` detection), audio `BD`/`C0..DF` (→ `OnAudioPes(bytes)`), 3-byte tail keep unless final, `ErasePrefix`. Callbacks only — no decoder, no ring mirror, no guest access | `mpeg_hle.cpp:82-105` (ids), `:754-762`, `:764-773`, `:775-776`, `:778-788`, `:790-792`, `:797-823`, `:825-835`, `:1463-1625` (walk; `:1431-1461` census NOT written) | `PS2_MPEG_SEQCENSUS` (DELETE, not written) |
| `engine/ipu/hle/video_decoder.h` | `VideoDecoder` — interface: `Feed(data,size,out)`, `Flush(out)`, `AuthoredFps()`, `Shutdown()`; `DecodedPicture {int w, h; std::vector<uint8_t> rgba;}` (RGBA8 row-major) | `mpeg_hle.cpp:111-115` (`DecodedFrame`), `:139-172`, `:176-201`, `:209`, `:211-233` (the public shape of `FfmpegDecoder`) | — |
| `engine/ipu/hle/ffmpeg_video_decoder.h/.cpp` | `FfmpegVideoDecoder : VideoDecoder` — exactly OLD's libav calls: `avcodec_find_decoder(AV_CODEC_ID_MPEG2VIDEO)`, `av_parser_init`, `avcodec_alloc_context3`, `thread_count = 1`, `avcodec_open2`, `av_parser_parse2(…, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0)`, `avcodec_send_packet`/`receive_frame` with EAGAIN drain-and-retry, `sws_getContext(w,h,fmt → w,h,AV_PIX_FMT_RGBA, SWS_BILINEAR)`, `sws_scale` with `dst_ls = {w*4,0,0,0}`, authored fps from `codec_ctx->framerate` after the first frame; `Flush` = NULL parser packet then `send_packet(nullptr)`. Also exposes `static unsigned LibavcodecVersion()` (= `avcodec_version()`) for the fixture header (§4.3) | `mpeg_hle.cpp:125-380` (`:152-156`, `:220-227`, `:236-274`, `:264`, `:276-303`, `:305-320`, `:314-316`, `:322-361`, `:333-338`, `:348-354`) | — |
| `engine/ipu/hle/frame_writer.h/.cpp` | `FrameWriter` — `WriteRgb32Strips(uint8_t* dst, const DecodedPicture&)`: aligned `W/H` to 16, `mb_cols` strips of `aligned_h` rows × 64 bytes, RGB from the picture, alpha forced `0x80` (PS2 opaque), outside → `0,0,0,0x80` (INV A1.c quoted loops, byte-exact); `WriteRgb16Strips(dst, target_w, target_h, pic)`: same strips × 32-byte rows, `r5|g5<<5|b5<<10|0x8000`, outside `0x8000`, LE store, sampling clamped to the decoded size. Pure byte layout into a caller-supplied span (`GuestMemory::Translate` is the caller's) | `mpeg_hle.cpp:1773-1822` (RGB32), `:1832-1886` (RGB16; `:1848-1849`, `:1869`, `:1872-1880`) | — |
| `engine/ipu/hle/fmv_pacer.h/.cpp` | `FmvPacer` — ONE arithmetic core on an `int64` tick domain: `ticks_per_picture = (int64)(60.0/fps + 0.5)`, min 1; `Next(now_tick)` = OLD `TickPace::NextBurst` semantics as the SOLE path ("first delivery arms", late → `late=true, wait=0`, NO debt clamp — grid unchanged); `Reset()`, `Armed()`; `frames_served < 2` → instant (the caller's rule, exposed as `ShouldPace(frames_served)`); rate latch with the `29.97` fallback; `FmvActive()` signal (the explicit replacement for `DecodeActiveRecently`, HACK_LEDGER `:307`) | `mpeg_hle.h:146-` (`TickPace`; debt-clamp `Next :158-178` NOT written, `Reset/Armed :179-180`, `NextBurst :188-`), `:145-148`; `mpeg_hle.cpp:584`, `:586-608`, `:636-643`, `:648` (tick `NextBurst` call; `:651-653` target); the wall-clock `PaceDeadline` (`mpeg_hle.h:56-105`; `Next :68-88`, `Reset/Armed :89-90`, `NextBurst :107-`) is NOT written — the domain is ticks only | `PS2_MPEG_PACE_AUTHORED` (NORM), `PS2_MPEG_PACE_OFF` (DELETE), `PS2_FMV_OPEN_BURST` (ledgered DELETE; ON arm written as the sole path — P7-LEDGER-BURST, §5.5) |
| `engine/ipu/hle/fmv_audio_capture.h/.cpp` | `FmvAudioCapture` — PES `0xBD` payload: 4-byte prefix `FF A0 00 00` (`p[0] != 0xFF` → skip), first packet `SShd` len `0x18` `{format=1, rate=48000, channels=2, interleave=0x200}` validated (else `disabled`), `SSbd` body; per complete 1024-byte planar pair → 256 interleaved stereo frames → `FmvPcmSink::Push(fr, 256)`; `GateOpen()` on the first served picture and `Flush()` on Init/Destroy/Reset are `MpegApi`'s calls | `mpeg_hle.cpp:1302-1312` (rationale), `:1322-1428` (`:1331`, `:1372-1384`, `:1409-1425`); sink API `sound_core.h:155-177` | `PS2_SND_FMV` (NORM: capture unconditional) |
| `engine/ipu/hle/mpeg_context.h/.cpp` | `MpegContext` — per-ctx state = OLD `PlaybackState`: `width/height` (default 512×256), `frames_served`, `saw_input`, `stream_ended`, `end_after_pending`, `pss_buffer`, `std::deque<DecodedPicture> frames`, `std::unique_ptr<VideoDecoder>`, `pending_es` + offset (decode-ahead deferral, 64 KiB slices, compaction > 4 MiB), `FmvPacer`, `FmvAudioCapture` state; `StreamEnded()` = `stream_ended && frames.empty() && frames_served > 0` | `mpeg_hle.cpp:387-428`, `:747-749`, `:1252-1300` (`DrainPendingEs`), `:1584-1605` (deferral) | `PS2_FMV_DECODE_CAP` (constant via `IpuSettings`) |
| `engine/ipu/hle/mpeg_api.h/.cpp` | `MpegApi` — `Init/Destroy/Reset/NotifyCdStreamEof/DemuxPss/DemuxPssRing/GetPicture/GetPictureRaw8/ShouldHoldAbort/Width/Height` (+ `AlalcInit/AlalcAlloc/ClearRefBuff` as guest-word helpers, D7) over `GuestMemory` + a caller-passed `FmvContextLayout`; owns the ctx table (`std::unordered_map<uint32_t, MpegContext>`) under one mutex; the guest side effects `RingFillVideoPayload` (write cursor + wrap, dims log at the anchor), `RingDrainToWrite`, `WriteInnerStatus`, `HoldFmvCompletionGate` (hold `play_total` at `play_to_eof_total` while `scene_state == 2` until `StreamEnded`), `PumpUntilFrame` over `FmvSectorSource` (16 sectors/pump, ≤ 4096/call; ring-owning ctx only), the serve path (pop → `FrameWriter` → `RingDrainToWrite` → `frames_served++` → `GateOpen` on the first serve); returns `wait_ticks` from `FmvPacer` to the CALLER (the park is integration). No `EeTimers` pump, no `TickSync`, no logging probes | `mpeg_hle.cpp:1890-1896`, `:1898-1906`, `:1908-1942`, `:1944-1968`, `:1970-1998`, `:2000-2032`, `:2043-2424` (serve; `:2195` pace call, `:2223` pop, `:2313` write, `:2376` gate), `:2426-2529` (RAW8; `:2476-2478`, `:2516-2517`), `:2537-2547`, `:2549-2557`; `:881-970`, `:972-979`, `:1013-1035`, `:1175-1200`, `:1664-1720`, `:1729-1766`; `libmpeg.cpp:189-249` (alalc), `:403-424` (clear-ref slots) | `PS2_MPEG_PDIFF`, `PS2_MPEG_WM` (DELETE, not written); `PS2_MPEG_DUMP_N` (DIAG, site not written — §5.2) |
| `engine/ipu/hle/libipu_api.h/.cpp` | `LibipuApi` — the libipu SDK dance as a pure sequence over `IpuDevice`: `Init` = `WriteCtrl(RST)` then `WriteCmd(BCLR)`; `Sync` = poll CTRL.BUSY → 0 (returns 0); `StopDma`/`RestartDma` produce/consume the 9-word DMAC snapshot through the `DmacChannelFace` interface (row above; the ch3/ch4 CHCR/QWC access is integration). Landed in 7E (D7 allows 7D or 7E; 7E is fixed here so 7D's budget covers the decoder chain) | `libipu.cpp:67-77` (init), `:88-99`, `:102-105` (sync), `:139-172` (stop), `:212-264` (restart); OLD `[IPU]` census: 549 BCLR / 8 RESET / 0 IDEC in a coldboot log (INV A6) | — |
| `engine/ipu/README.md`, `engine/ipu/hle/README.md` | rows for every class above (purpose / belongs / does-not-belong / OLD sources) + a copy of the §8.1 binding table marked "integration — NOT installed by Phase 7" | — | — |

### 2.3 `engine/platform/sdl/` — 2 rows + README (OLD `host_app/sound/mixer.{h,cpp}` 29+94 ln, INV B)

| New file | Class | OLD source (file:line) | Flags applied |
|---|---|---|---|
| `engine/platform/sdl/audio_pull_ring.h/.cpp` | `AudioPullRing` — **NEW MECHANISM** (INV B2: OLD has no PCM output ring). SPSC float stereo ring: `kQuantumFrames = 800` (one vsync at 48 kHz), `kCapacityQuanta = 8` (6400 frames ≈ 133 ms); `Push(const float* interleaved, int frames)` (producer, boundary thread): overrun → drop the OLDEST quantum + `overruns++`, never blocks; `Pull(float* out, int frames)` (consumer, device thread): underrun → zero-fill the remainder + `underruns++`; counters `pushed/pulled/dropped/underruns/overruns`; `Available()`. Contiguous copies, wrap handled; no locks (acquire/release indices). Never feeds anything back to the engine | (none — NEW; rationale §3.1; the tick-clocked producer contract is OLD `verify_boundary.cpp:60-73` — `MixInto(mixbuf, 800)` once per boundary) | — |
| `engine/platform/sdl/audio_sink.h/.cpp` | `SdlAudioSink` — the transplanted device half: `Open(const AudioSpec&, AudioPullRing&)` → `SDL_AudioSpec{freq 48000, AUDIO_F32SYS, 2 ch, 512 samples}`, `SDL_OpenAudioDevice(nullptr, 0, &want, &got, /*allowed_changes*/0)`, `SDL_PauseAudioDevice(dev, 0)`; callback `= ring.Pull(out, len / (2*sizeof(float)))`; `Close()` → `SDL_CloseAudioDevice`; `Running()` — exactly the D4 surface (no `Lock()/Unlock()`: OLD `mixer.cpp:71`, `:77-82` bracket savestate load, which has no consumer in this phase; integration adds it if `core/savestate` needs the device paused). Idempotent on open. The `TickClocked()` suppression (`mixer.cpp:30-35`) is the CALLER's rule: under `time.tick_sync=true` integration never calls `Open` | `mixer.cpp:15`, `:17-21` (callback shape), `:25-60` (`:38-43` spec, `:45-48`, `:54`), `:62-69`, `:71`, `:77-82`; `mixer.h:1-29`; `:86-92` (`DefaultConfigPathImpl`) NOT written | `PS2_SND_TICKCLOCK` (NORM in Phase 4 — the device-clock arm is `time.tick_sync=false`, FLAG_LEDGER `:695`; read here, not counted) |
| `engine/platform/sdl/README.md` | rows for both classes; the NEW-mechanism note for `AudioPullRing` (play-mode latency/underrun behaviour differs from OLD's device-clocked mixing; not harness-visible; §3.1) | — | — |

### 2.4 `engine/platform/sdl/input/` — 8 rows + data + README (OLD `host_app/input/*` + `pad_input.cpp` device half, INV C)

| New file | Class | OLD source (file:line) | Flags applied |
|---|---|---|---|
| `engine/platform/sdl/input/input_backend.h` | `InputBackend` interface (`Poll(NormalizedInput&)`, `IsActive()`, `Name()`; MAIN thread only, after `SDL_PollEvent`) + `NormalizedInput` POD (sticks float [-1,+1], y down+, buttons ACTIVE HIGH, mouse dx/dy, 3 mouse buttons) + `enum LogicalButton` in DualShock 2 wire-bit order (`SELECT=1<<0 … SQUARE=1<<15`). SDL-free | `input_backend.h:14-22`, `:27-36`, `:38-46` | — |
| `engine/platform/sdl/input/pad_sample.h/.cpp` | `PadSample` POD (= OLD `PS2PadState`: `uint16 buttons` active-high, `lx/ly/rx/ry` neutral `0x80`) + free functions `ToInputState(const PadSample&) → platform::InputState` (ONE active-low inversion `wire = ~buttons`, OLD `pad_input.cpp:783`) and `FromInputState` | `input_mapper.h:17-23`; `pad_input.cpp:783-785` | — |
| `engine/platform/sdl/input/input_mapper.h/.cpp` | `InputMapper` + `Bindings` POD + free functions `StickToByte` (`lroundf(v*127.5f + 127.5f)`, clamped), `ScancodeFromName` (aliases `lshift/rshift/lctrl/rctrl/lalt/ralt`; case-insensitive; `SDL_GetScancodeFromName` is the ONLY SDL call in this module — no serializer here, see `BindingsRoundtrip` §2.5), `ParseMouseName` (`mouse1→0, mouse2→2, mouse3→1`); INI grammar verbatim (`[sticks]/[buttons]/[gamepad]`, `;`/`#` comments, strict floats, key=value before any section → reject, WHOLE-FILE rejection on any malformed line with the baked default untouched); `LoadConfigText(const std::string&)` (the file loader's body, exposed so tests need no temp files); `Map(NormalizedInput, dt)` (mouse accumulate `d * sens*0.01`, decay `exp(-decay*dt)`, snap `< 0.5/127.5`, clamp, `StickToByte` per axis, `buttons & 0xFFFF`); `KeyStickFromKeys` (opposites cancel); baked default = the configure-time generated header `${PS2R_GENERATED_DIR}/input_default_bindings.h` (`kDefaultBindingsIni` raw string) from `default_bindings.ini` | `input_mapper.h:26-43`, `:49-59`, `:61-93`; `input_mapper.cpp:4-6`, `:21-28`, `:47-53`, `:61-70`, `:72-79`, `:81-87`, `:95-206` (`ParseIni`), `:213-238`, `:236-249`, `:257-301`; generator `host_app/CMakeLists.txt:104-113` | — |
| `engine/platform/sdl/input/kbm_backend.h/.cpp` | `KeyboardMouseBackend : InputBackend` — `SDL_GetKeyboardState`, `SDL_GetRelativeMouseState`, relative-mouse capture on `SDL_MOUSEBUTTONDOWN`+LEFT, release on ESC / `SDL_WINDOWEVENT_FOCUS_LOST`; UI-capture suppression via a caller-supplied predicate; deltas only while captured; `mouse_buttons = LMASK/MMASK/RMASK`; `InjectKeyForTest(scancode)` overlay (member state, consumed per `Poll`); `static_assert(kNumScancodes == SDL_NUM_SCANCODES)` | `input_backend_kbm.h:7`, `:13-31`; `.cpp:15-17`, `:35-45`, `:47`, `:53-70`, `:78-87` | — |
| `engine/platform/sdl/input/gamepad_convert.h/.cpp` | `GamepadConvert` (pure, testable without a device) + `GamepadRaw {int16 axis[6]; uint8 button[21];}`: button table `A→CROSS, B→CIRCLE, X→SQUARE, Y→TRIANGLE, BACK→SELECT, START→START, LEFTSTICK→L3, RIGHTSTICK→R3, LEFTSHOULDER→L1, RIGHTSHOULDER→R1, DPAD_*`; triggers `> 32767/4` → L2/R2; `Pcsx2Stick(rawx, rawy, deadzone, sensitivity)` — normalize /32767, clamp, radial deadzone snap (`mag <= deadzone` → 0), scale by sensitivity, clamp per axis (INV C1 quoted) | `input_backend_gamepad.h:25-28`, `:37-38`; `.cpp:16-21`, `:45-73`, `:75-113` (`:78-93`, `:100-102`, `:106-112`) | — |
| `engine/platform/sdl/input/gamepad_backend.h/.cpp` | `GamepadBackend : InputBackend` — instance-owned selection state (`kSelectionAuto=-1`, `kSelectionNone=-2`, preferred GUID, deadzone, sensitivity, enabled — NO statics, HACK_LEDGER `:346`); `SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1")`, `SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)`, `SDL_GameControllerAddMappingsFromFile(<caller-supplied path>)`, device enumeration/open/close, generic fallback mapping, `SDL_JOYDEVICEADDED/REMOVED`; `Poll` multi-pad merge (buttons OR, sticks by larger magnitude); `Snapshot()` → `std::vector<PadInfo>` for a GUI | `input_backend_gamepad.h:42-43`, `:47-52`, `:54-100`; `.cpp:200`, `:204`, `:214-221`, `:229`, `:240`, `:251-292`, `:318`, `:346-358`, `:360-416` (`:365-368`, `:396-402`, `:414-415` sticktrace NOT written), `:390-392` | `PS2_STICKTRACE` (DIAG in Phase 4's count; probe not written) |
| `engine/platform/sdl/input/input_system.h/.cpp` | `InputSystem` — owns `InputMapper` + the two backends + `InputSettings`; `Init(settings)`: bindings file over the baked default (`LoadConfig` returns false when absent → default stays), backends constructed with the INI's deadzone/sensitivity/enabled, device config loaded from `settings.device_config_path` (`pad = auto|none|guid:<GUID>`, `kbm`, `deadzone`, `sensitivity`); `HandleEvent(const SDL_Event*)`; `Frame(dt) → PadSample`: poll kbm FIRST then gamepad, kbm result DISCARDED when `kbm_enabled=false`, merge `buttons |=`, sticks `|cand| > |cur| ? cand : cur` (strictly greater → kbm wins ties, INV C2 correction), mouse `dx/dy +=`, buttons `||=`, then `Map`; `SetUiCapture` → mouse accumulator reset + neutral sample; `SaveDeviceConfig()`; `LastSample()`. No `getenv`, no `SDL_GetBasePath`, no selftest injector, no ring-log probes | `input_system.h:22-27`, `:37-54`; `.cpp:37-47`, `:56-58`, `:85-127`, `:175-208`, `:209-243` (`:214-220` `PS2_INPUT` gate NOT written), `:268-273` (neutral), `:305-340` (merge/publish), `:126-155` + `:287-303` (selftest NOT written), `:349-389` (probes NOT written) | `PS2_INPUT` (NORM: always on), `PS2_INPUT_SELFTEST` (TEST: not written), `PS2_INPUT_LAT` (DIAG: not written) |
| `engine/platform/sdl/input/input_settings.h` | `InputSettings` POD — `bindings_path`, `device_config_path`, `gamecontrollerdb_path`, `kbm_enabled = true`, `gamepad_enabled = true`, `gamepad_deadzone = 0.15f`, `gamepad_sensitivity = 1.33f`; module-local (the `Config::input` mapping is integration) | `input_mapper.h:35-43` defaults; `input_system.cpp:77-83` (path rule NOT copied) | — |
| `engine/platform/sdl/input/default_bindings.ini` | data (values verbatim): `[sticks] mouse=right key_left_up=W key_left_down=S key_left_left=A key_left_right=D mouse_sensitivity=0.35 mouse_decay=12.0`; `[buttons] cross=Space square=LShift circle=Mouse2 triangle=E l1=1 l2=2 r1=3 r2=4 l3=5 r3=6 start=Y`; `[gamepad] enabled=1 deadzone=0.15 sensitivity=1.33`. Header REWORDED ("Default host input bindings"), the OLD narrative lines 1-5 dropped — `LintEnginePurity` scans `.ini` under `engine/` (INV D4, RISK 7a) | `default_bindings.ini:6-13`, `:15-27`, `:30-37` | — |
| `engine/platform/sdl/input/README.md` | rows for the eight classes + the data file; the thread rule (poll on the MAIN thread; the boundary receives a snapshot — §3.2) | — | — |

### 2.5 Tests — `tests/platform_sdl/` (2), `tests/input/` (3), `tests/ipu/` (7 + 1 tool + 1 script), each with its `CMakeLists.txt`

| New file | Class / ctest | OLD source (file:line) | Flags applied |
|---|---|---|---|
| `tests/platform_sdl/test_audio_pull_ring.cpp` — `AudioPullRing` | quantum arithmetic; N pushes of 800 then pulls of 512 → byte-exact stream order; underrun zero-fill + count; overrun drop-oldest-quantum + count; no drift over 10^5 quanta (`pulled == pushed − dropped − remaining`); SPSC thread test (one producer / one consumer) | (NEW contract §3.1) | — |
| `tests/platform_sdl/test_audio_sink.cpp` — `SdlAudioSink` | `SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy")` → `Open`/`Running`/`Close`; prints `SKIP` and exits 0 when SDL audio init fails | `mixer.cpp:25-71` | — |
| `tests/input/test_input_mapper.cpp` — `InputMapper` | ports OLD T1 (default key table incl. `LShift` alias, unbound → 0), T2 (mouse accumulate/decay: monotonic approach, decay to `0x80`, no drift), T3 (WASD: `W⇒ly 0x00, S⇒0xFF, W+S⇒0x80`, diagonals), T5 (INI parse: default parses; overrides; malformed line → whole-file rejection), gamepad tables (14 buttons, trigger 50 %/12.5 %, deadzone 0.10 snap / 0.16 pass, rails), `StickToByte`, held-stick invariance, full-deflection jitter, PCSX2 stick semantics; helpers as free functions / method-less PODs (lint scans `tests/`) | `host_app/input/test_input_mapper.cpp:44`, `:95`, `:184`, `:212`, `:274`, `:361`, `:377`, `:442`, `:490`, `:632-645`; CMake `tests/CMakeLists.txt:2154-2170` (no `SDL_Init`, no window) | — |
| `tests/input/test_padlog_replay.cpp` — `PadlogReplay` | padlog spec → `NormalizedInput` (inverse `StickToByte`, bits) → `InputMapper::Map` → `PadSample` → `ToInputState` wire → `InputLog::Feed` recorder → tokens byte-identical to the input spec; a synthetic padlog is the fixture. **Plan-time check:** `verify/baseline/*/` holds only `audio.ahash/frames.fhash/mem.mhash/run.meta` — no `input.padlog` exists → the OLD-anchor half is a recorded finding (P7-NO-ANCHOR-PADLOG, §7.1), not a test | `pad_input.cpp:461-570` (grammar), `:787-810` (overlay); NEW `input_log.cpp:101-152` | `PS2_PAD_SCRIPT` (TEST in Phase 4's count) |
| `tests/input/test_bindings_roundtrip.cpp` — `BindingsRoundtrip` | `default_bindings.ini` → `Bindings` → serialized INI → parsed again → identical `Bindings`; malformed line → whole-file rejection. OLD has NO `Bindings` serializer and the engine module keeps `SDL_GetScancodeFromName` as its ONLY SDL call (§2.4); therefore `SerializeBindings(const Bindings&) → std::string` is a TEST-LOCAL free function in this file (NEW test scaffolding, §3.3) that names keys through `SDL_GetScancodeName` and mouse buttons through the inverse of `ParseMouseName`; the test links `SDL2::SDL2` for it. Nothing under `engine/` gains a serializer or a second SDL call | `input_mapper.cpp:99-206` (the grammar the serializer must emit); `SDL_GetScancodeName` (SDL2 API, test only) | — |
| `tests/ipu/test_ipu_device.cpp` — `IpuDevice` | RST/BCLR/BUSY/ifc-ofc semantics; SETIQ zigzag; FDEC; CSC of a constant macroblock vs a computed reference | `ipu.cpp:138-148`, `:161-162`, `:208-245`, `:250-261`, `:275-318` | — |
| `tests/ipu/test_mpeg_decode.cpp` — `MpegDecode` | IDCT of a DC-only block; VLC table spot checks | `ipu.cpp:376-600` | — |
| `tests/ipu/test_pss_demux.cpp` — `PssDemux` (+ `tests/ipu/pss_census.py`, title-agnostic copy of OLD's tool) | synthetic PS stream (pack / system / video / audio / program-end); gated on `PS2R_OG_ROOT`: `VIVENDI.PSS` + `TTIDENT.PSS` from `OLD/game_files/example_title.rep/FMV/` vs `pss_census.py` counts (`video_pes`, `audio_pes`, `packs`, `sys`, `program_end`, `sequence_end`, `total_pictures`); SKIP when absent | `mpeg_hle.cpp:1463-1625`; `tools/pss_census.py:13-27`, `:52-77`, `:92-137` | — |
| `tests/ipu/test_frame_writer.cpp` — `FrameWriter` | known 20×18 picture → expected strip bytes (aligned 32×32, 2 strips, alpha `0x80`, outside black-opaque); RGB16 packing incl. clamped sampling | `mpeg_hle.cpp:1777-1821`, `:1832-1886` | — |
| `tests/ipu/test_fmv_pacer.cpp` — `FmvPacer` (ctest `FmvPaceDeadline` name kept) | ports the OLD burst + tick cases: unarmed / arm / steady drift-free / early jitter / small-late keeps grid / burst drains backlog / no-clamp floor / tick twin / one-per-tick / reset re-arms. The `long_stall_clamps_debt` cases are NOT ported (the clamp arm is not written) | `test_fmv_pace.cpp:48`, `:64`, `:79`, `:101`, `:123`, `:186`, `:208`, `:229`, `:257`, `:269`, `:298-450` (tick mirrors), `:452`; `:146` NOT ported | — |
| `tests/ipu/test_fmv_audio_capture.cpp` — `FmvAudioCapture` | `SShd` header validate (accept / each reject field); planar 1024-byte pair → 256 interleaved frames; `p[0] != 0xFF` skip | `mpeg_hle.cpp:1331`, `:1372-1384`, `:1409-1425` | — |
| `tests/ipu/test_libipu_api.cpp` — `LibipuApi` (7E) | `Init` over a fake `IpuDevice` register file = `RST` then `BCLR` observed in order; `Sync` returns 0 once `CTRL.BUSY` clears (bounded poll); `StopDma`/`RestartDma` round-trip the 9-word snapshot through a recording `DmacChannelFace` | `libipu.cpp:67-77`, `:88-105`, `:139-172`, `:212-264` | — |
| `tests/ipu/fmv_picture_oracle.cpp` — `FmvPictureOracle` (tool + ctest, OLD-gated) | demux + decode a `.PSS` → per-picture FNV-1a64 of `rgba` + P6 PPM bytes (OLD `PS2_MPEG_DUMP_N` layout) + sha256; `--manifest <file>` compare mode asserts the golden PREFIX property (§4.3); fixture `tests/ipu/fixtures/coldboot_pictures.sha256` | `mpeg_hle.cpp:2282-2299` (PPM layout: `P6\n%d %d\n255\n` + RGB triplets row-major; the dump precedes `WriteFrameToGuest` at `:2313`) | `PS2_MPEG_DUMP_N` (the OLD emitter used to MAKE the golden; no NEW env site) |

### 2.6 Row count per group

`engine/ipu` 3 · `engine/ipu/hle` 14 (incl. 5 interface headers — `guest_memory`, `fmv_pcm_sink`, `fmv_sector_source`,
`dmac_channel_face`, `video_decoder` — and 1 layout POD) + 2 READMEs · `engine/platform/sdl`
2 + README · `engine/platform/sdl/input` 8 + data + README · tests 12 executables (13 ctests — `FmvPictureOracle` is a tool +
ctest) + 1 script · CMake: `cmake/FindFFmpeg.cmake`
(new; included by `engine/ipu/CMakeLists.txt` via `include(${CMAKE_SOURCE_DIR}/cmake/FindFFmpeg.cmake)` — the root has no
`CMAKE_MODULE_PATH` and gains none), `# Phase 7` blocks in 3 shared files, 3 new test `CMakeLists.txt`, `engine/ipu/CMakeLists.txt`. CONSTRUCTION §4.7 names `ipu/hle/mpeg_ipu` — this
plan has no `MpegIpu`: its duties are split between `MpegContext` (state) and `MpegApi` (entry points) per D4; the
`core/diag/{input_script,input_latency}` row of §4.7 is not created (core/diag read-only; §5.2/§8.2).

## 3. Design — the determinism boundary and the FMV data flow

### 3.1 Audio: tick-clocked producer, wall-clocked consumer, one NEW ring between them (D2; BUG-002 / BUG-014)

Wall clock never enters the engine. In OLD the two modes are mutually exclusive (INV B1): under `PS2_SND_TICKCLOCK=1` the
device is never opened (`mixer.cpp:30-35`) and the boundary thread calls `MixInto(mixbuf, 800)` once per EE boundary
(`verify_boundary.cpp:60-73`); in play mode the SDL callback calls `MixInto` directly for 512-frame chunks at device rate
(`mixer.cpp:17-21`) — no ring exists anywhere (INV B2). NEW keeps the tick-clocked producer in BOTH modes and inserts the
`AudioPullRing` so the device thread can only ever READ what the boundary already produced:

```
   EE boundary thread (tick domain)                        │        SDL audio device thread (wall)
                                                            │
   BoundarySequencer  Step::Verify body  (Phase 4)          │
   ┌──────────────────────────────────────────────┐         │
   │ Mixer::Pump(800)  ─► mixbuf[800×2 float]     │         │
   │      ├─► AudioHasher::Push(mixbuf, 800)      │  audio.ahash lane (harness; unchanged)
   │      └─► AudioPullRing::Push(mixbuf, 800)    │ ──┐     │
   └──────────────────────────────────────────────┘   │     │
        (integration adapter, §8.2 item 1 —           │     │
         same buffer, AFTER the hasher push)          ▼     │
                                    ┌─────────────────────────────────┐
                                    │ AudioPullRing  (NEW, SPSC float) │
                                    │ quantum 800 fr = 1 vsync         │
                                    │ capacity 8 quanta = 6400 fr      │
                                    │ overrun  → drop OLDEST quantum,  │
                                    │            overruns++ (no block) │
                                    │ underrun → zero-fill, underruns++│
                                    └─────────────────────────────────┘
                                                            │        ▲  Pull(out, 512)
                                                            │        │
                                                            │   SDL callback (AUDIO_F32SYS, 2 ch, 512 samples)
                                                            │   SdlAudioSink::Open(spec, ring) — ONLY when
                                                            │   time.tick_sync == false (play mode);
                                                            │   under the deterministic profile the device
                                                            │   is never opened (OLD mixer.cpp:30-35)
                                                            │
        NOTHING crosses this line right-to-left: no counter, no clock, no back-pressure reaches the engine.
```

Consequences, recorded in `engine/platform/sdl/README.md` (INV B2): the game-visible sound state (NAX/ENDX/VOLX readbacks)
is a pure function of the EE frame counter in play mode too (the P0 pin's intent); audible latency becomes ring depth
(≤ 133 ms) instead of device-buffer latency; sustained producer lead is bounded by drop-oldest (audible skip + counter), sustained
lag by zero-fill (audible gap + counter). Not harness-visible (the harness never opens the device) → no accepted-difference
record; a play-mode note only. `PcmSource` (D1) IS the ring — the sink takes an `AudioPullRing&`, nothing else.

### 3.2 Input: main-thread poll, snapshot, tick-domain stamp (D2)

OLD polled SDL from the EE boundary thread under `PS2_MAINT_EEPACED=1` (`gs_hle.cpp:373-381`, INV C2 / RISK 10), violating
SDL's main-thread rule. NEW polls on the MAIN thread and hands a snapshot to the boundary:

```
   Host main thread (wall)                                  │   EE boundary thread (tick domain)
                                                            │
   SDL_PollEvent loop ─► InputSystem::HandleEvent(ev)       │
        │   (kbm capture/release, joystick add/remove)      │
        ▼                                                   │
   InputSystem::Frame(dt)                                   │
     ├─ KeyboardMouseBackend::Poll ─► NormalizedInput A     │
     ├─ GamepadBackend::Poll      ─► NormalizedInput B      │
     ├─ merge: buttons |= ; stick = |cand|>|cur| ? cand:cur │
     │         (kbm first → kbm wins ties, INV C2)          │
     └─ InputMapper::Map ─► PadSample {buttons AH, lx,ly,rx,ry}
                │                                           │
                ▼  ToInputState(): wire = ~buttons (ONE inversion, pad_input.cpp:783)
        platform::InputState snapshot  ───────────────────► │  Step::MaintDuties
        (Platform.poll_input fills it; atomic publish)      │    PadProtocol::Stamp(InputState)  (Phase 4)
                                                            │       → the two 128-byte PADMAN slots
                                                            │  game logic reads ONLY the stamped slot
                                                            │
   Headless legs: no backend is constructed; the source is InputLog replay or the neutral
   InputState{0xFFFF, 0x80×4} — selected by the harness (CLI), never by env (PS2_INPUT NORM).
```

The engine never blocks on input and never reads a clock for it; `dt` for the mouse decay is the caller's (frame period
under `time.tick_sync`, wall delta in play mode — a `Map` parameter, not a member clock).

### 3.3 FMV pipeline data flow (transplant of `mpeg_hle.cpp:1-28`'s header + INV A1)

```
   bytes IN                                   engine/ipu/hle                                     OUT
   ────────                                   ──────────────                                     ───
   sceMpegDemuxPss(ctx, addr, size)  ─┐
   sceMpegDemuxPssRing(ctx, …, ring) ─┼─► GuestMemory::Translate ─► MpegContext::pss_buffer
   FmvSectorSource::PumpSectors(16)  ─┘    (PumpUntilFrame: ring-owning ctx only, ≤ 4096 sectors/call)
                                               │
                                               ▼
                                        PssDemux::Process(buf, final)
                                          │ 0xBA pack / system hdrs / PES walk / 0xB9 end
                                          ├─ OnVideoEs(bytes) ──► RingFillVideoPayload (guest ring mirror, layout offsets)
                                          │                      ├─ frames.size() < 64 ? VideoDecoder::Feed
                                          │                      └─ else pending_es (deferred; DrainPendingEs on every serve)
                                          ├─ OnAudioPes(bytes) ─► FmvAudioCapture ─► FmvPcmSink::Push(256 fr)  (→ Phase-4 FmvStream)
                                          └─ OnProgramEnd()    ─► stream_ended / end_after_pending; Flush
                                               │
                                               ▼
                                        FfmpegVideoDecoder (thread_count=1, sws → RGBA) ─► deque<DecodedPicture> frames
                                               │
   sceMpegGetPicture(ctx, image_addr) ─► MpegApi::GetPicture:
        [integration: EeTimers pump first]  DrainPendingEs → PumpUntilFrame while frames.empty()
                                            FmvPacer::Next(now_tick) → wait_ticks  (returned to the caller; park = integration)
                                            pop_front ─► FrameWriter::WriteRgb32Strips(Translate(image_addr, total), pic)
                                            RingDrainToWrite; frames_served++; first serve → FmvPcmSink::GateOpen()
                                            WriteInnerStatus / HoldFmvCompletionGate (layout offsets, guest words)
   sceMpegGetPictureRAW8(ctx, plane, w, h) ─► MpegApi::GetPictureRaw8 ─► FrameWriter::WriteRgb16Strips(dst, w, h, pic)
```

Ownership: `MpegApi` owns the ctx table and the mutex; `MpegContext` owns the decoder, queue, pacer and capture state;
`PssDemux`, `FrameWriter`, `FmvPacer`, `FmvAudioCapture`, `MpegDecode` are pure (no guest access, no clock). What is TRANSPLANT
vs NEW: every class in §2.1/§2.2/§2.4 and `SdlAudioSink` transplant OLD logic with the same observable bytes; `AudioPullRing`
(§3.1), the five D1 interface headers (`GuestMemory`, `FmvPcmSink`, `FmvSectorSource`, `DmacChannelFace`, `VideoDecoder`),
`FmvContextLayout`-as-parameter, `PadSample::ToInputState` as a free function, the `FmvActive()` signal, and — test-side only —
the `SerializeBindings` free function of `BindingsRoundtrip` (§2.5; OLD has no `Bindings` serializer) are NEW shapes that OLD
expressed as globals, externs, hooks or not at all (`g_mutex/g_playback`, `memory::*`,
`sound::Fmv*`, `cdvd_stream::*`, `DecodeActiveRecently`). The `IDEC/CSC` decoder is transplanted although the coldboot FMVs
never use it (INV A6: 0 IDEC in a coldboot log) — the register model's BCLR/RST/BUSY is what the game sees.

## 4. Oracle and test strategy

Every test is host-only (no ISO, no guest, no window): the D1 interfaces are mocked by test-local free functions or
method-less PODs (`LintOneClass` scans `tests/`, INV D4 rule 1). OLD-gated tests receive `${PS2R_OG_ROOT}` as `argv[1]` and print
`SKIP` + exit 0 when the OLD file is absent (the `tests/core/CMakeLists.txt:44-49` pattern). Every ctest carries `TIMEOUT 120`
except `FmvPictureOracle` (600 — two full movies through libavcodec). ctest names: `IpuDevice`, `MpegDecode`, `PssDemux`,
`FrameWriter`, `FmvPaceDeadline` (OLD's name kept), `FmvAudioCapture`, `FmvPictureOracle`, `AudioPullRing`, `SdlAudioSinkDummy`,
`InputMapper` (OLD's name kept), `PadlogReplay`, `BindingsRoundtrip`.

### 4.1 OLD test ports (INV A7, C3)

- `test_fmv_pace.cpp` (498 ln, header-only against `mpeg_hle.h`) → `tests/ipu/test_fmv_pacer.cpp`: the tick-domain cases are
  ported 1:1 with `TP = 2`, `TK0 = 1000`; the wall-`ns` cases are re-keyed to ticks (the wall core is not written); the two
  `long_stall_clamps_debt` cases (`:146`, tick mirror in `:298-450`) are dropped and the `burst_no_clamp_floor` (`:257`) and
  `burst_drains_backlog_instantly` (`:229`) cases become the invariants of the sole `Next` (`next_tick == now + 2·TP` after a
  late arrival — NO floor). The `burst_steady_matches_next` case (`:208`) is vacuous with one core and is dropped.
- `host_app/input/test_input_mapper.cpp` (645 ln) → `tests/input/test_input_mapper.cpp`: all nine cases (§2.5 row) against the REAL
  `input_mapper.cpp` + `gamepad_convert.cpp`; links `SDL2::SDL2` for `SDL_GetScancodeFromName` and the `SDL_SCANCODE_*` /
  `SDL_CONTROLLER_*` constants only (`tests/CMakeLists.txt:2150-2167` — `-Wno-error` is NOT carried; the port compiles clean).

### 4.2 Byte-contract unit tests (NEW, from quoted OLD arithmetic)

- `FrameWriter`: a 20×18 picture with a known gradient → the 32×32 aligned image = 2 strips × 32 rows × 64 bytes; every
  in-picture pixel `{r,g,b,0x80}`, every padding pixel `{0,0,0,0x80}`; strip 1 begins at byte `32·64 = 2048`. RGB16: the same
  picture at `target 24×20` → `r5|g5<<5|b5<<10|0x8000`, LE, sampling clamped to 20×18, outside `0x8000`.
- `PssDemux`: a synthetic stream built by the test from the OLD rules (pack `0xBA` MPEG-2 with stuffing, `0xBB` system header,
  two `0xE0` PES with MPEG-2 header length, one `0xBD` PES, `0xB9`) → callback sequence and byte counts; a `pkt_len == 0` PES; a
  3-byte tail across two `Process` calls; `final=true` flush.
- `FmvAudioCapture`: `SShd` accept + four single-field rejects; a 1024-byte pair → 256 frames with `fr[2i]=L[i], fr[2i+1]=R[i]`;
  a packet whose `p[0] != 0xFF` is skipped without disabling.
- `IpuDevice`: after `WriteCtrl(1<<30)` CTRL reads `0` (BUSY clear, ifc/ofc 0); `WriteCmd(BCLR)` clears the bit cursor;
  `FeedInputFifo(64 bytes)` + `SETIQ` → the IQ table in zigzag order; `FDEC(fb)` peeks 32 bits at cursor+fb; CSC of one
  constant-Y/Cb/Cr macroblock → 256 identical RGB32 pixels equal to `MpegDecode::YCbCrToRgb32` of those constants.
- `MpegDecode`: DC-only block through `InverseDct` → a flat 8×8 within float rounding of `dc/8`; VLC spot checks for the
  DC-size and first AC codes.

### 4.3 Per-picture golden — module gate item (a), the exact procedure (DECISIONS D5; stage 7E)

OLD never emitted per-picture hashes (INV A4). Its `PS2_MPEG_DUMP_N=<n>` writes the first `n` SERVED pictures (RGB32 `GetPicture`
path only, global index across both intro movies, `Reset` clears the queue between movies) as `logs/mpegpic_%03d.ppm` — P6 RGB
from `f.rgba`, row-major — BEFORE `WriteFrameToGuest` (`mpeg_hle.cpp:2282-2299`, `:2313`). `run_leg.py` strips every inherited
`PS2_*` (`profile_deterministic.py:96-97`, INV D5), so a helper outside the tree arms it through `run_leg.py`'s own env builder
(`env = build_leg_env(os.environ, out_dir, tree, extra)` at `run_leg.py:101`).

**Meta-gate fact that shapes step 2:** OLD `run_meta.cpp:33-49` folds EVERY `PS2_*` variable except `PS2_VERIFY_*`
and `PS2_PAD_SCRIPT_FILE` into `env_ps2` and `env_hash`. Arming `PS2_MPEG_DUMP_N=1000` therefore changes `env_hash`, and
`diff_legs.py` WITHOUT `--cross-engine` asserts `env_hash` equality first and exits 2 `META-GATE-FAIL` before any lane compare
(`diff_legs.py:108-112`). The `--cross-engine` mode instead reports `gl_renderer/env_hash/build_id/tree` differences as NOTEs and
still gates on `profile` + `finalized=1` (`:100-106`). **`--cross-engine` is used here for that meta policy ONLY — both legs are
OLD binaries; no engine claim is made.** Step 2b then proves the env delta is exactly the dump variable and nothing else.

All commands run from the worktree root:

```sh
cd <repo>-p7
OLD=$PS2R_OLD_ROOT

# 0. Pre-state: OLD's tooling never deletes logs/mpegpic_*.ppm, so record what is there BEFORE the run and the run start time
ls "$OLD"/logs/mpegpic_*.ppm 2>/dev/null | wc -l        # expect 0 (0 on 2026-09-27); record the count in IMPL either way
date +%s > /tmp/p7/run_start_epoch                       # only PPMs with mtime >= this are goldens

# 1. OLD golden run (bounded; OLD/logs/* is the accepted side effect, REFERENCE_FREEZE; verify/ is NOT edited)
mkdir -p /tmp/p7 && cat > /tmp/p7/old_fmv_dump.py <<'EOF'
# Arms PS2_MPEG_DUMP_N on an OLD leg through run_leg's own env builder (monkey-patch; verify/ untouched).
import sys; sys.path.insert(0, "verify")
import profile_deterministic as pd, run_leg
_orig = pd.build_leg_env
def _patched(base_env, out_dir, tree, extra=None):
    extra = dict(extra or {}); extra["PS2_MPEG_DUMP_N"] = "1000"
    return _orig(base_env, out_dir, tree, extra)
pd.build_leg_env = _patched; run_leg.build_leg_env = _patched
sys.argv = ["run_leg.py", "--binary", "$PS2R_OLD_ROOT/build/host_app/host_app",
            "--tree", "legacy", "--anchor", "coldboot", "--frames", "1500", "--out", "/tmp/p7/old_cb"]
sys.exit(run_leg.main())
EOF
python3 /tmp/p7/old_fmv_dump.py            # run_leg: OK expected; ~50 s wall (baseline wall_ms=50176)

# 2. Prove the dump did not perturb the run: the leg equals the frozen baseline on all three lanes.
#    --cross-engine = the meta policy that tolerates the env_hash NOTE (see above); it is NOT an engine claim.
python3 verify/diff_legs.py verify/baseline/coldboot /tmp/p7/old_cb --cross-engine --lanes frames,audio,mem
#   expected, in order:  exactly ONE line  "NOTE (cross-engine): meta env_hash differs: ... vs ..."
#                        "meta: gate PASS (profile=deterministic, both finalized)"
#                        final line "diff_legs: IDENTICAL (lanes=frames,audio,mem)"
#   ANY other NOTE (build_id / gl_renderer / tree) INVALIDATES the run: it means the wrong binary, GPU or tree was used.

# 2b. Prove the env delta is EXACTLY the dump variable (and the leg length is the baseline's)
cat > /tmp/p7/check_env_delta.py <<'EOF'
# Asserts: frames == 1494 on both; sorted env_ps2 delta (ignoring PS2_VERIFY_* / PS2_PAD_SCRIPT_FILE, which run_meta.cpp:33-49
# also excludes from env_hash) is exactly {+PS2_MPEG_DUMP_N=1000}. Exit 0 = OK, 1 = mismatch (printed).
import sys
def meta(p):
    d = {}
    for ln in open(p):
        k, _, v = ln.rstrip("\n").partition("=")
        d[k] = v
    return d
def env(d):
    out = set()
    for kv in d.get("env_ps2", "").split(";"):
        if not kv: continue
        k = kv.split("=", 1)[0]
        if k.startswith("PS2_VERIFY_") or k == "PS2_PAD_SCRIPT_FILE": continue
        out.add(kv)
    return out
a, b = meta(sys.argv[1] + "/run.meta"), meta(sys.argv[2] + "/run.meta")
ok = a.get("frames") == "1494" == b.get("frames")
added, removed = sorted(env(b) - env(a)), sorted(env(a) - env(b))
ok &= (added == ["PS2_MPEG_DUMP_N=1000"] and removed == [])
print("frames baseline=%s leg=%s  +%s  -%s  -> %s" % (a.get("frames"), b.get("frames"), added, removed, "OK" if ok else "MISMATCH"))
sys.exit(0 if ok else 1)
EOF
python3 /tmp/p7/check_env_delta.py verify/baseline/coldboot /tmp/p7/old_cb
#   expected: "frames baseline=1494 leg=1494  +['PS2_MPEG_DUMP_N=1000']  -[]  -> OK"

# 3. Collect the goldens: ONLY PPMs written by THIS run (mtime >= run start); copy out of OLD/logs; never edit there
mkdir -p /tmp/p7/golden
START=$(cat /tmp/p7/run_start_epoch)
for f in "$OLD"/logs/mpegpic_*.ppm; do
  [ "$(stat -f %m "$f")" -ge "$START" ] && cp "$f" /tmp/p7/golden/
done
ls /tmp/p7/golden | wc -l                                # = served_total (definition below)
cat > /tmp/p7/make_manifest.py <<'EOF'
# make_manifest.py <golden_dir> <manifest_out> <build_id> <avcodec_version>
# served_total := the number of PPMs in <golden_dir>; ASSERTS their indices are contiguous 000..N-1 (no gaps, no stale extras).
# Header: "# source=OLD coldboot leg PS2_MPEG_DUMP_N=1000 build_id=<b>" / "# avcodec_version=<v>  served_total=<n>";
# rows:   "<index>\t<sha256 of the PPM bytes>\t<w>\t<h>"  (P6 header parsed for w/h; index from mpegpic_NNN.ppm).
import hashlib, os, re, sys
gd, out, build_id, avv = sys.argv[1:5]
files = sorted(f for f in os.listdir(gd) if re.fullmatch(r"mpegpic_\d+\.ppm", f))
idx = [int(re.search(r"(\d+)", f).group(1)) for f in files]
assert idx == list(range(len(files))), "non-contiguous indices: %r" % idx
rows = []
for i, f in zip(idx, files):
    b = open(os.path.join(gd, f), "rb").read()
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", b)
    assert m, "not a P6 PPM: " + f
    rows.append("%d\t%s\t%s\t%s" % (i, hashlib.sha256(b).hexdigest(), m.group(1).decode(), m.group(2).decode()))
with open(out, "w") as o:
    o.write("# source=OLD coldboot leg PS2_MPEG_DUMP_N=1000 build_id=%s\n" % build_id)
    o.write("# avcodec_version=%s  served_total=%d\n" % (avv, len(rows)))
    o.write("\n".join(rows) + "\n")
print("served_total=%d -> %s" % (len(rows), out))
EOF
python3 /tmp/p7/make_manifest.py /tmp/p7/golden tests/ipu/fixtures/coldboot_pictures.sha256 \
    "$(grep '^build_id=' /tmp/p7/old_cb/run.meta | cut -d= -f2-)" \
    "$(./build/tests/ipu/fmv_picture_oracle --avcodec-version)"   # NEW tool, built earlier in 7E (cmake --build first)
#   expected: "served_total=<n> -> tests/ipu/fixtures/coldboot_pictures.sha256"

# 4. NEW side: decode movie 1 then movie 2 and assert the PREFIX property
./build/tests/ipu/fmv_picture_oracle --og-root "$OLD" \
    --movie FMV/VIVENDI.PSS --movie FMV/TTIDENT.PSS --manifest tests/ipu/fixtures/coldboot_pictures.sha256
#   → "movie1 decoded=<d1> served_prefix=<s1>; movie2 decoded=<d2> served_prefix=<s2>; PREFIX OK"
ctest --preset macos-dev -R FmvPictureOracle
```

Rules of the golden:
- The golden sequence MUST be a PREFIX of movie-1's decoded pictures followed by a PREFIX of movie-2's (served ≤ decoded; the
  game may stop asking before the last picture). The oracle records `s1`, `s2` (INV A5's wall-paced log showed 136 + 281 —
  the deterministic counts are what step 1 yields; INV OPEN Q3 is closed by this run). `s1 + s2 == served_total`.
- PPM bytes are compared by sha256 of the whole file; the oracle writes P6 exactly as OLD (`P6\n%d %d\n255\n` + RGB triplets).
- Decoder-version rule: both sides link the same brew `libavcodec 62` (`/opt/homebrew/lib`, INV D3). `avcodec_version()` is
  written into the fixture header; a mismatch at test time is a META-GATE failure (`FmvPictureOracle` reports `META: avcodec
  <x> != fixture <y>` and fails), never an accepted difference (CONSTRUCTION §4.7 `:701-703`).
- Any picture mismatch = a construction bug in `PssDemux`/`FfmpegVideoDecoder` ordering or flush (the decode is the same
  library) — filed as a §7.1 row, never tolerated.
- Helpers live under `/tmp/p7/` (STAGE_RULES 2); the three `.py` helpers (`old_fmv_dump.py`, `check_env_delta.py`,
  `make_manifest.py`) are quoted in full in `PHASE7_IMPL.md` 7E.

### 4.4 `AudioPullRing` contract tests (module gate item (b); NEW mechanism, §3.1)

- Arithmetic: `kQuantumFrames == 800`, `kCapacityFrames == 6400`; `Available()` after `k` pushes = `800k` for `k ≤ 8`.
- Order: push 25 quanta of a counting pattern, pull in 512-frame chunks → the concatenated output equals the concatenated input
  byte for byte (no wrap artefact; capacity is not a multiple of 512).
- Underrun: pull 512 from an empty ring → 512×2 zero floats, `underruns == 1`, `pulled == 512`; a partial ring (300 frames)
  → 300 real + 212 zero, `underruns == 1`.
- Overrun: push 9 quanta → the FIRST quantum is gone, `overruns == 1`, `dropped == 800`; the next pull returns quantum 2's bytes.
- No drift: over `10^5` pushes interleaved with pulls at a 512/800 ratio, `pulled == pushed − dropped − Available()` holds at
  every step (checked every 1000 iterations).
- SPSC: one producer thread pushing 10^4 quanta, one consumer pulling until it has read `pushed − dropped`; the read sequence
  is a subsequence of the written sequence with drops only at quantum boundaries; no data race under `-fsanitize=thread` when
  the preset allows (recorded, not required).
- `SdlAudioSinkDummy`: with `SDL_HINT_AUDIODRIVER=dummy`, `Open` returns true, `Running()` true, `Close` idempotent; when
  `SDL_InitSubSystem(SDL_INIT_AUDIO)` fails the test prints `SKIP: SDL audio unavailable` and exits 0.

### 4.5 Input contract tests (module gate item (c))

- `InputMapper` (§4.1) — all nine OLD cases green.
- `PadlogReplay`: a synthetic spec `"10-20:CROSS,15-30:LX=200,25-25:START"` → for each frame `f ∈ [0, 40)` the test builds the
  `NormalizedInput` the mapper would need (`buttons` from the spec bits via the `LogicalButton` order; sticks via the inverse of
  `StickToByte`, `(b − 127.5)/127.5`), runs `InputMapper::Map` with a zero-delta mouse, converts to wire with `ToInputState`, feeds
  `InputLog::Feed(f, buttons_active_high, axes, out)` and finally compares the recorder's tokens to the spec's `ParseSpec`
  result token by token (the grammar equivalence of INV C4). This proves the Phase-7 host half produces exactly what Phase 4's
  `PadProtocol` will stamp, without `PadProtocol`.
- `BindingsRoundtrip`: default INI → `Bindings` → `Serialize()` → `LoadConfigText` → `==`; a line `cross = ` (empty value) rejects
  the whole file and leaves the baked default.

### 4.6 What is deliberately NOT tested here (and where it is)

The `mem`/`audio`/`frames` effect of any Phase-7 class (guest strip bytes at the game's plane address, FMV PCM inside the 162
coldboot windows — INV D6, the pad slot images, present-skip gaps) → the INTEGRATION GATE (§6.2). The `EeTimers` pump at the
GetPicture boundary (INV RISK 15), the pacer park on `TickSync`, the ring-mirror's effect on `FUN_002afc00`'s header read
(`mpeg_hle.cpp:838-871`), and the `fmv_abort_hold` workaround → integration legs.

## 5. Flags consumed (12 rows: 3 NORM, 0 SETTING, 2 DIAG, 1 TEST, 6 DELETE — `FLAG_PHASE_COUNTS.txt` P7 lines 59-62)

The heading count is the checked-in `docs/filemap/FLAG_PHASE_COUNTS.txt` P7 attribution (`P7 NORM (3) … P7 DELETE (6)`), which
Phase 7 does NOT edit (FORBIDDEN list). "OLD site" is INV's re-verified line. Applied form per STAGE_RULES: NORM → the winning arm
only; DIAG → `core/diag` category (read-only for us → the SITE is not written, the category is an §8.2 item); TEST → harness
channel; DELETE → not transplanted. Zero new `getenv` sites (`CoreSingleGetenvSite` stays 1; OLD had 12 in `mpeg_hle.cpp`, 2 in
`input_system.cpp`, 2 in `pad_input.cpp`, 1 in `input_latency.h` — INV RISK 8). Flags READ but attributed to other phases
(`PS2_SND_TICKCLOCK`, `PS2_TICK_SYNC`, `PS2_TICK_DETERMINISTIC`, `PS2_PERF`, `PS2_PAD_SCRIPT`, `PS2_STICKTRACE`, `PS2_SOUND`) are
named in §2 and not counted.

### 5.1 NORM — 3 (write the winning arm only)

| Var | OLD site | Winning arm → applied form | §2 row |
|---|---|---|---|
| `PS2_INPUT` | `input_system.cpp:214-220` | ON: backends always constructed when `InputSystem::Init` is called; the `=0` arm (no backends, neutral `Frame()`, `g_input_selftest_expect = 0xFFFFFFFF`) is not written. Headless legs get the neutral pad by NOT constructing an `InputSystem` (harness/CLI decision, never env) | 2.4 `input_system` |
| `PS2_MPEG_PACE_AUTHORED` | `mpeg_hle.cpp:458-476` (default `!VBlankFreerunOn()`, FREERUN=1 → false; but the call sites `:2195`, `:2476` are `PaceAuthoredOn() \|\| TickSync::On()` and TICK_SYNC=1) | The tick-domain pacer is the ONLY path: `MpegApi::GetPicture` always consults `FmvPacer`; the legacy present-coupled wait (`:2202-2222`, `g_frames_presented` poll + 100 ms valve) and the wall-clock `PaceDeadline` branch (`:705-731`) are not written. Closes HACK_LEDGER `:153` (no gate reads another gate) | 2.2 `fmv_pacer`, `mpeg_api` |
| `PS2_SND_FMV` | `mpeg_hle.cpp:1314-1320` (default ON) | Audio capture unconditional inside `PssDemux::OnAudioPes → FmvAudioCapture`; whether the PCM is audible is the `FmvPcmSink` implementer's (`audio.enabled`, Phase 4). The `=0` kill is not written; the `warp::Active()` skip (`:1328`) is not written either (warp is play-mode-only and the sink decides) | 2.2 `fmv_audio_capture` |

### 5.2 DIAG — 2 (`core/diag` is read-only for Phase 7: the probe SITES are not written; the categories are §8.2 items)

| Var | OLD site | Applied form | §2 row |
|---|---|---|---|
| `PS2_INPUT_LAT` | `input/input_latency.h:16-22`; emit sites `input_system.cpp:349-380` (`[INLAT-T1]`, ring tags `INLAT_*`) | Not written in release. `ring_tags.h:39-49` already carries the `CH_INPUT` tags (INV D1) — a future `core/diag/input_latency` category re-uses them; recorded as integration item §8.2 (12) | 2.4 `input_system` |
| `PS2_MPEG_DUMP_N` | `mpeg_hle.cpp:2282-2299` | Not written in release. Its OUTPUT FORMAT is the golden of §4.3 (`fmv_picture_oracle` reproduces the P6 layout as a TOOL under `tests/`, not as a runtime probe); a `fmv_dump` diag category is §8.2 (12) | 2.5 `fmv_picture_oracle` |

### 5.3 TEST — 1 (harness channel only)

| Var | OLD site | Applied form | §2 row |
|---|---|---|---|
| `PS2_INPUT_SELFTEST` | `input_system.cpp:126-155`, `:287-303` (default `"300-500:Y"`; `InjectKeyForTest` + `g_input_selftest_expect`, verified by `pad_input.cpp:976-1004`) | `InputLog` replay via `--inputs` (Phase 1) is the channel; the selftest grammar, the `"300-500:Y"` default (HACK_LEDGER `:54`: "moves to the title's verify scripts") and the expect-word publish are not written. `KeyboardMouseBackend::InjectKeyForTest` survives ONLY as the unit-test seam (no env, no frame window) | 2.4 `input_system`, `kbm_backend` |

### 5.4 DELETE — 4 of the ledger's 6 (not transplanted: var, gate and subject arm)

| Var | OLD site | What is not written | §2 row |
|---|---|---|---|
| `PS2_MPEG_PACE_OFF` | `mpeg_hle.cpp:566-571` | the early `return` (no pacing) — an adjudication switch | 2.2 `fmv_pacer` |
| `PS2_MPEG_PDIFF` | `mpeg_hle.cpp:2086-2113` (+ `:2127-2146`) | player-struct diff at `*0x30a700` every 8 serves (a game address; fails the purity lint outright) | 2.2 `mpeg_api` |
| `PS2_MPEG_WM` | `mpeg_hle.cpp:2226-2278` | watermark modes 1..6 painted into `f.rgba` | 2.2 `mpeg_api` |
| `PS2_MPEG_SEQCENSUS` | `mpeg_hle.cpp:1431-1461` | the `[R196-SEQ]` stderr census (offline `tests/ipu/pss_census.py` covers the question) | 2.2 `pss_demux` |

### 5.5 The two remaining "DELETE" rows are written as constants — findings P7-LEDGER-BURST / P7-LEDGER-CAP (D3)

| Var | Ledger | OLD site | Fact (INV A3) | What Phase 7 writes | Ledger correction (integration doc item, §8.2 (9)) |
|---|---|---|---|---|---|
| `PS2_FMV_OPEN_BURST` | `FLAG_LEDGER.md:345` DELETE (delete-both) | `mpeg_hle.cpp:531-547` (default **1**); use `:648` (tick call; `:651-653` target), `:706` (wall) | Unset → ON → `NextBurst` is the runtime path of EVERY baseline leg; the debt-clamp `Next` (`mpeg_hle.h:68-88` wall, `:158-178` tick) is the LOSING arm | `FmvPacer::Next` = `TickPace::NextBurst` (`mpeg_hle.h:188-`) semantics as the SOLE core; no clamp arm, no switch. `test_fmv_pacer` pins "no floor" | row `:345` → NORM (winning arm = burst). HACK_LEDGER `:310` ("KEEP debt clamp (~2 frames)") is SUPERSEDED — the clamp was never live under the defaults; `:134` ("KEEP both arithmetic cores") is narrowed to the tick core (the wall core `PaceDeadline`, `mpeg_hle.h:56-105`, has no domain in NEW) |
| `PS2_FMV_DECODE_CAP` | `FLAG_LEDGER.md:343` DELETE (delete-both) | `mpeg_hle.cpp:1228-1245` (default **64**); use `:1584-1605`, `:1252-1300` | Unset → 64 → bounded decode-ahead with `pending_es` deferral is the baseline path; `cap == 0` eager decode is the LOSING arm (OLD measured +6 GB transient, `:1207-1215`) | `IpuSettings::decode_ahead_pictures = 64` (`kDecodeAheadPictures`); deferral + `DrainPendingEs` written; the `cap == 0` branch not written | row `:343` → NORM (winning arm = 64). A `Config` SETTING is NOT introduced (no `Config` edit in scope; the constant suffices for byte identity) |

Why this is not an accepted difference: deleting the ON arm would change `next_tick` after every late EE arrival (cadence →
`mem`/`frames` divergence, INV RISK 2) and unbound the decode queue; writing the ON arm preserves OLD's observable behaviour
exactly. The ledger text, not the behaviour, is what is corrected. `FLAG_PHASE_COUNTS.txt` P7 keeps `DELETE (6)` until the
ledger rows move (then `NORM 5 / DELETE 4` — regenerated by whoever edits the ledger; `FlagPhaseCountsReproducible` guards it).

### 5.6 Outside the count (named in §2; none changes the 12)

`PS2_PERF` sites (`mpeg_hle.cpp:464-473`, `:536-545`, `:600-605`, `:1234-1243`, `:1397-1403`, `PaceCensusBook :490-519`) — stderr
self-reports under the profile's `PS2_PERF=1`, not game-visible → not written (the `PS2R_PERF_SCOPE` macro has no `kMpeg`/`kIpu`
subsystem yet, `perf_profiler.h:16-48` grep = none → §8.2 (12)). `PS2_TICK_SYNC`/`PS2_TICK_DETERMINISTIC` (`:635`, `:663`) — the
looping demand-signalling wait (`:663-678`) is the INTEGRATION park shape; the 250 ms single wait (`:680-687`) is not written.
`PS2_SND_TICKCLOCK` (`mixer.cpp:30-35`) — NORM in Phase 4; here it is the caller's rule for never calling `SdlAudioSink::Open`.
`PS2_STICKTRACE` (`input_backend_gamepad.cpp:365-415`) — Phase 4 DIAG; sites not written. `PS2_PAD_SCRIPT` — Phase 4 TEST; the
grammar lives in Phase 1's `InputLog`. Ungated OLD side effects `logs/mpegback_006.ppm` (`:2350-2364`) and the `[R196-IMG]/
[R196-SERVE]` fingerprints (`:2311-2346`) — not written (INV RISK 5).

## 6. Proof gate — MODULE GATE (ours) and INTEGRATION GATE (not ours)

### 6.1 MODULE GATE (checkable items; `PHASE7_IMPL.md` quotes the evidence line for each)

| # | Item | Check |
|---|---|---|
| (a) | **FMV per-picture golden** | `tests/ipu/fixtures/coldboot_pictures.sha256` exists with `served_total`, `avcodec_version`, `build_id` header and one row per OLD-served picture (contiguous indices `000..N-1`, `served_total == N`, step 0's pre-count recorded); the OLD golden leg equals `verify/baseline/coldboot` on `frames,audio,mem` under the `--cross-engine` meta policy (§4.3 step 2 — quoted: exactly ONE `NOTE (cross-engine): meta env_hash differs`, `meta: gate PASS (profile=deterministic, both finalized)`, final line `diff_legs: IDENTICAL (lanes=frames,audio,mem)`; any `build_id`/`gl_renderer`/`tree` NOTE invalidates the run) AND step 2b quoted: `frames baseline=1494 leg=1494  +['PS2_MPEG_DUMP_N=1000']  -[]  -> OK`; `ctest -R FmvPictureOracle` PASS with the line `movie1 decoded=<d1> served_prefix=<s1>; movie2 decoded=<d2> served_prefix=<s2>; PREFIX OK` and `s1 + s2 == served_total`; `PssDemux` OLD-gated census agrees with `tests/ipu/pss_census.py` on both movies (7 counters each); `FrameWriter`, `FmvPaceDeadline`, `FmvAudioCapture`, `IpuDevice`, `MpegDecode` green |
| (b) | **Audio sink module** | `AudioPullRing` green on all six contracts of §4.4 (arithmetic, order, underrun, overrun, no-drift 10^5, SPSC); `SdlAudioSinkDummy` PASS (or `SKIP: SDL audio unavailable` — recorded, and the reason quoted); `SdlAudioSink` has no clock read, no `MixInto`-equivalent call, no reference into `engine/sound` (grep quoted); `engine/platform/sdl/README.md` carries the NEW-mechanism row |
| (c) | **Input module** | `InputMapper` green (9 OLD cases); `PadlogReplay` green (recorder tokens == spec tokens); `BindingsRoundtrip` green incl. whole-file rejection; the generated `input_default_bindings.h` regenerates when `default_bindings.ini` changes (touch → rebuild → new hash quoted); `default_bindings.ini` passes `LintEnginePurity` (header reworded); `GamepadBackend` has no `static` mutable member (grep quoted) |
| (d) | **Tree health at every stop** | `cmake --build --preset macos-dev -j4` 0 warnings (`-Wall -Wextra -Werror` on every new target); `ctest --preset macos-dev` 100 % (88 + 13 new); `LintOneClass` 0, `LintEnginePurity` 0 (incl. `.ini`), `CoreSingleGetenvSite` = 1 (no Phase-7 `getenv`), `SettingsTableReproducible` / `FlagPhaseCountsReproducible` unchanged (nothing of ours edits their inputs) |
| (e) | **Scope discipline** | `git status` shows changes ONLY under the OWNED paths of §1.4 plus one `# Phase 7` append block in each of the 3 SHARED files (diff quoted: additions only, at end of file); `engine/platform/platform.h` untouched; no file under `engine/core`, `engine/ee`, `engine/iop`, `engine/sound`, `engine/pad`, `engine/gs`, `app/`, `games/`, `verify/`, `docs/filemap/`, `README.md` modified |
| (f) | **Interfaces, not dependencies** | `grep -rn '#include "engine/\(ee\|sound\|pad\|cdvd\|dmac\|platform\)' engine/ipu engine/platform/sdl` → only `engine/ee/mem/mmio_handler.h` (from `ipu_device.h`) and `engine/platform/platform.h` (from `pad_sample.h`); every other cross-module need goes through one of the five D1 interface headers declared in `engine/ipu/hle/` (`guest_memory.h`, `fmv_pcm_sink.h`, `fmv_sector_source.h`, `dmac_channel_face.h`, `video_decoder.h`) or the ring (P7-NO-PHASE4-DEPS, §7.1) |
| (g) | **Records** | `PHASE7_IMPL.md` has one section per stage (files, OLD sources transplanted with `file:line`, flags with outcome, tests + result lines, handoff) + the INTEGRATION CHECKLIST (§8.2) + the numeric summary (files, lines, tests, served counts `s1/s2`, `served_total`, `avcodec_version`) + the three `/tmp/p7/*.py` helpers quoted in full; §7.1 rows filled |
| DoD | all of (a)–(g) | the three modules exist, are unit- and oracle-proven against OLD-derived goldens (the OLD golden leg proven baseline-identical on all three lanes under the `--cross-engine` meta policy with the env delta pinned to `PS2_MPEG_DUMP_N=1000` alone), and are wired to nothing; the plan's findings are recorded; **"Phase 7 module gate: PASSED"** is written only when each item is quoted, else the failing command is the handoff |

### 6.2 INTEGRATION GATE — explicitly NOT this phase's

CONSTRUCTION §4.7's proof gate (`:686-696`) — **"All lanes, all legs: `--lanes frames,audio,mem` IDENTICAL on ALL 12 legs END TO
END — the coldboot leg's intro FMVs close the last sink: 1,494 frame lines, 162 audio windows, 17 memory checkpoints;
`MmioRouter::UnhandledRanges()` is empty"**, the anchor `input.padlog` replays through `PadProtocol`, and the DoD "realtime play has
audible audio (device sink), FMV, live pad input and memory-card save/load working" — is the INTEGRATION GATE. It requires the
Phase-4 `Mixer`/`FmvStream`/`PadProtocol`/`IsoFs`/stream cursor, the Phase-5/6 sinks, and every §8 row. None of it is claimed,
attempted or approximated here; no lane is claimed on any leg (§1.3). The accepted-difference protocol of §4.7 (`:701-703`,
decoder version as a meta gate) is honoured by the fixture header rule of §4.3 so the integration gate inherits it unchanged.

## 7. Stages (≤ 45 min wall each; tree green + both lints + `CoreSingleGetenvSite` = 1 + 0 warnings at EVERY stop; one `PHASE7_IMPL.md` section per stage)

Order = dependency order: the smallest NEW mechanism with its harness first (7A), the SDL-only module (7B), the pure decoder +
register model (7C), the HLE pipeline over FFmpeg (7D), then the OLD-derived golden that proves 7C/7D end to end (7E). Two stages
are pre-registered to split (7B′, 7D′): the STOP rule of STAGE_RULES 3 applies at ~35 min, the tree stays green, the named
remainder moves to the primed stage. Helpers under `/tmp/p7/`; every OLD binary run through `verify/run_bounded.sh`; no commits.
The `# Phase 7` block of each SHARED file is created by the first stage that needs it (7A for `engine/platform/CMakeLists.txt`
and `tests/CMakeLists.txt`, 7C for the root) and EXTENDED IN PLACE by later stages — still one block per file, appended at the end,
never interleaved with earlier phases' lines.

| Stage | Scope | OLD sources read | Green criterion (own) | Pre-registered split |
|---|---|---|---|---|
| **7A** audio sink | `engine/platform/sdl/{audio_pull_ring, audio_sink}.{h,cpp}` + `README.md`; `engine/platform/CMakeLists.txt` `# Phase 7` block: `find_package(SDL2 CONFIG REQUIRED)` + `add_library(ps2r_platform_sdl STATIC …)` linking `ps2r_platform SDL2::SDL2`, `-Wall -Wextra -Werror`; `tests/platform_sdl/{CMakeLists.txt, test_audio_pull_ring.cpp, test_audio_sink.cpp}`; `tests/CMakeLists.txt` `# Phase 7` block `add_subdirectory(platform_sdl)`; `PHASE7_IMPL.md` created with §7A + P7-TESTS-PLATFORM-SDL-DIR recorded. Flags: `PS2_SND_TICKCLOCK` (read; caller's rule) | `sound/mixer.{h,cpp}` (`mixer.cpp:15-82`, `mixer.h:1-29`), `verify/verify_boundary.cpp:22`, `:35`, `:60-73`, `sound_core.h:114-118` (the `MixInto` contract the ring replaces) | `AudioPullRing` + `SdlAudioSinkDummy` green; gate items (b) and (d); `git status` = owned paths + 2 append blocks | none (small stage) |
| **7B** input | `engine/platform/sdl/input/{input_backend.h, pad_sample, input_mapper, kbm_backend, gamepad_convert, gamepad_backend, input_system, input_settings.h, default_bindings.ini, README.md}`; configure-time generator (`file(READ default_bindings.ini)` → `${PS2R_GENERATED_DIR}/input_default_bindings.h`, `kDefaultBindingsIni` raw string, with `CMAKE_CONFIGURE_DEPENDS` on the INI) appended to the `ps2r_platform_sdl` block; `tests/input/{CMakeLists.txt, test_input_mapper.cpp, test_padlog_replay.cpp, test_bindings_roundtrip.cpp}` (+ `add_subdirectory(input)` in the `tests/CMakeLists.txt` block). Flags: `PS2_INPUT` NORM, `PS2_INPUT_SELFTEST` TEST, `PS2_INPUT_LAT` DIAG (sites not written), `PS2_STICKTRACE` (not written) | `input/input_backend.h`, `input_mapper.{h,cpp}`, `input_backend_kbm.{h,cpp}`, `input_backend_gamepad.{h,cpp}`, `input_system.{h,cpp}`, `default_bindings.ini`, `input_latency.h` (read to confirm nothing to port), `pad_input.cpp:461-570`, `:779-817`, `host_app/input/test_input_mapper.cpp` (all 645), `host_app/CMakeLists.txt:104-113`, `tests/CMakeLists.txt:2150-2170`; NEW `core/triage/input_log.{h,cpp}`, `platform.h:21` | `InputMapper` (9 cases), `PadlogReplay`, `BindingsRoundtrip` green (its `SerializeBindings` is test-local, §2.5); `LintEnginePurity` passes over the `.ini`; `LintOneClass` passes over `tests/input`; gate items (c), (d) | **7B′**: at ~35 min STOP with `input_backend.h`, `pad_sample`, `input_mapper`, `gamepad_convert`, `input_settings.h`, the INI + generated header and `InputMapper` + `BindingsRoundtrip` green; hand `kbm_backend`, `gamepad_backend`, `input_system`, `PadlogReplay` and the README to **7B′** |
| **7C** ipu device + decoder | `engine/ipu/{ipu_device, mpeg_decode}.{h,cpp}`, `ipu_settings.h`, `engine/ipu/README.md` rows; root `CMakeLists.txt` `# Phase 7` block (`add_subdirectory(engine/ipu)`); `engine/ipu/CMakeLists.txt` (`ps2r_ipu` STATIC → `ps2r_core`; include `engine/ee/mem/mmio_handler.h` only); `tests/ipu/{CMakeLists.txt, test_ipu_device.cpp, test_mpeg_decode.cpp}` (+ `add_subdirectory(ipu)` in the tests block). Flags: `PS2_FMV_DECODE_CAP` constant (P7-LEDGER-CAP recorded) | `ipu.{h,cpp}` (all), `memory.cpp:618-627`, `:1059-1068`, `libipu.cpp:6-15`, `:67-105` (what the game touches), `dmac.cpp:1500-1550` (read for the FIFO face only); NEW `mmio_handler.h`, `mmio_router.h:16-57` | `IpuDevice`, `MpegDecode` green; `ps2r_ipu` builds with `-Werror`; gate item (d); the `// diverges:` stubs enumerated in the README | none |
| **7D** ipu/hle | `engine/ipu/hle/{guest_memory.h, fmv_pcm_sink.h, fmv_sector_source.h, dmac_channel_face.h, fmv_context_layout.h, pss_demux, video_decoder.h, ffmpeg_video_decoder, frame_writer, fmv_pacer, fmv_audio_capture, mpeg_context, mpeg_api}` (`libipu_api` is 7E) + `README.md` rows + the §8.1 binding-table copy marked NOT installed; `cmake/FindFFmpeg.cmake` (`find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h HINTS /opt/homebrew/include)`, `find_library` × `avcodec avutil swscale` HINTS `/opt/homebrew/lib`, IMPORTED targets `FFmpeg::avcodec/avutil/swscale`, `message(FATAL_ERROR)` when absent) — pulled in by `engine/ipu/CMakeLists.txt` with `include(${CMAKE_SOURCE_DIR}/cmake/FindFFmpeg.cmake)` (the root `# Phase 7` block stays `add_subdirectory(engine/ipu)` only; no `CMAKE_MODULE_PATH` edit); `ps2r_ipu` gains the hle sources + `FFmpeg::*`; `tests/ipu/{test_pss_demux.cpp, pss_census.py, test_frame_writer.cpp, test_fmv_pacer.cpp, test_fmv_audio_capture.cpp}` (+ `MpegApi` smoke inside `test_pss_demux`: a fake `GuestMemory` over a 1 MiB vector + a fake `FmvSectorSource` over a `.PSS` file + a counting `FmvPcmSink`, all test-local, OLD-gated). Flags: `PS2_MPEG_PACE_AUTHORED` NORM, `PS2_SND_FMV` NORM, `PS2_FMV_OPEN_BURST` constant (P7-LEDGER-BURST recorded), `PS2_MPEG_PACE_OFF`/`PDIFF`/`WM`/`SEQCENSUS` not written, `PS2_MPEG_DUMP_N` site not written | `mpeg_hle.h` (all 318; `PaceDeadline :56-105`, `TickPace :146-`), `mpeg_hle.cpp:1-28`, `:82-135`, `:125-380`, `:387-437`, `:458-547`, `:560-731`, `:747-835`, `:838-979`, `:1013-1035`, `:1131-1300`, `:1302-1428`, `:1430-1626`, `:1664-1766`, `:1773-1886`, `:1890-2558`; `libmpeg.cpp:70-95`, `:189-249`, `:403-424`, `:594-628`, `:663-782` (for the API shape the bindings will need); `libipu.cpp:67-264`; `tests/test_fmv_pace.cpp`; `tools/pss_census.py`; `sound_core.h:155-177`; `cdvd_stream.h:24-47`; `host_app/CMakeLists.txt:16-56` (FFmpeg fallback pattern) | `PssDemux` (synthetic + OLD-gated census 7/7 counters on both movies), `FrameWriter`, `FmvPaceDeadline`, `FmvAudioCapture` green; `ps2r_ipu` links against brew FFmpeg 8.1.1 / libavcodec 62 (version printed by the `MpegApi` smoke); `grep getenv engine/ipu` = 0; gate items (d), (f) | **7D′**: at ~35 min STOP with the five interface headers, `fmv_context_layout.h`, `pss_demux`, `frame_writer`, `fmv_pacer`, `fmv_audio_capture`, `FindFFmpeg.cmake` and the tests `PssDemux` (synthetic), `FrameWriter`, `FmvPaceDeadline`, `FmvAudioCapture` green; hand `ffmpeg_video_decoder`, `mpeg_context`, `mpeg_api`, the OLD-gated census half and the `MpegApi` smoke to **7D′** |
| **7E** per-picture oracle + checklist | `/tmp/p7/old_fmv_dump.py` + `/tmp/p7/check_env_delta.py` + `/tmp/p7/make_manifest.py` (helpers, quoted in IMPL); the OLD golden leg (§4.3 steps 0–3, bounded, ≈ 60 s): step 0 pre-count of `OLD/logs/mpegpic_*.ppm` (expect 0) + run-start epoch, step 2 `diff_legs --cross-engine` proof, step 2b env-delta proof, step 3 mtime-filtered copy; `tests/ipu/fixtures/coldboot_pictures.sha256`; `tests/ipu/fmv_picture_oracle.cpp` (tool: `--og-root`, `--movie` ×N, `--manifest`, `--avcodec-version`, `--dump-dir`) + ctest `FmvPictureOracle` (OLD-gated, TIMEOUT 600); `engine/ipu/hle/libipu_api.h/.cpp` + `tests/ipu/test_libipu_api.cpp` (`LibipuApi` over a fake `IpuDevice` register file and a recording `DmacChannelFace`: Init = RST then BCLR; Sync returns 0 once BUSY clears; StopDma/RestartDma round-trip the 9-word snapshot); `PHASE7_IMPL.md` INTEGRATION CHECKLIST (§8.2 verbatim + per-item status) + numeric summary + §7.1 rows closed + the module-gate table with evidence lines | `mpeg_hle.cpp:2282-2299` (PPM layout), `:2223` (pop), `:2313` (`WriteFrameToGuest`); `run_meta.cpp:33-49` (what `env_hash` folds); `verify/run_leg.py:101`, `:111-140`, `profile_deterministic.py:93-104` (for the monkey-patch); `verify/diff_legs.py:92-112` (meta policy); `OLD/.gitignore:8`; `docs/REFERENCE_FREEZE.md` "Accepted side effects" | step 2 quoted with exactly ONE `env_hash` NOTE, `meta: gate PASS`, `diff_legs: IDENTICAL (lanes=frames,audio,mem)`; step 2b `-> OK`; `FmvPictureOracle` PASS with `PREFIX OK`, `s1 + s2 == served_total`, `avcodec_version` header match; gate items (a), (e), (g); **"Phase 7 module gate: PASSED"** or the failing command as the handoff | none; if the OLD run cannot complete bounded, or step 2 shows any NOTE other than `env_hash`, or step 2b prints `MISMATCH`, the fixture is not written and (a) is the handoff to a 7E′ with the blocker quoted |

### 7.1 Findings ledger (recorded, never silently tolerated; ids `P7-<NAME>`; plan-time rows filled now, stage rows during the stages)

| Id | Stage | Finding | Disposition |
|---|---|---|---|
| P7-LEDGER-BURST | plan | `PS2_FMV_OPEN_BURST` is ledgered DELETE (`FLAG_LEDGER.md:345`, delete-both) but defaults ON (`mpeg_hle.cpp:531-547`) and `NextBurst` is the live path of every baseline leg (`:648` tick call, `:651-653` target, `:706` wall, INV A3). Deleting the ON arm would alter `next_tick` after every late EE arrival (INV RISK 2) | `FmvPacer::Next` = `TickPace::NextBurst` (`mpeg_hle.h:188-`) semantics as the SOLE core; the clamp arm (`mpeg_hle.h:158-178`) not written; `test_fmv_pacer` pins no-floor. Ledger row `:345` → NORM is an integration DOC item (§8.2 (9)); HACK_LEDGER `:310` superseded, `:134` narrowed. Not an accepted difference (behaviour preserved) |
| P7-LEDGER-CAP | plan | `PS2_FMV_DECODE_CAP` is ledgered DELETE (`FLAG_LEDGER.md:343`) but defaults 64 (`mpeg_hle.cpp:1228-1245`); the bounded decode-ahead + `pending_es` deferral (`:1584-1605`, `:1252-1300`) is the baseline path; `cap == 0` (eager, +6 GB transient `:1207-1215`) is the losing arm | `IpuSettings::decode_ahead_pictures = 64` (`kDecodeAheadPictures`), deferral written, eager arm not written; row `:343` → NORM is §8.2 (9). No `Config` SETTING introduced |
| P7-ABORT-HOLD | plan | OLD `main.cpp:12753-12796` wraps `recompiled_functions[0x2af0d8]` (the scene updater) and clears `gctx+0x15c` while `ShouldHoldAbort(gctx)` (`:12781-12786`, `[FMV-ABORT-HOLD]`); rationale `:12770-12779` (host front-loads the PSS so the refill EOF is premature). Game-visible on the coldboot leg; NOT in HOOK_RECLASSIFICATION's 747 rows, HACK_LEDGER or the FILEMAP shards (INV OPEN Q7) — an UNLEDGERED game-address workaround | `MpegApi::ShouldHoldAbort(ctx)` (engine mechanism, `mpeg_hle.cpp:2537-2547`) IS written. The hook itself is title-side: §8.2 (10) requires a `games/<t>/hle/workarounds/fmv_abort_hold.{h,cpp}` row (class: emulation-bug workaround; root fix = refill/EOF visibility follows served frames in `cdvd`/`StreamReader`; harness-gated expiry) + a HOOK_RECLASSIFICATION row before any coldboot lane closes. Out of our tree; recorded here and in the checklist |
| P7-NO-PHASE4-DEPS | plan | The branch point has no `Mixer`, `FmvStream`, `PadProtocol`, `IsoFs`, `PumpSectors`, `EeTimers` face usable from `engine/ipu` (INV D2, RISK 1); PHASE4_PLAN promised them at 4C–4E′ | Phase 7 codes against the five interface headers it declares in `engine/ipu/hle/` (`FmvPcmSink`, `FmvSectorSource`, `GuestMemory`, `DmacChannelFace`, `VideoDecoder`) + `PcmSource`=`AudioPullRing` + `PadSample`/`InputState`; every adapter is §8.2 with its Phase-4/5 implementer named; gate item (f) greps the include set. No lane is claimed (§1.3) |
| P7-TESTS-PLATFORM-SDL-DIR | plan | `tests/` mirrors module paths (`tests/{core,gs,ee,iop}` ↔ `engine/{core,gs,ee,iop}`); the audio sink lives under `engine/platform/sdl/`, and CONSTRUCTION §4.7 names only `tests/{ipu,input}`. A `tests/platform/` dir would collide with the Phase-1 platform stubs' future tests | NEW dir `tests/platform_sdl/` (module path with the platform folder flattened); recorded as the plan deviation; `tests/input/` is kept as §4.7 names it even though the module is `engine/platform/sdl/input/` |
| P7-NO-ANCHOR-PADLOG | plan | CONSTRUCTION §4.7 says "the recorded `input.padlog` of every anchor leg replays…"; `ls verify/baseline/*/` shows only `audio.ahash frames.fhash mem.mhash run.meta` — no baseline carries an `input.padlog` | `PadlogReplay` uses a synthetic spec only; the OLD-anchor half of D4's test is not writable here and is listed in §8.2 (13) as an integration/harness item (record the padlogs when the anchors are re-run through `PadProtocol`) |
| P7-MPEG-IPU-SPLIT | plan | CONSTRUCTION §4.7 and the READMEs name a class `MpegIpu`; D4's table has none | Its duties are `MpegContext` (state) + `MpegApi` (entry points); the READMEs' "Belongs here" rows are rewritten in 7D (owned files) |
| P7-IPU-FIFO-PORT | plan | OLD maps only `0x10002000/10/20/30`; the IPU FIFO port `0x10007010..1c` is not mapped (`memory.cpp` grep, INV A6/OPEN Q8) although `sceIpuInit` pushes 10×16 bytes there per the SDK | Kept UNMAPPED (byte identity of `unhandled_mmio_pages` accounting with OLD); `IpuDevice` exposes `FeedInputFifo` for a later FIFO-port handler; §8.2 (11) records the decision for the integration owner |
| (stage rows) | 7A–7E | one row per finding (a split taken, a lint hazard, a test that needed an OLD re-read, a served-count surprise) | as Phase 4's §7.1 |

## 8. Binding table (D7 — documented, NOT coded) and the INTEGRATION CHECKLIST

### 8.1 SDK binding rows (library | function | handler | notes) — copied into `engine/ipu/hle/README.md` marked "integration — NOT installed by Phase 7"

Rows are keyed by (library, function) — never an address (`sdk_bindings.tsv:2-4`); the title manifest already lists every symbol
(`games/<title>/hle/sdk_bindings.tsv`, INV D1). Unimplemented rows stay TRAP rows (~84 libmpeg internals:
`_getpic, _updateRefImage, _decodeOrSkipFrame, … _sceMpegNextHeader, _rix_*, _ri0_*, receiveDataFromIPU, _sequenceHeader`;
OLD `sce_hle_registry.cpp:485-499`) — OLD relies on the HLE'd entry points never reaching libmpeg's internal IPU decoder.

| Library | Function | Handler (Phase-7 class) | Notes / OLD (`libmpeg.cpp` / `libipu.cpp` line) |
|---|---|---|---|
| libmpeg.a | `sceMpegInit` | `MpegApi::Init` | then DMAC ch3/ch4 `CHCR.STR`/`QWC` clear (ee/dmac integration), then `sceIpuInit`; `v0 = 1` (`:70-95`) |
| libmpeg.a | `sceMpegCreate` | recompiled body (`FUN_002b70a8` via dispatch) | no HLE (`:130-133`) |
| libmpeg.a | `_sceMpegAlalcInit` / `_sceMpegAlalcAlloc` | `MpegApi::AlalcInit` / `AlalcAlloc` | 4 word stores / bump allocator (`:189-206`, `:209-249`) |
| libmpeg.a | `_clearOnce` / `_clearEach` | recompiled (`FUN_002b6fd8` / `FUN_002b72e0`) | `_clearEach` writes `IPU_CTRL = 0x40000000` → `IpuDevice::WriteCtrl` (`:294-297`, `:373-376`, `:358`) |
| libmpeg.a | `sceMpegReset` | `MpegApi::Reset` then recompiled `FUN_002b7cb0` | (`:329-334`) |
| libmpeg.a | `sceMpegClearRefBuff` | `MpegApi::ClearRefBuff` | six slot clears at `inner + {0x1c8,0x1d8,0x1e8,0x1cc,0x1dc,0x1ec} + 0x28` (SDK struct layout; offsets via `FmvContextLayout`) (`:403-424`) |
| libmpeg.a | `_sceMpegError1` | log row | (`:458-480`) |
| libmpeg.a | `sceMpegAddStrCallback` / `sceMpegAddCallback` | recompiled (`FUN_002bbeb0` / `FUN_002b7d48`) | (`:517-520`, `:559-562`) |
| libmpeg.a | `sceMpegDemuxPss` | `MpegApi::DemuxPss(a0, a1, a2)` | (`:594-602`) |
| libmpeg.a | `sceMpegDemuxPssRing` | `MpegApi::DemuxPssRing(a0, a1, a2, a3, *(sp+0x10))` | (`:616-628`) |
| libmpeg.a | `sceMpegGetPicture` | [`EeTimers` pump at the boundary] → `MpegApi::GetPicture(a0, a1)` → park `wait_ticks` on `TickSync` | pump is game-visible (TIM0, INV RISK 15); the park = `NotePending(Demand)` loop, INV A1.d `:663-678` (`:744-782`) |
| libmpeg.a | `sceMpegGetPictureRAW8` | [pump] → plane select by layout (`w=*(a0+0x58)`, `h=*(a0+0x5c)`, `idx=(*(a0+0x54)&1)?(*(a0+0x158)!=0):0`, `plane=*(a0+0x94+idx*4)` — title offsets) → `MpegApi::GetPictureRaw8` | (`:663-737`) |
| libmpeg.a | `sceMpegIsEnd`, `sceMpegDelete` | **NOT rows** | handlers existed in OLD but were never bound (no row in `sce_hle_bindings.inc`; INV D1 finding) |
| libipu.a | `sceIpuInit` | `LibipuApi::Init` → `IpuDevice` RST + BCLR | (`libipu.cpp:67-77`) |
| libipu.a | `sceIpuSync` | `LibipuApi::Sync` → CTRL.BUSY poll → 0 | (`:88-105`) |
| libipu.a | `sceIpuStopDMA` / `sceIpuRestartDMA` | `LibipuApi::StopDma` / `RestartDma` over a `DmacChannelFace` | ch3/ch4 CHCR/QWC snapshot (9 words) — dmac = integration (`:139-172`, `:212-264`) |
| libipu.a | `setD3_CHCR` | trap row | (registry `:421-434`) |

### 8.2 INTEGRATION CHECKLIST (each item names its owner; copied verbatim into `PHASE7_IMPL.md` with a status column)

1. **Mixer → ring** (Phase 4 `Mixer`, `Step::Verify` body): after `AudioHasher::Push(mixbuf, 800)`, call `AudioPullRing::Push(mixbuf,
   800)` on the SAME buffer; `SdlAudioSink::Open(spec, ring)` only when `time.tick_sync == false` (§3.1).
2. **FmvPcmSink** implemented by Phase 4 `FmvStream` (`Push` → FIFO producer drop-newest, `GateOpen`, `Flush`; consumed in `MixInto`
   after voices at core-0 BVOLL/BVOLR gain, `sound_core.cpp:780-812`). Lane-critical timing: gate on the first SERVED picture,
   flush on Init/Destroy/Reset (INV RISK 12).
3. **FmvSectorSource** implemented over the Phase-4 stream reader with the SHARED cursor semantics of `cdvd_stream::PumpSectors`
   (advances the game's `sceCdSt*` cursor; INV OPEN Q5) — or a §7.1-style finding if the NEW reader has no shared cursor.
4. **Pad stamp**: `Platform.poll_input` filled from `InputSystem::Frame()` → `ToInputState` on the main thread; Phase 4
   `PadProtocol::Stamp` at `Step::MaintDuties` (slot 4, after the RPC end-callback duty — OLD `gs_hle.cpp:373-381`).
5. **GuestMemory** adapter over `MainMemory`/`AddressMap` (`Translate` = `MainMemory::Pointer(Phys(addr), len)`); `FmvContextLayout`
   filled from title data (`games/<t>/hle/` — the offsets of §1.5); `MpegApi` constructed with both.
6. **IpuDevice registration**: `MmioRouter::Register(0x10002000, 0x40, ipu, "IPU")` in the composition root; the DMAC ch3/ch4 face
   (`FeedInputFifo`/`DrainOutputFifo`, `CompleteChannel` synchronously as `dmac.cpp:1512-1550`) from Phase 5's dmac.
7. **Binding rows** of §8.1 as `engine/ipu/hle/{libmpeg,libipu}_bindings.cpp` (`RegisterLibmpegBindings(SdkBindingManifest&)`
   form, INV D1) — incl. the `EeTimers` pump before every `GetPicture*` and the `TickSync` demand-park for `wait_ticks`.
8. **FmvActive signal** from `FmvPacer` → the frame boundary (present policy / `FrameAnnotator` input; HACK_LEDGER `:307`) replacing
   `DecodeActiveRecently` sniffs; savestate refusal during FMV consumes the same signal.
9. **Ledger docs**: `FLAG_LEDGER.md:343/:345` → NORM (winning arm = ON) + `FLAG_PHASE_COUNTS.txt` regen (P7 → NORM 5 / DELETE 4);
   HACK_LEDGER `:310` superseded, `:134` narrowed to the tick core; INDEX.md `mpeg_ipu` → `mpeg_context + mpeg_api`.
10. **`fmv_abort_hold`** (P7-ABORT-HOLD): `games/<t>/hle/workarounds/fmv_abort_hold.{h,cpp}` consuming `MpegApi::ShouldHoldAbort`, a
    HOOK_RECLASSIFICATION row (class: emulation-bug workaround), retirement gate = coldboot leg IDENTICAL with the hook off after
    the `cdvd` completion-visibility root fix.
11. **IPU FIFO port** decision recorded (P7-IPU-FIFO-PORT): unmapped as OLD; revisit only if `unhandled_mmio_pages` shows `0x10007000`
    on a gate leg.
12. **Diag categories** (`core/diag`, diag builds only): `input_latency` (re-using `ring_tags.h:39-49`), `fmv_dump` (the `PS2_MPEG_DUMP_N`
    successor, writing where `Config::paths.log_dir` says), `PS2R_PERF_SCOPE` subsystems `kMpeg`/`kIpu`.
13. **Anchor padlogs** (P7-NO-ANCHOR-PADLOG): record `input.padlog` per anchor once `PadProtocol` + `InputLog` record run; then the
    OLD-anchor half of `PadlogReplay` becomes writable.
14. **Config mapping**: `InputSettings` ← `Config::input.{device, kbm_pad, bindings_file}` (+ a device-config path rule: next to the
    exe or `paths.game_files`? — decision for the integrator; the engine takes a path); `IpuSettings` stays constant unless a
    SETTING row is opened; `audio.enabled` gates `SdlAudioSink::Open`.
15. **Meta gate**: stamp `avcodec_version()` into `run.meta` / the config fingerprint (CONSTRUCTION §4.7 `:701-703`) via the
    `LegCloser`/`Config::Fingerprint` owner.
16. **`docs/phases/README.md` row for Phase 7** — orchestrator/user, after the module gate (the file is READ-ONLY for every
    Phase-7 stage; the row states the module-gate result and points at `PHASE7_IMPL.md`).

## 9. HANDOFF template (each stage appends one to `PHASE7_IMPL.md`; the Phase-4 §8.5 form)

```
### HANDOFF (7<X> → 7<Y>)
**Done:** <files landed (paths)>; <OLD sources transplanted (file:lines)>; <flags applied: NAME → outcome>; <tests added
(ctest names) + result line>; <OLD bounded runs: command, wall, side effects (OLD/logs/*)>; <ledger/doc deltas in owned files>.
**Not done (moved to 7<Y>):** <exact remainder from the §7 row>.
**Module gate status:** (a)..(g) each ✔ / ✘ / – (not yet) with one evidence phrase.
**Integration checklist:** items of §8.2 touched by this stage (documented only), if any.
**Next command:** cmake --build --preset macos-dev -j4 && ctest --preset macos-dev, then <the first test or helper command of
7<Y> with its expected output line>.
```

## 10. Self-review (plan-writer, 2026-09-27)

Checked against DECISIONS D1–D8 and INV, item by item; corrections applied in place:

1. **Scope literalness.** Every OWNED/SHARED/FORBIDDEN entry of DECISIONS appears in §1.4/§1.5 and gate item (e); no row of §2
   creates a file outside the OWNED set; the three SHARED files appear only as `# Phase 7` append blocks (§7 preamble clarifies
   "one block, extended in place"). `platform.h` is never edited (D1 keeps `InputState`/`AudioSpec` as read-only inputs; INV
   RISK 11's `audio_open` extension is therefore NOT done — the sink takes the ring directly, §2.3).
2. **D1 interfaces.** All five interface headers (`GuestMemory`, `FmvPcmSink`, `FmvSectorSource`, `DmacChannelFace`,
   `VideoDecoder`) each have a §2.2 file row (one primary type with member functions → its own header under `LintOneClass`),
   plus `PcmSource`=ring and `PadSample`; the `FmvPacer` no-clock rule is in §1.4 with its OLD provenance; the `engine/ee/mem`
   check D1 asked for was done (no virtual face → declared by us, §1.4/§2.2). Gate item (f) makes the include discipline greppable.
3. **D2 diagram.** §3.1 (Mixer → AudioPullRing → SDL callback) and §3.2 (main-thread poll → InputSystem → InputState → PadProtocol
   stamp) carry the quantum/capacity/underrun/overrun/never-blocks/never-feeds-back rules and the headless-leg source; the FMV data
   flow is §3.3. NEW mechanism vs transplant is stated in §2.3 (row) and §3.3 (closing paragraph).
4. **D3 flags.** 12 rows in §5.1–§5.5 with the two findings as a separate table (§5.5) and the reason they are not accepted
   differences; the DIAG rows state "site not written, category = integration"; `FLAG_PHASE_COUNTS.txt` P7 is quoted, not edited.
5. **D4 module table.** All named pairs present (§2.1–§2.4), `libipu_api` fixed to 7E; file names are
   snake_case of the class; every OLD anchor is `file:line` from INV, re-checked against OLD. Counts: 3 ipu · 14 hle (5 interface
   headers + 1 layout POD) · 2 sdl · 8 input class rows; 7 ipu tests + tool; 12 executables / 13 ctests.
6. **D5 golden.** §4.3 gives the exact helper (monkey-patches `run_leg.build_leg_env` — verified: `run_leg.py:34` imports the name
   into its namespace, the call is at `:101`, `main()` exists at `:190`), the baseline-identity proof under `diff_legs.py
   --cross-engine` (meta policy only — `run_meta.cpp:33-49` folds `PS2_MPEG_DUMP_N` into `env_hash`, so the plain mode would exit 2
   before any lane compare, `diff_legs.py:108-112`), the `check_env_delta.py` pin (`frames=1494`, delta == exactly
   `PS2_MPEG_DUMP_N=1000`), the step-0 pre-count + mtime-filtered copy (OLD tooling never deletes `logs/mpegpic_*.ppm`; 0 present
   on 2026-09-27), the `served_total` definition (count of copied PPMs, contiguous indices asserted by `make_manifest.py`), the
   fixture header (`avcodec_version`, `build_id`, `served_total`), the PREFIX assertion and the meta-gate rule.
7. **D6 stages.** 7A–7E with scope / OLD sources / green criterion / split columns; 7B′ and 7D′ pre-registered; 7E's no-golden
   fallback stated.
8. **D7 binding table.** §8.1 lists every row incl. the two NOT-rows and the trap policy; marked documented-not-coded; §8.2 lists
   16 integration items with owners (item 16 = the `docs/phases/README.md` row, orchestrator-owned).
9. **Findings.** The five requested ids are pre-filled (§7.1) plus three plan-time findings discovered while writing
   (P7-NO-ANCHOR-PADLOG from the `verify/baseline/*/` listing; P7-MPEG-IPU-SPLIT; P7-IPU-FIFO-PORT from INV OPEN Q8).
10. **Prose rules.** No session tags except inside backticks quoting OLD log/comment identifiers (`[R196-…]`, `[FMV-ABORT-HOLD]`,
    `[INLAT-T1]`); no history narration; ~775 lines (target 600–900); exactly one file written. `docs/phases/README.md` is
    READ-ONLY for Phase 7 (not in the OWNED set, DECISIONS) — no stage edits it; its Phase-7 row is an orchestrator item after the
    module gate, listed as §8.2 item 16.

Open for the orchestrator (not blocking 7A): whether `tests/input/` should be renamed `tests/platform_sdl_input/` for symmetry with
P7-TESTS-PLATFORM-SDL-DIR (the plan keeps CONSTRUCTION §4.7's `tests/input` name), and whether `libipu_api`'s `DmacChannelFace`
should instead wait for Phase 5's dmac face (the plan declares a two-method interface in `engine/ipu/hle/`).

## HANDOFF (plan → stage 7A)

- Done: this plan (§1–§9 + §10 review). Facts read: `/tmp/p7/DECISIONS.md`, `/tmp/p7/INVESTIGATION.md`, `STAGE_RULES.md`,
  `PHASE4_PLAN.md` §1/§2.7/§2.8/§7/§8.5/HANDOFF, CONSTRUCTION §4.7 `:665-703`; NEW greps (read-only): `engine/ee/mem/*.h`
  (no virtual memory face → `GuestMemory` declared by us), `tick_sync.h:22-56`, `engine/platform/CMakeLists.txt`,
  `tests/CMakeLists.txt`, `FLAG_PHASE_COUNTS.txt:59-62`, `verify/baseline/*/` listing (no `input.padlog`), `verify/baseline/coldboot/run.meta`
  (`frames=1494`, `env_ps2` shape), `verify/diff_legs.py:92-121` (meta policy), `engine/{ipu,
  ipu/hle,platform/sdl}/README.md`, `perf_profiler.h` (no `kMpeg`/`kIpu`); OLD re-reads: `mpeg_hle.h:56-190`,
  `mpeg_hle.cpp:706/1679-1680/2195/2223/2313/2376`, `run_meta.cpp:33-49`, `logs/mpegpic_*.ppm` count (0). No code, no runs,
  no OLD binary executed.
- Not done: everything in §7 (stages 7A–7E). Tree unchanged except this file; no commits; OLD read-only.
- Next command (stage 7A): `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev` (expect 88/88), then read
  `OLD/host_app/sound/mixer.{h,cpp}` and `verify/verify_boundary.cpp:60-73`, write `engine/platform/sdl/{audio_pull_ring,
  audio_sink}.{h,cpp}` + `tests/platform_sdl/*` per §2.3/§4.4, append the `# Phase 7` blocks to `engine/platform/CMakeLists.txt`
  and `tests/CMakeLists.txt`, create `PHASE7_IMPL.md` with §7A; expect `ctest -R 'AudioPullRing|SdlAudioSinkDummy'` → 2/2.
