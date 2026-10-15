#pragma once
// PhaseClassifier: classifies each guest frame by its OWN render work (draws
// submitted during the frame that just ended) so the BudgetClock charges the
// synthetic render cost that frame class really has on hardware. Full frames
// (gameplay: hundreds of draws) take base_hi; empty/load-class frames (boot
// FMV loop, loading screens: a fullscreen blit + HUD) take base_low -- on
// hardware those frames reach the maintenance pump with almost the whole field
// left, so delivery volume never paces a load.
//
// Rule: enter the low phase only after kConfirmFrames (2) CONSECUTIVE low-draw
// frames (a single stall-glitch frame in gameplay never opens the pump); leave
// it on the first full frame. `draws < lowdraw_max` is the low test (strict).
// The thresholds and bases are title data supplied per call.
#include <cstdint>

class PhaseClassifier {
public:
    static constexpr uint32_t kConfirmFrames = 2;

    // Once per guest frame (anchor change). Returns the effective base.
    uint32_t OnFrame(uint64_t draws_in_frame, uint32_t lowdraw_max, uint32_t base_low, uint32_t base_hi,
                     bool enabled);

    bool low() const { return low_; }
    uint32_t low_streak() const { return low_streak_; }

private:
    uint32_t low_streak_ = 0;
    bool low_ = false;
};
