// VuBlockTier: the block tier against the interpreter reference on
// hand-encoded programs (fresh unit per leg, block then interp -> identical
// registers, data memory, clock, bundle count and kicked register-op stream),
// every program also swept over EVERY cycle cap (TPC and the ring at a cut),
// the static stall schedule against the stall model on random straight-line
// FMAC sequences, the block structure (terminals, the merged delay slot,
// static targets, successor chaining), the stall kinds, the post-issue advance
// guard (a second XGKICK's stall inside a static run), XTOP yields inside a
// body and inside a delay slot, the NOP sled, the image-end wrap, a
// misaligned entry, VU0 staying on the cached tier, the epoch fast path and
// the 64-entry eviction.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/vu/stall_model.h"
#include "engine/vu/tiers/vu_block_tier.h"
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
constexpr uint32_t F_ADD = 0x28, F_MUL = 0x2A, F_SUB = 0x2C, F_MULQ = 0x1C;

struct Bundle {
    uint32_t lower, upper;
};

uint32_t EncUpper(uint32_t func, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t dest) {
    return func | (fd << 6) | (fs << 11) | (ft << 16) | (dest << 21);
}
uint32_t EncIaddiu(uint32_t it, uint32_t is, uint32_t imm15) {
    return (0x08u << 25) | (((imm15 >> 11) & 0xFu) << 21) | (it << 16) | (is << 11) | (imm15 & 0x7FFu);
}
uint32_t EncIsubiu(uint32_t it, uint32_t is, uint32_t imm15) {
    return (0x09u << 25) | (((imm15 >> 11) & 0xFu) << 21) | (it << 16) | (is << 11) | (imm15 & 0x7FFu);
}
uint32_t EncIlw(uint32_t it, uint32_t is, int32_t imm11) {
    return (0x04u << 25) | (0x8u << 21) | (it << 16) | (is << 11) | (static_cast<uint32_t>(imm11) & 0x7FFu);
}
uint32_t EncLq(uint32_t ft, uint32_t is, int32_t imm11, uint32_t dest = 0xF) {
    return (0x00u << 25) | (dest << 21) | (ft << 16) | (is << 11) | (static_cast<uint32_t>(imm11) & 0x7FFu);
}
uint32_t EncSq(uint32_t fs, uint32_t it, int32_t imm11, uint32_t dest = 0xF) {
    return (0x01u << 25) | (dest << 21) | (it << 16) | (fs << 11) | (static_cast<uint32_t>(imm11) & 0x7FFu);
}
uint32_t EncMove(uint32_t ft, uint32_t fs, uint32_t dest = 0xF) {
    return (0x40u << 25) | (dest << 21) | (ft << 16) | (fs << 11) | (0x0Cu << 6) | 0x3Cu;
}
uint32_t EncDiv(uint32_t fs, uint32_t fsf, uint32_t ft, uint32_t ftf) {
    return (0x40u << 25) | (ftf << 23) | (fsf << 21) | (ft << 16) | (fs << 11) | (0x0Eu << 6) | 0x3Cu;
}
uint32_t EncWaitQ() { return (0x40u << 25) | (0x0Eu << 6) | 0x3Fu; }
uint32_t EncXtop(uint32_t it) { return (0x40u << 25) | (it << 16) | (0x1Au << 6) | 0x3Cu; }
uint32_t EncXgkick(uint32_t is) { return (0x40u << 25) | (is << 11) | (0x1Bu << 6) | 0x3Cu; }
uint32_t EncB(int32_t imm11) { return (0x20u << 25) | (static_cast<uint32_t>(imm11) & 0x7FFu); }
uint32_t EncBal(uint32_t it, int32_t imm11) { return (0x21u << 25) | (it << 16) | (static_cast<uint32_t>(imm11) & 0x7FFu); }
uint32_t EncJr(uint32_t is) { return (0x24u << 25) | (is << 11); }
uint32_t EncJalr(uint32_t it, uint32_t is) { return (0x25u << 25) | (it << 16) | (is << 11); }
uint32_t EncIbne(uint32_t it, uint32_t is, int32_t imm11) {
    return (0x29u << 25) | (it << 16) | (is << 11) | (static_cast<uint32_t>(imm11) & 0x7FFu);
}

struct TestVuBlockTier {
    RegisterOpStream ops;
    GifRegs regs;
    GifParser parser;
    GifArbiter arbiter;
    TestVuBlockTier() : parser(ops, regs), arbiter(parser) {}
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
    uint64_t ops_static = 0;
    uint64_t ops_model = 0;
    uint64_t blocks_run = 0;
};

using Setup = void (*)(VuUnit&);

struct Program {
    const char* tag = "";
    std::vector<Bundle> code;
    uint32_t base = 0;
    uint32_t start_pc = 0;
    Setup setup = nullptr;
};

Program Prog(const char* tag) {
    Program p;
    p.tag = tag;
    return p;
}

Outcome RunMode(VuTierMode mode, const Program& p, uint64_t cycle_cap = 1000000) {
    TestVuBlockTier rig;
    VuUnit vu(1, &rig.arbiter);
    WriteProgram(vu, p.code, p.base);
    if (p.setup) p.setup(vu);
    vu.dispatch().settings().vu1_mode = mode;
    vu.dispatch().aot().set_assume_verified(mode == VuTierMode::Aot);  // the AOT leg: the specialised bodies on every op
    vu.dispatch().set_cycle_cap(cycle_cap);
    vu.Execute(p.start_pc);
    rig.arbiter.drain();
    Outcome o;
    o.regs = vu.regs();
    std::memcpy(o.data, vu.state().data, sizeof(o.data));
    o.cycle = vu.cycle();
    o.bundles = vu.dispatch().bundles();
    o.ophash = rig.ops.Hash();
    o.opcount = rig.ops.Count();
    o.ops_static = vu.dispatch().block().stats().ops_static;
    o.ops_model = vu.dispatch().block().stats().ops_model;
    o.blocks_run = vu.dispatch().block().stats().blocks_run;
    return o;
}

bool Same(const Outcome& a, const Outcome& b, const char* tag) {
    bool ok = true;
    if (std::memcmp(&a.regs, &b.regs, sizeof(VUregs)) != 0) {
        ok = false;
        for (int r = 0; r < 32; ++r) {
            if (std::memcmp(&a.regs.VF[r], &b.regs.VF[r], 16) != 0)
                std::printf("  %s VF%02d block=%08x,%08x,%08x,%08x interp=%08x,%08x,%08x,%08x\n", tag, r, a.regs.VF[r].I[0],
                            a.regs.VF[r].I[1], a.regs.VF[r].I[2], a.regs.VF[r].I[3], b.regs.VF[r].I[0], b.regs.VF[r].I[1],
                            b.regs.VF[r].I[2], b.regs.VF[r].I[3]);
        }
        for (int r = 0; r < 16; ++r) {
            if (a.regs.VI[r] != b.regs.VI[r]) std::printf("  %s VI%02d block=%04x interp=%04x\n", tag, r, a.regs.VI[r], b.regs.VI[r]);
        }
        if (a.regs.TPC != b.regs.TPC) std::printf("  %s TPC block=%04x interp=%04x\n", tag, a.regs.TPC, b.regs.TPC);
        if (a.regs.status_flag != b.regs.status_flag || a.regs.mac_flag != b.regs.mac_flag)
            std::printf("  %s status/mac block=%04x/%04x interp=%04x/%04x\n", tag, a.regs.status_flag, a.regs.mac_flag,
                        b.regs.status_flag, b.regs.mac_flag);
        if (a.regs.I != b.regs.I) std::printf("  %s I block=%08x interp=%08x\n", tag, a.regs.I, b.regs.I);
    }
    if (std::memcmp(a.data, b.data, sizeof(a.data)) != 0) {
        ok = false;
        std::printf("  %s data memory differs\n", tag);
    }
    if (a.cycle != b.cycle) {
        ok = false;
        std::printf("  %s cycle block=%llu interp=%llu\n", tag, static_cast<unsigned long long>(a.cycle),
                    static_cast<unsigned long long>(b.cycle));
    }
    if (a.bundles != b.bundles) {
        ok = false;
        std::printf("  %s bundles block=%llu interp=%llu\n", tag, static_cast<unsigned long long>(a.bundles),
                    static_cast<unsigned long long>(b.bundles));
    }
    if (a.ophash != b.ophash || a.opcount != b.opcount) {
        ok = false;
        std::printf("  %s kicks block=%zu/%016llx interp=%zu/%016llx\n", tag, a.opcount,
                    static_cast<unsigned long long>(a.ophash), b.opcount, static_cast<unsigned long long>(b.ophash));
    }
    return ok;
}

// The block tier, the AOT tier (the (kind, dest)-specialised bodies on the same
// blocks) and the interpreter on fresh units, then the cycle-cap sweep (every cap
// from 1 to the full run + 2, or every `stride`-th for long runs) for both tiers;
// returns the block outcome.
Outcome RunCase(const Program& p, uint64_t sweep_stride = 1) {
    const Outcome b = RunMode(VuTierMode::Block, p);
    const Outcome i = RunMode(VuTierMode::Interp, p);
    const Outcome a = RunMode(VuTierMode::Aot, p);
    const bool same = Same(b, i, p.tag) && Same(a, i, (std::string(p.tag) + " [aot]").c_str());
    CHECK(same);
    int cut_fail = 0;
    uint64_t caps = 0;
    for (uint64_t cap = 1; cap <= b.bundles + 2; cap += sweep_stride) {
        const Outcome bc = RunMode(VuTierMode::Block, p, cap);
        const Outcome ic = RunMode(VuTierMode::Interp, p, cap);
        const Outcome ac = RunMode(VuTierMode::Aot, p, cap);
        char tag[96];
        std::snprintf(tag, sizeof(tag), "%s@cap%llu", p.tag, static_cast<unsigned long long>(cap));
        if (!Same(bc, ic, tag)) ++cut_fail;
        std::snprintf(tag, sizeof(tag), "%s@cap%llu [aot]", p.tag, static_cast<unsigned long long>(cap));
        if (!Same(ac, ic, tag)) ++cut_fail;
        ++caps;
    }
    CHECK(cut_fail == 0);
    std::printf("  %-20s bundles=%llu cycles=%llu kicks_ops=%zu blocks=%llu static=%llu model=%llu caps=%llu %s\n", p.tag,
                static_cast<unsigned long long>(b.bundles), static_cast<unsigned long long>(b.cycle), b.opcount,
                static_cast<unsigned long long>(b.blocks_run), static_cast<unsigned long long>(b.ops_static),
                static_cast<unsigned long long>(b.ops_model), static_cast<unsigned long long>(caps),
                same && cut_fail == 0 ? "identical" : "DIFFER");
    return b;
}

std::vector<Bundle> Tail() { return {{L_NOP, U_NOP | U_EBIT}, {L_NOP, U_NOP}}; }
void Append(std::vector<Bundle>& p, const std::vector<Bundle>& t) { p.insert(p.end(), t.begin(), t.end()); }

// The block table of a program (a block run over a fresh unit), for the structural checks.
struct Built {
    VuUnit vu{1};
    VuBlockProgram* prog = nullptr;
};

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

void SeedVf(VuUnit& vu) {
    for (int r = 1; r < 32; ++r) {
        for (int k = 0; k < 4; ++k) vu.regs().VF[r].F[k] = 1.0f + 0.25f * static_cast<float>(r) + 0.5f * static_cast<float>(k);
    }
}

// (a) 64 dependent FMAC bundles then E: the schedule engages after the warmup.
void TestFmacChain() {
    Program p = Prog("fmac_chain_64");
    for (uint32_t i = 0; i < 64; ++i) {
        const uint32_t fd = 1 + (i % 8), fs = 1 + ((i + 3) % 8), ft = 1 + ((i + 5) % 8);
        p.code.push_back({L_NOP, EncUpper((i % 3 == 0) ? F_MUL : F_ADD, fd, fs, ft, 0xF)});
    }
    Append(p.code, Tail());
    p.setup = SeedVf;
    const Outcome b = RunCase(p);
    CHECK(b.ops_static > 40);  // the schedule carried most of the chain
    CHECK(b.regs.TPC == 64 * 8 + 16);
    // Structure: one block from 0 through the E bit with the delay slot merged.
    Built bt;
    WriteProgram(bt.vu, p.code);
    bt.prog = bt.vu.dispatch().block().Acquire(bt.vu);
    const VuBlock* blk = bt.vu.dispatch().block().BlockAt(*bt.prog, 0);
    CHECK(blk->n_body == 65 && blk->has_ds && blk->e_bit && blk->branch_idx == -1 && blk->ops.size() == 66);
    CHECK(blk->ops[0].s_delta == 0);
    CHECK(blk->ops[0].stall_kind == VuBlockOp::kStallModel);  // reads VFs
    CHECK(blk->ops[65].stall_kind == VuBlockOp::kStallNone);  // NOP pair... no: NOP words, no VF traffic
}

// (b) A counted loop whose body reads what the previous iteration (and the
// delay slot) wrote: pre-block writebacks in flight at the body's first ops;
// an ILW two bundles before the IBNE that reads it (the IALU branch stall).
void TestLoopIalu() {
    Program p = Prog("loop_ialu");
    p.code.push_back({EncIaddiu(1, 0, 13), U_NOP});                                      // 0: vi1 = 13 (6 iterations to 7)
    p.code.push_back({EncIsubiu(1, 1, 1), EncUpper(F_ADD, 1, 4, 4, 0xF)});             // 1: L: vf1 = vf4 + vf4 (vf4 from the ds)
    p.code.push_back({EncIlw(2, 0, 4), EncUpper(F_MUL, 2, 1, 1, 0xF)});                // 2: vf2 = vf1 * vf1 (stall 3); vi2 = word
    p.code.push_back({L_NOP, EncUpper(F_ADD, 3, 2, 1, 0xF)});                            // 3: vf3 = vf2 + vf1 (stall 3)
    p.code.push_back({EncIbne(2, 1, -4), EncUpper(F_SUB, 4, 3, 2, 0xF)});               // 4: vf4 = vf3 - vf2; IBNE vi2, vi1 -> L
    p.code.push_back({L_NOP, EncUpper(F_ADD, 5, 4, 1, 0xF)});                            // 5: ds: vf5 = vf4 + vf1 (stall 3)
    p.code.push_back({EncIaddiu(3, 0, 1), U_NOP});                                       // 6: fallthrough
    Append(p.code, Tail());
    p.setup = [](VuUnit& vu) {
        SeedVf(vu);
        const uint32_t w4[4] = {7, 0, 0, 0};
        vu.WriteDataMem(4 * 16, reinterpret_cast<const uint8_t*>(w4), 16);
    };
    const Outcome b = RunCase(p);
    CHECK(b.regs.VI[3] == 1);
    Built bt;
    WriteProgram(bt.vu, p.code);
    bt.prog = bt.vu.dispatch().block().Acquire(bt.vu);
    const VuBlock* body = bt.vu.dispatch().block().BlockAt(*bt.prog, 8);
    CHECK(body->n_body == 4 && body->has_ds && body->branch_idx == 3 && body->static_target == 8 && !body->e_bit);
    CHECK(body->ops[1].s_delta == 3 && body->ops[2].s_delta == 3);
    CHECK((body->ops[3].flags & VuBlockOp::kDynamic) != 0);  // the branch: the IALU read
    CHECK((body->ops[1].flags & VuBlockOp::kIaluWr) != 0 && body->ops[1].ialu_vi == 2);
    const VuBlock* head = bt.vu.dispatch().block().BlockAt(*bt.prog, 0);
    CHECK(head->n_body == 5 && head->fall == nullptr);  // the entry block runs into the loop terminal; chained lazily
}

// (c) Subroutines: BAL / JR, JALR; a branch and an E bit inside a delay slot
// (ignored); a branch on the E-bit bundle (ignored).
void TestSubroutines() {
    Program p = Prog("subroutines");
    p.code.push_back({EncBal(15, 7), EncUpper(F_ADD, 1, 2, 3, 0xF)});     // 0: call slot 8 (pc+8+7*8 = 64)
    p.code.push_back({EncIaddiu(1, 0, 1), U_NOP});                       // 1: ds
    p.code.push_back({EncIaddiu(14, 0, 10), U_NOP});                     // 2: vi14 = slot 10 (JALR target)
    p.code.push_back({EncJalr(13, 14), EncUpper(F_MUL, 4, 1, 1, 0xF)});  // 3: call slot 10 via vi14
    p.code.push_back({EncB(2), U_NOP});                                  // 4: ds with a branch: IGNORED
    p.code.push_back({EncIaddiu(5, 0, 5), U_NOP});                       // 5: after the call
    p.code.push_back({EncB(3), U_NOP | U_EBIT});                         // 6: E with a branch: the branch is ignored
    p.code.push_back({EncIaddiu(6, 0, 6), U_NOP | U_EBIT});              // 7: ds of the E (its own E ignored); also sub A entry
    p.code.push_back({EncJr(15), EncUpper(F_SUB, 2, 1, 1, 0xF)});        // 8: sub A: return
    p.code.push_back({EncIaddiu(7, 0, 7), U_NOP});                       // 9: ds
    p.code.push_back({EncIaddiu(8, 0, 8), EncUpper(F_ADD, 3, 2, 2, 0xF)});  // 10: sub B
    p.code.push_back({EncJr(13), U_NOP});                                // 11: return
    p.code.push_back({EncIaddiu(9, 0, 9), U_NOP});                       // 12: ds
    p.code.push_back({L_NOP, U_NOP});
    p.setup = SeedVf;
    const Outcome b = RunCase(p);
    CHECK(b.regs.VI[1] == 1 && b.regs.VI[5] == 5 && b.regs.VI[6] == 6 && b.regs.VI[7] == 7 && b.regs.VI[8] == 8 && b.regs.VI[9] == 9);
    CHECK(b.regs.VI[15] == 2 && b.regs.VI[13] == 5);  // the links: slot after the delay slot
    CHECK(b.regs.TPC == 6 * 8 + 16);
}

// (d) Same-bundle hazards: hide_upper (the lower reads the pre-upper VF),
// discard_lower (both write the same VF), the I bit (upper reads the old I).
void TestHazards() {
    Program p = Prog("hazards");
    p.code.push_back({EncMove(10, 1), EncUpper(F_ADD, 1, 2, 3, 0xF)});        // 0: vf10 = OLD vf1; vf1 = vf2 + vf3
    p.code.push_back({EncLq(1, 0, 4), EncUpper(F_MUL, 1, 2, 2, 0xF)});         // 1: both write vf1: the LQ is dropped
    p.code.push_back({EncSq(1, 0, 5), U_NOP});                                 // 2: store vf1
    p.code.push_back({0x40000000u, EncUpper(0x22, 3, 1, 0, 0xF) | U_IBIT});    // 3: ADDi vf3 = vf1 + I(old); I = 2.0
    p.code.push_back({L_NOP, EncUpper(0x22, 4, 1, 0, 0xF)});                   // 4: ADDi vf4 = vf1 + 2.0
    p.code.push_back({EncMove(11, 4), EncUpper(F_SUB, 4, 4, 1, 0xE)});         // 5: hide again (partial dest)
    Append(p.code, Tail());
    p.setup = [](VuUnit& vu) {
        SeedVf(vu);
        vu.regs().I = 0x40400000u;  // 3.0
        const float q[4] = {-1.0f, -2.0f, -3.0f, -4.0f};
        vu.WriteDataMem(4 * 16, reinterpret_cast<const uint8_t*>(q), 16);
    };
    const Outcome b = RunCase(p);
    CHECK(b.regs.VF[10].x == 1.25f && b.regs.VF[1].x != -1.0f && b.regs.I == 0x40000000u);
}

// (e) FDIV / WAITQ (dynamic ops inside a block) and two XGKICKs in a row
// inside a static run: the second kick's stall is a post-issue clock advance.
void TestDivAndKicks() {
    Program p = Prog("div_kicks");
    p.code.push_back({EncIaddiu(1, 0, 8), U_NOP});                                     // 0
    for (uint32_t i = 0; i < 10; ++i) {
        p.code.push_back({L_NOP, EncUpper(F_ADD, 1 + (i % 6), 1 + ((i + 2) % 6), 0, 0xF)});  // 1..10: static run
    }
    p.code.push_back({EncXgkick(1), EncUpper(F_ADD, 7, 1, 2, 0xF)});                  // 11: kick
    p.code.push_back({EncXgkick(1), EncUpper(F_ADD, 8, 7, 2, 0xF)});                  // 12: second kick: stalls
    p.code.push_back({L_NOP, EncUpper(F_ADD, 9, 8, 7, 0xF)});                         // 13: reads 8 and 7 after the advance
    p.code.push_back({L_NOP, EncUpper(F_ADD, 10, 9, 8, 0xF)});                        // 14
    p.code.push_back({L_NOP, EncUpper(F_ADD, 11, 10, 1, 0xF)});                       // 15
    p.code.push_back({EncDiv(1, 0, 2, 1), EncUpper(F_ADD, 12, 11, 1, 0xF)});          // 16: Q = vf1.x / vf2.y
    p.code.push_back({L_NOP, EncUpper(F_MULQ, 13, 12, 0, 0xF)});                      // 17: MULq (the pending Q)
    p.code.push_back({EncWaitQ(), EncUpper(F_ADD, 14, 13, 1, 0xF)});                  // 18: WAITQ
    p.code.push_back({L_NOP, EncUpper(F_MULQ, 15, 14, 0, 0xF)});                      // 19
    for (uint32_t i = 0; i < 8; ++i) p.code.push_back({L_NOP, EncUpper(F_ADD, 16 + i, 15 + i, 1, 0xF)});  // 20..27
    Append(p.code, Tail());
    p.setup = [](VuUnit& vu) {
        SeedVf(vu);
        WriteKickPacket(vu);
    };
    const Outcome b = RunCase(p);
    CHECK(b.opcount == 8);  // two kicks of the same packet
    CHECK(b.ops_static > 0);
}

// (f) XTOP yields: inside a block body (TPC = the XTOP's address) and inside
// a delay slot (TPC untouched), after 1024 reads without a kick.
void TestXtopYield() {
    {
        Program p = Prog("xtop_yield_body");
        p.code.push_back({EncIaddiu(1, 0, 0), U_NOP});                       // 0
        p.code.push_back({EncIaddiu(1, 1, 1), EncUpper(F_ADD, 1, 2, 3, 0xF)});  // 1: L
        p.code.push_back({EncXtop(2), EncUpper(F_ADD, 4, 1, 1, 0xF)});       // 2: XTOP inside the body
        p.code.push_back({EncB(-3), EncUpper(F_ADD, 5, 4, 1, 0xF)});         // 3: B L
        p.code.push_back({EncIaddiu(3, 3, 1), U_NOP});                       // 4: ds
        Append(p.code, Tail());
        p.setup = [](VuUnit& vu) {
            SeedVf(vu);
            vu.SetVifTop(0x321, 0x12);
        };
        const Outcome b = RunCase(p, 97);
        CHECK(b.regs.VI[2] == 0x321 && b.regs.VI[1] == 1025);
        CHECK(b.regs.TPC == 2 * 8);
    }
    {
        Program p = Prog("xtop_yield_ds");
        p.code.push_back({EncIaddiu(1, 0, 0), U_NOP});                       // 0
        p.code.push_back({EncIaddiu(1, 1, 1), EncUpper(F_ADD, 1, 2, 3, 0xF)});  // 1: L
        p.code.push_back({EncB(-2), EncUpper(F_ADD, 5, 1, 1, 0xF)});         // 2: B L
        p.code.push_back({EncXtop(2), U_NOP});                               // 3: ds: XTOP
        Append(p.code, Tail());
        p.setup = [](VuUnit& vu) {
            SeedVf(vu);
            vu.SetVifTop(0x321, 0x12);
        };
        const Outcome b = RunCase(p, 89);
        CHECK(b.regs.VI[2] == 0x321 && b.regs.VI[1] == 1025);
        CHECK(b.regs.TPC == 3 * 8);  // the branch bundle's advance; the delay slot does not move it
    }
}

// (g) The NOP sled: four all-zero bundles end the run; a NOP pair in a delay
// slot counts toward the sled at the branch target.
void TestNopSled() {
    {
        Program p = Prog("nop_sled");
        p.code.push_back({EncIaddiu(1, 0, 3), EncUpper(F_ADD, 1, 2, 3, 0xF)});
        p.code.push_back({EncIaddiu(2, 0, 4), U_NOP});
        for (int i = 0; i < 6; ++i) p.code.push_back({0, 0});
        p.setup = SeedVf;
        const Outcome b = RunCase(p);
        CHECK(b.bundles == 2 + 4 && b.regs.VI[1] == 3 && b.regs.VI[2] == 4);
    }
    {
        Program p = Prog("nop_sled_ds");
        p.code.push_back({EncB(2), U_NOP});    // 0: -> slot 3
        p.code.push_back({0, 0});              // 1: ds: NOP pair (count 1)
        p.code.push_back({EncIaddiu(9, 0, 9), U_NOP});  // 2: skipped
        for (int i = 0; i < 6; ++i) p.code.push_back({0, 0});  // 3..: three more end the run
        const Outcome b = RunCase(p);
        CHECK(b.bundles == 2 + 3 && b.regs.VI[9] == 0);
    }
}

// (h) The image end: an E-bit terminal on the last slot (the reference ends
// the run without its delay slot) and a taken branch there (its delay slot at
// address 0, not merged: the block does not wrap).
void TestWrap() {
    {
        Program p = Prog("wrap_ebit");
        p.code.push_back({EncIaddiu(2, 0, 7), U_NOP});          // slot 0: the delay slot
        p.code.push_back({L_NOP, U_NOP});
        p.code.push_back({L_NOP, U_NOP});
        p.setup = [](VuUnit& vu) {
            Bundle last{EncIaddiu(1, 0, 5), U_NOP | U_EBIT};
            uint8_t raw[8];
            std::memcpy(raw, &last.lower, 4);
            std::memcpy(raw + 4, &last.upper, 4);
            vu.WriteMicroMem(0x3FF8, raw, 8);
        };
        p.start_pc = 0x3FF8;
        const Outcome b = RunCase(p);
        // The reference ends the run at the E bundle: its delay slot's address is
        // past the micro memory (the interpreter's "past the image" rule).
        CHECK(b.regs.VI[1] == 5 && b.regs.VI[2] == 0 && b.regs.TPC == 0 && b.bundles == 1);
    }
    {
        Program p = Prog("wrap_branch");
        p.code.push_back({EncIaddiu(2, 0, 7), U_NOP});          // slot 0: the delay slot
        p.code.push_back({EncIaddiu(3, 0, 3), U_NOP});          // slot 1: skipped
        p.code.push_back({EncIaddiu(4, 0, 4), U_NOP | U_EBIT});  // slot 2: the target
        p.code.push_back({L_NOP, U_NOP});
        p.setup = [](VuUnit& vu) {
            // B from 0x3FF8: target = (0x3FF8 + 8 + imm*8) & 0x3FFF = 16 -> imm = 2 - 0 ... (0x4000 + imm*8) & mask = imm*8 -> imm 2
            Bundle last{EncB(2), U_NOP};
            uint8_t raw[8];
            std::memcpy(raw, &last.lower, 4);
            std::memcpy(raw + 4, &last.upper, 4);
            vu.WriteMicroMem(0x3FF8, raw, 8);
        };
        p.start_pc = 0x3FF8;
        const Outcome b = RunCase(p);
        CHECK(b.regs.VI[2] == 7 && b.regs.VI[3] == 0 && b.regs.VI[4] == 4 && b.regs.TPC == 2 * 8 + 16);
    }
}

// (i) A misaligned entry runs on the interpreter (identical by construction).
void TestMisaligned() {
    Program p = Prog("misaligned");
    p.code.push_back({EncIaddiu(1, 0, 3), U_NOP});
    p.code.push_back({EncIaddiu(2, 0, 4), U_NOP});
    Append(p.code, Tail());
    p.start_pc = 4;
    const Outcome b = RunMode(VuTierMode::Block, p, 50);  // (the misaligned words are whatever they are: capped)
    const Outcome i = RunMode(VuTierMode::Interp, p, 50);
    CHECK(Same(b, i, p.tag));
    CHECK(b.blocks_run == 0 && b.bundles == i.bundles);
    std::printf("  %-20s bundles=%llu (interpreter leg)\n", p.tag, static_cast<unsigned long long>(b.bundles));
}

// (j) The static schedule against the stall model: random straight-line FMAC
// sequences from an empty ring, the model's stall per bundle == s_delta.
void TestScheduleProperty() {
    uint32_t seed = 0x1234567u;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    int mismatches = 0;
    for (int trial = 0; trial < 3000; ++trial) {
        const int n = 2 + static_cast<int>(rnd() % 10);
        std::vector<VuDecodedSlot> slots(static_cast<size_t>(n));
        std::vector<VuBlockOp> ops(static_cast<size_t>(n));
        for (int k = 0; k < n; ++k) {
            const uint32_t funcs[5] = {F_ADD, F_MUL, F_SUB, 0x00 + (rnd() % 4), 0x18 + (rnd() % 4)};  // + ADDbc, MULbc
            const uint32_t func = funcs[rnd() % 5];
            const uint32_t fd = rnd() % 8, fs = rnd() % 8, ft = rnd() % 8, dest = 1 + (rnd() % 15);
            const uint32_t upper = EncUpper(func, fd, fs, ft, dest);
            const uint32_t lower = (rnd() % 3 == 0) ? EncLq(1 + (rnd() % 7), 0, static_cast<int32_t>(rnd() % 4)) : L_NOP;
            slots[static_cast<size_t>(k)] = VuDecoder::DecodeVuSlot(lower, upper);
            VuBlockTier::ResolveOp(ops[static_cast<size_t>(k)], slots[static_cast<size_t>(k)]);
        }
        VuBlockTier::Schedule(ops.data(), ops.size());
        StallModel model;
        FlagPipeline pipes;
        VUregs regs{};
        for (int k = 0; k < n; ++k) {
            ++pipes.cycle();
            const uint64_t before = pipes.cycle();
            model.Apply(slots[static_cast<size_t>(k)], regs, pipes);
            const uint64_t stall = pipes.cycle() - before;
            if (stall != ops[static_cast<size_t>(k)].s_delta) {
                if (mismatches < 5)
                    std::printf("  schedule trial %d op %d model=%llu s_delta=%u\n", trial, k, static_cast<unsigned long long>(stall),
                                ops[static_cast<size_t>(k)].s_delta);
                ++mismatches;
            }
        }
    }
    CHECK(mismatches == 0);
    std::printf("  schedule_property     3000 random sequences, mismatches=%d\n", mismatches);
}

// (k) VU0 never runs here: under Block mode a VU0 program goes to the cached tier.
void TestVu0OnCached() {
    VuUnit vu(0);
    std::vector<Bundle> p;
    p.push_back({EncIaddiu(1, 0, 3), U_NOP});
    Append(p, Tail());
    WriteProgram(vu, p);
    vu.dispatch().settings().vu1_mode = VuTierMode::Block;
    vu.Execute(0);
    CHECK(vu.regs().VI[1] == 3);
    CHECK(vu.dispatch().block().bundles() == 0 && vu.dispatch().cached().bundles() == 3);
}

// (l) The program table: epoch fast path, hash hit after a rewrite, eviction at 64.
void TestTable() {
    VuUnit vu(1);
    VuBlockTier& tier = vu.dispatch().block();
    std::vector<Bundle> a = {{EncIaddiu(1, 0, 1), U_NOP}};
    Append(a, Tail());
    WriteProgram(vu, a);
    VuBlockProgram* pa = tier.Acquire(vu);
    CHECK(tier.table_size() == 1 && pa->exec_count == 1);
    CHECK(tier.Acquire(vu) == pa && pa->exec_count == 2);  // same epoch: no hash
    vu.dispatch().settings().vu1_mode = VuTierMode::Block;
    vu.Execute(0);
    CHECK(pa->exec_count == 3 && pa->bundles == 3 && pa->blocks.size() >= 1);
    WriteProgram(vu, a);  // the same image rewritten: a new epoch, the hash finds it
    CHECK(tier.Acquire(vu) == pa && tier.table_size() == 1 && pa->exec_count == 4);
    for (uint32_t k = 0; k < 63; ++k) {
        std::vector<Bundle> b = {{EncIaddiu(1, 0, 2 + k), U_NOP}};
        Append(b, Tail());
        WriteProgram(vu, b);
        tier.Acquire(vu);
    }
    CHECK(tier.table_size() == 64);
    std::vector<Bundle> z = {{EncIaddiu(1, 0, 99), U_NOP}};
    Append(z, Tail());
    WriteProgram(vu, z);
    VuBlockProgram* pz = tier.Acquire(vu);
    CHECK(tier.table_size() == 64 && pz->exec_count == 1);
    WriteProgram(vu, a);
    CHECK(tier.Acquire(vu) == pa);  // `a` survived (exec_count 4 vs the 1-count images)
    CHECK(tier.table_size() == 64);
}

}  // namespace

int main() {
    TestFmacChain();
    TestLoopIalu();
    TestSubroutines();
    TestHazards();
    TestDivAndKicks();
    TestXtopYield();
    TestNopSled();
    TestWrap();
    TestMisaligned();
    TestScheduleProperty();
    TestVu0OnCached();
    TestTable();
    if (g_fail) {
        std::printf("VuBlockTier: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("VuBlockTier: ALL PASS\n");
    return 0;
}
