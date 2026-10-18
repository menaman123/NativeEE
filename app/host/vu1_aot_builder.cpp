// Vu1AotBuilder -- toolchain discovery, the staged include root, the worker
// (emit -> compile -> load -> re-verify -> publish), the native.txt rows.
#include "app/host/vu1_aot_builder.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "engine/core/log/log.h"
#include "engine/vu/tiers/vu_block_tier.h"
#include "engine/vu/vu1_aot/vu1_aot_emitter.h"
#include "engine/vu/vu1_aot/vu1_aot_verifier.h"
#include "engine/vu/vu1_case.h"
#include "vu1_aot_abi_key.h"
#include "vu1_aot_paths.h"

namespace ps2r::app {

namespace {

namespace fs = std::filesystem;

uint64_t NowNs() { return ps2r::host::MonotonicNowNs(); }

std::string Token(const std::string& s) {
    std::string tok = s.empty() ? "unknown" : s;
    for (char& ch : tok) {
        const bool ok = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '.' || ch == '_' || ch == '-';
        if (!ok) ch = '_';
    }
    return tok;
}

std::string FirstLine(const std::string& s) {
    const size_t nl = s.find('\n');
    std::string line = nl == std::string::npos ? s : s.substr(0, nl);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    return line;
}

bool WriteFile(const std::string& path, const std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    return ok;
}

// Run `argv` (argv[0] = the image) and gather its output.
int Run(const std::vector<std::string>& argv, bool search_path, std::string* output) {
    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const std::string& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);
    return ps2r::host::ProcessRun(argv[0].c_str(), cargv.data(), search_path, output);
}

}  // namespace

Vu1AotBuilder::Vu1AotBuilder(Vu1AotTier& tier, Settings settings) : tier_(tier), settings_(std::move(settings)) {
    if (settings_.workers < 1) settings_.workers = 1;
    if (settings_.workers > 2) settings_.workers = 2;
    if (settings_.opt_level != 1) settings_.opt_level = 2;
}

Vu1AotBuilder::~Vu1AotBuilder() {
    Close();
    // Withdraw before unmapping: a kick after this point would re-install
    // nothing (the tier sees null and clears its blocks). The VU must be quiet.
    for (Library& l : libraries_) {
        tier_.SetNativeExports(l.image, nullptr);
        ps2r::host::DynamicLibraryClose(l.lib);
    }
    libraries_.clear();
}

// ---- paths and the toolchain ------------------------------------------------

std::string Vu1AotBuilder::BuildDir(const std::string& root, const std::string& build_id) { return root + "/" + Token(build_id); }
std::string Vu1AotBuilder::IncludeDir(const std::string& root, const std::string& build_id) {
    return BuildDir(root, build_id) + "/include";
}
std::string Vu1AotBuilder::LibraryPath(const std::string& root, const std::string& build_id, uint64_t image_hash, int opt) {
    return BuildDir(root, build_id) + "/" + Vu1AotEmitter::Hex16(image_hash) + ".O" + std::to_string(opt) + "." +
           ps2r::host::DynamicLibraryExtension();
}
std::string Vu1AotBuilder::SourcePath(const std::string& root, const std::string& build_id, uint64_t image_hash) {
    return BuildDir(root, build_id) + "/" + Vu1AotEmitter::Hex16(image_hash) + ".cpp";
}

bool Vu1AotBuilder::FindCompiler(const std::string& preferred, bool search_defaults, std::string* path, std::string* version) {
    std::vector<std::string> candidates;
    if (!preferred.empty()) candidates.push_back(preferred);
    if (search_defaults) {
        if (kVu1AotHostCompiler[0] != '\0') candidates.push_back(kVu1AotHostCompiler);  // what built the engine
        candidates.push_back("/usr/bin/clang++");
        candidates.push_back("c++");
        candidates.push_back("g++");
        candidates.push_back("clang++");
    }
    for (const std::string& c : candidates) {
        std::string out;
        if (Run({c, "--version"}, /*search_path=*/true, &out) == 0 && !out.empty()) {
            *path = c;
            *version = FirstLine(out);
            return true;
        }
    }
    return false;
}

// The engine's VU semantic flags (engine/vu/CMakeLists.txt: -fno-strict-aliasing
// -fwrapv, no fast-math; GCC adds -ffp-contract=off -frounding-math at the
// root) plus the definitions this binary was built with, so the bodies compile
// under the rules the engine's own instantiations saw.
std::vector<std::string> Vu1AotBuilder::CompileCommand(const std::string& compiler, const std::string& include_dir, int opt,
                                                       const std::string& src, const std::string& out) {
    std::vector<std::string> cmd = {compiler, "-std=c++17", opt == 2 ? "-O2" : "-O1", "-fno-strict-aliasing", "-fwrapv",
                                    "-fPIC", "-shared", "-w"};
#if defined(NDEBUG)
    cmd.push_back("-DNDEBUG");
#endif
#if defined(PS2R_SIMD) && PS2R_SIMD
    cmd.push_back("-DPS2R_SIMD=1");
#endif
    const bool gcc = compiler.find("g++") != std::string::npos;
    if (gcc) {
        cmd.push_back("-ffp-contract=off");
        cmd.push_back("-frounding-math");
    }
#if PS2R_HOST_MACOS
    cmd.push_back("-undefined");
    cmd.push_back("dynamic_lookup");
#endif
    cmd.push_back("-I" + include_dir);
    cmd.push_back(src);
    cmd.push_back("-o");
    cmd.push_back(out);
    return cmd;
}

bool Vu1AotBuilder::StageInclude(const std::string& prelude_dir, const std::string& include_dir, std::string* err) {
    std::error_code ec;
    const std::string src_manifest = prelude_dir + "/MANIFEST.txt";
    std::FILE* f = std::fopen(src_manifest.c_str(), "r");
    if (f == nullptr) {
        if (err) *err = "no staged prelude at " + prelude_dir + " (MANIFEST.txt missing)";
        return false;
    }
    std::vector<std::string> files;
    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (!s.empty()) files.push_back(s);
    }
    std::fclose(f);
    fs::create_directories(include_dir, ec);
    for (const std::string& rel : files) {
        const fs::path dst = fs::path(include_dir) / rel;
        const fs::path src = fs::path(prelude_dir) / rel;
        fs::create_directories(dst.parent_path(), ec);
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            if (err) *err = "cannot stage " + rel + ": " + ec.message();
            return false;
        }
    }
    fs::copy_file(src_manifest, fs::path(include_dir) / "MANIFEST.txt", fs::copy_options::overwrite_existing, ec);
    return true;
}

// ---- open / close -------------------------------------------------------------

// The shipped pack: Settings::pack_dir, else next to the executable; accepted when
// its manifest names this engine's ABI key and this host's platform.
void Vu1AotBuilder::OpenPack() {
    pack_dir_.clear();
    if (!settings_.search_pack) return;
    const std::string dir = settings_.pack_dir.empty() ? Vu1AotPack::DefaultDir() : settings_.pack_dir;
    if (dir.empty()) return;
    std::string err;
    Vu1AotPack::Manifest m;
    if (!Vu1AotPack::Read(dir, &m, &err)) {
        if (!settings_.pack_dir.empty()) PS2R_LOG(HOST, Warn, "[VU1-AOT] pack %s: %s", dir.c_str(), err.c_str());
        return;
    }
    if (m.abi != abi_key_) {
        PS2R_LOG(HOST, Warn, "[VU1-AOT] pack %s ignored: built for %s, this engine is %s", dir.c_str(), m.abi.c_str(), abi_key_.c_str());
        return;
    }
    if (m.platform != Vu1AotPack::PlatformToken()) {
        PS2R_LOG(HOST, Warn, "[VU1-AOT] pack %s ignored: platform %s, this host is %s", dir.c_str(), m.platform.c_str(),
                 Vu1AotPack::PlatformToken());
        return;
    }
    pack_ = std::move(m);
    pack_dir_ = dir;
}

bool Vu1AotBuilder::Open(std::string* note) {
    if (open_) return true;
    abi_key_ = kVu1AotAbiKey;
    std::string no_compiler_why;
    if (settings_.compiler == "none") {
        // The loader-only shape (engine.vu1_aot_compiler=none): no toolchain is probed or run;
        // libraries come from the shipped pack or the cache alone -- a player who wants no
        // compile on this machine, and the test of what a host without a compiler gets.
        compiler_.clear();
        compiler_version_.clear();
        no_compiler_why = "engine.vu1_aot_compiler=none";
    } else if (!FindCompiler(settings_.compiler, settings_.search_defaults, &compiler_, &compiler_version_)) {
        compiler_.clear();
        compiler_version_.clear();
        no_compiler_why = "no C++ compiler found (tried " +
                          (settings_.compiler.empty() ? std::string("engine.vu1_aot_compiler=<unset>") : settings_.compiler) +
                          (settings_.search_defaults ? std::string(", the engine's own, /usr/bin/clang++, c++, g++, clang++ on the PATH")
                                                     : std::string()) +
                          ")";
    }
    std::error_code ec;
    fs::create_directories(BuildDir(settings_.root, abi_key_), ec);
    include_dir_ = IncludeDir(settings_.root, abi_key_);
    if (!compiler_.empty()) {
        const std::string prelude = settings_.prelude_dir.empty() ? std::string(kVu1AotPreludeStageDir) : settings_.prelude_dir;
        std::string err;
        if (!fs::exists(fs::path(include_dir_) / "MANIFEST.txt", ec) && !StageInclude(prelude, include_dir_, &err)) {
            // A compiler with nothing to include is no compiler.
            compiler_.clear();
            compiler_version_.clear();
            no_compiler_why = err;
        }
    }
    OpenPack();
    // Anything a run without a compiler could still load: the pack, or a library an
    // earlier run compiled into the cache.
    bool cache_has_libs = false;
    if (compiler_.empty()) {
        const std::string ext = std::string(".O") + std::to_string(settings_.opt_level) + "." + ps2r::host::DynamicLibraryExtension();
        for (const auto& e : fs::directory_iterator(BuildDir(settings_.root, abi_key_), ec)) {
            const std::string name = e.path().filename().string();
            if (name.size() > ext.size() && name.compare(name.size() - ext.size(), ext.size(), ext) == 0) { cache_has_libs = true; break; }
        }
    }
    if (compiler_.empty() && pack_dir_.empty() && !cache_has_libs) {
        if (note) *note = "vu1_aot codegen: " + no_compiler_why + ", no shipped pack, no cached library -- the specialised tier stays";
        return false;
    }
    stop_.store(false);
    for (int i = 0; i < settings_.workers; ++i) workers_.emplace_back([this] { WorkerMain(); });
    open_ = true;
    if (note) {
        *note = "vu1_aot codegen: abi=" + abi_key_ + " compiler=" + (compiler_.empty() ? "none (" + no_compiler_why + ")" : compiler_ + " (" + compiler_version_ + ")") +
                " opt=O" + std::to_string(settings_.opt_level) + " cache=" + BuildDir(settings_.root, abi_key_) +
                " pack=" + (pack_dir_.empty() ? std::string("none") : pack_dir_ + " (" + std::to_string(pack_.rows.size()) + " images)") +
                " workers=" + std::to_string(settings_.workers);
    }
    return true;
}

void Vu1AotBuilder::Close() {
    if (!open_) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_.store(true);
        queue_.clear();
    }
    cv_.notify_all();
    for (std::thread& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
    open_ = false;
}

void Vu1AotBuilder::Submit(uint64_t image_hash, std::vector<std::unique_ptr<Vu1Case>> cases, std::vector<uint32_t> seed_pcs) {
    if (!open_ || cases.empty()) return;
    std::error_code ec;
    Job j;
    j.image = image_hash;
    j.cases = std::move(cases);
    j.seeds = std::move(seed_pcs);
    j.cached = fs::exists(LibraryPath(settings_.root, abi_key_, image_hash, settings_.opt_level), ec) ||
               (!pack_dir_.empty() && Vu1AotPack::Find(pack_, image_hash) != nullptr);
    {
        std::lock_guard<std::mutex> lock(mu_);
        // Libraries already on disk first (a load, no compile), then the images
        // with the largest corpus (the title's hot programs) -- so the frame
        // goes native soonest.
        auto before = [](const Job& a, const Job& b) {
            if (a.cached != b.cached) return a.cached;
            return a.cases.size() > b.cases.size();
        };
        auto it = queue_.begin();
        while (it != queue_.end() && !before(j, *it)) ++it;
        queue_.insert(it, std::move(j));
        ++census_.jobs;
        ++census_.pending;
    }
    cv_.notify_one();
}

void Vu1AotBuilder::WorkerMain() {
    ps2r::host::SetCurrentThreadName("vu1aotbuild");
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] { return !queue_.empty() || stop_.load(); });
            if (stop_.load() || queue_.empty()) return;
            job = std::move(queue_.front());
            queue_.erase(queue_.begin());
        }
        std::vector<const Vu1Case*> ptrs;
        ptrs.reserve(job.cases.size());
        for (const auto& c : job.cases) ptrs.push_back(c.get());
        const Built b = BuildOne(job.image, ptrs, job.seeds);
        Fold(b);
        AppendRow(job.image, b);
        std::lock_guard<std::mutex> lock(mu_);
        if (census_.pending > 0) --census_.pending;
    }
}

Vu1AotBuilder::Built Vu1AotBuilder::BuildNow(const std::vector<const Vu1Case*>& cases, const std::vector<uint32_t>& seed_pcs) {
    Built b;
    if (!open_ || cases.empty()) {
        b.status = "no_cases";
        b.reason = open_ ? "no corpus case" : "builder not open";
        return b;
    }
    const uint64_t image = Vu1AotVerifier::ImageHashOf(*cases[0]);
    {
        std::lock_guard<std::mutex> lock(mu_);
        ++census_.jobs;
    }
    b = BuildOne(image, cases, seed_pcs);
    Fold(b);
    AppendRow(image, b);
    return b;
}

// ---- the pipeline ------------------------------------------------------------

bool Vu1AotBuilder::Compile(const std::string& src, const std::string& out, std::string* log, uint64_t* ns) {
    const std::string tmp = out + ".tmp";
    const std::vector<std::string> cmd = CompileCommand(compiler_, include_dir_, settings_.opt_level, src, tmp);
    const uint64_t t0 = NowNs();
    const int rc = Run(cmd, /*search_path=*/true, log);
    *ns = NowNs() - t0;
    std::error_code ec;
    if (rc != 0) {
        fs::remove(tmp, ec);
        if (log->empty()) *log = "compiler exit " + std::to_string(rc);
        return false;
    }
    fs::rename(tmp, out, ec);
    if (ec) {
        *log = "rename: " + ec.message();
        return false;
    }
    return true;
}

bool Vu1AotBuilder::Load(uint64_t image, const std::string& path, Library* out, std::string* err) {
    ps2r::host::DynamicLibrary lib;
    if (!ps2r::host::DynamicLibraryOpen(&lib, path.c_str(), err)) return false;
    const std::string sym = Vu1AotEmitter::EntrySymbol(image);
    using EntryFn = const Vu1AotImageExports* (*)();
    const EntryFn entry = reinterpret_cast<EntryFn>(ps2r::host::DynamicLibrarySymbol(lib, sym.c_str()));
    const Vu1AotImageExports* ex = entry != nullptr ? entry() : nullptr;
    std::string why;
    if (entry == nullptr) why = "entry " + sym + " missing";
    else if (ex == nullptr) why = "entry returned null";
    else if (ex->abi_version != kVu1AotAbiVersion) why = "abi " + std::to_string(ex->abi_version) + " != " + std::to_string(kVu1AotAbiVersion);
    else if (ex->slot_size != sizeof(VuDecodedSlot)) why = "slot size " + std::to_string(ex->slot_size);
    else if (ex->image_hash != image) why = "image hash mismatch";
    else if (ex->engine_build == nullptr || abi_key_ != ex->engine_build) why = "abi key mismatch (" + std::string(ex->engine_build ? ex->engine_build : "null") + ")";
    else if (ex->block_count == 0 || ex->blocks == nullptr) why = "empty table";
    if (!why.empty()) {
        ps2r::host::DynamicLibraryClose(lib);
        *err = why;
        return false;
    }
    out->image = image;
    out->lib = lib;
    out->exports = ex;
    return true;
}

// Load `path`, check the table, run the gate on the loaded code, publish. False
// leaves `b` describing the failure (load_failed | refused); the caller decides
// what happens to the file (a pack library is never touched).
bool Vu1AotBuilder::LoadAndPublish(uint64_t image, const std::string& path, const std::vector<const Vu1Case*>& cases, const char* source,
                                   Built* b) {
    Library lib;
    std::string err;
    if (!Load(image, path, &lib, &err)) {
        b->status = "load_failed";
        b->reason = err;
        PS2R_LOG(HOST, Warn, "[VU1-AOT] image %s: load failed (%s) [%s %s]", Vu1AotEmitter::Hex16(image).c_str(), err.c_str(), source,
                 path.c_str());
        return false;
    }
    b->blocks = lib.exports->block_count;
    b->ops = 0;
    for (uint32_t i = 0; i < lib.exports->block_count; ++i) b->ops += lib.exports->blocks[i].n_ops;
    // The gate again, on the loaded code.
    const uint64_t t0 = NowNs();
    const Vu1AotVerifier::Result r = Vu1AotVerifier::Verify(cases, lib.exports);
    b->verify_ns = NowNs() - t0;
    if (!r.identical) {
        b->status = "refused";
        b->reason = r.divergence;
        ps2r::host::DynamicLibraryClose(lib.lib);
        PS2R_LOG(HOST, Warn, "[VU1-AOT] image %s: generated code REFUSED (%s) [%s %s]", Vu1AotEmitter::Hex16(image).c_str(),
                 r.divergence.c_str(), source, path.c_str());
        return false;
    }
    if (r.native_blocks == 0) {
        // Loaded, identical, but no block of the table was ever entered: the
        // library is useless for this corpus; keep it (harmless) and say so.
        b->reason = "no generated block entered";
    }
    // Publish: the tier installs at the image's next kick.
    {
        std::lock_guard<std::mutex> lock(mu_);
        libraries_.push_back(lib);
    }
    tier_.SetNativeExports(image, lib.exports);
    b->exports = lib.exports;
    b->status = "native";
    b->source = source;
    PS2R_LOG(HOST, Info,
             "[VU1-AOT] image %s native: blocks=%u ops=%llu %s compile=%.0f ms verify=%.0f ms (%zu cases, %llu native block runs)",
             Vu1AotEmitter::Hex16(image).c_str(), b->blocks, static_cast<unsigned long long>(b->ops), source,
             static_cast<double>(b->compile_ns) / 1e6, static_cast<double>(b->verify_ns) / 1e6, cases.size(),
             static_cast<unsigned long long>(r.native_blocks));
    return true;
}

Vu1AotBuilder::Built Vu1AotBuilder::BuildOne(uint64_t image, const std::vector<const Vu1Case*>& cases,
                                             const std::vector<uint32_t>& seeds) {
    Built b;
    std::error_code ec;
    // 1. The shipped pack (read-only: a library that fails is skipped, never deleted).
    if (!pack_dir_.empty()) {
        if (const Vu1AotPack::Row* row = Vu1AotPack::Find(pack_, image)) {
            const std::string path = Vu1AotPack::LibraryPath(pack_dir_, image, row->opt);
            if (fs::exists(path, ec)) {
                if (LoadAndPublish(image, path, cases, "pack", &b)) return b;
                std::lock_guard<std::mutex> lock(mu_);
                ++census_.pack_rejected;
            }
        }
    }
    // 2. The user cache.
    const std::string lib_path = LibraryPath(settings_.root, abi_key_, image, settings_.opt_level);
    const std::string src_path = SourcePath(settings_.root, abi_key_, image);
    if (fs::exists(lib_path, ec)) {
        b = Built{};
        b.cached = true;
        if (LoadAndPublish(image, lib_path, cases, "cache", &b)) return b;
        fs::remove(lib_path, ec);   // a stale or foreign artefact: rebuilt below / next time
        if (compiler_.empty()) return b;
    }
    // 3. The builder.
    if (compiler_.empty()) {
        b = Built{};
        b.status = "no_compiler";
        b.reason = "no library in the pack or the cache and no compiler";
        return b;
    }
    b = Built{};
    // The blocks: the cases' entries, the seeds (JR / JALR targets the verifier's
    // native legs built -- gathered here when none were given) and their structural
    // closure, on a scratch program decoded from the image the cases carry.
    std::vector<uint32_t> entries(seeds);
    if (entries.empty()) {
        const Vu1AotVerifier::Result pre = Vu1AotVerifier::Verify(cases);
        entries = pre.block_pcs;
    }
    for (const Vu1Case* c : cases) entries.push_back(c->start_pc & VuBlockTier::kPcMask);
    VuBlockProgram scratch;
    VuBlockTier::DecodeImage(cases[0]->micro_mem, Vu1Case::kMicroSize, scratch);
    scratch.hash = image;
    const std::vector<uint32_t> pcs = Vu1AotEmitter::ReachableBlocks(scratch, entries, 8192);
    Vu1AotEmitter::Options opt;
    opt.engine_build = abi_key_;
    opt.note = "corpus cases: " + std::to_string(cases.size()) + "; engine build: " + settings_.build_id + "; compiler: " +
               compiler_version_ + "; -O" + std::to_string(settings_.opt_level);
    const Vu1AotEmitter::Output out = Vu1AotEmitter::Emit(scratch, pcs, opt);
    b.blocks = out.blocks;
    b.ops = out.ops;
    if (!WriteFile(src_path, out.source)) {
        b.status = "compile_failed";
        b.reason = "cannot write " + src_path;
        return b;
    }
    // The host compiler.
    std::string log;
    if (!Compile(src_path, lib_path, &log, &b.compile_ns)) {
        WriteFile(BuildDir(settings_.root, abi_key_) + "/" + Vu1AotEmitter::Hex16(image) + ".log", log);
        b.status = "compile_failed";
        b.reason = FirstLine(log);
        PS2R_LOG(HOST, Warn, "[VU1-AOT] image %s: compile failed (%s)", Vu1AotEmitter::Hex16(image).c_str(), b.reason.c_str());
        return b;
    }
    if (!settings_.keep_source) fs::remove(src_path, ec);
    if (!LoadAndPublish(image, lib_path, cases, "compiled", &b)) {
        fs::remove(lib_path, ec);   // refused or unloadable: the artefact goes
        return b;
    }
    return b;
}

void Vu1AotBuilder::Fold(const Built& b) {
    std::lock_guard<std::mutex> lock(mu_);
    if (b.source == "pack") ++census_.pack;
    else if (b.cached) ++census_.cached;
    else if (b.compile_ns != 0) ++census_.compiled;
    if (b.status == "no_compiler") ++census_.no_compiler;
    else if (b.status == "compile_failed") ++census_.compile_failed;
    else if (b.status == "load_failed") ++census_.load_failed;
    else if (b.status == "refused") ++census_.refused;
    else if (b.status == "native") {
        ++census_.native;
        census_.blocks += b.blocks;
        census_.ops += b.ops;
    }
    census_.compile_ns += b.compile_ns;
    census_.verify_ns += b.verify_ns;
}

void Vu1AotBuilder::AppendRow(uint64_t image, const Built& b) {
    std::error_code ec;
    const std::string dir = BuildDir(settings_.root, abi_key_);
    fs::create_directories(dir, ec);
    std::string row = "image=" + Vu1AotEmitter::Hex16(image) + " status=" + b.status + " compiler=" + Token(compiler_version_.empty() ? "none" : compiler_version_) +
                      " opt=" + std::to_string(settings_.opt_level) + " blocks=" + std::to_string(b.blocks) + " ops=" +
                      std::to_string(b.ops) + " compile_ms=" + std::to_string(b.compile_ns / 1000000u) + " verify_ms=" +
                      std::to_string(b.verify_ns / 1000000u) + " cached=" + (b.cached ? "1" : "0");
    const std::string lib_path = b.source == "pack" && !pack_dir_.empty()
                                     ? Vu1AotPack::LibraryPath(pack_dir_, image, Vu1AotPack::Find(pack_, image) ? Vu1AotPack::Find(pack_, image)->opt : settings_.opt_level)
                                     : LibraryPath(settings_.root, abi_key_, image, settings_.opt_level);
    row += " bytes=" + std::to_string(fs::exists(lib_path, ec) ? static_cast<unsigned long long>(fs::file_size(lib_path, ec)) : 0ull);
    if (!b.source.empty()) row += " source=" + b.source;
    if (!b.reason.empty()) row += " reason=" + b.reason;
    std::lock_guard<std::mutex> lock(mu_);
    if (std::FILE* f = std::fopen((dir + "/native.txt").c_str(), "a")) {
        row += "\n";
        std::fwrite(row.data(), 1, row.size(), f);
        std::fclose(f);
    }
}

// ---- the census ----------------------------------------------------------------

Vu1AotBuilder::Census Vu1AotBuilder::census() const {
    std::lock_guard<std::mutex> lock(mu_);
    return census_;
}

std::string Vu1AotBuilder::CensusRow() const {
    const Census c = census();
    const Vu1AotTier::Stats& s = tier_.stats();
    return "compiler:" + Token(compiler_version_.empty() ? "none" : compiler_version_) + ":abi:" + abi_key_ + ":opt:" +
           std::to_string(settings_.opt_level) + ":jobs:" + std::to_string(c.jobs) + ":pack:" + std::to_string(c.pack) +
           ":pack_rejected:" + std::to_string(c.pack_rejected) + ":no_compiler:" + std::to_string(c.no_compiler) +
           ":cached:" + std::to_string(c.cached) + ":compiled:" + std::to_string(c.compiled) + ":native:" + std::to_string(c.native) +
           ":refused:" + std::to_string(c.refused) + ":compile_failed:" + std::to_string(c.compile_failed) + ":load_failed:" +
           std::to_string(c.load_failed) + ":pending:" + std::to_string(c.pending) + ":compile_ms:" +
           std::to_string(c.compile_ns / 1000000u) + ":verify_ms:" + std::to_string(c.verify_ns / 1000000u) + ":blocks:" +
           std::to_string(c.blocks) + ":ops:" + std::to_string(c.ops) + ":installs:" + std::to_string(s.native_installs) +
           ":installed_blocks:" + std::to_string(s.native_blocks) + ":install_refused:" + std::to_string(tier_.native_refused());
}

}  // namespace ps2r::app
