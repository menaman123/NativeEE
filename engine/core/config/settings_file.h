// core/config/settings_file.h -- SettingsFile: the ONE writer of the settings
// file ps2r.ini (ConfigLoader is its reader). An in-game option (the title's
// added menu pages) or the launcher persists one `key=value` at a time: the
// line of an existing key is replaced in place (flat `section.key` lines and
// `[section]`-scoped lines alike), a new key is appended as a flat line, every
// other line -- comments, unknown-to-us ordering -- is preserved byte for byte.
// Only schema keys are written (Config::IsKey); harness.* is refused as the
// reader refuses it. The file is written whole through a temporary and renamed
// into place, so a crash never leaves a truncated settings file.
#pragma once

#include <string>
#include <vector>

namespace ps2r {

class SettingsFile {
public:
    // Replaces or appends `key = value` in the ini text; returns the new text.
    // `changed` (optional) reports whether the text differs from the input.
    static std::string SetInText(const std::string& text, const std::string& key, const std::string& value,
                                 bool* changed = nullptr);
    // Reads `path` (absent = empty text), applies SetInText, writes it back.
    // False with *err set on a refused key or an I/O failure.
    static bool Set(const std::string& path, const std::string& key, const std::string& value, std::string* err);
    // The value of `key` in the ini text ("" and false when absent).
    static bool GetFromText(const std::string& text, const std::string& key, std::string* value);
    // Whether `key` may be written: a schema key that is not harness plumbing.
    static bool Writable(const std::string& key);
};

}  // namespace ps2r
