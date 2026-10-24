#pragma once
// ControlsPageBinding -- the in-game Controls page's model (ControlsSection::Model) over the
// launcher's ControlsBindings: the keyboard rows live in bindings.ini BESIDE the settings
// file (ConfigStore::ActiveDir(); the InputMapper's grammar, one file, one writer), a landed
// key press saves the file and reloads the live mapper (PlatformInput::ReloadBindings), so
// the rebound key drives the pad from the next sample on and the next start reads the same
// file (PlatformInput::SettingsFrom looks there first). Main thread (the GUI's and the pad owner's).
// No ImGui here: the page draws, this binds.
#include <string>

#include "app/gui/controls_section.h"
#include "app/host/controls_bindings.h"

class PlatformInput;

namespace ps2r::app {

class ControlsPageBinding {
public:
    // `bindings_dir` = the settings file's folder (ConfigStore::ActiveDir()); `input` may be
    // null (no live pad: the file is still written).
    ControlsPageBinding(std::string bindings_dir, PlatformInput* input);

    // The model SessionRuntime::ConfigureControlsPage takes (callbacks into this object:
    // keep it alive for the session).
    ControlsSection::Model Model();
    const ControlsBindings& Bindings() const { return bindings_; }
    const std::string& Status() const { return status_; }
    uint64_t Saves() const { return saves_; }

private:
    bool OfferKey(const std::string& key_name);
    bool SaveAndApply();

    ControlsBindings bindings_;
    PlatformInput* input_;
    std::string status_;
    uint64_t saves_ = 0;
};

}  // namespace ps2r::app
