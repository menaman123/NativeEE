// platform/sdl/input/input_settings.h -- what the composition root hands
// InputSystem::Init. Module-local POD: the mapping from Config's input.*
// fields onto it is the composition root's; nothing here reads a file
// location on its own (no exe-relative paths, no environment).
#pragma once

#include <string>

namespace ps2r::platform::sdl {

struct InputSettings {
    std::string bindings_path;            // bindings INI; empty or absent -> baked default stays
    std::string device_config_path;       // device selection INI (pad / kbm / deadzone / sensitivity)
    std::string gamecontrollerdb_path;    // optional SDL_GameControllerAddMappingsFromFile input
    bool  kbm_enabled         = true;     // keyboard+mouse feed the pad
    bool  gamepad_enabled     = true;     // game controllers feed the pad
    float gamepad_deadzone    = 0.15f;    // radial snap radius, [0, 0.9]
    float gamepad_sensitivity = 1.33f;    // per-axis scale-and-clamp, [0.5, 2.0]
};

}  // namespace ps2r::platform::sdl
