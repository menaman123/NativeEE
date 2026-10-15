// engine/ipu/hle/mpeg_context.h -- MpegContext: the per-SceMpeg-context playback
// state of the sceMpeg HLE (one per context address the game passes as `mpeg`):
// the program-stream demuxer, the elementary-stream decoder with its bounded
// decode-ahead deferral, the decoded-picture FIFO, the delivery pacer and the
// muxed-PCM capture. It is the demuxer's listener: video payloads are decoded (or
// deferred behind the decode-ahead cap), audio payloads go to the capture, the
// program end / sequence end mark the stream ended. Guest-memory side effects
// (the bitstream ring mirror, the audio-context rule) are hooks MpegApi installs;
// nothing here reads guest memory, a clock or the environment.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

#include "engine/ipu/hle/decoded_picture.h"
#include "engine/ipu/hle/fmv_audio_capture.h"
#include "engine/ipu/hle/fmv_pacer.h"
#include "engine/ipu/hle/fmv_pcm_sink.h"
#include "engine/ipu/hle/pss_demux.h"
#include "engine/ipu/hle/pss_demux_listener.h"
#include "engine/ipu/hle/video_decoder.h"
#include "engine/ipu/ipu_settings.h"

namespace ps2r::ipu {

class MpegContext : public PssDemuxListener {
public:
    using DecoderFactory = std::function<std::unique_ptr<VideoDecoder>()>;
    // Guest side effect per video payload (the bitstream ring mirror).
    using VideoPayloadHook = std::function<void(const uint8_t* data, size_t size)>;
    // Whether this context's audio payloads are the movie's sound (only the
    // context that owns a bitstream ring carries audio; texture-streaming
    // contexts share the MPEG path and must never inject sound).
    using AudioAllowedHook = std::function<bool()>;

    // Picture dimensions reported before any picture has been served.
    static constexpr uint32_t kDefaultWidth = 512;
    static constexpr uint32_t kDefaultHeight = 256;
    // Deferred elementary stream is decoded in slices of this many bytes.
    static constexpr size_t kDrainSliceBytes = 64 * 1024;
    // Consumed prefix of the deferred backlog is compacted past this size.
    static constexpr size_t kCompactBytes = 4u << 20;

    MpegContext(DecoderFactory factory, const IpuSettings& settings, FmvPcmSink& sink);

    // PssDemuxListener.
    void OnVideoEs(const uint8_t* data, size_t size, bool contains_sequence_end) override;
    void OnAudioPes(const uint8_t* data, size_t size) override;
    void OnProgramEnd() override;

    // Appends program-stream bytes and walks every complete packet.
    void Demux(const uint8_t* data, size_t size, bool final_chunk);

    // The movie has genuinely finished: the elementary stream ended, every
    // decoded picture has been served, and at least one picture WAS served
    // (the last clause keeps a front-loaded end of stream from ending the
    // movie before the player showed anything).
    bool StreamEnded() const;

    // Decodes deferred elementary-stream bytes until the picture FIFO holds
    // `decode_ahead_pictures` or the backlog empties; a backlog that drains
    // completely promotes a deferred end marker to `stream_ended` and flushes
    // the decoder.
    void DrainPendingEs();

    // The stream ended by an external event (CD stream end of file, sector
    // source exhausted): ends now, or after the pending backlog drains.
    void EndFromSource();

    // Drains the decoder's buffered pictures if the stream has ended.
    void FlushIfStreamEnded();

    // sceMpegReset: fresh playback (pictures, backlog, capture); the decoder
    // object is kept but its codec state is released. The pacer's tick grid
    // and period are KEPT: only a new context starts a fresh grid.
    void ResetPlayback();

    bool PendingBacklog() const { return pending_es_off < pending_es.size(); }
    bool AudioCarried() const { return audio.Pushed() || audio.HeaderParsed(); }

    // Playback state (the fields the entry points read and write).
    uint32_t width = kDefaultWidth;
    uint32_t height = kDefaultHeight;
    uint32_t frames_served = 0;
    bool saw_input = false;
    bool stream_ended = false;
    bool end_after_pending = false;
    uint32_t ctx_addr = 0;  // the EE SceMpeg context address this state is keyed on
    std::deque<DecodedPicture> frames;
    std::unique_ptr<VideoDecoder> decoder;
    std::vector<uint8_t> pending_es;
    size_t pending_es_off = 0;
    FmvPacer pacer;
    double pace_rate_fps = 0.0;  // latched delivery rate (0 = not yet armed)
    FmvAudioCapture audio;
    bool gate_opened = false;  // the sink's GateOpen has been issued for this playback
    PssDemux demux;
    VideoPayloadHook on_video_payload;
    AudioAllowedHook audio_allowed;

private:
    void EnsureDecoder();

    DecoderFactory factory_;
    const IpuSettings& settings_;
};

}  // namespace ps2r::ipu
