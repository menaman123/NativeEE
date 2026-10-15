# `engine/ipu/hle/`

**Phase:** 7 (stages 7D, 7D′, 7E)  
**Purpose:** the sceMpeg / libipu HLE pipeline: program-stream demux, video decoding behind a small interface, the IPU-layout frame writer, the tick-domain delivery pacer, the muxed-PCM capture, and the per-context API the SDK bindings call. Everything here is title-neutral: the SceMpeg context offsets are a parameter (`FmvContextLayout`) the title fills at integration.

## Belongs here — landed (stages 7D, 7D′, 7E)

| File | Class | What it is | OLD provenance (read-only) |
|---|---|---|---|
| `guest_memory.h` | `GuestMemory` | pure interface: `Translate(addr, len)`, `Read32`, `Write32` — the EE-RAM face the HLE bodies use (never `engine/ee/mem` directly) | replaces `memory::translate_address` / `memory::read<u32>` at `mpeg_hle.cpp:1729-1766`, `:1785`, `:1613` |
| `fmv_pcm_sink.h` | `FmvPcmSink` | pure interface: `Push(const int16_t* stereo, frames)`, `GateOpen()`, `Flush()` — implemented by the sound module at integration | mirrors `sound::FmvPushPcm/FmvGateOpen/FmvFlush`, `sound_core.h:168-170` |
| `fmv_sector_source.h` | `FmvSectorSource` | pure interface: `PumpSectors(dst, max_sectors)` over the shared CD stream cursor | mirrors `cdvd_stream::PumpSectors`, `cdvd_stream.h:24-47` |
| `dmac_channel_face.h` | `DmacChannelFace` | pure interface: CHCR/MADR/TADR/QWC read/write for channels 3 (IPU_FROM) and 4 (IPU_TO) — the words sceIpuStopDMA/RestartDMA snapshot | `libipu.cpp:139-172`, `:212-264` |
| `decoded_picture.h` | `DecodedPicture` | method-less POD `{int width, height; std::vector<uint8_t> rgba}` (RGBA8 row-major) | `mpeg_hle.cpp:111-115` (`DecodedFrame`) |
| `video_decoder.h` | `VideoDecoder` | interface: `Feed(data, size, deque&)`, `Flush(deque&)`, `AuthoredFps()`, `Shutdown()` | the public shape of `FfmpegDecoder`, `mpeg_hle.cpp:139-233` |
| `fmv_context_layout.h` | `FmvContextLayout` | POD of SceMpeg ctx byte offsets (`ring_base/size/write/read/anchor`, `inner_status_ptr`, `total`, `presented`, `mode`, `plane_select`, `plane0/1`, `target_w/h`, `abort_flag`), the sceMpegAlalc allocator word offsets (`alalc_base/capacity/watermark/base_copy`), the inner work area's reference-slot offsets (`ref_slot[kRefSlots = 6]`, `ref_slot_status`) plus the two guest word VALUES the HLE recognises/writes (`play_to_eof_total`, `mode_raw8`) — NO defaults; each field's libmpeg meaning is in the header comment | offsets/values NOT copied: `mpeg_hle.cpp:873-877`, `:1013`, `:1175-1177`, `libmpeg.cpp:198-206`, `:413-416`, `:706-714` |
| `pss_demux_listener.h` | `PssDemuxListener` | interface: `OnVideoEs(data, size, contains_sequence_end)`, `OnAudioPes(data, size)`, `OnProgramEnd()` | the three branches of `ProcessBuffer` |
| `pss_demux.h/.cpp` | `PssDemux` | the 13818-1 packet walk: `Append`, `Process(final_chunk)`, `Reset`, `MarkEnded`, `Ended`, `Consumed`, `PacksSeen`, `SystemHeadersSeen`; static classifiers `FindStartCode`, `ContainsSequenceEnd`, `IsVideoStreamId` (E0..EF), `IsAudioStreamId` (BD, C0..DF), `IsLengthPrefixedHeader` {BB,BC,BE,BF,F0,F1,F2,F8,FF}, `ParsePesPayloadOffset` (MPEG-2 `9 + header_data_length`; MPEG-1 stuffing / STD / PTS / PTS+DTS / 0x0F). Pack header 12 (MPEG-1) or `14 + (buf[13] & 7)` (MPEG-2); `PES_packet_length == 0` runs to the next start code; the last 3 bytes are kept when no start code is visible (unless final); program end or a video payload with `00 00 01 B7` ends the stream and every later byte is discarded | `mpeg_hle.cpp:82-105`, `:754-829`, `:1463-1625` (the `PS2_MPEG_SEQCENSUS` census `:1431-1461` not written) |
| `frame_writer.h/.cpp` | `FrameWriter` | static `Rgb32Bytes/Rgb16Bytes(w, h)`, `WriteRgb32Strips(dst, pic)`, `WriteRgb16Strips(dst, target_w, target_h, pic)`: 16-pixel column strips × `aligned_h` rows; RGB32 alpha forced `0x80`, padding `0,0,0,0x80`; PSMCT16 `r5 | g5<<5 | b5<<10 | 0x8000` little-endian, outside the decoded extent `0x8000` | `mpeg_hle.cpp:1773-1822`, `:1832-1886` |
| `fmv_pacer.h/.cpp` | `FmvPacer` | int64 tick grid: `SetPeriod`, `SetAuthoredRateTicks(fps)` = `(int64)(60/fps + 0.5)` min 1 (`fps <= 0` → `kFallbackFps` 29.97), `Next(now_tick)` (first delivery arms `now + period` with wait 0; late → `late = true`, wait 0, grid unchanged — NO debt clamp; every call advances one period), `Reset`, `Armed`, `Period`, `NextTick`, static `ShouldPace(frames_served) = frames_served >= 2` | `mpeg_hle.h:188-` (`TickPace::NextBurst`, the SOLE core — finding P7-LEDGER-BURST), `mpeg_hle.cpp:584`, `:636-643`; the debt-clamp `Next` (`mpeg_hle.h:158-178`) and the wall-clock `PaceDeadline` (`:56-105`) are NOT written |
| `fmv_audio_capture.h/.cpp` | `FmvAudioCapture` | private_stream_1 payload → PCM: `p[0] != 0xFF` skip; `SShd` len `0x18` `{format 1, rate 48000, channels 2, interleave 0x200}` validated else `Disabled()`; `SSbd` required; body bound honoured; each complete 1024-byte planar pair → 256 interleaved frames → `FmvPcmSink::Push` (`Pushed()` latches); `Reset()` | `mpeg_hle.cpp:1302-1312`, `:1322-1428` (`warp::Active()` skip `:1328` and `PS2_SND_FMV` gate `:1314-1320` not written — NORM: capture unconditional) |

`GateOpen()` on the first served picture and `Flush()` on Init/Destroy/Reset are `MpegApi`'s calls, not the capture's.

| File | Class | What it is | OLD provenance (read-only) |
|---|---|---|---|
| `ffmpeg_video_decoder.h/.cpp` | `FfmpegVideoDecoder : VideoDecoder` | libavcodec MPEG-2 video: `avcodec_find_decoder(AV_CODEC_ID_MPEG2VIDEO)`, `av_parser_init`, `avcodec_alloc_context3`, `thread_count = 1`, `avcodec_open2`; `Feed` = `av_parser_parse2` loop → `av_new_packet`/`avcodec_send_packet` (EAGAIN: drain then retry once) → `avcodec_receive_frame` loop → `sws_getContext(w,h,fmt → w,h,RGBA, SWS_BILINEAR)` + `sws_scale` with `dst_ls {w*4,0,0,0}`; `Flush` = NULL parser packet then `send_packet(nullptr)`; `AuthoredFps()` latched from `codec_ctx->framerate` at the first picture; `Shutdown` frees everything; `static LibraryVersion()` "MAJOR.MINOR.MICRO" / `LibavcodecVersion()` from `avcodec_version()`. FFmpeg headers are included by the `.cpp` only; `ps2r_ipu` links `FFmpeg::avcodec/avutil/swscale` privately | `mpeg_hle.cpp:125-380` (`FfmpegDecoder`; the `g_logFile` decode-count lines not written, libav errors go to the core logger's IPU category) |
| `mpeg_context.h/.cpp` | `MpegContext : PssDemuxListener` | per-context playback state: `width/height` (512×256 until a serve), `frames_served`, `saw_input`, `stream_ended`, `end_after_pending`, `ctx_addr`, `deque<DecodedPicture> frames`, `unique_ptr<VideoDecoder>` (lazily built by the factory), `pending_es` + `pending_es_off`, `FmvPacer pacer` + `pace_rate_fps`, `FmvAudioCapture audio` + `gate_opened`, the owned `PssDemux demux` (its buffer IS the pending program-stream input); listener: `OnVideoEs` → `saw_input`, mirror hook, defer past `IpuSettings::decode_ahead_pictures` (or behind an existing backlog) else `stream_ended` on the sequence end + `Feed`; `OnAudioPes` → capture when the `audio_allowed` hook agrees; `OnProgramEnd` → `stream_ended` / `end_after_pending`; `DrainPendingEs` (64 KiB slices, promotion of the deferred end marker, compaction past 4 MiB); `EndFromSource` (`MarkEnded` + end now or after the backlog); `StreamEnded()` = `stream_ended && frames.empty() && frames_served > 0`; `ResetPlayback` clears pictures/backlog/capture/demux and `Shutdown`s the kept decoder object but KEEPS the pacer's grid and period (a second movie on the same context catches up with the carried grid; only a new context arms fresh) | `mpeg_hle.cpp:387-428`, `:747-749`, `:831-835`, `:1252-1300`, `:1488-1496`, `:1562-1618` (video/audio branches), `:1908-1942` (Reset body) |
| `mpeg_api.h/.cpp` | `MpegApi` | the entry points over `GuestMemory` + `FmvContextLayout` + `FmvSectorSource` + `FmvPcmSink` + a `VideoDecoder` factory + `IpuSettings`: `Init` (table cleared, `Flush`), `Destroy` (`Flush` if the ctx carried audio), `Reset` (same rule, `ResetPlayback`), `NotifyCdStreamEof` (`Process(final)` + `EndFromSource` per open ctx), `DemuxPss` / `DemuxPssRing` (flat / wrap-around guest copy → `Demux` → `WriteInnerStatus` → `HoldFmvCompletionGate`), `GetPicture` / `GetPictureRaw8` → `PictureResult {served, wait_ticks, late}` (`DrainPendingEs` → flush-once at end → `PumpUntilFrame` (`sectors_per_pump`, `max_sector_budget`, ring-owning ctx only) → pace booking (`ShouldPace(frames_served)`; rate latch `AuthoredFps()` else 29.97 → `SetAuthoredRateTicks`; `pacer.Next(now_tick)` RETURNED, never waited) → pop → `FrameWriter::WriteRgb32Strips` / `WriteRgb16Strips` into `Translate(addr, bytes)` → `RingDrainToWrite` → `frames_served++` → first `GateOpen` → `WriteInnerStatus`), `IsEnd`, `ShouldHoldAbort`, `Width/Height`, `FmvActive()`; the libmpeg-internal guest-word helpers `AlalcInit(alalc, base, size)` (four word stores), `AlalcAlloc(alalc, size, align)` (watermark aligned up, `0` for a null allocator / zero align / exhausted region) and `ClearRefBuff(mpeg)` (zero `ref_slot_status` in every non-null `ref_slot` of the inner area) over the layout offsets; a null image/plane pointer or zero plane geometry writes nothing but the serve counts; guest side effects `RingFillVideoPayload` (write cursor + wrap over the layout offsets), `RingDrainToWrite`, `WriteInnerStatus`, `HoldFmvCompletionGate` (ring-owning ctx in `mode_raw8` → hold `total` at `play_to_eof_total` until `StreamEnded`). One mutex over the ctx table | `mpeg_hle.cpp:881-970`, `:972-979`, `:1013-1035`, `:1175-1200`, `:1664-1720`, `:1729-1766`, `:1890-1906`, `:1944-2032`, `:2043-2424` (serve; every diagnostics block of INV A3 not written), `:2426-2557`; `libmpeg.cpp:189-206`, `:209-249`, `:403-424` (the helpers; their `g_logFile`/`log_call` lines not written) |
| `libipu_api.h/.cpp` | `LibipuApi` (+ `LibipuDmaSnapshot` POD, nine SDK words) | `Init` issues RST/BCLR twice; quantizer/CLUT/threshold seeding remains omitted. `Sync` polls the synchronous device. `StopDma` snapshots channel words, BP and CTRL. `RestartDma` restores ch3, issues BCLR with **word [7] IPU_BP.BP**, and rewinds ch4 by **word [7] IPU_BP.FP + IPU_BP.IFC**. Word [8] is CTRL and must not supply those fields. | OLD `libipu.cpp` for transplant provenance; EE User's Manual 8.3 and a read-only third-party reverse-engineering of the title's movie code for the corrected resume rule. |


The real `DmacChannelFaceAdapter` calls an injected channel starter after STR
writes. Stop/resume snapshots normalize input beyond the capped IFC into
TO-MADR/QWC, preserving the nine-word format without private snapshot state.
Invalid rewind addresses/counts reject before restart discards input; the
lifetime `LibipuApi::DmaSnapshotErrorCount()` observes these errors.
This is a contiguous-buffer model; source-chain/input-backpressure and the
RAW8 caller's subsequent conversion remain open in the completion report.

## SDK binding rows — integration — NOT installed by Phase 7

Rows are keyed by (library, function) — never an address; the title manifest lists every symbol. The installed bindings are `engine/ipu/hle/{libmpeg,libipu}_bindings.cpp`; the outer `GetPicture*` and demux replacements bypass the retail decoder internals, which remain TRAP rows where no handler is registered. See `docs/design/subsystem_completion_ipu.md` for the current reachability limits and the RAW8 bypass contract.

| Library | Function | Handler (Phase-7 class) | Notes / OLD (`libmpeg.cpp` / `libipu.cpp` line) |
|---|---|---|---|
| libmpeg.a | `sceMpegInit` | `MpegApi::Init` | then DMAC ch3/ch4 `CHCR.STR`/`QWC` clear (ee/dmac integration), then `sceIpuInit`; `v0 = 1` (`:70-95`) |
| libmpeg.a | `sceMpegCreate` | recompiled body via dispatch | no HLE (`:130-133`) |
| libmpeg.a | `_sceMpegAlalcInit` / `_sceMpegAlalcAlloc` | `MpegApi::AlalcInit(a0, a1, a2)` / `v0 = MpegApi::AlalcAlloc(a1, a2, a3)` (a0 = the mpeg ctx, unused) | 4 word stores / bump allocator (`:189-206`, `:209-249`) |
| libmpeg.a | `_clearOnce` / `_clearEach` | recompiled | `_clearEach` writes `IPU_CTRL` → `IpuDevice::WriteCtrl` (`:294-297`, `:373-376`, `:358`) |
| libmpeg.a | `sceMpegReset` | `MpegApi::Reset` then the recompiled body | (`:329-334`) |
| libmpeg.a | `sceMpegClearRefBuff` | `MpegApi::ClearRefBuff(a0)`; `v0 = 1` | for each of the six slot pointers read from `inner + ref_slot[i]`, when non-null zero the word at `ref_slot_status` inside that slot (offsets via `FmvContextLayout`) (`:403-424`) |
| libmpeg.a | `_sceMpegError1` | log row | (`:458-480`) |
| libmpeg.a | `sceMpegAddStrCallback` / `sceMpegAddCallback` | recompiled | (`:517-520`, `:559-562`) |
| libmpeg.a | `sceMpegDemuxPss` | `MpegApi::DemuxPss(a0, a1, a2)` | (`:594-602`) |
| libmpeg.a | `sceMpegDemuxPssRing` | `MpegApi::DemuxPssRing(a0, a1, a2, a3, *(sp+0x10))` | (`:616-628`) |
| libmpeg.a | `sceMpegGetPicture` | [`EeTimers` pump at the boundary] → `MpegApi::GetPicture(a0, a1, now_tick)` → park `wait_ticks` on `TickSync`; `v0 = served ? 0 : -1` | pump is game-visible (TIM0); the park = `NotePending(Demand)` loop (`:744-782`) |
| libmpeg.a | `sceMpegGetPictureRAW8` | [pump] → plane select by layout (`w = *(a0 + target_w)`, `h = *(a0 + target_h)`, `idx = (*(a0 + presented) & 1) ? (*(a0 + plane_select) != 0) : 0`, `plane = *(a0 + plane{idx})`) → `MpegApi::GetPictureRaw8` → park; `v0 = served ? mbcount : -1` | (`:663-737`) |
| libmpeg.a | `sceMpegIsEnd`, `sceMpegDelete` | **NOT rows** | handlers existed in OLD but were never bound |
| libipu.a | `sceIpuInit` | `LibipuApi::Init` (7E) → `IpuDevice` RST + BCLR | (`libipu.cpp:67-77`) |
| libipu.a | `sceIpuSync` | `LibipuApi::Sync` (7E) → CTRL.BUSY poll → 0 | (`:88-105`) |
| libipu.a | `sceIpuStopDMA` / `sceIpuRestartDMA` | `LibipuApi::StopDma` / `RestartDma` (7E) over a `DmacChannelFace` | ch3/ch4 CHCR/QWC snapshot (9 words) — dmac = integration (`:139-172`, `:212-264`) |
| libipu.a | `setD3_CHCR` | trap row | (registry `:421-434`) |

## Lands in 7E (landed)

- `libipu_api.h/.cpp` — `LibipuApi` (row above; ctest `LibipuApi`).
- The per-picture golden: `tests/ipu/fmv_picture_oracle.cpp` + `tests/ipu/fixtures/coldboot_pictures.sha256` (547 OLD-served pictures; `FfmpegVideoDecoder::LibraryVersion()` in the header; ctest `FmvPictureOracle`).

## Does not belong here

- Any absolute EE address or SceMpeg offset value (the title fills `FmvContextLayout`).
- Clocks, sleeps, thread parks (`FmvPacer` returns ticks, `MpegApi::PictureResult::wait_ticks` carries them; the caller parks).
- SDK binding rows (`*_bindings.cpp`), MMIO registration, DMAC state (interfaces only).
- The deleted arms: `PS2_MPEG_PACE_OFF`, `PS2_MPEG_PDIFF`, `PS2_MPEG_WM`, `PS2_MPEG_SEQCENSUS`, the debt-clamp pacer, the wall-clock pacer, the legacy present-coupled wait, the `PS2_MPEG_DUMP_N` probe.

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.7; `docs/phases/PHASE7_PLAN.md` §2.2, §3.3.
