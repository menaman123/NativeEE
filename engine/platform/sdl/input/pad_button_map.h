// platform/sdl/input/pad_button_map.h -- which SDL_GameController control
// presses which DualShock 2 button (CHARTER 2 A5: the gamepad-button remap).
// One entry per LogicalButton bit (16): an SDL_CONTROLLER_BUTTON_* index, or
// one of the two trigger axes (pressed past 25 % travel), or nothing. The
// default is SDL's standard layout, which equals the DualShock 2 layout
// (A = Cross, B = Circle, X = Square, Y = Triangle, ...). Header SDL-free:
// the indices are plain ints; the .cpp resolves SDL's control names
// (SDL_GameControllerGetButtonFromString / GetAxisFromString, no SDL_Init).
#pragma once

#include <cstdint>
#include <string>

namespace ps2r::platform::sdl {

// One control of a pad: a button index, or a trigger axis index; both -1 = unbound (POD).
struct PadControl {
    int button = -1;   // SDL_CONTROLLER_BUTTON_* (0..20) or -1
    int axis = -1;     // SDL_CONTROLLER_AXIS_TRIGGERLEFT / TRIGGERRIGHT or -1
};
inline bool operator==(const PadControl& a, const PadControl& b) { return a.button == b.button && a.axis == b.axis; }
inline bool operator!=(const PadControl& a, const PadControl& b) { return !(a == b); }

class PadButtonMap {
public:
    static constexpr int kButtons = 16;   // LogicalButton bits 0..15

    // SDL's standard layout (== the DualShock 2 layout).
    PadButtonMap();

    // The control bound to LogicalButton bit `bit_index` (0..15).
    const PadControl& Control(int bit_index) const { return control_[bit_index & 15]; }
    void Set(int bit_index, const PadControl& c) { control_[bit_index & 15] = c; }

    // LogicalButton bits pressed by the raw pad state: `buttons[21]` nonzero =
    // pressed, `trigger_left/right` raw axis values (pressed past 25 % travel).
    uint32_t Pressed(const uint8_t* buttons, int num_buttons, int16_t trigger_left, int16_t trigger_right) const;

    // SDL's control names ("a", "b", "x", "y", "back", "guide", "start",
    // "leftstick", "rightstick", "leftshoulder", "rightshoulder", "dpup",
    // "dpdown", "dpleft", "dpright", "misc1", "paddle1".."paddle4",
    // "touchpad") plus "lefttrigger" / "righttrigger" and "none". Case-insensitive.
    static bool ControlFromName(const std::string& name, PadControl& out);
    static std::string ControlName(const PadControl& c);   // "none" when unbound
    static bool Bound(const PadControl& c) { return c.button >= 0 || c.axis >= 0; }

    bool operator==(const PadButtonMap& o) const;
    bool operator!=(const PadButtonMap& o) const { return !(*this == o); }

private:
    PadControl control_[kButtons];
};

}  // namespace ps2r::platform::sdl
