// engine/iop/iop_clock.cpp -- IopClock: frame-derived virtual time or steady_clock.
#include "engine/iop/iop_clock.h"

namespace ps2r::iop {

uint64_t IopClock::NowUs() const {
    if (policy_ == Policy::Realtime) return SteadyNowUs();
    return virtual_us_;
}

void IopClock::OnFrameBoundary() {
    if (policy_ != Policy::Frame) return;
    frames_ += 1;
    AdvanceToUs(frames_ * kFrameUs);
}

void IopClock::AdvanceToUs(uint64_t t) {
    if (policy_ != Policy::Frame) return;
    if (t <= virtual_us_) return;
    virtual_us_ = t;
    // An overshoot the NEXT boundary absorbs (its base <= the time, its ceiling beyond it: that pump runs
    // the remainder of its frame) is not a skip. Beyond that the base would stay behind and the pumps
    // that follow would be no-ops (dark boundaries) until the count caught up: move the base to the frame
    // BEFORE the one containing the time, so the next boundary lands in that frame and runs its rest.
    const uint64_t next_ceiling = (frames_ + 2) * kFrameUs;
    if (virtual_us_ <= next_ceiling) return;
    const uint64_t base = (virtual_us_ - 1) / kFrameUs - 1;
    skips_++;
    skipped_frames_ += base - frames_;
    const uint64_t skip_us = virtual_us_ - next_ceiling;
    if (skip_us > max_skip_us_) max_skip_us_ = skip_us;
    if (follow_skips_) frames_ = base;
}

void IopClock::RebaseToFrame(uint64_t frames) {
    if (policy_ != Policy::Frame) return;
    frames_ = frames;
    AdvanceToUs(frames_ * kFrameUs);
}

uint64_t IopClock::FrameCeilingUs() const { return (frames_ + 1) * kFrameUs; }

uint64_t IopClock::OffsetInFrameUs() const {
    const uint64_t base = frames_ * kFrameUs;
    return virtual_us_ > base ? virtual_us_ - base : 0;
}

std::chrono::steady_clock::time_point IopClock::DueToSteady(uint64_t due_us) const {
    return start_ + std::chrono::microseconds(due_us);
}

uint64_t IopClock::SteadyNowUs() const {
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - start_)
        .count();
}

}  // namespace ps2r::iop
