#include "app/host/controls_page_binding.h"

#include <cstdio>

#include "app/host/platform_input.h"
#include "engine/core/config/settings_file.h"
#include "engine/core/log/log.h"

namespace ps2r::app {

ControlsPageBinding::ControlsPageBinding(std::string user_data_root, std::string settings_file, PlatformInput* input)
    : bindings_(std::move(user_data_root)), settings_file_(std::move(settings_file)), input_(input) {
    bindings_.Load();   // the launcher's / a previous session's file when present; the defaults otherwise
    status_ = "Bindings file: " + bindings_.Path();
}

ControlsSection::Model ControlsPageBinding::Model() {
    ControlsSection::Model m;
    m.rows = [this] {
        std::vector<ControlsSection::Row> rows;
        for (const ControlsBindings::Row& r : bindings_.Keys()) rows.push_back({r.name, r.value});
        return rows;
    };
    m.arm = [this](int index) { bindings_.Arm(ControlsBindings::Target::Key, index); };
    m.armed_row = [this] { return bindings_.Armed() == ControlsBindings::Target::Key ? bindings_.ArmedRow() : -1; };
    m.offer_key = [this](const std::string& key_name) { return OfferKey(key_name); };
    m.status = [this] { return status_; };
    return m;
}

bool ControlsPageBinding::OfferKey(const std::string& key_name) {
    // A consumed offer that changed nothing (Escape's cancel, the same key again) is not a save.
    const std::string before = bindings_.Text();
    if (!bindings_.OfferKey(key_name)) return false;
    if (bindings_.Text() != before) SaveAndApply();
    return true;
}

// Save bindings.ini, point the settings file at it, reload the live mapper. Every failure
// is loud (stderr + the HOST log + the page's status line), never silent.
bool ControlsPageBinding::SaveAndApply() {
    std::string err;
    if (!bindings_.Save(&err)) {
        status_ = "Bindings not saved: " + err;
        std::fprintf(stderr, "nativeee: controls: %s\n", status_.c_str());
        return false;
    }
    ++saves_;
    std::string persist_err;
    const bool persisted = SettingsFile::Set(settings_file_, "input.bindings_file", bindings_.Path(), &persist_err);
    if (!persisted) std::fprintf(stderr, "nativeee: controls: input.bindings_file not saved to %s: %s\n",
                                 settings_file_.c_str(), persist_err.c_str());
    const bool live = input_ ? input_->ReloadBindings(bindings_.Path()) : false;
    status_ = "Saved " + bindings_.Path() + (persisted ? "" : " (settings file not updated)") +
              (input_ ? (live ? "; applied to the pad" : "; NOT applied: the mapper rejected the file") : "");
    PS2R_LOG(HOST, Info, "controls: %s", status_.c_str());
    return live || !input_;
}

}  // namespace ps2r::app
