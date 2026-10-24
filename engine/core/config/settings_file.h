// core/config/settings_file.h -- SettingsFile: the ONE writer of the settings
// file nativeee.ini (ConfigStore names it, ConfigLoader reads it). An in-game
// option (the title's added menu pages) or the launcher persists one
// `key=value` at a time: the line of an existing key is replaced in place (flat
// `section.key` lines and `[section]`-scoped lines alike), a new key is written
// under its `[section]` header -- after the header's last key line, or in a
// new header appended at the end -- and every other line (comments, order) is
// preserved byte for byte.
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
    // Every key the text sets, in file order, as (`section.key`, value).
    static std::vector<std::pair<std::string, std::string>> Entries(const std::string& text);
    // Whether `key` may be written: a schema key that is not harness plumbing.
    static bool Writable(const std::string& key);
};

}  // namespace ps2r
