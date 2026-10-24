#pragma once
// Vu1AotPack -- the PRE-BUILT VU1 image library pack: the generated libraries of a
// title's VU1 microprograms, built by tools/vu1_aot_pack from the title's corpus,
// shipped NEXT TO THE BINARY so a player without a C++ compiler gets the native
// tier from the first frame. Read-only for the engine (never written, never
// pruned at run time; the user cache under <user_data>/vu1_aot stays the writable
// side). The loader's search order per image: the shipped pack -> the user cache
// -> the builder (compile), app/host/vu1_aot_builder.h.
//
//   <pack dir>/pack.txt                      the manifest:
//        vu1_aot_pack version=1 abi=<abi key> platform=<os-arch> compiler=<token> opt=<n> images=<n>
//        image=<16 hex> opt=<n> blocks=<n> ops=<n> bytes=<n>        one row per library
//   <pack dir>/<image 16 hex>.O<opt>.<dylib|so|dll>   the library (the builder's artefact, copied)
//
// A pack is accepted only when its abi key equals the running engine's
// (kVu1AotAbiKey) and its platform token equals this host's; each library is then
// loaded and RE-VERIFIED on the image's corpus like any other artefact (the ABI
// key is a filter, the verification is the proof). Default location: the
// directory of the running executable + "/vu1_aot_pack" (DefaultDir); Config
// engine.vu1_aot_pack overrides it.
//
// Licence boundary (the title's README / CONTRIBUTING state it): the pack is
// generated code derived from the game's microprograms, like the corpus it was
// built from -- game-derived. It is never committed to a repository and never
// published as a public release asset; it is built on the player's machine (the
// builder, a compiler present) or on a lane that has the disc, for private use.
#include <cstdint>
#include <string>
#include <vector>

namespace ps2r::app {

class Vu1AotPack {
public:
    static constexpr uint32_t kVersion = 1;

    struct Row {
        uint64_t image = 0;
        int opt = 2;
        uint32_t blocks = 0;
        uint64_t ops = 0;
        uint64_t bytes = 0;
    };
    struct Manifest {
        std::string abi;         // the ABI key the libraries were built against
        std::string platform;    // PlatformToken() of the building host
        std::string compiler;    // the compiler token (informative)
        int opt = 2;
        std::vector<Row> rows;
    };

    // "<dir of the running executable>/vu1_aot_pack" ("" when the executable path is unknown).
    static std::string DefaultDir();
    // "macos-arm64" | "macos-x86_64" | "linux-x86_64" | "linux-arm64" | "windows-x64" | "unknown".
    static const char* PlatformToken();
    static std::string ManifestPath(const std::string& dir);
    static std::string LibraryPath(const std::string& dir, uint64_t image, int opt);

    // Read pack.txt under `dir`; false (and `err`) when absent or malformed.
    static bool Read(const std::string& dir, Manifest* out, std::string* err);
    // Write pack.txt under `dir` (created); the libraries are the caller's copies.
    static bool Write(const std::string& dir, const Manifest& m, std::string* err);
    // The row of `image` or nullptr.
    static const Row* Find(const Manifest& m, uint64_t image);

    // One manifest row / header line (public for the round-trip test).
    static std::string FormatRow(const Row& r);
    static bool ParseRow(const std::string& line, Row* r);
    static std::string FormatHeader(const Manifest& m);
    static bool ParseHeader(const std::string& line, Manifest* m);
};

}  // namespace ps2r::app
