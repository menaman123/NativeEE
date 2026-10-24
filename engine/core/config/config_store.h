// core/config/config_store.h -- ConfigStore: THE settings file, `nativeee.ini`.
//
// One file is read by ConfigLoader at start and written by every settings
// surface -- the launcher's Settings page (a separate process: it edits the
// same file with the same grammar), the in-game pages (SessionRuntime's
// persist route) and the first-run flow. Nothing else persists a setting.
//
// Where the file is (Resolve, in this order; the first that applies wins):
//   1. `--config <file>`        the file named is the store (read AND written);
//   2. `<executable dir>/nativeee.ini` when that file exists: a game folder the
//      launcher prepared (the binary and its settings side by side) finds its
//      settings without any flag;
//   3. `<user data root>/nativeee.ini` (created on the first write): the
//      platform's user data root (SDL's pref path), or `--set paths.user_data=`
//      when the command line pins another root -- a harness leg's sandbox.
//
// Grammar: INI, the schema's sections as `[section]` headers and the key's
// short name inside (`[video]` / `vsync = on`); the flat `section.key = value`
// form is read too (SettingsFile keeps whichever spelling a line has).
// `#` / `;` start comments; unknown keys are refused by the reader.
//
// Legacy (one release): a root holding the former `ps2r.ini` (+ `display.ini`)
// and no nativeee.ini is MIGRATED once at Resolve -- nativeee.ini is written
// from both (display.ini's five video keys win, as their tier did), the old
// files are renamed `.migrated`, and the loader notes it. When the rename or
// the write fails the legacy files are read in place and the note says so.
//
// The resolved store is process-wide (Active): the app side writes through
// `ConfigStore::Set` without being handed a path.
#pragma once

#include <string>
#include <vector>

namespace ps2r {

class ConfigStore {
public:
    static constexpr const char* kFileName = "nativeee.ini";
    static constexpr const char* kLegacyName = "ps2r.ini";
    static constexpr const char* kLegacyDisplayName = "display.ini";
    static constexpr const char* kMigratedSuffix = ".migrated";

    struct Resolution {
        std::string path;        // the store file
        std::string dir;         // its folder (where bindings.ini lives too)
        std::string source;      // "--config" | "executable folder" | "user data"
        bool exists = false;     // the file was present at Resolve
        bool migrated = false;   // legacy files were converted in this call
        std::vector<std::string> legacy_in_place;   // legacy files still to be read (migration refused)
        std::vector<std::string> notes;             // one line each for the log
    };

    // The rule above. `explicit_config` = --config (may be ""); `exe_dir` = the
    // executable's folder ("" = unknown); `user_root` = the data root ("" =
    // none: the cwd-relative name stands). `migrate` = convert legacy files
    // found beside the chosen store (never for an explicit --config file).
    static Resolution Resolve(const std::string& explicit_config, const std::string& exe_dir,
                              const std::string& user_root, bool migrate);

    // The migration alone (public for the tests): `dir`/ps2r.ini + display.ini
    // -> `dir`/nativeee.ini when the latter is absent. Returns true when the
    // store was written; `legacy_in_place` receives the files left to read
    // when the conversion could not complete; `note` one line for the log.
    static bool MigrateLegacy(const std::string& dir, std::vector<std::string>* legacy_in_place, std::string* note);
    // The converted text (pure): the legacy ps2r.ini text's comments and every
    // key it sets, re-homed under `[section]` headers, display.ini's keys over
    // them. Keys the schema does not know are dropped with a comment line.
    static std::string MigratedText(const std::string& ps2r_text, const std::string& display_text);

    // The process-wide store the loader resolved (the app's writers use it).
    static void SetActive(const Resolution& r);
    static const Resolution& Active();
    static const std::string& ActivePath() { return Active().path; }
    static const std::string& ActiveDir() { return Active().dir; }

    // Write one key to the active store (SettingsFile's atomic in-place write;
    // false with *err on a refused key or an I/O failure).
    static bool Set(const std::string& key, const std::string& value, std::string* err);
    // Read one key from the active store ("" and false when absent or no store).
    static bool Get(const std::string& key, std::string* value);
    // Every key the active store sets, in file order: `key=value` pairs.
    static std::vector<std::pair<std::string, std::string>> ReadAll();
    static std::vector<std::pair<std::string, std::string>> ReadAllFrom(const std::string& path);

    // The folder of a path ("" for a bare name).
    static std::string DirOf(const std::string& path);

private:
    static Resolution active_;
};

}  // namespace ps2r
