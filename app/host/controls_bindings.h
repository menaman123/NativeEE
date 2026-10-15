#pragma once
// ControlsBindings -- the launcher Controls page's model (CHARTER 2 A5): the
// keyboard rows ([buttons]), the gamepad-button rows ([pad], the remap), the
// gamepad enable / deadzone / sensitivity ([gamepad]) and the press-to-bind
// state machine, written as <user data>/bindings.ini in the InputMapper's
// grammar. No ImGui, no SDL device: the launcher decodes its SDL events into
// the key name / control name it offers here, and a test offers synthetic
// ones. One file, one writer; the InputMapper reads it at game start.
#include <string>
#include <vector>

namespace ps2r::app {

class ControlsBindings {
public:
    struct Row {
        const char* name;    // PS2 button: cross, square, ... (the INI key)
        std::string value;   // keyboard: SDL key name / Mouse1..3 / "" (unbound); pad: SDL control name / "none"
    };
    enum class Target { None, Key, Pad };

    explicit ControlsBindings(std::string user_data_root);

    std::string Path() const { return user_data_root_ + "/bindings.ini"; }
    // Reads Path() when present (unknown lines are ignored); the defaults
    // stay otherwise. Returns true when a file was read.
    bool Load();
    // The INI text (InputMapper grammar) and its write; `err` names a failure.
    std::string Text() const;
    bool Save(std::string* err);

    const std::vector<Row>& Keys() const { return keys_; }
    const std::vector<Row>& Pad() const { return pad_; }
    bool GamepadEnabled() const { return gamepad_enabled_; }
    void SetGamepadEnabled(bool on) { gamepad_enabled_ = on; }
    float& Deadzone() { return deadzone_; }
    float& Sensitivity() { return sensitivity_; }

    // Press-to-bind: Arm one row of one table (arming the armed row disarms it),
    // then the next offered key / control lands in it and Save() runs.
    void Arm(Target target, int row);
    void Disarm();
    Target Armed() const { return armed_; }
    int ArmedRow() const { return armed_row_; }
    // A key name while a KEY row is armed: Escape cancels, Backspace unbinds,
    // anything else binds. Returns true when the arm was consumed.
    bool OfferKey(const std::string& sdl_key_name);
    // A pad control name ("a", "lefttrigger", ...) while a PAD row is armed;
    // "" is ignored (not a bindable edge). Returns true when consumed.
    bool OfferPadControl(const std::string& control_name);
    // The last Save()'s outcome for the page's log ("" = fine).
    const std::string& LastError() const { return last_error_; }

    // SDL event -> control name, SDL-free ints: a controller BUTTON event
    // (`is_button`, `index` = SDL_CONTROLLER_BUTTON_*) names that button; a
    // trigger AXIS event (index = SDL_CONTROLLER_AXIS_TRIGGERLEFT/RIGHT) past
    // 25 % travel names "lefttrigger" / "righttrigger"; anything else "".
    static std::string PadControlFromEvent(bool is_button, int index, int axis_value);

private:
    std::string user_data_root_;
    std::vector<Row> keys_;
    std::vector<Row> pad_;
    float deadzone_ = 0.15f, sensitivity_ = 1.33f;
    bool gamepad_enabled_ = true;
    Target armed_ = Target::None;
    int armed_row_ = -1;
    std::string last_error_;
};

}  // namespace ps2r::app
