#include "engine/ee/timing/warp.h"

void Warp::SetTarget(uint32_t target_frame) {
    target_ = target_frame;
    active_ = target_frame != 0;
}

bool Warp::OnFrame(uint64_t frame) {
    if (target_ == 0 || !active_) return false;
    if (frame >= target_) {
        active_ = false;
        return true;
    }
    return false;
}

void Warp::Apply(TickSource& source, TickSource::Mode fallback, uint32_t fallback_k) const {
    if (active_) source.SetMode(TickSource::Mode::Warp);
    else source.SetMode(fallback, fallback_k);
}
