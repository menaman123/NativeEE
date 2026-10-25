#include "app/host/savestate_slots.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

#include "engine/core/config/config.h"
#include "engine/core/log/log.h"
#include "engine/platform/host/platform.h"

namespace ps2r::app {
namespace {
constexpr const char* kDirectory = "savestates";
constexpr const char* kPrefix = "slot";
constexpr const char* kExtension = ".ps2st";

bool WriteAtomically(const std::string& path, const std::string& text, std::string* err) {
    const std::string temporary = path + ".tmp";
    std::FILE* file = std::fopen(temporary.c_str(), "wb");
    if (!file) {
        if (err) *err = "cannot create " + temporary;
        return false;
    }
    const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    const bool closed = std::fclose(file) == 0;
    std::error_code ec;   // std::filesystem::rename replaces an existing file on Windows too; std::rename does not
    if (written && closed) std::filesystem::rename(temporary, path, ec);
    if (!written || !closed || ec) {
        std::remove(temporary.c_str());
        if (err) *err = "cannot publish " + path;
        return false;
    }
    return true;
}
}  // namespace

std::string SavestateSlots::Root(const Config& config) {
    if (!config.paths.save_dir.empty()) return config.paths.save_dir;
    if (!config.harness.out_dir.empty()) return config.harness.out_dir;
    return config.paths.log_dir.empty() ? std::string("logs") : config.paths.log_dir;
}

std::string SavestateSlots::Directory(const Config& config) {
    return (std::filesystem::path(Root(config)) / kDirectory).string();
}

std::string SavestateSlots::SlotPath(const std::string& directory, int slot) {
    return (std::filesystem::path(directory) / (std::string(kPrefix) + std::to_string(slot < 0 ? 0 : slot) + kExtension))
        .string();
}

std::string SavestateSlots::SlotPath(const Config& config) { return SlotPath(Directory(config), config.save.slot); }

std::string SavestateSlots::MetaPath(const std::string& slot_path) { return slot_path + ".meta"; }

std::string SavestateSlots::Composition(const Config& config) {
    std::string out;
    for (const std::string& key : Config::Keys()) {
        if (key.rfind("hle.", 0) != 0) continue;
        std::string value;
        config.Get(key, &value);
        out += key + "=" + value + ";";
    }
    out += "profile=" + config.meta.profile;
    return out;
}

std::string SavestateSlots::RenderMeta(const Meta& meta) {
    std::string out;
    out += "frame=" + std::to_string(meta.frame) + "\n";
    out += "wall_ms=" + std::to_string(meta.wall_ms) + "\n";
    out += "build_id=" + meta.build_id + "\n";
    out += "composition=" + meta.composition + "\n";
    out += "sections=";
    for (size_t i = 0; i < meta.sections.size(); ++i) out += (i ? "," : "") + meta.sections[i];
    out += "\n";
    return out;
}

bool SavestateSlots::WriteMeta(const std::string& slot_path, Meta meta, std::string* err) {
    savestate::FileMeta file_meta;
    std::vector<savestate::Blob> sections;
    if (!savestate::ReadStateFile(slot_path, &file_meta, &sections, err)) return false;
    meta.sections.clear();
    for (const auto& blob : sections)
        meta.sections.push_back(savestate::TagName(blob.tag) + ":v" + std::to_string(blob.version));
    return WriteAtomically(MetaPath(slot_path), RenderMeta(meta), err);
}

bool SavestateSlots::ReadMeta(const std::string& meta_path, Meta* meta, std::string* err) {
    std::ifstream in(meta_path);
    if (!in) {
        if (err) *err = "no sidecar " + meta_path;
        return false;
    }
    Meta out;
    bool composition_seen = false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "frame") out.frame = std::strtoull(value.c_str(), nullptr, 10);
        else if (key == "wall_ms") out.wall_ms = std::strtoull(value.c_str(), nullptr, 10);
        else if (key == "build_id") out.build_id = value;
        else if (key == "composition") { out.composition = value; composition_seen = true; }
        else if (key == "sections") {
            out.sections.clear();
            size_t start = 0;
            while (start < value.size()) {
                const size_t comma = value.find(',', start);
                const size_t end = comma == std::string::npos ? value.size() : comma;
                if (end > start) out.sections.push_back(value.substr(start, end - start));
                start = end + 1;
            }
        }
    }
    if (!composition_seen) {
        if (err) *err = "sidecar " + meta_path + " has no composition= line";
        return false;
    }
    if (meta) *meta = std::move(out);
    return true;
}

namespace {
// "k=v;k=v;…" -> ordered (key, value) pairs; a bare "v" (no '=') keeps an empty key.
std::vector<std::pair<std::string, std::string>> SplitComposition(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> out;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t semi = text.find(';', start);
        const size_t end = semi == std::string::npos ? text.size() : semi;
        if (end > start) {
            const std::string field = text.substr(start, end - start);
            const size_t eq = field.find('=');
            if (eq == std::string::npos) out.emplace_back(field, std::string());
            else out.emplace_back(field.substr(0, eq), field.substr(eq + 1));
        }
        if (semi == std::string::npos) break;
        start = semi + 1;
    }
    return out;
}
}  // namespace

std::string SavestateSlots::CompositionDifference(const std::string& saved, const std::string& live) {
    if (saved == live) return std::string();
    const auto a = SplitComposition(saved);
    const auto b = SplitComposition(live);
    auto find = [](const std::vector<std::pair<std::string, std::string>>& in, const std::string& key) -> const std::string* {
        for (const auto& kv : in) if (kv.first == key) return &kv.second;
        return nullptr;
    };
    std::string out;
    auto add = [&](const std::string& key, const std::string& s, const std::string& l) {
        out += (out.empty() ? "" : "; ") + key + "(saved=" + s + ",live=" + l + ")";
    };
    for (const auto& kv : a) {
        const std::string* l = find(b, kv.first);
        if (!l) add(kv.first, kv.second, "<absent>");
        else if (*l != kv.second) add(kv.first, kv.second, *l);
    }
    for (const auto& kv : b)
        if (!find(a, kv.first)) add(kv.first, "<absent>", kv.second);
    // Equal field sets in another order (or a malformed string) still differ as text.
    if (out.empty()) out = "field order or text (saved=" + saved + ",live=" + live + ")";
    return out;
}

bool SavestateSlots::PreflightComposition(const std::string& slot_path, const std::string& live_composition,
                                          const std::string& live_build_id, std::string* err, std::string* warning) {
    Meta meta;
    std::string read_error;
    if (!ReadMeta(MetaPath(slot_path), &meta, &read_error)) {
        if (err) *err = read_error + " -- the slot's composition cannot be checked against this session";
        return false;
    }
    const std::string difference = CompositionDifference(meta.composition, live_composition);
    if (!difference.empty()) {
        if (err)
            *err = "composition differs: " + difference +
                   " -- the slot was captured under another composition; its --load-state would refuse after this "
                   "session closed, so the session stays open";
        return false;
    }
    if (warning) {
        warning->clear();
        if (meta.build_id != live_build_id)
            *warning = "build_id differs: saved=" + meta.build_id + ", live=" + live_build_id +
                       " -- the image is of another build of this composition; restoring anyway";
    }
    return true;
}

std::string SavestateSlots::RunningImage(std::string* err) {
    const std::string image = ps2r::platform::host::ExecutablePath(err);
    if (image.empty()) return std::string();
    // The running image must still be there to relaunch (the F8 restore): a regular file
    // (POSIX' X_OK is not a Win32 notion; the image we are running from is executable).
    std::error_code ec;
    if (!std::filesystem::is_regular_file(image, ec)) {
        if (err) *err = image + ": " + (ec ? ec.message() : std::string("not a regular file"));
        return std::string();
    }
    return image;
}

std::string SavestateSlots::Latest(const std::string& directory, std::string* err) {
    std::error_code ec;
    std::string best;
    std::filesystem::file_time_type best_time{};
    const std::string prefix = kPrefix;
    const std::string extension = kExtension;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec) break;
        const std::string name = entry.path().filename().string();
        if (name.size() <= prefix.size() + extension.size() || name.rfind(prefix, 0) != 0 ||
            name.compare(name.size() - extension.size(), extension.size(), extension) != 0)
            continue;
        const std::string digits = name.substr(prefix.size(), name.size() - prefix.size() - extension.size());
        if (digits.empty() || !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; }))
            continue;
        std::error_code tec;
        const auto when = entry.last_write_time(tec);
        if (tec) continue;
        if (best.empty() || when > best_time) {
            best = entry.path().string();
            best_time = when;
        }
    }
    if (best.empty() && err)
        *err = "No savestate slot (" + std::string(kPrefix) + "<N>" + kExtension + ") under " + directory +
               " -- press F5 first.";
    return best;
}

bool SavestateSlots::Preflight(const std::string& path, const savestate::SectionRegistry& registry,
                               std::string* err, savestate::FileMeta* meta) {
    savestate::FileMeta file_meta;
    std::vector<savestate::Blob> sections;
    if (!savestate::ReadStateFile(path, &file_meta, &sections, err)) return false;
    if (meta) *meta = file_meta;
    for (const auto& spec : registry.Specs()) {
        if (!spec.required && !spec.capture_required) continue;
        const bool present = std::any_of(sections.begin(), sections.end(),
                                         [&](const savestate::Blob& blob) { return blob.tag == spec.tag; });
        if (present) continue;
        if (err)
            *err = "required section missing: " + savestate::TagName(spec.tag) + " (" +
                   (spec.owner && *spec.owner ? spec.owner : "unnamed owner") +
                   ") -- the image was not captured under this composition";
        return false;
    }
    return true;
}

std::string SavestateSlots::TriggerMetaLine(const std::string& state, const std::string& path, uint64_t frame,
                                            uint64_t boundary, const std::string& detail) {
    if (state == "written")
        return "written,path=" + path + ",frame=" + std::to_string(frame) + ",boundary=" + std::to_string(boundary);
    if (state == "refused") return "refused," + detail;
    if (state == "failed") return "failed," + detail;
    return "pending";
}

void SavestateSlots::Report(const char* action, const char* phase, const std::string& detail) {
    std::fprintf(stderr, "nativeee: savestate %s %s: %s\n", action, phase, detail.c_str());
    std::fflush(stderr);
    const std::string p = phase;
    if (p == "Succeeded" || p == "Queued" || p == "Working" || p == "Waiting")
        PS2R_LOG(SAVE, Info, "savestate %s %s: %s", action, phase, detail.c_str());
    else
        PS2R_LOG(SAVE, Warn, "savestate %s %s: %s", action, phase, detail.c_str());
}

std::vector<std::string> SavestateSlots::RelaunchArguments(const std::vector<std::string>& argv,
                                                           const std::string& state_path, const std::string& save_root,
                                                           uint64_t stamp) {
    std::vector<std::string> out;
    const std::string suffix = std::to_string(stamp);
    bool save_dir_set = false;
    auto takes_value = [&](size_t i) { return i + 1 < argv.size() && argv[i + 1].rfind("--", 0) != 0; };
    for (size_t i = 0; i < argv.size(); ++i) {
        const std::string& a = argv[i];
        if (i == 0) { out.push_back(a); continue; }
        if (a == "--load-state" || a == "--inputs" || a == "--warp-to") {
            if (takes_value(i)) ++i;
            continue;
        }
        if (a == "--harness-out" && takes_value(i)) {
            out.push_back(a);
            out.push_back((std::filesystem::path(argv[i + 1]) / "restores" / suffix).generic_string());   // one separator on every host
            ++i;
            continue;
        }
        if (a == "--record-input" && takes_value(i)) {
            std::filesystem::path recording(argv[i + 1]);
            const std::string stem = recording.stem().string();
            const std::string extension = recording.has_extension() ? recording.extension().string() : ".padlog";
            out.push_back(a);
            out.push_back((recording.parent_path() / (stem + "-r" + suffix + extension)).generic_string());
            ++i;
            continue;
        }
        if (a == "--set" && takes_value(i)) {
            const std::string& kv = argv[i + 1];
            ++i;
            // The one-shot harness triggers belong to the leg that pressed
            // them: carried over, the relaunch would save/press again.
            if (kv.rfind("harness.save_at_frame=", 0) == 0 || kv.rfind("harness.restore_at_frame=", 0) == 0) continue;
            if (kv.rfind("paths.save_dir=", 0) == 0) save_dir_set = true;
            out.push_back(a);
            out.push_back(kv);
            continue;
        }
        out.push_back(a);
    }
    if (!save_dir_set && !save_root.empty()) {
        out.push_back("--set");
        out.push_back("paths.save_dir=" + save_root);
    }
    out.push_back("--load-state");
    out.push_back(state_path);
    return out;
}

}  // namespace ps2r::app
