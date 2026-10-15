// platform/sdl/input/input_mapper.h -- host controls -> PadSample. Owns the
// bindings (a baked-in default INI compiled in at configure time, replaced
// wholesale by a valid on-disk file) and translates a merged NormalizedInput
// into the DualShock 2 sample. SDL-free header: scancodes are plain ints
// (SDL_Scancode values) resolved from names in the .cpp, whose ONLY SDL call
// is SDL_GetScancodeFromName (no SDL_Init needed); the [pad] control names
// resolve through PadButtonMap.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "engine/platform/sdl/input/pad_button_map.h"

namespace ps2r::platform::sdl {

struct NormalizedInput;
struct PadSample;

// Parsed bindings; the INI grammar is documented in default_bindings.ini.
struct Bindings {
    std::vector<std::pair<int, uint32_t>> key_buttons;     // SDL scancode -> LogicalButton bit
    std::vector<std::pair<int, uint32_t>> mouse_buttons;   // 0=L 1=M 2=R -> LogicalButton bit
    int   key_left_up = 0, key_left_down = 0, key_left_left = 0, key_left_right = 0;
    bool  mouse_to_right_stick = true;    // [sticks] mouse = right|left
    float mouse_sensitivity    = 0.35f;   // stick units per 100 px
    float mouse_decay          = 12.0f;   // per-second exponential decay rate
    bool  gamepad_enabled      = true;
    float gamepad_deadzone     = 0.15f;   // radial snap radius, [0, 1)
    float gamepad_sensitivity  = 1.33f;   // per-axis scale-and-clamp, [0.01, 2]
    PadButtonMap pad_map;                 // [pad] cross = a ... (default: SDL's standard layout)
};

// Stick float [-1, +1] -> pad byte: -1 -> 0x00, 0 -> 0x80 exactly, +1 -> 0xFF
// (uniform 127.5 scale so both rails are reachable and neutral is exact).
uint8_t StickToByte(float v);
// SDL key name (SDL_GetScancodeFromName, case-insensitive) or one of the
// aliases LShift/RShift/LCtrl/RCtrl/LAlt/RAlt. 0 (SDL_SCANCODE_UNKNOWN) on failure.
int ScancodeFromName(const std::string& name);
// "Mouse1" -> 0 (left), "Mouse2" -> 2 (right), "Mouse3" -> 1 (middle); -1 otherwise.
int ParseMouseName(const std::string& name);

class InputMapper {
public:
    InputMapper();   // starts with the baked-in default bindings

    // Parse an INI file. ANY malformed line or unresolvable name rejects the
    // WHOLE file (one stderr line) and the current bindings stay. False when
    // the file is absent or rejected.
    bool LoadConfig(const std::string& path);
    // Same parser over in-memory text; `origin` names the source in the message.
    bool LoadConfigText(const std::string& text, const char* origin);

    void SetMouseSensitivity(float s);

    // Active-high LogicalButton bits for a raw SDL key-state array.
    uint32_t ButtonsFromKeys(const uint8_t* keystate, int numkeys) const;
    // Digital left-stick keys -> floats; opposite keys cancel to 0.
    void KeyStickFromKeys(const uint8_t* keystate, int numkeys, float& lx, float& ly) const;
    // LogicalButton bits bound to one scancode.
    uint32_t LogicalForScancode(int scancode) const;

    // NormalizedInput -> PadSample. `dt_seconds` drives the mouse-accumulator
    // decay only (gamepad and key sticks are stateless); the caller supplies
    // it -- the mapper reads no clock.
    PadSample Map(const NormalizedInput& in, float dt_seconds);

    void ResetMouseAccum();
    const Bindings& Config() const { return b_; }

private:
    Bindings b_;
    float accum_x_ = 0.f, accum_y_ = 0.f;   // mouse accumulate/decay state
};

}  // namespace ps2r::platform::sdl
