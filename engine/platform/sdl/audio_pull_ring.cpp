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
    const int lead = LeadQuanta();
    for (int q = 0; q < lead; ++q) Push(silence.data(), kQuantumFrames);
}

// Pad mode, consumer side: the previous quantum, cyclic, phase-continuous
// across the pulls of ONE pad event. A new pad event starts at the quantum's
// first frame -- the stream stopped at its last, so the seam is one loop-back
// of 800 frames, de-clicked by a kCrossFadeFrames linear cross-fade from the
// quantum's last frames (what the device just played) into its first. The
// frames handed out are counted in pad_phase_: the producer's re-latch pushes
// only the quantum's UNPLAYED remainder, so the stream resumes where the pad
// left it (one seam per pad event, not two). Past kPadHoldFrames the repeat
// fades to silence.
void AudioPullRing::PadFromTail(float* dst, int frames, uint64_t write_pos) {
    const uint64_t tail_start = write_pos - static_cast<uint64_t>(kQuantumFrames);
    if (!in_pad_) { in_pad_ = true; pad_phase_ = 0; }
    for (int i = 0; i < frames; ++i) {
        if (pad_phase_ >= static_cast<uint64_t>(kPadHoldFrames)) {
            pad_faded_ = true;
            const uint64_t into = pad_phase_ - static_cast<uint64_t>(kPadHoldFrames);
            if (into >= static_cast<uint64_t>(kPadFadeFrames)) {
                dst[static_cast<size_t>(i) * kChannels] = 0.0f;
                dst[static_cast<size_t>(i) * kChannels + 1] = 0.0f;
                ++pad_phase_;
                continue;
            }
        }
        const uint64_t phase = pad_phase_ % static_cast<uint64_t>(kQuantumFrames);
        const float* src = &samples_[static_cast<size_t>(Slot(tail_start + phase)) * kChannels];
        float l = src[0], r = src[1];
        if (pad_phase_ < static_cast<uint64_t>(kCrossFadeFrames)) {
            // The seam: the frames the device just played fade out under the quantum's first frames.
            const float t = static_cast<float>(pad_phase_ + 1) / static_cast<float>(kCrossFadeFrames + 1);
            const float* out_src = &samples_[static_cast<size_t>(Slot(write_pos - static_cast<uint64_t>(kCrossFadeFrames) + pad_phase_)) * kChannels];
            l = out_src[0] * (1.0f - t) + l * t;
            r = out_src[1] * (1.0f - t) + r * t;
        }
        if (pad_faded_) {
            const float g = 1.0f - static_cast<float>(pad_phase_ - static_cast<uint64_t>(kPadHoldFrames)) / static_cast<float>(kPadFadeFrames);
            l *= g;
            r *= g;
        }
        dst[static_cast<size_t>(i) * kChannels]     = l;
        dst[static_cast<size_t>(i) * kChannels + 1] = r;
        ++pad_phase_;
    }
    pad_phase_published_.store(static_cast<uint32_t>(pad_phase_ % static_cast<uint64_t>(kQuantumFrames)), std::memory_order_release);
}

// Pad mode, producer side after a push: a late burst that left more than the
// trim bound in the ring is cut back to the standing lead -- the OLDEST frames
// go (the late audio the pad already stood in for); latency never grows past
// the lead.
void AudioPullRing::TrimToLead(uint64_t write_pos) {
    const uint64_t lead_frames = static_cast<uint64_t>(LeadQuanta()) * kQuantumFrames;
    const uint64_t trim_frames = lead_frames + static_cast<uint64_t>(kTrimSlackQuanta) * kQuantumFrames;
    uint64_t r = read_.load(std::memory_order_acquire);
    for (;;) {
        const uint64_t content = write_pos - r;
        if (content <= trim_frames) return;
        const uint64_t new_read = write_pos - lead_frames;
        if (read_.compare_exchange_weak(r, new_read, std::memory_order_acq_rel, std::memory_order_acquire)) {
            trimmed_.fetch_add(new_read - r, std::memory_order_relaxed);
            return;
        }
    }
}

void AudioPullRing::Push(const float* interleaved, int frames) {
    if (frames <= 0) return;
    if (PadWithTail()) {
        // A pad happened since the last push: rebuild the lead with the UNPLAYED
        // remainder of the previous quantum (the pad handed out its first
        // `phase` frames; the stream resumes at frame `phase` without a seam),
        // then the new audio.
        if (relatch_.exchange(false, std::memory_order_acq_rel) && last_quantum_frames_ > 0) {
            const int phase = std::min(static_cast<int>(pad_phase_published_.load(std::memory_order_acquire)), last_quantum_frames_);
            if (phase < last_quantum_frames_)
                PushRaw(last_quantum_ + static_cast<size_t>(phase) * kChannels, last_quantum_frames_ - phase);
            relatches_.fetch_add(1, std::memory_order_relaxed);
            // The lead was shorter than the producer's cadence: keep the re-pushed
            // quantum as standing lead from now on.
            const int lead = LeadQuanta();
            if (lead < kMaxLeadQuanta) lead_quanta_.store(lead + 1, std::memory_order_relaxed);
            pushes_since_pad_ = 0;
        } else if (++pushes_since_pad_ >= kDecayPushes) {
            pushes_since_pad_ = 0;
            const int lead = LeadQuanta();
            if (lead > kLeadQuanta) lead_quanta_.store(lead - 1, std::memory_order_relaxed);
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
    PullInto(out, frames);
    if (tap_fn_) tap_fn_(tap_ctx_, out, frames);
}

void AudioPullRing::PullInto(float* out, int frames) {
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

    if (got > 0 && pad_faded_) {
        pad_faded_ = false;   // audio after a faded pad ramps up from silence
        fade_in_ = 0;
    }
    for (int i = 0; i < got && fade_in_ < kCrossFadeFrames; ++i, ++fade_in_) {
        const float g = static_cast<float>(fade_in_ + 1) / static_cast<float>(kCrossFadeFrames + 1);
        out[static_cast<size_t>(i) * kChannels] *= g;
        out[static_cast<size_t>(i) * kChannels + 1] *= g;
    }
    if (got == frames) {
        in_pad_ = false;   // the pad event (if any) ended: the next one restarts at the quantum's first frame
        return;
    }
    {
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

void AudioPullRing::SetPullTap(PullTap fn, void* ctx) {
    tap_fn_ = fn;
    tap_ctx_ = ctx;
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
    pad_phase_published_.store(0, std::memory_order_release);
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
