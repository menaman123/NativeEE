// tests/core/test_config.cpp -- Config schema peak defaults, string access,
// loader precedence (defaults < nativeee.ini | --config < --set < --profile),
// the legacy ps2r.ini / display.ini migration and the --display-ini compatibility
// read (the original test_display_settings cases), the
// validator DAG, the deterministic preset (PROFILE.md rows) and the
// fingerprint golden. argv[1] = scratch dir; argv[2] = the generated
// docs/filemap/settings_table.tsv whose header carries the golden.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "engine/core/config/config.h"
#include "engine/core/config/config_loader.h"
#include "engine/core/config/config_validator.h"
#include "engine/core/config/deterministic_profile.h"
#include "engine/core/hash/hash.h"

static int g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            g_fail++;                                                        \
        }                                                                    \
    } while (0)

using namespace ps2r;

static std::string g_dir = "/tmp";
static std::string g_settings_table;   // argv[2]: docs/filemap/settings_table.tsv
static std::string P(const char* n) { return g_dir + "/" + n; }
static void WriteFile(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::trunc);
    f << text;
}
// `# deterministic_fingerprint=<value>` from the generated settings table.
static std::string ReadGoldenFingerprint(const std::string& tsv_path) {
    std::ifstream f(tsv_path);
    std::string line;
    const std::string tag = "# deterministic_fingerprint=";
    while (std::getline(f, line))
        if (line.compare(0, tag.size(), tag) == 0) return line.substr(tag.size());
    return std::string();
}
static std::string Get(const Config& c, const char* key) {
    std::string v;
    c.Get(key, &v);
    return v;
}
static ConfigLoader::Result Load(Config& out, std::vector<std::string> args) {
    std::vector<char*> argv;
    static std::vector<std::string> keep;
    keep = args;
    keep.insert(keep.begin(), "nativeee");
    for (auto& s : keep) argv.push_back(&s[0]);
    ConfigLoader l;
    l.SetSearchDir(g_dir);
    return l.Load((int)argv.size(), argv.data(), out);
}

static void TestPeakDefaults() {
    Config c;
    // FLAG_LEDGER SETTINGS table / PHASE1_PLAN section 3 peak column.
    CHECK(c.video.scale == 1);   // track 1d: scale 1 is the play default (2..4 an option)
    CHECK(c.video.interp == true);
    CHECK(c.video.fps_cap == 60);
    CHECK(c.video.vsync == false);
    CHECK(c.video.fullscreen == FullscreenMode::Windowed);   // A2: the window's mode, persisted
    CHECK(c.video.render_rate == RenderRate::Tick60);        // A3: one presented frame per game tick
    CHECK(c.harness.display_hz == 0);                        // A3: the real display's refresh
    CHECK(c.video.backend == VideoBackend::Auto);   // round 3: Metal on an Apple-silicon host, else GL (resolved at Create)
    CHECK(c.video.fit_mode == FitMode::Fit);
    CHECK(c.video.aspect == AspectSel::Auto);
    {
        Config e;
        std::string err;
        CHECK(e.Set("video.fullscreen", "borderless", &err) && e.video.fullscreen == FullscreenMode::Borderless);
        CHECK(e.Set("video.fullscreen", "FULLSCREEN", &err) && e.video.fullscreen == FullscreenMode::Fullscreen);
        CHECK(!e.Set("video.fullscreen", "window", &err));
        CHECK(e.Set("video.render_rate", "display", &err) && e.video.render_rate == RenderRate::Display);
        CHECK(e.Set("video.render_rate", "60", &err) && e.video.render_rate == RenderRate::Tick60);
        CHECK(!e.Set("video.render_rate", "120", &err));
        CHECK(e.video.frame_cap == FrameCap::Hz60);   // the presented-frame cadence: every field by default
        CHECK(e.Set("video.frame_cap", "30", &err) && e.video.frame_cap == FrameCap::Hz30);
        CHECK(e.Set("video.frame_cap", "off", &err) && e.video.frame_cap == FrameCap::Off);
        CHECK(e.Set("video.frame_cap", "60", &err) && e.video.frame_cap == FrameCap::Hz60);
        CHECK(!e.Set("video.frame_cap", "45", &err));
        std::string v;
        CHECK(e.Get("video.fullscreen", &v) && v == "fullscreen");
    }
    CHECK(c.video.custom_scale_pct == 100 && c.video.custom_offset_x == 0 && c.video.custom_offset_y == 0);
    CHECK(c.audio.enabled == true);
    CHECK(c.cdvd.latency_model == CdvdLatencyModel::Instant);
    CHECK(c.cdvd.latency_scale == 1.0);
    CHECK(c.cdvd.mc_latency_polls == 4);
    CHECK(c.cdvd.load_turbo == false);
    CHECK(c.engine.vu1_mode == Vu1Mode::Cached);
    CHECK(c.engine.mtvu == true);
    CHECK(c.engine.mtvu_depth == 2);
    CHECK(c.engine.mtvu_depth_hold == 0);        // round 5: the hold off (it costs pictures when the chain is consumer-bound)
    CHECK(c.video.capture_wait_ms == 8);         // round 5: the prompt present capture on (the latency track's gates, 2026-10-23)
    CHECK(c.video.present_relax == true);        // round 5: the presenter's relax on (same gates)
    CHECK(c.engine.mtvu_early_finish == true);   // E425/E426/E427 on by default (the finish track's gates)
    CHECK(c.engine.mtvu_wait_probe == true);     // E430 on by default (the waitsites track's gates, 2026-10-22)
    CHECK(c.engine.ee_yield_spin_gate == true);  // E432 on by default (the waitsites track's gates, 2026-10-22)
    CHECK(c.engine.ee_fast_dispatch == true);    // eecode round 4: the raw dispatch fast path (gates green 2026-10-22)
    CHECK(c.engine.cop0_count == true);          // round 5: the COP0 Count register advances (finalize5 gates green 2026-10-23)
    CHECK(c.engine.ee_runahead == 2);
    CHECK(c.engine.pacing == Pacing::Realtime);
    CHECK(c.engine.time_regime_pin == -1);
    CHECK(c.engine.turbo == 0);
    CHECK(c.engine.shutdown_wait_s == 10);
    CHECK(!c.time.tick_sync && !c.time.tick_deterministic && !c.time.t0_tickpaced && !c.time.maint_eepaced);
    CHECK(c.iop.dispatch == IopDispatch::Frame);   // the shipped default until BUG-026 closes
    CHECK(c.game.rtc_fixed == false);
    CHECK(c.save.slot == 0);
    CHECK(c.save.restore_mode == RestoreMode::Live);   // F8 restores in-process (track 3a)
    CHECK(c.ui.gui_open == true);
    CHECK(c.dev.log_level == LogLevelSetting::Quiet);
    CHECK(c.log.channels == "all");
    CHECK(c.log.ring_events == 65536);
    CHECK(c.diag.perf == false);
    CHECK(c.triage.enabled && c.triage.fallbacks && c.triage.perf && c.triage.spikes && c.triage.fps_target == 60);
    CHECK(c.hle.meta_arena);   // the META-ARENA hook (DP-31 toggle) defaults on until the score keeps off
    {
        Config h;
        std::string err;
        CHECK(h.Set("hle.meta_arena", "off", &err) && !h.hle.meta_arena && Get(h, "hle.meta_arena") == "off");
        // ALL-ON, NO EXCEPTIONS (CUTOVER.md, 2026-10-09 ~11:30): EVERY hle.* toggle
        // is the compiled default (27 keys, graphics_strips=all included); every
        // retired arm stays selectable with "off" and is stated EXPLICITLY below
        // wherever a dependency chain or a backend rule is pinned (nothing weakened).
        CHECK(c.hle.libmc_format_in_t0 && Get(c, "hle.libmc_format_in_t0") == "on");   // BUG-089 toggle: t0 (the hardware)
        CHECK(h.Set("hle.libmc_format_in_t0", "off", &err) && !h.hle.libmc_format_in_t0 && Get(h, "hle.libmc_format_in_t0") == "off");
        CHECK(h.Set("hle.libmc_format_in_t0", "on", &err) && h.hle.libmc_format_in_t0);
        // Cutover flip 5 (2026-10-08): the MathLibrary helpers are the compiled
        // default; the retired VU0-macro/generated-body arm is still selectable
        // explicitly (the control arm of every math self-oracle).
        CHECK(c.hle.math_helpers && Get(c, "hle.math_helpers") == "on");
        CHECK(h.Set("hle.math_helpers", "off", &err) && !h.hle.math_helpers && Get(h, "hle.math_helpers") == "off");
        CHECK(c.hle.sound_bank_headers);
        // Cutover flip 4 (2026-10-08): the owned Movie lifecycle is the compiled default;
        // the retired IPU/libmpeg lifecycle arm is still selectable explicitly.
        CHECK(c.hle.movie_lifecycle && Get(c, "hle.movie_lifecycle") == "on");
        CHECK(h.Set("hle.movie_lifecycle", "off", &err) && !h.hle.movie_lifecycle && Get(h, "hle.movie_lifecycle") == "off");
        CHECK(h.Set("hle.math_helpers", "on", &err) && h.hle.math_helpers);
        CHECK(c.hle.culling && Get(c, "hle.culling") == "on");   // all-on
        CHECK(h.Set("hle.culling", "off", &err) && !h.hle.culling && Get(h, "hle.culling") == "off");
        CHECK(h.Set("hle.culling", "on", &err) && h.hle.culling);
        CHECK(h.Set("hle.movie_lifecycle", "on", &err) && h.hle.movie_lifecycle);
        // The complete software player chain is the compiled default (all-on);
        // its dependency DAG is pinned from the retired arms stated explicitly.
        CHECK(c.hle.movie_step && c.hle.movie_start && c.hle.movie_wait && c.hle.movie_stop);
        Config waiting;
        waiting.hle.movie_start = false;   // the retired Start arm, stated
        CHECK(waiting.Set("hle.movie_wait", "on", &err));
        CHECK(!ConfigValidator::Validate(waiting, false, &err));
        Config start;
        start.hle.movie_wait = start.hle.movie_stop = start.hle.movie_step = false;   // the retired arms, stated
        start.hle.music_buffers = false;
        CHECK(start.Set("hle.movie_start", "on", &err));
        CHECK(start.Set("hle.movie_lifecycle", "on", &err));
        CHECK(start.Set("hle.graphics_movie", "on", &err));
        CHECK(!ConfigValidator::Validate(start, false, &err));
        CHECK(start.Set("hle.music", "on", &err));
        CHECK(start.Set("hle.music_buffers", "on", &err));
        CHECK(ConfigValidator::Validate(start, false, &err));
        CHECK(start.Set("hle.movie_wait", "on", &err) && ConfigValidator::Validate(start, false, &err));
        CHECK(start.Set("hle.movie_stop", "on", &err) && !ConfigValidator::Validate(start, false, &err));
        CHECK(start.Set("hle.movie_step", "on", &err) && ConfigValidator::Validate(start, false, &err));
        h.hle.movie_start = h.hle.movie_wait = h.hle.movie_stop = false;   // the retired arms, stated
        h.hle.graphics_movie = false;                                      // the generated picture arm, stated
        CHECK(h.Set("hle.movie_step", "on", &err) && h.hle.movie_step);
        CHECK(!ConfigValidator::Validate(h, false, &err)); // Step without owned pictures refused
        h.hle.movie_step = false;
        CHECK(h.Set("hle.sound_bank_headers", "off", &err) && !h.hle.sound_bank_headers);
        CHECK(c.hle.stream_files && Get(c, "hle.stream_files") == "on");   // all-on, no exceptions (was off: flip 6)
        CHECK(c.hle.graphics_strips == GraphicsStrips::All && Get(c, "hle.graphics_strips") == "all");   // was plain (flip 3)
        CHECK(h.Set("hle.stream_files", "off", &err) && !h.hle.stream_files && Get(h, "hle.stream_files") == "off");
        CHECK(h.Set("hle.stream_files", "on", &err) && h.hle.stream_files);
        CHECK(h.Set("hle.graphics_strips", "plain", &err) && h.hle.graphics_strips == GraphicsStrips::Plain);   // the old default, stated
        CHECK(c.hle.files && Get(c, "hle.files") == "on");   // all-on
        CHECK(h.Set("hle.files", "off", &err) && !h.hle.files && Get(h, "hle.files") == "off");
        CHECK(h.Set("hle.files", "on", &err) && h.hle.files);
        CHECK(c.hle.sound_samples && Get(c, "hle.sound_samples") == "on");   // all-on
        CHECK(h.Set("hle.sound_samples", "off", &err) && !h.hle.sound_samples && Get(h, "hle.sound_samples") == "off");
        CHECK(h.Set("hle.sound_samples", "on", &err) && h.hle.sound_samples);
        // Cutover flip 9b (2026-10-09, DT-13): the owned MusicLibrary service is
        // the compiled default; both values are stated explicitly (the retired
        // libsd/STREAM music arm stays selectable with "off").
        CHECK(c.hle.music && Get(c, "hle.music") == "on");
        CHECK(h.Set("hle.music", "off", &err) && !h.hle.music && Get(h, "hle.music") == "off");
        CHECK(h.Set("hle.music", "on", &err) && h.hle.music);
        CHECK(h.Set("hle.music", "off", &err) && !h.hle.music);
        CHECK(h.Set("hle.music", "on", &err) && h.hle.music); // music_buffers below requires it
        CHECK(c.hle.music_buffers && Get(c, "hle.music_buffers") == "on");   // all-on
        CHECK(h.Set("hle.music_buffers", "off", &err) && !h.hle.music_buffers && Get(h, "hle.music_buffers") == "off");
        CHECK(h.Set("hle.music_buffers", "on", &err) && h.hle.music_buffers);
        CHECK(c.hle.audio_mix && Get(c, "hle.audio_mix") == "on");   // all-on
        CHECK(h.Set("hle.audio_mix", "off", &err) && !h.hle.audio_mix && Get(h, "hle.audio_mix") == "off");
        CHECK(h.Set("hle.audio_mix", "on", &err) && h.hle.audio_mix);
        CHECK(c.hle.service_time && Get(c, "hle.service_time") == "on");   // all-on
        CHECK(h.Set("hle.service_time", "off", &err) && !h.hle.service_time && Get(h, "hle.service_time") == "off");
        CHECK(h.Set("hle.service_time", "on", &err) && h.hle.service_time);
        CHECK(c.hle.input_service && Get(c, "hle.input_service") == "on");   // all-on
        CHECK(h.Set("hle.input_service", "off", &err) && !h.hle.input_service && Get(h, "hle.input_service") == "off");
        CHECK(c.hle.save_storage && Get(c, "hle.save_storage") == "on");   // all-on
        CHECK(h.Set("hle.save_storage", "off", &err) && !h.hle.save_storage && Get(h, "hle.save_storage") == "off");
        CHECK(h.Set("hle.graphics_strips", "all", &err) && h.hle.graphics_strips == GraphicsStrips::All);
        CHECK(h.Set("hle.graphics_strips", "model", &err) && h.hle.graphics_strips == GraphicsStrips::Model);
        CHECK(!h.Set("hle.graphics_strips", "unknown", &err));
        // The Graphics producers: ALL seven are the compiled default (all-on, no
        // exceptions; M_CUTOVER.md "All-on build, no exceptions"); the old value
        // (off) is stated explicitly and stays selectable.
        for (const auto* key : {"hle.graphics_distortion", "hle.graphics_sky", "hle.graphics_movie",
                                "hle.graphics_sprites", "hle.graphics_frame", "hle.graphics_shadows", "hle.graphics_text",
                                "hle.graphics_screen", "hle.graphics_textures", "hle.graphics_meshes"}) {
            CHECK(Get(c, key) == "on");
            CHECK(h.Set(key, "off", &err) && Get(h, key) == "off");
            CHECK(h.Set(key, "on", &err) && Get(h, key) == "on");
        }
        h.hle.movie_lifecycle = false;
        CHECK(!ConfigValidator::Validate(h, false, &err));
        h.hle.movie_lifecycle = true;
        CHECK(ConfigValidator::Validate(h, false, &err));
        h.video.backend = VideoBackend::Soft;
        CHECK(!ConfigValidator::Validate(h, false, &err));
        // The Soft backend admits no Graphics producer: the compiled default (every
        // producer on) is REFUSED under Soft by the validator (the loader DEGRADES
        // them with a logged line first -- TestDegradeDependents); with every
        // producer stated off the Plain strip arm validates, and All does not.
        Config soft_default;
        soft_default.video.backend = VideoBackend::Soft;
        CHECK(!ConfigValidator::Validate(soft_default, false, &err));
        Config strips;
        strips.video.backend = VideoBackend::Soft;
        strips.hle.graphics_movie = false; // the generated Movie picture arm, stated
        strips.hle.movie_step = strips.hle.movie_start = strips.hle.movie_wait = strips.hle.movie_stop = false; // the player chain it carries, stated
        strips.hle.graphics_shadows = strips.hle.graphics_distortion = false;   // the retained arms, stated
        strips.hle.graphics_screen = false;
        strips.hle.graphics_text = strips.hle.graphics_sky = false;
        strips.hle.graphics_textures = false;                              // textures at load, stated off
        strips.hle.graphics_meshes = false;                                // the owned mesh bucket (M8 B), stated off
        strips.hle.graphics_sprites = strips.hle.graphics_frame = false;   // the generated arms, stated
        strips.hle.graphics_strips = GraphicsStrips::Plain;                // the old default, stated
        CHECK(ConfigValidator::Validate(strips, false, &err)); // accepted Plain behavior
        strips.hle.graphics_strips = GraphicsStrips::All;
        CHECK(!ConfigValidator::Validate(strips, false, &err));
        Config shadows = strips;
        shadows.hle.graphics_strips = GraphicsStrips::Plain;
        shadows.hle.graphics_shadows = true;
        CHECK(!ConfigValidator::Validate(shadows, false, &err)); // shadows alone need the GL backend too
        shadows.video.backend = VideoBackend::Gl;
        CHECK(ConfigValidator::Validate(shadows, false, &err));
        Config distortion = strips;
        distortion.hle.graphics_strips = GraphicsStrips::Plain;
        distortion.hle.graphics_distortion = true;
        CHECK(!ConfigValidator::Validate(distortion, false, &err)); // distortion alone needs the GL backend too
        distortion.video.backend = VideoBackend::Gl;
        CHECK(ConfigValidator::Validate(distortion, false, &err));
    }
    CHECK(c.triage.disk_budget_mb == 2048 && c.triage.ring_period_s == 10);   // TRIAGE_CAPTURE section 3
    CHECK(c.harness.out_dir.empty() && !c.harness.headless && c.harness.frames == 0);
    // C4 user data layout: the compiled defaults are EMPTY (resolved at Load under
    // the user data root, or to the former cwd-relative names with no root known).
    CHECK(c.paths.log_dir.empty() && c.paths.mc_dir.empty() && c.paths.game_files.empty() &&
          c.paths.texture_packs.empty() && c.paths.user_data.empty());
    {
        Config r;
        CHECK(ConfigLoader::ResolveUserPaths(r, "").empty());
        CHECK(r.paths.log_dir == "logs" && r.paths.mc_dir == "memcards" && r.paths.game_files == "game_files" &&
              r.paths.texture_packs == "textures" && r.paths.user_data.empty());
        Config u;
        u.paths.mc_dir = "/explicit/cards";
        CHECK(ConfigLoader::ResolveUserPaths(u, "/data/NativeEE") == "/data/NativeEE");
        CHECK(u.paths.mc_dir == "/explicit/cards" && u.paths.game_files == "/data/NativeEE/game_files" &&
              u.paths.log_dir == "/data/NativeEE/logs" && u.paths.texture_packs == "/data/NativeEE/textures" &&
              u.paths.user_data == "/data/NativeEE");
        Config o;
        o.paths.user_data = "/pinned";   // paths.user_data (any tier) overrides the platform root
        CHECK(ConfigLoader::ResolveUserPaths(o, "/data/NativeEE") == "/pinned" && o.paths.log_dir == "/pinned/logs");
    }
    CHECK(c.ui.perf_overlay == false);   // the Display page's first option (MenuExtension), off by default
    CHECK(c.ui.menu_pages == true);      // the extension itself (play mode only)
    CHECK(c.meta.profile == "default");

    // String access round-trips every key and the key set is sorted.
    const auto& keys = Config::Keys();
    CHECK(keys.size() >= 59);
    for (size_t i = 1; i < keys.size(); i++) CHECK(keys[i - 1] < keys[i]);
    for (const auto& k : keys) {
        std::string v, err;
        CHECK(c.Get(k, &v));
        Config d;
        CHECK(d.Set(k, v, &err));
        CHECK(Get(d, k.c_str()) == v);
    }
    CHECK(Get(c, "video.scale") == "1" && Get(c, "video.interp") == "on" && Get(c, "engine.vu1_mode") == "cached");
    std::string err;
    CHECK(!c.Set("video.nope", "1", &err) && err.find("unknown") != std::string::npos);
    CHECK(!c.Set("video.scale", "two", &err));
    CHECK(!c.Set("engine.pacing", "sideways", &err));
    CHECK(c.Set("engine.pacing", "FREERUN", &err) && c.engine.pacing == Pacing::Freerun);
    CHECK(c.Set("video.interp", "false", &err) && !c.video.interp);
}

static void TestFingerprint() {
    Config c;
    CHECK(c.Fingerprint() == "profile=default");
    c.video.scale = 2;                   // non-default (the default is 1)
    c.engine.mtvu = false;
    c.harness.out_dir = "/tmp/leg";      // plumbing: excluded
    c.paths.iso = "/x.iso";              // excluded
    c.diag.tick_census = true;           // an observer: excluded
    c.engine.mtvu_kick_drop = false;     // the worker's host scheduling: excluded
    c.engine.mtvu_depth_hold = 1;        // the hold cap (round 5): where the EE parks on the worker, excluded
    c.video.capture_wait_ms = 0;         // when the window's picture is read back (round 5; default 8): excluded
    c.video.present_relax = false;       // when the presenter shows a posted picture (round 5; default on): excluded
    c.engine.mtvu_early_finish = false;  // the FINISH raise / boundary scheduling (E425): excluded
    c.engine.mtvu_wait_probe = false;    // the observation point's probe (E430): excluded
    c.engine.ee_yield_spin_gate = false; // the yield point's spin gate (E432): excluded
    c.engine.ee_fast_dispatch = false;   // the host dispatch mechanics of a guest call: excluded
    c.engine.cop0_count = false;         // the COP0 Count clock (not an image property): excluded
    CHECK(c.Fingerprint() == "engine.mtvu=off;video.scale=2;profile=default");
    // Hash semantics: FNV over each entry followed by ";".
    const std::string fp = c.Fingerprint();
    uint64_t h = ps2r::hash::kFnv1a64Offset;
    h = ps2r::hash::Fnv1a64("engine.mtvu=off", 15, h); h = ps2r::hash::Fnv1a64(";", 1, h);
    h = ps2r::hash::Fnv1a64("video.scale=2", 13, h);   h = ps2r::hash::Fnv1a64(";", 1, h);
    h = ps2r::hash::Fnv1a64("profile=default", 15, h); h = ps2r::hash::Fnv1a64(";", 1, h);
    CHECK(c.FingerprintHash() == h);

    // The deterministic preset's canonical fingerprint: the golden is READ from
    // the generated settings table's header (the single source; the table's
    // own consistency is test_config_schema's job).
    Config d;
    DeterministicProfile::Apply(d);
    const std::string golden = ReadGoldenFingerprint(g_settings_table);
    CHECK(!golden.empty());
    CHECK(golden.find("profile=deterministic") != std::string::npos);
    if (d.Fingerprint() != golden) std::printf("fingerprint: %s\n", d.Fingerprint().c_str());
    CHECK(d.Fingerprint() == golden);
    std::printf("deterministic env_hash=0x%016llx\n", (unsigned long long)d.FingerprintHash());
}

static void TestDeterministicProfile() {
    Config d;
    DeterministicProfile::Apply(d);
    // PROFILE.md rows -> fields.
    CHECK(d.video.scale == 1);                                   // PS2_GS_SCALE=1
    CHECK(d.video.interp == false);                              // PS2_GS_INTERP=0
    CHECK(d.video.fps_cap == 0);                                 // PS2_FPS_CAP=0
    CHECK(d.video.vsync == false);                               // PS2_NOVSYNC=1
    CHECK(d.engine.pacing == Pacing::Freerun);                   // PS2_VBLANK_FREERUN=1
    CHECK(d.engine.mtvu == false);                               // PS2_MTVU=0
    CHECK(d.engine.mtvu_early_finish == false);                  // the parser-raised FINISH (E425 pinned off)
    CHECK(d.engine.mtvu_wait_probe == false);                    // the unconditional idle loop (E430 pinned off)
    CHECK(d.engine.ee_yield_spin_gate == false);             // the free-running yield budget (E432 pinned off)
    CHECK(d.engine.cop0_count == true);                      // Count advances as on the console; the oracle bases are frozen with it (gsc_h600_cop0)
    CHECK(d.engine.time_regime_pin == 1);                        // PS2_TIME_EMU_REGIME=1
    CHECK(d.cdvd.latency_model == CdvdLatencyModel::Instant);    // PS2_CDVD_INSTANT=1
    CHECK(d.cdvd.latency_scale == 0.0);                          // PS2_DVD_LATENCY_SCALE=0
    CHECK(d.cdvd.mc_latency_polls == 0);                         // PS2_MC_LATENCY=0
    CHECK(d.ui.gui_open == false);                               // PS2_GUI_OPEN=0
    CHECK(d.iop.dispatch == IopDispatch::Frame);                 // PS2_IOP_DISPATCH=frame
    CHECK(d.game.rtc_fixed == true);                             // PS2_RTC_FIXED=1
    CHECK(d.time.tick_deterministic == true);                    // PS2_TICK_DETERMINISTIC=1
    CHECK(d.time.maint_eepaced == true);                         // PS2_MAINT_EEPACED=1
    CHECK(d.time.t0_tickpaced == true);                          // PS2_T0_TICKPACED=1
    CHECK(d.time.tick_sync == true);                             // PS2_TICK_SYNC=1
    CHECK(d.dev.log_level == LogLevelSetting::Quiet);            // PS2_LOG=0
    CHECK(d.audio.enabled == true);                              // device suppressed by tick_sync
    CHECK(d.meta.profile == "deterministic");
    std::string err;
    CHECK(ConfigValidator::Validate(d, false, &err));
    Config x;
    CHECK(DeterministicProfile::ApplyNamed("default", x) && x.meta.profile == "default");
    CHECK(!DeterministicProfile::ApplyNamed("bogus", x));
}

static void TestValidator() {
    Config c;
    std::string err;
    CHECK(ConfigValidator::Validate(c, false, &err));
    // The dependency chains are pinned from the retired arms stated explicitly
    // (default-independent; the all-on build of 2026-10-09 made every owned arm
    // the compiled default): the lifecycle, the Movie picture arm and the whole
    // software player chain, the music/mixer/service-time composition.
    c.hle.graphics_movie = false;
    c.hle.movie_lifecycle = false;
    c.hle.movie_step = c.hle.movie_start = c.hle.movie_wait = c.hle.movie_stop = false;
    c.hle.music = false;
    c.hle.music_buffers = false;
    c.hle.audio_mix = false;
    c.hle.service_time = false;
    c.hle.sound_samples = false;
    CHECK(ConfigValidator::Validate(c, false, &err));
    c.hle.music_buffers = true;
    CHECK(!ConfigValidator::Validate(c, false, &err));
    c.hle.music = true;
    CHECK(!ConfigValidator::Validate(c, false, &err));
    c.hle.movie_lifecycle = true;
    CHECK(ConfigValidator::Validate(c, false, &err));
    c.hle.music_buffers = c.hle.music = c.hle.movie_lifecycle = false;
    c.hle.audio_mix = true;
    CHECK(!ConfigValidator::Validate(c, false, &err));
    c.hle.music = true;
    CHECK(!ConfigValidator::Validate(c, false, &err));
    c.hle.movie_lifecycle = true;
    CHECK(ConfigValidator::Validate(c, false, &err));
    c.iop.dispatch = IopDispatch::Realtime;
    CHECK(!ConfigValidator::Validate(c, false, &err));
    c.iop.dispatch = IopDispatch::Frame;
    c.hle.audio_mix = c.hle.music = c.hle.movie_lifecycle = false;
    // The service_time dependency is pinned from the retired file-job, sample
    // and music arms stated explicitly (default-independent; all three are on
    // by default under all-on, no exceptions).
    c.hle.stream_files = false;
    c.hle.sound_samples = false;
    c.hle.music = false;
    c.hle.service_time = true;
    CHECK(!ConfigValidator::Validate(c, false, &err));
    c.hle.stream_files = true;
    CHECK(ConfigValidator::Validate(c, false, &err));
    c.iop.dispatch = IopDispatch::Realtime;
    CHECK(!ConfigValidator::Validate(c, false, &err));
    c.iop.dispatch = IopDispatch::Frame;
    c.hle.stream_files = c.hle.service_time = false;
    c.time.tick_sync = true;                 // peak has mtvu on -> refused
    CHECK(!ConfigValidator::Validate(c, false, &err) && err.find("tick_sync") != std::string::npos);
    c.engine.mtvu = false;
    CHECK(ConfigValidator::Validate(c, false, &err));
    c = Config();
    c.engine.mtvu_depth = 0;
    CHECK(!ConfigValidator::Validate(c, false, &err));
    c = Config();
    c.video.scale = 5;
    CHECK(!ConfigValidator::Validate(c, false, &err));
    c = Config();
    c.video.scale = 0;   // A2: native window (the factor follows the window)
    CHECK(ConfigValidator::Validate(c, false, &err));
    c = Config();
    c.video.scale = -1;
    CHECK(!ConfigValidator::Validate(c, false, &err));
    c = Config();
    c.log.ring_events = 3000;
    CHECK(!ConfigValidator::Validate(c, false, &err));
    c = Config();
    c.triage.fps_target = 0;      // auto
    c.triage.ring_period_s = 0;   // ring off
    CHECK(ConfigValidator::Validate(c, false, &err));
    c.triage.ring_period_s = -1;
    CHECK(!ConfigValidator::Validate(c, false, &err));
    c = Config();
    c.harness.out_dir = "/tmp/x";
    CHECK(!ConfigValidator::Validate(c, false, &err));          // needs --headless
    CHECK(ConfigValidator::Validate(c, true, &err));            // or --harness-window
    c.harness.headless = true;
    CHECK(ConfigValidator::Validate(c, false, &err));
    // freerun defaults instant unless explicit.
    c = Config();
    c.engine.pacing = Pacing::Freerun;
    c.cdvd.latency_model = CdvdLatencyModel::Modelled;
    ConfigValidator::ApplyDependentDefaults(c, false);
    CHECK(c.cdvd.latency_model == CdvdLatencyModel::Instant);
    c.cdvd.latency_model = CdvdLatencyModel::Modelled;
    ConfigValidator::ApplyDependentDefaults(c, true);
    CHECK(c.cdvd.latency_model == CdvdLatencyModel::Modelled);
}

// ALL-ON, NO EXCEPTIONS: one unit per dependency rule -- the prerequisite off,
// the dependent degraded, exactly one note naming both -- from the compiled
// defaults (every key on); every other key keeps its value.
static void TestDegradeDependents() {
    auto count_on = [](const Config& c) {
        int n = 0;
        for (const auto& key : Config::Keys()) {
            if (key.rfind("hle.", 0) != 0) continue;
            std::string v;
            c.Get(key, &v);
            if (v == "on" || v == "all" || v == "native") n++;   // hle.startup=native is an on-arm
        }
        return n;
    };
    {
        Config c;
        CHECK(count_on(c) == 31);
        CHECK(ConfigValidator::DegradeDependents(c).empty() && count_on(c) == 31);
    }
    struct Rule { const char* prereq; const char* dependent; const char* note; int degraded; };
    const Rule rules[] = {
        {"hle.movie_lifecycle", "hle.graphics_movie", "hle.graphics_movie disabled: requires hle.movie_lifecycle", 8},
        {"hle.graphics_movie", "hle.movie_step", "hle.movie_step disabled: requires hle.graphics_movie", 5},
        {"hle.graphics_movie", "hle.movie_start", "hle.movie_start disabled: requires hle.graphics_movie", 5},
        {"hle.music_buffers", "hle.movie_start", "hle.movie_start disabled: requires hle.music_buffers", 4},
        {"hle.movie_start", "hle.movie_wait", "hle.movie_wait disabled: requires hle.movie_start", 3},
        {"hle.movie_wait", "hle.movie_stop", "hle.movie_stop disabled: requires hle.movie_wait", 1},
        {"hle.movie_step", "hle.movie_stop", "hle.movie_stop disabled: requires hle.movie_step", 1},
        {"hle.music", "hle.music_buffers", "hle.music_buffers disabled: requires hle.music", 6},
        {"hle.music", "hle.audio_mix", "hle.audio_mix disabled: requires hle.music", 6},
        {"hle.movie_lifecycle", "hle.audio_mix", "hle.audio_mix disabled: requires hle.movie_lifecycle", 8},
        {"hle.movie_lifecycle", "hle.music_buffers", "hle.music_buffers disabled: requires hle.movie_lifecycle", 8},
        // M8 slice B: the owned mesh bucket reads every world texture as a converted asset.
        {"hle.graphics_textures", "hle.graphics_meshes", "hle.graphics_meshes disabled: requires hle.graphics_textures", 1},
    };
    for (const Rule& rule : rules) {
        Config c;
        std::string err;
        CHECK(c.Set(rule.prereq, "off", &err));
        const auto notes = ConfigValidator::DegradeDependents(c);
        std::string v;
        CHECK(c.Get(rule.dependent, &v) && v == "off");
        bool found = false;
        for (const auto& n : notes) found = found || n == rule.note;
        CHECK(found);
        CHECK(static_cast<int>(notes.size()) == rule.degraded);
        CHECK(count_on(c) == 31 - 1 - rule.degraded);
        CHECK(ConfigValidator::Validate(c, false, &err));   // the degraded composition validates
    }
    {
        // service_time: carried by any of the three file owners; off only when all three are.
        // (hle.startup=native degrades once with the first file owner off: the IOP
        // boot needs every service owner -- one note, then the retained arm stays.)
        Config c;
        c.hle.stream_files = false;
        auto startup_notes = ConfigValidator::DegradeDependents(c);
        CHECK(startup_notes.size() == 1 && c.hle.startup == StartupMode::Retained && c.hle.service_time);
        CHECK(startup_notes[0] == "hle.startup=native degraded (retained): requires hle.stream_files");
        c.hle.sound_samples = false;
        CHECK(ConfigValidator::DegradeDependents(c).empty() && c.hle.service_time);
        c.hle.music = false;
        const auto notes = ConfigValidator::DegradeDependents(c);
        CHECK(!c.hle.service_time);
        bool found = false;
        for (const auto& n : notes) found = found || n == "hle.service_time disabled: requires hle.stream_files, hle.sound_samples or hle.music";
        CHECK(found);
        std::string err;
        CHECK(ConfigValidator::Validate(c, false, &err));
    }
    {
        // iop.dispatch=realtime: audio_mix and service_time degrade.
        Config c;
        c.iop.dispatch = IopDispatch::Realtime;
        const auto notes = ConfigValidator::DegradeDependents(c);
        CHECK(!c.hle.audio_mix && !c.hle.service_time && c.hle.startup == StartupMode::Retained && notes.size() == 3);
        CHECK(notes[0] == "hle.audio_mix disabled: requires iop.dispatch=frame");
        CHECK(notes[1] == "hle.service_time disabled: requires iop.dispatch=frame");
        CHECK(notes[2] == "hle.startup=native degraded (retained): requires hle.audio_mix");
        std::string err;
        CHECK(ConfigValidator::Validate(c, false, &err));
    }
    {
        // video.backend=soft: every Graphics producer degrades (strips All -> Plain), then the player chain.
        Config c;
        c.video.backend = VideoBackend::Soft;
        const auto notes = ConfigValidator::DegradeDependents(c);
        CHECK(!c.hle.graphics_shadows && !c.hle.graphics_distortion && !c.hle.graphics_screen && !c.hle.graphics_sprites &&
              !c.hle.graphics_text && !c.hle.graphics_sky && !c.hle.graphics_frame && !c.hle.graphics_movie &&
              !c.hle.graphics_textures && !c.hle.graphics_meshes && c.hle.graphics_strips == GraphicsStrips::Plain);
        CHECK(!c.hle.movie_step && !c.hle.movie_start && !c.hle.movie_wait && !c.hle.movie_stop);
        CHECK(notes.size() == 16);   // + hle.graphics_screen (gl) + hle.graphics_textures (gl) + hle.graphics_meshes (gl) + hle.startup (movie_start degraded)
        CHECK(notes[8] == "hle.graphics_strips=all disabled (plain): requires video.backend=gl");
        CHECK(notes[9] == "hle.graphics_textures disabled: requires video.backend=gl");
        CHECK(notes[10] == "hle.graphics_meshes disabled: requires video.backend=gl");
        CHECK(c.hle.startup == StartupMode::Retained);
        CHECK(count_on(c) == 31 - 16);
        std::string err;
        CHECK(ConfigValidator::Validate(c, false, &err));
    }
}

// M12 service-only binary (no retained arm compiled in): a native startup
// whose consumer went off REFUSES with the key named -- never a silent
// degrade to the IOP-booting arm (hle.startup stays native, no note); the
// same config degrades to retained with the existing note when the arm is
// available. One unit per native-startup consumer key, in the validator's
// prerequisite order.
static void TestServiceOnlyStartupRefusals() {
    // `unmet` = the first unmet consumer in the validator's order (a prerequisite
    // off cascades through its dependents before the chain reaches startup: the
    // loader names the turned-off key in its "[degraded first: ...]" tail).
    struct Edge { const char* key; const char* value; const char* unmet; };
    const Edge edges[] = {
        {"hle.stream_files", "off", "hle.stream_files"},
        {"hle.files", "off", "hle.files"},
        {"hle.sound_bank_headers", "off", "hle.sound_bank_headers"},
        {"hle.sound_samples", "off", "hle.sound_samples"},
        {"hle.music", "off", "hle.music"},
        {"hle.music_buffers", "off", "hle.music_buffers"},
        {"hle.audio_mix", "off", "hle.audio_mix"},
        {"hle.movie_lifecycle", "off", "hle.music_buffers"},   // music_buffers degrades first
        {"hle.movie_start", "off", "hle.movie_start"},
        {"hle.input_service", "off", "hle.input_service"},
        {"hle.save_storage", "off", "hle.save_storage"},
        {"iop.dispatch", "realtime", "hle.audio_mix"},          // audio_mix degrades first
    };
    for (const Edge& edge : edges) {
        std::string err;
        // The service-only binary: refusal, startup untouched.
        Config c;
        CHECK(c.Set(edge.key, edge.value, &err));
        std::string refusal;
        const auto notes = ConfigValidator::DegradeDependents(c, /*retained_startup_available=*/false, &refusal);
        CHECK(c.hle.startup == StartupMode::Native);
        CHECK(!refusal.empty());
        CHECK(refusal.rfind("hle.startup=native requires ", 0) == 0);
        CHECK(refusal.find(std::string(edge.unmet) + "=on") != std::string::npos);
        CHECK(refusal.find("service-only") != std::string::npos);
        CHECK(refusal.find("nativeee_oracle") != std::string::npos);
        for (const auto& n : notes) CHECK(n.rfind("hle.startup", 0) != 0);   // no startup note on the refusing arm
        // The oracle binary: the same config degrades to retained with the note.
        Config o;
        CHECK(o.Set(edge.key, edge.value, &err));
        std::string none;
        const auto onotes = ConfigValidator::DegradeDependents(o, /*retained_startup_available=*/true, &none);
        CHECK(none.empty());
        CHECK(o.hle.startup == StartupMode::Retained);
        bool found = false;
        for (const auto& n : onotes)
            found = found || n == std::string("hle.startup=native degraded (retained): requires ") + edge.unmet;
        CHECK(found);
        CHECK(ConfigValidator::Validate(o, false, &err));
    }
    {
        // Every consumer on: neither arm refuses or degrades.
        Config c;
        std::string refusal;
        CHECK(ConfigValidator::DegradeDependents(c, false, &refusal).empty());
        CHECK(refusal.empty() && c.hle.startup == StartupMode::Native);
    }
    // ConfigLoader on the service-only binary: an explicit hle.startup=retained
    // refuses by name; the deterministic profile's retained pin becomes native
    // with a [config] note; a consumer off refuses with the cascade named.
    auto LoadServiceOnly = [](Config& out, std::vector<std::string> args) {
        std::vector<char*> argv;
        static std::vector<std::string> keep;
        keep = args;
        keep.insert(keep.begin(), "nativeee");
        for (auto& s : keep) argv.push_back(&s[0]);
        ConfigLoader l;
        l.SetSearchDir(g_dir);
        l.SetRetainedStartupAvailable(false);
        return l.Load((int)argv.size(), argv.data(), out);
    };
    std::remove(P("display.ini").c_str());
    std::remove(P("nativeee.ini").c_str());
    {
        Config c;
        ConfigLoader::Result r = LoadServiceOnly(c, {"--set", "hle.startup=retained"});
        CHECK(!r.ok);
        CHECK(r.error.find("hle.startup=retained") != std::string::npos);
        CHECK(r.error.find("nativeee_oracle") != std::string::npos);
    }
    {
        Config c;
        ConfigLoader::Result r = LoadServiceOnly(c, {"--profile", "deterministic"});
        CHECK(r.ok);
        CHECK(c.hle.startup == StartupMode::Native);
        bool found = false;
        for (const auto& n : r.notes) found = found || n.rfind("hle.startup=native (the profile's retained pin", 0) == 0;
        CHECK(found);
        // The same profile on the oracle arm keeps its retained pin, no note.
        Config o;
        ConfigLoader::Result ro = Load(o, {"--profile", "deterministic"});
        CHECK(ro.ok && o.hle.startup == StartupMode::Retained);
        for (const auto& n : ro.notes) CHECK(n.rfind("hle.startup", 0) != 0);
    }
    {
        Config c;
        ConfigLoader::Result r = LoadServiceOnly(c, {"--set", "hle.music=off"});
        CHECK(!r.ok);
        CHECK(r.error.find("hle.startup=native requires hle.music=on") != std::string::npos);
        CHECK(r.error.find("nativeee_oracle") != std::string::npos);
        // The cascade: a prerequisite off reaches startup through its dependents.
        Config d;
        r = LoadServiceOnly(d, {"--set", "iop.dispatch=realtime"});
        CHECK(!r.ok);
        CHECK(r.error.find("hle.startup=native requires hle.audio_mix=on") != std::string::npos);
        CHECK(r.error.find("[degraded first:") != std::string::npos);
        CHECK(r.error.find("hle.audio_mix disabled: requires iop.dispatch=frame") != std::string::npos);
        Config e;
        r = LoadServiceOnly(e, {"--set", "hle.movie_lifecycle=off"});
        CHECK(!r.ok);
        CHECK(r.error.find("hle.startup=native requires hle.music_buffers=on") != std::string::npos);
        CHECK(r.error.find("hle.music_buffers disabled: requires hle.movie_lifecycle") != std::string::npos);
        // (The twelfth consumer, iop.dispatch=frame, is never the FIRST unmet one:
        // a non-frame dispatch degrades hle.audio_mix in the same pass, which the
        // validator names first -- the refusal above plus its cascade tail.)
    }
}

static void TestLoaderPrecedence() {
    std::remove(P("display.ini").c_str());
    WriteFile(P("nativeee.ini"), "# comment\n[video]\nscale = 3\nfps_cap = 30 ; trailing\n[engine]\nmtvu = off\n"
                             "engine.turbo = 1\n");
    WriteFile(P("extra.ini"), "video.fps_cap = 45\ncdvd.latency_model = modelled\n");
    Config c;
    ConfigLoader::Result r = Load(c, {});
    CHECK(r.ok);
    CHECK(c.video.scale == 3 && c.video.fps_cap == 30 && !c.engine.mtvu && c.engine.turbo == 1);

    // --config names THE store: the folder's nativeee.ini is not read beside it.
    r = Load(c, {"--config", P("extra.ini")});
    CHECK(r.ok && c.video.fps_cap == 45 && c.video.scale == 1);
    CHECK(c.cdvd.latency_model == CdvdLatencyModel::Modelled);
    CHECK(r.settings_file == P("extra.ini") && r.settings_source == "--config");
    r = Load(c, {});
    CHECK(r.ok && r.settings_file == P("nativeee.ini") && r.settings_source == "user data" && !r.settings_migrated);

    r = Load(c, {"--config", P("extra.ini"), "--set", "video.fps_cap=50", "--set", "video.scale=4;engine.turbo=2"});
    CHECK(r.ok && c.video.fps_cap == 50 && c.video.scale == 4 && c.engine.turbo == 2);

    r = Load(c, {"--set", "video.scale=4", "--profile", "deterministic"});
    CHECK(r.ok && c.video.scale == 1 && c.meta.profile == "deterministic" && c.engine.mtvu == false);

    // Harness plumbing + headless; a file cannot override the CLI plumbing.
    r = Load(c, {"--profile", "deterministic", "--headless", "--harness-out", P("leg"), "--frames", "600",
                 "--load-state", "s.ps2st", "--inputs", "in.padlog", "--state-every", "60", "--iso", "g.iso"});
    CHECK(r.ok && c.harness.headless && c.harness.out_dir == P("leg") && c.harness.frames == 600);
    CHECK(c.harness.load_state == "s.ps2st" && c.harness.inputs == "in.padlog" && c.harness.state_every == 60);
    CHECK(c.paths.iso == "g.iso");
    CHECK(c.Fingerprint().find("harness.") == std::string::npos);
    r = Load(c, {"--game-files", "/title/game_files"});
    CHECK(r.ok && c.paths.game_files == "/title/game_files");

    // --help names every flag the parser accepts (PLAY_SESSIONS.md: "--help shows
    // the flags" is how the published recorder command is verified; --game-files
    // was parsed but undocumented until the 2026-10-08 cutover review).
    {
        const std::string usage = ConfigLoader::Usage();
        for (const char* flag : {"--config", "--set", "--profile", "--headless", "--harness-out", "--frames",
                                 "--load-state", "--inputs", "--record-input", "--state-every", "--warp-to",
                                 "--iso", "--game-files", "--display-ini", "--save-display", "--harness-window",
                                 "--vu1-library", "--watch-word", "--watch-flush-from", "--watch-record-from",
                                 "--vu0-trace", "--dump-frames", "--bench-report", "--log-level", "--version",
                                 "--help"}) {
            const bool named = usage.find(std::string("  ") + flag + " ") != std::string::npos;
            if (!named) std::printf("  usage text does not name %s\n", flag);
            CHECK(named);
        }
    }

    // Errors.
    r = Load(c, {"--headless", "--record-input"});
    CHECK(!r.ok && r.error.find("--harness-out") != std::string::npos);
    r = Load(c, {"--harness-window", "--harness-out", P("recorded"), "--record-input"});
    CHECK(r.ok && c.harness.record_input && c.harness.out_dir == P("recorded") && c.harness.record_input_path.empty());
    // `--record-input <file>` names the first recording segment; a following
    // flag is never mistaken for that destination.
    r = Load(c, {"--harness-window", "--harness-out", P("recorded"), "--record-input", P("s1.padlog"), "--frames", "3"});
    CHECK(r.ok && c.harness.record_input && c.harness.record_input_path == P("s1.padlog") && c.harness.frames == 3);
    r = Load(c, {"--harness-window", "--harness-out", P("recorded"), "--record-input", "--frames", "4"});
    CHECK(r.ok && c.harness.record_input && c.harness.record_input_path.empty() && c.harness.frames == 4);
    CHECK(c.Fingerprint().find("record_input_path") == std::string::npos);
    r = Load(c, {"--harness-out", P("leg")});
    CHECK(!r.ok && r.error.find("headless") != std::string::npos);
    // harness.* outside the CLI flags is refused, never silently dropped.
    r = Load(c, {"--headless", "--set", "harness.frames=600"});
    CHECK(!r.ok && r.error.find("harness.frames") != std::string::npos);
    // The two root acceptance opt-ins are the exception: `--set` is their only
    // route (the battery's `--set` pass-through reaches them), files refuse them.
    r = Load(c, {"--headless", "--harness-out", P("leg"), "--set", "harness.legacy_input_save=on"});
    CHECK(r.ok && c.harness.legacy_input_save && !c.harness.live_restore);
    r = Load(c, {"--headless", "--harness-out", P("leg"), "--set", "harness.live_restore=on;hle.input_service=on"});
    CHECK(r.ok && c.harness.live_restore && !c.harness.legacy_input_save && c.hle.input_service);
    CHECK(c.Fingerprint().find("harness.") == std::string::npos);
    r = Load(c, {"--set", "harness.legacy_input_save=on"});   // no harness arming needed
    CHECK(r.ok && c.harness.legacy_input_save);
    r = Load(c, {"--set", "harness.live_restore=maybe"});
    CHECK(!r.ok);
    // save.restore_mode (track 3a): an ordinary setting -- a file may set it, the
    // CLI may set it, the two words are live|relaunch, anything else refuses.
    r = Load(c, {"--set", "save.restore_mode=relaunch"});
    CHECK(r.ok && c.save.restore_mode == RestoreMode::Relaunch);
    r = Load(c, {"--set", "save.restore_mode=live"});
    CHECK(r.ok && c.save.restore_mode == RestoreMode::Live);
    r = Load(c, {"--set", "save.restore_mode=sometimes"});
    CHECK(!r.ok && r.error.find("save.restore_mode") != std::string::npos);
    // The route is not part of the composition fingerprint: a slot saved under
    // one route restores under the other.
    r = Load(c, {"--set", "save.restore_mode=relaunch"});
    CHECK(r.ok && c.Fingerprint().find("save.restore_mode") == std::string::npos);
    // The fault-injection pin (TriageBundle acceptance): `--set` only, and only
    // with a harness leg (--harness-out); a plain play session refuses it.
    r = Load(c, {"--headless", "--harness-out", P("leg"), "--set", "harness.inject_fault_frame=7"});
    CHECK(r.ok && c.harness.inject_fault_frame == 7);
    CHECK(c.Fingerprint().find("inject_fault_frame") == std::string::npos);
    r = Load(c, {"--set", "harness.inject_fault_frame=7"});
    CHECK(!r.ok && r.error.find("harness.inject_fault_frame") != std::string::npos &&
          r.error.find("--harness-out") != std::string::npos);
    r = Load(c, {"--headless", "--harness-out", P("leg"), "--set", "harness.inject_fault_frame=-1"});
    CHECK(!r.ok && r.error.find("harness.inject_fault_frame") != std::string::npos);
    WriteFile(P("extra.ini"), "harness.inject_fault_frame = 7\n");
    r = Load(c, {"--config", P("extra.ini")});
    CHECK(!r.ok && r.error.find("harness.inject_fault_frame") != std::string::npos);
    // The savestate trigger (the F5 path without a key press; RootPlaySaveRestore):
    // `--set` only, harness armed or paths.save_dir set; files refuse it. The
    // savestate root paths.save_dir is an ordinary path setting (ini, --set),
    // outside the fingerprint like every paths.* key.
    r = Load(c, {"--headless", "--harness-out", P("leg"), "--set", "harness.save_at_frame=1300"});
    CHECK(r.ok && c.harness.save_at_frame == 1300 && c.paths.save_dir.empty());
    CHECK(c.Fingerprint().find("save_at_frame") == std::string::npos);
    r = Load(c, {"--set", "harness.save_at_frame=1300"});
    CHECK(!r.ok && r.error.find("harness.save_at_frame") != std::string::npos &&
          r.error.find("paths.save_dir") != std::string::npos);
    r = Load(c, {"--set", "harness.save_at_frame=1300;paths.save_dir=" + P("saves")});
    CHECK(r.ok && c.harness.save_at_frame == 1300 && c.paths.save_dir == P("saves"));
    CHECK(c.Fingerprint().find("save_dir") == std::string::npos);
    r = Load(c, {"--headless", "--harness-out", P("leg"), "--set", "harness.save_at_frame=-1"});
    CHECK(!r.ok && r.error.find("harness.save_at_frame") != std::string::npos);
    WriteFile(P("extra.ini"), "harness.save_at_frame = 7\n");
    r = Load(c, {"--config", P("extra.ini")});
    CHECK(!r.ok && r.error.find("harness.save_at_frame") != std::string::npos);
    // The F8 trigger (RootPlayRelaunchRestore): the same route and refusals.
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set", "harness.restore_at_frame=100"});
    CHECK(r.ok && c.harness.restore_at_frame == 100);
    CHECK(c.Fingerprint().find("restore_at_frame") == std::string::npos);
    r = Load(c, {"--set", "harness.restore_at_frame=100"});
    CHECK(!r.ok && r.error.find("harness.restore_at_frame") != std::string::npos);
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set", "harness.restore_at_frame=-2"});
    CHECK(!r.ok && r.error.find("harness.restore_at_frame") != std::string::npos);
    WriteFile(P("extra.ini"), "harness.restore_at_frame = 7\n");
    r = Load(c, {"--config", P("extra.ini")});
    CHECK(!r.ok && r.error.find("harness.restore_at_frame") != std::string::npos);
    WriteFile(P("extra.ini"), "paths.save_dir = " + P("saves") + "\n");
    r = Load(c, {"--config", P("extra.ini")});
    CHECK(r.ok && c.paths.save_dir == P("saves"));
    // The synthetic key press (RootPlayHotkeyF5): `--set` only, a window required, F1..F12@frame.
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set", "harness.inject_key=F5@300,F8@600"});
    CHECK(r.ok && c.harness.inject_key == "F5@300,F8@600");
    CHECK(c.Fingerprint().find("inject_key") == std::string::npos);
    r = Load(c, {"--set", "harness.inject_key=F5@300"});   // no --harness-window: refused (harness armed by the key)
    CHECK(!r.ok && r.error.find("harness") != std::string::npos);
    r = Load(c, {"--headless", "--harness-out", P("leg"), "--set", "harness.inject_key=F5@300"});
    CHECK(!r.ok && r.error.find("harness.inject_key") != std::string::npos);
    // Any SDL key name, with an optional hold range (RootKbmPadOnly: the pad route).
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set", "harness.inject_key=Space@600,W@700-820,Tab@900"});
    CHECK(r.ok && c.harness.inject_key == "Space@600,W@700-820,Tab@900");
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set", "harness.inject_key=W@820-700"});   // hold ends before it starts
    CHECK(!r.ok && r.error.find("harness.inject_key") != std::string::npos);
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set", "harness.inject_key=@300"});   // no key
    CHECK(!r.ok && r.error.find("harness.inject_key") != std::string::npos);
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set", "harness.inject_key=F5@0"});
    CHECK(!r.ok && r.error.find("harness.inject_key") != std::string::npos);
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set", "harness.inject_key=F5"});
    CHECK(!r.ok && r.error.find("harness.inject_key") != std::string::npos);
    WriteFile(P("extra.ini"), "harness.inject_key = F5@300\n");
    r = Load(c, {"--config", P("extra.ini")});
    CHECK(!r.ok && r.error.find("harness.inject_key") != std::string::npos);
    // The scripted display changes (RootScreenModeSwap): `--set` only, a window
    // required, <frame>:<mode> with the Display page's mode names.
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set",
                 "harness.display_sequence=300:borderless,500:windowed,700:fullscreen,900:scale2,1000:vsync,1100:novsync"});
    CHECK(r.ok && c.harness.display_sequence == "300:borderless,500:windowed,700:fullscreen,900:scale2,1000:vsync,1100:novsync");
    CHECK(c.Fingerprint().find("display_sequence") == std::string::npos);
    r = Load(c, {"--set", "harness.display_sequence=300:borderless"});   // no --harness-window: refused (harness armed by the key)
    CHECK(!r.ok && r.error.find("harness") != std::string::npos);
    r = Load(c, {"--headless", "--harness-out", P("leg"), "--set", "harness.display_sequence=300:borderless"});
    CHECK(!r.ok && r.error.find("harness.display_sequence") != std::string::npos);
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set", "harness.display_sequence=300:maximized"});
    CHECK(!r.ok && r.error.find("harness.display_sequence") != std::string::npos);
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set", "harness.display_sequence=0:borderless"});
    CHECK(!r.ok && r.error.find("harness.display_sequence") != std::string::npos);
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set", "harness.display_sequence=borderless"});
    CHECK(!r.ok && r.error.find("harness.display_sequence") != std::string::npos);
    // The window close (RootExitClean): `--set` only, a window required, frame > 0.
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set", "harness.close_at_frame=900"});
    CHECK(r.ok && c.harness.close_at_frame == 900 && c.Fingerprint().find("close_at_frame") == std::string::npos);
    r = Load(c, {"--headless", "--harness-out", P("leg"), "--set", "harness.close_at_frame=900"});
    CHECK(!r.ok && r.error.find("harness.close_at_frame") != std::string::npos);
    r = Load(c, {"--set", "harness.close_at_frame=900"});   // no --harness-window: refused (harness armed by the key)
    CHECK(!r.ok && r.error.find("harness") != std::string::npos);
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set", "harness.close_at_frame=900", "--set", "harness.close_after_ms=5000"});
    CHECK(r.ok && c.harness.close_after_ms == 5000);
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set", "harness.close_after_ms=5000"});   // no anchor boundary
    CHECK(!r.ok && r.error.find("harness.close_after_ms") != std::string::npos);
    // The virtual gamepad (RootPadHotplug): `--set` only, a window required, attach first.
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set", "harness.virtual_pad=attach@600,press@700-760,detach@900"});
    CHECK(r.ok && c.harness.virtual_pad == "attach@600,press@700-760,detach@900" && c.Fingerprint().find("virtual_pad") == std::string::npos);
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set", "harness.virtual_pad=press@700-760"});   // no attach
    CHECK(!r.ok && r.error.find("harness.virtual_pad") != std::string::npos);
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set", "harness.virtual_pad=attach@600,press@760"});   // press needs a range
    CHECK(!r.ok && r.error.find("harness.virtual_pad") != std::string::npos);
    r = Load(c, {"--headless", "--harness-out", P("leg"), "--set", "harness.virtual_pad=attach@600"});
    CHECK(!r.ok && r.error.find("harness.virtual_pad") != std::string::npos);
    // The live pad probe (RootScreenModeSwap): `--set` only, a window and a replay required.
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--inputs", "in.padlog", "--set", "harness.live_pad_probe=on"});
    CHECK(r.ok && c.harness.live_pad_probe && c.Fingerprint().find("live_pad_probe") == std::string::npos);
    r = Load(c, {"--headless", "--harness-out", P("leg"), "--inputs", "in.padlog", "--set", "harness.live_pad_probe=on"});
    CHECK(!r.ok && r.error.find("harness.live_pad_probe") != std::string::npos);
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set", "harness.live_pad_probe=on"});
    CHECK(!r.ok && r.error.find("harness.live_pad_probe") != std::string::npos);
    r = Load(c, {"--harness-window", "--harness-out", P("leg"), "--set", "harness.display_sequence=300:scale5"});
    CHECK(!r.ok && r.error.find("harness.display_sequence") != std::string::npos);
    WriteFile(P("extra.ini"), "harness.display_sequence = 300:borderless\n");
    r = Load(c, {"--config", P("extra.ini")});
    CHECK(!r.ok && r.error.find("harness.display_sequence") != std::string::npos);
    // ALL-ON, NO EXCEPTIONS: every hle.* key is settable to off through --set in
    // a plain play session (no harness), and a prerequisite turned off DEGRADES
    // its dependents (Result.notes, one line each) instead of refusing the run.
    for (const auto& key : Config::Keys()) {
        if (key.rfind("hle.", 0) != 0) continue;
        const std::string off = key == "hle.graphics_strips" ? "model" : key == "hle.startup" ? "retained" : "off";
        r = Load(c, {"--set", key + "=" + off});
        std::string v;
        CHECK(r.ok && c.Get(key, &v) && v == off);
    }
    r = Load(c, {"--set", "hle.graphics_movie=off"});
    CHECK(r.ok && !c.hle.graphics_movie && !c.hle.movie_step && !c.hle.movie_start && !c.hle.movie_wait && !c.hle.movie_stop);
    CHECK(r.notes.size() == 5 && r.notes[0] == "hle.movie_step disabled: requires hle.graphics_movie");
    CHECK(c.hle.startup == StartupMode::Retained);   // the IOP boot stays while a service owner is off
    r = Load(c, {"--set", "hle.music=off"});
    CHECK(r.ok && !c.hle.music && !c.hle.music_buffers && !c.hle.movie_start && !c.hle.movie_wait && !c.hle.movie_stop && !c.hle.audio_mix);
    CHECK(c.hle.service_time);   // stream_files/sound_samples still carry it
    CHECK(r.notes.size() == 6 && r.notes[0] == "hle.music_buffers disabled: requires hle.music");
    r = Load(c, {"--set", "hle.movie_lifecycle=off"});
    CHECK(r.ok && !c.hle.graphics_movie && !c.hle.movie_step && !c.hle.music_buffers && !c.hle.movie_start &&
          !c.hle.movie_wait && !c.hle.movie_stop && !c.hle.audio_mix);
    CHECK(!r.notes.empty() && r.notes[0] == "hle.graphics_movie disabled: requires hle.movie_lifecycle");
    r = Load(c, {"--set", "video.backend=soft"});
    CHECK(r.ok && !c.hle.graphics_frame && !c.hle.graphics_sprites && !c.hle.graphics_text && !c.hle.graphics_sky &&
          !c.hle.graphics_shadows && !c.hle.graphics_distortion && !c.hle.graphics_screen && !c.hle.graphics_movie &&
          !c.hle.graphics_textures && !c.hle.graphics_meshes && c.hle.graphics_strips == GraphicsStrips::Plain && !c.hle.movie_step);
    CHECK(r.notes.size() >= 9 && r.notes[0] == "hle.graphics_shadows disabled: requires video.backend=gl");
    r = Load(c, {});
    CHECK(r.ok && r.notes.empty());   // the compiled defaults degrade nothing
    WriteFile(P("nativeee.ini"), "[harness]\nlegacy_input_save = on\n");
    r = Load(c, {});
    CHECK(!r.ok && r.error.find("harness.legacy_input_save") != std::string::npos &&
          r.error.find("--set harness.legacy_input_save=on") != std::string::npos);
    WriteFile(P("extra.ini"), "harness.live_restore = on\n");
    r = Load(c, {"--config", P("extra.ini")});
    CHECK(!r.ok && r.error.find("harness.live_restore") != std::string::npos);
    WriteFile(P("nativeee.ini"), "[harness]\nheadless = on\n");
    r = Load(c, {});
    CHECK(!r.ok && r.error.find("harness.headless") != std::string::npos);
    WriteFile(P("nativeee.ini"), "# comment\n[video]\nscale = 3\nfps_cap = 30 ; trailing\n[engine]\nmtvu = off\n"
                             "engine.turbo = 1\n");
    r = Load(c, {"--set", "video.nope=1"});
    CHECK(!r.ok && r.error.find("unknown config key") != std::string::npos);
    r = Load(c, {"--set", "novalue"});
    CHECK(!r.ok);
    r = Load(c, {"--profile", "fast"});
    CHECK(!r.ok && r.error.find("unknown profile") != std::string::npos);
    r = Load(c, {"--frames"});
    CHECK(!r.ok && r.error.find("requires a value") != std::string::npos);
    r = Load(c, {"--bogus"});
    CHECK(!r.ok && r.error.find("unknown option") != std::string::npos);
    r = Load(c, {"--set", "time.tick_sync=on"});   // nativeee.ini turned mtvu off -> ok
    CHECK(r.ok);
    r = Load(c, {"--set", "time.tick_sync=on;engine.mtvu=on"});
    CHECK(!r.ok && r.error.find("tick_sync") != std::string::npos);
    WriteFile(P("nativeee.ini"), "video.nope = 1\n");
    r = Load(c, {});
    CHECK(!r.ok && r.error.find("unknown config key") != std::string::npos);
    WriteFile(P("nativeee.ini"), "garbage line\n");
    r = Load(c, {});
    CHECK(!r.ok && r.error.find("key=value") != std::string::npos);
    std::remove(P("nativeee.ini").c_str());
    r = Load(c, {"--version"});
    CHECK(r.ok && r.show_version);
    r = Load(c, {"--config", P("missing.ini")});
    CHECK(!r.ok);
}

static void TestDisplayIniCompat() {
    std::remove(P("nativeee.ini").c_str());
    std::remove(P("ps2r.ini").c_str());
    std::remove(P("display.ini").c_str());
    std::remove(P("ps2r.ini.migrated").c_str());
    std::remove(P("display.ini.migrated").c_str());
    Config c;
    // Defaults == absent file.
    CHECK(c.video.fit_mode == FitMode::Fit && c.video.aspect == AspectSel::Auto &&
          c.video.custom_scale_pct == 100);
    // The one-time migration: a root with the former ps2r.ini + display.ini and no
    // nativeee.ini -> nativeee.ini written from both (display.ini's five keys win),
    // the former files renamed .migrated, the Result says so.
    c.video.fit_mode = FitMode::Fill;
    c.video.aspect = AspectSel::R16_9;
    c.video.custom_scale_pct = 250;
    c.video.custom_offset_x = -40;
    c.video.custom_offset_y = 64;
    WriteFile(P("display.ini"), ConfigLoader::RenderDisplayIni(c));
    WriteFile(P("ps2r.ini"), "# mine\nvideo.aspect = 4:3\n[ui]\nperf_overlay = on\n");
    Config d;
    ConfigLoader::Result r = Load(d, {});
    CHECK(r.ok && r.settings_migrated && r.settings_file == P("nativeee.ini"));
    CHECK(d.video.fit_mode == FitMode::Fill && d.video.aspect == AspectSel::R16_9 && d.ui.perf_overlay);
    CHECK(d.video.custom_scale_pct == 250 && d.video.custom_offset_x == -40 && d.video.custom_offset_y == 64);
    CHECK(std::ifstream(P("nativeee.ini")).good() && std::ifstream(P("ps2r.ini.migrated")).good() &&
          std::ifstream(P("display.ini.migrated")).good() && !std::ifstream(P("ps2r.ini")).good() && !std::ifstream(P("display.ini")).good());
    {
        bool noted = false;
        for (const auto& n : r.notes) noted = noted || n.find("settings: moved to") != std::string::npos;
        CHECK(noted);
    }
    // The second start reads the store alone; a display.ini written afterwards is not consulted.
    WriteFile(P("display.ini"), "mode = integer\n");
    r = Load(d, {});
    CHECK(r.ok && !r.settings_migrated && d.video.fit_mode == FitMode::Fill && d.ui.perf_overlay);
    std::remove(P("display.ini").c_str());
    // Migration refused when the store exists already (never overwritten), and
    // turned off (SetMigrateLegacy(false)): the legacy files are read in place.
    std::remove(P("nativeee.ini").c_str());
    WriteFile(P("ps2r.ini"), "[video]\nscale = 3\n");
    {
        std::vector<char*> argv;
        static std::vector<std::string> keep;
        keep = {"nativeee"};
        for (auto& s : keep) argv.push_back(&s[0]);
        ConfigLoader l;
        l.SetSearchDir(g_dir);
        l.SetMigrateLegacy(false);
        Config e;
        r = l.Load((int)argv.size(), argv.data(), e);
        CHECK(r.ok && !r.settings_migrated && e.video.scale == 3 && std::ifstream(P("ps2r.ini")).good() && !std::ifstream(P("nativeee.ini")).good());
    }
    std::remove(P("ps2r.ini").c_str());
    std::remove(P("ps2r.ini.migrated").c_str());
    std::remove(P("display.ini.migrated").c_str());
    // --display-ini <path>: the compatibility read of an explicitly named file
    // (the original loader's clamps; malformed / unknown lines ignored).
    WriteFile(P("old_display.ini"),
              "; comment only\ngarbage line without equals\nunknown_key = 7\nmode = warp9\n"
              "aspect = 4:3\ncustom_scale = 100000\n  custom_offset_x   =   -12   ; trailing comment\n"
              "custom_offset_y = -99999\n");
    r = Load(d, {"--display-ini", P("old_display.ini")});
    CHECK(r.ok);
    CHECK(d.video.fit_mode == FitMode::Fit);          // unknown mode value ignored (default stands)
    CHECK(d.video.aspect == AspectSel::R4_3);
    CHECK(d.video.custom_scale_pct == 400);
    CHECK(d.video.custom_offset_x == -12);
    CHECK(d.video.custom_offset_y == -8192);
    // Every mode / aspect name round-trips.
    const char* modes[] = {"stretch", "fit", "fill", "integer", "custom"};
    for (int i = 0; i < 5; i++) {
        Config m;
        CHECK(ConfigLoader::ApplyDisplayIniText(std::string("mode = ") + modes[i] + "\n", m));
        CHECK((int)m.video.fit_mode == i);
    }
    const char* aspects[] = {"auto", "4:3", "16:9", "16:10", "21:9"};
    for (int i = 0; i < 5; i++) {
        Config m;
        ConfigLoader::ApplyDisplayIniText(std::string("aspect = ") + aspects[i] + "\n", m);
        CHECK((int)m.video.aspect == i);
    }
    // --set wins over --display-ini (a later tier).
    WriteFile(P("old_display.ini"), "mode = fill\n");
    r = Load(d, {"--display-ini", P("old_display.ini"), "--set", "video.fit_mode=integer"});
    CHECK(r.ok && d.video.fit_mode == FitMode::Integer);
    std::remove(P("old_display.ini").c_str());
    std::remove(P("nativeee.ini").c_str());
}

int main(int argc, char** argv) {
    if (argc > 1) g_dir = argv[1];
    if (argc > 2) g_settings_table = argv[2];
    if (g_settings_table.empty()) {
        std::printf("usage: test_config <scratch dir> <settings_table.tsv>\n");
        return 2;
    }
    TestPeakDefaults();
    TestFingerprint();
    TestDeterministicProfile();
    TestValidator();
    TestDegradeDependents();
    TestServiceOnlyStartupRefusals();
    TestLoaderPrecedence();
    TestDisplayIniCompat();
    if (g_fail) {
        std::printf("test_config: %d FAILURE(S)\n", g_fail);
        return 1;
    }
    std::printf("test_config: all checks passed\n");
    return 0;
}
