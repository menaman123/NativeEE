// platform/sdl/input/gamepad_convert.h -- the PURE game-controller mapping:
// a raw SDL_GameController snapshot -> NormalizedInput, with no device and
// no SDL call, so the tables are unit-testable with no pad present. Header
// SDL-free; the .cpp pins the array sizes against SDL's enums.
#pragma once

#include <cstdint>

#include "engine/platform/sdl/input/pad_button_map.h"

namespace ps2r::platform::sdl {

struct NormalizedInput;

// Raw snapshot of one SDL_GameController. Indices are SDL_CONTROLLER_AXIS_* /
// SDL_CONTROLLER_BUTTON_* values (SDL2: 6 axes, 21 buttons).
struct GamepadRaw {
    static constexpr int kAxes = 6;      // LEFTX, LEFTY, RIGHTX, RIGHTY, TRIGGERLEFT, TRIGGERRIGHT
    static constexpr int kButtons = 21;
    int16_t axis[kAxes] = {};
    uint8_t button[kButtons] = {};       // nonzero = pressed
};

class GamepadConvert {
public:
    // Buttons follow `map` (default: SDL's standard layout == the DualShock 2
    // layout, A = Cross, B = Circle, X = Square, Y = Triangle, ...; the
    // triggers press L2/R2 past 25 % travel -- the DualShock 2 wire has no
    // trigger axis). Sticks go through Stick(). Buttons are OR-ed into `out`;
    // stick fields are written.
    static void Convert(const GamepadRaw& raw, float deadzone, NormalizedInput& out,
                        float sensitivity = 1.33f, const PadButtonMap& map = PadButtonMap());

    // One stick: normalise by 32767, clamp, radial deadzone SNAP (a position
    // whose magnitude is <= deadzone reads as neutral; no rescale from the
    // edge -- the game applies its own inner deadzone), then per-axis
    // scale-and-clamp by `sensitivity` (1.33 lets circle-gated pads reach the
    // DualShock 2 gate's rail and corner values).
    static void Stick(int16_t rawx, int16_t rawy, float deadzone, float sensitivity,
                      float& outx, float& outy);
};

}  // namespace ps2r::platform::sdl
