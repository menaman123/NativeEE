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
//   test_vu1_capture_replay <case.bin> --block      the block tier
//   test_vu1_capture_replay <case.bin> --aot        the AOT tier, the image assumed verified
//   test_vu1_capture_replay --bench N [--cached|--block|--aot] <case.bin>...
//                                                   microbenchmark: every case replayed N times on ONE
//                                                   unit, 5 rounds, the fastest round reported (registers
//                                                   and data memory restored before each replay, the image
//                                                   left resident as between a title's kicks; only Execute
//                                                   is timed); per case the mean ns per replay and ns per
//                                                   bundle, then the aggregate bundles/s. No output
//                                                   comparison: pair it with the plain mode for identity.
//
// Exit 77 (SKIP) with no argument or an unreadable case; 1 on a decode error in the kicked stream.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu1_case.h"
#include "engine/vu/vu_float.h"
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

// The run-to-run restore of a case WITHOUT touching micro memory: registers, data memory, the VIF
// tops, the Q/P latches and the clock (LoadCase's fields minus the image), so the decoded program
// stays resident across replays exactly as it does between a title's kicks.
void RestoreRegsAndData(VuUnit& vu, const Vu1Case& c) {
    std::memcpy(&vu.regs(), &c.regs, sizeof(VUregs));
    vu.WriteDataMem(0, c.data_mem, Vu1Case::kDataSize);
    vu.SetVifTop(static_cast<uint16_t>(c.vif1_top), static_cast<uint16_t>(c.vif1_itops));
    vu.flags().SetQPending(c.q_pending_valid != 0, c.q_pending_retire, VuFloat::Bits2F(c.q_pending_value_bits),
                           static_cast<uint16_t>(c.q_pending_status));
    vu.flags().SetPPending(c.p_pending_valid != 0, c.p_pending_retire, VuFloat::Bits2F(c.p_pending_value_bits));
    vu.flags().SetCycle(c.vu_cycle);
}

const char* ModeName(VuTierMode mode) {
    return mode == VuTierMode::Cached ? "cached" : mode == VuTierMode::Block ? "block" : mode == VuTierMode::Aot ? "aot" : "interp";
}

// The mode on a unit: the AOT tier runs every image native here (no manifest in a tool).
void SetMode(VuUnit& vu, VuTierMode mode) {
    vu.dispatch().settings().vu1_mode = mode;
    vu.dispatch().aot().set_assume_verified(mode == VuTierMode::Aot);
}

// --bench: N replays of every case on one unit; the mean ns per replay and per bundle, then the aggregate.
int RunBench(const std::vector<std::string>& files, int n, VuTierMode mode) {
    constexpr int kRounds = 5;
    if (files.empty() || n <= 0) {
        std::printf("SKIP: --bench needs N > 0 and at least one case\n");
        return 77;
    }
    TestVu1CaptureReplay rig;
    VuUnit vu(1, &rig.arbiter);
    SetMode(vu, mode);
    double total_ns = 0.0;
    double total_bundles = 0.0;
    for (const std::string& f : files) {
        Vu1Case c;
        if (!vu1_case::ReadCase(f.c_str(), c)) {
            std::printf("SKIP: capture not readable: %s\n", f.c_str());
            return 77;
        }
        vu.LoadCase(c);  // the image hash + decode happen once: the game's kicks find the image resident
        vu.Execute(c.start_pc);
        rig.arbiter.drain();
        rig.sink.Clear();
        // kRounds rounds of n replays; the fastest round is the number (a loaded host only adds time).
        double ns = 0.0;
        double bundles = 0.0;
        for (int round = 0; round < kRounds; ++round) {
            const uint64_t b0 = vu.dispatch().bundles();
            double round_ns = 0.0;
            for (int i = 0; i < n; ++i) {
                RestoreRegsAndData(vu, c);  // micro memory is unchanged: no epoch bump, no re-hash
                const auto t0 = std::chrono::steady_clock::now();
                vu.Execute(c.start_pc);
                const auto t1 = std::chrono::steady_clock::now();
                round_ns += static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
                rig.arbiter.drain();  // the GS side (parse into the sink) is not timed
                rig.sink.Clear();
            }
            if (round == 0 || round_ns < ns) {
                ns = round_ns;
                bundles = static_cast<double>(vu.dispatch().bundles() - b0);
            }
        }
        std::printf("bench %s start_pc=0x%04x replays=%d bundles/replay=%.0f ns/replay=%.0f ns/bundle=%.2f\n", f.c_str(),
                    c.start_pc, n, bundles / n, ns / n, bundles > 0 ? ns / bundles : 0.0);
        total_ns += ns;
        total_bundles += bundles;
    }
    std::printf("bench total mode=%s cases=%zu replays=%d(min of %d rounds) bundles=%.0f ns=%.0f ns/bundle=%.2f Mbundles/s=%.1f\n",
                ModeName(mode), files.size(), n, kRounds, total_bundles, total_ns,
                total_bundles > 0 ? total_ns / total_bundles : 0.0, total_ns > 0 ? total_bundles * 1e3 / total_ns : 0.0);
    if (mode == VuTierMode::Block || mode == VuTierMode::Aot) {
        const VuBlockTier::Stats& bs = vu.dispatch().block().stats();
        std::printf("bench block stats blocks_run=%llu ops_static=%llu ops_model=%llu blocks_built=%llu ops/block=%.2f static=%.1f%%\n",
                    static_cast<unsigned long long>(bs.blocks_run), static_cast<unsigned long long>(bs.ops_static),
                    static_cast<unsigned long long>(bs.ops_model), static_cast<unsigned long long>(bs.blocks_built),
                    bs.blocks_run ? static_cast<double>(bs.ops_static + bs.ops_model) / static_cast<double>(bs.blocks_run) : 0.0,
                    (bs.ops_static + bs.ops_model) ? 100.0 * static_cast<double>(bs.ops_static) / static_cast<double>(bs.ops_static + bs.ops_model) : 0.0);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("SKIP: no capture given (usage: test_vu1_capture_replay <case.bin> [--trace] [--cached|--block|--aot] | --bench N [--cached|--block|--aot] <case.bin>...)\n");
        return 77;
    }
    bool trace = false, cached = false, block = false, aot = false;
    int bench = 0;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--trace") trace = true;
        else if (a == "--cached") cached = true;
        else if (a == "--block") block = true;
        else if (a == "--aot") aot = true;
        else if (a == "--bench" && i + 1 < argc) bench = std::atoi(argv[++i]);
        else files.push_back(a);
    }
    const VuTierMode mode = aot ? VuTierMode::Aot : block ? VuTierMode::Block : cached ? VuTierMode::Cached : VuTierMode::Interp;
    if (bench > 0) return RunBench(files, bench, mode);
    if (files.empty()) {
        std::printf("SKIP: no capture given\n");
        return 77;
    }
    Vu1Case c;
    if (!vu1_case::ReadCase(files[0].c_str(), c)) {
        std::printf("SKIP: capture not readable: %s\n", files[0].c_str());
        return 77;
    }
    const char* case_name = files[0].c_str();

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
    SetMode(vu, mode);
    vu.Execute(c.start_pc);
    rig.arbiter.drain();
    const GsPacketDiff::Stream s = GsPacketDiff::Decode(rig.sink.kicks());
    std::printf("case %s start_pc=0x%04x mode=%s bundles=%llu cycles=%llu kicks=%d kick_bytes=%zu TPC=0x%04x\n", case_name, c.start_pc,
                ModeName(mode), static_cast<unsigned long long>(vu.dispatch().bundles()),
                static_cast<unsigned long long>(vu.cycle() - c.vu_cycle), vu.state().xgkick_count, rig.sink.kick_bytes(), vu.regs().TPC);
    GsPacketDiff::Print(s, stdout);
    PrintRegs(vu.regs(), vu.state().cur_bundle_pc, vu.dispatch().bundles());
    if (!s.errors.empty()) {
        for (const auto& e : s.errors) std::printf("decode error: %s\n", e.c_str());
        return 1;
    }
    return 0;
}
