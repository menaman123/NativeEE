// platform/sdl/input/pad_sample.h -- the host half's output: one DualShock 2
// sample in ACTIVE-HIGH logical form (LogicalButton bits) plus the four analog
// bytes. The pad protocol (engine/pad) stamps the wire form: the free
// functions below perform the ONE active-low inversion between the two.
#pragma once

#include <cstdint>

#include "engine/platform/platform.h"

namespace ps2r::platform::sdl {

struct PadSample {
    uint16_t buttons = 0;             // ACTIVE HIGH LogicalButton bits (wire order)
    uint8_t  lx = 0x80, ly = 0x80;    // 0x00..0xFF, neutral 0x80
    uint8_t  rx = 0x80, ry = 0x80;
    // Pressure bytes are not sampled: the pad is an analog-locked DualShock 2
    // whose pressure bytes read zero.
};

// Wire form: DualShock 2 buttons are ACTIVE LOW on the wire (a released pad
// reads 0xFFFF), the analog bytes are copied. Neutral PadSample{} maps to the
// neutral InputState{0xFFFF, 0x80 x4}.
platform::InputState ToInputState(const PadSample& s);
PadSample            FromInputState(const platform::InputState& w);

}  // namespace ps2r::platform::sdl
