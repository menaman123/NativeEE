// core/verify/audio_hasher.cpp
#include "engine/core/verify/audio_hasher.h"

#include "engine/core/hash/hash.h"

namespace ps2r::verify {

void AudioHasher::Push(const float* interleaved, int frames) {
    int done = 0;
    while (done < frames) {
        const int room = kWindowFrames - filled_;
        const int n = (frames - done) < room ? (frames - done) : room;
        hash_ = hash::Fnv1a64(interleaved + (size_t)done * 2, (size_t)n * 2 * sizeof(float), hash_);
        filled_ += n;
        done += n;
        if (filled_ == kWindowFrames) {
            if (sink_) sink_(next_window_, hash_);
            next_window_++;
            filled_ = 0;
            hash_ = hash::kFnv1a64Offset;
        }
    }
}

void AudioHasher::Finish() {
    if (filled_ == 0) return;
    const uint32_t tail = (uint32_t)filled_;
    const uint8_t b[4] = {(uint8_t)(tail & 0xFF), (uint8_t)((tail >> 8) & 0xFF), (uint8_t)((tail >> 16) & 0xFF),
                          (uint8_t)((tail >> 24) & 0xFF)};
    hash_ = hash::Fnv1a64(b, sizeof(b), hash_);
    if (sink_) sink_(next_window_, hash_);
    next_window_++;
    filled_ = 0;
    hash_ = hash::kFnv1a64Offset;
}

}  // namespace ps2r::verify
