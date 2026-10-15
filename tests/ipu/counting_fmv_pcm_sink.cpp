// tests/ipu/counting_fmv_pcm_sink.cpp -- CountingFmvPcmSink bodies.
#include "counting_fmv_pcm_sink.h"

void CountingFmvPcmSink::Push(const int16_t* stereo, uint32_t frame_count) {
    samples.insert(samples.end(), stereo, stereo + static_cast<size_t>(frame_count) * 2);
    ++push_calls;
    frames += frame_count;
}

void CountingFmvPcmSink::GateOpen() { ++gate_opens; }

void CountingFmvPcmSink::Flush() { ++flushes; }
