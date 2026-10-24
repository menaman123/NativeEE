// VuLockstepChain: the in-game tier A/B (VuLockstep) over a CHAIN of recorded VU1 kicks -- the
// consecutive Vu1Case files a title composition's capture probe wrote for one stretch of a run
// (case_<nnn>_<pc>.bin in ordinal order, each the complete unit state at its kick: registers, both
// memories, TOP/ITOP, the Q/P latches, the clock -- so the inter-kick VIF writes and the previous
// kick's stores are in the next case's memory), optionally with the probe's recorded PATH1 packets
// (kicks_<nnn>.bin). Every case is restored into ONE unit whose dispatch runs the configured tier
// with the lockstep on: the tier and the interpreter reference run the kick side by side and the
// end states (registers, data memory, clock, latches, bundles, kicked bytes) must agree; when the
// recording is present the tier's packets must also equal the game's own. The files live OUTSIDE
// the tree (no game bytes here).
//
//   test_vu_lockstep <chain dir> [--aot | --block] [--native <libdir>] [--limit N]
//     --aot (default)    the AOT tier, every image assumed verified; with --native the generated
//                        library of each case's image is loaded from <libdir>/<hash>.O1|O2.<ext>
//                        (built by test_vu1_aot_native --keep or tools/vu1_aot_pack) -- without it
//                        the specialised block table runs (the generated code is not exercised)
//     --block            the block tier
//
// Exit 0 = every case identical (and equal to the recording where present); 1 = a divergence;
// 77 = SKIP (no directory, no case files, or a missing library under --native).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gif/null_gs_sink.h"
#include "engine/platform/host/platform.h"
#include "engine/vu/tiers/vu_lockstep.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu1_aot/vu1_aot_emitter.h"
#include "engine/vu/vu1_aot/vu1_aot_exports.h"
#include "engine/vu/vu1_aot/vu1_aot_verifier.h"
#include "engine/vu/vu1_case.h"
#include "engine/vu/vu_unit.h"

namespace {

namespace fs = std::filesystem;

// The rig: the GS side the unit's XGKICKs reach (a null parser behind the arbiter), the tier's
// packets of the current kick in the probe's [u32 size][bytes] shape, the loaded libraries.
struct TestVuLockstep {
    NullGsSink sink;
    GifRegs gif_regs;
    GifParser parser;
    GifArbiter arbiter;
    std::vector<uint8_t> bytes;
    std::vector<ps2r::host::DynamicLibrary> libs;
    TestVuLockstep() : parser(sink, gif_regs), arbiter(parser) { arbiter.SetSubmitObserver(&TestVuLockstep::OnSubmit, this); }
    static void OnSubmit(void* user, GifPath path, const uint8_t* data, size_t size) {
        if (path != GifPath::Path1) return;
        TestVuLockstep* r = static_cast<TestVuLockstep*>(user);
        const uint32_t n = static_cast<uint32_t>(size);
        const uint8_t* np = reinterpret_cast<const uint8_t*>(&n);
        r->bytes.insert(r->bytes.end(), np, np + 4);
        r->bytes.insert(r->bytes.end(), data, data + size);
    }
    // <libdir>/<hash>.O1.<ext>, then .O2, then the bare name -> the exports handed to the unit's AOT
    // tier. 0 = installed or already there, 77 = no usable library.
    int InstallNative(VuUnit& vu, const Vu1Case& c, const std::string& libdir);
};

bool ReadWhole(const std::string& path, std::vector<uint8_t>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    out.clear();
    uint8_t buf[16384];
    for (;;) {
        const size_t n = std::fread(buf, 1, sizeof(buf), f);
        if (n == 0) break;
        out.insert(out.end(), buf, buf + n);
    }
    std::fclose(f);
    return true;
}

int TestVuLockstep::InstallNative(VuUnit& vu, const Vu1Case& c, const std::string& libdir) {
    const uint64_t hash = Vu1AotVerifier::ImageHashOf(c);
    if (vu.dispatch().aot().native_exports(hash) != nullptr) return 0;
    const std::string base = libdir + "/" + Vu1AotEmitter::Hex16(hash);
    const std::string ext = ps2r::host::DynamicLibraryExtension();
    ps2r::host::DynamicLibrary lib;
    std::string err, path;
    bool opened = false;
    for (const char* mid : {".O1.", ".O2.", "."}) {
        path = base + mid + ext;
        if ((opened = ps2r::host::DynamicLibraryOpen(&lib, path.c_str(), &err))) break;
    }
    if (!opened) {
        std::printf("SKIP: no generated library for image %s under %s (%s)\n", Vu1AotEmitter::Hex16(hash).c_str(), libdir.c_str(), err.c_str());
        return 77;
    }
    using EntryFn = const Vu1AotImageExports* (*)();
    const EntryFn entry = reinterpret_cast<EntryFn>(ps2r::host::DynamicLibrarySymbol(lib, Vu1AotEmitter::EntrySymbol(hash).c_str()));
    const Vu1AotImageExports* ex = entry ? entry() : nullptr;
    if (ex == nullptr || ex->abi_version != kVu1AotAbiVersion || ex->slot_size != sizeof(VuDecodedSlot) || ex->image_hash != hash) {
        std::printf("SKIP: %s is not a usable library for image %s\n", path.c_str(), Vu1AotEmitter::Hex16(hash).c_str());
        ps2r::host::DynamicLibraryClose(lib);
        return 77;
    }
    libs.push_back(lib);
    vu.dispatch().aot().SetNativeExports(hash, ex);
    std::printf("native library %s: %u blocks\n", path.c_str(), ex->block_count);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string dir, libdir;
    bool block = false;
    size_t limit = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--aot") block = false;
        else if (a == "--block") block = true;
        else if (a == "--native" && i + 1 < argc) libdir = argv[++i];
        else if (a == "--limit" && i + 1 < argc) limit = static_cast<size_t>(std::atoll(argv[++i]));
        else dir = a;
    }
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) {
        std::printf("SKIP: no chain directory (usage: test_vu_lockstep <dir> [--aot|--block] [--native <libdir>])\n");
        return 77;
    }
    // case_<nnn>_<pc>.bin in ordinal order (the probe's names sort by ordinal).
    std::vector<std::string> cases;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string name = e.path().filename().string();
        if (name.compare(0, 5, "case_") == 0 && name.size() > 4 && name.compare(name.size() - 4, 4, ".bin") == 0) cases.push_back(name);
    }
    std::sort(cases.begin(), cases.end());
    if (limit != 0 && cases.size() > limit) cases.resize(limit);
    if (cases.empty()) {
        std::printf("SKIP: no case_*.bin under %s\n", dir.c_str());
        return 77;
    }

    TestVuLockstep rig;
    VuUnit vu(1, &rig.arbiter);
    VuSettings& settings = vu.dispatch().settings();
    settings.vu1_mode = block ? VuTierMode::Block : VuTierMode::Aot;
    settings.hle_enabled = false;
    settings.lockstep = true;
    vu.dispatch().aot().set_assume_verified(!block);

    size_t recorded = 0, recorded_equal = 0;
    uint64_t divergent_before = 0;
    int rc = 0;
    for (const std::string& name : cases) {
        Vu1Case c;
        if (!vu1_case::ReadCase((dir + "/" + name).c_str(), c)) {
            std::printf("SKIP: capture not readable: %s\n", name.c_str());
            return 77;
        }
        vu.LoadCase(c);
        if (!block && !libdir.empty()) {
            const int r = rig.InstallNative(vu, c, libdir);
            if (r != 0) return r;
        }
        rig.bytes.clear();
        vu.Execute(c.start_pc);  // the dispatch: lockstep -> tier + reference, the diff printed on stderr
        rig.arbiter.drain();
        const VuLockstep* ls = vu.dispatch().lockstep();
        const uint64_t divergent = ls != nullptr ? ls->stats().divergent : 0;
        if (divergent != divergent_before) {
            std::printf("FAIL %s: the tier and the interpreter parted (see vu1_lockstep: above)\n", name.c_str());
            rc = 1;
        }
        divergent_before = divergent;
        // The game's own recording of this kick, when the probe wrote one: kicks_<nnn>.bin.
        const std::string ord = name.substr(5, name.find('_', 5) - 5);
        std::vector<uint8_t> game;
        if (ReadWhole(dir + "/kicks_" + ord + ".bin", game)) {
            ++recorded;
            if (game == rig.bytes) {
                ++recorded_equal;
            } else {
                std::printf("FAIL %s: the tier's packets (%zu bytes) differ from the recording kicks_%s.bin (%zu bytes)\n", name.c_str(),
                            rig.bytes.size(), ord.c_str(), game.size());
                rc = 1;
            }
        }
    }
    const VuLockstep* ls = vu.dispatch().lockstep();
    std::printf("chain %s mode=%s%s cases=%zu lockstep %s recorded=%zu equal=%zu -> %s\n", dir.c_str(), block ? "block" : "aot",
                libdir.empty() ? "" : " native", cases.size(), ls != nullptr ? ls->Summary().c_str() : "(no kick)", recorded,
                recorded_equal, rc == 0 ? "ALL PASS" : "FAIL");
    return rc;
}
