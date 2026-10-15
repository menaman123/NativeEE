// VuLazyFlagsAlgebra (part 1, the ring algebra): random push / advance /
// read / Q-commit / flush action sequences replayed on a fresh EAGER and a
// fresh LAZY FlagPipeline must produce identical read logs, identical ring
// views after every action, identical final MAC/STATUS/clock, and an empty
// ring after the final flush. Part 2: random straight-line programs (vector /
// broadcast / ACC / q-form FMAC ops, DIV + WAITQ, FSSET, FS*/FM* consumers
// storing their VI through ISW) run through the interpreter tier on a fresh
// EAGER and a fresh LAZY unit must leave identical registers, data memory
// and clock, with every consumer store landed. (The cached-tier leg joins
// with that tier.)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "engine/vu/flag_pipeline.h"
#include "engine/vu/tiers/vu_interpreter_tier.h"
#include "engine/vu/vu_float.h"
#include "engine/vu/vu_unit.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t Rnd64() {
    uint64_t x = g_rng;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_rng = x;
    return x * 0x2545F4914F6CDD1Dull;
}
static uint32_t Rnd(uint32_t n) { return static_cast<uint32_t>(Rnd64() % n); }

static const uint32_t kPool[16] = {
    0x00000000u, 0x80000000u, 0x00000001u, 0x807FFFFFu, 0x7F800000u, 0xFF800000u, 0x7FC00000u, 0xFFC00001u,
    0x7F7FFFFFu, 0xFF7FFFFFu, 0x3F800000u, 0xBF800000u, 0x42280000u, 0xC2280000u, 0x00800000u, 0x80800000u,
};
static const uint16_t kQPool[8] = {0x000, 0x010, 0x020, 0x030, 0x400, 0x800, 0xC30, 0x030};

enum ActionKind : uint8_t { kPush, kAdvance, kRead, kQCommit, kFlush };
struct Action {
    ActionKind kind;
    uint32_t bits[4];
    uint8_t mask;
    uint8_t flavor;
    uint32_t arg;
};

static std::vector<Action> GenSequence() {
    std::vector<Action> seq;
    const uint32_t n = 8 + Rnd(72);
    for (uint32_t i = 0; i < n; ++i) {
        Action a{};
        const uint32_t r = Rnd(100);
        if (r < 45) {
            a.kind = kPush;
            for (int k = 0; k < 4; ++k) a.bits[k] = kPool[Rnd(16)];
            a.mask = static_cast<uint8_t>(Rnd(16));
            a.flavor = static_cast<uint8_t>(Rnd(2));
        } else if (r < 70) {
            a.kind = kAdvance;
            a.arg = Rnd(6);
        } else if (r < 85) {
            a.kind = kRead;
        } else if (r < 95) {
            a.kind = kQCommit;
            a.arg = kQPool[Rnd(8)];
        } else {
            a.kind = kFlush;
        }
        seq.push_back(a);
    }
    Action fl{};
    fl.kind = kFlush;
    seq.push_back(fl);
    return seq;
}

// FNV over (depth, per-entry (retire - cycle) << 16 | packed MAC).
static uint64_t RingHash(const FlagPipeline& fp) {
    uint64_t h = 0xcbf29ce484222325ull;
    auto mix = [&h](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (8 * i)) & 0xFF;
            h *= 0x100000001b3ull;
        }
    };
    mix(static_cast<uint64_t>(fp.mac_depth()));
    for (int i = 0; i < fp.mac_depth(); ++i) {
        mix(((fp.mac_retire(i) - fp.cycle()) << 16) | fp.PackPendingMac(i));
    }
    return h;
}

struct Replay {
    std::vector<uint32_t> reads;
    std::vector<uint64_t> rings;
    uint16_t mac = 0, status = 0;
    uint64_t cycle = 0;
    int depth = 0;
};

static Replay Run(const std::vector<Action>& seq, bool lazy) {
    FlagPipeline fp(lazy);
    VUregs regs;
    std::memset(&regs, 0, sizeof(regs));
    regs.Reset();
    Replay out;
    for (const Action& a : seq) {
        switch (a.kind) {
            case kPush:
                if (lazy) {
                    fp.PushMacRaw(a.bits, a.mask);
                } else {
                    fp.PushMac(regs, VuFloat::PackMacBits(a.bits, a.mask));
                }
                break;
            case kAdvance: fp.cycle() += a.arg; break;
            case kRead:
                fp.RetireMacStatus(regs);
                out.reads.push_back((static_cast<uint32_t>(regs.mac_flag) << 16) | regs.status_flag);
                break;
            case kQCommit: VuFloat::CommitQStatus(regs, static_cast<uint16_t>(a.arg)); break;
            case kFlush:
                fp.FlushMacPipe(regs);
                out.reads.push_back((static_cast<uint32_t>(regs.mac_flag) << 16) | regs.status_flag);
                break;
        }
        out.rings.push_back(RingHash(fp));
    }
    out.mac = regs.mac_flag;
    out.status = regs.status_flag;
    out.cycle = fp.cycle();
    out.depth = fp.mac_depth();
    return out;
}

// The dest-mask form (UpdateMACFlags) must agree with the raw entry points.
static void TestUpdateEntryPoint() {
    for (int t = 0; t < 200; ++t) {
        FlagPipeline e(false), l(true);
        VUregs re, rl;
        std::memset(&re, 0, sizeof(re));
        std::memset(&rl, 0, sizeof(rl));
        re.Reset();
        rl.Reset();
        for (int k = 0; k < 12; ++k) {
            VR_reg v;
            for (int j = 0; j < 4; ++j) v.I[j] = kPool[Rnd(16)];
            const uint8_t mask = static_cast<uint8_t>(Rnd(16));
            e.UpdateMACFlags(re, v, mask);
            l.UpdateMACFlags(rl, v, mask);
            const uint32_t adv = Rnd(5);
            e.cycle() += adv;
            l.cycle() += adv;
            if (Rnd(3) == 0) {
                e.RetireMacStatus(re);
                l.RetireMacStatus(rl);
                CHECK(re.mac_flag == rl.mac_flag && re.status_flag == rl.status_flag);
            }
        }
        e.FlushMacPipe(re);
        l.FlushMacPipe(rl);
        CHECK(re.mac_flag == rl.mac_flag && re.status_flag == rl.status_flag);
    }
}


// ---------------------------------------------------------------- PART 2 --
namespace {

constexpr uint32_t U_NOP = 0x000002FFu;
constexpr uint32_t L_NOP = 0x8000033Cu;
constexpr uint32_t U_EBIT = 0x40000000u;

uint32_t EncUpper(uint32_t func6, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t dest) {
    return func6 | (fd << 6) | (fs << 11) | (ft << 16) | (dest << 21);
}
uint32_t EncLowerFlag(uint32_t op7, uint32_t it, uint32_t is, uint32_t imm12) {
    return (op7 << 25) | (it << 16) | (is << 11) | (imm12 & 0x7FFu) | (((imm12 >> 11) & 1u) << 21);
}
uint32_t EncIswX(uint32_t ft, uint32_t fs, uint32_t imm11) {
    return (0x05u << 25) | (0x8u << 21) | (ft << 16) | (fs << 11) | (imm11 & 0x7FFu);
}
uint32_t EncDiv(uint32_t fs, uint32_t fsf, uint32_t ft, uint32_t ftf) {
    return (0x40u << 25) | (ftf << 23) | (fsf << 21) | (ft << 16) | (fs << 11) | (0x0Eu << 6) | 0x3Cu;
}
uint32_t EncWaitQ() { return (0x40u << 25) | (0x0Eu << 6) | 0x3Fu; }

struct Bundle {
    uint32_t lower, upper;
};

struct Program {
    std::vector<Bundle> code;
    uint32_t slots = 0;  // consumer stores at data QW 64 + i
};

Program GenProgram() {
    static const uint32_t kVec[5] = {0x28, 0x29, 0x2A, 0x2C, 0x2D};
    static const uint32_t kBc[5] = {0x00, 0x04, 0x08, 0x0C, 0x18};
    static const uint32_t kAcc[4] = {0x01e021bfu, 0x01eb10bdu, 0x01eb18beu, 0x01eb0a48u};
    static const uint32_t kConsumer[6] = {0x14, 0x16, 0x17, 0x18, 0x1A, 0x1B};
    Program p;
    uint32_t consumers = 0;
    auto consumer = [&](uint32_t op, uint32_t it, uint32_t is, uint32_t imm) {
        p.code.push_back({EncLowerFlag(op, it, is, imm), U_NOP});
        p.code.push_back({EncIswX(it, 0, 64 + p.slots++), U_NOP});
        ++consumers;
    };
    const uint32_t body = 12 + Rnd(28);
    for (uint32_t i = 0; i < body; ++i) {
        const uint32_t r = Rnd(100);
        const uint32_t fd = 1 + Rnd(31), fs = Rnd(32), ft = Rnd(32), dest = 1 + Rnd(15);
        if (r < 34) {
            p.code.push_back({L_NOP, EncUpper(kVec[Rnd(5)], fd, fs, ft, dest)});
        } else if (r < 52) {
            p.code.push_back({L_NOP, EncUpper(kBc[Rnd(5)] + Rnd(4), fd, fs, ft, dest)});
        } else if (r < 60) {
            p.code.push_back({L_NOP, (kAcc[Rnd(4)] & ~0x01FFF800u) | (dest << 21) | (ft << 16) | (fs << 11)});
        } else if (r < 66) {
            p.code.push_back({L_NOP, EncUpper(Rnd(2) ? 0x20 : 0x1C, fd, fs, 0, dest)});
        } else if (r < 74) {
            p.code.push_back({EncDiv(Rnd(32), Rnd(4), Rnd(32), Rnd(4)), U_NOP});
            if (Rnd(2)) p.code.push_back({EncWaitQ(), U_NOP});
        } else if (r < 80) {
            p.code.push_back({EncLowerFlag(0x15, 0, 0, Rnd(0x1000)), U_NOP});
        } else {
            consumer(kConsumer[Rnd(6)], 1 + Rnd(14), 1 + Rnd(14), Rnd(0x1000));
        }
    }
    while (consumers < 3) consumer(0x16, 1 + Rnd(14), 0, 0xFFF);
    p.code.push_back({L_NOP, U_NOP | U_EBIT});
    p.code.push_back({L_NOP, U_NOP});
    return p;
}

struct ProgramResult {
    VUregs regs;
    std::vector<uint8_t> data;
    uint64_t cycle;
    int mac_depth;
    uint64_t bundles;
};

ProgramResult RunProgram(const Program& p, const VUregs& seed, bool lazy) {
    VuUnit vu(1, nullptr, true, lazy);
    std::memcpy(&vu.regs(), &seed, sizeof(VUregs));
    std::vector<uint8_t> fill(Vu1State::kMemSize, 0xAA);
    vu.WriteDataMem(0, fill.data(), fill.size());
    for (size_t i = 0; i < p.code.size(); ++i) {
        uint8_t b[8];
        std::memcpy(b, &p.code[i].lower, 4);
        std::memcpy(b + 4, &p.code[i].upper, 4);
        vu.WriteMicroMem(static_cast<uint32_t>(i) * 8u, b, 8);
    }
    VuInterpreterTier tier;
    tier.Run(vu, 0);
    ProgramResult r;
    std::memcpy(&r.regs, &vu.regs(), sizeof(VUregs));
    r.data.assign(vu.state().data, vu.state().data + Vu1State::kMemSize);
    r.cycle = vu.cycle();
    r.mac_depth = vu.flags().mac_depth();
    r.bundles = tier.bundles();
    return r;
}

void SeedRegs(VUregs& r) {
    std::memset(&r, 0, sizeof(r));
    r.Reset();
    for (int reg = 1; reg < 32; ++reg)
        for (int k = 0; k < 4; ++k) r.VF[reg].I[k] = kPool[Rnd(16)];
    for (int i = 1; i <= 14; ++i) r.VI[i] = static_cast<uint16_t>(Rnd64() & 0xFFFFu);
}

}  // namespace

static void RunPart2(int programs) {
    int total_bundles = 0;
    for (int n = 0; n < programs; ++n) {
        const Program p = GenProgram();
        VUregs seed;
        SeedRegs(seed);
        const ProgramResult eager = RunProgram(p, seed, false);
        const ProgramResult lazy = RunProgram(p, seed, true);
        total_bundles += static_cast<int>(eager.bundles);
        CHECK(eager.bundles == p.code.size());  // straight-line: every bundle issued once, ended at the E bit
        CHECK(std::memcmp(&eager.regs, &lazy.regs, sizeof(VUregs)) == 0);
        CHECK(eager.data == lazy.data);
        CHECK(eager.cycle == lazy.cycle);
        CHECK(eager.mac_depth == 0 && lazy.mac_depth == 0);
        for (uint32_t s = 0; s < p.slots; ++s) {  // ISW.x stored the zero-extended VI: high half zero
            uint32_t word;
            std::memcpy(&word, eager.data.data() + (64 + s) * 16, 4);
            CHECK((word >> 16) == 0);
        }
        if (g_fail) {
            std::printf("  program %d diverged (%zu bundles, %u consumer slots)\n", n, p.code.size(), p.slots);
            break;
        }
    }
    if (!g_fail) std::printf("  part 2: %d programs, %d bundles, eager == lazy through the interpreter\n", programs, total_bundles);
}

int main(int argc, char** argv) {
    const int sequences = (argc > 1) ? std::atoi(argv[1]) : 250;
    const int programs = (argc > 2) ? std::atoi(argv[2]) : 120;
    int total_actions = 0;
    for (int s = 0; s < sequences; ++s) {
        const std::vector<Action> seq = GenSequence();
        total_actions += static_cast<int>(seq.size());
        const Replay eager = Run(seq, false);
        const Replay lazy = Run(seq, true);
        CHECK(eager.reads == lazy.reads);
        CHECK(eager.rings == lazy.rings);
        CHECK(eager.mac == lazy.mac);
        CHECK(eager.status == lazy.status);
        CHECK(eager.cycle == lazy.cycle);
        CHECK(eager.depth == 0 && lazy.depth == 0);
        if (g_fail) {
            std::printf("  sequence %d diverged (%zu actions)\n", s, seq.size());
            break;
        }
    }
    TestUpdateEntryPoint();
    if (!g_fail) RunPart2(programs);
    if (g_fail) {
        std::printf("VuLazyFlagsAlgebra: FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("VuLazyFlagsAlgebra: PASS (%d sequences, %d actions, eager == lazy%s)\n", sequences, total_actions,
#if defined(PS2R_SIMD) && PS2R_SIMD && defined(__aarch64__)
                " [NEON fold]"
#else
                " [scalar fold]"
#endif
    );
    return 0;
}
