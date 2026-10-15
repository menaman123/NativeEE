#include "engine/ee/timing/tick_source.h"

uint32_t TickSource::PeriodUsForK(uint32_t k) {
    if (k < kTurboKMin) return kBasePeriodUs;
    if (k > kTurboKMax) k = kTurboKMax;
    return kBasePeriodUs / k;
}

void TickSource::SetMode(Mode m, uint32_t k) {
    mode_ = m;
    if (m == Mode::Turbo) {
        if (k < kTurboKMin) k = kTurboKMin;
        if (k > kTurboKMax) k = kTurboKMax;
        k_ = k;
    } else {
        k_ = (m == Mode::Freerun) ? 0 : 1;
    }
}

uint64_t TickSource::Tick() { return ++raw_count_; }

uint32_t TickSource::PeriodUs() const {
    switch (mode_) {
        case Mode::Realtime: return kBasePeriodUs;
        case Mode::Turbo: return PeriodUsForK(k_);
        case Mode::Freerun:
        case Mode::Warp: return 0;
    }
    return kBasePeriodUs;
}
