// tools/settings/nativeee_settings.cpp -- the settings file from the command line.
//
//   nativeee_settings path [--config F] [--exe-dir D] [--user-root R] [--no-migrate]
//       The store the game resolves for these inputs (ConfigStore::Resolve):
//       `path=<file>` `source=<rule>` `exists=0|1` `migrated=0|1` + notes.
//   nativeee_settings get <file> <key>          the value (exit 1 when absent)
//   nativeee_settings set <file> <key> <value>  write (the value is validated against the schema first)
//   nativeee_settings list <file>               every `key = value` the file sets, file order
//   nativeee_settings migrate <dir>             ps2r.ini / display.ini -> nativeee.ini (exit 0 when written)
//   nativeee_settings keys                      every writable schema key
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/core/config/config.h"
#include "engine/core/config/config_store.h"
#include "engine/core/config/settings_file.h"

using ps2r::Config;
using ps2r::ConfigStore;
using ps2r::SettingsFile;

namespace {

int Usage() {
    std::fprintf(stderr,
                 "nativeee_settings path [--config F] [--exe-dir D] [--user-root R] [--no-migrate]\n"
                 "nativeee_settings get <file> <key>\n"
                 "nativeee_settings set <file> <key> <value>\n"
                 "nativeee_settings list <file>\n"
                 "nativeee_settings migrate <dir>\n"
                 "nativeee_settings keys\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return Usage();
    const std::string cmd = argv[1];
    if (cmd == "path") {
        std::string config, exe_dir, root;
        bool migrate = true;
        for (int i = 2; i < argc; i++) {
            const std::string a = argv[i];
            if (a == "--no-migrate") migrate = false;
            else if (i + 1 < argc && a == "--config") config = argv[++i];
            else if (i + 1 < argc && a == "--exe-dir") exe_dir = argv[++i];
            else if (i + 1 < argc && a == "--user-root") root = argv[++i];
            else return Usage();
        }
        const ConfigStore::Resolution r = ConfigStore::Resolve(config, exe_dir, root, migrate);
        std::printf("path=%s\nsource=%s\nexists=%d\nmigrated=%d\n", r.path.c_str(), r.source.c_str(), r.exists ? 1 : 0, r.migrated ? 1 : 0);
        for (const std::string& n : r.notes) std::printf("note=%s\n", n.c_str());
        for (const std::string& f : r.legacy_in_place) std::printf("legacy_in_place=%s\n", f.c_str());
        return 0;
    }
    if (cmd == "get" && argc == 4) {
        const auto entries = ConfigStore::ReadAllFrom(argv[2]);
        std::string value;
        bool found = false;
        for (const auto& e : entries)
            if (e.first == argv[3]) { value = e.second; found = true; }   // the LAST line wins, as the loader applies them
        if (!found) return 1;
        std::printf("%s\n", value.c_str());
        return 0;
    }
    if (cmd == "set" && argc == 5) {
        const std::string key = argv[3], value = argv[4];
        std::string err;
        if (!SettingsFile::Writable(key)) {
            std::fprintf(stderr, "nativeee_settings: %s: not a writable setting\n", key.c_str());
            return 1;
        }
        Config probe;
        if (!probe.Set(key, value, &err)) {   // the game would refuse the file: never write such a value
            std::fprintf(stderr, "nativeee_settings: %s\n", err.c_str());
            return 1;
        }
        if (!SettingsFile::Set(argv[2], key, value, &err)) {
            std::fprintf(stderr, "nativeee_settings: %s\n", err.c_str());
            return 1;
        }
        return 0;
    }
    if (cmd == "list" && argc == 3) {
        for (const auto& e : ConfigStore::ReadAllFrom(argv[2])) std::printf("%s = %s\n", e.first.c_str(), e.second.c_str());
        return 0;
    }
    if (cmd == "migrate" && argc == 3) {
        std::vector<std::string> in_place;
        std::string note;
        const bool ok = ConfigStore::MigrateLegacy(argv[2], &in_place, &note);
        if (!note.empty()) std::printf("%s\n", note.c_str());
        for (const std::string& f : in_place) std::printf("legacy_in_place=%s\n", f.c_str());
        return ok ? 0 : 1;
    }
    if (cmd == "keys" && argc == 2) {
        for (const std::string& k : Config::Keys())
            if (SettingsFile::Writable(k)) std::printf("%s\n", k.c_str());
        return 0;
    }
    return Usage();
}
