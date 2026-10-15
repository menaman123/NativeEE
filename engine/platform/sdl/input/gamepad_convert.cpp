// platform/sdl/input/gamepad_convert.cpp
#include "engine/platform/sdl/input/gamepad_convert.h"

#include <SDL.h>   // SDL_CONTROLLER_* enum constants only

#include <cmath>

#include "engine/platform/sdl/input/input_backend.h"

namespace ps2r::platform::sdl {

static_assert(GamepadRaw::kAxes == SDL_CONTROLLER_AXIS_MAX, "GamepadRaw::axis pins SDL_CONTROLLER_AXIS_MAX == 6");
static_assert(GamepadRaw::kButtons == SDL_CONTROLLER_BUTTON_MAX,
              "GamepadRaw::button pins SDL_CONTROLLER_BUTTON_MAX == 21");

void GamepadConvert::Stick(int16_t rawx, int16_t rawy, float deadzone, float sensitivity,
                           float& outx, float& outy) {
    outx = 0.f;
    outy = 0.f;
    float x = (float)rawx / 32767.f;
    float y = (float)rawy / 32767.f;
    if (x < -1.f) x = -1.f;
    if (x > +1.f) x = +1.f;
    if (y < -1.f) y = -1.f;
    if (y > +1.f) y = +1.f;

    // Radial deadzone: a position inside the circle snaps to centre.
    if (deadzone > 0.f) {
        const float mag = std::sqrt(x * x + y * y);
        if (mag <= deadzone) return;
    }

    // Sensitivity: scale, then clamp per axis. The angle is preserved while
    // unclamped; near the rails the clamp compresses toward the cardinal.
    x *= sensitivity;
    y *= sensitivity;
    if (x < -1.f) x = -1.f;
    if (x > +1.f) x = +1.f;
    if (y < -1.f) y = -1.f;
    if (y > +1.f) y = +1.f;
    outx = x;
    outy = y;
}

void GamepadConvert::Convert(const GamepadRaw& raw, float deadzone, NormalizedInput& out, float sensitivity,
                             const PadButtonMap& map) {
    // Buttons (and the two triggers as buttons past 25 % travel) by the map.
    out.buttons |= map.Pressed(raw.button, GamepadRaw::kButtons, raw.axis[SDL_CONTROLLER_AXIS_TRIGGERLEFT],
                               raw.axis[SDL_CONTROLLER_AXIS_TRIGGERRIGHT]);

    Stick(raw.axis[SDL_CONTROLLER_AXIS_LEFTX], raw.axis[SDL_CONTROLLER_AXIS_LEFTY], deadzone, sensitivity,
          out.lx, out.ly);
    Stick(raw.axis[SDL_CONTROLLER_AXIS_RIGHTX], raw.axis[SDL_CONTROLLER_AXIS_RIGHTY], deadzone, sensitivity,
          out.rx, out.ry);
}

}  // namespace ps2r::platform::sdl
