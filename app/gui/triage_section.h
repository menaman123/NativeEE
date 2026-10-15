#pragma once

#include <string>

#include "app/host/session_controls.h"

namespace ps2r::app {

// The menu's triage panel: the session's visible facts in one place (tick,
// pause, presenter readers, input source, recording file, reproduction
// selection, saves folder) and the full pending/recent command history, with
// a one-click copy of a plain-text report to the clipboard for bug reports.
// Reads the published snapshot only; no file, game or device access.
class TriageSection {
public:
    void Draw(const SessionControls::Snapshot& snapshot);
    // The text the Copy button places on the clipboard (pure; unit-testable).
    static std::string Report(const SessionControls::Snapshot& snapshot);

private:
    bool show_history_ = false;
};

}  // namespace ps2r::app
