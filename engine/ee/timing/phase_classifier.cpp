#include "engine/ee/timing/phase_classifier.h"

uint32_t PhaseClassifier::OnFrame(uint64_t draws_in_frame, uint32_t lowdraw_max, uint32_t base_low,
                                  uint32_t base_hi, bool enabled) {
    if (draws_in_frame < lowdraw_max) {
        if (low_streak_ < 0xFFFFu) ++low_streak_;
    } else {
        low_streak_ = 0;
    }
    low_ = enabled && low_streak_ >= kConfirmFrames;
    return low_ ? base_low : base_hi;
}
