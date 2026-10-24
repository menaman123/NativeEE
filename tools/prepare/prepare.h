// tools/prepare/prepare.h -- ps2r::prepare: "Preparing your game" (decision D-A).
//
// A release ships the launcher, an ENGINE BUNDLE per platform (cmake/EngineBundle.cmake:
// the engine as static archives + the host objects + the tools + the title's non-game-derived
// inputs + BUNDLE.json with the exact command templates of that build) and a pinned portable
// COMPILER per platform. Nothing derived from the game is downloaded or published. This
// library turns the player's own disc image into the game binary ON THE PLAYER'S MACHINE:
//
//   1. fetch     the release index (GitHub Releases API shape, or a local / file:// copy of it),
//                the engine bundle and the compiler archives -- resumable (curl -C -), SHA-256
//                verified against the index digests / SHA256SUMS, cached under <folder>/cache/downloads
//   2. unpack    <folder>/cache/bundle/<tag>/..., <folder>/cache/compiler/ (the compiler also serves the
//                first run's VU1 AOT compile: nativeee.ini engine.vu1_aot_compiler)
//   3. disc      ps2r_disc_extract / ps2r_elf_embedded per BUNDLE.json: the ELF and the IOP images out of
//                the disc image, SHA-256 gated by the title's TITLE_INPUTS.sha256 rows
//   4. generate  ps2r_recompiler_v2 + ps2r_continuations, ps2r_iop_recompiler; the output MUST equal the
//                title's MANIFEST.sha256 pins (same tool + same inputs = same text) before anything compiles
//   5. compile   the shard PCH, the 32 shards + the common TU + the IOP TUs + the one hle TU, -j<jobs>
//   6. link      the game binary into <folder>/<binary_name>; the title tables into <folder>/title/;
//                <folder>/nativeee.ini (paths.mc_dir=memcards, paths.user_data=cache, paths.game_files=game,
//                paths.iso=<the disc>, engine.vu1_aot_prelude, engine.vu1_aot_compiler)
//   7. stamp     <folder>/cache/prepared.json: (ELF sha256, release tag, bundle sha256, binary sha256) --
//                a later Run with the same disc and release finishes at once
//
// The PrepareRunner interface the launcher drives (one progress bar, one plain-language line):
//   progress(fraction 0..1, eta_seconds (<0 = unknown), phase_text)   phase_text from ui_strings.h ONLY
//   failed(text, log_path)                                            text from ui_strings.h; the log is for the Details sheet
//   finished(binary_path)
// Everything else (tool output, command lines) goes to log_line() and the log file, never to the UI.
// Run() is synchronous: call it from a worker thread; Cancel() from any thread stops between steps
// (a running compiler is waited for; partial outputs are removed).
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ps2r::prepare {

struct Options {
    std::string index_url;      // the release index: https://api.github.com/repos/<o>/<r>/releases, or
                                // http://127.0.0.1:<port>/releases.json, or file:///dir/releases.json, or a local path
    std::string release_tag;    // "" = the newest non-prerelease entry of the index
    std::string platform;       // "" = this host (macos-arm64 | macos-x86_64 | windows-x86_64 | linux-x86_64)
    std::string iso;            // the player's disc image
    std::string folder;         // the game folder
    int jobs = 0;               // 0 = the hardware's concurrency
    bool force = false;         // prepare again even when prepared.json matches
    bool keep_objects = true;   // leave <folder>/cache/work/<tag>/obj after a successful link
    // Development / judge overrides (never set by the launcher):
    std::string compiler_dir;   // use this compiler root instead of downloading one ("" = download)
    std::string sysroot;        // the SDK ("" = resolve: bundle sysroot/, else the system's)
    std::string curl;           // the curl executable ("" = `curl` on the PATH)
    std::string tar;            // the tar executable ("" = `tar` on the PATH)
};

// The phases, for the report and the tests (the UI never sees these names).
enum class Phase { Index, Download, Unpack, Disc, Generate, Compile, Link, Write, Done, Failed };
const char* PhaseName(Phase p);

class Listener {
public:
    virtual ~Listener() = default;
    virtual void progress(double fraction, double eta_seconds, const std::string& phase_text) = 0;
    virtual void failed(const std::string& text, const std::string& log_path) = 0;
    virtual void finished(const std::string& binary_path) = 0;
    virtual void log_line(const std::string& line) { (void)line; }
};

struct Report {
    std::string release_tag;
    std::string platform;
    std::string bundle_dir, compiler_dir, sysroot, compiler_driver;
    std::string binary_path, ini_path, log_path, work_dir;
    std::string elf_sha256, build_id, abi_key;
    uint64_t bytes_downloaded = 0;      // this run (0 when everything was cached)
    uint64_t bytes_total = 0;           // the archives' sizes
    bool cache_hit = false;             // prepared.json matched: nothing was done
    bool manifest_ok = false;           // the generated text equalled MANIFEST.sha256 (EE) and the IOP pins
    std::map<std::string, double> phase_seconds;
    double total_seconds = 0;
    Phase failed_phase = Phase::Done;
    std::string failure;                // the developer-facing reason (the UI text is in the listener call)
};

class PrepareRunner {
public:
    PrepareRunner(Options options, Listener& listener);
    ~PrepareRunner();

    // Runs every step; true when the binary is in place (finished() was called), false after failed().
    bool Run();
    void Cancel();
    bool cancelled() const { return cancel_.load(); }
    const Report& report() const { return report_; }

    // The release platform id of this host (macos-arm64 | macos-x86_64 | windows-x86_64 | linux-x86_64).
    static std::string HostPlatform();
    // The stamp a Run leaves; true when `folder` holds a binary prepared from a disc with `elf_sha256`
    // for `release_tag` (and the binary is still there). The launcher's "ready to play" test.
    static bool IsPrepared(const std::string& folder, const std::string& elf_sha256, const std::string& release_tag,
                           std::string* binary_path);
    // Substitute ${name} placeholders (public for the tests).
    static std::string Substitute(const std::string& text, const std::map<std::string, std::string>& vars);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Options options_;
    Listener& listener_;
    Report report_;
    std::atomic<bool> cancel_{false};
};

}  // namespace ps2r::prepare
