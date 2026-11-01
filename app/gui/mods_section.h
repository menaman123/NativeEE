#pragma once

#include "app/host/session_controls.h"

namespace ps2r::app {

// The menu's 'Mods (for testing)' section (mods.enabled): the level select --
// the title's own level table grouped by episode, the level the game is in
// highlighted, a Go button per row (ModsWarp) -- and the Skip cutscene button
// (ModsSkipCutscene; the same request Backspace and the controller's
// Select + Start submit). Widgets only: the rows come from the snapshot the
// game owner publishes (RuntimeState::mods_*); the title does the loading.
// Drawn only when the capability is offered (the knob on, a play session).
class ModsSection {
public:
    void Draw(SessionControls& controls, const SessionControls::Snapshot& snapshot);
};

}  // namespace ps2r::app
