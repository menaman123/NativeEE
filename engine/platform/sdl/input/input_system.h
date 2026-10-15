// platform/sdl/input/input_system.h -- the composition root's input surface:
// owns the InputMapper and the two backends, forwards SDL events, and turns
// one main-loop iteration into one PadSample. MAIN thread only. The boundary
// thread receives a snapshot (ToInputState) that the pad protocol stamps on
// the tick domain; nothing here reads a clock -- `dt` is the caller's.
// Headless legs never construct this class (the harness selects InputLog
// replay or the neutral pad by CLI).
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "engine/platform/sdl/input/input_mapper.h"
#include "engine/platform/sdl/input/input_settings.h"
#include "engine/platform/sdl/input/pad_sample.h"

union SDL_Event;

namespace ps2r::platform::sdl {

class KeyboardMouseBackend;
class GamepadBackend;

class InputSystem {
public:
    InputSystem();
    ~InputSystem();
    InputSystem(const InputSystem&) = delete;
    InputSystem& operator=(const InputSystem&) = delete;

    // Precedence: InputSettings (Config) < bindings file (when it loads) for
    // the gamepad deadzone/sensitivity/enabled < device-config file for the
    // pad selection, kbm toggle, deadzone and sensitivity. Idempotent.
    void Init(const InputSettings& settings);
    void Shutdown();

    void HandleEvent(const SDL_Event& e);

    // One call consumes the pending physical button edges for one game
    // sample. Event-pump or GUI-only iterations must not call Frame.
    // Poll kbm first, then gamepad; merge = buttons OR, each stick axis by
    // strictly larger magnitude (kbm wins ties), mouse deltas summed, mouse
    // buttons OR; then InputMapper::Map. While the UI has capture the
    // backends are still polled (SDL's relative-mouse accumulator keeps
    // draining) but the result is the neutral sample and the mouse
    // accumulator is reset. The kbm result is discarded when kbm is disabled.
    PadSample Frame(float dt_seconds);

    // The pre-suppression sample of the last Frame (a GUI's live tester).
    const PadSample& LastSample() const { return last_; }
    // Connection for the same event drain as Frame: an enabled keyboard is a
    // virtual controller, otherwise a selected physical controller is needed.
    // Focus loss/UI capture neutralize controls without unplugging that device.
    bool Connected() const;

    void SetUiCapture(bool captured);
    // Main-thread reset/restore hand-off: discard physical edges and mouse
    // momentum from the preceding game session; held input is sampled anew.
    void ResetPending();
    bool UiCaptured() const { return ui_capture_; }
    void SetKbmEnabled(bool enabled);
    bool KbmEnabled() const { return kbm_enabled_; }

    // Device-selection INI at settings.device_config_path:
    //   pad = auto | none | guid:<SDL joystick GUID>
    //   kbm = 0 | 1
    //   deadzone = <float>
    //   sensitivity = <float>
    // Load is part of Init; Save is the GUI's call. Both return false when the
    // file cannot be opened.
    bool LoadDeviceConfig(const std::string& path);
    bool SaveDeviceConfig(const std::string& path) const;

    InputMapper&          Mapper() { return mapper_; }
    const InputMapper&    Mapper() const { return mapper_; }
    GamepadBackend*       Gamepad() { return gamepad_.get(); }
    KeyboardMouseBackend* Kbm() { return kbm_.get(); }

    // The source contract (the 2026-10-13 kbm BUG): a connected gamepad is used
    // when present, keyboard/mouse are ALWAYS live as a pad, the ACTIVE source is
    // whichever produced the LAST event (a key/mouse event -> kbm; a controller
    // button/axis event -> gamepad; the active pad unplugged -> kbm), hot-plug
    // both ways. Sources(): "kbm=on|off,gamepad=connected|absent,active=kbm|
    // gamepad|none,switches=N,hotplug_added=N,hotplug_removed=N" (run.meta
    // input_sources=; pins RootKbmPadOnly, RootPadHotplug).
    enum class Source { None, Kbm, Gamepad };
    Source ActiveSource() const { return active_; }
    std::string Sources() const;

private:
    void NoteSource(Source source);
    Source active_ = Source::None;
    uint64_t switches_ = 0, hotplug_added_ = 0, hotplug_removed_ = 0;
    InputSettings settings_;
    InputMapper   mapper_;
    std::unique_ptr<KeyboardMouseBackend> kbm_;
    std::unique_ptr<GamepadBackend>       gamepad_;
    PadSample last_;
    bool ui_capture_ = false;
    bool kbm_enabled_ = true;
    bool initialized_ = false;
};

}  // namespace ps2r::platform::sdl
