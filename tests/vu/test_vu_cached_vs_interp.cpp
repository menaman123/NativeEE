// VuCachedVsInterpCorpus: every recorded VU1 case of the frozen corpus
// directory run through the cached tier, the block tier, the AOT tier (every
// image assumed verified: the specialised bodies on every op) and the
// interpreter tier on fresh units (cold program table each leg) -> identical
// post-run registers (the whole 608-byte file), data memory, clock, bundle
// count and kicked register-op stream (cached / block / aot vs interp); then the
// three retail VU0 fixture images from their entry points through the cached
// and interpreter tiers the same way (VU0 never runs on the block tier). SKIP 77 when the corpus
// directory is absent (the fixtures always run).
#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu1_case.h"
#include "engine/vu/vu_unit.h"

namespace {

struct TestVuCachedVsInterp {
    RegisterOpStream ops;
    GifRegs regs;
    GifParser parser;
    GifArbiter arbiter;
    TestVuCachedVsInterp() : parser(ops, regs), arbiter(parser) {}
};

struct Outcome {
    VUregs regs;
    std::vector<uint8_t> data;
    uint64_t cycle = 0;
    uint64_t bundles = 0;
    uint64_t ophash = 0;
    size_t opcount = 0;
    int kicks = 0;
};

Outcome RunLeg(const Vu1Case& c, VuTierMode mode) {
    TestVuCachedVsInterp rig;
    VuUnit vu(1, &rig.arbiter);
    vu.LoadCase(c);
    vu.dispatch().settings().vu1_mode = mode;
    vu.dispatch().aot().set_assume_verified(mode == VuTierMode::Aot);
    vu.Execute(c.start_pc);
    rig.arbiter.drain();
    Outcome o;
    o.regs = vu.regs();
    o.data.assign(vu.state().data, vu.state().data + Vu1Case::kDataSize);
    o.cycle = vu.cycle();
    o.bundles = vu.dispatch().bundles();
    o.ophash = rig.ops.Hash();
    o.opcount = rig.ops.Count();
    o.kicks = vu.state().xgkick_count;
    return o;
}

Outcome RunVu0Leg(const uint8_t* code, const uint8_t* data, uint32_t entry, VuTierMode mode) {
    VuUnit vu(0);
    vu.WriteMicroMem(0, code, 4096);
    vu.WriteDataMem(0, data, 4096);
    vu.dispatch().settings().vu1_mode = mode;
    vu.dispatch().set_cycle_cap(200000);
    vu.Execute(entry);
    Outcome o;
    o.regs = vu.regs();
    o.data.assign(vu.state().data, vu.state().data + 4096);
    o.cycle = vu.cycle();
    o.bundles = vu.dispatch().bundles();
    return o;
}

bool Same(const Outcome& a, const Outcome& b, const char* tag) {
    bool ok = true;
    if (std::memcmp(&a.regs, &b.regs, sizeof(VUregs)) != 0) {
        ok = false;
        for (int r = 0; r < 32; ++r) {
            if (std::memcmp(&a.regs.VF[r], &b.regs.VF[r], 16) != 0)
                std::printf("  %s VF%02d cached=%08x,%08x,%08x,%08x interp=%08x,%08x,%08x,%08x\n", tag, r, a.regs.VF[r].I[0],
                            a.regs.VF[r].I[1], a.regs.VF[r].I[2], a.regs.VF[r].I[3], b.regs.VF[r].I[0], b.regs.VF[r].I[1],
                            b.regs.VF[r].I[2], b.regs.VF[r].I[3]);
        }
        for (int r = 0; r < 16; ++r) {
            if (a.regs.VI[r] != b.regs.VI[r]) std::printf("  %s VI%02d cached=%04x interp=%04x\n", tag, r, a.regs.VI[r], b.regs.VI[r]);
        }
        if (a.regs.TPC != b.regs.TPC) std::printf("  %s TPC cached=%04x interp=%04x\n", tag, a.regs.TPC, b.regs.TPC);
        if (a.regs.status_flag != b.regs.status_flag || a.regs.mac_flag != b.regs.mac_flag || a.regs.clip_flag != b.regs.clip_flag)
            std::printf("  %s status/mac/clip cached=%04x/%04x/%06x interp=%04x/%04x/%06x\n", tag, a.regs.status_flag,
                        a.regs.mac_flag, a.regs.clip_flag, b.regs.status_flag, b.regs.mac_flag, b.regs.clip_flag);
        std::printf("  %s regs DIFFER\n", tag);
    }
    if (a.data != b.data) {
        size_t n = 0;
        for (size_t i = 0; i < a.data.size(); ++i) n += a.data[i] != b.data[i];
        std::printf("  %s data_diff_bytes=%zu\n", tag, n);
        ok = false;
    }
    if (a.cycle != b.cycle) {
        std::printf("  %s cycle cached=%llu interp=%llu\n", tag, static_cast<unsigned long long>(a.cycle),
                    static_cast<unsigned long long>(b.cycle));
        ok = false;
    }
    if (a.bundles != b.bundles) {
        std::printf("  %s bundles cached=%llu interp=%llu\n", tag, static_cast<unsigned long long>(a.bundles),
                    static_cast<unsigned long long>(b.bundles));
        ok = false;
    }
    if (a.ophash != b.ophash || a.opcount != b.opcount || a.kicks != b.kicks) {
        std::printf("  %s kicks cached=%d/%zu/%016llx interp=%d/%zu/%016llx\n", tag, a.kicks, a.opcount,
                    static_cast<unsigned long long>(a.ophash), b.kicks, b.opcount, static_cast<unsigned long long>(b.ophash));
        ok = false;
    }
    return ok;
}

std::vector<std::string> ListCases(const char* dir) {
    std::vector<std::string> out;
    DIR* d = opendir(dir);
    if (d == nullptr) return out;
    while (dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".bin") == 0) out.push_back(std::string(dir) + "/" + name);
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

bool ReadFile(const std::string& path, uint8_t* dst, size_t n) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    const size_t got = std::fread(dst, 1, n, f);
    std::fclose(f);
    return got == n;
}

}  // namespace

int main(int argc, char** argv) {
    int failures = 0;
    // VU0 fixtures: the retail captures are private fixtures (argv[2] names their directory;
    // absent -> that image is skipped, and the run is a SKIP unless something else failed).
    bool fixtures_skipped = false;
    if (argc > 2) {
        static const char* kImages[3] = {"vu0_shadow", "vu0_vis", "vu0_decal"};
        static const uint32_t kEntries[3][2] = {{0xA08, 0xB90}, {0x1C0, 0x1C0}, {0x1C0, 0x1C0}};
        static uint8_t code[4096], data[4096];
        for (int i = 0; i < 3; ++i) {
            const std::string base = std::string(argv[2]) + "/" + kImages[i];
            if (!ReadFile(base + "_code.bin", code, sizeof(code)) || !ReadFile(base + "_data.bin", data, sizeof(data))) {
                std::printf("SKIP: private fixture absent: %s_code.bin\n", base.c_str());
                fixtures_skipped = true;
                continue;
            }
            for (int e = 0; e < 2; ++e) {
                if (e == 1 && kEntries[i][1] == kEntries[i][0]) break;
                const Outcome c = RunVu0Leg(code, data, kEntries[i][e], VuTierMode::Cached);
                const Outcome in = RunVu0Leg(code, data, kEntries[i][e], VuTierMode::Interp);
                char tag[64];
                std::snprintf(tag, sizeof(tag), "%s@%04x", kImages[i], kEntries[i][e]);
                const bool same = Same(c, in, tag);
                if (!same) ++failures;
                std::printf("  VU0 %-18s bundles=%llu cycles=%llu TPC=%04x %s\n", tag, static_cast<unsigned long long>(c.bundles),
                            static_cast<unsigned long long>(c.cycle), c.regs.TPC, same ? "identical" : "DIFFER");
            }
        }
    }
    // The VU1 corpus.
    const std::vector<std::string> cases = argc > 1 ? ListCases(argv[1]) : std::vector<std::string>();
    if (cases.empty()) {
        if (failures) {
            std::printf("VuCachedVsInterpCorpus: %d failure(s) (corpus absent)\n", failures);
            return 1;
        }
        std::printf("SKIP: corpus directory absent or empty: %s\n", argc > 1 ? argv[1] : "(none)");
        return 77;
    }
    size_t same_n = 0;
    Vu1Case* c = new Vu1Case();
    for (const std::string& path : cases) {
        if (!vu1_case::ReadCase(path.c_str(), *c)) {
            std::printf("  unreadable case %s\n", path.c_str());
            ++failures;
            continue;
        }
        const std::string name = path.substr(path.find_last_of('/') + 1);
        const Outcome cached = RunLeg(*c, VuTierMode::Cached);
        const Outcome interp = RunLeg(*c, VuTierMode::Interp);
        const Outcome block = RunLeg(*c, VuTierMode::Block);
        const Outcome aot = RunLeg(*c, VuTierMode::Aot);
        const bool same_c = Same(cached, interp, name.c_str());
        const bool same_b = Same(block, interp, (name + " [block]").c_str());
        const bool same_a = Same(aot, interp, (name + " [aot]").c_str());
        const bool same = same_c && same_b && same_a;
        if (same) ++same_n; else ++failures;
        std::printf("COMPARE %s: pc=0x%04x bundles=%llu cycles=%llu kicks=%d ops=%zu ophash=%016llx %s (cached, block, aot vs interp)\n",
                    same ? "PASS" : "FAIL", c->start_pc, static_cast<unsigned long long>(cached.bundles),
                    static_cast<unsigned long long>(cached.cycle - c->vu_cycle), cached.kicks, cached.opcount,
                    static_cast<unsigned long long>(cached.ophash), name.c_str());
    }
    delete c;
    std::printf("VuCachedVsInterpCorpus: cases=%zu identical=%zu failures=%d%s\n", cases.size(), same_n, failures,
                fixtures_skipped ? " (VU0 fixtures SKIPPED: private)" : "");
    if (failures) return 1;
    return fixtures_skipped ? 77 : 0;
}
