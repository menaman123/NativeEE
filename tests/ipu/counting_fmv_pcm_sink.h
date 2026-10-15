// tests/ipu/counting_fmv_pcm_sink.h -- CountingFmvPcmSink: an FmvPcmSink that keeps
// every pushed frame and counts the lifecycle calls.
#pragma once

#include <cstdint>
#include <vector>

#include "engine/ipu/hle/fmv_pcm_sink.h"

class CountingFmvPcmSink : public ps2r::ipu::FmvPcmSink {
public:
    std::vector<int16_t> samples;  // interleaved L R L R ...
    unsigned push_calls = 0;
    uint64_t frames = 0;
    unsigned gate_opens = 0;
    unsigned flushes = 0;

    void Push(const int16_t* stereo, uint32_t frame_count) override;
    void GateOpen() override;
    void Flush() override;
};
