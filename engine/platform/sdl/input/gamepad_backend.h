// platform/sdl/input/gamepad_backend.h -- SDL_GameController devices ->
// NormalizedInput. Every connected controller is opened; the default
// selection (Auto) merges all of them, or one pad is selected by instance id
// with its GUID remembered for reconnection. Joysticks SDL has no mapping
// for get a best-effort generic layout. All state is instance-owned; SDL
// handles and every member are touched on the MAIN thread only -- a GUI
// reads Snapshot() and calls the setters through the composition root's own
// main-thread hand-off.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/platform/sdl/input/input_backend.h"
#include "engine/platform/sdl/input/pad_button_map.h"

union SDL_Event;

namespace ps2r::platform::sdl {

// Descriptor of one connected pad (no SDL handles).
struct PadInfo {
    int         instance_id = -1;
    std::string name;
    std::string guid;                     // SDL joystick GUID string (persistence key)
    bool        generic_mapping = false;  // opened via the fallback mapping
};

class GamepadBackend : public InputBackend {
public:
    static constexpr int kSelectionAuto = -1;   // merge input from ALL connected pads
    static constexpr int kSelectionNone = -2;   // gamepads disabled
    static constexpr float kMinDeadzone = 0.f, kMaxDeadzone = 0.9f;
    static constexpr float kMinSensitivity = 0.5f, kMaxSensitivity = 2.0f;

    // Initialises SDL's game-controller subsystem (refcounted; a no-op when
    // the composition root already did), loads extra mappings from
    // `gamecontrollerdb_path` when non-empty, and opens every present pad.
    GamepadBackend(float deadzone, float sensitivity, bool enabled, const std::string& gamecontrollerdb_path);
    ~GamepadBackend() override;
    GamepadBackend(const GamepadBackend&) = delete;
    GamepadBackend& operator=(const GamepadBackend&) = delete;

    // Merges the selected pads: buttons OR, each stick axis by strictly
    // larger magnitude (the earlier pad wins ties).
    void Poll(NormalizedInput& out) override;
    bool IsActive() const override;
    const char* Name() const override { return "gamepad"; }

    // SDL_JOYDEVICEADDED / SDL_JOYDEVICEREMOVED (they fire for every
    // joystick, mapped or not, so they are the single hotplug source).
    void HandleEvent(const SDL_Event& e);
    void SetUiCaptured(bool captured);
    void ClearPending();

    const std::vector<PadInfo>& Snapshot() const { return snapshot_; }
    // kSelectionAuto / kSelectionNone / an instance id from Snapshot().
    // Selecting a pad records its GUID; Auto/None clear the preference.
    void SetSelection(int selection);
    int  Selection() const { return selection_; }
    // Arms "reconnect to me": the selection snaps to the pad with this GUID
    // when it is (or becomes) connected while the selection is Auto.
    void SetPreferredGuid(const std::string& guid) { preferred_guid_ = guid; }
    const std::string& PreferredGuid() const { return preferred_guid_; }
    void  SetDeadzone(float dz);
    float Deadzone() const { return deadzone_; }
    void  SetSensitivity(float s);
    float Sensitivity() const { return sensitivity_; }
    void SetEnabled(bool enabled);
    bool Enabled() const { return enabled_; }
    // The gamepad-button remap (bindings.ini [pad]); default SDL standard layout.
    void SetButtonMap(const PadButtonMap& map) { map_ = map; }
    const PadButtonMap& ButtonMap() const { return map_; }

private:
    struct Pad {
        void* controller = nullptr;   // SDL_GameController*
        int   instance_id = -1;
        bool  generic_mapping = false;
        uint32_t pending_buttons = 0;
    };

    void TryOpenDeviceIndex(int device_index);
    void CloseInstance(int instance_id);
    void PublishSnapshot();

    std::vector<Pad>     pads_;
    std::vector<PadInfo> snapshot_;
    std::string          preferred_guid_;
    int   selection_ = kSelectionAuto;
    float deadzone_ = 0.15f;
    float sensitivity_ = 1.33f;
    PadButtonMap map_;
    bool  enabled_ = true;
    bool  subsystem_ok_ = false;
    bool  ui_captured_ = false;
};

}  // namespace ps2r::platform::sdl
