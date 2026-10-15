// platform/sdl/input/keyboard_mouse_backend.cpp
#include "engine/platform/sdl/input/keyboard_mouse_backend.h"

#include <SDL.h>

#include <cstring>

#include "engine/platform/sdl/input/input_mapper.h"

namespace ps2r::platform::sdl {

static_assert(KeyboardMouseBackend::kNumScancodes == SDL_NUM_SCANCODES,
              "keyboard_mouse_backend.h pins SDL_NUM_SCANCODES == 512");

KeyboardMouseBackend::KeyboardMouseBackend(const InputMapper* mapper) : mapper_(mapper) {
    std::memset(inject_, 0, sizeof(inject_));
    std::memset(merged_, 0, sizeof(merged_));
}

void KeyboardMouseBackend::InjectKey(int scancode) {
    if (scancode > 0 && scancode < kNumScancodes) inject_[scancode] = 1;
}

void KeyboardMouseBackend::ClearPending() {
    pending_buttons_ = pending_mouse_ = 0;
    std::memset(inject_, 0, sizeof(inject_));
}

void KeyboardMouseBackend::SetUiCaptured(bool captured) {
    if (ui_captured_ != captured) ClearPending();
    ui_captured_ = captured;
}

void KeyboardMouseBackend::HandleEvent(const SDL_Event& e) {
    if (e.type == SDL_WINDOWEVENT) {
        if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            focused_ = false;
            ClearPending();
            SDL_SetRelativeMouseMode(SDL_FALSE);
        } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            focused_ = true;
            ClearPending();
        }
    }
    // Keep button presses until the next game sample even if SDL's current
    // state has already received the release. Sticks use current state only.
    if (focused_ && !ui_captured_) {
        if (e.type == SDL_KEYDOWN && !e.key.repeat && mapper_)
            pending_buttons_ |= mapper_->LogicalForScancode(e.key.keysym.scancode);
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button >= SDL_BUTTON_LEFT &&
            e.button.button <= SDL_BUTTON_RIGHT)
            pending_mouse_ |= SDL_BUTTON(e.button.button);
    }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        if (focused_ && !ui_captured_ && SDL_GetRelativeMouseMode() == SDL_FALSE) SDL_SetRelativeMouseMode(SDL_TRUE);
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
        if (SDL_GetRelativeMouseMode() == SDL_TRUE) SDL_SetRelativeMouseMode(SDL_FALSE);
    }
}

void KeyboardMouseBackend::Poll(NormalizedInput& out) {
    int numkeys = 0;
    const Uint8* keys = SDL_GetKeyboardState(&numkeys);

    // Merge the real key state with the injection overlay; the overlay is
    // consumed by this Poll.
    const Uint8* effective = keys;
    bool any_inject = false;
    for (int i = 0; i < kNumScancodes; i++)
        if (inject_[i]) { any_inject = true; break; }
    if (any_inject) {
        const int n = numkeys < kNumScancodes ? numkeys : kNumScancodes;
        if (keys) std::memcpy(merged_, keys, (size_t)n);
        else      std::memset(merged_, 0, sizeof(merged_));
        if (n < kNumScancodes) std::memset(merged_ + n, 0, (size_t)(kNumScancodes - n));
        for (int i = 0; i < kNumScancodes; i++)
            if (inject_[i]) merged_[i] = 1;
        effective = merged_;
        numkeys = kNumScancodes;
    }
    std::memset(inject_, 0, sizeof(inject_));

    if (focused_ && effective && mapper_) {
        out.buttons |= mapper_->ButtonsFromKeys(effective, numkeys);
        mapper_->KeyStickFromKeys(effective, numkeys, out.lx, out.ly);
    }

    int mdx = 0, mdy = 0;
    const Uint32 mb = SDL_GetRelativeMouseState(&mdx, &mdy) | pending_mouse_;
    if (!focused_) {
        ClearPending();
        return;
    }
    out.buttons |= pending_buttons_;
    pending_buttons_ = pending_mouse_ = 0;
    // Deltas feed the camera only while captured: an uncaptured mouse
    // crossing the window must not spin the view.
    if (SDL_GetRelativeMouseMode() == SDL_TRUE) {
        out.mouse_dx = (float)mdx;
        out.mouse_dy = (float)mdy;
    }
    out.mouse_buttons[0] = (mb & SDL_BUTTON_LMASK) != 0;
    out.mouse_buttons[1] = (mb & SDL_BUTTON_MMASK) != 0;
    out.mouse_buttons[2] = (mb & SDL_BUTTON_RMASK) != 0;
}

}  // namespace ps2r::platform::sdl
