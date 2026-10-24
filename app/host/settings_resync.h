// app/host/settings_resync.h -- SettingsResync: the in-game menu re-reads the
// settings file when it opens. The launcher's Settings page may have edited
// nativeee.ini while the game ran; the rows the F1 menu shows come from the
// runtime (what is in force), so on every menu open the file's values are
// compared with the runtime's and each difference is submitted through
// SessionControls exactly as the row's widget would -- applied live, and
// written back as a no-op (the file already says so). Pure: the plan is a list
// of (action, value) requests; SessionRuntime submits them.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "app/host/session_controls.h"

namespace ps2r::app {

class SettingsResync {
public:
    using Entry = std::pair<std::string, std::string>;   // (section.key, value) as the file spells it
    using Request = std::pair<SessionControls::Action, int>;

    // The requests that bring `runtime` to what `entries` say. Keys the menu
    // has no row for, values the schema refuses, and values already in force
    // produce nothing. `has_texture_filter` / `has_display_rows` gate the rows
    // the session supports (SessionControls::Capabilities).
    static std::vector<Request> Plan(const std::vector<Entry>& entries, const SessionControls::RuntimeState& runtime,
                                     const SessionControls::Capabilities& capabilities);
};

}  // namespace ps2r::app
