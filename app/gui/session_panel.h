#pragma once

#include <array>

#include "app/gui/controls_section.h"
#include "app/gui/display_section.h"
#include "app/gui/savestate_browser.h"
#include "app/gui/sound_section.h"
#include "app/gui/triage_section.h"
#include "app/host/session_controls.h"

namespace ps2r::app {

// Builds widgets only. Call with the GUI context locked on its UI owner;
// actions are queued through SessionControls and perform no game or file IO.
class SessionPanel {
public:
    bool WantsFrame(const SessionControls::Snapshot& snapshot, double now_seconds);
    void Draw(SessionControls& controls, const SessionControls::Snapshot& snapshot);
    // The Controls page's binding model (the composition's; main thread).
    ControlsSection& Controls() { return controls_page_; }
    // The pause menu's Resume: close the menu and release the pause it holds.
    static void Resume(SessionControls& controls, const SessionControls::Snapshot& snapshot);

private:
    void DrawSession(SessionControls& controls, const SessionControls::Snapshot& snapshot);
    void DrawSettings(SessionControls& controls, const SessionControls::Snapshot& snapshot);
    void DrawReproduction(SessionControls& controls, const SessionControls::Snapshot& snapshot);
    void DrawRecording(SessionControls& controls, const SessionControls::Snapshot& snapshot);
    void DrawResults(const SessionControls::Snapshot& snapshot);
    void DrawNotice(const SessionControls::Snapshot& snapshot);
    void DrawPerfOverlay(const SessionControls::Snapshot& snapshot);

    std::array<char, SessionControls::kTextLimit + 1> path_{};
    bool path_editing_ = false;
    SavestateBrowser browser_;
    SoundSection sound_;
    DisplaySection display_;
    ControlsSection controls_page_;
    TriageSection triage_;
    uint64_t notice_id_ = 0;
    SessionControls::Phase notice_phase_ = SessionControls::Phase::Queued;
    double notice_until_ = 0.0;
};

}  // namespace ps2r::app
