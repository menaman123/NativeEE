// core/config/config.cpp -- string access table, fingerprint.
#include "engine/core/config/config.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

#include "engine/core/hash/hash.h"
#include "engine/platform/host/platform.h"

namespace ps2r {

namespace {

struct Field {
    const char*                                                     key;
    std::function<std::string(const Config&)>                       get;
    std::function<bool(Config&, const std::string&, std::string*)>  set;
};

std::string BoolStr(bool b) { return b ? "on" : "off"; }

bool ParseBool(const std::string& v, bool* out) {
    static const char* const on[]  = {"on", "true", "1", "yes"};
    static const char* const off[] = {"off", "false", "0", "no"};
    for (const char* s : on)
        if (host::StrCaseCmp(v.c_str(), s) == 0) { *out = true; return true; }
    for (const char* s : off)
        if (host::StrCaseCmp(v.c_str(), s) == 0) { *out = false; return true; }
    return false;
}

bool ParseInt(const std::string& v, int* out) {
    if (v.empty()) return false;
    char* end = nullptr;
    long n = std::strtol(v.c_str(), &end, 10);
    if (!end || *end) return false;
    *out = (int)n;
    return true;
}

bool ParseDouble(const std::string& v, double* out) {
    if (v.empty()) return false;
    char* end = nullptr;
    double d = std::strtod(v.c_str(), &end);
    if (!end || *end) return false;
    *out = d;
    return true;
}

std::string DoubleStr(double d) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", d);
    return buf;
}

struct EnumTable {
    const char* const* names;
    int                count;
};

std::string EnumName(const EnumTable& t, int i) {
    return (i >= 0 && i < t.count) ? t.names[i] : "?";
}

bool EnumParse(const EnumTable& t, const std::string& v, int* out) {
    for (int i = 0; i < t.count; i++)
        if (host::StrCaseCmp(v.c_str(), t.names[i]) == 0) { *out = i; return true; }
    return false;
}

const char* const kBackendNames[]   = {"gl", "soft"};
const char* const kFitNames[]       = {"stretch", "fit", "fill", "integer", "custom"};
const char* const kAspectNames[]    = {"auto", "4:3", "16:9", "16:10", "21:9"};
const char* const kTextureFilterNames[] = {"title", "point", "bilinear", "trilinear"};
const char* const kLatencyNames[]   = {"instant", "modelled"};
const char* const kVisibilityNames[] = {"boundary", "poll"};
const char* const kVu1Names[]       = {"hle", "cached", "interp", "block", "aot"};
const char* const kVu1AotFbNames[]  = {"block", "interp"};
const char* const kPacingNames[]    = {"realtime", "freerun"};
const char* const kIopNames[]       = {"realtime", "frame"};
const char* const kTimerDueNames[]  = {"one_period", "at_restore"};
const char* const kLevelNames[]     = {"quiet", "error", "warn", "info", "narrate"};
const char* const kGsQueueNames[]   = {"inline", "thread", "pipelined"};
const char* const kGraphicsStripsNames[] = {"model", "plain", "all"};
const char* const kStartupNames[] = {"retained", "native"};
const char* const kRestoreModeNames[] = {"live", "relaunch"};
const char* const kFullscreenNames[] = {"windowed", "fullscreen", "borderless"};
const char* const kRenderRateNames[] = {"60", "display"};

const EnumTable kBackend{kBackendNames, 2};
const EnumTable kFit{kFitNames, 5};
const EnumTable kAspect{kAspectNames, 5};
const EnumTable kTextureFilter{kTextureFilterNames, 4};
const EnumTable kLatency{kLatencyNames, 2};
const EnumTable kVisibility{kVisibilityNames, 2};
const EnumTable kVu1{kVu1Names, 5};
const EnumTable kVu1AotFb{kVu1AotFbNames, 2};
const EnumTable kPacing{kPacingNames, 2};
const EnumTable kIop{kIopNames, 2};
const EnumTable kTimerDue{kTimerDueNames, 2};
const EnumTable kLevel{kLevelNames, 5};
const EnumTable kGsQueue{kGsQueueNames, 3};
const EnumTable kGraphicsStrips{kGraphicsStripsNames, 3};
const EnumTable kStartup{kStartupNames, 2};
const EnumTable kRestoreMode{kRestoreModeNames, 2};
const EnumTable kFullscreen{kFullscreenNames, 3};
const EnumTable kRenderRate{kRenderRateNames, 2};

bool Fail(std::string* err, const char* key, const std::string& v, const char* what) {
    if (err) *err = std::string(key) + ": bad " + what + " value '" + v + "'";
    return false;
}

#define BOOL_FIELD(KEY, MEMBER)                                                             \
    {KEY, [](const Config& c) { return BoolStr(c.MEMBER); },                               \
     [](Config& c, const std::string& v, std::string* err) {                               \
         bool b;                                                                           \
         if (!ParseBool(v, &b)) return Fail(err, KEY, v, "bool");                          \
         c.MEMBER = b;                                                                     \
         return true;                                                                      \
     }}
#define INT_FIELD(KEY, MEMBER)                                                              \
    {KEY, [](const Config& c) { return std::to_string(c.MEMBER); },                        \
     [](Config& c, const std::string& v, std::string* err) {                               \
         int n;                                                                            \
         if (!ParseInt(v, &n)) return Fail(err, KEY, v, "integer");                        \
         c.MEMBER = n;                                                                     \
         return true;                                                                      \
     }}
#define DOUBLE_FIELD(KEY, MEMBER)                                                           \
    {KEY, [](const Config& c) { return DoubleStr(c.MEMBER); },                             \
     [](Config& c, const std::string& v, std::string* err) {                               \
         double d;                                                                         \
         if (!ParseDouble(v, &d)) return Fail(err, KEY, v, "number");                      \
         c.MEMBER = d;                                                                     \
         return true;                                                                      \
     }}
#define STR_FIELD(KEY, MEMBER)                                                              \
    {KEY, [](const Config& c) { return c.MEMBER; },                                        \
     [](Config& c, const std::string& v, std::string*) {                                   \
         c.MEMBER = v;                                                                     \
         return true;                                                                      \
     }}
#define ENUM_FIELD(KEY, MEMBER, TABLE)                                                      \
    {KEY, [](const Config& c) { return EnumName(TABLE, (int)c.MEMBER); },                  \
     [](Config& c, const std::string& v, std::string* err) {                               \
         int i;                                                                            \
         if (!EnumParse(TABLE, v, &i)) return Fail(err, KEY, v, "enum");                   \
         c.MEMBER = static_cast<decltype(c.MEMBER)>(i);                                    \
         return true;                                                                      \
     }}

// The schema table. Order here is the canonical (sorted) key order.
const std::vector<Field>& Fields() {
    static const std::vector<Field> f = {
        BOOL_FIELD("audio.enabled", audio.enabled),
        INT_FIELD("audio.volume", audio.volume),
        INT_FIELD("audio.sdr_block_ms", audio.sdr_block_ms),
        ENUM_FIELD("cdvd.delivery_visibility", cdvd.delivery_visibility, kVisibility),
        ENUM_FIELD("cdvd.latency_model", cdvd.latency_model, kLatency),
        DOUBLE_FIELD("cdvd.latency_scale", cdvd.latency_scale),
        BOOL_FIELD("cdvd.load_turbo", cdvd.load_turbo),
        INT_FIELD("cdvd.mc_latency_polls", cdvd.mc_latency_polls),
        DOUBLE_FIELD("cdvd.poll_latency_scale", cdvd.poll_latency_scale),
        ENUM_FIELD("dev.log_level", dev.log_level, kLevel),
        BOOL_FIELD("diag.perf", diag.perf),
        BOOL_FIELD("diag.tick_census", diag.tick_census),
        INT_FIELD("engine.ee_runahead", engine.ee_runahead),
        BOOL_FIELD("engine.mtvu", engine.mtvu),
        INT_FIELD("engine.mtvu_depth", engine.mtvu_depth),
        BOOL_FIELD("engine.mtvu_kick_drop", engine.mtvu_kick_drop),
        ENUM_FIELD("engine.pacing", engine.pacing, kPacing),
        INT_FIELD("engine.shutdown_wait_s", engine.shutdown_wait_s),
        INT_FIELD("engine.time_regime_pin", engine.time_regime_pin),
        INT_FIELD("engine.turbo", engine.turbo),
        BOOL_FIELD("engine.vu1_aot_codegen", engine.vu1_aot_codegen),
        STR_FIELD("engine.vu1_aot_compiler", engine.vu1_aot_compiler),
        STR_FIELD("engine.vu1_aot_corpus", engine.vu1_aot_corpus),
        ENUM_FIELD("engine.vu1_aot_fallback", engine.vu1_aot_fallback, kVu1AotFb),
        INT_FIELD("engine.vu1_aot_opt", engine.vu1_aot_opt),
        STR_FIELD("engine.vu1_aot_prelude", engine.vu1_aot_prelude),
        BOOL_FIELD("engine.vu1_aot_verify", engine.vu1_aot_verify),
        ENUM_FIELD("engine.vu1_mode", engine.vu1_mode, kVu1),
        BOOL_FIELD("game.rtc_fixed", game.rtc_fixed),
        ENUM_FIELD("gs.queue", gs.queue, kGsQueue),
        INT_FIELD("harness.frames", harness.frames),
        INT_FIELD("harness.close_at_frame", harness.close_at_frame),
        INT_FIELD("harness.close_after_ms", harness.close_after_ms),
        STR_FIELD("harness.virtual_pad", harness.virtual_pad),
        INT_FIELD("harness.display_hz", harness.display_hz),
        STR_FIELD("harness.display_sequence", harness.display_sequence),
        BOOL_FIELD("harness.headless", harness.headless),
        INT_FIELD("harness.inject_fault_frame", harness.inject_fault_frame),
        STR_FIELD("harness.inject_key", harness.inject_key),
        STR_FIELD("harness.inputs", harness.inputs),
        BOOL_FIELD("harness.legacy_input_save", harness.legacy_input_save),
        BOOL_FIELD("harness.live_pad_probe", harness.live_pad_probe),
        BOOL_FIELD("harness.live_restore", harness.live_restore),
        STR_FIELD("harness.load_state", harness.load_state),
        STR_FIELD("harness.out_dir", harness.out_dir),
        BOOL_FIELD("harness.record_input", harness.record_input),
        STR_FIELD("harness.record_input_path", harness.record_input_path),
        INT_FIELD("harness.restore_at_frame", harness.restore_at_frame),
        INT_FIELD("harness.save_at_frame", harness.save_at_frame),
        INT_FIELD("harness.state_every", harness.state_every),
        INT_FIELD("harness.warp_to_frame", harness.warp_to_frame),
        BOOL_FIELD("hle.meta_arena", hle.meta_arena),
        BOOL_FIELD("hle.libmc_format_in_t0", hle.libmc_format_in_t0),
        BOOL_FIELD("hle.math_helpers", hle.math_helpers),
        BOOL_FIELD("hle.culling", hle.culling),
        BOOL_FIELD("hle.movie_lifecycle", hle.movie_lifecycle),
        BOOL_FIELD("hle.movie_step", hle.movie_step),
        BOOL_FIELD("hle.movie_start", hle.movie_start),
        BOOL_FIELD("hle.movie_wait", hle.movie_wait),
        BOOL_FIELD("hle.movie_stop", hle.movie_stop),
        BOOL_FIELD("hle.sound_bank_headers", hle.sound_bank_headers),
        BOOL_FIELD("hle.stream_files", hle.stream_files),
        BOOL_FIELD("hle.files", hle.files),
        BOOL_FIELD("hle.sound_samples", hle.sound_samples),
        BOOL_FIELD("hle.music", hle.music),
        BOOL_FIELD("hle.music_buffers", hle.music_buffers),
        BOOL_FIELD("hle.input_service", hle.input_service),
        BOOL_FIELD("hle.save_storage", hle.save_storage),
        BOOL_FIELD("hle.audio_mix", hle.audio_mix),
        BOOL_FIELD("hle.service_time", hle.service_time),
        ENUM_FIELD("hle.startup", hle.startup, kStartup),
        ENUM_FIELD("hle.graphics_strips", hle.graphics_strips, kGraphicsStrips),
        BOOL_FIELD("hle.graphics_shadows", hle.graphics_shadows),
        BOOL_FIELD("hle.graphics_distortion", hle.graphics_distortion),
        BOOL_FIELD("hle.graphics_screen", hle.graphics_screen),
        BOOL_FIELD("hle.graphics_sprites", hle.graphics_sprites),
        BOOL_FIELD("hle.graphics_text", hle.graphics_text),
        BOOL_FIELD("hle.graphics_sky", hle.graphics_sky),
        BOOL_FIELD("hle.graphics_textures", hle.graphics_textures),
        BOOL_FIELD("hle.graphics_meshes", hle.graphics_meshes),
        BOOL_FIELD("hle.graphics_movie", hle.graphics_movie),
        BOOL_FIELD("hle.graphics_frame", hle.graphics_frame),
        STR_FIELD("input.bindings_file", input.bindings_file),
        STR_FIELD("input.device", input.device),
        BOOL_FIELD("input.kbm_pad", input.kbm_pad),
        BOOL_FIELD("input.rumble", input.rumble),
        ENUM_FIELD("iop.dispatch", iop.dispatch, kIop),
        ENUM_FIELD("iop.restored_timer_due", iop.restored_timer_due, kTimerDue),
        BOOL_FIELD("loader.ring_cache", loader.ring_cache),
        STR_FIELD("log.channels", log.channels),
        INT_FIELD("log.ring_events", log.ring_events),
        STR_FIELD("meta.profile", meta.profile),
        STR_FIELD("meta.source_fingerprint", meta.source_fingerprint),
        STR_FIELD("paths.game_files", paths.game_files),
        STR_FIELD("paths.iso", paths.iso),
        STR_FIELD("paths.log_dir", paths.log_dir),
        STR_FIELD("paths.mc_dir", paths.mc_dir),
        STR_FIELD("paths.save_dir", paths.save_dir),
        STR_FIELD("paths.texture_packs", paths.texture_packs),
        STR_FIELD("paths.user_data", paths.user_data),
        ENUM_FIELD("save.restore_mode", save.restore_mode, kRestoreMode),
        INT_FIELD("save.slot", save.slot),
        BOOL_FIELD("time.maint_eepaced", time.maint_eepaced),
        BOOL_FIELD("time.t0_tickpaced", time.t0_tickpaced),
        BOOL_FIELD("time.tick_deterministic", time.tick_deterministic),
        BOOL_FIELD("time.tick_sync", time.tick_sync),
        INT_FIELD("triage.disk_budget_mb", triage.disk_budget_mb),
        BOOL_FIELD("triage.enabled", triage.enabled),
        BOOL_FIELD("triage.fallbacks", triage.fallbacks),
        INT_FIELD("triage.fps_target", triage.fps_target),
        BOOL_FIELD("triage.perf", triage.perf),
        INT_FIELD("triage.ring_period_s", triage.ring_period_s),
        BOOL_FIELD("triage.spikes", triage.spikes),
        BOOL_FIELD("fixes.look_rest_seconds", fixes.look_rest_seconds),
        BOOL_FIELD("fixes.skid_mark_init", fixes.skid_mark_init),
        STR_FIELD("launcher.elf_sha256", launcher.elf_sha256),
        BOOL_FIELD("launcher.show", launcher.show),
        INT_FIELD("qol.fast_forward", qol.fast_forward),
        BOOL_FIELD("qol.skip_movies", qol.skip_movies),
        BOOL_FIELD("ui.gui_open", ui.gui_open),
        BOOL_FIELD("ui.menu_pages", ui.menu_pages),
        BOOL_FIELD("ui.perf_overlay", ui.perf_overlay),
        INT_FIELD("video.anisotropy", video.anisotropy),
        ENUM_FIELD("video.aspect", video.aspect, kAspect),
        ENUM_FIELD("video.backend", video.backend, kBackend),
        INT_FIELD("video.custom_offset_x", video.custom_offset_x),
        INT_FIELD("video.custom_offset_y", video.custom_offset_y),
        INT_FIELD("video.custom_scale_pct", video.custom_scale_pct),
        ENUM_FIELD("video.fit_mode", video.fit_mode, kFit),
        INT_FIELD("video.fps_cap", video.fps_cap),
        ENUM_FIELD("video.fullscreen", video.fullscreen, kFullscreen),
        BOOL_FIELD("video.interp", video.interp),
        ENUM_FIELD("video.render_rate", video.render_rate, kRenderRate),
        INT_FIELD("video.scale", video.scale),
        ENUM_FIELD("video.texture_filter", video.texture_filter, kTextureFilter),
        STR_FIELD("video.texture_pack", video.texture_pack),
        BOOL_FIELD("video.vsync", video.vsync),
    };
    return f;
}

#undef BOOL_FIELD
#undef INT_FIELD
#undef DOUBLE_FIELD
#undef STR_FIELD
#undef ENUM_FIELD

const Field* FindField(const std::string& key) {
    for (const Field& f : Fields())
        if (key == f.key) return &f;
    return nullptr;
}

bool ExcludedFromFingerprint(const std::string& key) {
    // save.restore_mode is the F8 ROUTE (live in-process / relaunch): it never
    // changes what the title executes or what an image carries, so a slot saved
    // under one route restores under the other (the relaunch re-executes with
    // the setting on its argv; a fingerprinted route would refuse its own slot).
    // diag.* are observers (the perf counters, the tick census sampler): they
    // change what the leg PUBLISHES, never what the title executes or what an
    // image carries -- a slot saved without them restores under them (the boss
    // resume leg attributes its stall with diag.tick_census=on).
    // engine.mtvu_kick_drop is the host's scheduling of the worker (park vs
    // drop): nothing the title executes or an image carries changes with it.
    // engine.vu1_aot_corpus is a path to oracle data (where the AOT tier's
    // verifier reads its cases): the tiers are the identity on what executes.
    // engine.vu1_aot_codegen / _compiler / _prelude / _opt choose the host
    // toolchain of the generated code, which is verified the identity before
    // it runs: nothing the title executes changes with them.
    return key.compare(0, 8, "harness.") == 0 || key.compare(0, 6, "paths.") == 0 || key.compare(0, 5, "diag.") == 0 ||
           key == "meta.profile" || key == "meta.source_fingerprint" || key == "save.restore_mode" ||
           key == "engine.mtvu_kick_drop" || key == "engine.vu1_aot_corpus" || key == "engine.vu1_aot_codegen" ||
           key == "engine.vu1_aot_compiler" || key == "engine.vu1_aot_prelude" || key == "engine.vu1_aot_opt";
}

}  // namespace

const std::vector<std::string>& Config::Keys() {
    static const std::vector<std::string> keys = [] {
        std::vector<std::string> k;
        for (const Field& f : Fields()) k.push_back(f.key);
        std::sort(k.begin(), k.end());
        return k;
    }();
    return keys;
}

bool Config::IsKey(const std::string& key) { return FindField(key) != nullptr; }

bool Config::Get(const std::string& key, std::string* out) const {
    const Field* f = FindField(key);
    if (!f) return false;
    if (out) *out = f->get(*this);
    return true;
}

bool Config::Set(const std::string& key, const std::string& value, std::string* err) {
    const Field* f = FindField(key);
    if (!f) {
        if (err) *err = "unknown config key '" + key + "'";
        return false;
    }
    return f->set(*this, value, err);
}

std::string Config::Fingerprint() const {
    static const Config defaults;
    std::string out;
    for (const std::string& key : Keys()) {
        if (ExcludedFromFingerprint(key)) continue;
        std::string mine, dflt;
        Get(key, &mine);
        defaults.Get(key, &dflt);
        if (mine == dflt) continue;
        out += key;
        out += "=";
        out += mine;
        out += ";";
    }
    out += "profile=";
    out += meta.profile;
    return out;
}

uint64_t Config::FingerprintHash() const {
    // Each `key=value` entry is folded followed by ";" -- the same fold the
    // frozen harness used over its sorted `VAR=value` env list.
    const std::string fp = Fingerprint();
    uint64_t h = hash::kFnv1a64Offset;
    size_t start = 0;
    while (start < fp.size()) {
        size_t end = fp.find(';', start);
        if (end == std::string::npos) end = fp.size();
        h = hash::Fnv1a64(fp.data() + start, end - start, h);
        h = hash::Fnv1a64(";", 1, h);
        start = end + 1;
    }
    return h;
}

}  // namespace ps2r
