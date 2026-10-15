// VuInterpSmoke: the interpreter tier's bundle-loop rules on hand-encoded
// programs (E-bit delay slot + TPC, branch delay slot, I-bit ordering, VF0/VI0
// pins, discard_lower / hide_upper hazards, NOP-sled stop, cycle cap, XTOP
// yield, VU0 self-spin park + resume, the VU0 4 KB edge), XGKICK issued from a
// microprogram through GifArbiter -> GifParser -> RegisterOpStream, and one
// real recorded VU1 case from the frozen corpus run end to end (SKIP 77 when
// the corpus is absent).
#include <cstdio>
#include <cstring>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/vu/tiers/vu_interpreter_tier.h"
#include "engine/vu/vu1_case.h"
#include "engine/vu/vu_image_hash.h"
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

struct TestVuInterpSmoke {
    RegisterOpStream ops;
    GifRegs regs;
    GifParser parser;
    GifArbiter arbiter;
    TestVuInterpSmoke() : parser(ops, regs), arbiter(parser) {}
};

void PutBundle(VuUnit& vu, uint32_t pc, uint32_t lower, uint32_t upper) {
    uint8_t b[8];
    std::memcpy(b, &lower, 4);
    std::memcpy(b + 4, &upper, 4);
    vu.WriteMicroMem(pc, b, 8);
}

uint32_t EncUpper(uint32_t func, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t dest) {
    return func | (fd << 6) | (fs << 11) | (ft << 16) | (dest << 21);
}
// IADDIU it, is, imm15
uint32_t EncIaddiu(uint32_t it, uint32_t is, uint32_t imm15) {
    return (0x08u << 25) | (((imm15 >> 11) & 0xFu) << 21) | (it << 16) | (is << 11) | (imm15 & 0x7FFu);
}
// B imm11 (relative to the next bundle, in bundles)
uint32_t EncB(int32_t imm11) { return (0x20u << 25) | (static_cast<uint32_t>(imm11) & 0x7FFu); }
uint32_t EncIbne(uint32_t it, uint32_t is, int32_t imm11) {
    return (0x29u << 25) | (it << 16) | (is << 11) | (static_cast<uint32_t>(imm11) & 0x7FFu);
}
uint32_t EncIbeq(uint32_t it, uint32_t is, int32_t imm11) {
    return (0x28u << 25) | (it << 16) | (is << 11) | (static_cast<uint32_t>(imm11) & 0x7FFu);
}
// MOVE.dest ft, fs
uint32_t EncMove(uint32_t ft, uint32_t fs, uint32_t dest) { return (0x40u << 25) | (dest << 21) | (ft << 16) | (fs << 11) | (0x0Cu << 6) | 0x3Cu; }
// XTOP it / XGKICK is
uint32_t EncXtop(uint32_t it) { return (0x40u << 25) | (it << 16) | (0x1Au << 6) | 0x3Cu; }
uint32_t EncXgkick(uint32_t is) { return (0x40u << 25) | (is << 11) | (0x1Bu << 6) | 0x3Cu; }

void TestEbitAndTpc() {
    VuUnit vu(1);
    // 0x00 ADDw vf1 = vf0 + vf0.w (1.0) ; 0x08 E-bit + ADDw vf2 ; 0x10 delay slot ADDw vf3 ; 0x18 never runs
    PutBundle(vu, 0x00, L_NOP, EncUpper(0x03, 1, 0, 0, 0xF));
    PutBundle(vu, 0x08, L_NOP, EncUpper(0x03, 2, 0, 0, 0xF) | U_EBIT);
    PutBundle(vu, 0x10, L_NOP, EncUpper(0x03, 3, 0, 0, 0xF));
    PutBundle(vu, 0x18, L_NOP, EncUpper(0x03, 4, 0, 0, 0xF));
    VuInterpreterTier tier;
    tier.Run(vu, 0);
    CHECK(vu.regs().VF[1].x == 1.0f && vu.regs().VF[2].x == 1.0f && vu.regs().VF[3].x == 1.0f);
    CHECK(vu.regs().VF[4].x == 0.0f);
    CHECK(vu.regs().TPC == 0x18u);  // E-bit address (0x08) + 16
    CHECK(tier.bundles() == 3);
    CHECK(vu.cycle() == 3 + 4);     // three bundles + the program-end flush of vf3's writeback (issue + 4)
}

void TestBranchDelaySlot() {
    VuUnit vu(1);
    // 0x00 B +3 (target = 0x08 + 3*8 = 0x20) ; 0x08 delay slot: vi1 = 5 ; 0x10/0x18 skipped ; 0x20 E ; 0x28 delay.
    PutBundle(vu, 0x00, EncB(3), U_NOP);
    PutBundle(vu, 0x08, EncIaddiu(1, 0, 5), U_NOP);
    PutBundle(vu, 0x10, EncIaddiu(2, 0, 7), U_NOP);
    PutBundle(vu, 0x18, EncIaddiu(3, 0, 9), U_NOP);
    PutBundle(vu, 0x20, L_NOP, U_NOP | U_EBIT);
    PutBundle(vu, 0x28, EncIaddiu(4, 0, 11), U_NOP);
    VuInterpreterTier tier;
    tier.Run(vu, 0);
    CHECK(vu.regs().VI[1] == 5 && vu.regs().VI[2] == 0 && vu.regs().VI[3] == 0 && vu.regs().VI[4] == 11);
    CHECK(vu.regs().TPC == 0x30u);
    CHECK(tier.bundles() == 4);
    // A branch inside a delay slot is ignored (kept divergence).
    VuUnit vu2(1);
    PutBundle(vu2, 0x00, EncB(3), U_NOP);       // -> 0x20
    PutBundle(vu2, 0x08, EncB(4), U_NOP);       // in the delay slot: ignored
    PutBundle(vu2, 0x20, EncIaddiu(1, 0, 1), U_NOP | U_EBIT);
    PutBundle(vu2, 0x28, L_NOP, U_NOP);
    PutBundle(vu2, 0x30, EncIaddiu(2, 0, 2), U_NOP | U_EBIT);
    PutBundle(vu2, 0x38, L_NOP, U_NOP);
    tier.Run(vu2, 0);
    CHECK(vu2.regs().VI[1] == 1 && vu2.regs().VI[2] == 0 && vu2.regs().TPC == 0x30u);
}

void TestIbitAndPins() {
    VuUnit vu(1);
    uint32_t bits = 0;
    const float two = 2.0f;
    std::memcpy(&bits, &two, 4);
    vu.regs().I = bits;  // old I = 2.0
    const float three = 3.0f;
    std::memcpy(&bits, &three, 4);
    // 0x00: I-bit bundle: ADDi vf1 = vf0 + I (sees OLD I = 2.0), then I = 3.0
    PutBundle(vu, 0x00, bits, EncUpper(0x22, 1, 0, 0, 0xF) | U_IBIT);
    // 0x08: ADDi vf2 = vf0 + I (3.0); lower tries to write VI0 and VF0 (dropped / re-pinned)
    PutBundle(vu, 0x08, EncIaddiu(0, 0, 77), EncUpper(0x22, 2, 0, 0, 0xF));
    PutBundle(vu, 0x10, L_NOP, EncUpper(0x28, 0, 2, 2, 0xF) | U_EBIT);  // ADD vf0 = vf2 + vf2 (write suppressed)
    PutBundle(vu, 0x18, L_NOP, U_NOP);
    VuInterpreterTier tier;
    tier.Run(vu, 0);
    CHECK(vu.regs().VF[1].x == 2.0f && vu.regs().VF[1].w == 3.0f);
    CHECK(vu.regs().VF[2].x == 3.0f && vu.regs().VF[2].w == 4.0f);
    CHECK(vu.regs().VF[0].x == 0.0f && vu.regs().VF[0].w == 1.0f && vu.regs().VI[0] == 0);
    CHECK(vu.regs().I == bits);
}

void TestSameBundleHazards() {
    // hide_upper: upper writes vf1, lower MOVE vf2 <- vf1 sees the pre-upper vf1.
    VuUnit vu(1);
    vu.regs().VF[1].x = 5.0f;
    PutBundle(vu, 0x00, EncMove(2, 1, 0xF), EncUpper(0x03, 1, 0, 0, 0xF));  // vf1 = 1.0 ; vf2 = old vf1
    PutBundle(vu, 0x08, L_NOP, U_NOP | U_EBIT);
    PutBundle(vu, 0x10, L_NOP, U_NOP);
    VuInterpreterTier tier;
    tier.Run(vu, 0);
    CHECK(vu.regs().VF[1].x == 1.0f && vu.regs().VF[2].x == 5.0f);
    // discard_lower: both write vf3 -> the lower is not issued at all.
    VuUnit vu2(1);
    vu2.regs().VF[4].x = 9.0f;
    PutBundle(vu2, 0x00, EncMove(3, 4, 0xF), EncUpper(0x03, 3, 0, 0, 0xF));  // upper vf3 = 1.0 wins
    PutBundle(vu2, 0x08, L_NOP, U_NOP | U_EBIT);
    PutBundle(vu2, 0x10, L_NOP, U_NOP);
    tier.Run(vu2, 0);
    CHECK(vu2.regs().VF[3].x == 1.0f && vu2.regs().VF[3].w == 2.0f);
}

void TestNopSledAndCap() {
    VuUnit vu(1);  // all-zero micro memory: four zero bundles end the run, TPC untouched
    vu.regs().TPC = 0x1234;
    VuInterpreterTier tier;
    tier.Run(vu, 0x100);
    CHECK(tier.bundles() == 4);
    CHECK(vu.regs().TPC == 0x118u);  // advanced by the three completed bundles (0x100 -> 0x118)
    // Cycle cap: a tight self-loop on VU1 is cut after `cycle_limit` bundles.
    VuUnit vu2(1);
    PutBundle(vu2, 0x00, EncB(-1), U_NOP);  // B to itself
    PutBundle(vu2, 0x08, L_NOP, U_NOP);
    VuInterpreterTier::RunOptions opt;
    opt.cycle_limit = 100;
    VuInterpreterTier t2;
    t2.Run(vu2, 0, opt);
    CHECK(t2.bundles() == 100);
    CHECK(!vu2.state().suspended);  // VU1 never parks
}

void TestXtopYield() {
    VuUnit vu(1);
    vu.SetVifTop(0x123, 0x45);
    // 0x00 XTOP vi1 ; 0x08 B -2 (back to 0x00) ; delay slot 0x10 nop
    PutBundle(vu, 0x00, EncXtop(1), U_NOP);
    PutBundle(vu, 0x08, EncB(-2), U_NOP);
    PutBundle(vu, 0x10, L_NOP, U_NOP);
    VuInterpreterTier tier;
    tier.Run(vu, 0);
    CHECK(vu.regs().VI[1] == 0x123);
    CHECK(tier.bundles() == 1025 * 3 - 2);  // the 1025th XTOP read yields at its bundle boundary
    CHECK(!vu.state().xtop_exit_requested && vu.state().xtop_read_count == 0);
    CHECK(vu.regs().TPC == 0x10u);          // TPC as set at the last normal advance (the B bundle); redirects do not set it
}

void TestVu0SelfSpinPark() {
    VuUnit vu(0);
    vu.regs().VI[1] = 1;
    vu.regs().VI[2] = 2;
    // 0x00 IBNE vi1, vi2, -1 (self) ; 0x08 delay nop ; -> parked after the second issue
    PutBundle(vu, 0x00, EncIbne(1, 2, -1), U_NOP);
    PutBundle(vu, 0x08, L_NOP, U_NOP);
    PutBundle(vu, 0x10, EncIaddiu(3, 0, 1), U_NOP | U_EBIT);
    PutBundle(vu, 0x18, L_NOP, U_NOP);
    VuInterpreterTier tier;
    tier.Run(vu, 0);
    CHECK(vu.state().suspended && vu.state().suspended_tpc == 0 && vu.regs().TPC == 0);
    CHECK(tier.bundles() == 3);  // spin, delay, spin (count 2) -> park
    // The EE releases the gate: resume falls through to the E bit.
    vu.regs().VI[2] = 1;
    tier.ResumeIfWaiting(vu);
    CHECK(!vu.state().suspended && vu.regs().VI[3] == 1 && vu.regs().TPC == 0x20u);
    tier.ResumeIfWaiting(vu);  // not parked: no-op
    CHECK(vu.regs().TPC == 0x20u);
    // An IBEQ that is not a self-branch (target != pc) never parks.
    VuUnit vu2(0);
    PutBundle(vu2, 0x00, EncIbeq(0, 0, 1), U_NOP);  // -> 0x10
    PutBundle(vu2, 0x08, L_NOP, U_NOP);
    PutBundle(vu2, 0x10, L_NOP, U_NOP | U_EBIT);
    PutBundle(vu2, 0x18, L_NOP, U_NOP);
    tier.Run(vu2, 0);
    CHECK(!vu2.state().suspended && vu2.regs().TPC == 0x20u);
}

void TestVu0SizeEdge() {
    VuUnit vu(0);  // VU0: the run ends silently once pc reaches 0x1000
    PutBundle(vu, 0xFF0, EncIaddiu(1, 0, 3), U_NOP);
    PutBundle(vu, 0xFF8, EncIaddiu(2, 0, 4), U_NOP);
    VuInterpreterTier tier;
    tier.Run(vu, 0xFF0);
    CHECK(vu.regs().VI[1] == 3 && vu.regs().VI[2] == 4);
    CHECK(tier.bundles() == 2 && vu.regs().TPC == 0x1000u);
}

void TestXgkickFromMicro() {
    TestVuInterpSmoke rig;
    VuUnit vu(1, &rig.arbiter);
    // A PACKED GIFtag at data QW 8: NLOOP 2, EOP, NREG 1 (PRIM 0x00 register A+D), 2 A+D quadwords.
    uint8_t qw[16] = {};
    const uint64_t tag_lo = 2ull | (1ull << 15) | (0ull << 58) | (1ull << 60);
    const uint64_t tag_hi = 0x0Eull;  // REGS0 = A+D
    std::memcpy(qw, &tag_lo, 8);
    std::memcpy(qw + 8, &tag_hi, 8);
    vu.WriteDataMem(8 * 16, qw, 16);
    const uint64_t ad0_lo = 0x1234ull, ad0_hi = 0x47ull;  // TEST_1 (0x47)
    std::memcpy(qw, &ad0_lo, 8);
    std::memcpy(qw + 8, &ad0_hi, 8);
    vu.WriteDataMem(9 * 16, qw, 16);
    const uint64_t ad1_lo = 0x5678ull, ad1_hi = 0x42ull;  // ALPHA_1 (0x42)
    std::memcpy(qw, &ad1_lo, 8);
    std::memcpy(qw + 8, &ad1_hi, 8);
    vu.WriteDataMem(10 * 16, qw, 16);
    // 0x00 vi1 = 8 ; 0x08 XGKICK vi1 ; 0x10 E ; 0x18 delay
    PutBundle(vu, 0x00, EncIaddiu(1, 0, 8), U_NOP);
    PutBundle(vu, 0x08, EncXgkick(1), U_NOP);
    PutBundle(vu, 0x10, L_NOP, U_NOP | U_EBIT);
    PutBundle(vu, 0x18, L_NOP, U_NOP);
    VuInterpreterTier tier;
    tier.Run(vu, 0);
    CHECK(vu.state().xgkick_count == 1 && vu.state().xgkick_has_fired);
    CHECK(!vu.xgkick()->armed());  // completed by the program-end flush
    rig.arbiter.drain();
    CHECK(rig.arbiter.DrainedPackets() == 1);
    CHECK(rig.ops.Count() == 4);  // Tag, two A+D register writes, Eop
    std::printf("  xgkick-from-micro: ops=%zu hash=%016llx cycles=%llu\n", rig.ops.Count(),
                static_cast<unsigned long long>(rig.ops.Hash()), static_cast<unsigned long long>(vu.cycle()));
}

int RunCorpusCase(const char* path) {
    Vu1Case* c = new Vu1Case();
    if (!vu1_case::ReadCase(path, *c)) {
        std::printf("SKIP: corpus case not readable: %s\n", path);
        delete c;
        return 77;
    }
    TestVuInterpSmoke rig;
    VuUnit vu(1, &rig.arbiter);
    vu.LoadCase(*c);
    const uint64_t cycle0 = vu.cycle();
    VuInterpreterTier tier;
    tier.Run(vu, c->start_pc);
    rig.arbiter.drain();
    CHECK(tier.bundles() > 0 && tier.bundles() < tier.cycle_cap());  // ended at an E bit within the cap
    CHECK(vu.cycle() > cycle0);
    CHECK(!vu.state().suspended);
    CHECK(rig.arbiter.DrainedPackets() >= 1);  // a VU1 program kicks at least one packet
    std::printf("  corpus %s: start_pc=%04x image=%016llx bundles=%llu cycles=%llu kicks=%d packets=%llu ops=%zu ophash=%016llx TPC=%04x\n",
                path, c->start_pc, static_cast<unsigned long long>(VuImageHash::Fast(c->micro_mem, Vu1Case::kMicroSize)),
                static_cast<unsigned long long>(tier.bundles()), static_cast<unsigned long long>(vu.cycle() - cycle0),
                vu.state().xgkick_count, static_cast<unsigned long long>(rig.arbiter.DrainedPackets()), rig.ops.Count(),
                static_cast<unsigned long long>(rig.ops.Hash()), vu.regs().TPC);
    delete c;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    TestEbitAndTpc();
    TestBranchDelaySlot();
    TestIbitAndPins();
    TestSameBundleHazards();
    TestNopSledAndCap();
    TestXtopYield();
    TestVu0SelfSpinPark();
    TestVu0SizeEdge();
    TestXgkickFromMicro();
    int rc = 0;
    if (argc > 1) rc = RunCorpusCase(argv[1]);
    if (g_fail) {
        std::printf("VuInterpSmoke: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("VuInterpSmoke: ALL PASS%s\n", rc == 77 ? " (corpus case SKIPPED)" : "");
    return rc;
}
