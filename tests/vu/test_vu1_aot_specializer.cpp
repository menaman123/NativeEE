// Vu1AotSpecializer: the AOT tier's (kind, dest)-specialised instruction bodies
// (VuCachedTier::UpperFastFnD / LowerFastFnD) against the dynamic bodies
// (UpperFastFn / LowerFastFn) the cached and block tiers run, one bundle word at
// a time on two units in the same random state: every upper encoding (func 0x00-
// 0x3F x the special-2 op field) and every lower encoding (opcode 0x00-0x7F, the
// 0x40 group's fn x funct2) under every dest mask 0..15 and several random
// register files -> identical registers (the whole file), data memory, clock,
// branch verdict and target after the pipes drain. The monomorphisation identity,
// kind by kind; the test also requires every (upper kind, dest) pair and every
// (dest-reading lower kind, dest) pair to have been exercised.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <utility>

#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu_decoder.h"
#include "engine/vu/vu_unit.h"

namespace {

int g_fails = 0;
#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            if (g_fails < 40) {                                             \
                std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
                std::printf(__VA_ARGS__);                                   \
                std::printf("\n");                                          \
            }                                                               \
            ++g_fails;                                                      \
        }                                                                   \
    } while (0)

// The test's deterministic random source (an LCG over the seed word).
struct Lcg {
    uint64_t s;
};
uint32_t Next(Lcg& r) {
    r.s = r.s * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<uint32_t>(r.s >> 32);
}

// A random lane: mostly normal floats, with zeros, denormals and Inf/NaN patterns mixed in.
uint32_t RandomLane(Lcg& r) {
    const uint32_t k = Next(r) % 16;
    const uint32_t bits = Next(r);
    if (k < 10) return (bits & 0x807FFFFFu) | ((0x70u + (Next(r) % 0x20)) << 23);  // exponents 0x70..0x8F
    if (k < 12) return bits & 0x80000000u;                                           // +/-0
    if (k < 13) return bits & 0x807FFFFFu;                                           // denormal
    if (k < 14) return (bits & 0x807FFFFFu) | 0x7F800000u;                           // Inf/NaN
    return bits;                                                                      // anything
}

struct Snapshot {
    VUregs regs;
    uint8_t data[16384];
    uint64_t cycle;
    uint16_t vif_top, vif_itop;
};

void Randomize(Lcg& r, Snapshot& s) {
    std::memset(&s, 0, sizeof(s));
    for (int i = 0; i < 32; ++i)
        for (int k = 0; k < 4; ++k) s.regs.VF[i].I[k] = RandomLane(r);
    s.regs.VF[0].x = 0.0f; s.regs.VF[0].y = 0.0f; s.regs.VF[0].z = 0.0f; s.regs.VF[0].w = 1.0f;
    for (int i = 0; i < 16; ++i) s.regs.VI[i] = static_cast<uint16_t>(Next(r));
    s.regs.VI[0] = 0;
    for (int k = 0; k < 4; ++k) s.regs.ACC.I[k] = RandomLane(r);
    uint32_t q = RandomLane(r), p = RandomLane(r);
    std::memcpy(&s.regs.Q, &q, 4);
    std::memcpy(&s.regs.P, &p, 4);
    s.regs.I = RandomLane(r);
    s.regs.R = (Next(r) & 0x7FFFFFu) | 0x3F800000u;
    s.regs.status_flag = static_cast<uint16_t>(Next(r) & 0xFFFu);
    s.regs.mac_flag = static_cast<uint16_t>(Next(r));
    s.regs.clip_flag = Next(r) & 0xFFFFFFu;
    for (size_t i = 0; i < sizeof(s.data); i += 4) {
        const uint32_t v = RandomLane(r);
        std::memcpy(s.data + i, &v, 4);
    }
    s.cycle = 1000 + (Next(r) % 1000);
    s.vif_top = static_cast<uint16_t>(Next(r) & 0x3FFu);
    s.vif_itop = static_cast<uint16_t>(Next(r) & 0x3FFu);
}

// Every pipe drained into the registers (the interpreter's program-end drain, piecewise).
void Drain(VuUnit& u) {
    FlagPipeline& p = u.flags();
    p.FlushMacPipe(u.regs());
    p.FlushClipPipe(u.regs());
    p.FlushQPipe(u.regs());
    p.FlushPPipe(u.regs());
    u.stalls().FlushFmacWb(p);
    u.stalls().FlushIaluWb(p);
}

void Restore(VuUnit& u, const Snapshot& s) {
    u.Reset();
    Drain(u);  // empties the MAC / CLIP / Q / P pipes left by the previous op
    std::memcpy(&u.regs(), &s.regs, sizeof(VUregs));
    u.WriteDataMem(0, s.data, sizeof(s.data));
    u.flags().SetCycle(s.cycle);
    u.SetVifTop(s.vif_top, s.vif_itop);
}

struct Outcome {
    VUregs regs;
    uint8_t data[16384];
    uint64_t cycle;
    bool taken;
    uint32_t target;
};

void Capture(VuUnit& u, bool taken, uint32_t target, Outcome& o) {
    Drain(u);
    std::memcpy(&o.regs, &u.regs(), sizeof(VUregs));
    std::memcpy(o.data, u.state().data, sizeof(o.data));
    o.cycle = u.cycle();
    o.taken = taken;
    o.target = target;
}

bool SameOutcome(const Outcome& a, const Outcome& b) {
    return std::memcmp(&a.regs, &b.regs, sizeof(VUregs)) == 0 && std::memcmp(a.data, b.data, sizeof(a.data)) == 0 &&
           a.cycle == b.cycle && a.taken == b.taken && a.target == b.target;
}

}  // namespace

int main(int argc, char** argv) {
    const int picks = argc > 1 ? std::atoi(argv[1]) : 3;  // random register files per encoding
    Lcg rng{0x5eed5eed1234ull};
    VuUnit a(1), b(1);
    Snapshot snap;
    Outcome oa, ob;
    std::set<std::pair<int, int>> upper_seen, lower_seen;
    uint64_t runs = 0;

    // ---- upper words: func 0-0x3F, the special-2 op field 0-0x1F (fd's bits), dest 0-15 ----
    for (uint32_t func = 0; func < 0x40; ++func) {
        for (uint32_t op = 0; op < 0x20; ++op) {
            for (uint32_t dest = 0; dest < 16; ++dest) {
                for (int pick = 0; pick < picks; ++pick) {
                    const uint32_t fs = Next(rng) & 0x1Fu, ft = Next(rng) & 0x1Fu;
                    const uint32_t upper = func | (op << 6) | (fs << 11) | (ft << 16) | (dest << 21);
                    const VuDecodedSlot s = VuDecoder::DecodeVuSlot(0x8000033Cu /* a NOP lower */, upper);
                    Randomize(rng, snap);
                    Restore(a, snap);
                    Restore(b, snap);
                    VuCachedTier::UpperFastFn(s.upper_kind)(a.dispatch().interp(), a, s);
                    VuCachedTier::UpperFastFnD(s.upper_kind, s.u_dest)(b.dispatch().interp(), b, s);
                    Capture(a, false, 0, oa);
                    Capture(b, false, 0, ob);
                    CHECK(SameOutcome(oa, ob), "upper %08x kind=%u dest=%x pick=%d", upper, s.upper_kind, s.u_dest, pick);
                    upper_seen.insert({s.upper_kind, s.u_dest});
                    ++runs;
                }
            }
        }
    }
    // ---- lower words: opcode 0-0x7F; the 0x40 group over fn x funct2; dest 0-15 ----
    auto lower_case = [&](uint32_t lower) {
        const VuDecodedSlot s = VuDecoder::DecodeVuSlot(lower, 0x000002FFu /* upper NOP */);
        for (int pick = 0; pick < picks; ++pick) {
            Randomize(rng, snap);
            const uint32_t pc = (Next(rng) & 0x3FF8u);
            Restore(a, snap);
            Restore(b, snap);
            bool ta = false, tb = false;
            uint32_t ga = 0, gb = 0;
            VuCachedTier::LowerFastFn(s.lower_kind)(a.dispatch().interp(), a, s, pc, ta, ga);
            VuCachedTier::LowerFastFnD(s.lower_kind, s.l_dest)(b.dispatch().interp(), b, s, pc, tb, gb);
            Capture(a, ta, ga, oa);
            Capture(b, tb, gb, ob);
            CHECK(SameOutcome(oa, ob), "lower %08x kind=%u dest=%x pick=%d", lower, s.lower_kind, s.l_dest, pick);
            lower_seen.insert({s.lower_kind, s.l_dest});
            ++runs;
        }
    };
    for (uint32_t opcode = 0; opcode < 0x80; ++opcode) {
        for (uint32_t dest = 0; dest < 16; ++dest) {
            const uint32_t fields = (dest << 21) | ((Next(rng) & 0x1Fu) << 16) | ((Next(rng) & 0x1Fu) << 11);
            if (opcode == 0x40) {
                for (uint32_t fn = 0; fn < 0x40; ++fn) {
                    if (fn >= 0x3C) {
                        for (uint32_t funct2 = 0; funct2 < 0x20; ++funct2) lower_case((opcode << 25) | fields | (funct2 << 6) | fn);
                    } else {
                        lower_case((opcode << 25) | fields | ((Next(rng) & 0x1Fu) << 6) | fn);
                    }
                }
            } else {
                lower_case((opcode << 25) | fields | (Next(rng) & 0x7FFu));
            }
        }
    }

    // ---- coverage: every fast upper kind under every dest; every dest-reading lower kind under every dest ----
    for (int k = 1; k < VUK_U_COUNT; ++k)
        for (int d = 0; d < 16; ++d) CHECK(upper_seen.count({k, d}) != 0, "upper kind %d dest %x never exercised", k, d);
    for (int k = 1; k < VUK_L_COUNT; ++k) {
        if (!VuCachedTier::LowerKindReadsDest(static_cast<uint8_t>(k))) continue;
        for (int d = 0; d < 16; ++d) CHECK(lower_seen.count({k, d}) != 0, "lower kind %d dest %x never exercised", k, d);
    }
    // The table shape: a dest-free lower kind resolves to its dynamic body for every dest.
    for (int d = 0; d < 16; ++d) {
        CHECK(VuCachedTier::LowerFastFnD(VUK_L_IADD, static_cast<uint8_t>(d)) == VuCachedTier::LowerFastFn(VUK_L_IADD), "IADD dest %d", d);
        CHECK(VuCachedTier::UpperFastFnD(VUK_U_ADD, static_cast<uint8_t>(d)) != nullptr, "ADD dest %d", d);
    }
    CHECK(VuCachedTier::UpperFastFnD(VUK_U_ADD, 0xF) != VuCachedTier::UpperFastFn(VUK_U_ADD), "the specialised ADD is its own body");

    std::printf("Vu1AotSpecializer: runs=%llu upper_pairs=%zu lower_pairs=%zu %s\n", static_cast<unsigned long long>(runs),
                upper_seen.size(), lower_seen.size(), g_fails ? "FAIL" : "ALL PASS");
    return g_fails ? 1 : 0;
}
