#pragma once

#include "app/host/session_controls.h"

namespace ps2r::app {

// The menu's sound panel: output volume and a mute toggle through the same
// SetVolume command the hotkeys/root use, plus the device state the root
// publishes. No audio device call happens here; the root applies the gain on
// its owner. Mute remembers the last non-zero level for unmute.
class SoundSection {
public:
    void Draw(SessionControls& controls, const SessionControls::Snapshot& snapshot);

private:
    int volume_ = 100;
    bool volume_editing_ = false;
    int unmute_level_ = 100;
};

}  // namespace ps2r::app
