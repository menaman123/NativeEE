// core/config/config_loader.cpp -- see config_loader.h.
#include "engine/core/config/config_loader.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "engine/core/config/config_store.h"
#include "engine/core/config/config_validator.h"
#include "engine/core/config/deterministic_profile.h"

namespace ps2r {

namespace {

std::string Trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Strips a trailing '#' or ';' comment (outside quotes are not a concern for
// this grammar: values never contain either character).
std::string StripComment(const std::string& line) {
    size_t cut = line.size();
    const size_t h = line.find('#');
    const size_t s = line.find(';');
    if (h != std::string::npos) cut = h;
    if (s != std::string::npos && s < cut) cut = s;
    return line.substr(0, cut);
}

bool ReadFile(const std::string& path, std::string* out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

const char* const kUsage =
    "nativeee [options]\n"
    "  --config <file>       the settings file to use (read and written); default <executable dir>/nativeee.ini when\n"
    "                        present, else <user data>/nativeee.ini\n"
    "  --set key=value       set one Config field (repeatable; ';' joins several). Every hle.* key is\n"
    "                        settable: `--set hle.<key>=off` turns one replacement back off per run and\n"
    "                        the keys that depend on it degrade with a `[config] ... disabled: requires ...`\n"
    "                        line instead of a refusal. The only harness.* keys it accepts are the root\n"
    "                        acceptance opt-ins harness.legacy_input_save / harness.live_restore and the\n"
    "                        fault-injection pin harness.inject_fault_frame=N and the savestate trigger\n"
    "                        harness.save_at_frame=N / harness.restore_at_frame=N and the synthetic key press\n"
    "                        harness.inject_key=F5@N[,F8@M], the simulated display harness.display_hz=N and the\n"
    "                        scripted display changes harness.display_sequence=N:<mode>[,M:<mode>] and the\n"
    "                        live pad probe harness.live_pad_probe=on (a replay reads the live pad too) and the\n"
    "                        window close harness.close_at_frame=N (a real SDL_QUIT at boundary N, or\n"
    "                        harness.close_after_ms=M ms after it, from main) and the virtual gamepad\n"
    "                        harness.virtual_pad=attach@N[,press@K-L][,detach@M] (a hot-plugged SDL pad)\n"
    "                        (harness legs only; files refuse them; every other\n"
    "                        harness.* has its flag). F5/F8 savestates: `--set save.slot=N` picks the slot,\n"
    "                        `--set paths.save_dir=<dir>` where <dir>/savestates/slot<N>.ps2st lands\n"
    "                        (default: the --harness-out dir)\n"
    "  --profile <name>      apply a preset: default | deterministic\n"
    "  --headless            no window (harness runs)\n"
    "  --harness-out <dir>   write the harness leg (frames/audio/mem/run.meta) here\n"
    "  --frames <N>          exit after N frame boundaries\n"
    "  --load-state <path>   restore a savestate before running\n"
    "  --inputs <padlog>     replay a recorded input script\n"
    "  --record-input [file] record served pad state into the leg (default <out dir>/input.padlog)\n"
    "  --state-every <K>     drop a savestate every K boundaries into the leg\n"
    "  --warp-to <N>         fast-forward to frame N\n"
    "  --iso <path>          disc image\n"
    "  --game-files <dir>    the title's ELF / disc-side files (paths.game_files; default <user data>/game_files)\n"
    "  --extract-disc <image> first-run extraction without a window: verify the disc image (ISO9660, the title's\n"
    "                        ELF present with the SHA-256 this build recompiled), write the game files + memcards +\n"
    "                        settings + stamp under the user data root, exit (0 ok, 1 refused). The CLI route always\n"
    "                        copies the image under game_files (a raw .bin is converted to a plain .iso)\n"
    "  --launcher            show the launcher / settings screen before the game even when the files exist\n"
    "  --launcher-screenshot <ppm> with --launcher: write the launcher window's 3rd presented frame (P6) and exit\n"
    "  --launcher-then-play  press Play as soon as the launcher's Ready screen is up (after the screenshot when one\n"
    "                        is asked for): the launcher -> game hand-off; the game then runs with the remaining flags\n"
    "  --launcher-disc <image> the scripted drop: the launcher verifies and extracts this image at open (first-run drill)\n"
    "  --launcher-page <name> the Ready page the launcher opens on: play | display | controls | saves | fixes | data\n"
    "  --display-ini <path>  a former display.ini to honour (compatibility; the settings file carries these keys now)\n"
    "  --save-display        write that display.ini back from the loaded values\n"
    "  --harness-window      allow harness.* with a window\n"
    "  --vu1-library <name>  title (default) | none: none turns the VU1 HLE library consult off\n"
    "                        (every program runs on the engine.vu1_mode tier); a harness knob, not a Config key\n"
    "  --vu1-miss-cases <dir> write the first 4 kicks of every VU1 program the library MISSES as <dir>/<unit>_<variant>_<pc>_<n>.bin\n"
    "                        (Vu1Case records for vu1_replay --hle; run.meta vu1_hle_miss_cases=); a harness knob, not a Config key\n"
    "  --watch-word <list>   0xADDR[,0xADDR...]: diagnostics builds log every CPU store to these guest words\n"
    "                        (<harness out dir>/watch.log, run.meta watch_words=); a harness knob, not a Config key\n"
    "  --watch-flush-from <N> diagnostics builds rewrite watch.log every 64 recorded stores from boundary N on\n"
    "                        (a leg that spins inside one frame still leaves its writers on disk)\n"
    "  --watch-record-from <N> diagnostics builds record --watch-word stores only from boundary N on (earlier\n"
    "                        stores are not counted: a hot word no longer fills the census cap before the frame of interest)\n"
    "  --vu0-trace <list>    0xPC[,0xPC...]: diagnostics builds record the COP2 block before/after every VCALLMS\n"
    "                        of these VU0 micro pcs (<harness out dir>/vu0_trace.log, run.meta vu0_trace=); a harness knob\n"
    "  --dump-frames <dir>   write every presented frame as <dir>/f<key>.ppm (the frames lane's image, top-down);\n"
    "  --dump-textures <dir> write every converted texture as <dir>/<hash>.png (+ index.tsv, contact_sheet.png):\n"
    "                        the TextureContentHash a texture pack names (textures/<pack>/<hash>.png)\n"
    "                        a harness knob, not a Config key\n"
    "  --bench-report <path> diagnostics bench report path\n"
    "  --log-level <level>   quiet | error | warn | info | narrate\n"
    "  --version | -v        print the build id\n"
    "  --help | -h           this text\n";

// harness.* is CLI plumbing (--headless/--harness-out/--frames/...), never a
// setting: a file, --set or PS2R_SET naming it is refused instead of being
// silently discarded (Review 1A finding 2). The two root acceptance opt-ins
// (harness.legacy_input_save, harness.live_restore) have no flag of their own:
// they are the command line's `--set` route and nothing else -- a file still
// refuses them, so a profile cannot approve a legacy migration or a live F8.
const char kHarnessRefusal[] = ": harness.* is set by the CLI flags only (--headless, --harness-out, --frames, ...)";
const char kHarnessFileRefusal[] = ": harness.* is set by the command line only (--set harness.legacy_input_save=on | "
                                   "--set harness.live_restore=on | --set harness.inject_fault_frame=N | "
                                   "--set harness.save_at_frame=N | --set harness.restore_at_frame=N | --set harness.inject_key=F5@N | "
                                   "--set harness.display_hz=N | --set harness.display_sequence=N:<mode> | "
                                   "--set harness.live_pad_probe=on | --set harness.close_at_frame=N | --set harness.close_after_ms=M | --set harness.virtual_pad=attach@N,...)";
bool IsHarnessKey(const std::string& key) { return key.rfind("harness.", 0) == 0; }
bool IsHarnessOptIn(const std::string& key) {
    return key == "harness.legacy_input_save" || key == "harness.live_restore" ||
           key == "harness.inject_fault_frame" || key == "harness.save_at_frame" ||
           key == "harness.restore_at_frame" || key == "harness.inject_key" || key == "harness.display_hz" ||
           key == "harness.display_sequence" || key == "harness.live_pad_probe" ||
           key == "harness.close_at_frame" || key == "harness.close_after_ms" ||
           key == "harness.virtual_pad";
}

}  // namespace

const char* ConfigLoader::Usage() { return kUsage; }

ConfigLoader::HarnessRequest ConfigLoader::request_;

bool ConfigLoader::ApplyIniText(const std::string& text, Config& c, std::string* err) {
    std::istringstream in(text);
    std::string line, section;
    int lineno = 0;
    while (std::getline(in, line)) {
        lineno++;
        std::string l = Trim(StripComment(line));
        if (l.empty()) continue;
        if (l.front() == '[' && l.back() == ']') {
            section = Trim(l.substr(1, l.size() - 2));
            continue;
        }
        const size_t eq = l.find('=');
        if (eq == std::string::npos) {
            if (err) *err = "ini line " + std::to_string(lineno) + ": expected key=value";
            return false;
        }
        std::string key = Trim(l.substr(0, eq));
        const std::string val = Trim(l.substr(eq + 1));
        if (!section.empty() && key.find('.') == std::string::npos) key = section + "." + key;
        std::string e;
        if (IsHarnessKey(key)) {
            if (err) *err = "ini line " + std::to_string(lineno) + ": " + key +
                            (IsHarnessOptIn(key) ? kHarnessFileRefusal : kHarnessRefusal);
            return false;
        }
        if (!c.Set(key, val, &e)) {
            if (err) *err = "ini line " + std::to_string(lineno) + ": " + e;
            return false;
        }
    }
    return true;
}

bool ConfigLoader::ApplyIniFile(const std::string& path, Config& c, std::string* err,
                                bool* existed) {
    std::string text;
    const bool ok = ReadFile(path, &text);
    if (existed) *existed = ok;
    if (!ok) return true;   // absent file == defaults
    if (!ApplyIniText(text, c, err)) {
        if (err) *err = path + ": " + *err;
        return false;
    }
    return true;
}

bool ConfigLoader::ApplyDisplayIniText(const std::string& text, Config& c) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        // The original loader: strip a ';' comment, split at '=', trim, and
        // ignore unknown keys / unknown enum names (forward compatibility).
        const size_t sc = line.find(';');
        if (sc != std::string::npos) line.resize(sc);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Trim(line.substr(0, eq));
        const std::string val = Trim(line.substr(eq + 1));
        if (key == "mode") {
            Config probe;
            if (probe.Set("video.fit_mode", val, nullptr)) c.video.fit_mode = probe.video.fit_mode;
        } else if (key == "aspect") {
            Config probe;
            if (probe.Set("video.aspect", val, nullptr)) c.video.aspect = probe.video.aspect;
        } else if (key == "custom_scale") {
            c.video.custom_scale_pct = Clamp(std::atoi(val.c_str()), 25, 400);
        } else if (key == "custom_offset_x") {
            c.video.custom_offset_x = Clamp(std::atoi(val.c_str()), -8192, 8192);
        } else if (key == "custom_offset_y") {
            c.video.custom_offset_y = Clamp(std::atoi(val.c_str()), -8192, 8192);
        }
    }
    return true;
}

bool ConfigLoader::ApplyDisplayIniFile(const std::string& path, Config& c) {
    std::string text;
    if (!ReadFile(path, &text)) return false;
    return ApplyDisplayIniText(text, c);
}

std::string ConfigLoader::RenderDisplayIni(const Config& c) {
    std::string mode, aspect;
    c.Get("video.fit_mode", &mode);
    c.Get("video.aspect", &aspect);
    std::string out;
    out += "; NativeEE display settings\n";
    out += "; mode = stretch | fit | fill | integer | custom\n";
    out += "; aspect = auto | 4:3 | 16:9 | 16:10 | 21:9   (the shape the title renders for)\n";
    out += "mode = " + mode + "\n";
    out += "aspect = " + aspect + "\n";
    out += "custom_scale = " + std::to_string(c.video.custom_scale_pct) + "\n";
    out += "custom_offset_x = " + std::to_string(c.video.custom_offset_x) + "\n";
    out += "custom_offset_y = " + std::to_string(c.video.custom_offset_y) + "\n";
    return out;
}

bool ConfigLoader::ApplySetBlob(const std::string& blob, Config& c, std::string* err,
                                bool* touched_latency_model) {
    size_t start = 0;
    while (start < blob.size()) {
        size_t end = blob.find(';', start);
        if (end == std::string::npos) end = blob.size();
        const std::string item = Trim(blob.substr(start, end - start));
        start = end + 1;
        if (item.empty()) continue;
        const size_t eq = item.find('=');
        if (eq == std::string::npos) {
            if (err) *err = "--set expects key=value, got '" + item + "'";
            return false;
        }
        const std::string key = Trim(item.substr(0, eq));
        if (IsHarnessKey(key) && !IsHarnessOptIn(key)) {
            if (err) *err = key + kHarnessRefusal;
            return false;
        }
        if (!c.Set(key, Trim(item.substr(eq + 1)), err)) return false;
        if (touched_latency_model && key == "cdvd.latency_model") *touched_latency_model = true;
    }
    return true;
}

ConfigLoader::Result ConfigLoader::Load(int argc, char** argv, Config& out) {
    Result r;
    out = Config();
    std::string config_file, profile, log_level;
    std::vector<std::string> sets;
    bool latency_explicit = false;

    auto value = [&](int& i, const char* flag) -> const char* {
        if (i + 1 >= argc) {
            r.error = std::string(flag) + " requires a value";
            return nullptr;
        }
        return argv[++i];
    };
    auto set_int = [&](const char* key, const char* v) {
        std::string e;
        if (!out.Set(key, v, &e)) r.error = e;
    };

    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        const char* v = nullptr;
        if (!std::strcmp(a, "--version") || !std::strcmp(a, "-v")) { r.show_version = true; continue; }
        if (!std::strcmp(a, "--help") || !std::strcmp(a, "-h")) { r.show_help = true; continue; }
        if (!std::strcmp(a, "--headless")) { out.harness.headless = true; r.harness_cli = true; continue; }
        if (!std::strcmp(a, "--launcher")) { r.launcher = true; continue; }
        if (!std::strcmp(a, "--launcher-then-play")) { r.launcher_then_play = true; continue; }
        if (!std::strcmp(a, "--record-input")) {
            out.harness.record_input = true;
            // Optional destination: the next argument when it is not another flag.
            if (i + 1 < argc && argv[i + 1][0] != '-' && argv[i + 1][0] != '\0') out.harness.record_input_path = argv[++i];
            continue;
        }
        if (!std::strcmp(a, "--save-display")) { r.save_display = true; continue; }
        if (!std::strcmp(a, "--harness-window")) { r.harness_window = true; continue; }
        if (!std::strcmp(a, "--config")) { if ((v = value(i, a))) { config_file = v; r.harness_cli = true; } }
        else if (!std::strcmp(a, "--set")) { if ((v = value(i, a))) { sets.push_back(v); r.harness_cli = true; } }
        else if (!std::strcmp(a, "--profile")) { if ((v = value(i, a))) { profile = v; r.harness_cli = true; } }
        else if (!std::strcmp(a, "--harness-out")) { if ((v = value(i, a))) { out.harness.out_dir = v; r.harness_cli = true; } }
        else if (!std::strcmp(a, "--frames")) { if ((v = value(i, a))) { set_int("harness.frames", v); r.harness_cli = true; } }
        else if (!std::strcmp(a, "--load-state")) { if ((v = value(i, a))) { out.harness.load_state = v; r.harness_cli = true; } }
        else if (!std::strcmp(a, "--inputs")) { if ((v = value(i, a))) { out.harness.inputs = v; r.harness_cli = true; } }
        else if (!std::strcmp(a, "--launcher-screenshot")) {
            if ((v = value(i, a))) {
                if (!*v) r.error = "--launcher-screenshot requires a path";
                else r.launcher_screenshot = v;
            }
        }
        else if (!std::strcmp(a, "--launcher-disc")) {
            if ((v = value(i, a))) {
                if (!*v) r.error = "--launcher-disc requires a disc image path";
                else r.launcher_disc = v;
            }
        }
        else if (!std::strcmp(a, "--launcher-page")) {
            if ((v = value(i, a))) {
                if (!*v) r.error = "--launcher-page requires a page name";
                else r.launcher_page = v;
            }
        }
        else if (!std::strcmp(a, "--extract-disc")) {
            if ((v = value(i, a))) {
                if (!*v) r.error = "--extract-disc requires a disc image path";
                else r.extract_disc = v;
            }
        }
        else if (!std::strcmp(a, "--state-every")) { if ((v = value(i, a))) set_int("harness.state_every", v); }
        else if (!std::strcmp(a, "--warp-to")) { if ((v = value(i, a))) set_int("harness.warp_to_frame", v); }
        else if (!std::strcmp(a, "--iso")) { if ((v = value(i, a))) { out.paths.iso = v; r.harness_cli = true; } }
        else if (!std::strcmp(a, "--game-files")) { if ((v = value(i, a))) { out.paths.game_files = v; r.harness_cli = true; } }
        else if (!std::strcmp(a, "--display-ini")) { if ((v = value(i, a))) r.display_ini_path = v; }
        else if (!std::strcmp(a, "--bench-report")) { if ((v = value(i, a))) r.bench_report_path = v; }
        else if (!std::strcmp(a, "--log-level")) { if ((v = value(i, a))) log_level = v; }
        else if (!std::strcmp(a, "--vu1-library")) {
            if ((v = value(i, a))) {
                if (!std::strcmp(v, "none")) r.vu1_library_none = true;
                else if (!std::strcmp(v, "title")) r.vu1_library_none = false;
                else r.error = std::string("--vu1-library expects title | none, got '") + v + "'";
            }
        }
        else if (!std::strcmp(a, "--vu1-miss-cases")) {
            if ((v = value(i, a))) {
                if (!*v) r.error = "--vu1-miss-cases requires a directory";
                else r.vu1_miss_cases_dir = v;
            }
        }
        else if (!std::strcmp(a, "--watch-word")) {
            if ((v = value(i, a))) {
                if (!*v) r.error = "--watch-word requires a value";
                else r.watch_words = r.watch_words.empty() ? std::string(v) : r.watch_words + "," + v;
            }
        }
        else if (!std::strcmp(a, "--watch-flush-from")) {
            if ((v = value(i, a))) {
                char* end = nullptr;
                const unsigned long long n = std::strtoull(v, &end, 10);
                if (!*v || !end || *end) r.error = "--watch-flush-from requires a boundary number";
                else r.watch_flush_from = n;
            }
        }
        else if (!std::strcmp(a, "--watch-record-from")) {
            if ((v = value(i, a))) {
                char* end = nullptr;
                const unsigned long long n = std::strtoull(v, &end, 10);
                if (!*v || !end || *end) r.error = "--watch-record-from requires a boundary number";
                else r.watch_record_from = n;
            }
        }
        else if (!std::strcmp(a, "--dump-frames")) {
            if ((v = value(i, a))) {
                if (!*v) r.error = "--dump-frames requires a directory";
                else r.dump_frames_dir = v;
            }
        }
        else if (!std::strcmp(a, "--dump-textures")) {
            if ((v = value(i, a))) {
                if (!*v) r.error = "--dump-textures requires a directory";
                else r.dump_textures_dir = v;
            }
        }
        else if (!std::strcmp(a, "--vu0-trace")) {
            if ((v = value(i, a))) {
                if (!*v) r.error = "--vu0-trace requires a value";
                else r.vu0_trace = r.vu0_trace.empty() ? std::string(v) : r.vu0_trace + "," + v;
            }
        }
        else { r.error = std::string("unknown option '") + a + "'"; }
        if (!r.error.empty()) return r;
    }
    if (r.show_version || r.show_help) {
        r.ok = true;
        return r;
    }

    // The CLI path flags above are plumbing, not settings: remember them and
    // re-apply after the file tiers so a file cannot override the command
    // line. harness.* needs no re-apply: the tiers refuse it (IsHarnessKey).
    const PathsConfig   cli_paths = out.paths;

    // Tier: the settings store (ConfigStore). The root the store lives under when
    // no --config / executable-folder store applies: the search dir (tests), else
    // `--set paths.user_data=` when the command line pins one (a leg's sandbox),
    // else the platform's user data root (C4), else the cwd.
    std::string root = search_dir_;
    if (root.empty()) {
        for (const std::string& blob : sets) {
            Config probe;
            std::string ignored;
            ApplySetBlob(blob, probe, &ignored, nullptr);
            if (!probe.paths.user_data.empty()) root = probe.paths.user_data;
        }
    }
    if (root.empty()) root = user_data_root_;
    if (root.empty()) root = ".";
    ConfigStore::Resolution store = ConfigStore::Resolve(config_file, exe_dir_, root, migrate_legacy_);
    ConfigStore::SetActive(store);
    r.settings_file = store.path;
    r.settings_source = store.source;
    r.settings_migrated = store.migrated;
    for (const std::string& n : store.notes) r.notes.push_back(n);
    bool existed = false;
    if (!config_file.empty() && !store.exists) {
        r.error = "--config " + config_file + ": cannot open";
        return r;
    }
    if (store.exists) {
        if (!ApplyIniFile(store.path, out, &r.error, &existed)) return r;
        if (existed) r.sources.push_back(store.path);
    }
    // Legacy files the migration left to read (a refused conversion), then an
    // explicitly named display.ini (compatibility).
    for (const std::string& legacy : store.legacy_in_place) {
        if (legacy.size() >= 11 && legacy.compare(legacy.size() - 11, 11, "display.ini") == 0) {
            if (ApplyDisplayIniFile(legacy, out)) r.sources.push_back(legacy);
        } else {
            if (!ApplyIniFile(legacy, out, &r.error, &existed)) return r;
            if (existed) r.sources.push_back(legacy);
        }
    }
    if (!r.display_ini_path.empty() && ApplyDisplayIniFile(r.display_ini_path, out)) r.sources.push_back(r.display_ini_path);

    // Tier: --set (repeatable).
    for (const std::string& s : sets) {
        if (!ApplySetBlob(s, out, &r.error, &latency_explicit)) return r;
        r.sources.push_back("--set " + s);
    }
    if (!log_level.empty()) {
        if (!out.Set("dev.log_level", log_level, &r.error)) return r;
    }

    // Tier: --profile.
    // M12: in the service-only binary an explicit `hle.startup=retained` (any
    // tier before the profile) is a refusal; the profile's own retained pin is
    // re-homed to the oracle binary and becomes `native` here, with a note.
    const bool retained_explicit = out.hle.startup == StartupMode::Retained;
    if (!profile.empty()) {
        if (!DeterministicProfile::ApplyNamed(profile.c_str(), out)) {
            r.error = "unknown profile '" + profile + "' (default | deterministic)";
            return r;
        }
        latency_explicit = true;   // the profile pins cdvd.latency_model
        r.sources.push_back("--profile " + profile);
    }
    if (!retained_startup_available_) {
        if (retained_explicit) {
            r.error = "hle.startup=retained: the retained (IOP-booting) startup arm is not compiled into this "
                      "service-only binary (nativeee); the oracle binary nativeee_oracle carries it";
            return r;
        }
        if (out.hle.startup == StartupMode::Retained) {
            out.hle.startup = StartupMode::Native;
            r.notes.push_back("hle.startup=native (the profile's retained pin is the oracle binary's; this "
                              "service-only binary has no IOP to boot)");
        }
    }

#ifndef NDEBUG
    // Dev builds only: the ONE getenv site in the tree (docs: STAGE_RULES,
    // ARCHITECTURE section 8). Harness runners use it to pin a field without
    // editing the command line; Release builds do not read the environment.
    if (const char* blob = std::getenv("PS2R_SET"); blob && *blob) {
        if (!ApplySetBlob(blob, out, &r.error, &latency_explicit)) {
            r.error = "PS2R_SET: " + r.error;
            return r;
        }
        r.sources.push_back("PS2R_SET");
    }
#endif

    if (!cli_paths.iso.empty()) out.paths.iso = cli_paths.iso;
    if (cli_paths.game_files != PathsConfig().game_files) out.paths.game_files = cli_paths.game_files;
    {
        const std::string root = ResolveUserPaths(out, user_data_root_);
        if (!root.empty()) r.sources.push_back("user_data " + root);
    }

    ConfigValidator::ApplyDependentDefaults(out, latency_explicit);
    // A prerequisite the user turned off degrades its dependents (one note
    // each) instead of refusing the run; Validate then holds for the hle.* DAG.
    // The one edge that cannot degrade in the service-only binary -- a native
    // startup whose consumer went off -- refuses here with the key named (M12).
    {
        std::string startup_refusal;
        std::vector<std::string> degrade_notes = ConfigValidator::DegradeDependents(out, retained_startup_available_, &startup_refusal);
        r.notes.insert(r.notes.end(), degrade_notes.begin(), degrade_notes.end());
        if (!startup_refusal.empty()) {
            // The chain that led here (a `--set` of another key cascades through the
            // DAG before it reaches startup): every degrade note, so the user sees
            // which key they turned off.
            r.error = startup_refusal;
            if (!degrade_notes.empty()) {
                r.error += " [degraded first:";
                for (const std::string& n : degrade_notes) r.error += " " + n + ";";
                r.error += "]";
            }
            return r;
        }
    }
    if (!ConfigValidator::Validate(out, r.harness_window, &r.error)) return r;
    request_ = HarnessRequest{};
    request_.vu1_library_none = r.vu1_library_none;
    request_.vu1_miss_cases_dir = r.vu1_miss_cases_dir;
    request_.watch_words = r.watch_words;
    request_.watch_flush_from = r.watch_flush_from;
    request_.watch_record_from = r.watch_record_from;
    request_.vu0_trace = r.vu0_trace;
    request_.dump_frames_dir = r.dump_frames_dir;
    request_.dump_textures_dir = r.dump_textures_dir;
    if (r.vu1_library_none) r.sources.push_back("--vu1-library none");
    if (!r.vu1_miss_cases_dir.empty()) r.sources.push_back("--vu1-miss-cases " + r.vu1_miss_cases_dir);
    if (!r.watch_words.empty()) r.sources.push_back("--watch-word " + r.watch_words);
    if (r.watch_flush_from) r.sources.push_back("--watch-flush-from " + std::to_string(r.watch_flush_from));
    if (r.watch_record_from) r.sources.push_back("--watch-record-from " + std::to_string(r.watch_record_from));
    if (!r.vu0_trace.empty()) r.sources.push_back("--vu0-trace " + r.vu0_trace);
    if (!r.dump_frames_dir.empty()) r.sources.push_back("--dump-frames " + r.dump_frames_dir);
    if (!r.dump_textures_dir.empty()) r.sources.push_back("--dump-textures " + r.dump_textures_dir);
    r.ok = true;
    return r;
}

std::string ConfigLoader::ResolveUserPaths(Config& c, const std::string& platform_root) {
    const std::string root = !c.paths.user_data.empty() ? c.paths.user_data : platform_root;
    const auto under = [&root](std::string& field, const char* name) {
        if (!field.empty()) return;
        field = root.empty() ? std::string(name) : root + "/" + name;
    };
    under(c.paths.mc_dir, "memcards");
    under(c.paths.game_files, "game_files");
    under(c.paths.log_dir, "logs");
    under(c.paths.texture_packs, "textures");
    if (c.paths.user_data.empty()) c.paths.user_data = root;
    return root;
}

}  // namespace ps2r
