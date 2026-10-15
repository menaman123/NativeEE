// platform/sdl/input/input_system.cpp
#include "engine/platform/sdl/input/input_system.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <SDL.h>

#include "engine/platform/sdl/input/gamepad_backend.h"
#include "engine/platform/sdl/input/input_backend.h"
#include "engine/platform/sdl/input/keyboard_mouse_backend.h"

namespace ps2r::platform::sdl {

namespace {

void TrimInPlace(std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    const size_t e = s.find_last_not_of(" \t\r\n");
    s = (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
}

// Larger magnitude wins; on a tie the current (earlier) value stays.
float Pick(float cur, float cand) {
    const float ac = cur < 0 ? -cur : cur, aa = cand < 0 ? -cand : cand;
    return aa > ac ? cand : cur;
}

}  // namespace

InputSystem::InputSystem() = default;
InputSystem::~InputSystem() { Shutdown(); }

void InputSystem::Init(const InputSettings& settings) {
    if (initialized_) return;
    initialized_ = true;
    settings_ = settings;
    kbm_enabled_ = settings.kbm_enabled;

    // Bindings: baked-in default, replaced by the file when every line parses.
    bool from_file = false;
    if (!settings.bindings_path.empty()) {
        from_file = mapper_.LoadConfig(settings.bindings_path);
        if (from_file) std::fprintf(stderr, "input: bindings loaded from %s\n", settings.bindings_path.c_str());
        else           std::fprintf(stderr, "input: baked-in default bindings (%s absent or rejected)\n",
                                    settings.bindings_path.c_str());
    }
    const Bindings& b = mapper_.Config();
    const float deadzone    = from_file ? b.gamepad_deadzone    : settings.gamepad_deadzone;
    const float sensitivity = from_file ? b.gamepad_sensitivity : settings.gamepad_sensitivity;
    const bool  pad_enabled = settings.gamepad_enabled && (!from_file || b.gamepad_enabled);

    kbm_     = std::make_unique<KeyboardMouseBackend>(&mapper_);
    gamepad_ = std::make_unique<GamepadBackend>(deadzone, sensitivity, pad_enabled, settings.gamecontrollerdb_path);
    gamepad_->SetButtonMap(b.pad_map);   // [pad] remap (the baked default when no file / no section)
    kbm_->SetUiCaptured(ui_capture_ || !kbm_enabled_);
    gamepad_->SetUiCaptured(ui_capture_);

    if (!settings.device_config_path.empty()) LoadDeviceConfig(settings.device_config_path);
    std::fprintf(stderr, "input: backends kbm + gamepad(%s)\n", gamepad_->IsActive() ? "active" : "idle");
}

void InputSystem::Shutdown() {
    gamepad_.reset();
    kbm_.reset();
    initialized_ = false;
    mapper_.ResetMouseAccum();
    last_ = {};
}

void InputSystem::HandleEvent(const SDL_Event& e) {
    if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
        mapper_.ResetMouseAccum();
    if (kbm_) kbm_->HandleEvent(e);
    if (gamepad_) gamepad_->HandleEvent(e);
    // The active source follows the last event (the kbm BUG contract).
    switch (e.type) {
        case SDL_KEYDOWN: case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEMOTION: case SDL_MOUSEWHEEL:
            if (kbm_enabled_ && !(e.type == SDL_KEYDOWN && e.key.repeat)) NoteSource(Source::Kbm);
            break;
        case SDL_CONTROLLERBUTTONDOWN: case SDL_JOYBUTTONDOWN:
            NoteSource(Source::Gamepad);
            break;
        case SDL_CONTROLLERAXISMOTION:
            if (e.caxis.value > 8192 || e.caxis.value < -8192) NoteSource(Source::Gamepad);
            break;
        case SDL_JOYDEVICEADDED:
            ++hotplug_added_;
            break;
        case SDL_JOYDEVICEREMOVED:
            ++hotplug_removed_;
            // The unplugged pad hands over: the keyboard is the live pad at once.
            if (active_ == Source::Gamepad && !(gamepad_ && gamepad_->IsActive())) NoteSource(kbm_enabled_ ? Source::Kbm : Source::None);
            break;
        default: break;
    }
}

void InputSystem::NoteSource(Source source) {
    if (source == active_) return;
    if (active_ != Source::None) ++switches_;
    active_ = source;
}

std::string InputSystem::Sources() const {
    const bool pad = gamepad_ && gamepad_->IsActive();
    return std::string("kbm=") + (kbm_enabled_ ? "on" : "off") + ",gamepad=" + (pad ? "connected" : "absent") +
           ",active=" + (active_ == Source::Kbm ? "kbm" : active_ == Source::Gamepad ? "gamepad" : "none") +
           ",switches=" + std::to_string(switches_) + ",hotplug_added=" + std::to_string(hotplug_added_) +
           ",hotplug_removed=" + std::to_string(hotplug_removed_);
}

bool InputSystem::Connected() const {
    return initialized_ && (kbm_enabled_ || (gamepad_ && gamepad_->IsActive()));
}

void InputSystem::SetUiCapture(bool captured) {
    ui_capture_ = captured;
    if (captured) mapper_.ResetMouseAccum();
    if (kbm_) kbm_->SetUiCaptured(captured || !kbm_enabled_);
    if (gamepad_) gamepad_->SetUiCaptured(captured);
}

void InputSystem::ResetPending() {
    if (kbm_) kbm_->ClearPending();
    if (gamepad_) gamepad_->ClearPending();
    mapper_.ResetMouseAccum();
    last_ = {};
}

void InputSystem::SetKbmEnabled(bool enabled) {
    kbm_enabled_ = enabled;
    if (kbm_) kbm_->SetUiCaptured(ui_capture_ || !enabled);
    if (!enabled) mapper_.ResetMouseAccum();
}

PadSample InputSystem::Frame(float dt_seconds) {
    if (!initialized_) return PadSample{};   // neutral: no buttons, sticks 0x80

    NormalizedInput merged;
    InputBackend* order[2] = {kbm_.get(), gamepad_.get()};
    for (InputBackend* be : order) {
        if (!be || !be->IsActive()) continue;
        NormalizedInput one;
        be->Poll(one);
        if (be == kbm_.get() && !kbm_enabled_) continue;   // polled, discarded
        merged.buttons |= one.buttons;
        merged.lx = Pick(merged.lx, one.lx);
        merged.ly = Pick(merged.ly, one.ly);
        merged.rx = Pick(merged.rx, one.rx);
        merged.ry = Pick(merged.ry, one.ry);
        merged.mouse_dx += one.mouse_dx;
        merged.mouse_dy += one.mouse_dy;
        for (int i = 0; i < 3; i++) merged.mouse_buttons[i] = merged.mouse_buttons[i] || one.mouse_buttons[i];
    }

    PadSample mapped = mapper_.Map(merged, dt_seconds);
    last_ = mapped;
    if (ui_capture_) {
        mapper_.ResetMouseAccum();   // no stale camera momentum when the UI closes
        mapped = PadSample{};
    }
    return mapped;
}

bool InputSystem::LoadDeviceConfig(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string line;
    while (std::getline(f, line)) {
        const size_t sc = line.find(';');
        if (sc != std::string::npos) line.resize(sc);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        TrimInPlace(key);
        TrimInPlace(val);
        if (key == "pad") {
            if (!gamepad_) continue;
            if (val == "auto")      gamepad_->SetSelection(GamepadBackend::kSelectionAuto);
            else if (val == "none") gamepad_->SetSelection(GamepadBackend::kSelectionNone);
            else if (val.rfind("guid:", 0) == 0) gamepad_->SetPreferredGuid(val.substr(5));   // Auto until it connects
        } else if (key == "kbm") {
            SetKbmEnabled(val != "0");
        } else if (key == "deadzone") {
            if (gamepad_) gamepad_->SetDeadzone((float)std::atof(val.c_str()));
        } else if (key == "sensitivity") {
            if (gamepad_) gamepad_->SetSensitivity((float)std::atof(val.c_str()));
        }
    }
    std::fprintf(stderr, "input: device config loaded from %s\n", path.c_str());
    return true;
}

bool InputSystem::SaveDeviceConfig(const std::string& path) const {
    std::string pad = "auto";
    float deadzone = settings_.gamepad_deadzone, sensitivity = settings_.gamepad_sensitivity;
    if (gamepad_) {
        const int sel = gamepad_->Selection();
        if (sel == GamepadBackend::kSelectionNone) {
            pad = "none";
        } else if (sel >= 0) {
            for (const auto& p : gamepad_->Snapshot())
                if (p.instance_id == sel && !p.guid.empty()) pad = "guid:" + p.guid;
        } else if (!gamepad_->PreferredGuid().empty()) {
            pad = "guid:" + gamepad_->PreferredGuid();   // Auto with an armed preference (pad unplugged)
        }
        deadzone = gamepad_->Deadzone();
        sensitivity = gamepad_->Sensitivity();
    }

    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) {
        std::fprintf(stderr, "input: failed to write %s\n", path.c_str());
        return false;
    }
    f << "; input device selection\n"
      << "; pad = auto | none | guid:<SDL joystick GUID>\n"
      << "pad = " << pad << "\n"
      << "kbm = " << (kbm_enabled_ ? 1 : 0) << "\n"
      << "deadzone = " << deadzone << "\n"
      << "sensitivity = " << sensitivity << "\n";
    return true;
}

}  // namespace ps2r::platform::sdl
