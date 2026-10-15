// engine/ipu/hle/fmv_pcm_tally_sink.h -- FmvPcmTallySink: an FmvPcmSink that
// only counts. The movie's muxed PCM belongs in the mixer after the voice mix
// (PHASE7 checklist item 2, the sound module's FmvPcmSink); until that hook
// exists the composition root binds this sink so the movie path runs and the
// census shows the frames the mixer would have received, the gate opens and the
// flushes. No PCM is kept.
#pragma once

#include <cstdint>

#include "engine/ipu/hle/fmv_pcm_sink.h"

namespace ps2r::ipu {

class FmvPcmTallySink : public FmvPcmSink {
public:
    void Push(const int16_t* stereo, uint32_t frames) override;
    void GateOpen() override;
    void Flush() override;

    uint64_t FramesPushed() const { return frames_; }
    uint64_t Pushes() const { return pushes_; }
    uint64_t GateOpens() const { return gate_opens_; }
    uint64_t Flushes() const { return flushes_; }
    bool Open() const { return open_; }

private:
    uint64_t frames_ = 0;
    uint64_t pushes_ = 0;
    uint64_t gate_opens_ = 0;
    uint64_t flushes_ = 0;
    bool open_ = false;
};

}  // namespace ps2r::ipu
