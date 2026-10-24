#pragma once
// UserData -- the user data layout (CHARTER 2 C4): the ONE root under which the
// memory cards, savestates, texture packs and the extracted game files live --
// never beside the binary, so an updated binary keeps the user's data. The
// settings file (nativeee.ini) is ConfigStore's: beside the executable in a
// game folder the launcher prepared, else under this root.
//
//   macOS    ~/Library/Application Support/NativeEE
//   Windows  %APPDATA%/NativeEE
//   Linux    $XDG_DATA_HOME/NativeEE (~/.local/share/NativeEE)
//
// The root is SDL's preference path (SDL_GetPrefPath -- the platform's own
// rule, no environment read of ours); the composition root hands it to
// ConfigLoader::SetUserDataRoot before Load, and ConfigLoader resolves every
// empty paths.* directory under it (`--set paths.user_data=<dir>` pins another
// root for a leg). Migration: the former cwd-relative directories of a
// repository checkout (memcards/, logs/savestates/, play_sessions/) are COPIED
// into the root the first time the root lacks them -- the source is never
// moved or deleted, every copy is logged, and an existing destination is left
// alone. The same rule carries the previous product name's root over: when
// the root SDL names is brand new (empty) and the sibling directory of the
// former name (kLegacyAppName, "Ps2Recomp") exists, its contents are COPIED
// in once, before the settings are read; the old directory is left in place.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ps2r { class Config; }

namespace ps2r::app {

class UserData {
public:
    static constexpr const char* kAppName = "NativeEE";
    // The product's former name: the user data root of earlier builds, copied
    // into kAppName's root the first time that root is empty (see above).
    static constexpr const char* kLegacyAppName = "Ps2Recomp";

    // The platform's root for kAppName (created when absent), without a
    // trailing separator; "" when the platform cannot name one (the caller then
    // runs with the former cwd-relative defaults and says so). `legacy_note`
    // (optional) receives CarryOverLegacyRoot's line.
    static std::string PlatformRoot(std::string* legacy_note = nullptr);

    // The one-time carry-over: when `root` is an EMPTY directory and its sibling
    // kLegacyAppName directory exists, that directory's contents are copied into
    // `root` (recursive, nothing moved or deleted). Returns true when the copy
    // ran (even partially); `note` (optional) gets one line for the log / stderr
    // when it ran or failed, and stays empty when there was nothing to do.
    static bool CarryOverLegacyRoot(const std::string& root, std::string* note);

    // One migration step: `from` (a directory under the former cwd layout) is
    // copied recursively into `to` when `from` exists and `to` does not.
    struct Migration {
        std::string from, to;
        bool copied = false;       // the copy happened in this call
        uint64_t files = 0;        // files copied
        std::string error;         // non-empty = the copy failed (partial copies are reported)
    };
    // The migrations the resolved Config implies, applied against `cwd`: the
    // memory cards into paths.mc_dir, the savestate slots into
    // <paths.log_dir>/savestates, the play-session recordings into
    // <paths.user_data>/play_sessions. No-ops (destination present or source
    // absent) are returned with copied=false so the caller can log the layout.
    static std::vector<Migration> Migrate(const Config& cfg, const std::string& cwd);

    // The settings file every option persists to: the store ConfigLoader resolved
    // (ConfigStore::ActivePath()); with no store resolved (a test), <root>/nativeee.ini.
    static std::string SettingsFile(const Config& cfg);

    // One line per migration for the log / stderr.
    static std::string Describe(const Migration& m);
};

}  // namespace ps2r::app
