#pragma once

#include "app/host/session_controls.h"

namespace ps2r::app {

// The menu's Display page: the presenter's rows (display mode, aspect, vsync,
// render rate, the performance overlay), the game's internal scale and the
// texture filtering enhancement (mode + anisotropy), each through the same
// SessionControls command the root's presenter applies and persists
// (SetDisplayMode / SetAspect / SetVSync / SetRenderRate / SetPerfOverlay /
// SetScale / SetTextureFilter / SetAnisotropy). Widgets only: the live values
// come from the snapshot the presenter publishes; nothing here touches the
// window or the GL device. A row whose capability is absent is not offered.
class DisplaySection {
public:
    void Draw(SessionControls& controls, const SessionControls::Snapshot& snapshot);

private:
    // One combo row: submits `action` with the chosen index when it changes.
    static void Combo(SessionControls& controls, const SessionControls::Snapshot& snapshot,
                      SessionControls::Action action, const char* label, const char* const* names, int count,
                      int current, const int* values = nullptr);
};

}  // namespace ps2r::app
