// tests/input/test_input_mapper.cpp -- ctest InputMapper. The nine OLD cases
// against the real InputMapper + GamepadConvert: default key table, mouse
// accumulate/decay, digital left stick, INI parse + whole-file rejection,
// gamepad tables, StickToByte pins, held-stick invariance, full-deflection
// jitter, and the radial-deadzone + sensitivity stick semantics. No SDL_Init.
#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

#include "engine/platform/sdl/input/gamepad_convert.h"
#include "engine/platform/sdl/input/input_backend.h"
#include "engine/platform/sdl/input/input_mapper.h"
#include "engine/platform/sdl/input/pad_sample.h"

using namespace ps2r::platform::sdl;

static int g_checks = 0;
static int g_fails = 0;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        g_checks++;                                                         \
        if (!(cond)) {                                                      \
            g_fails++;                                                      \
            std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);   \
            std::printf(__VA_ARGS__);                                       \
            std::printf("\n");                                              \
        }                                                                   \
    } while (0)

// Synthetic SDL-style key-state array.
struct Keys {
    uint8_t k[SDL_NUM_SCANCODES] = {};
};

static void PressKey(Keys& ks, int sc) {
    if (sc > 0 && sc < SDL_NUM_SCANCODES) ks.k[sc] = 1;
}

// ---------------------------------------------------------------------------
// T1: key table -> LogicalButton bits; every default binding; unbound -> 0.
// ---------------------------------------------------------------------------
static void TestT1DefaultKeyTable() {
    InputMapper m;
    struct Case { const char* key; uint32_t bit; };
    static const Case kCases[] = {
        {"Space", kBtnCross}, {"E", kBtnTriangle}, {"Y", kBtnStart},
        {"1", kBtnL1},        {"2", kBtnL2},       {"3", kBtnR1},
        {"4", kBtnR2},        {"5", kBtnL3},       {"6", kBtnR3},
    };
    for (const auto& c : kCases) {
        const int sc = ScancodeFromName(c.key);
        CHECK(sc != 0, "scancode for %s", c.key);
        Keys ks;
        PressKey(ks, sc);
        const uint32_t got = m.ButtonsFromKeys(ks.k, SDL_NUM_SCANCODES);
        CHECK(got == c.bit, "%s -> got 0x%x want 0x%x", c.key, got, c.bit);
        CHECK(m.LogicalForScancode(sc) == c.bit, "LogicalForScancode(%s)", c.key);
    }
    {
        const int sc = ScancodeFromName("LShift");
        CHECK(sc == SDL_SCANCODE_LSHIFT, "LShift alias resolves (got %d)", sc);
        Keys ks;
        PressKey(ks, sc);
        CHECK(m.ButtonsFromKeys(ks.k, SDL_NUM_SCANCODES) == kBtnSquare, "LShift -> SQUARE");
    }
    {
        NormalizedInput in;
        in.mouse_buttons[2] = true;   // R (Mouse2 maps to index 2)
        const PadSample st = m.Map(in, 0.f);
        CHECK(st.buttons == kBtnCircle, "Mouse2 -> CIRCLE (got 0x%x)", st.buttons);
    }
    {
        Keys ks;
        PressKey(ks, SDL_SCANCODE_Z);
        CHECK(m.ButtonsFromKeys(ks.k, SDL_NUM_SCANCODES) == 0, "Z unbound");
    }
    {
        Keys ks;
        PressKey(ks, ScancodeFromName("Space"));
        PressKey(ks, ScancodeFromName("Y"));
        const uint32_t got = m.ButtonsFromKeys(ks.k, SDL_NUM_SCANCODES);
        CHECK(got == (kBtnCross | kBtnStart), "Space+Y OR (got 0x%x)", got);
    }
}

// ---------------------------------------------------------------------------
// T2: mouse accumulate/decay via Map(): monotonic approach to the clamp,
//     decay to 0x80 exactly, sensitivity linearity, no drift after settle.
// ---------------------------------------------------------------------------
static void TestT2MouseAccumDecay() {
    const float dt = 1.f / 60.f;
    {
        // dx = 200 makes the accumulate/decay equilibrium exceed +1 (clamp).
        InputMapper m;
        uint8_t prev = 0x80, last = 0x80;
        bool monotonic = true;
        for (int i = 0; i < 120; i++) {
            NormalizedInput in;
            in.mouse_dx = 200.f;
            const PadSample st = m.Map(in, dt);
            if (st.rx < prev) monotonic = false;
            prev = st.rx;
            last = st.rx;
        }
        CHECK(monotonic, "rx monotonic under sustained +dx");
        CHECK(last == 0xFF, "rx clamps at 0xFF (got 0x%02x)", last);
    }
    {
        InputMapper m;
        for (int i = 0; i < 30; i++) {
            NormalizedInput in;
            in.mouse_dx = 40.f;
            in.mouse_dy = -25.f;
            m.Map(in, dt);
        }
        int settled_at = -1;
        PadSample st{};
        for (int i = 0; i < 60; i++) {
            NormalizedInput in;
            st = m.Map(in, dt);
            if (settled_at < 0 && st.rx == 0x80 && st.ry == 0x80) settled_at = i;
        }
        CHECK(settled_at >= 0, "decays to 0x80/0x80 within 60 idle frames");
        for (int i = 0; i < 240; i++) {
            NormalizedInput in;
            st = m.Map(in, dt);
        }
        CHECK(st.rx == 0x80 && st.ry == 0x80, "no drift after settle (rx=0x%02x ry=0x%02x)", st.rx, st.ry);
    }
    {
        InputMapper a, b2;
        NormalizedInput i1;
        i1.mouse_dx = 100.f;
        NormalizedInput i2;
        i2.mouse_dx = 200.f;
        const PadSample s1 = a.Map(i1, 0.f);
        const PadSample s2 = b2.Map(i2, 0.f);
        const int d1 = (int)s1.rx - 0x80;
        const int d2 = (int)s2.rx - 0x80;
        CHECK(d1 > 0, "positive deflection (d1=%d)", d1);
        CHECK(std::abs(d2 - 2 * d1) <= 1, "linear: d1=%d d2=%d", d1, d2);
        // 100 px at 0.35 => accum 0.35 => byte round(0.35*127.5+127.5) = 172.
        CHECK(s1.rx == 172, "100px @0.35 => 0.35 units (rx=%d want 172)", s1.rx);
    }
    {
        InputMapper m;
        m.SetMouseSensitivity(0.70f);
        NormalizedInput in;
        in.mouse_dx = 100.f;
        const PadSample st = m.Map(in, 0.f);
        const int d = (int)st.rx - 0x80;
        CHECK(std::abs(d - 89) <= 1, "0.70 sens => ~0.70 units (d=%d)", d);
    }
    {
        InputMapper m;
        NormalizedInput in;
        in.mouse_dx = 100.f;
        const PadSample st = m.Map(in, 0.f);
        CHECK(st.lx == 0x80 && st.ly == 0x80, "left stick untouched by mouse");
        CHECK(st.rx != 0x80, "right stick moved by mouse");
    }
}

// ---------------------------------------------------------------------------
// T3: digital left stick: W => ly 0x00, S => 0xFF, W+S => 0x80; diagonals.
// ---------------------------------------------------------------------------
static void TestT3WasdStick() {
    InputMapper m;
    auto stick = [&](std::initializer_list<const char*> keys) {
        Keys ks;
        for (const char* k : keys) PressKey(ks, ScancodeFromName(k));
        float lx = 0, ly = 0;
        m.KeyStickFromKeys(ks.k, SDL_NUM_SCANCODES, lx, ly);
        NormalizedInput in;
        in.lx = lx;
        in.ly = ly;
        return m.Map(in, 0.f);
    };
    PadSample st;
    st = stick({"W"});      CHECK(st.ly == 0x00, "W => ly 0x00 (got 0x%02x)", st.ly);
    st = stick({"S"});      CHECK(st.ly == 0xFF, "S => ly 0xFF (got 0x%02x)", st.ly);
    st = stick({"W", "S"}); CHECK(st.ly == 0x80, "W+S => 0x80 (got 0x%02x)", st.ly);
    st = stick({"A"});      CHECK(st.lx == 0x00, "A => lx 0x00 (got 0x%02x)", st.lx);
    st = stick({"D"});      CHECK(st.lx == 0xFF, "D => lx 0xFF (got 0x%02x)", st.lx);
    st = stick({"A", "D"}); CHECK(st.lx == 0x80, "A+D => 0x80 (got 0x%02x)", st.lx);
    st = stick({"W", "D"});
    CHECK(st.ly == 0x00 && st.lx == 0xFF, "W+D diagonal (lx=0x%02x ly=0x%02x)", st.lx, st.ly);
    st = stick({});
    CHECK(st.lx == 0x80 && st.ly == 0x80, "no keys => neutral");
}

// ---------------------------------------------------------------------------
// T5: INI: default content parses; overrides; malformed -> whole file
//     rejected; missing file -> default kept.
// ---------------------------------------------------------------------------
static void TestT5IniParse() {
    {
        InputMapper m;
        const Bindings& b = m.Config();
        CHECK(b.key_buttons.size() == 15, "15 key bindings: 10 + select=Tab + dpad=arrows (got %zu)", b.key_buttons.size());
        CHECK(b.mouse_buttons.size() == 1, "1 mouse binding (got %zu)", b.mouse_buttons.size());
        CHECK(b.mouse_to_right_stick, "mouse = right");
        CHECK(std::fabs(b.mouse_sensitivity - 0.35f) < 1e-6, "sensitivity 0.35");
        CHECK(std::fabs(b.mouse_decay - 12.0f) < 1e-6, "decay 12.0");
        CHECK(b.key_left_up == SDL_SCANCODE_W && b.key_left_down == SDL_SCANCODE_S &&
              b.key_left_left == SDL_SCANCODE_A && b.key_left_right == SDL_SCANCODE_D,
              "WASD left-stick keys");
        CHECK(b.gamepad_enabled && std::fabs(b.gamepad_deadzone - 0.15f) < 1e-6, "gamepad enabled, deadzone 0.15");
        CHECK(std::fabs(b.gamepad_sensitivity - 1.33f) < 1e-6, "gamepad sensitivity 1.33");
    }
    {
        InputMapper m;
        const bool ok = m.LoadConfigText("[buttons]\ncross = K\n[sticks]\nmouse = left\n", "test-override");
        CHECK(ok, "valid override accepted");
        CHECK(m.Config().key_buttons.size() == 1, "override replaced table");
        CHECK(m.Config().key_buttons[0].first == SDL_SCANCODE_K, "cross = K");
        CHECK(!m.Config().mouse_to_right_stick, "mouse = left honored");
    }
    {
        InputMapper m;
        const size_t before = m.Config().key_buttons.size();
        CHECK(!m.LoadConfigText("[buttons]\ncross = NoSuchKeyName\n", "bad1"), "unresolvable key name rejected");
        CHECK(!m.LoadConfigText("[buttons]\nnot_a_button = Space\n", "bad2"), "unknown button rejected");
        CHECK(!m.LoadConfigText("[nonsense]\nfoo = bar\n", "bad3"), "unknown section rejected");
        CHECK(!m.LoadConfigText("cross = Space\n", "bad4"), "key=value before section rejected");
        CHECK(!m.LoadConfigText("[buttons]\ngarbage line\n", "bad5"), "no '=' rejected");
        CHECK(!m.LoadConfigText("[sticks]\nmouse_decay = fast\n", "bad6"), "non-numeric float rejected");
        CHECK(!m.LoadConfigText("[gamepad]\ndeadzone = 1.0\n", "bad7"), "deadzone >= 1 rejected");
        CHECK(!m.LoadConfigText("[gamepad]\nsensitivity = 2.5\n", "bad8"), "sensitivity > 2 rejected");
        CHECK(!m.LoadConfigText("[buttons\ncross = Space\n", "bad9"), "unterminated section rejected");
        CHECK(m.Config().key_buttons.size() == before, "default bindings intact after rejections");
        CHECK(m.LoadConfigText("; comment only\n\n# another\n[buttons]\nstart = Y ; trailing comment\n", "ok1"),
              "comments/blank lines accepted");
    }
    {
        InputMapper m;
        CHECK(!m.LoadConfig("/nonexistent/dir/bindings.ini"), "missing file");
        CHECK(m.Config().key_buttons.size() == 15, "default kept");
    }
}

// ---------------------------------------------------------------------------
// Gamepad tables (pure GamepadConvert; no pad required)
// ---------------------------------------------------------------------------
static void TestGamepadTables() {
    const float dz = 0.15f;
    struct Case { int sdl; uint32_t bit; const char* name; };
    static const Case kCases[] = {
        {SDL_CONTROLLER_BUTTON_A, kBtnCross, "A->CROSS"},
        {SDL_CONTROLLER_BUTTON_B, kBtnCircle, "B->CIRCLE"},
        {SDL_CONTROLLER_BUTTON_X, kBtnSquare, "X->SQUARE"},
        {SDL_CONTROLLER_BUTTON_Y, kBtnTriangle, "Y->TRIANGLE"},
        {SDL_CONTROLLER_BUTTON_BACK, kBtnSelect, "BACK->SELECT"},
        {SDL_CONTROLLER_BUTTON_START, kBtnStart, "START->START"},
        {SDL_CONTROLLER_BUTTON_LEFTSTICK, kBtnL3, "LS->L3"},
        {SDL_CONTROLLER_BUTTON_RIGHTSTICK, kBtnR3, "RS->R3"},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, kBtnL1, "LB->L1"},
        {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, kBtnR1, "RB->R1"},
        {SDL_CONTROLLER_BUTTON_DPAD_UP, kBtnUp, "DUP->UP"},
        {SDL_CONTROLLER_BUTTON_DPAD_DOWN, kBtnDown, "DDOWN->DOWN"},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT, kBtnLeft, "DLEFT->LEFT"},
        {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, kBtnRight, "DRIGHT->RIGHT"},
    };
    for (const auto& c : kCases) {
        GamepadRaw raw;
        raw.button[c.sdl] = 1;
        NormalizedInput out;
        GamepadConvert::Convert(raw, dz, out);
        CHECK(out.buttons == c.bit, "%s (got 0x%x want 0x%x)", c.name, out.buttons, c.bit);
    }
    {
        GamepadRaw raw;
        raw.axis[SDL_CONTROLLER_AXIS_TRIGGERLEFT] = 32767 / 2;
        NormalizedInput out;
        GamepadConvert::Convert(raw, dz, out);
        CHECK(out.buttons == kBtnL2, "half trigger L -> L2 (0x%x)", out.buttons);
    }
    {
        GamepadRaw raw;
        raw.axis[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 32767 / 8;   // below 25 %
        NormalizedInput out;
        GamepadConvert::Convert(raw, dz, out);
        CHECK(out.buttons == 0, "1/8 trigger R -> nothing (0x%x)", out.buttons);
    }
    {
        GamepadRaw raw;
        raw.axis[SDL_CONTROLLER_AXIS_LEFTX] = (int16_t)(0.10f * 32767);   // < dz
        NormalizedInput out;
        GamepadConvert::Convert(raw, dz, out);
        CHECK(out.lx == 0.f, "inside deadzone -> 0 (%f)", out.lx);
    }
    {
        GamepadRaw raw;
        raw.axis[SDL_CONTROLLER_AXIS_LEFTX] = 32767;
        raw.axis[SDL_CONTROLLER_AXIS_LEFTY] = -32768;
        NormalizedInput out;
        GamepadConvert::Convert(raw, dz, out);
        CHECK(std::fabs(out.lx - 1.f) < 1e-4, "full deflect +1 (%f)", out.lx);
        CHECK(out.ly <= -0.999f, "full deflect -1 (%f)", out.ly);
    }
    {
        // Just past the deadzone edge: no rescale from the edge; the value
        // passes through scaled by the sensitivity (1.33).
        GamepadRaw raw;
        raw.axis[SDL_CONTROLLER_AXIS_RIGHTX] = (int16_t)(0.16f * 32767);
        NormalizedInput out;
        GamepadConvert::Convert(raw, dz, out);
        CHECK(std::fabs(out.rx - 0.16f * 1.33f) < 0.01f, "edge pass-through x1.33 (%f)", out.rx);
    }
    {
        InputMapper m;
        GamepadRaw raw;
        raw.axis[SDL_CONTROLLER_AXIS_LEFTY] = 32767;   // full down
        raw.button[SDL_CONTROLLER_BUTTON_A] = 1;
        NormalizedInput out;
        GamepadConvert::Convert(raw, dz, out);
        const PadSample st = m.Map(out, 0.f);
        CHECK(st.ly == 0xFF, "gamepad full-down -> ly 0xFF (0x%02x)", st.ly);
        CHECK(st.buttons == kBtnCross, "gamepad A -> CROSS in PadSample");
    }
}

// ---------------------------------------------------------------------------
// StickToByte pins
// ---------------------------------------------------------------------------
static void TestStickToByte() {
    CHECK(StickToByte(0.f) == 0x80, "neutral -> 0x80");
    CHECK(StickToByte(-1.f) == 0x00, "-1 -> 0x00");
    CHECK(StickToByte(+1.f) == 0xFF, "+1 -> 0xFF");
    CHECK(StickToByte(-2.f) == 0x00, "clamped low");
    CHECK(StickToByte(+2.f) == 0xFF, "clamped high");
}

// ---------------------------------------------------------------------------
// Held-stick invariance: a stick held at constant deflection maps to the
// SAME bytes on every frame -- no time-based decay touches gamepad axes.
// ---------------------------------------------------------------------------
static void TestHeldStickInvariance() {
    const float dt = 1.f / 60.f;
    {
        InputMapper m;
        int first_bad = -1;
        for (int i = 0; i < 600; i++) {
            NormalizedInput in;
            in.lx = +1.f; in.ly = -1.f; in.rx = -1.f; in.ry = +1.f;
            const PadSample st = m.Map(in, dt);
            if (!(st.lx == 0xFF && st.ly == 0x00 && st.rx == 0x00 && st.ry == 0xFF) && first_bad < 0) first_bad = i;
        }
        CHECK(first_bad < 0, "full deflection invariant across 600 held frames (first degraded frame %d)", first_bad);
    }
    {
        InputMapper m;
        const uint8_t want = StickToByte(0.5f);
        int first_bad = -1;
        for (int i = 0; i < 600; i++) {
            NormalizedInput in;
            in.lx = 0.5f;
            const PadSample st = m.Map(in, dt);
            if (st.lx != want && first_bad < 0) first_bad = i;
        }
        CHECK(first_bad < 0, "half deflection byte constant (0x%02x) across 600 held frames (first bad frame %d)",
              want, first_bad);
    }
    {
        InputMapper m;
        int first_bad = -1;
        for (int i = 0; i < 600; i++) {
            GamepadRaw raw;
            raw.axis[SDL_CONTROLLER_AXIS_LEFTX] = 32767;
            raw.axis[SDL_CONTROLLER_AXIS_LEFTY] = -32768;
            NormalizedInput in;
            GamepadConvert::Convert(raw, 0.15f, in);
            const PadSample st = m.Map(in, dt);
            if (!(st.lx == 0xFF && st.ly == 0x00) && first_bad < 0) first_bad = i;
        }
        CHECK(first_bad < 0, "GamepadConvert+Map full deflection invariant for 600 frames (first bad frame %d)",
              first_bad);
    }
}

// ---------------------------------------------------------------------------
// Full-deflection jitter floor: raw values slightly below the rail stay near
// full scale and the conversion is monotonic through the jitter band.
// ---------------------------------------------------------------------------
static void TestGamepadFullDeflectionJitter() {
    const float dz = 0.15f;
    static const int16_t kJitter[] = {32767, 32700, 32500, 32200, 32000};
    for (int16_t v : kJitter) {
        GamepadRaw raw;
        raw.axis[SDL_CONTROLLER_AXIS_LEFTX] = v;
        NormalizedInput out;
        GamepadConvert::Convert(raw, dz, out);
        CHECK(out.lx >= 0.95f, "raw %d stays near full (got %f)", v, out.lx);
    }
    static const int16_t kJitterNeg[] = {-32768, -32500, -32000};
    for (int16_t v : kJitterNeg) {
        GamepadRaw raw;
        raw.axis[SDL_CONTROLLER_AXIS_LEFTY] = v;
        NormalizedInput out;
        GamepadConvert::Convert(raw, dz, out);
        CHECK(out.ly <= -0.95f, "raw %d stays near full-negative (got %f)", v, out.ly);
    }
    {
        float prev = -2.f;
        bool monotonic = true;
        for (int v = 30000; v <= 32700; v += 300) {
            GamepadRaw raw;
            raw.axis[SDL_CONTROLLER_AXIS_RIGHTX] = (int16_t)v;
            NormalizedInput out;
            GamepadConvert::Convert(raw, dz, out);
            if (out.rx < prev) monotonic = false;
            prev = out.rx;
        }
        CHECK(monotonic, "conversion monotonic through the jitter band");
    }
}

// ---------------------------------------------------------------------------
// Stick semantics: radial deadzone SNAP (no rescale) + per-axis
// scale-and-clamp (1.33): full tilt at any angle rails the dominant axis;
// 45 deg full tilt reaches ~(0.94, 0.94), not the square corner (1, 1).
// ---------------------------------------------------------------------------
static void FullTilt(float deg, GamepadRaw& raw) {
    const float kPi = 3.14159265358979f;
    const float rad = deg * kPi / 180.f;
    raw.axis[SDL_CONTROLLER_AXIS_LEFTX] = (int16_t)lroundf(std::cos(rad) * 32767.f);
    raw.axis[SDL_CONTROLLER_AXIS_LEFTY] = (int16_t)lroundf(std::sin(rad) * 32767.f);
}

static void TestStickSemantics() {
    const float dz = 0.15f;
    const float kPi = 3.14159265358979f;

    for (int deg = 0; deg < 360; deg += 5) {
        GamepadRaw raw;
        FullTilt((float)deg, raw);
        NormalizedInput out;
        GamepadConvert::Convert(raw, dz, out);
        const float len = std::sqrt(out.lx * out.lx + out.ly * out.ly);
        CHECK(len >= 0.995f, "full tilt @%d deg vector length >= 1 (len=%f)", deg, len);
    }
    {
        GamepadRaw raw;
        FullTilt(-85.f, raw);
        NormalizedInput out;
        GamepadConvert::Convert(raw, dz, out);
        CHECK(out.ly <= -0.999f, "5 deg drift off cardinal keeps full speed (ly=%f)", out.ly);
    }
    {
        GamepadRaw raw;
        FullTilt(45.f, raw);
        NormalizedInput out;
        GamepadConvert::Convert(raw, dz, out);
        CHECK(out.lx > 0.92f && out.lx < 0.96f && out.ly > 0.92f && out.ly < 0.96f,
              "45 deg full tilt == 0.94 not square 1.0 (%f, %f)", out.lx, out.ly);
    }
    {
        GamepadRaw raw;
        const float rad = 30.f * kPi / 180.f;
        raw.axis[SDL_CONTROLLER_AXIS_LEFTX] = (int16_t)lroundf(std::cos(rad) * 0.6f * 32767.f);
        raw.axis[SDL_CONTROLLER_AXIS_LEFTY] = (int16_t)lroundf(std::sin(rad) * 0.6f * 32767.f);
        NormalizedInput out;
        GamepadConvert::Convert(raw, dz, out);
        const float in_ang = std::atan2(std::sin(rad), std::cos(rad));
        const float out_ang = std::atan2(out.ly, out.lx);
        CHECK(std::fabs(in_ang - out_ang) < 0.01f, "angle preserved at partial tilt (in=%f out=%f)", in_ang, out_ang);
        const float len = std::sqrt(out.lx * out.lx + out.ly * out.ly);
        CHECK(std::fabs(len - 0.6f * 1.33f) < 0.01f, "partial tilt scales x1.33 pass-through (len=%f)", len);
    }
    {
        GamepadRaw raw;
        const float c = 0.09f * 32767.f;   // vector magnitude ~0.127 < 0.15
        raw.axis[SDL_CONTROLLER_AXIS_LEFTX] = (int16_t)lroundf(c);
        raw.axis[SDL_CONTROLLER_AXIS_LEFTY] = (int16_t)lroundf(c);
        NormalizedInput out;
        GamepadConvert::Convert(raw, dz, out);
        CHECK(out.lx == 0.f && out.ly == 0.f, "radial deadzone snap (%f, %f)", out.lx, out.ly);
    }
    {
        float prev = -1.f;
        for (int pct = 20; pct <= 100; pct += 10) {
            GamepadRaw raw;
            const float c = 0.70710678f * (pct / 100.f) * 32767.f;
            raw.axis[SDL_CONTROLLER_AXIS_LEFTX] = (int16_t)lroundf(c);
            raw.axis[SDL_CONTROLLER_AXIS_LEFTY] = (int16_t)lroundf(c);
            NormalizedInput out;
            GamepadConvert::Convert(raw, dz, out);
            const float len = std::sqrt(out.lx * out.lx + out.ly * out.ly);
            CHECK(len >= prev - 1e-4f, "tilt->speed monotonic @45deg (%d%%: %f after %f)", pct, len, prev);
            prev = len;
        }
        CHECK(prev >= 1.3f, "100%% tilt @45deg reaches 1.33x vector (%f)", prev);
    }
    {
        GamepadRaw raw;
        const float rad = -85.f * kPi / 180.f;
        raw.axis[SDL_CONTROLLER_AXIS_RIGHTX] = (int16_t)lroundf(std::cos(rad) * 32767.f);
        raw.axis[SDL_CONTROLLER_AXIS_RIGHTY] = (int16_t)lroundf(std::sin(rad) * 32767.f);
        NormalizedInput out;
        GamepadConvert::Convert(raw, dz, out);
        CHECK(out.ry <= -0.999f, "right stick 5 deg drift keeps full speed (ry=%f)", out.ry);
    }
    {
        InputMapper m;
        GamepadRaw raw;
        FullTilt(45.f, raw);
        NormalizedInput out;
        GamepadConvert::Convert(raw, dz, out);
        const PadSample st = m.Map(out, 0.f);
        CHECK(st.lx >= 0xF4 && st.lx <= 0xFA && st.ly >= 0xF4 && st.ly <= 0xFA,
              "45 deg full tilt -> ~0xF7 bytes (lx=0x%02x ly=0x%02x)", st.lx, st.ly);
    }
    {
        GamepadRaw raw;
        FullTilt(45.f, raw);
        NormalizedInput out;
        GamepadConvert::Convert(raw, dz, out, 1.0f);
        CHECK(std::fabs(out.lx - 0.7071f) < 0.01f && std::fabs(out.ly - 0.7071f) < 0.01f,
              "sens 1.0 = raw circle (%f, %f)", out.lx, out.ly);
    }
}

// ---------------------------------------------------------------------------
// PadSample <-> InputState: the ONE active-low inversion, neutral maps to
// wire 0xFFFF / 0x80 x4, and the round trip is the identity.
// ---------------------------------------------------------------------------
static void TestPadSampleWire() {
    const ps2r::platform::InputState neutral = ToInputState(PadSample{});
    CHECK(neutral.wire_buttons == 0xFFFF && neutral.lx == 0x80 && neutral.ly == 0x80 && neutral.rx == 0x80 &&
              neutral.ry == 0x80,
          "neutral wire 0xFFFF/0x80 (got 0x%04x)", neutral.wire_buttons);
    PadSample s;
    s.buttons = (uint16_t)(kBtnCross | kBtnStart);
    s.lx = 0x00; s.ly = 0xFF; s.rx = 0x12; s.ry = 0xEE;
    const ps2r::platform::InputState w = ToInputState(s);
    CHECK(w.wire_buttons == (uint16_t)~(kBtnCross | kBtnStart), "wire = ~buttons (0x%04x)", w.wire_buttons);
    CHECK(w.lx == 0x00 && w.ly == 0xFF && w.rx == 0x12 && w.ry == 0xEE, "axes copied");
    const PadSample back = FromInputState(w);
    CHECK(back.buttons == s.buttons && back.lx == s.lx && back.ly == s.ly && back.rx == s.rx && back.ry == s.ry,
          "round trip identity");
}

// ---------------------------------------------------------------------------
// [pad] remap (CHARTER 2 A5): the default map is SDL's standard layout; a
// [pad] section swaps buttons / moves a trigger / unbinds; a bad control name
// rejects the whole file; the swapped map drives GamepadConvert.
// ---------------------------------------------------------------------------
static void TestPadRemap() {
    // Default == standard layout, named back exactly.
    PadButtonMap std_map;
    CHECK(PadButtonMap::ControlName(std_map.Control(14)) == "a", "bit 14 (cross) = a by default");
    CHECK(PadButtonMap::ControlName(std_map.Control(9)) == "righttrigger", "bit 9 (r2) = righttrigger by default");
    PadControl c;
    CHECK(PadButtonMap::ControlFromName("DPUP", c) && c.button == SDL_CONTROLLER_BUTTON_DPAD_UP, "names are case-insensitive");
    CHECK(PadButtonMap::ControlFromName("none", c) && !PadButtonMap::Bound(c), "none = unbound");
    CHECK(!PadButtonMap::ControlFromName("leftx", c), "a stick axis is not a button control");
    CHECK(!PadButtonMap::ControlFromName("button7", c), "an unknown name is rejected");

    InputMapper m;
    const bool ok = m.LoadConfigText(
        "[pad]\ncross = b\ncircle = a\nl2 = leftshoulder\nl1 = lefttrigger\nselect = none\n", "pad-remap");
    CHECK(ok, "[pad] section parses");
    const PadButtonMap& pm = m.Config().pad_map;
    CHECK(pm.Control(14).button == SDL_CONTROLLER_BUTTON_B, "cross -> b");
    CHECK(pm.Control(13).button == SDL_CONTROLLER_BUTTON_A, "circle -> a");
    CHECK(pm.Control(8).button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER && pm.Control(8).axis < 0, "l2 -> leftshoulder");
    CHECK(pm.Control(10).axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT && pm.Control(10).button < 0, "l1 -> lefttrigger");
    CHECK(!PadButtonMap::Bound(pm.Control(0)), "select unbound");
    CHECK(pm.Control(12).button == SDL_CONTROLLER_BUTTON_Y, "an omitted row (triangle) keeps the standard layout");

    // The swapped map drives the conversion: SDL B now presses Cross, the left
    // shoulder presses L2, the left trigger presses L1, Back presses nothing.
    GamepadRaw raw;
    raw.button[SDL_CONTROLLER_BUTTON_B] = 1;
    raw.button[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = 1;
    raw.button[SDL_CONTROLLER_BUTTON_BACK] = 1;
    raw.axis[SDL_CONTROLLER_AXIS_TRIGGERLEFT] = 32767;
    NormalizedInput out;
    GamepadConvert::Convert(raw, 0.15f, out, 1.33f, pm);
    CHECK(out.buttons == (kBtnCross | kBtnL2 | kBtnL1), "swapped map: B/LS/LT/Back -> cross|l2|l1 (got 0x%04x)", out.buttons);
    NormalizedInput std_out;
    GamepadConvert::Convert(raw, 0.15f, std_out);
    CHECK(std_out.buttons == (kBtnCircle | kBtnL1 | kBtnSelect | kBtnL2), "standard map unchanged (got 0x%04x)", std_out.buttons);

    // Whole-file rejection on a bad control name / a bad button name.
    InputMapper r;
    const std::string before_name = PadButtonMap::ControlName(r.Config().pad_map.Control(14));
    CHECK(!r.LoadConfigText("[pad]\ncross = b\nsquare = button7\n", "bad-control"), "bad control name rejects the file");
    CHECK(!r.LoadConfigText("[pad]\nfoo = a\n", "bad-button"), "bad ps2 button name rejects the file");
    CHECK(PadButtonMap::ControlName(r.Config().pad_map.Control(14)) == before_name, "bindings untouched after rejection");
}

int main() {
    TestT1DefaultKeyTable();
    TestT2MouseAccumDecay();
    TestT3WasdStick();
    TestT5IniParse();
    TestGamepadTables();
    TestStickToByte();
    TestHeldStickInvariance();
    TestGamepadFullDeflectionJitter();
    TestStickSemantics();
    TestPadSampleWire();
    TestPadRemap();
    std::printf("test_input_mapper: %d/%d checks passed%s\n", g_checks - g_fails, g_checks,
                g_fails ? "" : " / ALL PASS");
    return g_fails ? 1 : 0;
}
