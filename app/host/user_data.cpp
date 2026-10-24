#include "app/host/user_data.h"

#include <SDL.h>
#include <cstdint>
#include <filesystem>
#include <system_error>

#include "engine/core/config/config.h"
#include "engine/core/config/config_store.h"

namespace ps2r::app {
namespace fs = std::filesystem;

std::string UserData::PlatformRoot(std::string* legacy_note) {
    char* pref = SDL_GetPrefPath("", kAppName);
    if (!pref) return {};
    std::string root(pref);
    SDL_free(pref);
    while (!root.empty() && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    CarryOverLegacyRoot(root, legacy_note);
    return root;
}

bool UserData::CarryOverLegacyRoot(const std::string& root, std::string* note) {
    // SDL has just created `root` (or it is still empty); the sibling
    // kLegacyAppName directory, when present, is copied in whole. Never moved,
    // never deleted; a non-empty root is never touched (the carry-over ran once).
    if (note) note->clear();
    if (root.empty()) return false;
    std::error_code ec;
    const fs::path legacy = fs::path(root).parent_path() / kLegacyAppName;
    if (!fs::is_directory(root, ec) || !fs::is_empty(root, ec) || !fs::is_directory(legacy, ec)) return false;
    ec.clear();
    fs::copy(legacy, root, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
    if (note) {
        *note = ec ? "user data: copying the former root " + legacy.string() + " -> " + root + " FAILED: " + ec.message()
                   : "user data: copied the former root " + legacy.string() + " -> " + root +
                         " (the old directory is left in place)";
    }
    return true;
}

std::string UserData::SettingsFile(const Config& cfg) {
    if (!ConfigStore::ActivePath().empty()) return ConfigStore::ActivePath();
    return cfg.paths.user_data.empty() ? std::string(ConfigStore::kFileName) : cfg.paths.user_data + "/" + ConfigStore::kFileName;
}

namespace {
UserData::Migration CopyIfAbsent(const std::string& from, const std::string& to) {
    UserData::Migration m;
    m.from = from;
    m.to = to;
    std::error_code ec;
    if (from.empty() || to.empty() || from == to) return m;
    if (!fs::is_directory(from, ec) || fs::exists(to, ec)) return m;
    fs::create_directories(fs::path(to).parent_path(), ec);
    ec.clear();
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
    if (ec) {
        m.error = ec.message();
        return m;
    }
    m.copied = true;
    for (fs::recursive_directory_iterator it(to, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) ++m.files;
    return m;
}
}  // namespace

std::vector<UserData::Migration> UserData::Migrate(const Config& cfg, const std::string& cwd) {
    std::vector<Migration> out;
    if (cfg.paths.user_data.empty()) return out;   // no root: the former layout stands, nothing to migrate
    const std::string base = cwd.empty() ? std::string(".") : cwd;
    const std::string& root = cfg.paths.user_data;
    // Only the ROOT-RESOLVED locations migrate: an explicit paths.mc_dir /
    // paths.log_dir (a leg's fresh card, a pinned log dir) is used as given.
    if (cfg.paths.mc_dir == root + "/memcards") out.push_back(CopyIfAbsent(base + "/memcards", cfg.paths.mc_dir));
    if (cfg.paths.log_dir == root + "/logs")
        out.push_back(CopyIfAbsent(base + "/logs/savestates", cfg.paths.log_dir + "/savestates"));
    out.push_back(CopyIfAbsent(base + "/play_sessions", root + "/play_sessions"));
    return out;
}

std::string UserData::Describe(const Migration& m) {
    if (!m.error.empty()) return "user data: migration " + m.from + " -> " + m.to + " FAILED: " + m.error;
    if (m.copied) return "user data: migrated " + m.from + " -> " + m.to + " (" + std::to_string(m.files) + " files)";
    return "user data: " + m.to + " (no migration from " + m.from + ")";
}

}  // namespace ps2r::app
