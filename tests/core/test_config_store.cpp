// tests/core/test_config_store.cpp -- ConfigStore: the ONE settings file
// (nativeee.ini). The resolution order (--config > <executable dir>/nativeee.ini
// > <user data>/nativeee.ini), the legacy ps2r.ini + display.ini migration (text
// and files), the process-wide Active store the writers use, and the reader /
// writer round trip through ConfigLoader. argv[1] = a scratch directory.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>

#include "engine/core/config/config.h"
#include "engine/core/config/config_loader.h"
#include "engine/core/config/config_store.h"
#include "engine/core/config/settings_file.h"

using ps2r::Config;
using ps2r::ConfigLoader;
using ps2r::ConfigStore;

static int g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            g_fail++;                                                        \
        }                                                                    \
    } while (0)

static std::string Read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}
static void Write(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::trunc);
    f << text;
}
static bool Exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    const std::string root = dir + "/store_root", game = dir + "/store_game";
    ::mkdir(root.c_str(), 0755);
    ::mkdir(game.c_str(), 0755);
    for (const char* n : {"nativeee.ini", "ps2r.ini", "display.ini", "ps2r.ini.migrated", "display.ini.migrated"}) {
        std::remove((root + "/" + n).c_str());
        std::remove((game + "/" + n).c_str());
    }

    // Resolution order.
    {
        ConfigStore::Resolution r = ConfigStore::Resolve("", game, root, true);
        CHECK(r.path == root + "/nativeee.ini" && r.source == "user data" && !r.exists && r.dir == root);
        Write(game + "/nativeee.ini", "[video]\nscale = 2\n");
        r = ConfigStore::Resolve("", game, root, true);
        CHECK(r.path == game + "/nativeee.ini" && r.source == "executable folder" && r.exists && r.dir == game);
        r = ConfigStore::Resolve(dir + "/explicit.ini", game, root, true);
        CHECK(r.path == dir + "/explicit.ini" && r.source == "--config" && !r.exists && r.dir == dir);
        r = ConfigStore::Resolve("", "", "", true);   // no root known: the bare name (cwd)
        CHECK(r.path == "nativeee.ini" && r.dir.empty());
        std::remove((game + "/nativeee.ini").c_str());
    }
    // DirOf.
    CHECK(ConfigStore::DirOf("/a/b/nativeee.ini") == "/a/b" && ConfigStore::DirOf("nativeee.ini").empty() && ConfigStore::DirOf("/x") == "/");

    // MigratedText: comments kept, keys re-homed under sections, display.ini's keys win,
    // unknown keys dropped with a comment, the former header line not carried.
    {
        const std::string text = ConfigStore::MigratedText(
            "# ps2r.ini -- the former header\n# keep me\nvideo.aspect = 4:3\nvideo.vsync = on\n[ui]\nperf_overlay = on\nnope.key = 1\n",
            "mode = fill\naspect = 16:9\ncustom_scale = 250\n");
        CHECK(text.find("# keep me\n") != std::string::npos && text.find("former header") == std::string::npos);
        CHECK(text.find("[video]\naspect = 16:9\nvsync = on\nfit_mode = fill\ncustom_scale_pct = 250\n") != std::string::npos);
        CHECK(text.find("[ui]\nperf_overlay = on\n") != std::string::npos);
        CHECK(text.find("# not a setting any more: nope.key = 1") != std::string::npos);
        Config c;
        std::string err;
        CHECK(ConfigLoader::ApplyIniText(text, c, &err));   // the loader takes what the migration wrote
        CHECK(c.video.aspect == ps2r::AspectSel::R16_9 && c.video.vsync && c.video.fit_mode == ps2r::FitMode::Fill && c.ui.perf_overlay);
        // display.ini alone: only its non-default keys are written.
        const std::string only = ConfigStore::MigratedText("", "aspect = 21:9\n");
        CHECK(only.find("[video]\naspect = 21:9\n") != std::string::npos && only.find("fit_mode") == std::string::npos);
    }
    // MigrateLegacy on files: written once, renamed; a second call does nothing.
    {
        Write(root + "/ps2r.ini", "video.scale = 3\n");
        Write(root + "/display.ini", "aspect = 16:10\n");
        std::vector<std::string> in_place;
        std::string note;
        CHECK(ConfigStore::MigrateLegacy(root, &in_place, &note));
        CHECK(in_place.empty() && note.find("moved to") != std::string::npos);
        CHECK(Exists(root + "/nativeee.ini") && Exists(root + "/ps2r.ini.migrated") && Exists(root + "/display.ini.migrated"));
        CHECK(!Exists(root + "/ps2r.ini") && !Exists(root + "/display.ini"));
        CHECK(Read(root + "/nativeee.ini").find("[video]\nscale = 3\naspect = 16:10\n") != std::string::npos);
        CHECK(!ConfigStore::MigrateLegacy(root, &in_place, &note));   // nothing left to convert
        // A legacy file the loader would refuse stays in place (never converted blind).
        std::remove((root + "/nativeee.ini").c_str());
        Write(root + "/ps2r.ini", "garbage\n");
        in_place.clear();
        CHECK(!ConfigStore::MigrateLegacy(root, &in_place, &note));
        CHECK(in_place.size() == 1 && in_place[0] == root + "/ps2r.ini" && note.find("not converted") != std::string::npos);
        CHECK(Exists(root + "/ps2r.ini") && !Exists(root + "/nativeee.ini"));
        std::remove((root + "/ps2r.ini").c_str());
        std::remove((root + "/ps2r.ini.migrated").c_str());
        std::remove((root + "/display.ini.migrated").c_str());
    }
    // Active store: Set / Get / ReadAll through the process-wide resolution, and
    // the loader reads back what the writers wrote -- in a folder with no file yet.
    {
        ConfigStore::Resolution r = ConfigStore::Resolve("", "", root, true);
        ConfigStore::SetActive(r);
        CHECK(ConfigStore::ActivePath() == root + "/nativeee.ini" && ConfigStore::ActiveDir() == root);
        std::string err, v;
        CHECK(!ConfigStore::Get("video.vsync", &v));
        CHECK(ConfigStore::Set("video.vsync", "on", &err));
        CHECK(ConfigStore::Set("audio.volume", "35", &err));
        CHECK(ConfigStore::Set("video.scale", "2", &err));
        CHECK(!ConfigStore::Set("harness.frames", "3", &err));
        CHECK(ConfigStore::Get("audio.volume", &v) && v == "35");
        CHECK(Read(root + "/nativeee.ini") == "[video]\nvsync = on\nscale = 2\n\n[audio]\nvolume = 35\n");
        const auto all = ConfigStore::ReadAll();
        CHECK(all.size() == 3 && all[0].first == "video.vsync" && all[1].first == "video.scale" && all[2].first == "audio.volume");
        std::vector<char*> args;
        std::string a0 = "nativeee";
        args.push_back(&a0[0]);
        ConfigLoader l;
        l.SetSearchDir(root);
        Config c;
        ConfigLoader::Result lr = l.Load(1, args.data(), c);
        CHECK(lr.ok && c.video.vsync && c.audio.volume == 35 && c.video.scale == 2 && lr.settings_file == root + "/nativeee.ini");
        // Load re-seats Active to what it resolved.
        CHECK(ConfigStore::ActivePath() == root + "/nativeee.ini");
        // --set paths.user_data pins the root the store lives under (a leg's sandbox).
        std::string a1 = "--set", a2 = "paths.user_data=" + game;
        std::vector<char*> args2 = {&a0[0], &a1[0], &a2[0]};
        ConfigLoader l2;
        l2.SetUserDataRoot(root);
        Config c2;
        lr = l2.Load(3, args2.data(), c2);
        CHECK(lr.ok && lr.settings_file == game + "/nativeee.ini" && lr.settings_source == "user data" && !c2.video.vsync);
        // An explicit --config is the store even when the executable folder has one.
        Write(game + "/nativeee.ini", "[video]\nscale = 4\n");
        Write(dir + "/explicit.ini", "[video]\nscale = 3\n");
        std::string b1 = "--config", b2 = dir + "/explicit.ini";
        std::vector<char*> args3 = {&a0[0], &b1[0], &b2[0]};
        ConfigLoader l3;
        l3.SetExecutableDir(game);
        l3.SetUserDataRoot(root);
        Config c3;
        lr = l3.Load(3, args3.data(), c3);
        CHECK(lr.ok && c3.video.scale == 3 && lr.settings_source == "--config" && ConfigStore::ActiveDir() == dir);
        ConfigLoader l4;
        l4.SetExecutableDir(game);
        l4.SetUserDataRoot(root);
        Config c4;
        lr = l4.Load(1, args.data(), c4);
        CHECK(lr.ok && c4.video.scale == 4 && lr.settings_source == "executable folder");
        std::remove((game + "/nativeee.ini").c_str());
        std::remove((dir + "/explicit.ini").c_str());
        std::remove((root + "/nativeee.ini").c_str());
    }
    std::printf("test_config_store: %s (%d failures)\n", g_fail ? "FAIL" : "ok", g_fail);
    return g_fail ? 1 : 0;
}
