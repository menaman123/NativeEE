#include "engine/vu/tiers/mtvu_clock.h"

#include "engine/core/threads/park.h"

uint64_t MtvuClock::NowNs() const { return ps2r::Park::NowNs(); }

MtvuClock& MtvuClock::Steady() {
    static MtvuClock instance;
    return instance;
}
