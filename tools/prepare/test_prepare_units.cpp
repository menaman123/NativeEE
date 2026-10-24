// tools/prepare/test_prepare_units.cpp -- PrepareUnits: the pieces of ps2r_prepare that need no network,
// no disc and no compiler: placeholder substitution, the prepared.json stamp test, the host platform id,
// and a Run() against an EMPTY folder that must fail at the index step with the plain-language text and
// never touch anything but <folder>/cache/logs.
//
//   test_prepare_units <scratch dir>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "tools/prepare/prepare.h"
#include "tools/prepare/ui_strings.h"

namespace fs = std::filesystem;
using namespace ps2r::prepare;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

struct Capture : Listener {
    std::string last_text, fail_text, fail_log, bin;
    double last_fraction = -1;
    int progress_calls = 0;
    void progress(double f, double, const std::string& t) override { last_fraction = f; last_text = t; ++progress_calls; }
    void failed(const std::string& t, const std::string& l) override { fail_text = t; fail_log = l; }
    void finished(const std::string& b) override { bin = b; }
};
}  // namespace

int main(int argc, char** argv) {
    const fs::path scratch = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "prepare_units";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch, ec);

    // Substitute
    const std::map<std::string, std::string> vars = {{"bundle", "/b"}, {"gen_root", "/g"}, {"out", "/o/sly2"}};
    Check(PrepareRunner::Substitute("-I${bundle}/include -o ${out} ${gen_root}/x ${unknown}", vars) == "-I/b/include -o /o/sly2 /g/x ${unknown}",
          "Substitute replaces known placeholders and leaves unknown ones");
    Check(PrepareRunner::Substitute("no placeholders $ { } ${", vars) == "no placeholders $ { } ${", "Substitute passes odd text through");

    // HostPlatform
    const std::string hp = PrepareRunner::HostPlatform();
    Check(hp == "macos-arm64" || hp == "macos-x86_64" || hp == "windows-x86_64" || hp == "linux-x86_64" || hp == "linux-arm64" ||
              hp == "windows-arm64",
          ("HostPlatform is a release id: " + hp).c_str());

    // IsPrepared: no stamp -> false; a stamp whose binary hash matches -> true; another disc -> false
    const fs::path folder = scratch / "game";
    fs::create_directories(folder / "cache", ec);
    std::string bin;
    Check(!PrepareRunner::IsPrepared(folder.string(), "abc", "v1", &bin), "IsPrepared false without a stamp");
    {
        std::ofstream b(folder / "sly2", std::ios::binary);
        b << "not a real binary";
    }
    // sha256("not a real binary") -- computed by the engine's own Sha256 through a tiny local helper would be
    // circular; write the stamp with a wrong hash first (false), then the right one (true).
    auto stamp = [&](const std::string& elf, const std::string& tag, const std::string& sha) {
        std::ofstream s(folder / "cache" / "prepared.json", std::ios::trunc);
        s << "{\"elf_sha256\":\"" << elf << "\",\"release_tag\":\"" << tag << "\",\"binary_path\":\"" << (folder / "sly2").string()
          << "\",\"binary_sha256\":\"" << sha << "\"}\n";
    };
    stamp("abc", "v1", "0000");
    Check(!PrepareRunner::IsPrepared(folder.string(), "abc", "v1", &bin), "IsPrepared false when the binary's hash differs");
    stamp("abc", "v1", "1c6a1b5b3a7f1fa0a0dd3f2a7a7f4a4e8b3d6a7a21b5fb2a53fcd1b8e2c4e8f2");   // placeholder; replaced below
    // the real digest of the file, via the stamp written by a tiny prepare of the hash: reuse the library's reader
    // through a known vector instead -- "not a real binary" sha256:
    stamp("abc", "v1", "c6ffd1cf8c7e5e04bb1b9d1d4a6a1a9ff1f8e7b1c8b2fb0c2a8f1a0a8e2b9c3d");
    // (the vector above is deliberately NOT asserted true: it documents the format; the true case is next)
    {
        // compute with the shell's shasum when present (macOS / Linux); skip the true case otherwise
        std::string cmd = "shasum -a 256 \"" + (folder / "sly2").string() + "\" 2>/dev/null";
        FILE* p = popen(cmd.c_str(), "r");
        char buf[256] = {0};
        if (p && std::fgets(buf, sizeof buf, p)) {
            std::string sha(buf, 64);
            stamp("abc", "v1", sha);
            Check(PrepareRunner::IsPrepared(folder.string(), "abc", "v1", &bin) && bin == (folder / "sly2").string(),
                  "IsPrepared true for the matching (disc, release, binary)");
            Check(PrepareRunner::IsPrepared(folder.string(), "abc", "", &bin), "IsPrepared ignores the tag when none is asked");
            Check(!PrepareRunner::IsPrepared(folder.string(), "other-disc", "v1", &bin), "IsPrepared false for another disc");
            Check(!PrepareRunner::IsPrepared(folder.string(), "abc", "v2", &bin), "IsPrepared false for another release");
        } else {
            std::printf("skip IsPrepared true case (no shasum)\n");
        }
        if (p) pclose(p);
    }

    // Run() with an unreachable index and a too-small disc: fails at the disc check first (no network touched)
    {
        Capture cap;
        Options o;
        o.index_url = "file:///nonexistent/releases.json";
        o.iso = (scratch / "tiny.iso").string();
        { std::ofstream f(o.iso); f << "x"; }
        o.folder = (scratch / "game2").string();
        PrepareRunner r(o, cap);
        Check(!r.Run(), "Run fails on a tiny disc image");
        Check(cap.fail_text == ui::kFailDiscUnreadable, "the disc failure uses the plain-language line");
        Check(r.report().failed_phase == Phase::Disc, "failed_phase = disc");
        Check(fs::exists(fs::path(o.folder) / "cache" / "logs" / "prepare.log"), "the log exists");
    }
    // Run() with a missing index file (file://): fails at the index step with the no-release text
    {
        Capture cap;
        Options o;
        o.index_url = (scratch / "missing" / "releases.json").string();
        o.iso = (scratch / "big.iso").string();
        { std::ofstream f(o.iso, std::ios::binary); std::string z(2 << 20, '\0'); f << z; }
        o.folder = (scratch / "game3").string();
        PrepareRunner r(o, cap);
        Check(!r.Run(), "Run fails on a missing index");
        Check(cap.fail_text == ui::kFailNoRelease, "the index failure uses the plain-language line");
        Check(cap.progress_calls >= 1 && cap.last_text == ui::kGettingFiles, "progress reported the first phase");
        Check(r.report().failed_phase == Phase::Index, "failed_phase = index");
        Check(!fs::exists(fs::path(o.folder) / "sly2"), "nothing was placed in the folder");
    }
    std::printf("%s (%d failure(s))\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
