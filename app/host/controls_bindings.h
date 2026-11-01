#pragma once
// ControlsBindings -- the launcher Controls page's model (CHARTER 2 A5): the
// keyboard rows ([buttons] + the [sticks] key_left_* / key_right_* keys), the
// gamepad-button rows ([pad], the remap), the two gamepad STICK rows ([sticks]
// pad_camera: which controller stick is the camera, the other one moves), the
// gamepad enable / deadzone / sensitivity ([gamepad]) and the press-to-bind
// state machine, written as bindings.ini BESIDE the settings file (the folder
// ConfigStore resolved) in the InputMapper's grammar. No ImGui, no SDL device:
// the launcher decodes its SDL events into the key name / control name / stick
// name it offers here, and a test offers synthetic ones. One file, one writer
// (an atomic replace); the InputMapper reads it at game start when it exists
// (PlatformInput::SettingsFrom), else the shipped default_bindings.ini.
#include <string>
#include <vector>

namespace ps2r::app {

class ControlsBindings {
public:
    struct Row {
        const char* name;    // the INI key: a PS2 button (cross, square, ...), a stick key (key_right_up, ...) or a stick role (move, camera)
        std::string value;   // keyboard: SDL key name / Mouse1..3 / "" (unbound); pad: SDL control name / "none"; stick: left / right
        const char* label;   // what the page shows ("Cross", "Camera up (right stick)", "Camera")
    };
    enum class Target { None, Key, Pad, Stick };

    // `dir` = the settings file's folder (ConfigStore::ActiveDir()).
    explicit ControlsBindings(std::string dir);

    static constexpr const char* kFileName = "bindings.ini";
    std::string Path() const { return dir_.empty() ? std::string(kFileName) : dir_ + "/" + kFileName; }
    // Reads Path() when present (unknown lines are ignored): the file replaces
    // the keyboard rows (a key the file lacks is unbound, the InputMapper's own
    // reading of it); [pad] rows and the stick roles it lacks keep their
    // defaults. Without a file the defaults stay. Returns true when a file was read.
    bool Load();
    // The INI text (InputMapper grammar) and its write; `err` names a failure.
    std::string Text() const;
    bool Save(std::string* err);

    // The keyboard table: the 16 buttons, then the move keys (key_left_*, W/S/A/D) and the camera keys (key_right_*, I/K/J/L).
    const std::vector<Row>& Keys() const { return keys_; }
    // The gamepad-button table (16 rows).
    const std::vector<Row>& Pad() const { return pad_; }
    // The gamepad-stick table: row 0 "move", row 1 "camera"; values "left" / "right" (the controller's sticks).
    // The default is the DualShock 2 layout: move = left, camera = right. Binding one stick to a role gives the other
    // role the other stick (two sticks, two roles); the INI carries it as [sticks] pad_camera = right|left.
    const std::vector<Row>& Sticks() const { return sticks_; }
    bool CameraOnLeftStick() const { return sticks_[1].value == "left"; }
    // The mouse's stick ([sticks] mouse = right|left; the default right = the camera). Read and written back, not bindable here.
    bool MouseIsCamera() const { return mouse_right_; }
    bool GamepadEnabled() const { return gamepad_enabled_; }
    void SetGamepadEnabled(bool on) { gamepad_enabled_ = on; }
    float& Deadzone() { return deadzone_; }
    float& Sensitivity() { return sensitivity_; }

    // Press-to-bind: Arm one row of one table (arming the armed row disarms it),
    // then the next offered key / control / stick lands in it and Save() runs.
    void Arm(Target target, int row);
    void Disarm();
    Target Armed() const { return armed_; }
    int ArmedRow() const { return armed_row_; }
    // A key name while a KEY row is armed: Escape cancels, Backspace unbinds,
    // anything else binds (and unbinds the row that had the same key: one key,
    // one PS2 control -- a button or a stick direction). Returns true when the arm was consumed.
    bool OfferKey(const std::string& sdl_key_name);
    // A pad control name ("a", "lefttrigger", ...) while a PAD row is armed;
    // "" is ignored (not a bindable edge). Returns true when consumed.
    bool OfferPadControl(const std::string& control_name);
    // A controller stick name ("left" / "right") while a STICK row is armed; "" is
    // ignored. The other role takes the other stick. Returns true when consumed.
    bool OfferPadStick(const std::string& stick_name);
    // The last Save()'s outcome for the page's log ("" = fine).
    const std::string& LastError() const { return last_error_; }

    // SDL event -> control name, SDL-free ints: a controller BUTTON event
    // (`is_button`, `index` = SDL_CONTROLLER_BUTTON_*) names that button; a
    // trigger AXIS event (index = SDL_CONTROLLER_AXIS_TRIGGERLEFT/RIGHT) past
    // 25 % travel names "lefttrigger" / "righttrigger"; anything else "".
    static std::string PadControlFromEvent(bool is_button, int index, int axis_value);
    // SDL axis event -> stick name: a LEFTX/LEFTY axis past 50 % travel either way
    // names "left", RIGHTX/RIGHTY "right"; a trigger or a smaller deflection "".
    static std::string PadStickFromEvent(int axis_index, int axis_value);

private:
    std::string dir_;
    std::vector<Row> keys_;
    std::vector<Row> pad_;
    std::vector<Row> sticks_;
    bool mouse_right_ = true;
    float mouse_sensitivity_ = 0.35f, mouse_decay_ = 12.0f;
    float deadzone_ = 0.15f, sensitivity_ = 1.33f;
    bool gamepad_enabled_ = true;
    Target armed_ = Target::None;
    int armed_row_ = -1;
    std::string last_error_;
};

}  // namespace ps2r::app
