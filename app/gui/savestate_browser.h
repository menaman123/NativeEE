#pragma once

#include <string>

#include "app/gui/savestate_rows.h"
#include "app/host/session_controls.h"

namespace ps2r::app {

// The menu's reproduction-file browser: lists the configured reproduction
// folder and selects a row as the restore path (the same selection F8 and the
// Restore button use). Scans on first open, after a completed save and on
// Refresh; it performs no restore, deletion or game work itself.
class SavestateBrowser {
public:
    // Call with the GUI context current. `directory` is the root's configured
    // reproduction folder (empty = none). Returns true when a row was selected.
    bool Draw(SessionControls& controls, const SessionControls::Snapshot& snapshot, const std::string& directory);
    // Forget the scan so the next Draw rescans (menu reopened, save completed).
    void Invalidate() { rows_ = SavestateRows(); }
    const SavestateRows& Rows() const { return rows_; }

private:
    void Rescan(const std::string& directory);

    SavestateRows rows_;
    std::string error_;
    std::string last_restore_path_;
};

}  // namespace ps2r::app
