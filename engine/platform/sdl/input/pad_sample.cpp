// platform/sdl/input/pad_sample.cpp
#include "engine/platform/sdl/input/pad_sample.h"

namespace ps2r::platform::sdl {

platform::InputState ToInputState(const PadSample& s) {
    platform::InputState w;
    w.wire_buttons = (uint16_t)~s.buttons;   // the ONE active-low inversion
    w.lx = s.lx;
    w.ly = s.ly;
    w.rx = s.rx;
    w.ry = s.ry;
    return w;
}

PadSample FromInputState(const platform::InputState& w) {
    PadSample s;
    s.buttons = (uint16_t)~w.wire_buttons;
    s.lx = w.lx;
    s.ly = w.ly;
    s.rx = w.rx;
    s.ry = w.ry;
    return s;
}

}  // namespace ps2r::platform::sdl
