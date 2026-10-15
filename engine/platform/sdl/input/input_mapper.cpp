// platform/sdl/input/input_mapper.cpp
#include "engine/platform/sdl/input/input_mapper.h"

#include <SDL.h>   // SDL_GetScancodeFromName only; no SDL_Init needed

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "engine/platform/sdl/input/input_backend.h"
#include "engine/platform/sdl/input/pad_sample.h"
#include "input_default_bindings.h"   // GENERATED at configure time from default_bindings.ini

namespace ps2r::platform::sdl {

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// strtof must consume the whole value.
bool ParseFloatStrict(const std::string& s, float& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    if (!end || *end != '\0') return false;
    out = v;
    return true;
}

const struct { const char* name; uint32_t bit; } kButtonNames[] = {
    {"select", kBtnSelect}, {"l3", kBtnL3}, {"r3", kBtnR3}, {"start", kBtnStart},
    {"up", kBtnUp}, {"right", kBtnRight}, {"down", kBtnDown}, {"left", kBtnLeft},
    {"l2", kBtnL2}, {"r2", kBtnR2}, {"l1", kBtnL1}, {"r1", kBtnR1},
    {"triangle", kBtnTriangle}, {"circle", kBtnCircle},
    {"cross", kBtnCross}, {"square", kBtnSquare},
};

bool ButtonBitFromName(const std::string& name, uint32_t& bit) {
    const std::string low = ToLower(name);
    for (const auto& b : kButtonNames)
        if (low == b.name) { bit = b.bit; return true; }
    return false;
}

// Flat [section] blocks of `key = value`; ';' and '#' start a comment anywhere
// on the line; ANY malformed line or unresolvable name rejects the whole file
// and `out` is left untouched.
bool ParseIni(const std::string& text, Bindings& out, const char* origin) {
    Bindings b;   // parse into a fresh struct; commit only on full success
    enum Section { kNone, kSticks, kButtons, kGamepad, kPad } sec = kNone;

    auto fail = [origin](int lineno, const std::string& line) {
        std::fprintf(stderr, "input: %s: malformed line %d ('%s') -- bindings unchanged\n",
                     origin, lineno, line.c_str());
        return false;
    };

    std::istringstream is(text);
    std::string raw;
    int lineno = 0;
    while (std::getline(is, raw)) {
        lineno++;
        const size_t c1 = raw.find(';');
        const size_t c2 = raw.find('#');
        const size_t cut = std::min(c1 == std::string::npos ? raw.size() : c1,
                                    c2 == std::string::npos ? raw.size() : c2);
        const std::string line = Trim(raw.substr(0, cut));
        if (line.empty()) continue;

        if (line.front() == '[') {
            if (line.back() != ']') return fail(lineno, line);
            const std::string name = ToLower(Trim(line.substr(1, line.size() - 2)));
            if      (name == "sticks")  sec = kSticks;
            else if (name == "buttons") sec = kButtons;
            else if (name == "gamepad") sec = kGamepad;
            else if (name == "pad")     sec = kPad;
            else return fail(lineno, line);
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) return fail(lineno, line);
        const std::string key = ToLower(Trim(line.substr(0, eq)));
        const std::string val = Trim(line.substr(eq + 1));
        if (key.empty() || val.empty()) return fail(lineno, line);

        switch (sec) {
        case kSticks: {
            if (key == "mouse") {
                const std::string v = ToLower(val);
                if (v == "right")     b.mouse_to_right_stick = true;
                else if (v == "left") b.mouse_to_right_stick = false;
                else return fail(lineno, line);
            } else if (key == "mouse_sensitivity") {
                if (!ParseFloatStrict(val, b.mouse_sensitivity)) return fail(lineno, line);
            } else if (key == "mouse_decay") {
                if (!ParseFloatStrict(val, b.mouse_decay)) return fail(lineno, line);
            } else if (key == "key_left_up" || key == "key_left_down" ||
                       key == "key_left_left" || key == "key_left_right") {
                const int sc = ScancodeFromName(val);
                if (sc == SDL_SCANCODE_UNKNOWN) return fail(lineno, line);
                if      (key == "key_left_up")   b.key_left_up = sc;
                else if (key == "key_left_down") b.key_left_down = sc;
                else if (key == "key_left_left") b.key_left_left = sc;
                else                             b.key_left_right = sc;
            } else {
                return fail(lineno, line);
            }
            break;
        }
        case kButtons: {
            uint32_t bit = 0;
            if (!ButtonBitFromName(key, bit)) return fail(lineno, line);
            const int mouse = ParseMouseName(val);
            if (mouse >= 0) {
                b.mouse_buttons.emplace_back(mouse, bit);
            } else {
                const int sc = ScancodeFromName(val);
                if (sc == SDL_SCANCODE_UNKNOWN) return fail(lineno, line);
                b.key_buttons.emplace_back(sc, bit);
            }
            break;
        }
        case kGamepad: {
            if (key == "enabled") {
                if (val == "1")      b.gamepad_enabled = true;
                else if (val == "0") b.gamepad_enabled = false;
                else return fail(lineno, line);
            } else if (key == "deadzone") {
                float d = 0.f;
                if (!ParseFloatStrict(val, d) || d < 0.f || d >= 1.f) return fail(lineno, line);
                b.gamepad_deadzone = d;
            } else if (key == "sensitivity") {
                float s = 0.f;
                if (!ParseFloatStrict(val, s) || s < 0.01f || s > 2.f) return fail(lineno, line);
                b.gamepad_sensitivity = s;
            } else {
                return fail(lineno, line);
            }
            break;
        }
        case kPad: {
            // [pad] <ps2 button> = <SDL control name | none>: the gamepad remap.
            uint32_t bit = 0;
            PadControl control;
            if (!ButtonBitFromName(key, bit) || !PadButtonMap::ControlFromName(val, control)) return fail(lineno, line);
            int index = 0;
            while (index < 15 && (1u << index) != bit) index++;
            b.pad_map.Set(index, control);
            break;
        }
        case kNone:
        default:
            return fail(lineno, line);   // key = value before any [section]
        }
    }

    out = b;
    return true;
}

}  // namespace

uint8_t StickToByte(float v) {
    if (v < -1.f) v = -1.f;
    if (v > +1.f) v = +1.f;
    long b = lroundf(v * 127.5f + 127.5f);   // -1 -> 0x00, 0 -> 0x80, +1 -> 0xFF
    if (b < 0)   b = 0;
    if (b > 255) b = 255;
    return (uint8_t)b;
}

int ScancodeFromName(const std::string& name) {
    // SDL's own names for the modifier keys are "Left Shift" etc.; the short
    // aliases are accepted case-insensitively like SDL itself.
    static const struct { const char* alias; const char* sdl; } kAliases[] = {
        {"lshift", "Left Shift"}, {"rshift", "Right Shift"},
        {"lctrl",  "Left Ctrl"},  {"rctrl",  "Right Ctrl"},
        {"lalt",   "Left Alt"},   {"ralt",   "Right Alt"},
    };
    const std::string low = ToLower(name);
    for (const auto& a : kAliases)
        if (low == a.alias) return SDL_GetScancodeFromName(a.sdl);
    return SDL_GetScancodeFromName(name.c_str());
}

int ParseMouseName(const std::string& name) {
    // NormalizedInput::mouse_buttons order is {L, M, R}; the config names
    // follow the common convention Mouse1 = left, Mouse2 = RIGHT, Mouse3 = middle.
    const std::string low = ToLower(name);
    if (low == "mouse1") return 0;
    if (low == "mouse2") return 2;
    if (low == "mouse3") return 1;
    return -1;
}

InputMapper::InputMapper() {
    // The baked-in default goes through the SAME parser, so the shipped
    // default is proven parseable at construction.
    if (!ParseIni(kDefaultBindingsIni, b_, "baked-in default"))
        std::fprintf(stderr, "input: baked-in default bindings failed to parse; input is inert\n");
}

bool InputMapper::LoadConfig(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;   // no file -> current bindings stay
    std::stringstream ss;
    ss << f.rdbuf();
    return LoadConfigText(ss.str(), path.c_str());
}

bool InputMapper::LoadConfigText(const std::string& text, const char* origin) {
    Bindings parsed;
    if (!ParseIni(text, parsed, origin)) return false;
    b_ = parsed;
    return true;
}

void InputMapper::SetMouseSensitivity(float s) { b_.mouse_sensitivity = s; }

void InputMapper::ResetMouseAccum() { accum_x_ = accum_y_ = 0.f; }

uint32_t InputMapper::ButtonsFromKeys(const uint8_t* keystate, int numkeys) const {
    uint32_t out = 0;
    if (!keystate) return 0;
    for (const auto& kb : b_.key_buttons)
        if (kb.first > 0 && kb.first < numkeys && keystate[kb.first]) out |= kb.second;
    return out;
}

void InputMapper::KeyStickFromKeys(const uint8_t* keystate, int numkeys, float& lx, float& ly) const {
    lx = 0.f;
    ly = 0.f;
    if (!keystate) return;
    auto down = [&](int sc) { return sc > 0 && sc < numkeys && keystate[sc] != 0; };
    // Digital-to-analog at full deflection; opposite keys cancel to centre.
    if (down(b_.key_left_left))  lx -= 1.f;
    if (down(b_.key_left_right)) lx += 1.f;
    if (down(b_.key_left_up))    ly -= 1.f;   // pad y: up = 0x00 = -1
    if (down(b_.key_left_down))  ly += 1.f;
}

uint32_t InputMapper::LogicalForScancode(int scancode) const {
    uint32_t out = 0;
    for (const auto& kb : b_.key_buttons)
        if (kb.first == scancode) out |= kb.second;
    return out;
}

PadSample InputMapper::Map(const NormalizedInput& in, float dt_seconds) {
    uint32_t btn = in.buttons;
    for (const auto& mb : b_.mouse_buttons)
        if (mb.first >= 0 && mb.first < 3 && in.mouse_buttons[mb.first]) btn |= mb.second;

    // Mouse delta -> stick: accumulate, then time-based exponential decay,
    // then clamp. Sensitivity is "stick units per 100 px", hence the 0.01.
    const float s = b_.mouse_sensitivity * 0.01f;
    accum_x_ += in.mouse_dx * s;
    accum_y_ += in.mouse_dy * s;
    if (dt_seconds > 0.f) {
        const float k = std::exp(-b_.mouse_decay * dt_seconds);
        accum_x_ *= k;
        accum_y_ *= k;
    }
    // Snap to neutral: the decay never reaches zero and lroundf of a tiny
    // negative residue yields 0x7F, so the sub-half-unit tail must become
    // exactly 0 for an idle stick to read 0x80.
    const float kSnap = 0.5f / 127.5f;
    if (accum_x_ > -kSnap && accum_x_ < kSnap) accum_x_ = 0.f;
    if (accum_y_ > -kSnap && accum_y_ < kSnap) accum_y_ = 0.f;
    auto clampf = [](float v) { return v < -1.f ? -1.f : (v > 1.f ? 1.f : v); };
    accum_x_ = clampf(accum_x_);
    accum_y_ = clampf(accum_y_);

    float lx = in.lx, ly = in.ly, rx = in.rx, ry = in.ry;
    if (b_.mouse_to_right_stick) {
        rx = clampf(rx + accum_x_);
        ry = clampf(ry + accum_y_);
    } else {
        lx = clampf(lx + accum_x_);
        ly = clampf(ly + accum_y_);
    }

    PadSample st;
    st.buttons = (uint16_t)(btn & 0xFFFF);
    st.lx = StickToByte(lx);
    st.ly = StickToByte(ly);
    st.rx = StickToByte(rx);
    st.ry = StickToByte(ry);
    return st;
}

}  // namespace ps2r::platform::sdl
