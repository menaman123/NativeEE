// tools/prepare/main_prepare.cpp -- nativeee-prepare: the headless "Preparing your game" (ps2r_prepare).
//
//   nativeee-prepare --index <url|path> --iso <disc image> --folder <game folder>
//                    [--release <tag>] [--platform <id>] [--jobs N] [--force]
//                    [--compiler-dir D] [--sysroot S] [--curl P] [--tar P] [--json <report.json>] [--quiet]
//
// stderr: one progress line per update ("[ 42%] Preparing your game (12 of 38)  about 1 min left") and the
// failure text; stdout: the binary path on success. --json writes the Report (phase seconds, bytes
// downloaded, cache hit, build id ...) for the judge. Exit 0 ready, 1 failed, 2 usage.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include "tools/prepare/prepare.h"
#include "tools/recompiler_v2/include/json.hpp"

namespace {

std::string EtaText(double s) {
    if (s <= 0) return "";
    if (s < 60) return "less than a minute left";
    const int m = static_cast<int>((s + 30) / 60);
    return "about " + std::to_string(m) + (m == 1 ? " minute left" : " minutes left");
}

class CliListener : public ps2r::prepare::Listener {
public:
    explicit CliListener(bool quiet) : quiet_(quiet) {}
    void progress(double fraction, double eta_seconds, const std::string& phase_text) override {
        if (quiet_) return;
        const int pct = static_cast<int>(fraction * 100 + 0.5);
        std::string line = phase_text + "  " + EtaText(eta_seconds);
        if (pct == last_pct_ && line == last_line_) return;
        last_pct_ = pct;
        last_line_ = line;
        std::fprintf(stderr, "[%3d%%] %s\n", pct, line.c_str());
    }
    void failed(const std::string& text, const std::string& log_path) override {
        std::fprintf(stderr, "\n%s\n(details: %s)\n", text.c_str(), log_path.c_str());
    }
    void finished(const std::string& binary_path) override { std::printf("%s\n", binary_path.c_str()); }
    void log_line(const std::string& line) override {
        if (verbose_) std::fprintf(stderr, "    %s\n", line.c_str());
    }
    bool verbose_ = false;

private:
    bool quiet_;
    int last_pct_ = -1;
    std::string last_line_;
};

int Usage(const char* msg) {
    if (msg) std::fprintf(stderr, "nativeee-prepare: %s\n", msg);
    std::fprintf(stderr,
                 "usage: nativeee-prepare --index <url|path> --iso <disc image> --folder <game folder>\n"
                 "                        [--release <tag>] [--platform <id>] [--jobs N] [--force]\n"
                 "                        [--compiler-dir D] [--sysroot S] [--curl P] [--tar P] [--json <report.json>] [--quiet] [--verbose]\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    ps2r::prepare::Options o;
    std::string json_out;
    bool quiet = false, verbose = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* into) { if (i + 1 >= argc) return false; *into = argv[++i]; return true; };
        if (a == "--index") { if (!next(&o.index_url)) return Usage("--index needs a value"); }
        else if (a == "--iso") { if (!next(&o.iso)) return Usage("--iso needs a value"); }
        else if (a == "--folder") { if (!next(&o.folder)) return Usage("--folder needs a value"); }
        else if (a == "--release") { if (!next(&o.release_tag)) return Usage("--release needs a value"); }
        else if (a == "--platform") { if (!next(&o.platform)) return Usage("--platform needs a value"); }
        else if (a == "--compiler-dir") { if (!next(&o.compiler_dir)) return Usage("--compiler-dir needs a value"); }
        else if (a == "--sysroot") { if (!next(&o.sysroot)) return Usage("--sysroot needs a value"); }
        else if (a == "--curl") { if (!next(&o.curl)) return Usage("--curl needs a value"); }
        else if (a == "--tar") { if (!next(&o.tar)) return Usage("--tar needs a value"); }
        else if (a == "--json") { if (!next(&json_out)) return Usage("--json needs a value"); }
        else if (a == "--jobs") { std::string v; if (!next(&v)) return Usage("--jobs needs a value"); o.jobs = std::atoi(v.c_str()); }
        else if (a == "--force") o.force = true;
        else if (a == "--drop-objects") o.keep_objects = false;
        else if (a == "--quiet") quiet = true;
        else if (a == "--verbose") verbose = true;
        else if (a == "--help" || a == "-h") return Usage(nullptr);
        else return Usage(("unknown argument " + a).c_str());
    }
    if (o.index_url.empty() || o.iso.empty() || o.folder.empty()) return Usage("--index, --iso and --folder are required");
    CliListener ui(quiet);
    ui.verbose_ = verbose;
    ps2r::prepare::PrepareRunner runner(o, ui);
    const bool ok = runner.Run();
    if (!json_out.empty()) {
        const auto& r = runner.report();
        nlohmann::json j = {{"ok", ok}, {"release_tag", r.release_tag}, {"platform", r.platform}, {"bundle_dir", r.bundle_dir},
                            {"compiler_dir", r.compiler_dir}, {"compiler_driver", r.compiler_driver}, {"sysroot", r.sysroot},
                            {"binary_path", r.binary_path}, {"ini_path", r.ini_path}, {"log_path", r.log_path}, {"work_dir", r.work_dir},
                            {"elf_sha256", r.elf_sha256}, {"build_id", r.build_id}, {"vu1_aot_abi_key", r.abi_key},
                            {"bytes_downloaded", r.bytes_downloaded}, {"bytes_total", r.bytes_total}, {"cache_hit", r.cache_hit},
                            {"manifest_ok", r.manifest_ok}, {"phase_seconds", r.phase_seconds}, {"total_seconds", r.total_seconds},
                            {"failed_phase", ps2r::prepare::PhaseName(r.failed_phase)}, {"failure", r.failure}};
        std::ofstream out(json_out, std::ios::trunc);
        out << j.dump(2) << "\n";
    }
    return ok ? 0 : 1;
}
