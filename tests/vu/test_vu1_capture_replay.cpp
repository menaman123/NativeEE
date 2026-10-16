// Vu1CaptureReplay: replay one recorded VU1 program run (a Vu1Case file written by a title
// composition's capture probe; the file lives OUTSIDE the tree -- no game bytes here) on a fresh
// VuUnit through the interpreter tier and print what the GS would see: every XGKICKed packet
// decoded (GsPacketDiff::Print), the run summary (bundles, cycles, kicks, TPC), and optionally a
// per-bundle register trace for a reference evaluator to diff against.
//
//   test_vu1_capture_replay <case.bin>              kicks + summary
//   test_vu1_capture_replay <case.bin> --trace      one line per retired bundle: the whole register
//                                                   file after that bundle (fresh unit per step,
//                                                   RunOptions{cycle_limit = k}, pipes drained)
//   test_vu1_capture_replay <case.bin> --cached     the cached tier instead of the interpreter
//
// Exit 77 (SKIP) with no argument or an unreadable case; 1 on a decode error in the kicked stream.
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu1_case.h"
#include "engine/vu/vu_unit.h"
#include "tools/vu1_replay/gs_packet_diff.h"
#include "tools/vu1_replay/xgk_capture_sink.h"

namespace {

struct TestVu1CaptureReplay {
    RegisterOpStream ops;
    GifRegs regs;
    GifParser parser;
    GifArbiter arbiter;
    XgkCaptureSink sink;
    TestVu1CaptureReplay() : parser(ops, regs), arbiter(parser) { sink.Attach(arbiter); }
};

void PrintRegs(const VUregs& r, uint32_t pc, uint64_t k) {
    std::printf("k=%llu pc=%04x", static_cast<unsigned long long>(k), pc);
    for (int i = 1; i < 32; ++i) std::printf(" VF%02d=%08x,%08x,%08x,%08x", i, r.VF[i].I[0], r.VF[i].I[1], r.VF[i].I[2], r.VF[i].I[3]);
    for (int i = 1; i < 16; ++i) std::printf(" VI%02d=%04x", i, unsigned(r.VI[i]));
    uint32_t q, p;
    std::memcpy(&q, &r.Q, 4);
    std::memcpy(&p, &r.P, 4);
    std::printf(" ACC=%08x,%08x,%08x,%08x Q=%08x P=%08x I=%08x R=%08x MAC=%04x STAT=%04x CLIP=%06x\n", r.ACC.I[0], r.ACC.I[1],
                r.ACC.I[2], r.ACC.I[3], q, p, r.I, r.R, unsigned(r.mac_flag), unsigned(r.status_flag), r.clip_flag);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("SKIP: no capture given (usage: test_vu1_capture_replay <case.bin> [--trace] [--cached])\n");
        return 77;
    }
    Vu1Case c;
    if (!vu1_case::ReadCase(argv[1], c)) {
        std::printf("SKIP: capture not readable: %s\n", argv[1]);
        return 77;
    }
    bool trace = false, cached = false;
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--trace") trace = true;
        if (std::string(argv[i]) == "--cached") cached = true;
    }
    const VuTierMode mode = cached ? VuTierMode::Cached : VuTierMode::Interp;

    if (trace) {
        // Step k = 1, 2, ... bundles on a fresh unit each time until the run stops growing.
        uint64_t last_bundles = 0;
        for (uint64_t k = 1; k < 200000; ++k) {
            TestVu1CaptureReplay rig;
            VuUnit vu(1, &rig.arbiter);
            vu.LoadCase(c);
            VuInterpreterTier::RunOptions opt;
            opt.cycle_limit = k;
            vu.dispatch().interp().Run(vu, c.start_pc, opt);
            const uint64_t b = vu.dispatch().bundles();
            if (b == last_bundles) break;
            last_bundles = b;
            PrintRegs(vu.regs(), vu.state().cur_bundle_pc, k);
        }
        std::printf("trace bundles=%llu\n", static_cast<unsigned long long>(last_bundles));
        return 0;
    }

    TestVu1CaptureReplay rig;
    VuUnit vu(1, &rig.arbiter);
    vu.LoadCase(c);
    vu.dispatch().settings().vu1_mode = mode;
    vu.Execute(c.start_pc);
    rig.arbiter.drain();
    const GsPacketDiff::Stream s = GsPacketDiff::Decode(rig.sink.kicks());
    std::printf("case %s start_pc=0x%04x mode=%s bundles=%llu cycles=%llu kicks=%d kick_bytes=%zu TPC=0x%04x\n", argv[1], c.start_pc,
                cached ? "cached" : "interp", static_cast<unsigned long long>(vu.dispatch().bundles()),
                static_cast<unsigned long long>(vu.cycle() - c.vu_cycle), vu.state().xgkick_count, rig.sink.kick_bytes(), vu.regs().TPC);
    GsPacketDiff::Print(s, stdout);
    PrintRegs(vu.regs(), vu.state().cur_bundle_pc, vu.dispatch().bundles());
    if (!s.errors.empty()) {
        for (const auto& e : s.errors) std::printf("decode error: %s\n", e.c_str());
        return 1;
    }
    return 0;
}
