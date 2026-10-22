// tests/app/test_controls_page_binding.cpp -- ctest ControlsPageLiveRebind (Bernoulli): the
// in-game Controls page's model (ControlsPageBinding over ControlsBindings + PlatformInput) under
// SYNTHETIC key events, no window: arm the `cross` row, offer "J" (the key name SessionGui offers
// from SDL_GetScancodeName) -> bindings.ini is written, ps2r.ini carries input.bindings_file, the
// LIVE mapper is reloaded, and a J key press now samples as CROSS on the pad while Space (the
// default) no longer does; Escape while armed cancels and binds nothing; a second binding of the
// same key moves it off the row that had it. Scratch dir = argv[1].
#include <SDL.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include "app/host/controls_page_binding.h"
#include "app/host/platform_input.h"
#include "engine/core/threads/park.h"
#include "engine/platform/host/platform.h"

using namespace ps2r::app;

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

namespace {

constexpr uint16_t kCross = 0x4000;   // libpad active-high wire bit (PadProtocol::Input::buttons)

std::string ReadFile(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int RowIndex(const ControlsSection::Model& m, const char* name) {
    const auto rows = m.rows();
    for (size_t i = 0; i < rows.size(); i++)
        if (rows[i].name == name) return static_cast<int>(i);
    return -1;
}

std::string RowValue(const ControlsSection::Model& m, const char* name) {
    const auto rows = m.rows();
    for (const auto& r : rows)
        if (r.name == name) return r.value;
    return "<absent>";
}

// One key press (down, then up after the game thread sampled) through PlatformInput's real
// route: the owner (this thread) handles the events and services the game thread's Sample.
uint16_t PressAndSample(PlatformInput& input, ps2r::Park& wake, SDL_Scancode scancode) {
    SDL_Event key{};
    key.type = SDL_KEYDOWN;
    key.key.keysym.scancode = scancode;
    input.HandleEvent(key);
    std::atomic<bool> done{false};
    PadProtocol::Input sample{};
    std::thread game([&] {
        sample = input.Sample();
        done.store(true);
        wake.Notify();
    });
    while (!done.load()) {
        const auto generation = wake.Generation();
        input.ServicePending();
        if (!done.load()) wake.WaitFor(1000000, generation);
    }
    game.join();
    key.type = SDL_KEYUP;
    input.HandleEvent(key);
    // Drain the release the same way (a Sample consumes the edge).
    std::atomic<bool> done2{false};
    std::thread game2([&] { input.Sample(); done2.store(true); wake.Notify(); });
    while (!done2.load()) {
        const auto generation = wake.Generation();
        input.ServicePending();
        if (!done2.load()) wake.WaitFor(1000000, generation);
    }
    game2.join();
    return sample.buttons;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: test_controls_page_binding <scratch dir>\n"); return 2; }
    const std::string root = argv[1];
    ps2r::host::MakeDirectory(root.c_str());
    std::remove((root + "/bindings.ini").c_str());
    std::remove((root + "/ps2r.ini").c_str());
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::printf("SDL_Init failed: %s\n", SDL_GetError()); return 3; }
    {
        ps2r::platform::sdl::InputSettings settings;   // the baked-in defaults: cross = Space
        settings.gamepad_enabled = false;
        PlatformInput input(settings);
        ps2r::Park wake;
        input.BindMainWake(&wake);
        ControlsPageBinding page(root, root + "/ps2r.ini", &input);
        ControlsSection::Model m = page.Model();
        CHECK(RowValue(m, "cross") == "Space", "default row: cross = Space (got %s)", RowValue(m, "cross").c_str());
        CHECK(m.armed_row() == -1, "nothing armed at start");
        CHECK(!m.offer_key("J"), "an unarmed offer binds nothing");

        // Baseline: Space samples as CROSS, J as nothing.
        CHECK((PressAndSample(input, wake, SDL_SCANCODE_SPACE) & kCross) != 0, "default: Space -> CROSS");
        CHECK((PressAndSample(input, wake, SDL_SCANCODE_J) & kCross) == 0, "default: J -> not CROSS");

        // Escape while armed cancels: nothing bound, nothing saved.
        const int cross = RowIndex(m, "cross");
        CHECK(cross >= 0, "cross row exists");
        m.arm(cross);
        CHECK(m.armed_row() == cross, "cross armed");
        CHECK(m.offer_key("Escape"), "Escape consumed the arm");
        CHECK(m.armed_row() == -1, "Escape disarmed");
        CHECK(RowValue(m, "cross") == "Space", "Escape bound nothing");
        CHECK(page.Saves() == 0, "Escape saved nothing");

        // The rebind: cross = J. File written, settings file points at it, the live mapper reloaded.
        m.arm(cross);
        CHECK(m.offer_key("J"), "J landed on the armed row");
        CHECK(RowValue(m, "cross") == "J", "cross = J (got %s)", RowValue(m, "cross").c_str());
        CHECK(page.Saves() == 1, "one save");
        CHECK(input.BindingsReloads() == 1 && input.BindingsReloadsOk() == 1, "the live mapper took the file (reloads=%llu ok=%llu)",
              (unsigned long long)input.BindingsReloads(), (unsigned long long)input.BindingsReloadsOk());
        const std::string ini = ReadFile(root + "/bindings.ini");
        CHECK(ini.find("cross") != std::string::npos && ini.find("= J") != std::string::npos, "bindings.ini has cross = J:\n%s", ini.c_str());
        const std::string settings_text = ReadFile(root + "/ps2r.ini");
        CHECK(settings_text.find("input.bindings_file") != std::string::npos && settings_text.find(root + "/bindings.ini") != std::string::npos,
              "ps2r.ini carries input.bindings_file = <root>/bindings.ini:\n%s", settings_text.c_str());
        CHECK(page.Status().find("applied to the pad") != std::string::npos, "status names the live apply: %s", page.Status().c_str());

        // The pad now: J -> CROSS, Space -> nothing (no restart, the same PlatformInput).
        CHECK((PressAndSample(input, wake, SDL_SCANCODE_J) & kCross) != 0, "after rebind: J -> CROSS");
        CHECK((PressAndSample(input, wake, SDL_SCANCODE_SPACE) & kCross) == 0, "after rebind: Space -> not CROSS");

        // A key bound to another row moves: square = J takes J off cross.
        const int square = RowIndex(m, "square");
        m.arm(square);
        CHECK(m.offer_key("J"), "J landed on square");
        CHECK(RowValue(m, "square") == "J" && RowValue(m, "cross").empty(), "J moved: square=%s cross='%s'",
              RowValue(m, "square").c_str(), RowValue(m, "cross").c_str());
        CHECK((PressAndSample(input, wake, SDL_SCANCODE_J) & kCross) == 0, "after the move: J is not CROSS");
        CHECK((PressAndSample(input, wake, SDL_SCANCODE_J) & 0x8000) != 0, "after the move: J -> SQUARE");

        // A fresh page over the same root reads the file back (what the next start sees).
        ControlsPageBinding again(root, root + "/ps2r.ini", nullptr);
        CHECK(RowValue(again.Model(), "square") == "J", "the file persists: square = J on reload");
    }
    SDL_Quit();
    std::printf("ControlsPageLiveRebind: %d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
