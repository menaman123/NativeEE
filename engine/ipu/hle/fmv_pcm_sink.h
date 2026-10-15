// engine/ipu/hle/fmv_pcm_sink.h -- FmvPcmSink: where the PCM muxed inside a
// program stream's private_stream_1 packets goes. The container carries raw
// PCM16LE 48 kHz stereo (the SPU2 output rate, so no resampling is involved);
// the sink is implemented by the sound module at integration and mixes the
// frames after the voice mix under core-0 BVOLL/BVOLR.
#pragma once

#include <cstdint>

namespace ps2r::ipu {

class FmvPcmSink {
public:
    virtual ~FmvPcmSink() = default;

    // `frames` interleaved stereo frames (L0 R0 L1 R1 ...), 48 kHz, 16-bit.
    virtual void Push(const int16_t* stereo, uint32_t frames) = 0;

    // Audio may flow from now on: called when the first video picture of a
    // movie is served, so A/V start together (PSS audio and video share their
    // presentation origin).
    virtual void GateOpen() = 0;

    // Lifecycle flush (sceMpegInit / sceMpegReset / sceMpegDelete): closes the
    // gate and discards queued PCM so a skipped movie's sound stops at once.
    virtual void Flush() = 0;
};

}  // namespace ps2r::ipu
