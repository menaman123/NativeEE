// core/config/config_store.cpp -- see config_store.h.
#include "engine/core/config/config_store.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/stat.h>

#include "engine/core/config/config.h"
#include "engine/core/config/config_loader.h"
#include "engine/core/config/settings_file.h"

namespace ps2r {
namespace {

bool IsFile(const std::string& path) {
    if (path.empty()) return false;
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG;
}

bool ReadText(const std::string& path, std::string* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    std::ostringstream buf;
    buf << in.rdbuf();
    *out = buf.str();
    return true;
}

// Whole-file write through a temporary + rename (a crash never leaves a torn store).
bool WriteTextAtomic(const std::string& path, const std::string& text, std::string* err) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            if (err) *err = "cannot write " + tmp;
            return false;
        }
        out << text;
        if (!out) {
            if (err) *err = "write failed: " + tmp;
            std::remove(tmp.c_str());
            return false;
        }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        if (err) *err = "cannot replace " + path;
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

std::string Join(const std::string& dir, const char* name) {
    if (dir.empty()) return name;
    return dir + "/" + name;
}

}  // namespace

ConfigStore::Resolution ConfigStore::active_;

std::string ConfigStore::DirOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return "";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

void ConfigStore::SetActive(const Resolution& r) { active_ = r; }
const ConfigStore::Resolution& ConfigStore::Active() { return active_; }

ConfigStore::Resolution ConfigStore::Resolve(const std::string& explicit_config, const std::string& exe_dir,
                                             const std::string& user_root, bool migrate) {
    Resolution r;
    if (!explicit_config.empty()) {
        r.path = explicit_config;
        r.dir = DirOf(explicit_config);
        r.source = "--config";
        r.exists = IsFile(r.path);
        return r;
    }
    if (!exe_dir.empty()) {
        const std::string beside = Join(exe_dir, kFileName);
        if (IsFile(beside)) {
            r.path = beside;
            r.dir = exe_dir;
            r.source = "executable folder";
            r.exists = true;
            return r;
        }
    }
    r.dir = user_root;
    r.path = Join(user_root, kFileName);
    r.source = "user data";
    r.exists = IsFile(r.path);
    if (!r.exists) {
        const bool legacy = IsFile(Join(user_root, kLegacyName)) || IsFile(Join(user_root, kLegacyDisplayName));
        if (legacy) {
            std::string note;
            if (migrate) {
                r.migrated = MigrateLegacy(user_root, &r.legacy_in_place, &note);
                r.exists = r.migrated;
            } else {
                if (IsFile(Join(user_root, kLegacyName))) r.legacy_in_place.push_back(Join(user_root, kLegacyName));
                if (IsFile(Join(user_root, kLegacyDisplayName))) r.legacy_in_place.push_back(Join(user_root, kLegacyDisplayName));
                note = "settings: former files read in place (no migration asked): " + Join(user_root, kLegacyName);
            }
            if (!note.empty()) r.notes.push_back(note);
        }
    }
    return r;
}

std::string ConfigStore::MigratedText(const std::string& ps2r_text, const std::string& display_text) {
    // Pass 1: the legacy file's comment lines (kept at the top) and its keys in order.
    std::vector<std::string> comments;
    std::vector<std::pair<std::string, std::string>> entries = SettingsFile::Entries(ps2r_text);
    {
        std::istringstream in(ps2r_text);
        std::string line;
        while (std::getline(in, line)) {
            size_t b = line.find_first_not_of(" \t\r");
            if (b == std::string::npos) continue;
            if (line[b] == '#' || line[b] == ';') {
                std::string c = line.substr(b);
                while (!c.empty() && (c.back() == '\r' || c.back() == ' ')) c.pop_back();
                if (c.find("ps2r.ini") != std::string::npos) continue;   // the former header names the former file
                comments.push_back(c);
            }
        }
    }
    // display.ini's five keys win over ps2r.ini's (their tier applied after it).
    if (!display_text.empty()) {
        Config probe;
        ConfigLoader::ApplyDisplayIniText(display_text, probe);
        const Config defaults;
        const char* keys[] = {"video.fit_mode", "video.aspect", "video.custom_scale_pct", "video.custom_offset_x", "video.custom_offset_y"};
        std::string v, d;
        for (const char* k : keys) {
            probe.Get(k, &v);
            defaults.Get(k, &d);
            bool present = false;
            for (auto& e : entries)
                if (e.first == k) { e.second = v; present = true; }
            if (!present && v != d) entries.emplace_back(k, v);
        }
    }
    // Pass 2: sections in first-seen order, keys in order; unknown keys become a comment.
    std::vector<std::string> order;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> by_section;
    std::vector<std::string> dropped;
    for (const auto& e : entries) {
        const size_t dot = e.first.find('.');
        if (dot == std::string::npos || !SettingsFile::Writable(e.first)) {
            dropped.push_back(e.first + " = " + e.second);
            continue;
        }
        const std::string section = e.first.substr(0, dot), name = e.first.substr(dot + 1);
        if (!by_section.count(section)) order.push_back(section);
        auto& list = by_section[section];
        bool replaced = false;
        for (auto& kv : list)
            if (kv.first == name) { kv.second = e.second; replaced = true; }
        if (!replaced) list.emplace_back(name, e.second);
    }
    std::string out = "# nativeee.ini -- the game's settings. The launcher's Settings page and the in-game menu both edit this file.\n";
    for (const std::string& c : comments) out += c + "\n";
    for (const std::string& d : dropped) out += "# not a setting any more: " + d + "\n";
    for (const std::string& s : order) {
        out += "\n[" + s + "]\n";
        for (const auto& kv : by_section[s]) out += kv.first + " = " + kv.second + "\n";
    }
    return out;
}

bool ConfigStore::MigrateLegacy(const std::string& dir, std::vector<std::string>* legacy_in_place, std::string* note) {
    const std::string store = Join(dir, kFileName);
    const std::string legacy = Join(dir, kLegacyName), display = Join(dir, kLegacyDisplayName);
    std::string ps2r_text, display_text;
    const bool has_legacy = ReadText(legacy, &ps2r_text);
    const bool has_display = ReadText(display, &display_text);
    if (!has_legacy && !has_display) return false;
    if (IsFile(store)) return false;   // never overwrite a store that exists
    // The legacy text must parse (the former reader refused unknown keys / garbage;
    // a file it would have refused is left in place for the loader to refuse by name).
    {
        Config probe;
        std::string err;
        if (has_legacy && !ConfigLoader::ApplyIniText(ps2r_text, probe, &err)) {
            if (legacy_in_place) legacy_in_place->push_back(legacy);
            if (legacy_in_place && has_display) legacy_in_place->push_back(display);
            if (note) *note = "settings: " + legacy + " not converted (" + err + "); read in place";
            return false;
        }
    }
    std::string err;
    if (!WriteTextAtomic(store, MigratedText(ps2r_text, display_text), &err)) {
        if (legacy_in_place) {
            if (has_legacy) legacy_in_place->push_back(legacy);
            if (has_display) legacy_in_place->push_back(display);
        }
        if (note) *note = "settings: could not write " + store + " (" + err + "); the former files are read in place";
        return false;
    }
    std::string renamed;
    for (const std::string& f : {legacy, display}) {
        if (!IsFile(f)) continue;
        const std::string to = f + kMigratedSuffix;
        std::remove(to.c_str());
        if (std::rename(f.c_str(), to.c_str()) == 0) renamed += (renamed.empty() ? "" : ", ") + f + " -> " + to;
        else renamed += (renamed.empty() ? "" : ", ") + f + " (rename failed; left in place, no longer read)";
    }
    if (note) *note = "settings: moved to " + store + " (" + renamed + ")";
    return true;
}

bool ConfigStore::Set(const std::string& key, const std::string& value, std::string* err) {
    if (active_.path.empty()) {
        if (err) *err = "no settings file resolved";
        return false;
    }
    return SettingsFile::Set(active_.path, key, value, err);
}

bool ConfigStore::Get(const std::string& key, std::string* value) {
    std::string text;
    if (active_.path.empty() || !ReadText(active_.path, &text)) return false;
    return SettingsFile::GetFromText(text, key, value);
}

std::vector<std::pair<std::string, std::string>> ConfigStore::ReadAllFrom(const std::string& path) {
    std::string text;
    if (!ReadText(path, &text)) return {};
    return SettingsFile::Entries(text);
}

std::vector<std::pair<std::string, std::string>> ConfigStore::ReadAll() { return ReadAllFrom(active_.path); }

}  // namespace ps2r
