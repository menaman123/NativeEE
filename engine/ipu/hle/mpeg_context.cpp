// engine/ipu/hle/mpeg_context.cpp -- MpegContext bodies (see the header).
#include "engine/ipu/hle/mpeg_context.h"

#include <algorithm>
#include <utility>

namespace ps2r::ipu {

MpegContext::MpegContext(DecoderFactory factory, const IpuSettings& settings, FmvPcmSink& sink)
    : audio(sink), demux(*this), factory_(std::move(factory)), settings_(settings) {}

void MpegContext::EnsureDecoder() {
    if (!decoder && factory_) decoder = factory_();
}

void MpegContext::OnVideoEs(const uint8_t* data, size_t size, bool contains_sequence_end) {
    saw_input = true;
    EnsureDecoder();
    // The guest-visible mirror happens at parse time regardless of whether
    // the decode of this payload is deferred.
    if (on_video_payload) on_video_payload(data, size);

    // Bounded decode-ahead: once the FIFO holds the cap (or a backlog already
    // exists -- elementary-stream order must be preserved) the compressed
    // payload waits in pending_es and is decoded as pictures are served.
    const bool defer = frames.size() >= settings_.decode_ahead_pictures || PendingBacklog();
    if (defer) {
        pending_es.insert(pending_es.end(), data, data + size);
        if (contains_sequence_end) end_after_pending = true;
        return;
    }
    if (contains_sequence_end) stream_ended = true;
    if (decoder) decoder->Feed(data, size, frames);
}

void MpegContext::OnAudioPes(const uint8_t* data, size_t size) {
    if (audio_allowed && !audio_allowed()) return;
    audio.OnAudioPes(data, size);
}

void MpegContext::OnProgramEnd() {
    if (PendingBacklog()) {
        end_after_pending = true;
        return;
    }
    stream_ended = true;
    FlushIfStreamEnded();
}

void MpegContext::Demux(const uint8_t* data, size_t size, bool final_chunk) {
    demux.Append(data, size);
    demux.Process(final_chunk);
}

bool MpegContext::StreamEnded() const { return stream_ended && frames.empty() && frames_served > 0; }

void MpegContext::FlushIfStreamEnded() {
    if (stream_ended && decoder) decoder->Flush(frames);
}

void MpegContext::DrainPendingEs() {
    const size_t cap = settings_.decode_ahead_pictures;
    if (!PendingBacklog()) {
        // Backlog empty: promote a parse-time end marker, if any.
        if (end_after_pending && !stream_ended) {
            stream_ended = true;
            end_after_pending = false;
            FlushIfStreamEnded();
        }
        return;
    }
    EnsureDecoder();

    while (frames.size() < cap && PendingBacklog()) {
        const size_t n = std::min(kDrainSliceBytes, pending_es.size() - pending_es_off);
        if (decoder) decoder->Feed(pending_es.data() + pending_es_off, n, frames);
        pending_es_off += n;
    }
    if (!PendingBacklog()) {
        pending_es.clear();
        pending_es_off = 0;
        if (end_after_pending && !stream_ended) {
            stream_ended = true;
            end_after_pending = false;
            FlushIfStreamEnded();
        }
    } else if (pending_es_off > kCompactBytes) {
        // Compact the consumed prefix so the backlog never doubles.
        pending_es.erase(pending_es.begin(),
                         pending_es.begin() + static_cast<std::ptrdiff_t>(pending_es_off));
        pending_es_off = 0;
    }
}

void MpegContext::EndFromSource() {
    demux.MarkEnded();
    if (PendingBacklog()) {
        end_after_pending = true;
        return;
    }
    stream_ended = true;
    FlushIfStreamEnded();
}

void MpegContext::ResetPlayback() {
    frames_served = 0;
    saw_input = false;
    stream_ended = false;
    demux.Reset();
    frames.clear();
    audio.Reset();
    gate_opened = false;
    pending_es.clear();
    pending_es_off = 0;
    end_after_pending = false;
    // The tick grid and its period survive sceMpegReset on the same context:
    // only sceMpegInit (a new context) starts a fresh grid. A movie that
    // follows another on the same context therefore catches up with the
    // carried grid (late, instant serves) rather than arming anew -- that
    // movie-to-movie catch-up is part of the delivery contract. The period is
    // not re-derived either (PaceDelivery latches it only while unset), so the
    // rate word stays as the first movie latched it.
    // Keep the decoder object; releasing its codec state clears the reference
    // pictures so the next movie's sequence header starts clean.
    if (decoder) decoder->Shutdown();
}

}  // namespace ps2r::ipu
