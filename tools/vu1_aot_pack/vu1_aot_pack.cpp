// vu1_aot_pack -- build (or check) the pre-built VU1 library pack of a title from its
// corpus of captured cases.
//
//   vu1_aot_pack build --corpus <dir> [--corpus <dir>]... --out <pack dir>
//                      [--compiler <c++>] [--opt 1|2] [--prelude <dir>] [--keep-work]
//                      [--region-blocks N] [--region-ops N] [--region-taken 0|1] [--chain 0|1]
//   vu1_aot_pack check --corpus <dir> [--corpus <dir>]... --pack <pack dir>
//   vu1_aot_pack key   [--prelude <dir>]
//
// build: for every image of the corpus (Vu1Case files), Vu1AotBuilder emits the
// image's blocks as C++, compiles them with the host toolchain, loads the library
// and RE-VERIFIES every case native vs interpreter (the gate the game applies); the
// libraries that pass are copied into <pack dir> with pack.txt (the manifest:
// the engine's VU1 AOT ABI key, the platform, the compiler, one row per image).
// An image that fails is reported and left out. Exit 0 = every image native, 1 =
// some image is not in the pack, 77 = no compiler / no corpus (SKIP).
// check: load every library of an existing pack WITHOUT a compiler and run the
// same gate over the corpus (what a player's engine does at first run); exit 0 when
// every image of the corpus is served by the pack, 1 otherwise, 77 without a corpus.
// key: print this engine's ABI key (kVu1AotAbiKey) and, with --prelude, the key
// computed from that staged tree (they must agree for a pack built here to load).
//
// The pack is game-derived (generated code of the title's microprograms): never
// commit it, never publish it as a release asset without a licence decision --
// the title's README / CONTRIBUTING state the boundary. The tool reads no
// environment; every input is a flag.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "app/host/vu1_aot_builder.h"
#include "app/host/vu1_aot_pack.h"
#include "engine/platform/host/platform.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu1_aot/vu1_aot_abi_key.h"
#include "engine/vu/vu1_aot/vu1_aot_emitter.h"
#include "engine/vu/vu1_aot/vu1_aot_verifier.h"
#include "engine/vu/vu1_case.h"
#include "engine/vu/vu_unit.h"
#include "vu1_aot_abi_key.h"
#include "vu1_aot_paths.h"

namespace {

namespace fs = std::filesystem;

using Images = std::map<uint64_t, std::vector<std::unique_ptr<Vu1Case>>>;

void LoadDir(const std::string& dir, Images& images) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        std::fprintf(stderr, "vu1_aot_pack: not a directory: %s\n", dir.c_str());
        return;
    }
    std::vector<std::string> files;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string name = e.path().filename().string();
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".bin") == 0) files.push_back(e.path().string());
    }
    std::sort(files.begin(), files.end());
    for (const std::string& f : files) {
        std::unique_ptr<Vu1Case> c(new Vu1Case());
        if (vu1_case::ReadCase(f.c_str(), *c)) images[Vu1AotVerifier::ImageHashOf(*c)].push_back(std::move(c));
    }
}

int Usage() {
    std::fprintf(stderr,
                 "usage: vu1_aot_pack build --corpus <dir>... --out <pack dir> [--compiler <c++>] [--opt 1|2] [--prelude <dir>] [--keep-work]\n"
                 "                          [--region-blocks N] [--region-ops N] [--region-taken 0|1] [--chain 0|1]   (the generator's region policy)\n"
                 "       vu1_aot_pack check --corpus <dir>... --pack <pack dir>\n"
                 "       vu1_aot_pack key [--prelude <dir>]\n");
    return 2;
}

struct Args {
    std::string mode;
    std::vector<std::string> corpus;
    std::string out, pack, compiler, prelude;
    int opt = 2;
    Vu1AotBlockGen::Policy region;
    bool keep_work = false;
};

bool Parse(int argc, char** argv, Args* a) {
    if (argc < 2) return false;
    a->mode = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&](std::string* dst) { if (i + 1 >= argc) return false; *dst = argv[++i]; return true; };
        if (s == "--corpus") { std::string d; if (!next(&d)) return false; a->corpus.push_back(d); }
        else if (s == "--out") { if (!next(&a->out)) return false; }
        else if (s == "--pack") { if (!next(&a->pack)) return false; }
        else if (s == "--compiler") { if (!next(&a->compiler)) return false; }
        else if (s == "--prelude") { if (!next(&a->prelude)) return false; }
        else if (s == "--opt") { std::string v; if (!next(&v)) return false; a->opt = std::atoi(v.c_str()); }
        else if (s == "--region-blocks") { std::string v; if (!next(&v)) return false; a->region.block_cap = std::atoi(v.c_str()); }
        else if (s == "--region-ops") { std::string v; if (!next(&v)) return false; a->region.op_cap = std::atoi(v.c_str()); }
        else if (s == "--region-taken") { std::string v; if (!next(&v)) return false; a->region.taken_targets = std::atoi(v.c_str()) != 0; }
        else if (s == "--chain") { std::string v; if (!next(&v)) return false; a->region.chain = std::atoi(v.c_str()) != 0; }
        else if (s == "--keep-work") a->keep_work = true;
        else return false;
    }
    return true;
}

int RunKey(const Args& a) {
    std::printf("engine abi key: %s\n", ps2r::app::kVu1AotAbiKey);
    const std::string prelude = a.prelude.empty() ? std::string(ps2r::app::kVu1AotPreludeStageDir) : a.prelude;
    std::string err;
    const std::string k = Vu1AotAbiKey::ComputeForThisBuild(prelude, &err);
    if (k.empty()) {
        std::printf("prelude %s: %s\n", prelude.c_str(), err.c_str());
        return 1;
    }
    std::printf("prelude %s: %s %s\n", prelude.c_str(), k.c_str(), k == ps2r::app::kVu1AotAbiKey ? "(matches)" : "(MISMATCH)");
    return k == ps2r::app::kVu1AotAbiKey ? 0 : 1;
}

// One builder over a corpus; `with_compiler` = the build mode (the check mode runs the
// loader half only: `--compiler /nonexistent`, no default search).
int RunImages(const Args& a, Images& images, bool with_compiler, const std::string& root, const std::string& pack_dir,
              std::vector<ps2r::app::Vu1AotPack::Row>* rows, std::string* compiler_token) {
    VuUnit unit(1, nullptr);
    Vu1AotTier& tier = unit.dispatch().aot();
    ps2r::app::Vu1AotBuilder::Settings bs;
    bs.root = root;
    bs.build_id = "vu1_aot_pack";
    bs.opt_level = a.opt;
    bs.region = a.region;
    bs.prelude_dir = a.prelude;
    if (with_compiler) {
        bs.compiler = a.compiler;
        bs.search_defaults = a.compiler.empty();
        bs.search_pack = false;
    } else {
        bs.compiler = "/nonexistent";
        bs.search_defaults = false;
        bs.pack_dir = pack_dir;
        bs.search_pack = true;
    }
    ps2r::app::Vu1AotBuilder builder(tier, bs);
    std::string note;
    if (!builder.Open(&note)) {
        std::printf("SKIP: %s\n", note.c_str());
        return 77;
    }
    std::printf("%s\n", note.c_str());
    if (with_compiler && !builder.has_compiler()) {
        std::printf("SKIP: no compiler\n");
        return 77;
    }
    if (!with_compiler && builder.pack_dir().empty()) {
        std::printf("FAIL: pack %s not accepted by this engine (abi %s, platform %s)\n", pack_dir.c_str(), builder.abi_key().c_str(),
                    ps2r::app::Vu1AotPack::PlatformToken());
        return 1;
    }
    int native = 0;
    for (const auto& kv : images) {
        std::vector<const Vu1Case*> ptrs;
        for (const auto& c : kv.second) ptrs.push_back(c.get());
        const ps2r::app::Vu1AotBuilder::Built b = builder.BuildNow(ptrs, {});
        std::printf("image=%s cases=%zu status=%s source=%s blocks=%u ops=%llu compile_ms=%.0f verify_ms=%.0f%s%s\n",
                    Vu1AotEmitter::Hex16(kv.first).c_str(), ptrs.size(), b.status.c_str(), b.source.empty() ? "-" : b.source.c_str(), b.blocks,
                    static_cast<unsigned long long>(b.ops), static_cast<double>(b.compile_ns) / 1e6, static_cast<double>(b.verify_ns) / 1e6,
                    b.reason.empty() ? "" : " reason=", b.reason.c_str());
        if (b.exports != nullptr) {
            ++native;
            if (rows) {
                ps2r::app::Vu1AotPack::Row r;
                r.image = kv.first;
                r.opt = a.opt;
                r.blocks = b.blocks;
                r.ops = b.ops;
                std::error_code ec;
                const std::string lib = ps2r::app::Vu1AotBuilder::LibraryPath(root, builder.abi_key(), kv.first, a.opt);
                r.bytes = fs::exists(lib, ec) ? static_cast<uint64_t>(fs::file_size(lib, ec)) : 0;
                rows->push_back(r);
            }
        }
    }
    if (compiler_token) *compiler_token = builder.compiler_version();
    std::printf("census: %s\n", builder.CensusRow().c_str());
    std::printf("images=%zu native=%d\n", images.size(), native);
    builder.Close();
    return native == static_cast<int>(images.size()) ? 0 : 1;
}

std::string Token(const std::string& s) {
    std::string tok = s.empty() ? "unknown" : s;
    for (char& ch : tok) {
        const bool ok = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '.' || ch == '_' || ch == '-';
        if (!ok) ch = '_';
    }
    return tok;
}

int RunBuild(const Args& a, Images& images) {
    if (a.out.empty()) return Usage();
    std::error_code ec;
    const std::string work = a.out + "/.work";
    fs::create_directories(work, ec);
    std::vector<ps2r::app::Vu1AotPack::Row> rows;
    std::string compiler_token;
    const int rc = RunImages(a, images, /*with_compiler=*/true, work, "", &rows, &compiler_token);
    if (rc == 77) {
        if (!a.keep_work) fs::remove_all(work, ec);
        return 77;
    }
    // The pack: the libraries that passed, copied flat; the manifest.
    ps2r::app::Vu1AotPack::Manifest m;
    m.abi = ps2r::app::kVu1AotAbiKey;
    m.platform = ps2r::app::Vu1AotPack::PlatformToken();
    m.compiler = Token(compiler_token);
    m.opt = a.opt;
    m.rows = rows;
    uint64_t bytes = 0;
    for (const ps2r::app::Vu1AotPack::Row& r : rows) {
        const std::string src = ps2r::app::Vu1AotBuilder::LibraryPath(work, m.abi, r.image, r.opt);
        const std::string dst = ps2r::app::Vu1AotPack::LibraryPath(a.out, r.image, r.opt);
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::fprintf(stderr, "vu1_aot_pack: cannot copy %s -> %s: %s\n", src.c_str(), dst.c_str(), ec.message().c_str());
            return 1;
        }
        bytes += r.bytes;
    }
    std::string err;
    if (!ps2r::app::Vu1AotPack::Write(a.out, m, &err)) {
        std::fprintf(stderr, "vu1_aot_pack: %s\n", err.c_str());
        return 1;
    }
    if (!a.keep_work) fs::remove_all(work, ec);
    std::printf("pack %s: %s\n", a.out.c_str(), ps2r::app::Vu1AotPack::FormatHeader(m).c_str());
    std::printf("pack bytes=%llu images=%zu of %zu%s\n", static_cast<unsigned long long>(bytes), rows.size(), images.size(),
                rc == 0 ? " ALL NATIVE" : " (some images are NOT in the pack)");
    return rc;
}

int RunCheck(const Args& a, Images& images) {
    if (a.pack.empty()) return Usage();
    std::error_code ec;
    const std::string work = (fs::temp_directory_path(ec) / ("vu1_aot_pack_check_" + std::to_string(ps2r::host::ProcessId()))).string();
    fs::create_directories(work, ec);
    const int rc = RunImages(a, images, /*with_compiler=*/false, work, a.pack, nullptr, nullptr);
    fs::remove_all(work, ec);   // the mapped libraries survive the unlink until the builder unmaps them
    std::printf("%s\n", rc == 0 ? "PACK SERVES EVERY IMAGE" : rc == 77 ? "SKIP" : "PACK INCOMPLETE OR REFUSED");
    return rc;
}

}  // namespace

int main(int argc, char** argv) {
    Args a;
    if (!Parse(argc, argv, &a)) return Usage();
    if (a.mode == "key") return RunKey(a);
    if (a.mode != "build" && a.mode != "check") return Usage();
    if (a.corpus.empty()) {
        std::printf("SKIP: no corpus directory given\n");
        return 77;
    }
    Images images;
    for (const std::string& d : a.corpus) LoadDir(d, images);
    size_t cases = 0;
    for (const auto& kv : images) cases += kv.second.size();
    if (images.empty()) {
        std::printf("SKIP: no readable case under the given directories\n");
        return 77;
    }
    std::printf("corpus: %zu cases, %zu images\n", cases, images.size());
    return a.mode == "build" ? RunBuild(a, images) : RunCheck(a, images);
}
