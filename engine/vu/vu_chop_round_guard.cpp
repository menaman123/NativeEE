// VuChopRoundGuard -- see the header for the rule.
#include "engine/vu/vu_chop_round_guard.h"

#include <cfenv>

VuChopRoundGuard::VuChopRoundGuard() : prev_(std::fegetround()) {
    if (prev_ != FE_TOWARDZERO) std::fesetround(FE_TOWARDZERO);
}

VuChopRoundGuard::~VuChopRoundGuard() {
    if (prev_ != FE_TOWARDZERO) std::fesetround(prev_);
}
