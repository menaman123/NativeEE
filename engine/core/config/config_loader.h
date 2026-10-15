// core/config/config_loader.h -- builds the process Config from its sources, in
// precedence order (later wins):
//   defaults < ps2r.ini < --config <file> < --set key=value (repeatable)
//            < --profile <name> < PS2R_SET (dev builds only)
// plus the display.ini [video] compatibility read (mode/aspect/custom_scale/
// custom_offset_x/y written by the original in-app GUI) applied at the
// ps2r.ini tier and never written back unless --save-display is given.
//
// CLI grammar (everything the app shell accepts):
//   --config <file>        --set k=v         --profile <name>
//   --headless             --harness-out D   --frames N        --load-state S
//   --inputs <padlog>      --record-input    --state-every K   --warp-to N
//   --iso <path>           --game-files D    --display-ini P   --save-display    --harness-window
//   --bench-report <path>  --log-level L     --version  -v     --help  -h
//   --vu1-library <title|none>   --vu1-miss-cases D   --watch-word 0xADDR[,0xADDR...]
//   --vu0-trace 0xPC[,0xPC...]
// Unknown options and malformed values are errors.
//
// Harness knobs that are not Config fields (--harness-window, --vu1-library,
// --vu1-miss-cases, --watch-word, --vu0-trace)
// are CLI plumbing like harness.*: never a file key, never in the Fingerprint.
// harness.* itself is refused from every source but its CLI flag, with three
// exceptions: the root acceptance opt-ins harness.legacy_input_save and
// harness.live_restore and the fault-injection pin harness.inject_fault_frame
// have no flag and are accepted by `--set` (and PS2R_SET in dev builds) only --
// never by ps2r.ini or --config.
// Every hle.* key is settable from every tier (`--set hle.<key>=off` is the
// per-run bisect form); a dependent whose prerequisite is off is DEGRADED
// (Result.notes) rather than refused.
// Load() records them in Result and in the process-wide HarnessRequest the
// composition root reads (it receives the Config, not the Result).
//
// PS2R_SET is the ONE getenv site in the whole tree: a ';'-separated
// `key=value` blob read only in dev (non-NDEBUG) builds so the harness runner
// can pin a field without touching the command line. It is applied LAST.
#pragma once

#include <string>
#include <vector>

#include "engine/core/config/config.h"

namespace ps2r {

class ConfigLoader {
public:
    struct Result {
        bool        ok = false;
        bool        show_version = false;
        bool        show_help = false;
        bool        harness_window = false;       // --harness-window given
        bool        vu1_library_none = false;     // --vu1-library none given
        std::string vu1_miss_cases_dir;           // --vu1-miss-cases <dir>: the library's missed kicks as Vu1Case files
        std::string watch_words;                  // --watch-word list (diagnostics builds act on it)
        uint64_t    watch_flush_from = 0;         // --watch-flush-from <boundary> (0 = flush at the leg's end only)
        uint64_t    watch_record_from = 0;        // --watch-record-from <boundary> (0 = record from the first store)
        std::string vu0_trace;                    // --vu0-trace list (diagnostics builds act on it)
        std::string dump_frames_dir;              // --dump-frames <dir>: one PPM per present key
        std::string dump_textures_dir;            // --dump-textures <dir>: every converted texture as <hash>.png
        bool        save_display = false;         // --save-display given
        // CHARTER 2 C2: `--extract-disc <image>` runs the first-run extraction
        // from the command line (no window) and exits; `--launcher` shows the
        // launcher screen even when the game files exist; `harness_cli` = a
        // harness/developer flag was given (--game-files, --profile, --set,
        // --iso, --headless, --harness-out, --frames, --inputs, --load-state,
        // --config): the launcher never interposes on such a run.
        std::string extract_disc;
        bool        launcher = false;
        std::string launcher_screenshot;   // --launcher-screenshot <ppm>: dump the launcher's 3rd frame, exit
        bool        launcher_then_play = false;   // --launcher-then-play: press Play once the Ready screen is up
                                                  // (after the screenshot when one is asked for)
        std::string launcher_disc;         // --launcher-disc <image>: the scripted drop (verify + extract at open)
        std::string launcher_page;         // --launcher-page <play|display|controls|saves|fixes|data>: the Ready page shown
        bool        harness_cli = false;
        std::string error;
        std::string display_ini_path;             // resolved display.ini
        std::string bench_report_path;            // --bench-report
        std::vector<std::string> sources;         // what was applied, in order
        // ConfigValidator::DegradeDependents' notes ("hle.movie_step disabled:
        // requires hle.graphics_movie"): the keys the loader turned off because
        // a prerequisite was off; the root logs each as `[config] <note>`.
        std::vector<std::string> notes;
    };

    // The CLI-only harness knobs of the last Load() (the composition root's
    // view; Result is the caller's). --vu1-library none turns the VU1 HLE
    // library consult off: the registry is empty and every image runs on the
    // fallback tier (PHASE5_IMPL (e)); run.meta vu1_library=none. Not a
    // Config key by decision DP-14 (docs/phases/DECISIONS_LOG.md).
    // --watch-word 0xADDR[,0xADDR...] names guest words whose CPU stores a
    // diagnostics build records (core/diag WordWatch: <leg>/watch.log, run.meta
    // watch_words=); dev/Release builds accept and ignore it. Not a Config key.
    // --vu0-trace 0xPC[,0xPC...] names VU0 micro entry pcs whose VCALLMS runs a
    // diagnostics build records operands/results for (engine/vu Vu0Trace:
    // <leg>/vu0_trace.log, run.meta vu0_trace=); likewise accepted and ignored
    // by dev/Release builds. Not a Config key.
    // --dump-frames <dir> writes every presented frame the frames lane hashes as
    // <dir>/f<key>.ppm (P6, top-down, the lane's native size) so a frame can be
    // pixel-diffed against the original's dump; every build acts on it. Not a
    // Config key.
    struct HarnessRequest {
        bool vu1_library_none = false;
        // --vu1-miss-cases <dir>: the first kicks of every (variant, entry) the VU1
        // library misses, as Vu1Case records (the replay oracle's input). Not a
        // Config key.
        std::string vu1_miss_cases_dir;
        std::string watch_words;
        uint64_t watch_flush_from = 0;
        uint64_t watch_record_from = 0;
        std::string vu0_trace;
        std::string dump_frames_dir;
        // --dump-textures <dir> writes every texture the run converts, once per
        // TextureContentHash, as <dir>/<hash>.png + index.tsv + contact_sheet.png
        // (the texture-pack key; A4). Every build acts on it. Not a Config key.
        std::string dump_textures_dir;
    };
    static const HarnessRequest& Request() { return request_; }
    static void ResetRequestForTest() { request_ = HarnessRequest{}; }

    // Directory searched for ps2r.ini / display.ini when no explicit path is
    // given (default: the current working directory).
    void SetSearchDir(const std::string& dir) { search_dir_ = dir; }
    // The platform's user data root (CHARTER 2 C4; the app resolves it --
    // SDL_GetPrefPath -- the engine never reads the environment for it). Load
    // searches ps2r.ini there (unless SetSearchDir named another directory) and
    // resolves every empty paths.* directory under it (paths.user_data from any
    // tier overrides the root for the resolution, never for the ini search).
    // Unset: the former cwd-relative names (memcards, game_files, logs, textures).
    void SetUserDataRoot(const std::string& root) { user_data_root_ = root; }
    // The resolution rule alone (public for the tests and the launcher): empty
    // mc_dir/game_files/log_dir/texture_packs become <root>/<name>, or the bare
    // name when root is empty. Returns the root used ("" = none).
    static std::string ResolveUserPaths(Config& c, const std::string& platform_root);

    // M12 (TRACK 3b): whether the retained (IOP-booting) startup arm is compiled
    // into this binary. The oracle binary (nativeee_oracle) says true (the
    // default: every engine test loads as before); the service-only play binary
    // (nativeee) says false, and then
    //   * `hle.startup=retained` set by the user REFUSES (named),
    //   * the deterministic profile's `hle.startup=retained` pin becomes `native`
    //     with a note (the profile is still usable for timing-deterministic legs),
    //   * the twelve degrade edges (`--set hle.<key>=off` / iop.dispatch!=frame
    //     that used to fall back to the IOP boot) REFUSE with the key named --
    //     never a silent IOP boot, because there is no IOP to boot.
    void SetRetainedStartupAvailable(bool available) { retained_startup_available_ = available; }

    // Parses argv, applies every source in precedence order, runs the
    // dependent-default rules and the validator. On failure `out` is left in
    // an unspecified state and Result.error names the cause.
    Result Load(int argc, char** argv, Config& out);

    // Pieces (public for the tests):
    // key=value lines (optional [section] headers prefix the key); '#'/';'
    // start comments. Unknown keys are errors.
    static bool ApplyIniText(const std::string& text, Config& c, std::string* err);
    static bool ApplyIniFile(const std::string& path, Config& c, std::string* err,
                             bool* existed);
    // display.ini compatibility: mode/aspect/custom_scale/custom_offset_x/y with
    // the original's clamps ([0,4]/[0,3]/[25,400]/[-8192,8192]); unknown mode/
    // aspect names and unknown keys are ignored.
    static bool ApplyDisplayIniText(const std::string& text, Config& c);
    static bool ApplyDisplayIniFile(const std::string& path, Config& c);
    static std::string RenderDisplayIni(const Config& c);
    // `key=value;key=value` blob (the --set / PS2R_SET grammar).
    static bool ApplySetBlob(const std::string& blob, Config& c, std::string* err,
                             bool* touched_latency_model);
    static const char* Usage();

private:
    static HarnessRequest request_;
    std::string search_dir_;
    std::string user_data_root_;
    bool retained_startup_available_ = true;
};

}  // namespace ps2r
