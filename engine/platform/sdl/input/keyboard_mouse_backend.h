// platform/sdl/input/keyboard_mouse_backend.h -- raw SDL keyboard/mouse state
// -> NormalizedInput. The key -> LogicalButton and key -> stick tables belong
// to the InputMapper; this backend queries them. MAIN thread only.
#pragma once

#include <cstdint>

#include "engine/platform/sdl/input/input_backend.h"

union SDL_Event;

namespace ps2r::platform::sdl {

class InputMapper;

class KeyboardMouseBackend : public InputBackend {
public:
    static constexpr int kNumScancodes = 512;   // SDL_NUM_SCANCODES, pinned in the .cpp

    explicit KeyboardMouseBackend(const InputMapper* mapper);

    void Poll(NormalizedInput& out) override;
    bool IsActive() const override { return true; }   // a keyboard is always present
    const char* Name() const override { return "kbm"; }

    // Relative-mouse ("camera") capture: a left click in the window grabs
    // the mouse, ESC or losing window focus releases it. While the UI owns
    // the mouse (SetUiCaptured(true)) a click never captures.
    void HandleEvent(const SDL_Event& e);
    void SetUiCaptured(bool captured);
    void ClearPending();

    // Key injection overlay for integration tests: the scancode reads as
    // held for the NEXT Poll only (the injector re-arms it every frame).
    void InjectKey(int scancode);

private:
    const InputMapper* mapper_;
    bool ui_captured_ = false;
    bool focused_ = true;
    uint32_t pending_buttons_ = 0, pending_mouse_ = 0;
    uint8_t inject_[kNumScancodes];
    uint8_t merged_[kNumScancodes];   // real key state + overlay, per Poll
};

}  // namespace ps2r::platform::sdl
