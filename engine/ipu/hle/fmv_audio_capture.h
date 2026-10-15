// engine/ipu/hle/fmv_audio_capture.h -- FmvAudioCapture: turns the private_stream_1
// payloads of a movie's program stream into PCM frames for the FmvPcmSink. The
// container carries raw PCM, not a codec: each payload starts with the 4-byte
// substream prefix FF A0 00 00; the first payload then holds a Sony ADS header
// ('SShd' u32 len(0x18) {u32 format(1 = PCM16LE), u32 rate(48000), u32 channels(2),
// u32 interleave(0x200), ...} 'SSbd' u32 body_size) and every later payload is
// body continuation. The body is PLANAR: 512 bytes of left samples then 512 bytes
// of right samples per 1024-byte pair (interleave 0x200). Capture = header
// guard + de-interleave + push; anything off the pinned format disables capture
// for the context (never a crash, never a wrong-rate push).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/ipu/hle/fmv_pcm_sink.h"

namespace ps2r::ipu {

class FmvAudioCapture {
public:
    static constexpr uint32_t kPairBytes = 1024;    // 512 L + 512 R
    static constexpr uint32_t kFramesPerPair = 256; // 16-bit samples per plane

    explicit FmvAudioCapture(FmvPcmSink& sink);

    // One audio PES payload (after ParsePesPayloadOffset). A payload whose
    // first byte is not 0xFF belongs to a substream this path does not model
    // and is skipped without disabling.
    void OnAudioPes(const uint8_t* p, size_t n);

    // Back to the pre-header state (sceMpegReset / a new movie).
    void Reset();

    bool Disabled() const { return disabled_; }
    bool HeaderParsed() const { return header_parsed_; }
    // At least one PCM pair has reached the sink (the A/V gate opens only for
    // a context that carries audio).
    bool Pushed() const { return pushed_; }
    // Body bytes the SSbd size still promises (0 once the body is complete).
    uint32_t BodyLeft() const { return body_left_; }
    // Sub-pair tail waiting for the next payload.
    size_t StagedBytes() const { return stage_.size(); }

private:
    void Disable();
    bool ParseHeader();
    void PushCompletePairs();

    FmvPcmSink& sink_;
    bool header_parsed_ = false;
    bool disabled_ = false;
    bool pushed_ = false;
    uint32_t body_left_ = 0;
    std::vector<uint8_t> stage_;
};

}  // namespace ps2r::ipu
