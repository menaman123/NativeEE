#pragma once

#include <functional>
#include <string>
#include <vector>

#include "app/host/session_controls.h"

namespace ps2r::app {

// The menu's Controls page: the keyboard rows of the pad (one button per PS2
// control; click a row, press a key; Backspace unbinds, Escape cancels) and the
// keyboard-as-pad toggle (SetKeyboardInput). The page draws widgets only: the
// binding model (ControlsBindings over <user data>/bindings.ini, the live
// InputMapper reload) is the composition's, reached through `Model` -- a set
// of callbacks so the GUI library needs no host or SDL device code. Every
// callback runs on the main thread, inside the GUI's lock. SessionGui offers
// the pressed key's name to `offer_key` while a row is armed.
class ControlsSection {
public:
    struct Row {
        std::string name;    // PS2 button (cross, square, ...)
        std::string value;   // the bound key's SDL name; "" = unbound
    };
    struct Model {
        std::function<std::vector<Row>()> rows;
        // Arm row `index` for the next key (arming the armed row disarms it).
        std::function<void(int index)> arm;
        std::function<int()> armed_row;   // -1 = none
        // The pressed key's SDL scancode name while a row is armed: binds /
        // unbinds / cancels and persists; true when the press was consumed.
        std::function<bool(const std::string& key_name)> offer_key;
        // One line of state for the page (the bindings file, the last error).
        std::function<std::string()> status;
    };

    void SetModel(Model model) { model_ = std::move(model); }
    bool HasModel() const { return static_cast<bool>(model_.rows); }
    bool Armed() const { return model_.armed_row && model_.armed_row() >= 0; }
    bool OfferKey(const std::string& key_name) { return model_.offer_key && model_.offer_key(key_name); }
    void Draw(SessionControls& controls, const SessionControls::Snapshot& snapshot);

private:
    Model model_;
};

}  // namespace ps2r::app
