// tests/core/test_settings_file.cpp -- SettingsFile, the settings writer the
// in-game options (MenuExtension) persist through. argv[1] = a scratch directory.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "engine/core/config/config.h"
#include "engine/core/config/config_loader.h"
#include "engine/core/config/settings_file.h"

using ps2r::Config;
using ps2r::ConfigLoader;
using ps2r::SettingsFile;

static int g_fail = 0;
#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            g_fail++;                                                                \
        }                                                                            \
    } while (0)

static std::string Read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    // Replace in place: flat form, comments and order preserved.
    {
        const std::string text = "# user settings\nvideo.scale = 2\nui.perf_overlay=off ; the overlay\naudio.volume = 80\n";
        bool changed = false;
        const std::string next = SettingsFile::SetInText(text, "ui.perf_overlay", "on", &changed);
        CHECK(changed);
        CHECK(next == "# user settings\nvideo.scale = 2\nui.perf_overlay = on\naudio.volume = 80\n");
        std::string v;
        CHECK(SettingsFile::GetFromText(next, "ui.perf_overlay", &v) && v == "on");
        CHECK(SettingsFile::GetFromText(next, "video.scale", &v) && v == "2");
        CHECK(!SettingsFile::GetFromText(next, "video.vsync", &v));
        // Unchanged value: text identical, changed=false.
        changed = false;
        CHECK(SettingsFile::SetInText(next, "ui.perf_overlay", "on", &changed) == next && !changed);
    }
    // Replace in place: a [section]-scoped short key keeps its spelling.
    {
        const std::string text = "[ui]\ngui_open = on\nperf_overlay = off\n[video]\nscale = 1\n";
        const std::string next = SettingsFile::SetInText(text, "ui.perf_overlay", "on");
        CHECK(next == "[ui]\ngui_open = on\nperf_overlay = on\n[video]\nscale = 1\n");
        // A new key of a section the file has goes under that header, after its last key.
        const std::string in_ui = SettingsFile::SetInText(next, "ui.menu_pages", "off");
        CHECK(in_ui == "[ui]\ngui_open = on\nperf_overlay = on\nmenu_pages = off\n[video]\nscale = 1\n");
        // A new key of a section the file lacks gets its own [section] at the end.
        const std::string app = SettingsFile::SetInText(next, "input.kbm_pad", "off");
        CHECK(app == "[ui]\ngui_open = on\nperf_overlay = on\n[video]\nscale = 1\n\n[input]\nkbm_pad = off\n");
        // Entries: every key in file order, section-qualified.
        const auto entries = SettingsFile::Entries(app);
        CHECK(entries.size() == 4 && entries[0].first == "ui.gui_open" && entries[2] == std::make_pair(std::string("video.scale"), std::string("1")) &&
              entries[3] == std::make_pair(std::string("input.kbm_pad"), std::string("off")));
        Config c;
        std::string err;
        CHECK(ConfigLoader::ApplyIniText(app, c, &err));   // the reader accepts what the writer wrote
        CHECK(c.ui.perf_overlay && !c.input.kbm_pad && c.video.scale == 1 && c.ui.gui_open);
    }
    // Append to an empty text.
    {
        const std::string next = SettingsFile::SetInText("", "ui.perf_overlay", "on");
        CHECK(next == "[ui]\nperf_overlay = on\n");
        // A flat-form file keeps its flat lines; a new section is appended after them.
        const std::string flat = SettingsFile::SetInText("video.scale = 2\n", "audio.volume", "5");
        CHECK(flat == "video.scale = 2\n\n[audio]\nvolume = 5\n");
        // A comment or blank after the section's keys stays after the inserted key? No: the key goes right
        // after the last KEY line, so trailing comments keep their place before the next header.
        const std::string trail = SettingsFile::SetInText("[ui]\ngui_open = on\n# end of ui\n\n[video]\nscale = 1\n", "ui.menu_pages", "off");
        CHECK(trail == "[ui]\ngui_open = on\nmenu_pages = off\n# end of ui\n\n[video]\nscale = 1\n");
    }
    // Writable: schema keys only, never harness plumbing.
    CHECK(SettingsFile::Writable("ui.perf_overlay"));
    CHECK(SettingsFile::Writable("input.kbm_pad"));
    CHECK(!SettingsFile::Writable("harness.frames"));
    CHECK(!SettingsFile::Writable("ui.not_a_key"));
    // The file route: create, replace, refuse, round-trip through the loader.
    {
        const std::string path = dir + "/settings_file_test.ini";
        std::remove(path.c_str());
        std::string err;
        CHECK(SettingsFile::Set(path, "ui.perf_overlay", "on", &err));
        CHECK(Read(path) == "[ui]\nperf_overlay = on\n");
        CHECK(SettingsFile::Set(path, "audio.volume", "55", &err));
        CHECK(SettingsFile::Set(path, "ui.perf_overlay", "off", &err));
        CHECK(Read(path) == "[ui]\nperf_overlay = off\n\n[audio]\nvolume = 55\n");
        CHECK(!SettingsFile::Set(path, "harness.frames", "3", &err) && err.find("harness.frames") != std::string::npos);
        CHECK(!SettingsFile::Set(path, "bogus.key", "3", &err));
        CHECK(Read(path) == "[ui]\nperf_overlay = off\n\n[audio]\nvolume = 55\n");   // refusals leave the file alone
        Config c;
        bool existed = false;
        CHECK(ConfigLoader::ApplyIniFile(path, c, &err, &existed) && existed);
        CHECK(!c.ui.perf_overlay && c.audio.volume == 55);
        CHECK(Read(path + ".tmp").empty());   // no temporary left behind
        std::remove(path.c_str());
    }
    if (g_fail) {
        std::fprintf(stderr, "test_settings_file: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("test_settings_file: PASS\n");
    return 0;
}
