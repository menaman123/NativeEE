// tests/app/test_controls_bindings.cpp -- ctest ControlsBindingsPressToBind (CHARTER 2 A5,
// Bernoulli): the launcher Controls page's model driven by SYNTHETIC events, no window, no pad:
// arm a pad row -> a controller button / trigger event (decoded exactly as the launcher decodes
// SDL's) lands the control, bindings.ini is written in the InputMapper grammar, the REAL
// InputMapper accepts the file and GamepadConvert presses the remapped PS2 button; the control
// moves off the row that had it; a stick axis / weak trigger / unarmed offer binds nothing;
// Escape cancels a key row; a reload reads the file back. Scratch dir = argv[1].
#include <SDL.h>

#include <cstdio>
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

    std::printf("test_controls_bindings: %d/%d checks passed%s\n", g_checks - g_fails, g_checks, g_fails ? "" : " / ALL PASS");
    return g_fails ? 1 : 0;
}
