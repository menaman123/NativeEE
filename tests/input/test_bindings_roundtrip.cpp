// tests/input/test_bindings_roundtrip.cpp -- ctest BindingsRoundtrip. The
// default_bindings.ini (argv[1]) -> Bindings -> a test-local serializer (the
// engine module has no serializer and keeps SDL_GetScancodeFromName as its
// only SDL call; this file names keys through SDL_GetScancodeName) -> parsed
// again -> identical Bindings. A malformed line rejects the whole file and
// leaves the current Bindings untouched.
#include <SDL.h>

#include <cstdio>
#include <string>

#include "engine/platform/sdl/input/input_backend.h"
#include "engine/platform/sdl/input/input_mapper.h"

using namespace ps2r::platform::sdl;

static int g_fails = 0;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            g_fails++;                                                      \
            std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);   \
            std::printf(__VA_ARGS__);                                       \
            std::printf("\n");                                              \
        }                                                                   \
    } while (0)

static const char* ButtonName(uint32_t bit) {
    switch (bit) {
    case kBtnSelect: return "select";     case kBtnL3: return "l3";
    case kBtnR3: return "r3";             case kBtnStart: return "start";
    case kBtnUp: return "up";             case kBtnRight: return "right";
    case kBtnDown: return "down";         case kBtnLeft: return "left";
    case kBtnL2: return "l2";             case kBtnR2: return "r2";
    case kBtnL1: return "l1";             case kBtnR1: return "r1";
    case kBtnTriangle: return "triangle"; case kBtnCircle: return "circle";
    case kBtnCross: return "cross";       case kBtnSquare: return "square";
    default: return nullptr;
    }
}

// Inverse of ParseMouseName: 0 -> Mouse1 (left), 2 -> Mouse2 (right), 1 -> Mouse3 (middle).
static const char* MouseName(int index) {
    switch (index) {
    case 0: return "Mouse1";
    case 2: return "Mouse2";
    case 1: return "Mouse3";
    default: return nullptr;
    }
}

static std::string FloatText(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.9g", (double)v);
    return buf;
}

// Emits the grammar input_mapper.cpp parses; key names come from SDL.
static std::string SerializeBindings(const Bindings& b) {
    std::string out;
    out += "[sticks]\n";
    out += std::string("mouse = ") + (b.mouse_to_right_stick ? "right" : "left") + "\n";
    out += "mouse_sensitivity = " + FloatText(b.mouse_sensitivity) + "\n";
    out += "mouse_decay = " + FloatText(b.mouse_decay) + "\n";
    if (b.key_left_up)    out += std::string("key_left_up = ") + SDL_GetScancodeName((SDL_Scancode)b.key_left_up) + "\n";
    if (b.key_left_down)  out += std::string("key_left_down = ") + SDL_GetScancodeName((SDL_Scancode)b.key_left_down) + "\n";
    if (b.key_left_left)  out += std::string("key_left_left = ") + SDL_GetScancodeName((SDL_Scancode)b.key_left_left) + "\n";
    if (b.key_left_right) out += std::string("key_left_right = ") + SDL_GetScancodeName((SDL_Scancode)b.key_left_right) + "\n";
    out += "[buttons]\n";
    for (const auto& kb : b.key_buttons) {
        const char* name = ButtonName(kb.second);
        if (!name) continue;
        out += std::string(name) + " = " + SDL_GetScancodeName((SDL_Scancode)kb.first) + "\n";
    }
    for (const auto& mb : b.mouse_buttons) {
        const char* name = ButtonName(mb.second);
        const char* mouse = MouseName(mb.first);
        if (!name || !mouse) continue;
        out += std::string(name) + " = " + mouse + "\n";
    }
    out += "[gamepad]\n";
    out += std::string("enabled = ") + (b.gamepad_enabled ? "1" : "0") + "\n";
    out += "deadzone = " + FloatText(b.gamepad_deadzone) + "\n";
    out += "sensitivity = " + FloatText(b.gamepad_sensitivity) + "\n";
    out += "[pad]\n";
    for (int i = 0; i < PadButtonMap::kButtons; i++) {
        const char* name = ButtonName(1u << i);
        if (!name) continue;
        out += std::string(name) + " = " + PadButtonMap::ControlName(b.pad_map.Control(i)) + "\n";
    }
    return out;
}

static bool BindingsEqual(const Bindings& a, const Bindings& b) {
    return a.key_buttons == b.key_buttons && a.mouse_buttons == b.mouse_buttons &&
           a.key_left_up == b.key_left_up && a.key_left_down == b.key_left_down &&
           a.key_left_left == b.key_left_left && a.key_left_right == b.key_left_right &&
           a.mouse_to_right_stick == b.mouse_to_right_stick && a.mouse_sensitivity == b.mouse_sensitivity &&
           a.mouse_decay == b.mouse_decay && a.gamepad_enabled == b.gamepad_enabled &&
           a.gamepad_deadzone == b.gamepad_deadzone && a.gamepad_sensitivity == b.gamepad_sensitivity &&
           a.pad_map == b.pad_map;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: test_bindings_roundtrip <default_bindings.ini>\n");
        return 2;
    }

    // 1. The on-disk default equals the baked-in default.
    InputMapper baked;
    InputMapper from_file;
    CHECK(from_file.LoadConfig(argv[1]), "default_bindings.ini loads from %s", argv[1]);
    CHECK(BindingsEqual(baked.Config(), from_file.Config()), "baked default == on-disk default");
    CHECK(baked.Config().key_buttons.size() == 15 && baked.Config().mouse_buttons.size() == 1,
          "default table sizes: 15 keys = 10 + select=Tab + dpad=arrows, 1 mouse (%zu keys, %zu mouse)", baked.Config().key_buttons.size(),
          baked.Config().mouse_buttons.size());

    // 2. Serialize -> parse -> identical.
    const std::string ini = SerializeBindings(baked.Config());
    InputMapper reparsed;
    CHECK(reparsed.LoadConfigText(ini, "serialized"), "serialized default parses");
    CHECK(BindingsEqual(baked.Config(), reparsed.Config()), "serialize -> parse round trip identical");
    const std::string ini2 = SerializeBindings(reparsed.Config());
    CHECK(ini == ini2, "serialization is a fixed point");

    // 3. A non-default table round-trips too (left-stick mouse, all 16 buttons
    //    bound, a gamepad section off the defaults).
    {
        InputMapper m;
        const bool ok = m.LoadConfigText(
            "[sticks]\nmouse = left\nmouse_sensitivity = 0.5\nmouse_decay = 3.25\n"
            "key_left_up = Up\nkey_left_down = Down\nkey_left_left = Left\nkey_left_right = Right\n"
            "[buttons]\nselect = Tab\nl3 = Q\nr3 = R\nstart = Return\nup = I\nright = L\ndown = K\nleft = J\n"
            "l2 = Z\nr2 = X\nl1 = C\nr1 = V\ntriangle = Mouse3\ncircle = Mouse2\ncross = Mouse1\nsquare = LCtrl\n"
            "[gamepad]\nenabled = 0\ndeadzone = 0.25\nsensitivity = 1\n",
            "custom");
        CHECK(ok, "custom table parses");
        CHECK(m.Config().key_buttons.size() == 13 && m.Config().mouse_buttons.size() == 3, "custom table sizes");
        InputMapper r;
        CHECK(r.LoadConfigText(SerializeBindings(m.Config()), "custom-serialized"), "custom serialized parses");
        CHECK(BindingsEqual(m.Config(), r.Config()), "custom round trip identical");
    }

    // 4. Whole-file rejection leaves the current Bindings untouched.
    {
        InputMapper m;
        const std::string before = SerializeBindings(m.Config());
        CHECK(!m.LoadConfigText(ini + "cross = \n", "empty-value"), "empty value rejects the whole file");
        CHECK(SerializeBindings(m.Config()) == before, "bindings untouched after rejection");
        CHECK(!m.LoadConfigText("[buttons]\ncross = Space\ncross = NoSuchKey\n", "late-error"),
              "a malformed line after valid ones still rejects the whole file");
        CHECK(SerializeBindings(m.Config()) == before, "bindings untouched after late rejection");
        CHECK(m.LoadConfigText(ini, "ok"), "the good text still loads afterwards");
    }

    if (g_fails) {
        std::printf("test_bindings_roundtrip: FAIL (%d)\n", g_fails);
        return 1;
    }
    std::printf("test_bindings_roundtrip: PASS (default == file, serialize/parse fixed point, custom table, rejection)\n");
    return 0;
}
