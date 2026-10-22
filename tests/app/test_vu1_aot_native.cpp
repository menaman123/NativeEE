// test_vu1_aot_native -- the GENERATED VU1 code against the interpreter over a corpus.
//
//   test_vu1_aot_native <case dir>... [--keep <dir>] [--opt 1|2] [--emit-excerpt <pc>] [--compiler <c++>]
//
// --compiler names the ONLY compiler tried (no default search): `--compiler /nonexistent` is
// the CI shape of a host without a toolchain (SKIP 77 after the in-process checks).
//
// For every image of the given directories (Vu1Case files): Vu1AotBuilder emits the
// image's blocks as C++, compiles them with the host toolchain found at run time, loads
// the library and re-verifies EVERY case of the image native vs interpreter (the same
// gate the specialised tier passed: registers, data, clock, bundles, kicked bytes).
// Prints one row per image (blocks, ops, compile ms, verify ms, status) and a total; 1 on
// any image that is not native. Before that, in-process and without any compiler: a fake
// exports table whose block function does nothing must be REFUSED by the gate (the
// re-verification has teeth), and the emitter's text is checked structurally.
//
// Exit 77 (SKIP) with no directory, no readable case, or no C++ compiler on the host, so
// a CI host without a toolchain stays green. The libraries are built under a temp root
// (deleted at exit) unless --keep names a directory: the bench's --native input.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/host/vu1_aot_builder.h"
#include "engine/vu/tiers/vu_block_tier.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu1_aot/vu1_aot_emitter.h"
#include "engine/vu/vu1_aot/vu1_aot_verifier.h"
#include "engine/vu/vu1_case.h"
#include "engine/vu/vu_unit.h"

namespace {

namespace fs = std::filesystem;

int g_fail = 0;
void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_fail;
}

// A block function that executes nothing: the gate must refuse it.
int BadBlock(VuUnit&, VuInterpreterTier&, bool& taken, uint32_t& target, int) {
    taken = false;
    target = 0;
    return 1;
}

size_t CountOf(const std::string& hay, const char* needle) {
    size_t n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1)) ++n;
    return n;
}

void LoadDir(const std::string& dir, std::map<uint64_t, std::vector<std::unique_ptr<Vu1Case>>>& images) {
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

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> dirs;
    std::string keep;
    std::string compiler;
    int opt = 1;
    uint32_t excerpt_pc = 0xFFFFFFFFu;
    Vu1AotBlockGen::Policy region;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--keep" && i + 1 < argc) keep = argv[++i];
        else if (a == "--opt" && i + 1 < argc) opt = std::atoi(argv[++i]);
        else if (a == "--region-blocks" && i + 1 < argc) region.block_cap = std::atoi(argv[++i]);
        else if (a == "--region-ops" && i + 1 < argc) region.op_cap = std::atoi(argv[++i]);
        else if (a == "--region-taken" && i + 1 < argc) region.taken_targets = std::atoi(argv[++i]) != 0;
        else if (a == "--chain" && i + 1 < argc) region.chain = std::atoi(argv[++i]) != 0;
        else if (a == "--compiler" && i + 1 < argc) compiler = argv[++i];
        else if (a == "--emit-excerpt" && i + 1 < argc) excerpt_pc = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 16));
        else dirs.push_back(a);
    }
    if (dirs.empty()) {
        std::printf("SKIP: no corpus directory given\n");
        return 77;
    }
    std::map<uint64_t, std::vector<std::unique_ptr<Vu1Case>>> images;
    for (const std::string& d : dirs) LoadDir(d, images);
    size_t total_cases = 0;
    for (const auto& kv : images) total_cases += kv.second.size();
    if (images.empty()) {
        std::printf("SKIP: no readable case under the given directories\n");
        return 77;
    }
    std::printf("corpus: %zu cases, %zu images\n", total_cases, images.size());

    // --- in-process, no compiler: the gate refuses code that does nothing ---
    {
        const auto& first = *images.begin();
        const Vu1Case& c0 = *first.second[0];
        VuBlockProgram scratch;
        VuBlockTier::DecodeImage(c0.micro_mem, Vu1Case::kMicroSize, scratch);
        scratch.hash = first.first;
        const VuBlock* blk = VuBlockTier::BuildBlock(scratch, (c0.start_pc & VuBlockTier::kPcMask) >> 3);
        Vu1AotBlockExport bad_block{blk->pc, static_cast<uint32_t>(blk->ops.size()), &BadBlock};
        Vu1AotImageExports bad;
        bad.abi_version = kVu1AotAbiVersion;
        bad.slot_size = sizeof(VuDecodedSlot);
        bad.image_hash = first.first;
        bad.engine_build = "test";
        bad.compiler = "none";
        bad.block_count = 1;
        bad.blocks = &bad_block;
        bad.flags_lazy = FlagPipeline::kDefaultLazy ? 1u : 0u;
        std::vector<const Vu1Case*> ptrs;
        for (const auto& c : first.second) ptrs.push_back(c.get());
        const Vu1AotVerifier::Result r = Vu1AotVerifier::Verify(ptrs, &bad);
        Check(!r.identical, "a do-nothing block function is refused by the re-verification gate");
        std::printf("     divergence: %s\n", r.divergence.c_str());
        const Vu1AotVerifier::Result ok = Vu1AotVerifier::Verify(ptrs, nullptr);
        Check(ok.identical, "the specialised tier (no exports) passes the same corpus");
        Check(!ok.block_pcs.empty(), "the native legs report the blocks they built");

        // --- the emitter's text, structurally ---
        std::vector<uint32_t> entries;
        for (const Vu1Case* c : ptrs) entries.push_back(c->start_pc & VuBlockTier::kPcMask);
        for (uint32_t pc : ok.block_pcs) entries.push_back(pc);
        const std::vector<uint32_t> pcs = Vu1AotEmitter::ReachableBlocks(scratch, entries, 8192);
        Vu1AotEmitter::Options eo;
        eo.engine_build = "test";
        eo.region = region;
        const Vu1AotEmitter::Output out = Vu1AotEmitter::Emit(scratch, pcs, eo);
        Check(out.blocks == pcs.size(), "one function per block");
        Check(CountOf(out.source, "\nint Blk_") == 2 * out.blocks, "one `int Blk_<pc>(` declaration + one definition per block in the text");
        Check(CountOf(out.source, "\n// region of ") == out.blocks, "one region function per block");
        Check(CountOf(out.source, "static constexpr VuDecodedSlot ") == out.ops, "one constexpr slot per op");
        Check(out.source.find("extern \"C\" const Vu1AotImageExports* " + out.entry_symbol + "()") != std::string::npos,
              "the extern \"C\" exports entry");
        Check(out.source.find("#include \"engine/vu/vu1_aot/vu1_aot_prelude.h\"") != std::string::npos, "includes the prelude only");
        Check(CountOf(out.source, "#include") == 1, "no other include");
        std::printf("     emitted: blocks=%u ops=%llu bytes=%zu\n", out.blocks, static_cast<unsigned long long>(out.ops), out.source.size());
        if (excerpt_pc != 0xFFFFFFFFu) {
            const VuBlock* b = VuBlockTier::BuildBlock(scratch, (excerpt_pc & VuBlockTier::kPcMask) >> 3);
            std::vector<uint32_t> emitted(pcs);
            std::sort(emitted.begin(), emitted.end());
            std::printf("%s", Vu1AotEmitter::EmitBlock(scratch, *b, "Blk_excerpt", emitted, region).c_str());
        }
    }

    // --- the toolchain path ---
    std::error_code ec;
    const bool keep_libs = !keep.empty();
    const std::string root = keep_libs ? keep : (fs::temp_directory_path(ec) / ("vu1_aot_native_test_" + std::to_string(ps2r::host::ProcessId()))).string();
    // One unit's tier to publish into (nothing kicks it; the test reads the census).
    VuUnit unit(1, nullptr);
    Vu1AotTier& tier = unit.dispatch().aot();
    ps2r::app::Vu1AotBuilder::Settings bs;
    bs.root = root;
    bs.build_id = "test-build";
    bs.opt_level = opt;
    bs.region = region;
    bs.compiler = compiler;
    bs.search_defaults = compiler.empty();
    ps2r::app::Vu1AotBuilder builder(tier, bs);
    std::string note;
    if (!builder.Open(&note)) {
        std::printf("SKIP: %s\n", note.c_str());
        return g_fail ? 1 : 77;
    }
    std::printf("%s\n", note.c_str());
    int native = 0;
    uint64_t compile_ns = 0, verify_ns = 0, blocks = 0, ops = 0;
    for (const auto& kv : images) {
        std::vector<const Vu1Case*> ptrs;
        for (const auto& c : kv.second) ptrs.push_back(c.get());
        const ps2r::app::Vu1AotBuilder::Built b = builder.BuildNow(ptrs, {});
        std::printf("image=%s cases=%zu status=%s blocks=%u ops=%llu compile_ms=%.0f verify_ms=%.0f%s%s%s\n",
                    Vu1AotEmitter::Hex16(kv.first).c_str(), ptrs.size(), b.status.c_str(), b.blocks, static_cast<unsigned long long>(b.ops),
                    static_cast<double>(b.compile_ns) / 1e6, static_cast<double>(b.verify_ns) / 1e6, b.cached ? " cached" : "",
                    b.reason.empty() ? "" : " reason=", b.reason.c_str());
        if (b.exports != nullptr) {
            ++native;
            compile_ns += b.compile_ns;
            verify_ns += b.verify_ns;
            blocks += b.blocks;
            ops += b.ops;
            // The cycle-cap sweep: the region functions' budget exits (a block
            // the cap will not hold is left to the loop, cut or whole) and the
            // loop's re-sync on the exit pc, against the interpreter under the
            // same cap. Every cap must be identical.
            // The oracle is the interpreter; a cap where the BLOCK tier itself
            // differs from the interpreter (a pre-existing cut-path item of the
            // loop, cycle only, e.g. corpus case 002 at cap 47) is judged against the
            // block tier and reported.
            static const uint64_t kCaps[] = {1, 2, 3, 4, 5, 7, 11, 17, 29, 47, 100, 333, 1000};
            bool caps_ok = true;
            int block_judged = 0;
            for (uint64_t cap : kCaps) {
                Vu1AotVerifier::Result rc = Vu1AotVerifier::Verify(ptrs, b.exports, cap);
                if (!rc.identical) {
                    const Vu1AotVerifier::Result rb = Vu1AotVerifier::Verify(ptrs, b.exports, cap, true);
                    if (rb.identical) {
                        std::printf("     cap=%llu: native == block tier; block tier vs interp: %s (pre-existing cut path)\n",
                                    static_cast<unsigned long long>(cap), rc.divergence.c_str());
                        ++block_judged;
                        continue;
                    }
                    std::printf("     cap=%llu divergence: %s\n", static_cast<unsigned long long>(cap), rc.divergence.c_str());
                    caps_ok = false;
                }
            }
            Check(caps_ok, "the loaded library is identical to the oracle under every cycle cap of the sweep");
            if (block_judged > 0) std::printf("     caps judged against the block tier: %d\n", block_judged);
        }
    }
    Check(native == static_cast<int>(images.size()), "every image of the corpus runs native-identical through its generated library");
    Check(tier.native_images() == images.size(), "every image's exports published to the tier");
    std::printf("total images=%zu native=%d cases=%zu blocks=%llu ops=%llu compile_ms=%.0f verify_ms=%.0f root=%s\n", images.size(), native,
                total_cases, static_cast<unsigned long long>(blocks), static_cast<unsigned long long>(ops), static_cast<double>(compile_ns) / 1e6,
                static_cast<double>(verify_ns) / 1e6, root.c_str());
    std::printf("census: %s\n", builder.CensusRow().c_str());
    builder.Close();
    if (!keep_libs) fs::remove_all(root, ec);  // the mapped libraries survive the unlink until the builder unmaps them
    std::printf("%s\n", g_fail ? "FAILURES" : "ALL PASS");
    return g_fail ? 1 : 0;
}
