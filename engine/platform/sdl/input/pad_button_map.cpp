// platform/sdl/input/pad_button_map.cpp
#include "engine/platform/sdl/input/pad_button_map.h"

#include <SDL.h>   // SDL_CONTROLLER_* constants + the name tables; no SDL_Init needed

#include <algorithm>
#include <cctype>

#include "engine/platform/sdl/input/input_backend.h"

namespace ps2r::platform::sdl {

namespace {

int BitIndex(uint32_t bit) {
    int i = 0;
    while (i < 31 && (1u << i) != bit) i++;
    return i;
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

}  // namespace

PadButtonMap::PadButtonMap() {
    static const struct { uint32_t bit; int button; int axis; } kStandard[] = {
        {kBtnCross,    SDL_CONTROLLER_BUTTON_A, -1},
        {kBtnCircle,   SDL_CONTROLLER_BUTTON_B, -1},
        {kBtnSquare,   SDL_CONTROLLER_BUTTON_X, -1},
        {kBtnTriangle, SDL_CONTROLLER_BUTTON_Y, -1},
        {kBtnSelect,   SDL_CONTROLLER_BUTTON_BACK, -1},
        {kBtnStart,    SDL_CONTROLLER_BUTTON_START, -1},
        {kBtnL3,       SDL_CONTROLLER_BUTTON_LEFTSTICK, -1},
        {kBtnR3,       SDL_CONTROLLER_BUTTON_RIGHTSTICK, -1},
        {kBtnL1,       SDL_CONTROLLER_BUTTON_LEFTSHOULDER, -1},
        {kBtnR1,       SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, -1},
        {kBtnUp,       SDL_CONTROLLER_BUTTON_DPAD_UP, -1},
        {kBtnDown,     SDL_CONTROLLER_BUTTON_DPAD_DOWN, -1},
        {kBtnLeft,     SDL_CONTROLLER_BUTTON_DPAD_LEFT, -1},
        {kBtnRight,    SDL_CONTROLLER_BUTTON_DPAD_RIGHT, -1},
        {kBtnL2,       -1, SDL_CONTROLLER_AXIS_TRIGGERLEFT},
        {kBtnR2,       -1, SDL_CONTROLLER_AXIS_TRIGGERRIGHT},
    };
    for (const auto& s : kStandard) control_[BitIndex(s.bit)] = PadControl{s.button, s.axis};
}

uint32_t PadButtonMap::Pressed(const uint8_t* buttons, int num_buttons, int16_t trigger_left, int16_t trigger_right) const {
    // The DualShock 2 wire has no trigger axis: a trigger is a button past 25 % travel.
    const int16_t kTrig = 32767 / 4;
    uint32_t out = 0;
    for (int i = 0; i < kButtons; i++) {
        const PadControl& c = control_[i];
        bool down = false;
        if (c.button >= 0 && c.button < num_buttons && buttons && buttons[c.button]) down = true;
        if (c.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT && trigger_left > kTrig) down = true;
        if (c.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT && trigger_right > kTrig) down = true;
        if (down) out |= 1u << i;
    }
    return out;
}

bool PadButtonMap::ControlFromName(const std::string& name, PadControl& out) {
    const std::string low = ToLower(name);
    if (low == "none") { out = PadControl{}; return true; }
    if (low == "lefttrigger")  { out = PadControl{-1, SDL_CONTROLLER_AXIS_TRIGGERLEFT}; return true; }
    if (low == "righttrigger") { out = PadControl{-1, SDL_CONTROLLER_AXIS_TRIGGERRIGHT}; return true; }
    const SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(low.c_str());
    if (b == SDL_CONTROLLER_BUTTON_INVALID) return false;
    out = PadControl{(int)b, -1};
    return true;
}

std::string PadButtonMap::ControlName(const PadControl& c) {
    if (c.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) return "lefttrigger";
    if (c.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) return "righttrigger";
    if (c.button >= 0 && c.button < SDL_CONTROLLER_BUTTON_MAX) {
        const char* n = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)c.button);
        if (n) return n;
    }
    return "none";
}

bool PadButtonMap::operator==(const PadButtonMap& o) const {
    for (int i = 0; i < kButtons; i++)
        if (control_[i] != o.control_[i]) return false;
    return true;
}

}  // namespace ps2r::platform::sdl
