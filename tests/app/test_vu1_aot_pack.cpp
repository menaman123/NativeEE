// tests/app/test_vu1_aot_pack.cpp -- ctest Vu1AotPackManifest (in-process) and
// Vu1AotPackRoundTrip (a corpus + a compiler; SKIP 77 otherwise): the pre-built VU1
// library pack (app/host/vu1_aot_pack.h) and the loader's search order.
//
//   test_vu1_aot_pack [<case dir>...]
//
// In-process, always: the manifest grammar round-trips (header + rows; a malformed
// header, a wrong version and a short image hash are rejected; Find); the engine's
// generated ABI key equals Vu1AotAbiKey::Compute over the build's staged prelude (the
// python and the C++ implementation agree); the platform token names this host.
// With a corpus: `vu1_aot_pack build` in-process (a builder with the host compiler
// writes the libraries, the test writes the manifest), then a builder with NO compiler
// (`compiler=none`) and the pack configured serves every image native from the pack
// (census pack:n, compiled:0); a pack whose manifest names another ABI key is ignored
// and, with no compiler and no cache, Open refuses with the one-line note; a library
// of the pack that was tampered with is skipped (pack_rejected) and never deleted.
#include <algorithm>
#include <cstdio>
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
#include "engine/vu/vu1_aot/vu1_aot_verifier.h"
#include "engine/vu/vu1_case.h"
#include "engine/vu/vu_unit.h"
#include "vu1_aot_abi_key.h"
#include "vu1_aot_paths.h"

using ps2r::app::Vu1AotBuilder;
using ps2r::app::Vu1AotPack;

namespace {

namespace fs = std::filesystem;

int g_fail = 0;
void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_fail;
}

using Images = std::map<uint64_t, std::vector<std::unique_ptr<Vu1Case>>>;

void LoadDir(const std::string& dir, Images& images) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
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

void ManifestGrammar() {
    Vu1AotPack::Manifest m;
    m.abi = "abi-0123456789abcdef";
    m.platform = Vu1AotPack::PlatformToken();
    m.compiler = "Apple_clang_17";
    m.opt = 2;
    m.rows.push_back(Vu1AotPack::Row{0x3326286ecab9d6f6ull, 2, 328, 2302, 907280});
    m.rows.push_back(Vu1AotPack::Row{0x55c3d2c682bf6fc1ull, 2, 1, 2, 50192});
    std::error_code ec;
    const std::string dir = (fs::temp_directory_path(ec) / ("vu1_aot_pack_test_" + std::to_string(ps2r::host::ProcessId()))).string();
    std::string err;
    Check(Vu1AotPack::Write(dir, m, &err), "Write creates the pack dir and pack.txt");
    Vu1AotPack::Manifest back;
    Check(Vu1AotPack::Read(dir, &back, &err), "Read parses it back");
    Check(back.abi == m.abi && back.platform == m.platform && back.compiler == m.compiler && back.opt == 2, "header fields round-trip");
    Check(back.rows.size() == 2 && back.rows[0].image == m.rows[0].image && back.rows[0].blocks == 328 && back.rows[0].ops == 2302 &&
              back.rows[0].bytes == 907280 && back.rows[1].image == m.rows[1].image,
          "rows round-trip");
    Check(Vu1AotPack::Find(back, 0x55c3d2c682bf6fc1ull) != nullptr && Vu1AotPack::Find(back, 0x1ull) == nullptr, "Find by image");
    Check(Vu1AotPack::LibraryPath(dir, 0x55c3d2c682bf6fc1ull, 2) ==
              dir + "/55c3d2c682bf6fc1.O2." + ps2r::host::DynamicLibraryExtension(),
          "the library path grammar");
    Vu1AotPack::Manifest bad;
    Check(!Vu1AotPack::ParseHeader("vu1_aot_pack version=2 abi=abi-1 platform=x", &bad), "another version is rejected");
    Check(!Vu1AotPack::ParseHeader("nope version=1 abi=abi-1 platform=x", &bad), "another leading token is rejected");
    Check(!Vu1AotPack::ParseHeader("vu1_aot_pack version=1 platform=x", &bad), "a header without abi is rejected");
    Vu1AotPack::Row r;
    Check(!Vu1AotPack::ParseRow("image=55c3 opt=2", &r), "a short image hash is rejected");
    Check(Vu1AotPack::ParseRow("image=55c3d2c682bf6fc1 opt=1 blocks=3 ops=9 bytes=77", &r) && r.opt == 1 && r.blocks == 3 && r.ops == 9 && r.bytes == 77,
          "a row parses field by field");
    Check(!Vu1AotPack::Read(dir + "/absent", &back, &err), "Read of a dir without pack.txt fails");
    fs::remove_all(dir, ec);
}

void AbiKey() {
    std::string err;
    const std::string k = Vu1AotAbiKey::ComputeForThisBuild(ps2r::app::kVu1AotPreludeStageDir, &err);
    std::printf("     generated %s computed %s %s\n", ps2r::app::kVu1AotAbiKey, k.c_str(), err.c_str());
    Check(!k.empty(), "the staged prelude's key computes");
    Check(k == ps2r::app::kVu1AotAbiKey, "the build-time key (python) equals Vu1AotAbiKey::Compute (C++)");
    Check(k.size() == 20 && k.compare(0, 4, "abi-") == 0, "key grammar abi-<16 hex>");
    const std::vector<std::string> other = {"PS2R_SIMD=0", "X=1"};
    Check(Vu1AotAbiKey::Compute(ps2r::app::kVu1AotPreludeStageDir, other, &err) != k, "other definitions give another key");
    Check(std::string(Vu1AotPack::PlatformToken()) != "unknown", "the platform token names this host");
    const std::string d = Vu1AotPack::DefaultDir();
    Check(!d.empty() && std::filesystem::path(d).filename() == "vu1_aot_pack", "DefaultDir = <exe dir>/vu1_aot_pack");   // either separator
}

int RoundTrip(Images& images) {
    std::error_code ec;
    const std::string root = (fs::temp_directory_path(ec) / ("vu1_aot_pack_rt_" + std::to_string(ps2r::host::ProcessId()))).string();
    const std::string work = root + "/work", pack = root + "/pack", cache = root + "/cache";
    fs::create_directories(work, ec);
    // 1. Build the libraries with the host compiler (the tool's build mode, in-process).
    VuUnit unit_a(1, nullptr);
    Vu1AotBuilder::Settings bs;
    bs.root = work;
    bs.build_id = "test";
    bs.opt_level = 2;
    bs.search_pack = false;
    Vu1AotBuilder builder(unit_a.dispatch().aot(), bs);
    std::string note;
    if (!builder.Open(&note) || !builder.has_compiler()) {
        std::printf("SKIP: %s\n", note.c_str());
        fs::remove_all(root, ec);
        return 77;
    }
    Vu1AotPack::Manifest m;
    m.abi = builder.abi_key();
    m.platform = Vu1AotPack::PlatformToken();
    m.compiler = "test";
    m.opt = 2;
    for (const auto& kv : images) {
        std::vector<const Vu1Case*> ptrs;
        for (const auto& c : kv.second) ptrs.push_back(c.get());
        const Vu1AotBuilder::Built b = builder.BuildNow(ptrs, {});
        Check(b.status == "native" && b.source == "compiled", "the compiler builds the image native (source=compiled)");
        if (b.status != "native") continue;
        fs::create_directories(pack, ec);
        fs::copy_file(Vu1AotBuilder::LibraryPath(work, builder.abi_key(), kv.first, 2), Vu1AotPack::LibraryPath(pack, kv.first, 2),
                      fs::copy_options::overwrite_existing, ec);
        m.rows.push_back(Vu1AotPack::Row{kv.first, 2, b.blocks, b.ops, 0});
    }
    builder.Close();
    std::string err;
    Check(Vu1AotPack::Write(pack, m, &err), "the pack manifest is written");

    // 2. No compiler, the pack configured: every image native from the pack.
    {
        VuUnit unit_b(1, nullptr);
        Vu1AotBuilder::Settings nb;
        nb.root = cache;
        nb.build_id = "test";
        nb.compiler = "none";
        nb.search_defaults = false;
        nb.pack_dir = pack;
        Vu1AotBuilder loader(unit_b.dispatch().aot(), nb);
        Check(loader.Open(&note), "Open succeeds without a compiler when the pack is accepted");
        std::printf("     %s\n", note.c_str());
        Check(!loader.has_compiler() && loader.pack_dir() == pack, "no compiler, the pack in use");
        size_t native = 0;
        for (const auto& kv : images) {
            std::vector<const Vu1Case*> ptrs;
            for (const auto& c : kv.second) ptrs.push_back(c.get());
            const Vu1AotBuilder::Built b = loader.BuildNow(ptrs, {});
            if (b.status == "native" && b.source == "pack" && b.compile_ns == 0) ++native;
            else std::printf("     image %llx: status=%s source=%s reason=%s\n", static_cast<unsigned long long>(kv.first), b.status.c_str(), b.source.c_str(), b.reason.c_str());
        }
        Check(native == images.size(), "every image is served native from the pack, no compile");
        const Vu1AotBuilder::Census c = loader.census();
        Check(c.pack == images.size() && c.compiled == 0 && c.cached == 0 && c.no_compiler == 0, "census: pack:n compiled:0 cached:0");
        Check(unit_b.dispatch().aot().native_images() == images.size(), "the tier holds every image's exports");
        std::printf("     %s\n", loader.CensusRow().c_str());
        loader.Close();
    }
    // 3. A pack for another ABI key is ignored; with no compiler and no cache Open refuses.
    {
        Vu1AotPack::Manifest wrong = m;
        wrong.abi = "abi-0000000000000000";
        const std::string pack2 = root + "/pack_wrong";
        Check(Vu1AotPack::Write(pack2, wrong, &err), "a second manifest (wrong key) is written");
        VuUnit unit_c(1, nullptr);
        Vu1AotBuilder::Settings nb;
        nb.root = root + "/cache_empty";
        nb.build_id = "test";
        nb.compiler = "none";
        nb.search_defaults = false;
        nb.pack_dir = pack2;
        Vu1AotBuilder loader(unit_c.dispatch().aot(), nb);
        Check(!loader.Open(&note), "Open refuses: wrong-key pack, no compiler, no cache");
        std::printf("     %s\n", note.c_str());
        Check(note.find("no shipped pack") != std::string::npos, "the note names the missing pack");
    }
    // 4. A tampered pack library is skipped (pack_rejected), never deleted; with no
    //    compiler the image stays no_compiler.
    {
        const uint64_t first = images.begin()->first;
        const std::string lib = Vu1AotPack::LibraryPath(pack, first, 2);
        std::FILE* f = std::fopen(lib.c_str(), "r+b");
        if (f) {
            std::fseek(f, 0, SEEK_SET);
            const char junk[8] = {'j', 'u', 'n', 'k', 'j', 'u', 'n', 'k'};
            std::fwrite(junk, 1, sizeof(junk), f);
            std::fclose(f);
        }
        VuUnit unit_d(1, nullptr);
        Vu1AotBuilder::Settings nb;
        nb.root = root + "/cache_d";
        nb.build_id = "test";
        nb.compiler = "none";
        nb.search_defaults = false;
        nb.pack_dir = pack;
        Vu1AotBuilder loader(unit_d.dispatch().aot(), nb);
        Check(loader.Open(&note), "Open with the (partly tampered) pack");
        std::vector<const Vu1Case*> ptrs;
        for (const auto& c : images.begin()->second) ptrs.push_back(c.get());
        const Vu1AotBuilder::Built b = loader.BuildNow(ptrs, {});
        Check(b.status == "no_compiler", "the tampered library is not loaded and there is no compiler to rebuild it");
        Check(loader.census().pack_rejected == 1, "census pack_rejected:1");
        Check(fs::exists(lib, ec), "the pack library is never deleted");
        loader.Close();
    }
    fs::remove_all(root, ec);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    ManifestGrammar();
    AbiKey();
    Images images;
    for (int i = 1; i < argc; ++i) LoadDir(argv[i], images);
    int rc = 0;
    if (images.empty()) {
        std::printf("%s (in-process checks); SKIP: no corpus for the round trip\n", g_fail ? "FAILURES" : "ALL PASS");
        return g_fail ? 1 : 77;
    }
    // One image is enough for the round trip (the compile is ~10 s per image).
    while (images.size() > 1) images.erase(std::prev(images.end()));
    rc = RoundTrip(images);
    if (rc == 77) return g_fail ? 1 : 77;
    std::printf("%s\n", g_fail ? "FAILURES" : "ALL PASS");
    return g_fail ? 1 : 0;
}
