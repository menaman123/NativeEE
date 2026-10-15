// platform/sdl/audio_pull_ring.cpp -- see audio_pull_ring.h for the contract.
#include "engine/platform/sdl/audio_pull_ring.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace ps2r::platform::sdl {

namespace {

// Slot index of a monotonic frame position.
inline int Slot(uint64_t pos) {
    return static_cast<int>(pos % static_cast<uint64_t>(AudioPullRing::kCapacityFrames));
}

}  // namespace

AudioPullRing::AudioPullRing() {
    std::memset(samples_, 0, sizeof(samples_));
}

void AudioPullRing::CopyIn(const float* src, int frames, uint64_t write_pos) {
    const int first = std::min(frames, kCapacityFrames - Slot(write_pos));
    std::memcpy(&samples_[static_cast<size_t>(Slot(write_pos)) * kChannels], src,
                static_cast<size_t>(first) * kChannels * sizeof(float));
    if (first < frames) {
        std::memcpy(&samples_[0], src + static_cast<size_t>(first) * kChannels,
                    static_cast<size_t>(frames - first) * kChannels * sizeof(float));
    }
}

void AudioPullRing::CopyOut(float* dst, int frames, uint64_t read_pos) const {
    const int first = std::min(frames, kCapacityFrames - Slot(read_pos));
    std::memcpy(dst, &samples_[static_cast<size_t>(Slot(read_pos)) * kChannels],
                static_cast<size_t>(first) * kChannels * sizeof(float));
    if (first < frames) {
        std::memcpy(dst + static_cast<size_t>(first) * kChannels, &samples_[0],
                    static_cast<size_t>(frames - first) * kChannels * sizeof(float));
    }
}

void AudioPullRing::Prime() {
    std::vector<float> silence(static_cast<size_t>(kQuantumFrames) * kChannels, 0.0f);
    for (int q = 0; q < kLeadQuanta; ++q) Push(silence.data(), kQuantumFrames);
}

// Pad mode, consumer side: the previous quantum's tail, cyclic and phase-continuous.
void AudioPullRing::PadFromTail(float* dst, int frames, uint64_t write_pos) {
    const uint64_t tail_start = write_pos - static_cast<uint64_t>(kQuantumFrames);
    for (int i = 0; i < frames; ++i) {
        const uint64_t pos = tail_start + (pad_phase_ % static_cast<uint64_t>(kQuantumFrames));
        const float* src = &samples_[static_cast<size_t>(Slot(pos)) * kChannels];
        dst[static_cast<size_t>(i) * kChannels]     = src[0];
        dst[static_cast<size_t>(i) * kChannels + 1] = src[1];
        ++pad_phase_;
    }
}

// Pad mode, producer side after a push: a late burst that left more than
// kTrimFrames in the ring is cut back to the standing lead -- the OLDEST frames
// go (the late audio the pad already stood in for); latency never grows.
void AudioPullRing::TrimToLead(uint64_t write_pos) {
    uint64_t r = read_.load(std::memory_order_acquire);
    for (;;) {
        const uint64_t content = write_pos - r;
        if (content <= static_cast<uint64_t>(kTrimFrames)) return;
        const uint64_t new_read = write_pos - static_cast<uint64_t>(kLeadFrames);
        if (read_.compare_exchange_weak(r, new_read, std::memory_order_acq_rel, std::memory_order_acquire)) {
            trimmed_.fetch_add(new_read - r, std::memory_order_relaxed);
            return;
        }
    }
}

void AudioPullRing::Push(const float* interleaved, int frames) {
    if (frames <= 0) return;
    if (PadWithTail()) {
        // A pad happened since the last push: rebuild one quantum of lead with
        // the previous quantum (the held buffer the device re-read), then the new.
        if (relatch_.exchange(false, std::memory_order_acq_rel) && last_quantum_frames_ > 0) {
            PushRaw(last_quantum_, last_quantum_frames_);
            relatches_.fetch_add(1, std::memory_order_relaxed);
        }
        const int keep = std::min(frames, kQuantumFrames);
        std::memcpy(last_quantum_, interleaved + static_cast<size_t>(frames - keep) * kChannels,
                    static_cast<size_t>(keep) * kChannels * sizeof(float));
        last_quantum_frames_ = keep;
    }
    PushRaw(interleaved, frames);
    if (PadWithTail()) TrimToLead(write_.load(std::memory_order_relaxed));
}

void AudioPullRing::PushRaw(const float* interleaved, int frames) {
    pushed_.fetch_add(static_cast<uint64_t>(frames), std::memory_order_relaxed);
    // A push wider than the ring keeps only its newest kCapacityFrames; the
    // head that can never be heard is accounted as dropped right away.
    if (frames > kCapacityFrames) {
        const int excess = frames - kCapacityFrames;
        dropped_.fetch_add(static_cast<uint64_t>(excess), std::memory_order_relaxed);
        interleaved += static_cast<size_t>(excess) * kChannels;
        frames = kCapacityFrames;
    }

    const uint64_t w = write_.load(std::memory_order_relaxed);
    uint64_t r = read_.load(std::memory_order_acquire);
    bool overran = false;
    for (;;) {
        const uint64_t used = w - r;
        const uint64_t free_frames = static_cast<uint64_t>(kCapacityFrames) - used;
        if (static_cast<uint64_t>(frames) <= free_frames) break;
        // Discard the oldest audio up to the next quantum boundary of the
        // stream position that frees enough room: whole quanta go, plus the
        // tail of a quantum the consumer had already started.
        const uint64_t need     = static_cast<uint64_t>(frames) - free_frames;
        const uint64_t quantum  = static_cast<uint64_t>(kQuantumFrames);
        const uint64_t new_read = std::min(w, ((r + need + quantum - 1) / quantum) * quantum);
        if (read_.compare_exchange_weak(r, new_read, std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
            dropped_.fetch_add(new_read - r, std::memory_order_relaxed);
            overran = true;
            r = new_read;
            break;
        }
        // The consumer advanced read_ concurrently: recompute with its value.
    }
    if (overran) overruns_.fetch_add(1, std::memory_order_relaxed);

    CopyIn(interleaved, frames, w);
    write_.store(w + static_cast<uint64_t>(frames), std::memory_order_release);
    if (w + static_cast<uint64_t>(frames) >= static_cast<uint64_t>(kQuantumFrames)) tail_valid_.store(true, std::memory_order_release);
}

void AudioPullRing::Pull(float* out, int frames) {
    if (frames <= 0) return;
    pulled_.fetch_add(static_cast<uint64_t>(frames), std::memory_order_relaxed);

    int got = 0;
    for (;;) {
        uint64_t r = read_.load(std::memory_order_acquire);
        const uint64_t w = write_.load(std::memory_order_acquire);
        const uint64_t avail = w - r;
        got = static_cast<int>(std::min<uint64_t>(avail, static_cast<uint64_t>(frames)));
        CopyOut(out, got, r);
        // A concurrent drop-oldest moved read_ past what was just copied: the
        // copy may have been overwritten, so discard it and read again.
        if (read_.compare_exchange_strong(r, r + static_cast<uint64_t>(got),
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
            break;
        }
    }

    if (got < frames) {
        const int missing = frames - got;
        const uint64_t w = write_.load(std::memory_order_acquire);
        if (PadWithTail()) {
            if (tail_valid_.load(std::memory_order_acquire) && w >= static_cast<uint64_t>(kQuantumFrames)) {
                // Rule 4: the previous quantum's tail stands in for the late audio.
                PadFromTail(out + static_cast<size_t>(got) * kChannels, missing, w);
                padded_.fetch_add(static_cast<uint64_t>(missing), std::memory_order_relaxed);
                relatch_.store(true, std::memory_order_release);
                return;
            }
            // No stream yet (or deliberately stopped): silence is not a gap.
            std::memset(out + static_cast<size_t>(got) * kChannels, 0,
                        static_cast<size_t>(missing) * kChannels * sizeof(float));
            return;
        }
        std::memset(out + static_cast<size_t>(got) * kChannels, 0,
                    static_cast<size_t>(missing) * kChannels * sizeof(float));
        underruns_.fetch_add(1, std::memory_order_relaxed);
    }
}

int AudioPullRing::Available() const {
    const uint64_t r = read_.load(std::memory_order_acquire);
    const uint64_t w = write_.load(std::memory_order_acquire);
    return static_cast<int>(w - r);
}

uint64_t AudioPullRing::DiscardStopped() {
    const uint64_t write = write_.load(std::memory_order_acquire);
    const uint64_t read = read_.exchange(write, std::memory_order_acq_rel);
    const uint64_t discarded = write - read;
    tail_valid_.store(false, std::memory_order_release);
    relatch_.store(false, std::memory_order_release);
    last_quantum_frames_ = 0;
    dropped_.fetch_add(discarded, std::memory_order_relaxed);
    return discarded;
}

uint64_t AudioPullRing::pushed() const    { return pushed_.load(std::memory_order_relaxed); }
uint64_t AudioPullRing::pulled() const    { return pulled_.load(std::memory_order_relaxed); }
uint64_t AudioPullRing::dropped() const   { return dropped_.load(std::memory_order_relaxed); }
uint64_t AudioPullRing::underruns() const { return underruns_.load(std::memory_order_relaxed); }
uint64_t AudioPullRing::overruns() const  { return overruns_.load(std::memory_order_relaxed); }
uint64_t AudioPullRing::padded() const    { return padded_.load(std::memory_order_relaxed); }
uint64_t AudioPullRing::relatches() const { return relatches_.load(std::memory_order_relaxed); }
uint64_t AudioPullRing::trimmed() const   { return trimmed_.load(std::memory_order_relaxed); }

}  // namespace ps2r::platform::sdl
