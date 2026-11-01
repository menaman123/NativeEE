// tests/app/test_controls_bindings.cpp -- ctest ControlsBindingsPressToBind (CHARTER 2 A5,
// Bernoulli): the launcher Controls page's model driven by SYNTHETIC events, no window, no pad:
// arm a pad row -> a controller button / trigger event (decoded exactly as the launcher decodes
// SDL's) lands the control, bindings.ini is written in the InputMapper grammar, the REAL
// InputMapper accepts the file and GamepadConvert presses the remapped PS2 button; the control
// moves off the row that had it; a stick axis / weak trigger / unarmed offer binds nothing;
// Escape cancels a key row; a reload reads the file back. Scratch dir = argv[1].
#include <SDL.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "app/host/controls_bindings.h"
#include "engine/platform/sdl/input/gamepad_convert.h"
#include "engine/platform/sdl/input/input_backend.h"
#include "engine/platform/sdl/input/input_mapper.h"
#include "engine/platform/host/platform.h"

using namespace ps2r::app;
using namespace ps2r::platform::sdl;

static int g_checks = 0, g_fails = 0;
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

static const std::string* RowValue(const std::vector<ControlsBindings::Row>& rows, const char* name) {
    for (const ControlsBindings::Row& r : rows)
        if (std::string(r.name) == name) return &r.value;
    return nullptr;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: test_controls_bindings <scratch dir>\n"); return 2; }
    const std::string root = argv[1];
    ps2r::host::MakeDirectory(root.c_str());
    std::remove((root + "/bindings.ini").c_str());

    ControlsBindings cb(root);
    CHECK(!cb.Load(), "no file yet -> defaults stay");
    CHECK(*RowValue(cb.Pad(), "cross") == "a" && *RowValue(cb.Pad(), "r2") == "righttrigger", "default pad rows = standard layout");
    CHECK(cb.Armed() == ControlsBindings::Target::None, "nothing armed at start");

    // The launcher's decode of SDL events (SDL-free ints here).
    CHECK(ControlsBindings::PadControlFromEvent(true, SDL_CONTROLLER_BUTTON_B, 0) == "b", "button event B -> b");
    CHECK(ControlsBindings::PadControlFromEvent(false, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 30000) == "lefttrigger", "trigger past 25 %%");
    CHECK(ControlsBindings::PadControlFromEvent(false, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 2000) == "", "weak trigger -> nothing");
    CHECK(ControlsBindings::PadControlFromEvent(false, SDL_CONTROLLER_AXIS_LEFTX, 32767) == "", "a stick axis -> nothing");
    CHECK(ControlsBindings::PadControlFromEvent(true, 99, 0) == "", "an out-of-range button -> nothing");

    // Unarmed: an offer binds nothing.
    CHECK(!cb.OfferPadControl("b"), "unarmed offer ignored");
    CHECK(*RowValue(cb.Pad(), "cross") == "a", "cross still a");

    // Arm cross (row 0 of the page order), press B: cross = b; circle (which had b) is unbound.
    cb.Arm(ControlsBindings::Target::Pad, 0);
    CHECK(cb.Armed() == ControlsBindings::Target::Pad && cb.ArmedRow() == 0, "cross armed");
    CHECK(!cb.OfferPadControl(ControlsBindings::PadControlFromEvent(false, SDL_CONTROLLER_AXIS_LEFTX, 32767)), "stick motion while armed binds nothing");
    CHECK(cb.Armed() == ControlsBindings::Target::Pad, "still armed after the stick");
    CHECK(cb.OfferPadControl(ControlsBindings::PadControlFromEvent(true, SDL_CONTROLLER_BUTTON_B, 0)), "B lands");
    CHECK(*RowValue(cb.Pad(), "cross") == "b", "cross = b");
    CHECK(*RowValue(cb.Pad(), "circle") == "none", "circle lost b (one control, one button)");
    CHECK(cb.Armed() == ControlsBindings::Target::None, "disarmed after the press");
    CHECK(cb.LastError().empty(), "saved: %s", cb.LastError().c_str());

    // Arm l1, pull the left trigger: l1 = lefttrigger; l2 (which had it) is unbound.
    int l1_row = -1;
    for (size_t i = 0; i < cb.Pad().size(); i++) if (std::string(cb.Pad()[i].name) == "l1") l1_row = (int)i;
    cb.Arm(ControlsBindings::Target::Pad, l1_row);
    CHECK(cb.OfferPadControl(ControlsBindings::PadControlFromEvent(false, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767)), "LT lands");
    CHECK(*RowValue(cb.Pad(), "l1") == "lefttrigger" && *RowValue(cb.Pad(), "l2") == "none", "l1 = lefttrigger, l2 unbound");

    // The camera: the stick rows exist with the DualShock 2 layout as the default, and the camera keys.
    CHECK(cb.Sticks().size() == 2 && *RowValue(cb.Sticks(), "move") == "left" && *RowValue(cb.Sticks(), "camera") == "right",
          "stick rows: move = left stick, camera = right stick by default");
    CHECK(!cb.CameraOnLeftStick() && cb.MouseIsCamera(), "the right stick and the mouse are the camera by default");
    CHECK(*RowValue(cb.Keys(), "key_right_up") == "I" && *RowValue(cb.Keys(), "key_right_down") == "K" &&
          *RowValue(cb.Keys(), "key_right_left") == "J" && *RowValue(cb.Keys(), "key_right_right") == "L", "camera keys I/K/J/L by default");
    CHECK(*RowValue(cb.Keys(), "key_left_up") == "W" && *RowValue(cb.Keys(), "select") == "Tab" && *RowValue(cb.Keys(), "up") == "Up",
          "move keys W/A/S/D, select = Tab, d-pad = arrows (the baked defaults)");
    CHECK(std::string(cb.Keys()[cb.Keys().size() - 4].label) == "Camera up (right stick)", "the camera rows carry a plain label");
    CHECK(ControlsBindings::PadStickFromEvent(SDL_CONTROLLER_AXIS_LEFTX, 32767) == "left", "left stick axis past half -> left");
    CHECK(ControlsBindings::PadStickFromEvent(SDL_CONTROLLER_AXIS_RIGHTY, -32767) == "right", "right stick axis (either way) -> right");
    CHECK(ControlsBindings::PadStickFromEvent(SDL_CONTROLLER_AXIS_RIGHTX, 8000) == "", "a small deflection -> nothing");
    CHECK(ControlsBindings::PadStickFromEvent(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767) == "", "a trigger -> no stick");
    CHECK(!cb.OfferPadStick("left"), "unarmed stick offer ignored");
    cb.Arm(ControlsBindings::Target::Stick, 1);   // the camera role
    CHECK(cb.Armed() == ControlsBindings::Target::Stick && cb.ArmedRow() == 1, "camera stick row armed");
    CHECK(!cb.OfferPadControl("a"), "a pad button is not offered to a stick row");
    CHECK(!cb.OfferPadStick(ControlsBindings::PadStickFromEvent(SDL_CONTROLLER_AXIS_LEFTX, 1000)), "a small deflection binds nothing");
    CHECK(cb.OfferPadStick(ControlsBindings::PadStickFromEvent(SDL_CONTROLLER_AXIS_LEFTY, -32767)), "the left stick lands on camera");
    CHECK(*RowValue(cb.Sticks(), "camera") == "left" && *RowValue(cb.Sticks(), "move") == "right" && cb.CameraOnLeftStick(),
          "camera = left stick, move took the right stick (two sticks, two roles)");
    CHECK(cb.Text().find("pad_camera = left") != std::string::npos, "bindings.ini carries [sticks] pad_camera = left:\n%s", cb.Text().c_str());
    {
        InputMapper ms;
        CHECK(ms.LoadConfig(cb.Path()), "InputMapper accepts the stick-swapped file");
        CHECK(ms.Config().pad_camera_left, "the mapper reads pad_camera = left from the written file");
        CHECK(ms.Config().key_right_up == ScancodeFromName("I") && ms.Config().key_left_up == ScancodeFromName("W"), "the stick keys round-trip");
    }
    // A camera key rebind: W (move up) onto camera up moves W off the move row -- one key, one control across both groups.
    int cam_up = -1;
    for (size_t i = 0; i < cb.Keys().size(); i++) if (std::string(cb.Keys()[i].name) == "key_right_up") cam_up = (int)i;
    cb.Arm(ControlsBindings::Target::Key, cam_up);
    CHECK(cb.OfferKey("W"), "W lands on camera up");
    CHECK(*RowValue(cb.Keys(), "key_right_up") == "W" && RowValue(cb.Keys(), "key_left_up")->empty(), "camera up = W, move up unbound");
    {
        InputMapper ms;
        CHECK(ms.LoadConfig(cb.Path()), "InputMapper accepts the file after the key move");
        CHECK(ms.Config().key_right_up == ScancodeFromName("W") && ms.Config().key_left_up == 0, "key_right_up = W, no key_left_up in the file");
    }
    cb.Arm(ControlsBindings::Target::Key, cam_up);
    CHECK(cb.OfferKey("I"), "I back on camera up");
    cb.Arm(ControlsBindings::Target::Stick, 0);
    CHECK(cb.OfferPadStick("left"), "move back to the left stick");
    CHECK(!cb.CameraOnLeftStick(), "camera back on the right stick");

    // Arming the armed row disarms it; Escape cancels a key row without a write.
    cb.Arm(ControlsBindings::Target::Key, 3);
    cb.Arm(ControlsBindings::Target::Key, 3);
    CHECK(cb.Armed() == ControlsBindings::Target::None, "arming the armed row disarms");
    cb.Arm(ControlsBindings::Target::Key, 3);
    CHECK(cb.OfferKey("Escape") && cb.Armed() == ControlsBindings::Target::None, "Escape cancels");
    CHECK(*RowValue(cb.Keys(), "triangle") == "E", "triangle key unchanged by Escape");
    cb.Arm(ControlsBindings::Target::Key, 3);
    CHECK(!cb.OfferPadControl("y"), "a pad control is not offered to a key row");
    CHECK(cb.OfferKey("T"), "T lands on triangle");
    CHECK(*RowValue(cb.Keys(), "triangle") == "T", "triangle = T");

    // The written file: the REAL InputMapper accepts it and the remap drives the conversion.
    InputMapper m;
    CHECK(m.LoadConfig(cb.Path()), "InputMapper accepts %s", cb.Path().c_str());
    const PadButtonMap& pm = m.Config().pad_map;
    GamepadRaw raw;
    raw.button[SDL_CONTROLLER_BUTTON_B] = 1;
    raw.axis[SDL_CONTROLLER_AXIS_TRIGGERLEFT] = 32767;
    NormalizedInput out;
    GamepadConvert::Convert(raw, 0.15f, out, 1.33f, pm);
    CHECK(out.buttons == (kBtnCross | kBtnL1), "B + LT -> cross|l1 through the written file (got 0x%04x)", out.buttons);
    const int sc_t = ScancodeFromName("T");
    CHECK(m.LogicalForScancode(sc_t) == kBtnTriangle, "T -> triangle through the written file");
    GamepadRaw raw_a;
    raw_a.button[SDL_CONTROLLER_BUTTON_A] = 1;
    NormalizedInput out_a;
    GamepadConvert::Convert(raw_a, 0.15f, out_a, 1.33f, pm);
    CHECK(out_a.buttons == 0, "A presses nothing now (cross moved to b; got 0x%04x)", out_a.buttons);

    // Reload reads the rows back.
    ControlsBindings again(root);
    CHECK(again.Load(), "reload reads the file");
    CHECK(*RowValue(again.Pad(), "cross") == "b" && *RowValue(again.Pad(), "circle") == "none" &&
          *RowValue(again.Pad(), "l1") == "lefttrigger" && *RowValue(again.Keys(), "triangle") == "T", "rows survive the round trip");
    CHECK(*RowValue(again.Keys(), "key_right_up") == "I" && RowValue(again.Keys(), "key_left_up")->empty() &&
          *RowValue(again.Sticks(), "camera") == "right", "the stick rows survive the round trip (camera up = I, move up unbound, camera = right stick)");
    // A hand-edited [sticks] section (pad_camera = left, mouse = left) is read back and kept by the next Save.
    {
        std::ofstream f(root + "/bindings.ini", std::ios::trunc);
        f << "[sticks]\nmouse = left\npad_camera = left\nkey_right_left = Keypad 4\n[buttons]\ncross = Space\n";
    }
    ControlsBindings edited(root);
    CHECK(edited.Load() && edited.CameraOnLeftStick() && !edited.MouseIsCamera() && *RowValue(edited.Keys(), "key_right_left") == "Keypad 4",
          "a hand-edited [sticks] section is read");
    CHECK(edited.Text().find("mouse = left") != std::string::npos && edited.Text().find("pad_camera = left") != std::string::npos,
          "...and written back");
    InputMapper me;
    CHECK(me.LoadConfigText(edited.Text(), "edited"), "the InputMapper accepts the rewritten text");

    std::printf("test_controls_bindings: %d/%d checks passed%s\n", g_checks - g_fails, g_checks, g_fails ? "" : " / ALL PASS");
    return g_fails ? 1 : 0;
}
