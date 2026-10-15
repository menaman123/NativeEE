// platform/sdl/input/gamepad_backend.cpp
#include "engine/platform/sdl/input/gamepad_backend.h"

#include <SDL.h>

#include <cstdio>

#include "engine/platform/sdl/input/gamepad_convert.h"

namespace ps2r::platform::sdl {

namespace {

std::string GuidOfJoystick(SDL_Joystick* js) {
    char buf[64] = {0};
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(js), buf, sizeof(buf));
    return std::string(buf);
}

// Larger magnitude wins; on a tie the current (earlier) value stays.
float Pick(float cur, float cand) {
    const float ac = cur < 0 ? -cur : cur, aa = cand < 0 ? -cand : cand;
    return aa > ac ? cand : cur;
}

}  // namespace

GamepadBackend::GamepadBackend(float deadzone, float sensitivity, bool enabled,
                               const std::string& gamecontrollerdb_path)
    : enabled_(enabled) {
    SetDeadzone(deadzone);
    SetSensitivity(sensitivity);

    // Keep pads usable while the window is unfocused; must precede the
    // joystick subsystem init.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "input: gamepad subsystem init failed: %s\n", SDL_GetError());
        return;
    }
    subsystem_ok_ = true;

    if (!gamecontrollerdb_path.empty()) {
        const int n = SDL_GameControllerAddMappingsFromFile(gamecontrollerdb_path.c_str());
        if (n > 0) std::fprintf(stderr, "input: gamepad: %d mappings loaded from %s\n", n, gamecontrollerdb_path.c_str());
    }

    const int n = SDL_NumJoysticks();
    for (int i = 0; i < n; i++) TryOpenDeviceIndex(i);
    PublishSnapshot();
    if (pads_.empty()) std::fprintf(stderr, "input: gamepad: none detected at startup (hotplug supported)\n");
}

GamepadBackend::~GamepadBackend() {
    for (auto& p : pads_)
        if (p.controller) SDL_GameControllerClose((SDL_GameController*)p.controller);
    pads_.clear();
    snapshot_.clear();
    if (subsystem_ok_) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

void GamepadBackend::SetSelection(int selection) {
    if (selection_ != selection) ClearPending();
    selection_ = selection;
    if (selection >= 0) {
        for (const auto& p : snapshot_)
            if (p.instance_id == selection) { preferred_guid_ = p.guid; return; }
    } else {
        preferred_guid_.clear();
    }
}

void GamepadBackend::ClearPending() {
    for (auto& pad : pads_) pad.pending_buttons = 0;
}

void GamepadBackend::SetEnabled(bool enabled) {
    if (enabled_ != enabled) ClearPending();
    enabled_ = enabled;
}

void GamepadBackend::SetUiCaptured(bool captured) {
    if (ui_captured_ != captured) ClearPending();
    ui_captured_ = captured;
}

void GamepadBackend::SetDeadzone(float dz) {
    if (dz < kMinDeadzone) dz = kMinDeadzone;
    if (dz > kMaxDeadzone) dz = kMaxDeadzone;
    deadzone_ = dz;
}

void GamepadBackend::SetSensitivity(float s) {
    if (s < kMinSensitivity) s = kMinSensitivity;
    if (s > kMaxSensitivity) s = kMaxSensitivity;
    sensitivity_ = s;
}

// Open device index i as a game controller. Without an SDL mapping, register
// a best-effort generic (standard Xbox-style) layout keyed on the joystick
// GUID first; a gamecontrollerdb entry overrides it when present.
void GamepadBackend::TryOpenDeviceIndex(int device_index) {
    const SDL_JoystickID iid = SDL_JoystickGetDeviceInstanceID(device_index);
    for (const auto& p : pads_)
        if (p.instance_id == (int)iid) return;   // JOYDEVICEADDED for a pad opened at startup

    bool generic = false;
    if (!SDL_IsGameController(device_index)) {
        char guid[64] = {0};
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(device_index), guid, sizeof(guid));
        const char* jname = SDL_JoystickNameForIndex(device_index);
        const std::string mapping = std::string(guid) + "," + (jname ? jname : "Generic Controller") +
            ",a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,"
            "leftstick:b7,rightstick:b8,leftshoulder:b9,rightshoulder:b10,"
            "dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,"
            "leftx:a0,lefty:a1,rightx:a2,righty:a3,"
            "lefttrigger:a4,righttrigger:a5";
        if (SDL_GameControllerAddMapping(mapping.c_str()) < 0) {
            std::fprintf(stderr, "input: gamepad: no mapping for '%s' (%s) and the generic fallback failed: %s\n",
                         jname ? jname : "?", guid, SDL_GetError());
            return;
        }
        generic = true;
    }

    SDL_GameController* gc = SDL_GameControllerOpen(device_index);
    if (!gc) {
        std::fprintf(stderr, "input: gamepad: open failed for device %d: %s\n", device_index, SDL_GetError());
        return;
    }

    Pad pad;
    pad.controller = gc;
    pad.generic_mapping = generic;
    SDL_Joystick* js = SDL_GameControllerGetJoystick(gc);
    pad.instance_id = js ? (int)SDL_JoystickInstanceID(js) : -1;
    pads_.push_back(pad);

    const char* name = SDL_GameControllerName(gc);
    std::fprintf(stderr, "input: gamepad connected: %s (instance %d)%s\n", name ? name : "?", pad.instance_id,
                 generic ? " [generic mapping]" : "");

    // Reconnect-by-GUID: a pad matching the persisted preference is selected
    // when the selection is Auto.
    if (js && !preferred_guid_.empty() && preferred_guid_ == GuidOfJoystick(js) && selection_ == kSelectionAuto) {
        selection_ = pad.instance_id;
        std::fprintf(stderr, "input: gamepad: preferred pad reconnected -> selected (instance %d)\n", pad.instance_id);
    }
}

void GamepadBackend::CloseInstance(int instance_id) {
    for (size_t i = 0; i < pads_.size(); i++) {
        if (pads_[i].instance_id != instance_id) continue;
        std::fprintf(stderr, "input: gamepad disconnected (instance %d)\n", instance_id);
        SDL_GameControllerClose((SDL_GameController*)pads_[i].controller);
        pads_.erase(pads_.begin() + (long)i);
        // The selected pad went away: fall back to Auto so input keeps
        // working; the preferred GUID stays armed for reconnection.
        if (selection_ == instance_id) selection_ = kSelectionAuto;
        return;
    }
}

void GamepadBackend::PublishSnapshot() {
    snapshot_.clear();
    snapshot_.reserve(pads_.size());
    for (auto& p : pads_) {
        PadInfo info;
        info.instance_id = p.instance_id;
        info.generic_mapping = p.generic_mapping;
        const char* n = SDL_GameControllerName((SDL_GameController*)p.controller);
        info.name = n ? n : "Unknown Controller";
        SDL_Joystick* js = SDL_GameControllerGetJoystick((SDL_GameController*)p.controller);
        if (js) info.guid = GuidOfJoystick(js);
        snapshot_.push_back(std::move(info));
    }
}

void GamepadBackend::HandleEvent(const SDL_Event& e) {
    if (e.type == SDL_JOYDEVICEADDED) {
        TryOpenDeviceIndex(e.jdevice.which);   // which = device INDEX
        PublishSnapshot();
    } else if (e.type == SDL_JOYDEVICEREMOVED) {
        CloseInstance((int)e.jdevice.which);   // which = INSTANCE id
        PublishSnapshot();
    } else if (e.type == SDL_WINDOWEVENT &&
               (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST || e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)) {
        ClearPending();
    } else if (enabled_ && !ui_captured_ && selection_ != kSelectionNone &&
               (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERAXISMOTION)) {
        const int instance = e.type == SDL_CONTROLLERBUTTONDOWN ? e.cbutton.which : e.caxis.which;
        for (auto& pad : pads_) {
            if (pad.instance_id != instance || (selection_ >= 0 && pad.instance_id != selection_)) continue;
            GamepadRaw edge{};
            if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button < GamepadRaw::kButtons)
                edge.button[e.cbutton.button] = 1;
            else if (e.type == SDL_CONTROLLERAXISMOTION &&
                     (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT))
                edge.axis[e.caxis.axis] = e.caxis.value;
            NormalizedInput converted;
            GamepadConvert::Convert(edge, deadzone_, converted, sensitivity_, map_);
            pad.pending_buttons |= converted.buttons;
        }
    }
}

void GamepadBackend::Poll(NormalizedInput& out) {
    if (!enabled_ || pads_.empty() || selection_ == kSelectionNone) return;
    for (auto& p : pads_) {
        if (selection_ >= 0 && p.instance_id != selection_) continue;
        SDL_GameController* gc = (SDL_GameController*)p.controller;

        GamepadRaw raw;
        for (int a = 0; a < GamepadRaw::kAxes; a++)
            raw.axis[a] = SDL_GameControllerGetAxis(gc, (SDL_GameControllerAxis)a);
        for (int b = 0; b < GamepadRaw::kButtons; b++)
            raw.button[b] = SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)b);

        NormalizedInput one;
        GamepadConvert::Convert(raw, deadzone_, one, sensitivity_, map_);
        one.buttons |= p.pending_buttons;
        p.pending_buttons = 0;

        out.buttons |= one.buttons;
        out.lx = Pick(out.lx, one.lx);
        out.ly = Pick(out.ly, one.ly);
        out.rx = Pick(out.rx, one.rx);
        out.ry = Pick(out.ry, one.ry);
    }
}

bool GamepadBackend::IsActive() const {
    return enabled_ && !pads_.empty() && selection_ != kSelectionNone;
}

}  // namespace ps2r::platform::sdl
