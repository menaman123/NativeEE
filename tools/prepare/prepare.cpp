// tools/prepare/prepare.cpp -- ps2r::prepare::PrepareRunner (see prepare.h).
#include "tools/prepare/prepare.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#include "engine/core/hash/sha256.h"
#include "engine/platform/host/platform.h"
#include "tools/prepare/ui_strings.h"
#include "tools/recompiler_v2/include/json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ps2r::prepare {

const char* PhaseName(Phase p) {
    switch (p) {
        case Phase::Index: return "index";
        case Phase::Download: return "download";
        case Phase::Unpack: return "unpack";
        case Phase::Disc: return "disc";
        case Phase::Generate: return "generate";
        case Phase::Compile: return "compile";
        case Phase::Link: return "link";
        case Phase::Write: return "write";
        case Phase::Done: return "done";
        case Phase::Failed: return "failed";
    }
    return "?";
}

namespace {

// --- small helpers -------------------------------------------------------------------------
double NowS() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool ReadFile(const fs::path& p, std::string* out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

bool WriteFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

std::string Sha256File(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return "";
    ps2r::hash::Sha256 h;
    std::vector<char> buf(1 << 20);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = in.gcount();
        if (n > 0) h.Update(buf.data(), static_cast<size_t>(n));
    }
    uint8_t d[32];
    h.Finish(d);
    return ps2r::hash::Sha256::Hex(d);
}

uint64_t FileSize(const fs::path& p) {
    std::error_code ec;
    const auto n = fs::file_size(p, ec);
    return ec ? 0 : static_cast<uint64_t>(n);
}

bool StartsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
bool EndsWith(const std::string& s, const char* p) {
    const size_t n = std::strlen(p);
    return s.size() >= n && s.compare(s.size() - n, n, p) == 0;
}

std::string JoinArgv(const std::vector<std::string>& argv) {
    std::string out;
    for (const std::string& a : argv) {
        if (!out.empty()) out += ' ';
        if (a.find(' ') != std::string::npos) out += '"' + a + '"';
        else out += a;
    }
    return out;
}

// A child process to completion (the platform's spawn; nothing of the game runs through it).
// `cwd` ("" = unchanged) is applied by chdir around the spawn -- only the serial phases use it.
int RunProcess(const std::vector<std::string>& argv, bool search_path, const std::string& cwd, std::string* output) {
    std::vector<std::string> copy = argv;
    std::vector<char*> cargv;
    for (std::string& a : copy) cargv.push_back(a.data());
    cargv.push_back(nullptr);
    std::error_code ec;
    fs::path old;
    if (!cwd.empty()) {
        old = fs::current_path(ec);
        fs::current_path(cwd, ec);
        if (ec) {
            if (output) *output = "cannot enter " + cwd + ": " + ec.message();
            return -1;
        }
    }
    const int rc = ps2r::host::ProcessRun(copy[0].c_str(), cargv.data(), search_path, output);
    if (!cwd.empty()) fs::current_path(old, ec);
    return rc;
}

#if defined(_WIN32)
constexpr const char* kExe = ".exe";
#else
constexpr const char* kExe = "";
#endif

// The ETA model: reference seconds per phase on the measurement machine (REPORT.md section 1, j4),
// scaled live by the observed pace. The compile phase is weighted per TU (the common TU is the
// longest single job; a shard is ~1/8 of it; an IOP module or the hle TU ~1/40).
constexpr double kRefUnpackS = 8, kRefDiscS = 1, kRefGenerateS = 4, kRefLinkS = 1, kRefWriteS = 1;
constexpr double kWeightCommon = 40, kWeightShard = 5.5, kWeightSmall = 1.0;

}  // namespace

// --- the runner -------------------------------------------------------------------------------
struct PrepareRunner::Impl {
    PrepareRunner& self;
    Options& opt;
    Listener& ui;
    Report& rep;
    std::mutex log_mu;
    std::ofstream log;
    fs::path folder, cache, downloads, work, gen_root, obj_dir;
    json bundle;
    std::map<std::string, std::string> vars;   // the placeholders
    std::string title_id, binary_name;
    double t0 = 0;
    // progress bookkeeping
    double total_weight = 0, done_weight = 0;   // in reference seconds
    double phase_started = 0;
    std::string phase_text;
    uint64_t dl_total = 0, dl_done = 0;

    explicit Impl(PrepareRunner& s, Options& o, Listener& l, Report& r) : self(s), opt(o), ui(l), rep(r) {}

    // --- logging / progress ------------------------------------------------------------------
    void Log(const std::string& line) {
        std::lock_guard<std::mutex> lk(log_mu);
        char ts[32];
        std::snprintf(ts, sizeof ts, "[%8.2f] ", NowS() - t0);
        if (log.is_open()) { log << ts << line << '\n'; log.flush(); }
        ui.log_line(line);
    }
    void Progress(double fraction, double eta, const std::string& text) {
        fraction = std::max(0.0, std::min(1.0, fraction));
        phase_text = text;
        ui.progress(fraction, eta, text);
    }
    // fraction over the whole run: the download by bytes first, then the reference-second weights
    void Tick(double extra_done_weight, double eta_hint = -1) {
        const double dl_frac = dl_total ? static_cast<double>(dl_done) / static_cast<double>(dl_total) : 1.0;
        const double dl_part = dl_total ? 0.35 : 0.0;   // the download is at most 35% of the bar when there is one
        const double work_frac = total_weight > 0 ? (done_weight + extra_done_weight) / total_weight : 0.0;
        // the bar reaches 100% only at "ready": the link and the folder writes are still ahead of the compile
        Progress(std::min(0.97, dl_part * dl_frac + (1.0 - dl_part) * work_frac), eta_hint, phase_text);
    }
    // A tool's output into the log: whole when short, else its head and tail (the EE recompiler prints a
    // line per block -- 20k lines -- that would bury the one that matters).
    void LogOutput(const std::string& out) {
        if (out.empty()) return;
        std::vector<std::string> lines;
        std::istringstream ss(out);
        for (std::string l; std::getline(ss, l);) lines.push_back(l);
        if (lines.size() <= 120) { Log(out); return; }
        std::string text;
        for (size_t i = 0; i < 40; ++i) text += lines[i] + "\n";
        text += "... (" + std::to_string(lines.size() - 100) + " lines not shown) ...\n";
        for (size_t i = lines.size() - 60; i < lines.size(); ++i) text += lines[i] + "\n";
        Log(text);
    }
    bool Cancelled() { return self.cancelled(); }

    // --- the placeholders ---------------------------------------------------------------------
    std::string Sub(const std::string& s) const { return PrepareRunner::Substitute(s, vars); }
    std::vector<std::string> Sub(const json& argv) const {
        std::vector<std::string> out;
        for (const auto& a : argv) out.push_back(Sub(a.get<std::string>()));
        return out;
    }

    // --- phase 1: the release index ------------------------------------------------------------
    static bool IsUrl(const std::string& s) { return StartsWith(s, "http://") || StartsWith(s, "https://") || StartsWith(s, "file://"); }
    static std::string FileUrlPath(const std::string& url) {
        std::string p = url.substr(7);   // file://
        if (StartsWith(p, "localhost/")) p = p.substr(9);
        return p;
    }
    bool Fetch(const std::string& url, const fs::path& dst, bool resume, std::string* why) {
        std::error_code ec;
        fs::create_directories(dst.parent_path(), ec);
        if (!IsUrl(url) || StartsWith(url, "file://")) {
            const fs::path src = IsUrl(url) ? fs::path(FileUrlPath(url)) : fs::path(url);
            if (!fs::exists(src)) { *why = "no such file: " + src.string(); return false; }
            // a chunked copy with progress (the offline fixture's "download")
            std::ifstream in(src, std::ios::binary);
            std::ofstream out(dst, std::ios::binary | std::ios::trunc);
            if (!in || !out) { *why = "cannot copy " + src.string(); return false; }
            std::vector<char> buf(4 << 20);
            while (in) {
                in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
                const std::streamsize n = in.gcount();
                if (n > 0) { out.write(buf.data(), n); dl_done += static_cast<uint64_t>(n); rep.bytes_downloaded += static_cast<uint64_t>(n); Tick(0, DownloadEta()); }
                if (Cancelled()) { *why = "cancelled"; return false; }
            }
            return static_cast<bool>(out);
        }
        std::vector<std::string> argv = {opt.curl.empty() ? std::string("curl") : opt.curl, "-L", "--fail", "--silent", "--show-error",
                                         "--retry", "3", "--retry-all-errors", "--connect-timeout", "20"};
        if (resume) { argv.push_back("-C"); argv.push_back("-"); }
        if (url.find("api.github.com") != std::string::npos) { argv.push_back("-H"); argv.push_back("Accept: application/vnd.github+json"); }
        argv.push_back("-o");
        argv.push_back(dst.string());
        argv.push_back(url);
        Log("$ " + JoinArgv(argv));
        const uint64_t before = FileSize(dst);
        std::string output;
        std::atomic<bool> done{false};
        int rc = 0;
        std::thread t([&] { rc = RunProcess(argv, opt.curl.empty(), "", &output); done = true; });
        uint64_t last = before;
        while (!done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            const uint64_t now = FileSize(dst);
            if (now != last) { dl_done += now - last; rep.bytes_downloaded += now - last; last = now; Tick(0, DownloadEta()); }
        }
        t.join();
        if (!output.empty()) Log(output);
        if (rc != 0) {
            *why = "curl exit " + std::to_string(rc) + (output.empty() ? "" : ": " + output);
            return false;
        }
        return true;
    }
    double dl_t0 = 0;
    double DownloadEta() {
        if (!dl_total || dl_done == 0) return -1;
        const double el = NowS() - dl_t0;
        if (el < 1) return -1;
        const double rate = static_cast<double>(dl_done) / el;
        return static_cast<double>(dl_total - std::min(dl_total, dl_done)) / std::max(1.0, rate) + total_weight;
    }

    struct Asset { std::string name, url, sha256; uint64_t size = 0; };
    bool ResolveRelease(std::string* why, Asset* bundle_asset, Asset* compiler_asset) {
        const fs::path index_path = downloads / "releases.json";
        if (!Fetch(opt.index_url, index_path, false, why)) return false;
        std::string text;
        if (!ReadFile(index_path, &text)) { *why = "cannot read the release index"; return false; }
        json idx;
        try { idx = json::parse(text); } catch (const std::exception& e) { *why = std::string("release index is not JSON: ") + e.what(); return false; }
        json releases = idx.is_array() ? idx : json::array({idx});
        const json* chosen = nullptr;
        for (const auto& r : releases) {
            if (!r.is_object()) continue;
            if (r.value("draft", false)) continue;
            const std::string tag = r.value("tag_name", "");
            if (!opt.release_tag.empty()) { if (tag == opt.release_tag) { chosen = &r; break; } continue; }
            if (r.value("prerelease", false)) continue;
            chosen = &r;   // the index is newest-first (GitHub's order)
            break;
        }
        if (!chosen) { *why = opt.release_tag.empty() ? "no release in the index" : "release " + opt.release_tag + " not in the index"; return false; }
        rep.release_tag = chosen->value("tag_name", "");
        const std::string want_bundle = "engine-bundle-" + rep.platform + ".tar.xz";
        const std::string want_compiler = "compiler-" + rep.platform + ".tar.xz";
        std::string sums_url;
        for (const auto& a : (*chosen)["assets"]) {
            const std::string name = a.value("name", "");
            Asset as{name, a.value("browser_download_url", a.value("url", "")), "", a.value("size", uint64_t{0})};
            const std::string digest = a.value("digest", "");
            if (StartsWith(digest, "sha256:")) as.sha256 = digest.substr(7);
            if (name == want_bundle) *bundle_asset = as;
            else if (name == want_compiler) *compiler_asset = as;
            else if (name == "SHA256SUMS") sums_url = as.url;
        }
        if (bundle_asset->url.empty()) { *why = "release " + rep.release_tag + " has no " + want_bundle; return false; }
        if (compiler_asset->url.empty() && opt.compiler_dir.empty()) { *why = "release " + rep.release_tag + " has no " + want_compiler; return false; }
        if ((bundle_asset->sha256.empty() || (compiler_asset->sha256.empty() && !compiler_asset->url.empty())) && !sums_url.empty()) {
            const fs::path sums = downloads / ("SHA256SUMS-" + rep.release_tag);
            if (Fetch(sums_url, sums, false, why) && ReadFile(sums, &text)) {
                std::istringstream ss(text);
                std::string line;
                while (std::getline(ss, line)) {
                    std::istringstream ls(line);
                    std::string hash, name;
                    ls >> hash >> name;
                    if (!name.empty() && name[0] == '*') name = name.substr(1);
                    if (name == want_bundle) bundle_asset->sha256 = hash;
                    if (name == want_compiler) compiler_asset->sha256 = hash;
                }
            }
        }
        if (bundle_asset->sha256.empty()) { *why = "no sha256 for " + want_bundle + " (asset digest or SHA256SUMS)"; return false; }
        if (!compiler_asset->url.empty() && compiler_asset->sha256.empty()) { *why = "no sha256 for " + want_compiler; return false; }
        Log("release " + rep.release_tag + ": " + want_bundle + " (" + std::to_string(bundle_asset->size) + " B) " +
            (compiler_asset->url.empty() ? std::string("compiler: local override") : want_compiler + " (" + std::to_string(compiler_asset->size) + " B)"));
        return true;
    }

    // --- phase 2: download + verify + unpack ------------------------------------------------------
    bool Obtain(const Asset& a, fs::path* archive, std::string* why) {
        *archive = downloads / a.name;
        if (fs::exists(*archive) && Sha256File(*archive) == a.sha256) {
            Log(a.name + ": cached (sha256 ok)");
            dl_done += a.size; Tick(0);
            return true;
        }
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (Cancelled()) { *why = "cancelled"; return false; }
            if (attempt == 1) { std::error_code ec; fs::remove(*archive, ec); }
            if (!Fetch(a.url, *archive, attempt == 0, why)) {
                if (attempt == 1) return false;
                continue;
            }
            const std::string got = Sha256File(*archive);
            if (got == a.sha256) return true;
            Log(a.name + ": sha256 " + got + " != " + a.sha256 + " (attempt " + std::to_string(attempt + 1) + ")");
            *why = a.name + ": sha256 mismatch";
        }
        return false;
    }
    bool Unpack(const fs::path& archive, const fs::path& dest, const std::string& sha, std::string* why) {
        const fs::path stamp = dest / ".unpacked.sha256";
        std::string have;
        if (ReadFile(stamp, &have) && have == sha) { Log(dest.filename().string() + ": already unpacked"); return true; }
        std::error_code ec;
        fs::remove_all(dest, ec);
        fs::create_directories(dest, ec);
        std::vector<std::string> argv = {opt.tar.empty() ? std::string("tar") : opt.tar, "-xf", archive.string(), "-C", dest.string()};
        Log("$ " + JoinArgv(argv));
        std::string output;
        const int rc = RunProcess(argv, opt.tar.empty(), "", &output);
        if (rc != 0) { *why = "tar exit " + std::to_string(rc) + ": " + output; return false; }
        WriteFile(stamp, sha);
        return true;
    }
    static fs::path FindDirWith(const fs::path& root, const fs::path& rel, int depth = 3) {
        if (fs::exists(root / rel)) return root;
        if (depth == 0) return {};
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(root, ec)) {
            if (!e.is_directory()) continue;
            const fs::path r = FindDirWith(e.path(), rel, depth - 1);
            if (!r.empty()) return r;
        }
        return {};
    }

    // --- the toolchain -------------------------------------------------------------------------------
    bool ResolveToolchain(std::string* why) {
        const fs::path croot = rep.compiler_dir;
        const std::string drv = std::string("clang++") + kExe;
        fs::path driver = croot / "bin" / drv;
        if (!fs::exists(driver)) {
            const fs::path alt = FindDirWith(croot, fs::path("bin") / drv);
            if (alt.empty()) { *why = "no bin/" + drv + " under " + croot.string(); return false; }
            rep.compiler_dir = alt.string();
            driver = alt / "bin" / drv;
        }
        rep.compiler_driver = driver.string();
        vars["cxx"] = rep.compiler_driver;
        const fs::path bin = fs::path(rep.compiler_dir) / "bin";
        auto pick = [&](const char* a, const char* b) {
            if (fs::exists(bin / (std::string(a) + kExe))) return (bin / (std::string(a) + kExe)).string();
            return std::string(b);
        };
        vars["ar"] = pick("llvm-ar", "ar");
        vars["ranlib"] = pick("llvm-ranlib", "ranlib");
        vars["lib"] = pick("llvm-lib", "lib");
        // the sysroot (D-P3): an override, the bundle's, the compiler bundle's, else the system's
        std::string sysroot = opt.sysroot;
        if (sysroot.empty() && fs::exists(fs::path(rep.bundle_dir) / "sysroot")) sysroot = (fs::path(rep.bundle_dir) / "sysroot").string();
        if (sysroot.empty() && fs::exists(fs::path(rep.compiler_dir) / "sysroot")) sysroot = (fs::path(rep.compiler_dir) / "sysroot").string();
#if defined(__APPLE__)
        if (sysroot.empty()) {
            std::string out;
            if (RunProcess({"xcrun", "--show-sdk-path"}, true, "", &out) == 0) {
                while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
                if (fs::exists(out)) sysroot = out;
            }
            if (sysroot.empty() && fs::exists("/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"))
                sysroot = "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk";
            if (sysroot.empty()) { *why = "no macOS SDK (no bundle sysroot/, no Command Line Tools)"; return false; }
        }
#endif
        rep.sysroot = sysroot;
        vars["sysroot"] = sysroot;
        // the compiler answers --version (a broken unpack shows here, not in the first shard)
        std::string ver;
        if (RunProcess({rep.compiler_driver, "--version"}, false, "", &ver) != 0) { *why = rep.compiler_driver + " does not run: " + ver; return false; }
        Log("compiler: " + ver.substr(0, ver.find('\n')) + " sysroot: " + sysroot);
        return true;
    }
    // -isysroot for a portable compiler that does not know the SDK; -fuse-ld=lld when the bundle has lld
    std::vector<std::string> Toolchainise(std::vector<std::string> argv, bool link) const {
        bool has_sysroot = false;
        for (const std::string& a : argv) if (a == "-isysroot" || StartsWith(a, "--sysroot")) has_sysroot = true;
#if defined(__APPLE__) || defined(__linux__)
        if (!has_sysroot && !vars.at("sysroot").empty() && argv.size() > 1) {
            argv.insert(argv.begin() + 1, vars.at("sysroot"));
            argv.insert(argv.begin() + 1, "-isysroot");
        }
#endif
        if (link) {
            const fs::path bin = fs::path(rep.compiler_dir) / "bin";
#if defined(__APPLE__)
            const bool lld = fs::exists(bin / "ld64.lld");
#elif defined(__linux__)
            const bool lld = fs::exists(bin / "ld.lld");
#else
            const bool lld = false;
#endif
            bool has_fuse = false;
            for (const std::string& a : argv) if (StartsWith(a, "-fuse-ld")) has_fuse = true;
            if (lld && !has_fuse) argv.insert(argv.begin() + 1, "-fuse-ld=lld");
        }
        return argv;
    }

    // --- phases 3-4: the disc + the generation -------------------------------------------------------
    bool RunStep(const json& step, std::string* output) {
        std::string cwd;
        for (const auto& s : step["steps"]) {
            if (s.contains("cd")) { cwd = Sub(s["cd"].get<std::string>()); std::error_code ec; fs::create_directories(cwd, ec); continue; }
            std::vector<std::string> argv = Sub(s["argv"]);
            for (size_t i = 1; i < argv.size(); ++i)   // every output path's directory exists
                if (StartsWith(argv[i], vars.at("gen_root").c_str())) { std::error_code ec; fs::create_directories(fs::path(argv[i]).parent_path(), ec); }
            Log("$ " + JoinArgv(argv));
            std::string out;
            const int rc = RunProcess(argv, false, cwd, &out);
            LogOutput(out);
            if (rc != 0) { *output = "exit " + std::to_string(rc) + ": " + out.substr(0, 4000); return false; }
        }
        return true;
    }
    bool CheckManifest(const fs::path& manifest, const fs::path& dir, std::string* why) {
        std::string text;
        if (!ReadFile(manifest, &text)) { *why = "cannot read " + manifest.string(); return false; }
        std::istringstream ss(text);
        std::string line;
        int rows = 0;
        while (std::getline(ss, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ls(line);
            std::string hash, name;
            ls >> hash >> name;
            if (!name.empty() && name[0] == '*') name = name.substr(1);
            if (hash.size() != 64 || name.empty()) continue;
            ++rows;
            const std::string got = Sha256File(dir / name);
            if (got != hash) { *why = name + ": generated sha256 " + got + " != pinned " + hash; return false; }
        }
        if (!rows) { *why = manifest.string() + " has no rows"; return false; }
        Log(manifest.filename().string() + ": " + std::to_string(rows) + " rows match (" + dir.string() + ")");
        return true;
    }

    // --- phase 5: compile ------------------------------------------------------------------------------
    struct Job { std::vector<std::string> argv; std::string object; std::string label; double weight; };
    bool Compile(std::vector<Job> jobs, int nthreads, std::string* why) {
        std::sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) { return a.weight > b.weight; });
        for (const Job& j : jobs) { std::error_code ec; fs::create_directories(fs::path(j.object).parent_path(), ec); }
        std::mutex mu;
        size_t next = 0, done = 0;
        bool failed = false;
        std::string fail_why;
        double weight_done = 0;
        const double started = NowS();
        const size_t total = jobs.size();
        auto worker = [&] {
            for (;;) {
                Job* j = nullptr;
                {
                    std::lock_guard<std::mutex> lk(mu);
                    if (failed || Cancelled() || next >= jobs.size()) return;
                    j = &jobs[next++];
                }
                std::string out;
                const double t = NowS();
                const int rc = RunProcess(j->argv, false, "", &out);
                std::lock_guard<std::mutex> lk(mu);
                if (rc != 0) {
                    if (!failed) { failed = true; fail_why = j->label + ": exit " + std::to_string(rc) + "\n" + JoinArgv(j->argv) + "\n" + out; }
                    std::error_code ec; fs::remove(j->object, ec);
                    return;
                }
                ++done;
                weight_done += j->weight;
                char buf[160];
                std::snprintf(buf, sizeof buf, "%s: %.1f s", j->label.c_str(), NowS() - t);
                Log(buf);
                // ETA: the observed pace over the weights done so far, applied to what is left
                const double el = NowS() - started;
                const double pace = weight_done > 0 ? el / weight_done : 0;
                double weight_left = 0;
                for (size_t i = next; i < jobs.size(); ++i) weight_left += jobs[i].weight;
                const double eta = pace > 0 ? weight_left * pace + kRefLinkS + kRefWriteS : -1;
                std::string text = ui::kPreparingCounted;
                text.replace(text.find("%1"), 2, std::to_string(done));
                text.replace(text.find("%2"), 2, std::to_string(total));
                phase_text = text;
                Tick(weight_done, eta);
            }
        };
        std::vector<std::thread> pool;
        for (int i = 0; i < std::max(1, nthreads); ++i) pool.emplace_back(worker);
        for (auto& t : pool) t.join();
        if (Cancelled()) { *why = "cancelled"; return false; }
        if (failed) { *why = fail_why; return false; }
        done_weight += weight_done;
        return true;
    }
};

// --- PrepareRunner ------------------------------------------------------------------------------
PrepareRunner::PrepareRunner(Options options, Listener& listener)
    : impl_(new Impl(*this, options_, listener, report_)), options_(std::move(options)), listener_(listener) {}
PrepareRunner::~PrepareRunner() = default;
void PrepareRunner::Cancel() { cancel_ = true; }

std::string PrepareRunner::HostPlatform() {
#if defined(__APPLE__)
    const char* os = "macos";
#elif defined(_WIN32)
    const char* os = "windows";
#else
    const char* os = "linux";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    const char* arch = "arm64";
#else
    const char* arch = "x86_64";
#endif
    return std::string(os) + "-" + arch;
}

std::string PrepareRunner::Substitute(const std::string& text, const std::map<std::string, std::string>& vars) {
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '$' && i + 1 < text.size() && text[i + 1] == '{') {
            const size_t close = text.find('}', i + 2);
            if (close != std::string::npos) {
                const std::string key = text.substr(i + 2, close - i - 2);
                auto it = vars.find(key);
                if (it != vars.end()) { out += it->second; i = close + 1; continue; }
            }
        }
        out.push_back(text[i++]);
    }
    return out;
}

bool PrepareRunner::IsPrepared(const std::string& folder, const std::string& elf_sha256, const std::string& release_tag,
                               std::string* binary_path) {
    std::string text;
    if (!ReadFile(fs::path(folder) / "cache" / "prepared.json", &text)) return false;
    try {
        const json j = json::parse(text);
        if (j.value("elf_sha256", "") != elf_sha256) return false;
        if (!release_tag.empty() && j.value("release_tag", "") != release_tag) return false;
        const std::string bin = j.value("binary_path", "");
        if (bin.empty() || !fs::exists(bin)) return false;
        if (j.value("binary_sha256", "") != Sha256File(bin)) return false;
        if (binary_path) *binary_path = bin;
        return true;
    } catch (...) {
        return false;
    }
}

bool PrepareRunner::Run() {
    Impl& I = *impl_;
    I.t0 = NowS();
    report_ = Report{};
    report_.platform = options_.platform.empty() ? HostPlatform() : options_.platform;
    I.folder = fs::path(options_.folder);
    I.cache = I.folder / "cache";
    I.downloads = I.cache / "downloads";
    std::error_code ec;
    fs::create_directories(I.downloads, ec);
    fs::create_directories(I.cache / "logs", ec);
    report_.log_path = (I.cache / "logs" / "prepare.log").string();
    I.log.open(report_.log_path, std::ios::app);
    I.Log("== prepare: index=" + options_.index_url + " iso=" + options_.iso + " folder=" + options_.folder + " platform=" + report_.platform);
    auto phase = [&](Phase p) { I.phase_started = NowS(); return p; };
    auto end_phase = [&](Phase p) { report_.phase_seconds[PhaseName(p)] += NowS() - I.phase_started; };
    std::string why;

    // the disc image itself
    if (!fs::exists(options_.iso) || FileSize(options_.iso) < (1 << 20)) {
        I.Fail(Phase::Disc, ui::kFailDiscUnreadable, "disc image missing or too small: " + options_.iso);
        return false;
    }

    // 1. the index
    Phase p = phase(Phase::Index);
    I.Progress(0, -1, ui::kGettingFiles);
    Impl::Asset bundle_asset, compiler_asset;
    if (!I.ResolveRelease(&why, &bundle_asset, &compiler_asset)) {
        const bool offline = why.find("curl exit") != std::string::npos;
        I.Fail(p, offline ? ui::kFailNoInternet : ui::kFailNoRelease, why);
        return false;
    }
    end_phase(p);

    // 2. download + unpack
    p = phase(Phase::Download);
    I.dl_total = bundle_asset.size + compiler_asset.size;
    report_.bytes_total = I.dl_total;
    I.dl_t0 = NowS();
    // the reference weights of the rest (compile is set once BUNDLE.json is read; a provisional guess here)
    I.total_weight = kRefUnpackS + kRefDiscS + kRefGenerateS + kWeightCommon + 32 * kWeightShard + 5 * kWeightSmall + kRefLinkS + kRefWriteS;
    fs::path bundle_archive, compiler_archive;
    if (!I.Obtain(bundle_asset, &bundle_archive, &why)) { I.Fail(p, why.find("sha256") != std::string::npos ? ui::kFailBadDownload : ui::kFailNoInternet, why); return false; }
    if (options_.compiler_dir.empty() && !I.Obtain(compiler_asset, &compiler_archive, &why)) { I.Fail(p, why.find("sha256") != std::string::npos ? ui::kFailBadDownload : ui::kFailNoInternet, why); return false; }
    end_phase(p);
    p = phase(Phase::Unpack);
    I.Progress(0.35, -1, ui::kUnpacking);
    I.phase_text = ui::kUnpacking;
    const fs::path bundle_root = I.cache / "bundle" / report_.release_tag;
    if (!I.Unpack(bundle_archive, bundle_root, bundle_asset.sha256, &why)) { I.Fail(p, ui::kFailBadDownload, why); return false; }
    const fs::path bundle_dir = Impl::FindDirWith(bundle_root, "BUNDLE.json");
    if (bundle_dir.empty()) { I.Fail(p, ui::kFailBadDownload, "the engine bundle has no BUNDLE.json"); return false; }
    report_.bundle_dir = bundle_dir.string();
    if (options_.compiler_dir.empty()) {
        const fs::path croot = I.cache / "compiler";
        if (!I.Unpack(compiler_archive, croot, compiler_asset.sha256, &why)) { I.Fail(p, ui::kFailBadDownload, why); return false; }
        report_.compiler_dir = croot.string();
    } else {
        report_.compiler_dir = options_.compiler_dir;
    }
    I.done_weight += kRefUnpackS;
    end_phase(p);

    // BUNDLE.json
    std::string text;
    if (!ReadFile(bundle_dir / "BUNDLE.json", &text)) { I.Fail(p, ui::kFailBadDownload, "cannot read BUNDLE.json"); return false; }
    try { I.bundle = json::parse(text); } catch (const std::exception& e) { I.Fail(p, ui::kFailBadDownload, std::string("BUNDLE.json: ") + e.what()); return false; }
    const json& B = I.bundle;
    I.title_id = B["title"].value("id", "");
    I.binary_name = B["title"].value("binary_name", "game");
    report_.build_id = B.value("build_id", "");
    report_.abi_key = B.value("vu1_aot_abi_key", "");
    if (B.value("platform", "") != report_.platform) {
        I.Fail(p, ui::kFailNoRelease, "bundle platform " + B.value("platform", "") + " != " + report_.platform);
        return false;
    }
    I.work = I.cache / "work" / report_.release_tag;
    I.gen_root = I.work / "gen";
    I.obj_dir = I.work / "obj";
    report_.work_dir = I.work.string();
    I.vars = {{"bundle", bundle_dir.string()}, {"gen_root", I.gen_root.string()}, {"obj", I.obj_dir.string()},
              {"iso", options_.iso}, {"out", (I.folder / I.binary_name).string()},
              {"jobs", std::to_string(options_.jobs > 0 ? options_.jobs : static_cast<int>(std::thread::hardware_concurrency()))}};
    if (!I.ResolveToolchain(&why)) {
        I.Fail(Phase::Unpack, why.find("SDK") != std::string::npos ? ui::kFailNoDeveloperTools : ui::kFailPrepare, why);
        return false;
    }
    // the real compile weights now that the bundle is known
    double compile_weight = 0;
    std::vector<Impl::Job> jobs;
    for (auto it = B["compile"].begin(); it != B["compile"].end(); ++it) {
        for (const auto& row : it.value()) {
            const std::string src = row.value("source", "");
            const double w = EndsWith(src, "recompiled.cpp") ? kWeightCommon : src.find("recompiled_shard_") != std::string::npos ? kWeightShard : kWeightSmall;
            jobs.push_back({I.Toolchainise(I.Sub(row["command"]), false), I.Sub(row.value("object", "")), fs::path(src).filename().string(), w});
            compile_weight += w;
        }
    }
    for (const auto& row : B["on_machine_sources"]) {
        jobs.push_back({I.Toolchainise(I.Sub(row["command"]), false), I.Sub(row.value("object", "")), fs::path(row.value("source", "")).filename().string(), kWeightSmall});
        compile_weight += kWeightSmall;
    }
    I.total_weight = kRefUnpackS + kRefDiscS + kRefGenerateS + compile_weight + kRefLinkS + kRefWriteS;

    // 3. the disc: extract + embedded (the generation steps are grouped by kind in dependency order)
    p = phase(Phase::Disc);
    I.phase_text = ui::kCheckingDisc;
    I.Tick(0);
    fs::create_directories(I.gen_root / "inputs", ec);
    for (const char* kind : {"extract", "embedded"}) {
        for (const auto& step : B["steps"]) {
            if (step.value("kind", "") != kind) continue;
            if (I.Cancelled()) { I.Fail(p, ui::kFailCancelled, "cancelled"); return false; }
            if (!I.RunStep(step, &why)) {
                const bool wrong = why.find("exit 6") != std::string::npos;
                I.Fail(p, wrong ? ui::kFailWrongDisc : ui::kFailDiscUnreadable, why);
                return false;
            }
        }
    }
    // the ELF's sha256 = the cache key (the title's expected hash is what the extractor gated)
    report_.elf_sha256 = B["title"].value("elf_sha256", "");
    {
        const fs::path elf = I.gen_root / "inputs" / B["title"].value("elf_path", "");
        const std::string got = Sha256File(elf);
        if (!got.empty()) report_.elf_sha256 = got;
    }
    I.done_weight += kRefDiscS;
    end_phase(p);

    // a prepared binary for this (disc, release) already? (the stamp)
    std::string ready;
    if (!options_.force && IsPrepared(options_.folder, report_.elf_sha256, report_.release_tag, &ready)) {
        report_.cache_hit = true;
        report_.binary_path = ready;
        report_.ini_path = (I.folder / "nativeee.ini").string();
        report_.manifest_ok = true;
        report_.total_seconds = NowS() - I.t0;
        I.Log("prepared.json matches (elf " + report_.elf_sha256 + ", " + report_.release_tag + "): nothing to do");
        I.Progress(1.0, 0, ui::kReady);
        listener_.finished(ready);
        return true;
    }

    // 4. generate + the manifest gate
    p = phase(Phase::Generate);
    I.phase_text = ui::kPreparing;
    I.Tick(0);
    for (const char* kind : {"recompile", "iop_recompile"}) {
        for (const auto& step : B["steps"]) {
            if (step.value("kind", "") != kind) continue;
            if (I.Cancelled()) { I.Fail(p, ui::kFailCancelled, "cancelled"); return false; }
            if (!I.RunStep(step, &why)) { I.Fail(p, ui::kFailPrepare, why); return false; }
        }
    }
    if (!I.CheckManifest(I.Sub(B["title"].value("recomp_manifest", "")), I.gen_root / "recomp", &why)) { I.Fail(p, ui::kFailPrepare, why); return false; }
    for (auto it = B["title"]["iop_manifests"].begin(); it != B["title"]["iop_manifests"].end(); ++it)
        if (!I.CheckManifest(I.Sub(it.value().get<std::string>()), I.gen_root / "iop" / it.key(), &why)) { I.Fail(p, ui::kFailPrepare, why); return false; }
    report_.manifest_ok = true;
    I.done_weight += kRefGenerateS;
    end_phase(p);

    // 5. the PCH, then everything else -j
    p = phase(Phase::Compile);
    if (!B["pch"].is_null()) {
        const json& P = B["pch"];
        if (!WriteFile(I.Sub(P.value("header", "")), I.Sub(P.value("header_text", ""))) ||
            !WriteFile(I.Sub(P.value("source", "")), P.value("source_text", "/* generated */\n"))) {
            I.Fail(p, ui::kFailDiskFull, "cannot write the precompiled header sources"); return false;
        }
        std::vector<std::string> argv = I.Toolchainise(I.Sub(P["command"]), false);
        fs::create_directories(fs::path(I.Sub(P.value("object", ""))).parent_path(), ec);
        I.Log("$ " + JoinArgv(argv));
        std::string out;
        if (RunProcess(argv, false, "", &out) != 0) { I.Fail(p, ui::kFailPrepare, "pch: " + out); return false; }
    }
    const int nthreads = options_.jobs > 0 ? options_.jobs : std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    I.Log("compiling " + std::to_string(jobs.size()) + " translation units with " + std::to_string(nthreads) + " jobs");
    if (!I.Compile(std::move(jobs), nthreads, &why)) {
        I.Fail(p, I.Cancelled() ? ui::kFailCancelled : ui::kFailPrepare, why);
        return false;
    }
    end_phase(p);

    // 6. archives + link
    p = phase(Phase::Link);
    I.phase_text = ui::kAlmostDone;
    I.Tick(0, kRefLinkS + kRefWriteS);
    fs::create_directories(I.obj_dir / "archives", ec);
    for (const auto& a : B["link"]["archives_on_machine"]) {
        const std::string archive = I.Sub(a.value("archive", ""));
        std::vector<std::string> objects = I.Sub(a["objects"]);
        fs::remove(archive, ec);
        for (const auto& rule : B["link"]["archive_rule"]) {
            std::vector<std::string> argv;
            for (const auto& tok : rule) {
                const std::string t = tok.get<std::string>();
                if (t == "${objects}") argv.insert(argv.end(), objects.begin(), objects.end());
                else if (t == "${archive}") argv.push_back(archive);
                else argv.push_back(I.Sub(t));
            }
            std::string out;
            const int rc = RunProcess(argv, argv[0].find('/') == std::string::npos, "", &out);
            if (rc != 0) { I.Fail(p, ui::kFailPrepare, "archive " + archive + ": exit " + std::to_string(rc) + " " + out); return false; }
        }
        I.Log("archived " + archive + " (" + std::to_string(objects.size()) + " objects)");
    }
    const fs::path out_bin = I.folder / I.binary_name;
    const fs::path tmp_bin = I.folder / (I.binary_name + ".part");
    std::map<std::string, std::string> link_vars = I.vars;
    link_vars["out"] = tmp_bin.string();
    std::vector<std::string> link_argv;
    for (const auto& t : B["link"]["command"]) link_argv.push_back(Substitute(t.get<std::string>(), link_vars));
    link_argv = I.Toolchainise(link_argv, true);
    I.Log("$ " + JoinArgv(link_argv));
    {
        std::string out;
        const int rc = RunProcess(link_argv, false, "", &out);
        if (!out.empty()) I.Log(out);
        if (rc != 0) { fs::remove(tmp_bin, ec); I.Fail(p, ui::kFailPrepare, "link: exit " + std::to_string(rc) + " " + out); return false; }
    }
    fs::rename(tmp_bin, out_bin, ec);
    if (ec) { I.Fail(p, ui::kFailDiskFull, "cannot place " + out_bin.string() + ": " + ec.message()); return false; }
    fs::permissions(out_bin, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
    report_.binary_path = out_bin.string();
    I.done_weight += kRefLinkS;
    end_phase(p);

    // 7. the folder: title tables, prelude, the ELF, the ini, the stamp
    p = phase(Phase::Write);
    const fs::path title_src = bundle_dir / "title" / I.title_id;
    fs::remove_all(I.folder / "title", ec);
    fs::copy(title_src, I.folder / "title", fs::copy_options::recursive, ec);
    fs::remove(I.folder / "title" / "hle" / "iop_host.cpp", ec);   // an on-machine source, not a table
    fs::remove_all(I.folder / "vu1_aot_prelude", ec);
    fs::copy(bundle_dir / "vu1_aot_prelude", I.folder / "vu1_aot_prelude", fs::copy_options::recursive, ec);
    const fs::path game_dir = I.folder / "game" / B["title"].value("elf_dir", "game");
    fs::create_directories(game_dir, ec);
    fs::copy_file(I.gen_root / "inputs" / B["title"].value("elf_path", ""), game_dir / B["title"].value("elf_path", ""),
                  fs::copy_options::overwrite_existing, ec);
    fs::create_directories(I.folder / "memcards", ec);
    fs::create_directories(I.folder / "cache" / "saves", ec);
    std::ostringstream ini;
    ini << "# nativeee.ini -- written by nativeee-prepare (" << report_.release_tag << "); the game and the launcher write here too.\n"
        << "paths.user_data = " << (I.folder / "cache").string() << "\n"
        << "paths.mc_dir = " << (I.folder / "memcards").string() << "\n"
        << "paths.game_files = " << (I.folder / "game").string() << "\n"
        << "paths.iso = " << options_.iso << "\n"
        << "engine.vu1_aot_prelude = " << (I.folder / "vu1_aot_prelude").string() << "\n"
        << "engine.vu1_aot_compiler = " << report_.compiler_driver << "\n"
        << "engine.vu1_aot_sysroot = " << report_.sysroot << "\n";
    if (B["ini"].contains("engine.vu1_aot_pack")) ini << "engine.vu1_aot_pack = " << B["ini"].value("engine.vu1_aot_pack", "") << "\n";
    report_.ini_path = (I.folder / "nativeee.ini").string();
    if (!WriteFile(report_.ini_path, ini.str())) { I.Fail(p, ui::kFailDiskFull, "cannot write nativeee.ini"); return false; }
    json stamp = {{"schema", 1}, {"release_tag", report_.release_tag}, {"platform", report_.platform},
                  {"elf_sha256", report_.elf_sha256}, {"bundle_sha256", bundle_asset.sha256},
                  {"compiler_sha256", compiler_asset.sha256}, {"build_id", report_.build_id}, {"vu1_aot_abi_key", report_.abi_key},
                  {"binary_path", out_bin.string()}, {"binary_sha256", Sha256File(out_bin)}, {"compiler_driver", report_.compiler_driver},
                  {"bytes_downloaded", report_.bytes_downloaded}};
    WriteFile(I.cache / "prepared.json", stamp.dump(2) + "\n");
    if (!options_.keep_objects) fs::remove_all(I.obj_dir, ec);
    I.done_weight += kRefWriteS;
    end_phase(p);
    report_.total_seconds = NowS() - I.t0;
    I.Log("done in " + std::to_string(report_.total_seconds) + " s: " + out_bin.string());
    I.Progress(1.0, 0, ui::kReady);
    listener_.finished(out_bin.string());
    return true;
}

}  // namespace ps2r::prepare
