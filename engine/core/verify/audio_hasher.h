// core/verify/audio_hasher.h -- mixer-output hash per 100 ms of samples
// (EQUIVALENCE_PLAN section 1.3, the audio lane).
//
// Fed the EXACT interleaved stereo float frames the mixer produced. One
// window = 4800 sample frames (100 ms at 48 kHz); the hash covers the raw
// little-endian bytes of the float pairs, so bit-identical mixing hashes
// identically and any sample-level divergence flips the window.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "engine/core/hash/hash.h"

namespace ps2r::verify {

class AudioHasher {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kWindowFrames = kSampleRate / 10;   // 100 ms

    // sink(window_index, hash) runs for every COMPLETED window.
    explicit AudioHasher(std::function<void(uint64_t, uint64_t)> sink) : sink_(std::move(sink)) {}

    // Push `frames` interleaved stereo float frames (2 floats each).
    void Push(const float* interleaved, int frames);

    // Emit a partial trailing window with its frame count folded in, so a
    // short tail never aliases a full window. No-op when nothing is pending.
    void Finish();

    uint64_t WindowsEmitted() const { return next_window_; }

private:
    std::function<void(uint64_t, uint64_t)> sink_;
    uint64_t hash_ = hash::kFnv1a64Offset;
    int      filled_ = 0;
    uint64_t next_window_ = 0;
};

}  // namespace ps2r::verify
