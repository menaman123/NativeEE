#pragma once
// Vu1AotBuilder -- the run-time half of the VU1 code generator: for a VERIFIED
// image (the cache's verdict) it emits the image's blocks as C++
// (Vu1AotEmitter), compiles them with a host toolchain found at run time,
// loads the library, RE-VERIFIES the loaded functions on the image's corpus
// with the same gate the specialised tier passed (Vu1AotVerifier with the
// exports installed), and only then publishes the exports table to the tier,
// which swaps the image's block functions at its next kick. Everything happens
// on this class's worker thread(s), never on the game or VU thread; a step that
// fails leaves the specialised tier running (and a refused library is deleted).
//
//   <user_data>/vu1_aot/<abi key>/include/               the staged prelude headers (MANIFEST.txt lists them)
//   <user_data>/vu1_aot/<abi key>/<image>.cpp            the generated TU
//   <user_data>/vu1_aot/<abi key>/<image>.O<n>.<dylib|so|dll>   the compiled library at -O<n> (reused on the next run)
//   <user_data>/vu1_aot/<abi key>/<image>.log            the compiler's output when it failed
//   <user_data>/vu1_aot/<abi key>/native.txt             one row per image this build judged:
//        image=<16 hex> status=native|refused|compile_failed|load_failed|no_compiler compiler=<token> opt=<n>
//        blocks=<n> ops=<n> compile_ms=<n> verify_ms=<n> bytes=<n> cached=0|1 source=pack|cache|compiled [reason=<text>]
//
// Generated code and libraries are WRITTEN only here (never under the tree);
// every artefact is keyed by image hash AND the engine's VU1 AOT ABI KEY
// (kVu1AotAbiKey, engine/vu/vu1_aot/vu1_aot_abi_key.h: a content hash of the
// prelude closure the TU compiles against -- unchanged VU sources keep the
// libraries across engine rebuilds; the exports table carries the same key and
// Load refuses a library built for another). The search order per image:
//   1. the SHIPPED PACK (app/host/vu1_aot_pack.h): Settings::pack_dir, else the
//      directory of the executable + "/vu1_aot_pack"; accepted when its abi key
//      and platform match; read-only (a library that fails to load or diverges
//      is skipped, never deleted);
//   2. the user cache above (a library this or an earlier run compiled);
//   3. the builder: emit + compile with the host toolchain.
// The toolchain: engine.vu1_aot_compiler, else the compiler that built the
// engine (vu1_aot_paths.h), else /usr/bin/clang++, c++, g++, clang++ on the
// PATH -- each probed with `--version` through the platform's spawn (the engine
// reads no environment). No compiler: Open still succeeds when a pack or a
// cached library can supply code (the loader half runs; an image with neither
// is recorded `no_compiler` and stays on the specialised tier); Open returns
// false with a one-line note only when nothing could ever produce a library.
//
// macOS note (hardened runtime): a locally built, unsigned nativeee loads the
// unsigned library it just compiled; a notarised release build would need the
// com.apple.security.cs.disable-library-validation entitlement (or to sign the
// artefact). Documented, not solved here. The library has undefined references
// to the engine's own symbols (the GENERIC bodies, the flag pipeline, the kick)
// resolved against the executable at load: -undefined dynamic_lookup on macOS;
// on Linux the executable must export them (-rdynamic); Windows (clang-cl, an
// import library of the exe) is an open gap.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/host/vu1_aot_pack.h"
#include "engine/platform/host/platform.h"
#include "engine/vu/vu1_aot/vu1_aot_block_gen.h"
#include "engine/vu/vu1_aot/vu1_aot_exports.h"
#include "engine/vu/vu1_aot/vu1_aot_tier.h"

struct Vu1Case;

namespace ps2r::app {

class Vu1AotBuilder {
public:
    struct Settings {
        std::string root;            // <user_data>/vu1_aot
        std::string build_id;        // g_build_id
        std::string compiler;        // engine.vu1_aot_compiler ("" = the search above; "none" = no compiler, loader only)
        bool search_defaults = true; // false = try `compiler` only (the no-compiler test path)
        std::string prelude_dir;     // the staged prelude tree ("" = the build's)
        std::string pack_dir;        // engine.vu1_aot_pack ("" = next to the executable)
        bool search_pack = true;     // false = never look for a shipped pack (tests)
        int opt_level = 2;           // -O2 | -O1
        Vu1AotBlockGen::Policy region;  // the region / chaining policy of the generated functions (engine.vu1_aot_region_*, _chain)
        int workers = 1;             // worker threads (1..2)
        bool keep_source = true;     // leave <image>.cpp beside the library
    };
    struct Census {
        uint64_t jobs = 0;           // images submitted
        uint64_t pack = 0;           // libraries loaded from the shipped pack
        uint64_t pack_rejected = 0;  // pack libraries that failed to load or diverged (skipped, kept)
        uint64_t no_compiler = 0;    // images with no library anywhere and no compiler to make one
        uint64_t cached = 0;         // libraries found in the user cache (no compile)
        uint64_t compiled = 0;       // TUs compiled this run
        uint64_t compile_failed = 0;
        uint64_t load_failed = 0;    // dlopen / symbol / table mismatch
        uint64_t native = 0;         // images published to the tier (re-verified identical)
        uint64_t refused = 0;        // loaded code that diverged from the interpreter (artefact deleted)
        uint64_t compile_ns = 0;
        uint64_t verify_ns = 0;
        uint64_t blocks = 0;         // block functions published
        uint64_t ops = 0;            // slots inside them
        uint64_t pending = 0;        // jobs queued or running
    };
    // One image's outcome (the synchronous path's return).
    struct Built {
        const Vu1AotImageExports* exports = nullptr;  // null = not native (see `status` / `reason`)
        std::string status;                          // native | refused | compile_failed | load_failed | no_cases | no_compiler
        std::string reason;
        std::string source;                          // pack | cache | compiled (native only)
        uint32_t blocks = 0;
        uint64_t ops = 0;
        uint64_t compile_ns = 0;
        uint64_t verify_ns = 0;
        bool cached = false;
    };

    Vu1AotBuilder(Vu1AotTier& tier, Settings settings);
    ~Vu1AotBuilder();

    // Find the toolchain, stage the include root, start the workers. False =
    // no compiler / no root (`note` says which); nothing runs then.
    bool Open(std::string* note);
    // Stop the workers (a running job finishes; queued jobs are dropped). The
    // loaded libraries stay mapped until destruction: the tier may still hold
    // their functions on its blocks. Destroy only when the VU is quiet.
    void Close();
    bool open() const { return open_; }

    // A verified image with its corpus (moved in) and the block pcs the verifier
    // saw (the generator's seeds beyond the cases' entries; may be empty).
    void Submit(uint64_t image_hash, std::vector<std::unique_ptr<Vu1Case>> cases, std::vector<uint32_t> seed_pcs);
    // The whole pipeline for one image on the CALLING thread (the tests and the
    // tools): emit / compile (or reuse the library), load, re-verify. Publishes
    // to the tier on success like the worker does. Needs Open.
    Built BuildNow(const std::vector<const Vu1Case*>& cases, const std::vector<uint32_t>& seed_pcs);

    // run.meta vu1_aot_codegen= row.
    std::string CensusRow() const;
    Census census() const;
    const std::string& compiler() const { return compiler_; }
    const std::string& compiler_version() const { return compiler_version_; }
    bool has_compiler() const { return !compiler_.empty(); }
    // The ABI key the libraries are keyed by (kVu1AotAbiKey unless Settings::build_id
    // names a test key -- see Open).
    const std::string& abi_key() const { return abi_key_; }
    // The shipped pack in use ("" = none).
    const std::string& pack_dir() const { return pack_dir_; }

    // Paths and the toolchain line (public for the test).
    static std::string BuildDir(const std::string& root, const std::string& build_id);
    static std::string IncludeDir(const std::string& root, const std::string& build_id);
    static std::string LibraryPath(const std::string& root, const std::string& build_id, uint64_t image_hash, int opt);
    static std::string SourcePath(const std::string& root, const std::string& build_id, uint64_t image_hash);
    // Whether `--version`'s first line names a compiler with a guaranteed tail call
    // (clang; gcc >= 15): a chained TU (Policy::chain) needs one, Open() generates the
    // unchained shape for any other and says so in its note.
    static bool CompilerChains(const std::string& version_line);
    // Probe `preferred` then the defaults with `--version`; the first that runs wins.
    static bool FindCompiler(const std::string& preferred, bool search_defaults, std::string* path, std::string* version);
    // The compile line: `compiler` + the engine's VU semantic flags + -O<opt> + the include root.
    static std::vector<std::string> CompileCommand(const std::string& compiler, const std::string& include_dir, int opt,
                                                   const std::string& src, const std::string& out);
    // Copy the staged prelude tree (MANIFEST.txt) into `include_dir` once.
    static bool StageInclude(const std::string& prelude_dir, const std::string& include_dir, std::string* err);

private:
    struct Job {
        uint64_t image = 0;
        std::vector<std::unique_ptr<Vu1Case>> cases;
        std::vector<uint32_t> seeds;
        bool cached = false;   // the library is on disk already
    };
    struct Library {
        uint64_t image = 0;
        ps2r::host::DynamicLibrary lib;
        const Vu1AotImageExports* exports = nullptr;
    };

    void WorkerMain();
    Built BuildOne(uint64_t image, const std::vector<const Vu1Case*>& cases, const std::vector<uint32_t>& seeds);
    // Load + re-verify `path` for `image`; publishes on success (Built::status native).
    // `source` names where the library came from; a failure never deletes a pack library.
    bool LoadAndPublish(uint64_t image, const std::string& path, const std::vector<const Vu1Case*>& cases, const char* source,
                        Built* b);
    void OpenPack();
    bool Compile(const std::string& src, const std::string& out, std::string* log, uint64_t* ns);
    bool Load(uint64_t image, const std::string& path, Library* out, std::string* err);
    void AppendRow(uint64_t image, const Built& b);
    void Fold(const Built& b);

    Vu1AotTier& tier_;
    Settings settings_;
    std::string compiler_;
    std::string compiler_version_;
    std::string abi_key_;
    std::string include_dir_;
    std::string pack_dir_;           // the accepted shipped pack ("" = none)
    Vu1AotPack::Manifest pack_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::vector<Job> queue_;
    std::vector<std::thread> workers_;
    std::vector<Library> libraries_;   // every library loaded (kept mapped)
    std::atomic<bool> stop_{false};
    bool open_ = false;
    Census census_;
};

}  // namespace ps2r::app
