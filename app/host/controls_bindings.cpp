#include "app/host/controls_bindings.h"

#include <SDL.h>   // SDL_CONTROLLER_* constants only

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "engine/platform/sdl/input/pad_button_map.h"

namespace ps2r::app {
namespace {

// The INI key order of the two button tables (the wire bit order is irrelevant here;
// this is the order the page shows).
const char* kButtonOrder[] = {"cross", "square", "circle", "triangle", "l1", "l2", "r1", "r2", "l3", "r3",
                              "start", "select", "up", "down", "left", "right"};
const char* kButtonLabel[] = {"Cross", "Square", "Circle", "Triangle", "L1", "L2", "R1", "R2", "L3", "R3 (Binocucom)",
                              "Start", "Select", "D-pad up", "D-pad down", "D-pad left", "D-pad right"};
const char* kDefaultKeys[] = {"Space", "LShift", "Mouse2", "E", "1", "2", "3", "4", "5", "6", "Y", "Tab", "Up", "Down", "Left", "Right"};
// SDL's standard layout == the DualShock 2 layout (PadButtonMap's default).
const char* kDefaultPad[] = {"a", "x", "b", "y", "leftshoulder", "lefttrigger", "rightshoulder", "righttrigger",
                             "leftstick", "rightstick", "start", "back", "dpup", "dpdown", "dpleft", "dpright"};
constexpr int kRows = 16;
// The stick keys ([sticks] key_left_* = the left stick = move, key_right_* = the right stick = the camera in this
// title's play), after the buttons in the keyboard table. Defaults = default_bindings.ini.
const char* kStickKeyOrder[] = {"key_left_up", "key_left_down", "key_left_left", "key_left_right",
                                "key_right_up", "key_right_down", "key_right_left", "key_right_right"};
const char* kStickKeyLabel[] = {"Move up (left stick)", "Move down (left stick)", "Move left (left stick)", "Move right (left stick)",
                                "Camera up (right stick)", "Camera down (right stick)", "Camera left (right stick)", "Camera right (right stick)"};
const char* kDefaultStickKeys[] = {"W", "S", "A", "D", "I", "K", "J", "L"};
constexpr int kStickKeyRows = 8;

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}

}  // namespace

ControlsBindings::ControlsBindings(std::string dir) : dir_(std::move(dir)) {
    for (int i = 0; i < kRows; i++) {
        keys_.push_back({kButtonOrder[i], kDefaultKeys[i], kButtonLabel[i]});
        pad_.push_back({kButtonOrder[i], kDefaultPad[i], kButtonLabel[i]});
    }
    for (int i = 0; i < kStickKeyRows; i++) keys_.push_back({kStickKeyOrder[i], kDefaultStickKeys[i], kStickKeyLabel[i]});
    sticks_.push_back({"move", "left", "Move (stick)"});
    sticks_.push_back({"camera", "right", "Camera (stick)"});
}

bool ControlsBindings::Load() {
    std::ifstream in(Path());
    if (!in.is_open()) return false;
    // The file replaces the keyboard tables, as it does in the InputMapper (a key row the file lacks is
    // unbound, which is how Text() spells an unbound key); the [pad] rows and the stick roles keep their
    // defaults when absent, as the mapper's do.
    for (Row& r : keys_) r.value.clear();
    std::string line, section;
    while (std::getline(in, line)) {
        const size_t c = line.find_first_of(";#");
        if (c != std::string::npos) line = line.substr(0, c);
        line = Trim(line);
        if (line.empty()) continue;
        if (line.front() == '[') { section = line.substr(1, line.find(']') - 1); continue; }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Trim(line.substr(0, eq)), value = Trim(line.substr(eq + 1));
        if (section == "buttons") {
            for (Row& r : keys_) if (key == r.name) r.value = value;
        } else if (section == "sticks") {
            // The stick keys are keyboard rows too (one key, one PS2 control, across both tables of keys).
            for (Row& r : keys_) if (key == r.name) r.value = value;
            if (key == "pad_camera") {
                const bool left = value == "left";
                sticks_[1].value = left ? "left" : "right";
                sticks_[0].value = left ? "right" : "left";
            } else if (key == "mouse") {
                mouse_right_ = value != "left";
            } else if (key == "mouse_sensitivity") {
                mouse_sensitivity_ = std::stof(value);
            } else if (key == "mouse_decay") {
                mouse_decay_ = std::stof(value);
            }
        } else if (section == "pad") {
            for (Row& r : pad_) if (key == r.name) r.value = value;
        } else if (section == "gamepad") {
            if (key == "deadzone") deadzone_ = std::stof(value);
            else if (key == "sensitivity") sensitivity_ = std::stof(value);
            else if (key == "enabled") gamepad_enabled_ = value != "0";
        }
    }
    return true;
}

std::string ControlsBindings::Text() const {
    std::ostringstream o;
    o << "; bindings.ini -- your controls, written by the Controls page (kept beside the settings file).\n"
         "[sticks]\nmouse = " << (mouse_right_ ? "right" : "left") << "\npad_camera = " << sticks_[1].value << "\n";
    for (size_t i = kRows; i < keys_.size(); i++)   // the stick keys live in [sticks]
        if (!keys_[i].value.empty()) o << keys_[i].name << " = " << keys_[i].value << "\n";
    o << "mouse_sensitivity = " << mouse_sensitivity_ << "\nmouse_decay = " << mouse_decay_ << "\n\n[buttons]\n";
    for (size_t i = 0; i < keys_.size() && i < static_cast<size_t>(kRows); i++)
        if (!keys_[i].value.empty()) o << keys_[i].name << " = " << keys_[i].value << "\n";
    o << "\n[gamepad]\nenabled = " << (gamepad_enabled_ ? 1 : 0) << "\ndeadzone = " << deadzone_ << "\nsensitivity = " << sensitivity_
      << "\n\n[pad]\n";
    for (const Row& r : pad_) o << r.name << " = " << (r.value.empty() ? "none" : r.value) << "\n";
    return o.str();
}

bool ControlsBindings::Save(std::string* err) {
    // Whole file through a temporary + rename: a crash never leaves torn bindings.
    const std::string tmp = Path() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << Text();
        if (!out) {
            last_error_ = "could not write " + tmp;
            if (err) *err = last_error_;
            std::remove(tmp.c_str());
            return false;
        }
    }
    std::error_code ec;   // std::filesystem::rename replaces an existing file on Windows too; std::rename does not
    std::filesystem::rename(tmp, Path(), ec);
    if (ec) {
        last_error_ = "could not replace " + Path();
        if (err) *err = last_error_;
        std::remove(tmp.c_str());
        return false;
    }
    last_error_.clear();
    return true;
}

void ControlsBindings::Disarm() {
    armed_ = Target::None;
    armed_row_ = -1;
}

void ControlsBindings::Arm(Target target, int row) {
    const std::vector<Row>& table = target == Target::Pad ? pad_ : target == Target::Stick ? sticks_ : keys_;
    if (target == Target::None || row < 0 || row >= static_cast<int>(table.size()) || (armed_ == target && armed_row_ == row)) {
        Disarm();
        return;
    }
    armed_ = target;
    armed_row_ = row;
}

bool ControlsBindings::OfferKey(const std::string& sdl_key_name) {
    if (armed_ != Target::Key || armed_row_ < 0 || armed_row_ >= static_cast<int>(keys_.size())) return false;
    if (sdl_key_name == "Escape") { Disarm(); return true; }
    if (sdl_key_name == "Backspace") {
        keys_[armed_row_].value.clear();
    } else {
        // The same key may drive only one PS2 button (as a pad control may): the row that
        // had it is unbound, so a rebound key never presses two buttons at once.
        for (Row& r : keys_)
            if (r.value == sdl_key_name) r.value.clear();
        keys_[armed_row_].value = sdl_key_name;
    }
    Disarm();
    Save(nullptr);
    return true;
}

bool ControlsBindings::OfferPadControl(const std::string& control_name) {
    if (armed_ != Target::Pad || armed_row_ < 0 || armed_row_ >= static_cast<int>(pad_.size()) || control_name.empty()) return false;
    // The same control may drive only one PS2 button: the row that had it is unbound.
    for (Row& r : pad_)
        if (r.value == control_name) r.value = "none";
    pad_[armed_row_].value = control_name;
    Disarm();
    Save(nullptr);
    return true;
}

bool ControlsBindings::OfferPadStick(const std::string& stick_name) {
    if (armed_ != Target::Stick || armed_row_ < 0 || armed_row_ >= static_cast<int>(sticks_.size())) return false;
    if (stick_name != "left" && stick_name != "right") return false;
    // Two sticks, two roles: the other role takes the other stick.
    sticks_[armed_row_].value = stick_name;
    sticks_[1 - armed_row_].value = stick_name == "left" ? "right" : "left";
    Disarm();
    Save(nullptr);
    return true;
}

std::string ControlsBindings::PadStickFromEvent(int axis_index, int axis_value) {
    if (axis_value > -32767 / 2 && axis_value < 32767 / 2) return "";
    if (axis_index == SDL_CONTROLLER_AXIS_LEFTX || axis_index == SDL_CONTROLLER_AXIS_LEFTY) return "left";
    if (axis_index == SDL_CONTROLLER_AXIS_RIGHTX || axis_index == SDL_CONTROLLER_AXIS_RIGHTY) return "right";
    return "";
}

std::string ControlsBindings::PadControlFromEvent(bool is_button, int index, int axis_value) {
    if (is_button) {
        if (index < 0 || index >= SDL_CONTROLLER_BUTTON_MAX) return "";
        return platform::sdl::PadButtonMap::ControlName(platform::sdl::PadControl{index, -1});
    }
    if (axis_value <= 32767 / 4) return "";
    if (index == SDL_CONTROLLER_AXIS_TRIGGERLEFT) return "lefttrigger";
    if (index == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) return "righttrigger";
    return "";
}

}  // namespace ps2r::app
