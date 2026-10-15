// VuCachedSuperblock: the cached tier against the interpreter reference on
// hand-encoded programs (fresh unit per leg, cached then interp -> identical
// registers, data memory, clock and kicked register-op stream), the structural
// decode of the program table (stall / fold classes, run lengths, span_rem
// recomputed independently), the superblock rules (run entry needs run_len
// >= 3; XTOP / E bit / I bit inside a run; the warmup positions; the span
// close materialisation landing a stall on the terminal; the cycle-cap
// backstop mid-span with shifting offsets; XGKICK pacing firing inside a run
// and inside a span), the epoch fast path and the 64-entry eviction.
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/vu/span_classifier.h"
#include "engine/vu/tiers/vu_cached_tier.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu_unit.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

namespace {

constexpr uint32_t U_NOP = 0x000002FFu;
constexpr uint32_t L_NOP = 0x8000033Cu;
constexpr uint32_t U_EBIT = 0x40000000u;
constexpr uint32_t U_IBIT = 0x80000000u;
constexpr uint32_t F_ADD = 0x28, F_MUL = 0x2A, F_SUB = 0x2C;

struct Bundle {
    uint32_t lower, upper;
};

uint32_t EncUpper(uint32_t func, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t dest) {
    return func | (fd << 6) | (fs << 11) | (ft << 16) | (dest << 21);
}
uint32_t EncIaddiu(uint32_t it, uint32_t is, uint32_t imm15) {
    return (0x08u << 25) | (((imm15 >> 11) & 0xFu) << 21) | (it << 16) | (is << 11) | (imm15 & 0x7FFu);
}
uint32_t EncIswX(uint32_t it, uint32_t is, int32_t imm11) {
    return (0x05u << 25) | (0x8u << 21) | (it << 16) | (is << 11) | (static_cast<uint32_t>(imm11) & 0x7FFu);
}
uint32_t EncIlw(uint32_t it, uint32_t is, int32_t imm11) {
    return (0x04u << 25) | (0x8u << 21) | (it << 16) | (is << 11) | (static_cast<uint32_t>(imm11) & 0x7FFu);
}
uint32_t EncDiv(uint32_t fs, uint32_t fsf, uint32_t ft, uint32_t ftf) {
    return (0x40u << 25) | (ftf << 23) | (fsf << 21) | (ft << 16) | (fs << 11) | (0x0Eu << 6) | 0x3Cu;
}
uint32_t EncWaitQ() { return (0x40u << 25) | (0x0Eu << 6) | 0x3Fu; }
uint32_t EncXtop(uint32_t it) { return (0x40u << 25) | (it << 16) | (0x1Au << 6) | 0x3Cu; }
uint32_t EncXgkick(uint32_t is) { return (0x40u << 25) | (is << 11) | (0x1Bu << 6) | 0x3Cu; }
uint32_t EncB(int32_t imm11) { return (0x20u << 25) | (static_cast<uint32_t>(imm11) & 0x7FFu); }
uint32_t EncIbne(uint32_t it, uint32_t is, int32_t imm11) {
    return (0x29u << 25) | (it << 16) | (is << 11) | (static_cast<uint32_t>(imm11) & 0x7FFu);
}

struct TestVuCachedTier {
    RegisterOpStream ops;
    GifRegs regs;
    GifParser parser;
    GifArbiter arbiter;
    TestVuCachedTier() : parser(ops, regs), arbiter(parser) {}
};

void WriteProgram(VuUnit& vu, const std::vector<Bundle>& b, uint32_t base = 0) {
    for (size_t i = 0; i < b.size(); ++i) {
        uint8_t raw[8];
        std::memcpy(raw, &b[i].lower, 4);
        std::memcpy(raw + 4, &b[i].upper, 4);
        vu.WriteMicroMem(base + static_cast<uint32_t>(i) * 8, raw, 8);
    }
}

struct Outcome {
    VUregs regs;
    uint8_t data[16384];
    uint64_t cycle = 0;
    uint64_t bundles = 0;
    uint64_t ophash = 0;
    size_t opcount = 0;
    bool suspended = false;
};

using Setup = void (*)(VuUnit&);

Outcome RunMode(VuTierMode mode, const std::vector<Bundle>& prog, int unit_id = 1, Setup setup = nullptr,
                uint32_t start_pc = 0, uint64_t cycle_cap = 1000000) {
    TestVuCachedTier rig;
    VuUnit vu(unit_id, unit_id == 1 ? &rig.arbiter : nullptr);
    WriteProgram(vu, prog);
    if (setup) setup(vu);
    vu.dispatch().settings().vu1_mode = mode;
    vu.dispatch().set_cycle_cap(cycle_cap);
    vu.Execute(start_pc);
    rig.arbiter.drain();
    Outcome o;
    o.regs = vu.regs();
    std::memcpy(o.data, vu.state().data, sizeof(o.data));
    o.cycle = vu.cycle();
    o.bundles = vu.dispatch().bundles();
    o.ophash = rig.ops.Hash();
    o.opcount = rig.ops.Count();
    o.suspended = vu.state().suspended;
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
        if (a.regs.status_flag != b.regs.status_flag || a.regs.mac_flag != b.regs.mac_flag)
            std::printf("  %s status/mac cached=%04x/%04x interp=%04x/%04x\n", tag, a.regs.status_flag, a.regs.mac_flag,
                        b.regs.status_flag, b.regs.mac_flag);
    }
    if (std::memcmp(a.data, b.data, sizeof(a.data)) != 0) {
        ok = false;
        std::printf("  %s data memory differs\n", tag);
    }
    if (a.cycle != b.cycle) {
        ok = false;
        std::printf("  %s cycle cached=%llu interp=%llu\n", tag, static_cast<unsigned long long>(a.cycle),
                    static_cast<unsigned long long>(b.cycle));
    }
    if (a.bundles != b.bundles) {
        ok = false;
        std::printf("  %s bundles cached=%llu interp=%llu\n", tag, static_cast<unsigned long long>(a.bundles),
                    static_cast<unsigned long long>(b.bundles));
    }
    if (a.ophash != b.ophash || a.opcount != b.opcount) {
        ok = false;
        std::printf("  %s kicks cached=%zu/%016llx interp=%zu/%016llx\n", tag, a.opcount,
                    static_cast<unsigned long long>(a.ophash), b.opcount, static_cast<unsigned long long>(b.ophash));
    }
    if (a.suspended != b.suspended) {
        ok = false;
        std::printf("  %s suspended differs\n", tag);
    }
    return ok;
}

// Both tiers on a fresh unit; returns the cached outcome for further checks.
Outcome RunCase(const char* tag, const std::vector<Bundle>& prog, int unit_id = 1, Setup setup = nullptr,
                uint32_t start_pc = 0, uint64_t cycle_cap = 1000000) {
    const Outcome c = RunMode(VuTierMode::Cached, prog, unit_id, setup, start_pc, cycle_cap);
    const Outcome i = RunMode(VuTierMode::Interp, prog, unit_id, setup, start_pc, cycle_cap);
    const bool same = Same(c, i, tag);
    CHECK(same);
    std::printf("  %-24s bundles=%llu cycles=%llu kicks_ops=%zu %s\n", tag, static_cast<unsigned long long>(c.bundles),
                static_cast<unsigned long long>(c.cycle), c.opcount, same ? "identical" : "DIFFER");
    return c;
}

// The decoded table for a program (a cached run over a fresh unit).
struct Decoded {
    std::vector<VuDecodedSlot> slots;
    std::vector<uint8_t> span_rem;
};
Decoded DecodeForTest(const std::vector<Bundle>& prog) {
    VuUnit vu(1);
    WriteProgram(vu, prog);
    const VuCachedProgram* p = vu.dispatch().cached().Acquire(vu);
    Decoded d;
    d.slots = p->slots;
    d.span_rem = p->span_rem;
    return d;
}

// span_rem recomputed from the stored fold classes: consecutive fold-15/16
// slots from i (cap 255).
void CheckSpanRem(const Decoded& d) {
    CHECK(d.span_rem.size() == d.slots.size());
    for (size_t i = 0; i < d.slots.size() && i < d.span_rem.size(); ++i) {
        uint32_t n = 0;
        for (size_t j = i; j < d.slots.size(); ++j) {
            const uint8_t fc = d.slots[j].fold_class;
            if (fc != 15 && fc != 16) break;
            if (++n == 255) break;
        }
        if (d.span_rem[i] != n) {
            std::printf("  span_rem[%zu]=%u recompute=%u\n", i, d.span_rem[i], n);
            CHECK(false);
            break;
        }
    }
}

std::vector<Bundle> Tail() { return {{L_NOP, U_NOP | U_EBIT}, {L_NOP, U_NOP}}; }
void Append(std::vector<Bundle>& p, const std::vector<Bundle>& t) { p.insert(p.end(), t.begin(), t.end()); }

// (a) 64 straight-line IADDIU bundles then E.
void TestStraightline64() {
    std::vector<Bundle> p;
    for (uint32_t i = 0; i < 64; ++i) p.push_back({EncIaddiu(1 + (i % 7), 1 + ((i + 3) % 7), i * 3 + 1), U_NOP});
    Append(p, Tail());
    const Outcome c = RunCase("straightline64", p);
    CHECK(c.bundles == 66);
    CHECK(c.regs.TPC == 66 * 8);
    const Decoded d = DecodeForTest(p);
    CHECK(d.slots[0].run_len == 65);  // through the E-bit terminal
    CHECK(d.slots[63].run_len == 2 && d.slots[64].run_len == 1);
    CheckSpanRem(d);
}

// (b) XTOP mid-run: a generic lower ends the run; the value lands.
void TestXtopMidRun() {
    std::vector<Bundle> p;
    for (uint32_t i = 0; i < 5; ++i) p.push_back({L_NOP, EncUpper(F_ADD, 1 + i, 0, 0, 0xF)});
    p.push_back({EncXtop(3), EncUpper(F_ADD, 6, 0, 0, 0xF)});
    for (uint32_t i = 0; i < 5; ++i) p.push_back({L_NOP, EncUpper(F_ADD, 7 + i, 0, 0, 0xF)});
    Append(p, Tail());
    const Outcome c = RunCase("xtop_mid_run", p, 1, [](VuUnit& vu) { vu.SetVifTop(0x321, 0x12); });
    CHECK(c.regs.VI[3] == 0x321);
    const Decoded d = DecodeForTest(p);
    CHECK(d.slots[0].run_len == 6 && d.slots[5].run_len == 1 && d.slots[6].run_len == 6);
}

// (c) E bit on the last bundle of a run: TPC = E address + 16.
void TestEbitBoundary() {
    std::vector<Bundle> p;
    for (uint32_t i = 0; i < 6; ++i) p.push_back({EncIaddiu(1, 1, 2), EncUpper(F_ADD, 2, 2, 0, 0xF)});
    p.push_back({EncIaddiu(4, 0, 9), U_NOP | U_EBIT});
    p.push_back({EncIaddiu(5, 0, 8), U_NOP});
    p.push_back({EncIaddiu(6, 0, 7), U_NOP});
    const Outcome c = RunCase("ebit_boundary", p);
    CHECK(c.regs.VI[1] == 12 && c.regs.VI[4] == 9 && c.regs.VI[5] == 8 && c.regs.VI[6] == 0);
    CHECK(c.regs.TPC == 6 * 8 + 16);
}

// (d) An I-bit bundle inside a run: the upper reads the OLD I, then I loads.
void TestIbitInRun() {
    std::vector<Bundle> p;
    p.push_back({L_NOP, EncUpper(F_ADD, 1, 0, 0, 0xF)});
    p.push_back({L_NOP, EncUpper(F_ADD, 2, 1, 1, 0xF)});
    p.push_back({0x40000000u, EncUpper(0x22, 3, 1, 0, 0xF) | U_IBIT});  // ADDi vf3 = vf1 + I(old); I = 2.0
    p.push_back({L_NOP, EncUpper(0x22, 4, 1, 0, 0xF)});                 // ADDi vf4 = vf1 + 2.0
    p.push_back({0x3F800000u, U_NOP | U_IBIT});                         // I = 1.0
    p.push_back({L_NOP, EncUpper(0x1E, 5, 2, 0, 0xF)});                 // MULi vf5 = vf2 * 1.0
    Append(p, Tail());
    const Outcome c = RunCase("ibit_in_run", p, 1, [](VuUnit& vu) { vu.regs().I = 0x40400000u; });  // old I = 3.0
    CHECK(c.regs.VF[3].x == 3.0f && c.regs.VF[4].x == 2.0f && c.regs.VF[5].w == 4.0f && c.regs.I == 0x3F800000u);
}

// (f) Stall-class matrix: FMAC RAW chains at distance 1-3, IALU, stores, an
// I bit, a DIV (dynamic class) and a WAITQ inside straight-line code.
void TestStallclassMatrix() {
    std::vector<Bundle> p;
    p.push_back({L_NOP, EncUpper(F_ADD, 1, 0, 0, 0xF)});     // vf1 = 1
    p.push_back({L_NOP, EncUpper(F_ADD, 2, 1, 1, 0xF)});     // reads vf1 at distance 1 -> stall 3
    p.push_back({EncIaddiu(1, 0, 4), EncUpper(F_MUL, 3, 2, 2, 0xF)});  // distance 1
    p.push_back({EncIswX(1, 0, 2), EncUpper(F_SUB, 4, 1, 0, 0xE)});    // partial lanes
    p.push_back({L_NOP, EncUpper(F_ADD, 5, 3, 4, 0x1)});     // reads vf3 at distance 2, vf4 at 1
    p.push_back({0x41200000u, EncUpper(0x22, 6, 5, 0, 0xF) | U_IBIT});  // ADDi vf6 = vf5 + I; I = 10
    p.push_back({EncIaddiu(2, 1, 1), EncUpper(F_ADD, 7, 6, 0, 0xF)});
    p.push_back({EncDiv(2, 0, 1, 0), EncUpper(F_ADD, 8, 0, 0, 0xF)});   // Q = vf2.x / vf1.x
    p.push_back({L_NOP, EncUpper(F_ADD, 9, 8, 8, 0xF)});
    p.push_back({L_NOP, EncUpper(F_ADD, 10, 9, 9, 0xF)});
    p.push_back({EncWaitQ(), EncUpper(F_ADD, 11, 10, 0, 0xF)});
    p.push_back({L_NOP, EncUpper(0x1C, 12, 11, 0, 0xF)});    // MULq
    p.push_back({EncIaddiu(3, 2, 5), EncUpper(F_ADD, 13, 12, 0, 0xF)});
    p.push_back({EncIswX(3, 0, 3), EncUpper(F_ADD, 14, 13, 12, 0xF)});
    for (uint32_t i = 0; i < 12; ++i) {
        p.push_back({EncIaddiu(4 + (i % 4), 4 + ((i + 1) % 4), 1), EncUpper(F_ADD, 15 + (i % 8), 1 + (i % 6), 0, 0xF)});
    }
    Append(p, Tail());
    RunCase("stallclass_matrix", p);
    const Decoded d = DecodeForTest(p);
    CHECK(d.slots[7].stall_class == 0);   // DIV: dynamic
    CHECK(d.slots[10].stall_class == 0);  // WAITQ: dynamic
    CHECK(d.slots[1].stall_class == 1);   // lower + read + 1 write
    CHECK(d.slots[5].stall_class == 7);   // I bit (no lower) + read + 1 write
    // (i) every static class recomputed from the public statics
    for (const VuDecodedSlot& s : d.slots) {
        const bool lower = !s.i_bit, rd = s.n_rd != 0, ialu = s.ialu_wr_vi >= 0;
        uint8_t cls = 0;
        if (s.ialu_branch_rd == 0 && !s.efu_pipe_op && !s.fdiv_pipe_op) {
            if (lower && !ialu) cls = rd ? (s.n_wr == 1 ? 1 : s.n_wr == 2 ? 2 : 3) : (s.n_wr == 1 ? 4 : s.n_wr == 2 ? 5 : 6);
            else if (!lower) cls = rd ? (s.n_wr == 1 ? 7 : s.n_wr == 0 ? 8 : 0) : (s.n_wr == 1 ? 9 : s.n_wr == 0 ? 10 : 0);
            else cls = rd ? (s.n_wr == 1 ? 11 : s.n_wr == 0 ? 12 : 0) : (s.n_wr == 1 ? 13 : s.n_wr == 0 ? 14 : 0);
        }
        CHECK(s.stall_class == cls);
    }
    CheckSpanRem(d);
}

// A PACKED GIFtag at data QW 8 with two A+D writes (the kick payload).
void WriteKickPacket(VuUnit& vu) {
    uint8_t qw[16] = {};
    const uint64_t tag_lo = 2ull | (1ull << 15) | (1ull << 60);
    const uint64_t tag_hi = 0x0Eull;
    std::memcpy(qw, &tag_lo, 8);
    std::memcpy(qw + 8, &tag_hi, 8);
    vu.WriteDataMem(8 * 16, qw, 16);
    const uint64_t a0 = 0x1234ull, r0 = 0x47ull, a1 = 0x5678ull, r1 = 0x42ull;
    std::memcpy(qw, &a0, 8);
    std::memcpy(qw + 8, &r0, 8);
    vu.WriteDataMem(9 * 16, qw, 16);
    std::memcpy(qw, &a1, 8);
    std::memcpy(qw + 8, &r1, 8);
    vu.WriteDataMem(10 * 16, qw, 16);
}

// (g) XGKICK then a long FMAC run: the 32-cycle pace fires inside the run.
void TestPaceFireInRun() {
    std::vector<Bundle> p;
    p.push_back({EncIaddiu(1, 0, 8), U_NOP});
    p.push_back({EncXgkick(1), U_NOP});
    for (uint32_t i = 0; i < 48; ++i) p.push_back({L_NOP, EncUpper(F_ADD, 1 + (i % 8), 1 + ((i + 3) % 8), 0, 0xF)});
    Append(p, Tail());
    const Outcome c = RunCase("pace_fire_in_run", p, 1, WriteKickPacket);
    CHECK(c.opcount == 4);
}

// (n) Fold window: a reader at k+3 keeps the stall (fold 0), at k+4 folds.
void TestStallfoldWindow() {
    {
        std::vector<Bundle> p;
        for (uint32_t i = 0; i < 6; ++i) p.push_back({L_NOP, EncUpper(F_ADD, 10 + i, 0, 0, 0xF)});  // writers 10..15
        p.push_back({L_NOP, EncUpper(F_ADD, 20, 13, 0, 0xF)});  // slot 6 reads vf13 written at slot 3 (k+3)
        for (uint32_t i = 0; i < 4; ++i) p.push_back({L_NOP, EncUpper(F_ADD, 21 + i, 0, 0, 0xF)});
        Append(p, Tail());
        RunCase("stallfold_window_k3", p);
        const Decoded d = DecodeForTest(p);
        CHECK(d.slots[6].fold_class == 0);
        CHECK(d.slots[5].fold_class == 16);
    }
    {
        std::vector<Bundle> p;
        for (uint32_t i = 0; i < 6; ++i) p.push_back({L_NOP, EncUpper(F_ADD, 10 + i, 0, 0, 0xF)});
        p.push_back({L_NOP, EncUpper(F_ADD, 20, 12, 0, 0xF)});  // slot 6 reads vf12 written at slot 2 (k+4)
        for (uint32_t i = 0; i < 4; ++i) p.push_back({L_NOP, EncUpper(F_ADD, 21 + i, 0, 0, 0xF)});
        Append(p, Tail());
        RunCase("stallfold_window_k4", p);
        const Decoded d = DecodeForTest(p);
        CHECK(d.slots[6].fold_class == 16);
    }
}

// (o) A branch landing mid-chain: the warmup positions run the model.
void TestStallfoldEntry() {
    std::vector<Bundle> p;
    p.push_back({EncB(4), U_NOP});                             // -> slot 5
    p.push_back({L_NOP, EncUpper(F_ADD, 1, 0, 0, 0xF)});       // delay slot: vf1 = 1
    p.push_back({L_NOP, U_NOP});                               // skipped
    p.push_back({L_NOP, U_NOP});
    p.push_back({L_NOP, EncUpper(F_ADD, 3, 0, 0, 0xF)});       // skipped
    p.push_back({L_NOP, EncUpper(F_ADD, 2, 1, 1, 0xF)});       // slot 5: reads vf1 at distance 1 (the delay slot) -> stall
    for (uint32_t i = 0; i < 8; ++i) p.push_back({L_NOP, EncUpper(F_ADD, 4 + i, 2, 0, 0xF)});
    Append(p, Tail());
    const Outcome c = RunCase("stallfold_entry", p);
    CHECK(c.regs.VF[2].w == 4.0f && c.regs.VF[4].w == 5.0f);
}

// (p) Dense dual-write chain to the ring's capacity (prune + push under pressure).
void TestStallfoldClass16() {
    std::vector<Bundle> p;
    for (uint32_t i = 0; i < 24; ++i) {
        // upper writes vf(1+i%10), lower LQI writes vf(11+i%10) -> two VF writes per bundle
        const uint32_t lqi = (0x40u << 25) | (0xFu << 21) | ((11 + i % 10) << 16) | (1u << 11) | (0x0Du << 6) | 0x3Cu;
        p.push_back({lqi, EncUpper(F_ADD, 1 + i % 10, 0, 0, 0xF)});
    }
    p.push_back({L_NOP, EncUpper(F_ADD, 25, 10, 20, 0xF)});  // terminal-side reader of both chains
    Append(p, Tail());
    RunCase("stallfold_class16", p, 1, [](VuUnit& vu) {
        for (uint32_t q = 0; q < 64; ++q) {
            uint32_t w[4] = {q, q * 3, q * 5, q * 7};
            vu.WriteDataMem(q * 16, reinterpret_cast<const uint8_t*>(w), 16);
        }
    });
    const Decoded d = DecodeForTest(p);
    CHECK(d.slots[4].fold_class == 16 && d.slots[4].n_wr == 2);
}

// (aa) A 22-bundle run whose terminal reads the last-2 writer at distance 2:
// the span-close materialisation must land that stall (proven against the
// no-read variant, which runs two cycles shorter).
void TestSpanfoldBasic() {
    std::vector<Bundle> p;
    for (uint32_t i = 0; i < 21; ++i) p.push_back({L_NOP, EncUpper(F_ADD, 1 + (i % 8), 0, 0, 0xF)});  // slot 19 writes vf4
    p.push_back({L_NOP, EncUpper(F_ADD, 20, 4, 0, 0xF) | U_EBIT});  // terminal reads vf4 (distance 2)
    p.push_back({L_NOP, U_NOP});
    const Outcome a = RunCase("spanfold_basic", p);
    std::vector<Bundle> q = p;
    q[21] = {L_NOP, EncUpper(F_ADD, 20, 0, 0, 0xF) | U_EBIT};  // no read
    const Outcome b = RunCase("spanfold_basic_noread", q);
    CHECK(a.cycle == b.cycle + 2);
    const Decoded d = DecodeForTest(p);
    for (size_t i = 4; i < 20; ++i) CHECK(d.slots[i].fold_class == 16);
    CHECK(d.slots[21].fold_class == 0);
    CheckSpanRem(d);
}

// (ab) 3-slot stretches never enter the span body (span < 4 falls through).
void TestSpanfoldShort() {
    std::vector<Bundle> p;
    for (uint32_t r = 0; r < 4; ++r) {
        for (uint32_t i = 0; i < 3; ++i) p.push_back({L_NOP, EncUpper(F_ADD, 1 + i, 0, 0, 0xF)});
        p.push_back({L_NOP, EncUpper(F_ADD, 5, 1, 0, 0xF)});  // reads vf1 at distance 3 -> fold 0
    }
    Append(p, Tail());
    RunCase("spanfold_short", p);
}

// (ac) A dynamic-class slot after a span reads the span writer at distance 3:
// the materialised push is seen.
void TestSpanfoldInterrupted() {
    std::vector<Bundle> p;
    for (uint32_t i = 0; i < 12; ++i) p.push_back({L_NOP, EncUpper(F_ADD, 1 + (i % 8), 0, 0, 0xF)});  // slot 11 writes vf4
    p.push_back({EncDiv(1, 0, 2, 0), EncUpper(F_ADD, 20, 0, 0, 0xF)});  // dynamic class: ends the span
    p.push_back({L_NOP, EncUpper(F_ADD, 21, 4, 0, 0xF)});                // reads vf4 at distance 3 -> stall 1
    p.push_back({EncWaitQ(), EncUpper(F_ADD, 22, 21, 0, 0xF)});
    p.push_back({L_NOP, EncUpper(0x1C, 23, 22, 0, 0xF)});
    Append(p, Tail());
    RunCase("spanfold_interrupted", p, 1, [](VuUnit& vu) {
        vu.regs().VF[1].x = 6.0f;
        vu.regs().VF[2].x = 3.0f;
    });
}

// (ae) An infinite loop inside a >= 8 span: every Execute exhausts the cap
// mid-span at a shifting offset (the materialise and the replay closes).
void TestSpanfoldBackstop() {
    std::vector<Bundle> p;
    for (uint32_t i = 0; i < 10; ++i) p.push_back({L_NOP, EncUpper(F_ADD, 1 + (i % 8), 0, 0, 0xF)});
    p.push_back({EncB(-11), U_NOP});                          // back to slot 0 (relative to the next bundle)
    p.push_back({L_NOP, EncUpper(F_ADD, 9, 0, 0, 0xF)});      // delay slot
    // Three caps: 4999 / 5000 / 5003 bundles land the cut at different span offsets.
    const uint64_t caps[3] = {4999, 5000, 5003};
    for (int leg = 0; leg < 3; ++leg) {
        char tag[48];
        std::snprintf(tag, sizeof(tag), "spanfold_backstop_%d", leg);
        const Outcome c = RunCase(tag, p, 1, nullptr, 0, caps[leg]);
        CHECK(c.bundles == caps[leg]);
    }
    // A cap of 1..12 cuts inside the warmup and the first span with < 4 executed (the replay close).
    for (uint64_t cap = 1; cap <= 12; ++cap) {
        char tag[48];
        std::snprintf(tag, sizeof(tag), "backstop_cap_%llu", static_cast<unsigned long long>(cap));
        const Outcome c = RunCase(tag, p, 1, nullptr, 0, cap);
        CHECK(c.bundles == cap);
    }
}

// (af) ILW before / after a span with an IBNE terminal.
void TestSpanfoldIalu() {
    std::vector<Bundle> p;
    p.push_back({EncIlw(1, 0, 4), U_NOP});                   // vi1 = word at QW 4
    for (uint32_t i = 0; i < 10; ++i) p.push_back({L_NOP, EncUpper(F_ADD, 1 + (i % 8), 0, 0, 0xF)});
    p.push_back({EncIlw(2, 0, 5), EncUpper(F_ADD, 10, 0, 0, 0xF)});
    p.push_back({EncIbne(1, 2, 2), U_NOP});                 // reads vi2 right after its ILW -> IALU stall
    p.push_back({L_NOP, EncUpper(F_ADD, 11, 0, 0, 0xF)});   // delay slot
    p.push_back({EncIaddiu(3, 0, 1), U_NOP});               // fallthrough only
    p.push_back({EncIaddiu(4, 0, 1), U_NOP | U_EBIT});      // branch target
    p.push_back({L_NOP, U_NOP});
    RunCase("spanfold_ialu", p, 1, [](VuUnit& vu) {
        const uint32_t w4[4] = {7, 0, 0, 0}, w5[4] = {9, 0, 0, 0};
        vu.WriteDataMem(4 * 16, reinterpret_cast<const uint8_t*>(w4), 16);
        vu.WriteDataMem(5 * 16, reinterpret_cast<const uint8_t*>(w5), 16);
    });
}

// (ag) XGKICK armed, then a >= 8 span: the pace fires inside the span.
void TestSpanfoldPace() {
    std::vector<Bundle> p;
    p.push_back({EncIaddiu(1, 0, 8), U_NOP});
    p.push_back({EncXgkick(1), U_NOP});
    for (uint32_t i = 0; i < 40; ++i) p.push_back({L_NOP, EncUpper(F_ADD, 1 + (i % 8), 0, 0, 0xF)});
    Append(p, Tail());
    const Outcome c = RunCase("spanfold_pace", p, 1, WriteKickPacket);
    CHECK(c.opcount == 4);
}

// VU0: the 4 KB image, the self-spin park and the resume through the ladder.
void TestVu0ParkResume() {
    std::vector<Bundle> p;
    for (uint32_t i = 0; i < 6; ++i) p.push_back({L_NOP, EncUpper(F_ADD, 1 + i, 0, 0, 0xF)});
    p.push_back({EncIbne(1, 2, -1), U_NOP});
    p.push_back({L_NOP, U_NOP});
    p.push_back({EncIaddiu(3, 0, 1), U_NOP | U_EBIT});
    p.push_back({L_NOP, U_NOP});
    for (int m = 0; m < 2; ++m) {
        const VuTierMode mode = m == 0 ? VuTierMode::Cached : VuTierMode::Interp;
        VuUnit vu(0);
        WriteProgram(vu, p);
        vu.regs().VI[1] = 1;
        vu.regs().VI[2] = 2;
        vu.dispatch().settings().vu1_mode = mode;
        vu.Execute(0);
        CHECK(vu.state().suspended && vu.state().suspended_tpc == 6 * 8 && vu.regs().TPC == 6 * 8);
        vu.regs().VI[2] = 1;
        vu.ResumeIfWaiting();
        CHECK(!vu.state().suspended && vu.regs().VI[3] == 1 && vu.regs().TPC == 8 * 8 + 16);
    }
    const Outcome c = RunCase("vu0_park", p, 0, [](VuUnit& vu) {
        vu.regs().VI[1] = 1;
        vu.regs().VI[2] = 2;
    });
    CHECK(c.suspended);
}

// The program table: the epoch fast path, the hash lookup on a re-write, and
// the least-executed eviction at the cap.
void TestTable() {
    VuUnit vu(1);
    VuCachedTier& tier = vu.dispatch().cached();
    std::vector<Bundle> a = {{EncIaddiu(1, 0, 1), U_NOP}};
    Append(a, Tail());
    WriteProgram(vu, a);
    const VuCachedProgram* pa = tier.Acquire(vu);
    CHECK(tier.table_size() == 1 && pa->exec_count == 1);
    CHECK(tier.Acquire(vu) == pa && pa->exec_count == 2);  // same epoch: no hash
    vu.Execute(0);
    CHECK(pa->exec_count == 3 && pa->bundles == 3);
    // Re-writing the same bytes bumps the epoch: hash lookup finds the entry.
    WriteProgram(vu, a);
    CHECK(tier.Acquire(vu) == pa && tier.table_size() == 1 && pa->exec_count == 4);
    // 63 more distinct images fill the table; the 65th evicts the least executed (not `a`).
    for (uint32_t i = 0; i < 63; ++i) {
        std::vector<Bundle> b = {{EncIaddiu(2, 0, 100 + i), U_NOP}};
        Append(b, Tail());
        WriteProgram(vu, b);
        tier.Acquire(vu);
    }
    CHECK(tier.table_size() == 64);
    std::vector<Bundle> z = {{EncIaddiu(3, 0, 999), U_NOP}};
    Append(z, Tail());
    WriteProgram(vu, z);
    const VuCachedProgram* pz = tier.Acquire(vu);
    CHECK(tier.table_size() == 64 && pz->exec_count == 1);
    WriteProgram(vu, a);
    CHECK(tier.Acquire(vu) == pa);  // `a` survived (exec_count 4 vs the 1-count images)
    CHECK(tier.table_size() == 64);
}

// The dispatch ladder: hooks take or decline; VU0 never enters them.
struct HookLog {
    int calls = 0;
    bool take = false;
};
bool HleHookFn(void* user, VuUnit& unit, uint32_t start_pc, uint64_t gen) {
    HookLog* log = static_cast<HookLog*>(user);
    ++log->calls;
    if (log->take) {
        unit.regs().VI[9] = static_cast<uint16_t>(start_pc + 1);
        unit.regs().TPC = static_cast<uint16_t>(gen & 0xFFFF);
    }
    return log->take;
}
void TestDispatch() {
    std::vector<Bundle> p = {{EncIaddiu(1, 0, 5), U_NOP}};
    Append(p, Tail());
    VuUnit vu(1);
    WriteProgram(vu, p);
    HookLog log;
    CHECK(vu.dispatch().settings().vu1_mode == VuTierMode::Cached && !vu.dispatch().settings().hle_enabled);
    vu.dispatch().SetHleHook(HleHookFn, &log);
    vu.Execute(0);
    CHECK(log.calls == 0 && vu.regs().VI[1] == 5);  // gate off: the hook is not consulted
    vu.dispatch().settings().hle_enabled = true;
    vu.regs().VI[1] = 0;
    vu.Execute(0);
    CHECK(log.calls == 1 && vu.regs().VI[1] == 5);  // declined: fell through, the program ran
    log.take = true;
    vu.regs().VI[1] = 0;
    vu.Execute(0x10);
    CHECK(log.calls == 2 && vu.regs().VI[1] == 0 && vu.regs().VI[9] == 0x11);  // taken: tiers untouched
    CHECK(vu.dispatch().bundles() == 6);
    vu.dispatch().settings().vu1_mode = VuTierMode::Interp;
    log.take = false;
    vu.Execute(0);
    CHECK(vu.dispatch().interp().bundles() == 3 && vu.dispatch().cached().bundles() == 6);
    VuUnit v0(0);
    WriteProgram(v0, p);
    v0.dispatch().settings().hle_enabled = true;
    v0.dispatch().SetHleHook(HleHookFn, &log);
    const int calls_before = log.calls;
    CHECK(calls_before == 3);  // the declined interp-mode run consulted the hook too
    v0.Execute(0);
    CHECK(log.calls == calls_before && v0.regs().VI[1] == 5);  // VU0 never enters the hooks
}

}  // namespace

int main() {
    TestStraightline64();
    TestXtopMidRun();
    TestEbitBoundary();
    TestIbitInRun();
    TestStallclassMatrix();
    TestPaceFireInRun();
    TestStallfoldWindow();
    TestStallfoldEntry();
    TestStallfoldClass16();
    TestSpanfoldBasic();
    TestSpanfoldShort();
    TestSpanfoldInterrupted();
    TestSpanfoldBackstop();
    TestSpanfoldIalu();
    TestSpanfoldPace();
    TestVu0ParkResume();
    TestTable();
    TestDispatch();
    if (g_fail) {
        std::printf("VuCachedSuperblock: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("VuCachedSuperblock: ALL PASS\n");
    return 0;
}
