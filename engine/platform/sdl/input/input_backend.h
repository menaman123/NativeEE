// platform/sdl/input/input_backend.h -- the device-facing input interface.
// A backend turns one host device class (keyboard+mouse, game controllers)
// into a NormalizedInput; the InputMapper turns NormalizedInput into the
// PadSample the pad protocol stamps. MAIN thread only (SDL's rule): Poll runs
// after the SDL_PollEvent drain of the same main-loop iteration. This header
// is SDL-free so the mapper and its tests need no SDL types.
#pragma once

#include <cstdint>

namespace ps2r::platform::sdl {

// Device-agnostic controls. Sticks are float [-1, +1], x right+, y DOWN+ so
// that +1 maps to the DualShock 2 byte 0xFF (down/right). Buttons are a
// bitmask of LogicalButton bits, ACTIVE HIGH; the single active-low
// inversion happens in ToInputState() at the wire boundary.
struct NormalizedInput {
    float    lx = 0.f, ly = 0.f;      // left stick
    float    rx = 0.f, ry = 0.f;      // right stick
    uint32_t buttons = 0;             // bit N = LogicalButton N pressed
    float    mouse_dx = 0.f;          // relative mouse motion this poll,
    float    mouse_dy = 0.f;          //   in pixels (keyboard/mouse backend only)
    bool     mouse_buttons[3] = {};   // L, M, R
};

// One bit per DualShock 2 control, in the WIRE bit order of the two button
// bytes the pad returns (byte 3 bits 0..7, byte 4 bits 0..7), so the wire word
// is a single bitwise NOT of this mask.
enum LogicalButton : uint32_t {
    kBtnSelect   = 1u << 0,  kBtnL3     = 1u << 1,
    kBtnR3       = 1u << 2,  kBtnStart  = 1u << 3,
    kBtnUp       = 1u << 4,  kBtnRight  = 1u << 5,
    kBtnDown     = 1u << 6,  kBtnLeft   = 1u << 7,
    kBtnL2       = 1u << 8,  kBtnR2     = 1u << 9,
    kBtnL1       = 1u << 10, kBtnR1     = 1u << 11,
    kBtnTriangle = 1u << 12, kBtnCircle = 1u << 13,
    kBtnCross    = 1u << 14, kBtnSquare = 1u << 15,
};

class InputBackend {
public:
    virtual ~InputBackend() = default;
    // Once per main-loop iteration, MAIN thread, after SDL_PollEvent drained
    // the queue. Merges INTO `out` (buttons OR, sticks written when the
    // backend owns them).
    virtual void Poll(NormalizedInput& out) = 0;
    // True when the backend currently has a usable device.
    virtual bool IsActive() const = 0;
    virtual const char* Name() const = 0;
};

}  // namespace ps2r::platform::sdl
