// vif_replay -- the VIF equivalence arms. Self-contained synthetic VIF1 streams
// fed through `Vif` on a fresh rig (GIF parser + arbiter, both VU units); no
// input files. The `--unpack-bulk` arm proves the bulk UNPACK lanes byte-identical
// to the scalar reference loop on 30 cases (lane census must-execute /
// must-decline, terminal parser state) and prints one PASS line per case. The
// `--mtvu N` arm proves the MTVU worker submit-equivalent to the inline feed
// (three synthetic VIF1 DMA chains per iteration, walked inline, through the
// worker ring, and through the ring with mid-stream eager drains + deferred
// frame boundaries); `--runahead` / `--runahead-off` prove the frame-cap
// producer park on the injected fake clock. The exec-async arms of the OLD
// tool are dropped (that thread was never transplanted).
//
// The bracketed tokens in the printed lines ("[W13R3-E3]", "[W10-EEFEED]",
// "[R168-DEFER]") are the OLD tool's output grammar, kept verbatim so the
// line-by-line oracle comparison holds; they name nothing in this tree.
//
// Output contract: the arm header goes to stderr; the per-case lines and the
// summary go to stdout, in the exact grammar the frozen OLD tool prints.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_boundary_hook.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/vif/vif.h"
#include "engine/vu/tiers/mtvu_backpressure.h"
#include "engine/vu/tiers/mtvu_clock.h"
#include "engine/vu/tiers/mtvu_sync.h"
#include "engine/vu/tiers/mtvu_worker.h"
#include "engine/vu/tiers/runahead_guard.h"
#include "engine/vu/vu_unit.h"

namespace {

// ---------------------------------------------------------------------------
// 32-bit-word stream builder. VIFcode = cmd<<24 | num<<16 | imm.
struct Stream {
    std::vector<uint32_t> w;
};

void Code(Stream& s, uint8_t cmd, uint8_t num, uint16_t imm) {
    s.w.push_back((static_cast<uint32_t>(cmd) << 24) | (static_cast<uint32_t>(num) << 16) | imm);
}
void Word(Stream& s, uint32_t x) { s.w.push_back(x); }
// The DMAC always hands whole quadwords: trailing zero words are NOPs.
void PadToQw(Stream& s) {
    while (s.w.size() % 4) s.w.push_back(0);
}

// Deterministic payload source (LCG) so both legs of a case see identical
// words and the sign-extension boundaries (0x80 / 0x8000 patterns) are hit.
uint32_t s_rng = 0;
uint32_t Rnd() {
    s_rng = s_rng * 1664525u + 1013904223u;
    return s_rng;
}

struct Case {
    const char* name;
    bool expect_bulk;   // must-execute (true) / must-decline (false)
    uint32_t pre_tops;  // VIF1 TOPS pre-state (0 = untouched)
    void (*build)(Stream&);
    // Which lane must serve a must-execute case: 0 = the CL==WL bulk lane,
    // 1 = the skip-cycle lane. Must-decline cases keep BOTH counters at zero.
    int lane;
};

// UNPACK opcodes: 0x60|format (bit 4 = masked). imm: bit15 = TOPS, bit14 = usn,
// bits 9:0 = addr_qw. STCYCL imm = CL | WL<<8.
void BV432UnsignedN7(Stream& s) {
    Code(s, 0x6C, 7, 0x4000 | 0);
    for (int i = 0; i < 7 * 4; i++) Word(s, Rnd());
}
void BV432SignedN256(Stream& s) {
    Code(s, 0x6C, 0, 100);  // num 0 = 256 vectors
    for (int i = 0; i < 256 * 4; i++) Word(s, Rnd());
}
void BV416SignedN13(Stream& s) {
    Code(s, 0x6D, 13, 40);
    for (int i = 0; i < 13 * 2; i++) Word(s, Rnd() | 0x80008000u);
}
void BV416UnsignedN13(Stream& s) {
    Code(s, 0x6D, 13, 0x4000 | 40);
    for (int i = 0; i < 13 * 2; i++) Word(s, Rnd() | 0x80008000u);
}
void BV48SignedN9(Stream& s) {
    Code(s, 0x6E, 9, 200);
    for (int i = 0; i < 9; i++) Word(s, Rnd() | 0x80808080u);
}
void BV48UnsignedN9(Stream& s) {
    Code(s, 0x6E, 9, 0x4000 | 200);
    for (int i = 0; i < 9; i++) Word(s, Rnd() | 0x80808080u);
}
void BV332Phase1N5(Stream& s) {
    // code at span word 0 -> payload starts at word 1 (odd) -> phase 1
    Code(s, 0x68, 5, 300);
    for (int i = 0; i < 5 * 3; i++) Word(s, Rnd());
}
void BV332Phase0N5(Stream& s) {
    // three NOPs put the code at word 3 -> payload at word 4 -> phase 0
    Code(s, 0x00, 0, 0);
    Code(s, 0x00, 0, 0);
    Code(s, 0x00, 0, 0);
    Code(s, 0x68, 5, 300);
    for (int i = 0; i < 5 * 3; i++) Word(s, Rnd());
}
void BV332N1(Stream& s) {
    Code(s, 0x68, 1, 310);
    for (int i = 0; i < 3; i++) Word(s, Rnd());
}
void BV432Wrap(Stream& s) {
    Code(s, 0x6C, 9, 1020);  // 1020..1023 then 0..4
    for (int i = 0; i < 9 * 4; i++) Word(s, Rnd());
}
void BV432Tops(Stream& s) {
    Code(s, 0x6C, 6, 0x8000 | 30);  // TOPS + 30 (pre_tops 512)
    for (int i = 0; i < 6 * 4; i++) Word(s, Rnd());
}
void BMaskedDecline(Stream& s) {
    Code(s, 0x20, 0, 0);  // STMASK
    Word(s, 0x00000055);
    Code(s, 0x7C, 4, 0);  // masked V4-32
    for (int i = 0; i < 4 * 4; i++) Word(s, Rnd());
}
void BModeDecline(Stream& s) {
    Code(s, 0x30, 0, 0);  // STROW
    for (int i = 0; i < 4; i++) Word(s, 0x10 + i);
    Code(s, 0x05, 0, 1);  // STMOD 1
    Code(s, 0x6C, 4, 16);
    for (int i = 0; i < 4 * 4; i++) Word(s, Rnd());
}
void BSkipDecline(Stream& s) {
    Code(s, 0x01, 0, (2 << 8) | 4);  // STCYCL CL 4 WL 2
    Code(s, 0x6C, 6, 64);
    for (int i = 0; i < 6 * 4; i++) Word(s, Rnd());
}
void BS32Decline(Stream& s) {
    Code(s, 0x60, 4, 128);  // S-32
    for (int i = 0; i < 4; i++) Word(s, Rnd());
}
void BV45Decline(Stream& s) {
    Code(s, 0x6F, 4, 140);  // V4-5
    Word(s, Rnd());
    Word(s, Rnd());
}

// Skip-cycle lane (CL > WL, WL == 1): payloads are dense (skip cycles consume
// no source); the V3-32 phase is set by the code word's span position.
void BxV332Cl4Ph0N7(Stream& s) {
    Code(s, 0x01, 0, 0x0104);  // STCYCL CL 4 WL 1 (word 0)
    Code(s, 0x68, 7, 300);     // code word 1 -> payload word 2 -> phase 0
    for (int i = 0; i < 7 * 3; i++) Word(s, Rnd());
}
void BxV332Cl4Ph1N7(Stream& s) {
    Code(s, 0x00, 0, 0);       // NOP (word 0)
    Code(s, 0x01, 0, 0x0104);  // STCYCL (word 1)
    Code(s, 0x68, 7, 300);     // code word 2 -> payload word 3 -> phase 1
    for (int i = 0; i < 7 * 3; i++) Word(s, Rnd());
}
void BxV432Cl4N6(Stream& s) {
    Code(s, 0x01, 0, 0x0104);
    Code(s, 0x6C, 6, 40);
    for (int i = 0; i < 6 * 4; i++) Word(s, Rnd());
}
void BxV432Cl4UsnN6(Stream& s) {
    Code(s, 0x01, 0, 0x0104);
    Code(s, 0x6C, 6, 0x4000 | 40);  // usn (32-bit ignores it)
    for (int i = 0; i < 6 * 4; i++) Word(s, Rnd());
}
void BxV48Cl4SN9(Stream& s) {
    Code(s, 0x01, 0, 0x0104);
    Code(s, 0x6E, 9, 200);
    for (int i = 0; i < 9; i++) Word(s, Rnd() | 0x80808080u);
}
void BxV48Cl4UN9(Stream& s) {
    Code(s, 0x01, 0, 0x0104);
    Code(s, 0x6E, 9, 0x4000 | 200);
    for (int i = 0; i < 9; i++) Word(s, Rnd() | 0x80808080u);
}
void BxV432Cl2N8(Stream& s) {
    Code(s, 0x01, 0, 0x0102);  // CL 2 WL 1
    Code(s, 0x6C, 8, 480);
    for (int i = 0; i < 8 * 4; i++) Word(s, Rnd());
}
void BxV432Cl4Wrap(Stream& s) {
    Code(s, 0x01, 0, 0x0104);
    Code(s, 0x6C, 6, 1020);  // dest 1020, 0, 4, 8, 12, 16
    for (int i = 0; i < 6 * 4; i++) Word(s, Rnd());
}
void BxV332Cl4Wrap(Stream& s) {
    Code(s, 0x01, 0, 0x0104);
    Code(s, 0x68, 5, 1016);  // dest 1016, 1020, 0, 4, 8
    for (int i = 0; i < 5 * 3; i++) Word(s, Rnd());
}
void BxV432Cl4Tops(Stream& s) {
    Code(s, 0x01, 0, 0x0104);
    Code(s, 0x6C, 6, 0x8000 | 30);  // TOPS + 30 (pre_tops 512)
    for (int i = 0; i < 6 * 4; i++) Word(s, Rnd());
}
void BxV48Cl4N256(Stream& s) {
    Code(s, 0x01, 0, 0x0104);
    Code(s, 0x6E, 0, 200);  // num 0 = 256 vectors, full wrap
    for (int i = 0; i < 256; i++) Word(s, Rnd());
}
void BxV416Cl4Decline(Stream& s) {
    Code(s, 0x01, 0, 0x0104);
    Code(s, 0x6D, 4, 40);  // V4-16 on the skip lane: not covered
    for (int i = 0; i < 4 * 2; i++) Word(s, Rnd() | 0x80008000u);
}
void BxMaskCl4Decline(Stream& s) {
    Code(s, 0x01, 0, 0x0104);
    Code(s, 0x20, 0, 0);  // STMASK
    Word(s, 0x00000055);
    Code(s, 0x7C, 4, 0);  // masked V4-32, CL 4 WL 1
    for (int i = 0; i < 4 * 4; i++) Word(s, Rnd());
}
void BxModeCl4Decline(Stream& s) {
    Code(s, 0x01, 0, 0x0104);
    Code(s, 0x30, 0, 0);  // STROW
    for (int i = 0; i < 4; i++) Word(s, 0x10 + i);
    Code(s, 0x05, 0, 1);   // STMOD 1
    Code(s, 0x68, 4, 16);  // V3-32, CL 4 WL 1
    for (int i = 0; i < 4 * 3; i++) Word(s, Rnd());
}

const Case kCases[] = {
    {"v432_unsigned_n7", true, 0, BV432UnsignedN7, 0},
    {"v432_signed_n256", true, 0, BV432SignedN256, 0},
    {"v416_signed_n13", true, 0, BV416SignedN13, 0},
    {"v416_unsigned_n13", true, 0, BV416UnsignedN13, 0},
    {"v48_signed_n9", true, 0, BV48SignedN9, 0},
    {"v48_unsigned_n9", true, 0, BV48UnsignedN9, 0},
    {"v332_phase1_n5", true, 0, BV332Phase1N5, 0},
    {"v332_phase0_n5", true, 0, BV332Phase0N5, 0},
    {"v332_n1", true, 0, BV332N1, 0},
    {"v432_wrap", true, 0, BV432Wrap, 0},
    {"v432_tops", true, 512, BV432Tops, 0},
    {"masked_decline", false, 0, BMaskedDecline, 0},
    {"mode_decline", false, 0, BModeDecline, 0},
    {"skip_decline", false, 0, BSkipDecline, 0},
    {"s32_decline", false, 0, BS32Decline, 0},
    {"v45_decline", false, 0, BV45Decline, 0},
    {"skx_v332_cl4_ph0_n7", true, 0, BxV332Cl4Ph0N7, 1},
    {"skx_v332_cl4_ph1_n7", true, 0, BxV332Cl4Ph1N7, 1},
    {"skx_v432_cl4_n6", true, 0, BxV432Cl4N6, 1},
    {"skx_v432_cl4_usn_n6", true, 0, BxV432Cl4UsnN6, 1},
    {"skx_v48_cl4_s_n9", true, 0, BxV48Cl4SN9, 1},
    {"skx_v48_cl4_u_n9", true, 0, BxV48Cl4UN9, 1},
    {"skx_v432_cl2_n8", true, 0, BxV432Cl2N8, 1},
    {"skx_v432_cl4_wrap", true, 0, BxV432Cl4Wrap, 1},
    {"skx_v332_cl4_wrap", true, 0, BxV332Cl4Wrap, 1},
    {"skx_v432_cl4_tops", true, 512, BxV432Cl4Tops, 1},
    {"skx_v48_cl4_n256", true, 0, BxV48Cl4N256, 1},
    {"skx_v416_cl4_decline", false, 0, BxV416Cl4Decline, 1},
    {"skx_mask_cl4_decline", false, 0, BxMaskCl4Decline, 1},
    {"skx_mode_cl4_decline", false, 0, BxModeCl4Decline, 1},
};
constexpr size_t kCaseCount = sizeof(kCases) / sizeof(kCases[0]);
constexpr size_t kVu1DataBytes = 16384;

// One leg's observables: the VU1 data memory, both lanes' command deltas and
// the parser's terminal state.
struct LegResult {
    std::vector<uint8_t> mem;
    uint64_t bulk_delta = 0;
    uint64_t skip_delta = 0;
    int end_state = 0;
    uint32_t end_pending = 0;
};

// Runs one case on a fresh rig with the bulk lanes forced off (`force` false)
// or on. Same seed (0xC0FFEE ^ case index) and the same position-sensitive
// canvas (0xA5 + (i & 0xFF)) on both legs.
void RunLeg(const Case& c, size_t index, bool force, bool vif_fast, LegResult& out) {
    RegisterOpStream ops;
    GifRegs gif_regs;
    GifParser parser(ops, gif_regs);
    GifArbiter arbiter(parser);
    VuUnit vu0(0);
    VuUnit vu1(1, &arbiter);
    Vif vif(vu0, vu1, arbiter, gif_regs, VifSettings{vif_fast, VifUnpackSettings{force}});
    vif.Reset();
    vif.regs(1).tops = c.pre_tops;
    std::vector<uint8_t> canvas(kVu1DataBytes);
    for (size_t i = 0; i < kVu1DataBytes; i++) canvas[i] = static_cast<uint8_t>(0xA5 + (i & 0xFF));
    vu1.WriteDataMem(0, canvas.data(), kVu1DataBytes);

    s_rng = 0xC0FFEEu ^ static_cast<uint32_t>(index);
    Stream s;
    c.build(s);
    PadToQw(s);

    const uint64_t before = vif.unpack().bulk_cmds();
    const uint64_t before_x = vif.unpack().skip_cmds();
    vif.ProcessData(1, reinterpret_cast<const uint8_t*>(s.w.data()), s.w.size() * 4);
    out.bulk_delta = vif.unpack().bulk_cmds() - before;
    out.skip_delta = vif.unpack().skip_cmds() - before_x;
    out.mem.assign(kVu1DataBytes, 0);
    vu1.ReadDataMem(0, out.mem.data(), kVu1DataBytes);
    out.end_state = static_cast<int>(vif.unit(1).state);
    out.end_pending = vif.unit(1).pending_bytes;
}

int RunUnpackBulk(bool vif_fast) {
    std::fprintf(stderr, "unpack-bulk equivalence selftest (vif_fast=%d)\n", vif_fast ? 1 : 0);
    int failures = 0;
    for (size_t index = 0; index < kCaseCount; index++) {
        const Case& c = kCases[index];
        LegResult off, on;
        RunLeg(c, index, false, vif_fast, off);
        RunLeg(c, index, true, vif_fast, on);

        const unsigned long long d_off = off.bulk_delta, d_on = on.bulk_delta;
        const unsigned long long x_off = off.skip_delta, x_on = on.skip_delta;
        const int idle = static_cast<int>(VifState::Idle);
        bool fail = false;
        if (off.end_state != idle || off.end_pending != 0 || on.end_state != idle || on.end_pending != 0) {
            std::printf("[UNPKB][%s] FAIL parser not terminal: off{st=%d pend=%u} on{st=%d pend=%u}\n", c.name,
                        off.end_state, off.end_pending, on.end_state, on.end_pending);
            fail = true;
        }
        if (d_off != 0 || x_off != 0) {
            std::printf("[UNPKB][%s] FAIL a lane executed on the OFF leg (bulk=%llu skip=%llu)\n", c.name, d_off,
                        x_off);
            fail = true;
        }
        const unsigned long long want_on = (c.lane == 0) ? d_on : x_on;
        const unsigned long long other_on = (c.lane == 0) ? x_on : d_on;
        if (c.expect_bulk && want_on == 0) {
            std::printf("[UNPKB][%s] FAIL must-execute: lane %d never ran on the ON leg\n", c.name, c.lane);
            fail = true;
        }
        if (c.expect_bulk && other_on != 0) {
            std::printf("[UNPKB][%s] FAIL lane isolation: other lane ran %llu time(s)\n", c.name, other_on);
            fail = true;
        }
        if (!c.expect_bulk && (d_on != 0 || x_on != 0)) {
            std::printf("[UNPKB][%s] FAIL must-decline: a lane ran (bulk=%llu skip=%llu)\n", c.name, d_on, x_on);
            fail = true;
        }
        if (off.mem != on.mem) {
            size_t i = 0;
            while (i < off.mem.size() && off.mem[i] == on.mem[i]) i++;
            std::printf("[UNPKB][%s] FAIL VU1 data mem diverges at byte 0x%zx (qw %zu): off=%02x on=%02x\n",
                        c.name, i, i / 16, off.mem[i], on.mem[i]);
            fail = true;
        }
        if (fail)
            failures++;
        else
            std::printf("[UNPKB][%s] PASS (bulk_cmds=%llu skip_cmds=%llu)\n", c.name, d_on, x_on);
    }
    if (failures == 0) {
        std::printf("UNPKB EQUIVALENCE PASS: %zu cases, byte-identical VU1 data memory, "
                    "must-execute/must-decline census clean\n",
                    kCaseCount);
        return 0;
    }
    std::printf("UNPKB EQUIVALENCE FAIL: %d case(s)\n", failures);
    return 1;
}


// ===========================================================================
// --mtvu N: inline-vs-worker equivalence on fabricated VIF1 DMA chains.
//
// Per iteration three chains are built into a private 32 MB guest RAM image
// and walked three times: pass 1 INLINE (spans handed to Vif::ProcessData on
// this thread), pass 2 MTVU (the same spans through the worker ring, even
// iterations staged as one Batch per walk, odd per span, with the EE's
// VIF1_STAT poll loop running alongside), pass 3 EAGER (the ring again, with a
// deferred frame boundary opened after every chain and the sealed prefix
// drained mid-stream). Asserts identical GIF packet census + order (path, size,
// FNV-1a; PATH1 XGKICK + PATH2 DIRECT), byte-identical VU1 data + code memory,
// identical VF/VI/TPC, and in pass 3 position-exact boundary delivery.
namespace mtvu_arm {

constexpr size_t kGuestRamBytes = 32u << 20;
constexpr uint32_t kGuestRamMask = 0x1FFFFFFu;
constexpr uint32_t kBusyView = MtvuSync::kVif1StatBusyView;
constexpr int kBoundaryPath = -7;  // sentinel `path` of a boundary in the drain-order log

struct RecPkt {
    int path;
    uint32_t size;
    uint64_t fnv;
};

uint64_t Fnv1a(const uint8_t* d, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) {
        h ^= d[i];
        h *= 1099511628211ull;
    }
    return h;
}

// xorshift32: deterministic per-iteration payload seeding.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
};

// VU micro encodings.
constexpr uint32_t U_NOP = 0x000002FF;
constexpr uint32_t U_NOPE = 0x400002FF;  // NOP with E-bit
constexpr uint32_t L_NOP = 0x8000033C;   // MOVE.- vf0,vf0
uint32_t L_IADDIU(int it, int is, int imm) {
    return (0x08u << 25) | (static_cast<uint32_t>(it) << 16) | (static_cast<uint32_t>(is) << 11) |
           static_cast<uint32_t>(imm & 0x7FF);
}
uint32_t L_XGKICK(int is) { return 0x80000000u | (static_cast<uint32_t>(is) << 11) | (0x1Bu << 6) | 0x3Cu; }
uint32_t L_XTOP(int it) { return 0x80000000u | (static_cast<uint32_t>(it) << 16) | (0x1Au << 6) | 0x3Cu; }

// 24-bundle microprogram: entry 0 kicks the packet at fixed qw2 (E; TPC ->
// bundle 4), bundle 4 (MSCNT) kicks qw8, entry 16 XTOPs and kicks TOP.
void BuildMicro(std::vector<uint32_t>& w) {
    struct B {
        uint32_t lo, up;
    };
    B b[24];
    for (int i = 0; i < 24; i++) b[i] = {L_NOP, U_NOP};
    b[0] = {L_IADDIU(1, 0, 2), U_NOP};
    b[1] = {L_XGKICK(1), U_NOP};
    b[2] = {L_NOP, U_NOPE};
    b[3] = {L_NOP, U_NOP};
    b[4] = {L_IADDIU(1, 0, 8), U_NOP};
    b[5] = {L_XGKICK(1), U_NOP};
    b[6] = {L_NOP, U_NOPE};
    b[7] = {L_NOP, U_NOP};
    b[16] = {L_XTOP(1), U_NOP};
    b[17] = {L_NOP, U_NOP};
    b[18] = {L_NOP, U_NOP};
    b[19] = {L_XGKICK(1), U_NOP};
    b[20] = {L_NOP, U_NOPE};
    b[21] = {L_NOP, U_NOP};
    for (int i = 0; i < 24; i++) {
        w.push_back(b[i].lo);
        w.push_back(b[i].up);
    }
}

// 2-quadword GIF packet (PACKED, NLOOP=1, NREGS=1 RGBAQ, EOP) with seeded payload.
void BuildGifPacket(std::vector<uint32_t>& w, Rng& rng) {
    w.push_back(0x00008001u);
    w.push_back(0x10000000u);
    w.push_back(0x00000001u);
    w.push_back(0);
    for (int i = 0; i < 4; i++) w.push_back(rng.next());
}

uint32_t Vc(uint8_t cmd, uint8_t num, uint16_t imm) {
    return (static_cast<uint32_t>(cmd) << 24) | (static_cast<uint32_t>(num) << 16) | imm;
}

// One DMAtag's words (padded to a quadword with VIF NOPs) + its TTE words.
struct Tag {
    std::vector<uint32_t> words;
    uint32_t tte0 = 0, tte1 = 0;
};

struct GuestRam {
    std::vector<uint8_t> mem;
    GuestRam() : mem(kGuestRamBytes, 0) {}
    void W32(uint32_t a, uint32_t v) { std::memcpy(&mem[a & kGuestRamMask], &v, 4); }
    uint32_t R32(uint32_t a) const {
        uint32_t v;
        std::memcpy(&v, &mem[a & kGuestRamMask], 4);
        return v;
    }
};

// CNT..CNT,END chain at `base`.
void EmitChain(GuestRam& ram, uint32_t base, std::vector<Tag>& tags) {
    uint32_t a = base;
    for (size_t t = 0; t < tags.size(); t++) {
        std::vector<uint32_t>& w = tags[t].words;
        while (w.size() % 4) w.push_back(0);
        const uint32_t qwc = static_cast<uint32_t>(w.size() / 4);
        const bool last = (t + 1 == tags.size());
        ram.W32(a + 0, qwc | ((last ? 7u : 1u) << 28));
        ram.W32(a + 4, 0);
        ram.W32(a + 8, tags[t].tte0);
        ram.W32(a + 12, tags[t].tte1);
        a += 16;
        for (uint32_t x : w) {
            ram.W32(a, x);
            a += 4;
        }
    }
}

// The iteration's three chains -> (tadr, CHCR.TTE) pairs. A payload that spans
// into the next tag ends its first tag exactly on a quadword edge (a pad word
// is a VIF NOP only at an idle parser); the spanning chain runs with TTE off.
std::vector<std::pair<uint32_t, bool>> BuildChains(GuestRam& ram, uint32_t iter) {
    Rng rng(0xC0FFEEu + iter * 2654435761u);
    auto span_take = [](size_t words_so_far) -> size_t {
        size_t t = (4 - (words_so_far & 3)) & 3;
        return t == 0 ? 4 : t;
    };

    // chain A @0x100000: MPG + absolute UNPACKs + MSCAL + MSCNT, second UNPACK spanning tags.
    std::vector<Tag> A(2);
    {
        std::vector<uint32_t>& w = A[0].words;
        w.push_back(Vc(0x01, 0, 0x0101));
        w.push_back(Vc(0x20, 0, 0));
        w.push_back(0);
        std::vector<uint32_t> micro;
        BuildMicro(micro);
        w.push_back(Vc(0x4A, static_cast<uint8_t>(micro.size() / 2), 0));
        for (uint32_t x : micro) w.push_back(x);
        w.push_back(Vc(0x6C, 2, 2));
        std::vector<uint32_t> p1;
        BuildGifPacket(p1, rng);
        for (uint32_t x : p1) w.push_back(x);
        w.push_back(Vc(0x6C, 2, 8));
        std::vector<uint32_t> p2;
        BuildGifPacket(p2, rng);
        const size_t take = span_take(w.size());
        for (size_t i = 0; i < take; i++) w.push_back(p2[i]);
        std::vector<uint32_t>& w2 = A[1].words;
        for (size_t i = take; i < p2.size(); i++) w2.push_back(p2[i]);
        w2.push_back(Vc(0x14, 0, 0));
        w2.push_back(Vc(0x17, 0, 0));
    }
    EmitChain(ram, 0x100000, A);

    // chain B @0x108000: BASE/OFFSET double buffer + TOPS UNPACKs + MSCAL 16 x2 + UNPACK variants + DIRECT + FLUSH.
    std::vector<Tag> B(3);
    {
        std::vector<uint32_t>& w = B[0].words;
        w.push_back(Vc(0x03, 0, 20));
        w.push_back(Vc(0x02, 0, 8));
        w.push_back(Vc(0x30, 0, 0));
        for (int i = 0; i < 4; i++) w.push_back(rng.next());
        w.push_back(Vc(0x01, 0, 0x0102));
        w.push_back(Vc(0x68, 3, 12));
        for (int i = 0; i < 9; i++) w.push_back(rng.next());
        w.push_back(Vc(0x01, 0, 0x0101));
        w.push_back(Vc(0x65, 2, 0x4000 | 15));
        w.push_back(rng.next());
        w.push_back(rng.next());
        w.push_back(Vc(0x20, 0, 0));
        w.push_back(0x00002200u);
        w.push_back(Vc(0x7E, 4, 18));
        for (int i = 0; i < 4; i++) w.push_back(rng.next());
        w.push_back(Vc(0x20, 0, 0));
        w.push_back(0);
        w.push_back(Vc(0x6C, 2, 0x8000 | 0));
        std::vector<uint32_t> p3;
        BuildGifPacket(p3, rng);
        for (uint32_t x : p3) w.push_back(x);
        w.push_back(Vc(0x14, 0, 16));
        std::vector<uint32_t>& w2 = B[1].words;
        w2.push_back(Vc(0x6C, 2, 0x8000 | 0));
        std::vector<uint32_t> p4;
        BuildGifPacket(p4, rng);
        for (uint32_t x : p4) w2.push_back(x);
        w2.push_back(Vc(0x14, 0, 16));
        std::vector<uint32_t>& w3 = B[2].words;
        w3.push_back(Vc(0x50, 0, 2));
        std::vector<uint32_t> p5;
        BuildGifPacket(p5, rng);
        for (uint32_t x : p5) w3.push_back(x);
        w3.push_back(Vc(0x11, 0, 0));
    }
    EmitChain(ram, 0x108000, B);

    // chain C @0x110000: REF data pull + MSCAL 0; the END tag's TTE words carry STCYCL at an idle boundary.
    {
        Rng r2(rng.next());
        std::vector<uint32_t> refdata;
        refdata.push_back(Vc(0x6C, 2, 2));
        std::vector<uint32_t> p6;
        BuildGifPacket(p6, r2);
        for (uint32_t x : p6) refdata.push_back(x);
        refdata.push_back(Vc(0x14, 0, 0));
        while (refdata.size() % 4) refdata.push_back(0);
        uint32_t da = 0x118000;
        for (uint32_t x : refdata) {
            ram.W32(da, x);
            da += 4;
        }
        uint32_t a = 0x110000;
        const uint32_t qwc = static_cast<uint32_t>(refdata.size() / 4);
        ram.W32(a + 0, qwc | (3u << 28));
        ram.W32(a + 4, 0x118000);
        ram.W32(a + 8, 0);
        ram.W32(a + 12, 0);
        a += 16;
        ram.W32(a + 0, 7u << 28);
        ram.W32(a + 4, 0);
        ram.W32(a + 8, Vc(0x01, 0, 0x0101));
        ram.W32(a + 12, 0);
    }
    return {{0x100000, false}, {0x108000, true}, {0x110000, true}};
}

// The DMAC walk (CNT/REF/END subset), feeding spans through `feed(data, size, tte)`;
// with CHCR.TTE the tag's upper words go first as a zero-padded quadword.
template <typename FeedFn>
void WalkChain(const GuestRam& ram, uint32_t tadr, bool tte_on, FeedFn&& feed) {
    bool end = false;
    int guard = 0;
    while (!end && ++guard < 4096) {
        const uint32_t w0 = ram.R32(tadr), w1 = ram.R32(tadr + 4);
        const uint32_t w2 = ram.R32(tadr + 8), w3 = ram.R32(tadr + 12);
        const uint32_t qwc = w0 & 0xFFFF;
        const uint32_t id = (w0 >> 28) & 7;
        const uint32_t addr = w1 & 0x7FFFFFF0;
        uint32_t src = 0, next = 0;
        switch (id) {
            case 1: src = tadr + 16; next = tadr + 16 + qwc * 16; break;
            case 3: src = addr; next = tadr + 16; break;
            case 7: src = tadr + 16; next = 0; end = true; break;
            default: return;
        }
        if (tte_on) {
            uint8_t tte_qw[16];
            std::memset(tte_qw, 0, sizeof(tte_qw));
            std::memcpy(tte_qw + 0, &w2, 4);
            std::memcpy(tte_qw + 4, &w3, 4);
            feed(tte_qw, 16, true);
        }
        if (qwc > 0) feed(&ram.mem[src & kGuestRamMask], qwc * 16, false);
        if (!end) tadr = next;
    }
}

struct EndState {
    std::vector<uint8_t> vu1d, vu1c;
    uint8_t vf[32 * 16];
    uint16_t vi[16];
    uint32_t tpc;
    int vif_state;
    uint32_t vif_pending;
    std::vector<RecPkt> pkts;
};

// The rig: one GIF parser + arbiter, both VU units, the VIF, the worker (up
// for ALL passes -- one VU1 register file, VIF1 bound to it), its sync.
struct Rig : public GifBoundaryHook {
    RegisterOpStream ops;
    GifRegs gif_regs;
    GifParser parser;
    GifArbiter arbiter;
    VuUnit vu0;
    VuUnit vu1;
    VuUnit vu1w;
    Vif vif;
    RunaheadGuard guard;
    MtvuBackpressure backpressure;
    MtvuWorker worker;
    MtvuSync sync;
    GuestRam ram;

    std::mutex rec_mtx;                 // the submit tap fires on the worker and the main thread
    std::vector<RecPkt>* rec = nullptr; // active submit recorder (null = off)
    std::vector<RecPkt> drained;        // drain-order log, main thread only (pass 3)
    std::atomic<uint64_t> bnd_hook_fires{0};
    std::atomic<uint64_t> bnd_offthread{0};
    std::thread::id main_tid;

    explicit Rig(bool vif_fast)
        : parser(ops, gif_regs),
          arbiter(parser, this),
          vu0(0),
          vu1(1, &arbiter),
          vu1w(1, &arbiter),
          vif(vu0, vu1, arbiter, gif_regs, VifSettings{vif_fast, VifUnpackSettings{}}),
          guard(2),
          backpressure(guard, MtvuClock::Steady()),
          worker(vif, vu1w, arbiter, backpressure, MtvuSettings{true, 2}),
          sync(worker, arbiter) {
        main_tid = std::this_thread::get_id();
        arbiter.SetSubmitObserver(&Rig::RecordSubmit, this);
        arbiter.SetDrainObserver(&Rig::RecordDrained, this);
        sync.InstallInto(vu1.dispatch());
    }

    static void RecordSubmit(void* user, GifPath path, const uint8_t* data, size_t size) {
        auto* r = static_cast<Rig*>(user);
        std::lock_guard<std::mutex> lk(r->rec_mtx);
        if (!r->rec) return;
        r->rec->push_back(RecPkt{static_cast<int>(path), static_cast<uint32_t>(size), Fnv1a(data, size)});
    }
    // Drain-order log: data packets by the drain tap, boundaries by the hook (both on the drain owner).
    static void RecordDrained(void* user, const GifArbiterPacket& p) {
        auto* r = static_cast<Rig*>(user);
        if (p.boundary_seq != 0) return;
        r->drained.push_back(RecPkt{static_cast<int>(p.path), static_cast<uint32_t>(p.data.size()),
                                    Fnv1a(p.data.data(), p.data.size())});
    }
    void OnBoundary(uint32_t seq, uint64_t) override {
        bnd_hook_fires.fetch_add(1, std::memory_order_relaxed);
        if (std::this_thread::get_id() != main_tid) bnd_offthread.fetch_add(1, std::memory_order_relaxed);
        drained.push_back(RecPkt{kBoundaryPath, seq, 0});
    }
    void SetRecorder(std::vector<RecPkt>* r) {
        std::lock_guard<std::mutex> lk(rec_mtx);
        rec = r;
    }

    void FeedInline(const uint8_t* d, size_t n, bool tte) {
        if (tte) vif.SetTteFeed(1, true);
        vif.ProcessData(1, d, n);
        if (tte) vif.SetTteFeed(1, false);
    }

    // Power-on: VIF reset, both VU1 memories zero, the worker's register file
    // reset, every queued / sealed arbiter slot dropped.
    void ResetPipeline() {
        vif.Reset();
        std::vector<uint8_t> zero(Vu1State::kMemSize, 0);
        vu1w.WriteDataMem(0, zero.data(), zero.size());
        vu1w.WriteMicroMem(0, zero.data(), zero.size());
        vu1w.Reset();
        vu1w.regs().Reset();
        vu1.regs().Reset();
        arbiter.clear();
    }

    void CaptureEnd(EndState& e, std::vector<RecPkt>&& pkts) {
        const Vu1State& st = vu1w.state();
        e.vu1d.assign(st.data, st.data + Vu1State::kMemSize);
        e.vu1c.assign(st.micro, st.micro + Vu1State::kMemSize);
        for (int r = 0; r < 32; r++) std::memcpy(&e.vf[r * 16], &st.regs.VF[r], 16);
        for (int r = 0; r < 16; r++) e.vi[r] = st.regs.VI[r];
        e.tpc = st.regs.TPC;
        e.vif_state = static_cast<int>(vif.unit(1).state);
        e.vif_pending = vif.unit(1).pending_bytes;
        e.pkts = std::move(pkts);
    }
};

// 0 on equivalence; prints the first divergence otherwise.
int Compare(const EndState& a, const EndState& b, uint32_t iter) {
    if (a.vif_state != 0 || a.vif_pending != 0 || b.vif_state != 0 || b.vif_pending != 0) {
        std::printf("[MTVU][iter %u] FAIL parser not terminal: inline{st=%d pend=%u} mtvu{st=%d pend=%u}\n", iter,
                    a.vif_state, a.vif_pending, b.vif_state, b.vif_pending);
        return 1;
    }
    if (a.pkts.size() != b.pkts.size()) {
        std::printf("[MTVU][iter %u] FAIL packet COUNT inline=%zu mtvu=%zu\n", iter, a.pkts.size(), b.pkts.size());
        return 1;
    }
    for (size_t i = 0; i < a.pkts.size(); i++) {
        if (a.pkts[i].path != b.pkts[i].path || a.pkts[i].size != b.pkts[i].size || a.pkts[i].fnv != b.pkts[i].fnv) {
            std::printf("[MTVU][iter %u] FAIL packet #%zu inline={P%d %u %016llx} mtvu={P%d %u %016llx}\n", iter, i,
                        a.pkts[i].path, a.pkts[i].size, static_cast<unsigned long long>(a.pkts[i].fnv),
                        b.pkts[i].path, b.pkts[i].size, static_cast<unsigned long long>(b.pkts[i].fnv));
            return 1;
        }
    }
    if (a.vu1d != b.vu1d) {
        for (size_t i = 0; i < a.vu1d.size(); i++)
            if (a.vu1d[i] != b.vu1d[i]) {
                std::printf("[MTVU][iter %u] FAIL VU1 data mem @0x%zx inline=%02x mtvu=%02x\n", iter, i, a.vu1d[i],
                            b.vu1d[i]);
                return 1;
            }
    }
    if (a.vu1c != b.vu1c) {
        std::printf("[MTVU][iter %u] FAIL VU1 code mem differs\n", iter);
        return 1;
    }
    if (std::memcmp(a.vf, b.vf, sizeof(a.vf)) != 0 || std::memcmp(a.vi, b.vi, sizeof(a.vi)) != 0 || a.tpc != b.tpc) {
        std::printf("[MTVU][iter %u] FAIL VU1 regs differ (tpc %u vs %u)\n", iter, a.tpc, b.tpc);
        return 1;
    }
    return 0;
}

// 256-bundle micro (MPG num=0): entry 0 kicks qw2 at b248 (E at b249; TPC ->
// b251), the MSCNT body at b251 kicks qw8.
void BuildMicroLong(std::vector<uint32_t>& w) {
    struct B {
        uint32_t lo, up;
    };
    std::vector<B> b(256, B{L_NOP, U_NOP});
    b[0] = {L_IADDIU(1, 0, 2), U_NOP};
    b[248] = {L_XGKICK(1), U_NOP};
    b[249] = {L_NOP, U_NOPE};
    b[250] = {L_NOP, U_NOP};
    b[251] = {L_IADDIU(1, 0, 8), U_NOP};
    b[252] = {L_XGKICK(1), U_NOP};
    b[253] = {L_NOP, U_NOPE};
    b[254] = {L_NOP, U_NOP};
    for (const auto& x : b) {
        w.push_back(x.lo);
        w.push_back(x.up);
    }
}

// The structural case: one fixed 4-tag chain covering masked / mode / wrap
// UNPACK, the 256-bundle MPG, an UNPACK into the other buffer around a kick,
// MSCNT, DIRECT after a kick, FLUSH, an MPG that overwrites bundles 224-227
// (so the later MSCAL 0 ends at b225's E-bit: exactly 3 packets), a final
// UNPACK after the last kick. Inline vs the worker ring.
int RunStructural(Rig& rig, bool vif_fast) {
    Rng rng(0xE3E3E3u);
    std::vector<Tag> S(4);
    {
        std::vector<uint32_t>& w = S[0].words;
        w.push_back(Vc(0x01, 0, 0x0101));
        w.push_back(Vc(0x20, 0, 0));
        w.push_back(0);
        std::vector<uint32_t> micro;
        BuildMicroLong(micro);
        w.push_back(Vc(0x4A, 0, 0));
        for (uint32_t x : micro) w.push_back(x);
        w.push_back(Vc(0x6C, 2, 2));
        std::vector<uint32_t> p1;
        BuildGifPacket(p1, rng);
        for (uint32_t x : p1) w.push_back(x);
        w.push_back(Vc(0x6C, 2, 8));
        std::vector<uint32_t> p2;
        BuildGifPacket(p2, rng);
        for (uint32_t x : p2) w.push_back(x);
    }
    {
        std::vector<uint32_t>& w = S[1].words;
        w.push_back(Vc(0x05, 0, 1));
        w.push_back(Vc(0x30, 0, 0));
        w.push_back(0x3F800000u);
        w.push_back(0x40000000u);
        w.push_back(0x40400000u);
        w.push_back(0x40800000u);
        w.push_back(Vc(0x20, 0, 0));
        w.push_back(0x00002200u);
        w.push_back(Vc(0x7C, 4, 0x3FE));
        for (int i = 0; i < 16; i++) w.push_back(rng.next());
        w.push_back(Vc(0x20, 0, 0));
        w.push_back(0);
        w.push_back(Vc(0x05, 0, 0));
        w.push_back(Vc(0x03, 0, 40));
        w.push_back(Vc(0x02, 0, 8));
    }
    {
        std::vector<uint32_t>& w = S[2].words;
        w.push_back(Vc(0x14, 0, 0));
        w.push_back(Vc(0x6C, 2, 0x8000 | 0));
        std::vector<uint32_t> p3;
        BuildGifPacket(p3, rng);
        for (uint32_t x : p3) w.push_back(x);
        w.push_back(Vc(0x17, 0, 0));
        w.push_back(Vc(0x50, 0, 2));
        std::vector<uint32_t> p4;
        BuildGifPacket(p4, rng);
        for (uint32_t x : p4) w.push_back(x);
        w.push_back(Vc(0x11, 0, 0));
    }
    {
        std::vector<uint32_t>& w = S[3].words;
        w.push_back(Vc(0x4A, 4, 224));
        w.push_back(L_IADDIU(2, 0, 7));
        w.push_back(U_NOP);
        w.push_back(L_NOP);
        w.push_back(U_NOPE);
        w.push_back(L_NOP);
        w.push_back(U_NOP);
        w.push_back(L_NOP);
        w.push_back(U_NOP);
        w.push_back(Vc(0x14, 0, 224));
        w.push_back(Vc(0x14, 0, 0));
        w.push_back(Vc(0x6C, 2, 30));
        std::vector<uint32_t> p5;
        BuildGifPacket(p5, rng);
        for (uint32_t x : p5) w.push_back(x);
    }
    EmitChain(rig.ram, 0x120000, S);

    rig.ResetPipeline();
    std::vector<RecPkt> rec_inline;
    rig.SetRecorder(&rec_inline);
    rig.vif.ResetLatch(1);
    WalkChain(rig.ram, 0x120000, false, [&](const uint8_t* d, size_t n, bool tte) { rig.FeedInline(d, n, tte); });
    EndState inline_end;
    rig.CaptureEnd(inline_end, std::move(rec_inline));

    rig.ResetPipeline();
    std::vector<RecPkt> rec_mtvu;
    rig.SetRecorder(&rec_mtvu);
    rig.worker.SubmitResetLatch();
    WalkChain(rig.ram, 0x120000, false,
              [&](const uint8_t* d, size_t n, bool tte) { rig.worker.SubmitVif1(d, n, tte); });
    rig.sync.Sync();
    EndState mtvu_end;
    rig.CaptureEnd(mtvu_end, std::move(rec_mtvu));
    rig.SetRecorder(nullptr);

    const int rc = Compare(inline_end, mtvu_end, 90001u);
    std::fprintf(stderr, "[W13R3-E3] structural wait-point case: %s (%zu packets, vif_fast=%d)\n",
                 rc == 0 ? "PASS" : "FAIL", inline_end.pkts.size(), vif_fast ? 1 : 0);
    return rc;
}

// --mtvu-fault-no-eager: the seeded fault of the liveness check -- pass 3 never
// tries an eager drain, so the run must end in the starvation FAIL line.
bool g_fault_no_eager = false;

int Run(int iterations, bool vif_fast) {
    Rig rig(vif_fast);
    rig.worker.Start();

    int failures = 0;
    failures += RunStructural(rig, vif_fast);
    size_t total_pkts = 0;
    uint64_t s_total_stat_busy = 0;
    uint64_t total_eager_early = 0;
    int iters_with_eager = 0;
    uint64_t total_boundaries = 0;
    uint64_t total_batches = 0;
    int staged_iters = 0;
    for (int it = 0; it < iterations && failures == 0; it++) {
        auto chains = BuildChains(rig.ram, static_cast<uint32_t>(it));

        // ---- pass 1: INLINE ----
        rig.ResetPipeline();
        std::vector<RecPkt> rec_inline;
        std::vector<size_t> chain_ends;  // packets recorded after chain k
        rig.SetRecorder(&rec_inline);
        for (auto& c : chains) {
            rig.vif.ResetLatch(1);
            WalkChain(rig.ram, c.first, c.second,
                      [&](const uint8_t* d, size_t n, bool tte) { rig.FeedInline(d, n, tte); });
            std::lock_guard<std::mutex> lk(rig.rec_mtx);
            chain_ends.push_back(rec_inline.size());
        }
        EndState inline_end;
        rig.CaptureEnd(inline_end, std::move(rec_inline));

        // ---- pass 2: MTVU ring + the EE's VIF1_STAT poll loop ----
        rig.ResetPipeline();
        std::vector<RecPkt> rec_mtvu;
        rig.SetRecorder(&rec_mtvu);
        uint64_t stat_busy_seen = 0;
        const bool staged_iter = (it % 2 == 0);
        const uint64_t batches_before = rig.worker.BatchSegmentsProcessed();
        auto poll_stat_once = [&]() -> bool {
            uint32_t stat = 0;
            if (rig.sync.TryVif1StatBusyView(&stat)) {
                if (stat != kBusyView) {
                    std::printf("[MTVU][iter %d] FAIL busy STAT view 0x%08x != 0x%08x\n", it, stat, kBusyView);
                    std::exit(1);
                }
                stat_busy_seen++;
                return true;
            }
            return false;
        };
        auto submit_chain = [&](const std::pair<uint32_t, bool>& c) {
            rig.worker.SubmitResetLatch();
            if (staged_iter) {
                WalkChain(rig.ram, c.first, c.second,
                          [&](const uint8_t* d, size_t n, bool tte) { rig.worker.SubmitVif1Staged(d, n, tte); });
                rig.worker.FlushVif1Staged();
            } else {
                WalkChain(rig.ram, c.first, c.second,
                          [&](const uint8_t* d, size_t n, bool tte) { rig.worker.SubmitVif1(d, n, tte); });
            }
        };
        for (auto& c : chains) {
            submit_chain(c);
            poll_stat_once();
        }
        {
            const auto t0 = std::chrono::steady_clock::now();
            while (poll_stat_once()) {
                if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(10)) {
                    std::printf("[MTVU][iter %d] FAIL STAT poll did not converge in 10s (busy views=%llu)\n", it,
                                static_cast<unsigned long long>(stat_busy_seen));
                    std::exit(1);
                }
                std::this_thread::yield();
            }
            if (poll_stat_once()) {
                std::printf("[MTVU][iter %d] FAIL STAT flickered busy after idle\n", it);
                std::exit(1);
            }
        }
        rig.sync.Sync();
        if (staged_iter) {
            const uint64_t nb = rig.worker.BatchSegmentsProcessed() - batches_before;
            if (nb == 0) {
                std::printf("[MTVU][iter %d] FAIL [W10-EEFEED] staged feed produced 0 Batch segments (lane never ran)\n",
                            it);
                failures++;
                break;
            }
            total_batches += nb;
            staged_iters++;
        }
        EndState mtvu_end;
        rig.CaptureEnd(mtvu_end, std::move(rec_mtvu));
        rig.SetRecorder(nullptr);
        s_total_stat_busy += stat_busy_seen;
        total_pkts += inline_end.pkts.size();
        if (inline_end.pkts.size() != 6) {
            std::printf("[MTVU][iter %d] FAIL: inline produced %zu pkts, expected 6 (fabricated stream degenerated)\n",
                        it, inline_end.pkts.size());
            failures++;
            break;
        }
        failures += Compare(inline_end, mtvu_end, static_cast<uint32_t>(it));
        if (failures) break;

        // ---- pass 3: MTVU ring + mid-stream eager drains + deferred boundaries ----
        {
            rig.ResetPipeline();
            rig.drained.clear();
            uint32_t bnd_seq = 0;
            // Handshake with the worker: park on its progress signal -- the seal of
            // the chain's marker, the same cv_idle_ wake Sync's eager loop parks on
            // (EnterSyncWait arms the worker's locked notify) -- until the seal count
            // advances or the worker has drained its queue. The loop ends on the
            // worker's progress, never on a clock, so the eager drain that follows
            // always finds a sealed prefix whatever the host's scheduling (a drain
            // tried before the seal finds nothing: under host load the worker's whole
            // burst runs while this thread is descheduled and the STAT poll then sees
            // idle at once, so the early-drain count depends on the scheduler alone).
            auto await_seal = [&](uint64_t seen) {
                rig.worker.EnterSyncWait();
                while (rig.worker.SealCount() == seen && !rig.worker.WorkerIdle())
                    rig.worker.WaitIdleOrSeal(MtvuSync::kPollNs, seen);
                rig.worker.LeaveSyncWait();
            };
            for (auto& c : chains) {
                const uint64_t seen = rig.worker.SealCount();
                submit_chain(c);
                // The deferred boundary HLE sequence: feed complete -> note -> open, one per chain.
                rig.arbiter.noteFeedForBoundary();
                rig.arbiter.openBoundary(++bnd_seq, 0);
                poll_stat_once();
                await_seal(seen);
                if (!g_fault_no_eager) rig.sync.TryEagerDrain();
            }
            {
                const auto t0 = std::chrono::steady_clock::now();
                while (poll_stat_once()) {
                    if (!g_fault_no_eager) rig.sync.TryEagerDrain();
                    if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(10)) {
                        std::printf("[MTVU][iter %d] FAIL eager-pass STAT poll did not converge in 10s\n", it);
                        std::exit(1);
                    }
                    std::this_thread::yield();
                }
            }
            rig.sync.Sync();
            size_t eager_early = 0;
            for (auto& p : rig.drained)
                if (p.path != kBoundaryPath) eager_early++;
            rig.arbiter.drain();
            EndState eager_end;
            rig.CaptureEnd(eager_end, std::move(rig.drained));
            rig.drained.clear();
            total_eager_early += eager_early;
            if (eager_early > 0) iters_with_eager++;
            // Expected = the inline stream with boundary k spliced right after chain k's packets.
            EndState expected = inline_end;
            expected.pkts.clear();
            {
                size_t pi = 0;
                uint32_t bseq = 0;
                for (size_t k = 0; k < chain_ends.size(); k++) {
                    for (; pi < chain_ends[k]; pi++) expected.pkts.push_back(inline_end.pkts[pi]);
                    expected.pkts.push_back(RecPkt{kBoundaryPath, ++bseq, 0});
                }
                for (; pi < inline_end.pkts.size(); pi++) expected.pkts.push_back(inline_end.pkts[pi]);
            }
            size_t bnd_delivered = 0;
            for (auto& p : eager_end.pkts)
                if (p.path == kBoundaryPath) bnd_delivered++;
            if (bnd_delivered != chain_ends.size() || rig.bnd_offthread.load(std::memory_order_relaxed) != 0) {
                std::printf("[MTVU][iter %d] FAIL boundary delivery: %zu of %zu delivered, offthread=%llu\n", it,
                            bnd_delivered, chain_ends.size(),
                            static_cast<unsigned long long>(rig.bnd_offthread.load()));
                failures++;
                break;
            }
            total_boundaries += bnd_delivered;
            failures += Compare(expected, eager_end, static_cast<uint32_t>(it));
            if (failures) {
                std::printf("[MTVU][iter %d] (failure was in the EAGER-DRAIN pass; %zu of %zu pkts drained before the "
                            "post-wait drain)\n",
                            it, eager_early, eager_end.pkts.size());
                break;
            }
        }
    }

    rig.worker.Stop();
    // Eager-drain liveness: every chain's sealed prefix is drained before the
    // post-wait drain (the handshake above makes the prefix sealed when the drain
    // is tried), so zero early packets means the seal -> ready -> eager-drain path
    // is broken, not that the host scheduled the worker first.
    if (failures == 0 && iterations >= 20 && total_eager_early == 0) {
        std::printf("MTVU EQUIVALENCE FAIL: eager-drain starvation - 0 packets drained mid-wait across %d iterations "
                    "(seal wake broken?)\n",
                    iterations);
        return 1;
    }
    if (failures == 0) {
        std::printf("[W10-EEFEED] staged-feed iterations: %d of %d (worker Batch segments: %llu)\n", staged_iters,
                    iterations, static_cast<unsigned long long>(total_batches));
        std::printf("MTVU EQUIVALENCE PASS: %d iterations, %zu packets total, "
                    "byte-identical VU1 data+code mem, identical packet census+order, "
                    "identical VF/VI/TPC; STAT poll: %llu busy views, converged every "
                    "iteration, monotone idle; EAGER-DRAIN pass: order+mem identical, "
                    "%llu pkts eager-drained before the post-wait drain (%d/%d iters); "
                    "BOUNDARIES [R168-DEFER]: %llu enqueued+delivered position-exact "
                    "(one per chain, drain-thread only, hook fired %llu)\n",
                    iterations, total_pkts, static_cast<unsigned long long>(s_total_stat_busy),
                    static_cast<unsigned long long>(total_eager_early), iters_with_eager, iterations,
                    static_cast<unsigned long long>(total_boundaries),
                    static_cast<unsigned long long>(rig.bnd_hook_fires.load()));
        return 0;
    }
    std::printf("MTVU EQUIVALENCE FAIL after %d failure(s)\n", failures);
    return 1;
}

}  // namespace mtvu_arm

// ===========================================================================
// --runahead [--depth N] / --runahead-off [--depth N]: the frame-denominated
// producer park. A: depth 10 > cap 2 at submit -> the submit parks and is
// released when the consumer presents frame 9 (progress release, well under
// the valve). B: depth frozen over the cap -> the 100 ms valve releases. C
// (cap 0): the same depth-10 submit returns at once.
//
// Time is the injected fake clock: every read advances it by the park loop's
// poll period (500 us), and the clock IS the frame consumer -- when fake time
// reaches +25 ms of case A it presents frame 9. So the printed milliseconds
// are the same on every run (the real park loop still waits its real 500 us
// per poll on the worker's frame park).
namespace runahead_arm {

struct Clock : public MtvuClock {
    static constexpr uint64_t kTickNs = MtvuBackpressure::kPollNs;
    mutable uint64_t t = 0;
    mutable uint64_t present_at = 0;    // 0 = no scheduled present
    std::atomic<uint64_t>* presented = nullptr;
    uint64_t present_value = 0;
    uint64_t NowNs() const override {
        const uint64_t v = t;
        t += kTickNs;
        if (present_at != 0 && v >= present_at && presented) {
            presented->store(present_value, std::memory_order_release);
            present_at = 0;
        }
        return v;
    }
    uint64_t Peek() const { return t; }
    long long ElapsedMs(uint64_t t0) const { return static_cast<long long>((t - t0) / 1000000ull); }
};

int Run(bool gate_on, int depth) {
    RegisterOpStream ops;
    GifRegs gif_regs;
    GifParser parser(ops, gif_regs);
    GifArbiter arbiter(parser);
    VuUnit vu0(0);
    VuUnit vu1(1, &arbiter);
    VuUnit vu1w(1, &arbiter);
    Vif vif(vu0, vu1, arbiter, gif_regs);
    Clock clock;
    RunaheadGuard guard(depth, {}, clock);
    MtvuBackpressure backpressure(guard, clock);
    MtvuWorker worker(vif, vu1w, arbiter, backpressure, MtvuSettings{true, depth});
    MtvuSync sync(worker, arbiter);
    std::atomic<uint64_t> declared{0};   // frames declared at the boundary enqueue
    std::atomic<uint64_t> presented{0};  // frames presented at the genuine VSync
    backpressure.SetFrameCounters(&declared, &presented);
    clock.presented = &presented;
    worker.Start();
    uint8_t nops[16] = {0};  // a VIF NOP quadword: a real segment round trip

    // Prime at depth 0: never parks in any mode.
    worker.SubmitVif1(nops, sizeof(nops), false);
    sync.Sync(SyncSite::Other);

    int failures = 0;

    // ---- A / C: depth 10 > cap 2; the consumer presents frame 9 at +25 ms ----
    declared.store(10, std::memory_order_relaxed);
    clock.present_at = clock.Peek() + 25000000ull;
    clock.present_value = 9;
    uint64_t t0 = clock.Peek();
    worker.SubmitVif1(nops, sizeof(nops), false);
    long long ms = clock.ElapsedMs(t0);
    if (gate_on) {
        if (ms < 20) {
            std::fprintf(stderr, "FAIL A: submit did not park (elapsed %lldms; park never engaged)\n", ms);
            failures++;
        } else if (ms >= 100) {
            std::fprintf(stderr, "FAIL A: released by valve, not progress (elapsed %lldms)\n", ms);
            failures++;
        } else {
            std::fprintf(stderr, "A ok: parked %lldms, released by frame progress\n", ms);
        }
    } else {
        clock.present_at = 0;
        if (ms >= 50) {
            std::fprintf(stderr, "FAIL C: gate off but submit parked (elapsed %lldms)\n", ms);
            failures++;
        } else {
            std::fprintf(stderr, "C ok: gate off, no park (%lldms)\n", ms);
        }
    }
    sync.Sync(SyncSite::Other);

    if (gate_on) {
        // ---- B: depth frozen over the cap -> the 100 ms valve, never a hang ----
        presented.store(9, std::memory_order_relaxed);
        declared.store(50, std::memory_order_relaxed);  // depth 41
        t0 = clock.Peek();
        worker.SubmitVif1(nops, sizeof(nops), false);
        ms = clock.ElapsedMs(t0);
        if (ms < 90) {
            std::fprintf(stderr, "FAIL B: frozen-depth park released early (elapsed %lldms; expected ~100ms valve)\n",
                         ms);
            failures++;
        } else if (ms > 1500) {
            std::fprintf(stderr, "FAIL B: valve did not release (elapsed %lldms; hang-class)\n", ms);
            failures++;
        } else {
            std::fprintf(stderr, "B ok: frozen depth released by valve at %lldms\n", ms);
        }
        // Clear the standing depth so teardown submits never park.
        presented.store(50, std::memory_order_relaxed);
    }

    sync.Sync(SyncSite::Other);
    worker.Stop();
    std::fprintf(stderr, "runahead selftest: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}

}  // namespace runahead_arm

int Usage() {
    std::fprintf(stderr,
                 "usage: vif_replay [--vif-fast] --unpack-bulk\n"
                 "       vif_replay [--vif-fast] --mtvu [N]               (N iterations, default 120)\n"
                 "       vif_replay --runahead [--depth N]                (default depth 2)\n"
                 "       vif_replay --runahead-off [--depth N]            (default depth 0)\n"
                 "  --vif-fast   feed the streams through the whole-span parser (default: the word latch)\n"
                 "  --mtvu-fault-no-eager   seeded fault: the MTVU arm never eager-drains (must FAIL)\n"
                 "  The OLD file mode (<mem.bin> <tadr-hex> <dump-lo> <dump-hi> ...) is not ported; the OLD\n"
                 "  exec-async arms (PS2_VU1_EXEC_ASYNC=1) have no counterpart: that thread was not transplanted.\n");
    return 2;
}

// `--depth N` anywhere after the arm; `def` when absent.
int DepthArg(const std::vector<std::string>& args, int def) {
    for (size_t i = 1; i + 1 < args.size(); i++)
        if (args[i] == "--depth") return std::atoi(args[i + 1].c_str());
    return def;
}

}  // namespace

int main(int argc, char** argv) {
    // `--vif-fast` may appear anywhere; it selects the span parser for every
    // feed of the run. Without it the word latch is forced (the OLD arm's
    // pinned default), so both feeds are proven by the two ctest rows.
    bool vif_fast = false;
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--vif-fast") == 0) {
            vif_fast = true;
            continue;
        }
        if (std::strcmp(argv[i], "--mtvu-fault-no-eager") == 0) {
            mtvu_arm::g_fault_no_eager = true;
            continue;
        }
        args.emplace_back(argv[i]);
    }
    if (args.empty()) return Usage();

    if (args[0] == "--unpack-bulk") return RunUnpackBulk(vif_fast);
    if (args[0] == "--mtvu") {
        int iters = (args.size() >= 2) ? std::atoi(args[1].c_str()) : 120;
        if (iters < 1) iters = 1;
        std::fprintf(stderr, "mtvu equivalence selftest: %d iterations (vif_fast=%d)\n", iters, vif_fast ? 1 : 0);
        return mtvu_arm::Run(iters, vif_fast);
    }
    if (args[0] == "--runahead") {
        std::fprintf(stderr, "runahead frame-cap selftest (gate on)\n");
        return runahead_arm::Run(true, DepthArg(args, 2));
    }
    if (args[0] == "--runahead-off") {
        std::fprintf(stderr, "runahead frame-cap selftest (gate off)\n");
        return runahead_arm::Run(false, DepthArg(args, 0));
    }
    return Usage();
}
