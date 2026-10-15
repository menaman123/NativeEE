// VuXgKick: the PATH1 tag walk (PacketSize over synthetic GIFtags), the
// 32-cycle pacing schedule (1 QW per 2 cycles), the second-kick stall, the
// 0x3FFF wrap, the 0x4000 packet cancel, the runaway drop, program-end flush
// without a stall charge, and both drain arms delivering identical bytes
// through GifArbiter -> GifParser -> RegisterOpStream.
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/vu/xg_kick.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

static constexpr uint32_t kMem = 0x4000;

// The rig: a GifParser + RegisterOpStream chain behind one arbiter.
struct TestXgKick {
    RegisterOpStream ops;
    GifRegs regs;
    GifParser parser;
    GifArbiter arbiter;
    TestXgKick() : parser(ops, regs), arbiter(parser) {}
};

static void PutQw(uint8_t* mem, uint32_t addr, uint64_t lo, uint64_t hi) {
    std::memcpy(mem + (addr & 0x3FF0u), &lo, 8);
    std::memcpy(mem + ((addr + 8) & 0x3FF8u), &hi, 8);
}

static uint64_t TagLo(uint32_t nloop, bool eop, uint32_t flg, uint32_t nregs) {
    return static_cast<uint64_t>(nloop & 0x7FFFu) | (static_cast<uint64_t>(eop) << 15) |
           (static_cast<uint64_t>(flg & 3u) << 58) | (static_cast<uint64_t>(nregs & 0xFu) << 60);
}

// PACKED A+D tag with `nloop` register writes to GS registers 0x18..0x1B.
static uint32_t WritePackedAd(uint8_t* mem, uint32_t addr, uint32_t nloop, bool eop, uint32_t seed) {
    PutQw(mem, addr, TagLo(nloop, eop, 0, 1), 0xEu);  // one descriptor: A+D
    for (uint32_t i = 0; i < nloop; ++i) {
        PutQw(mem, addr + 16 + i * 16, 0x1000ull * (seed + i) + i, 0x18u + (i % 4));
    }
    return 16 + nloop * 16;
}

static void TestPacketSize() {
    std::vector<uint8_t> mem(kMem, 0);
    // PACKED: nregs * nloop * 16 + 16
    PutQw(mem.data(), 0, TagLo(5, true, 0, 3), 0);
    CHECK(XgKick::PacketSize(mem.data(), 0, false) == ((16 + 5 * 3 * 16) | 0x80000000u));
    // REGLIST: odd register count pads to a whole quadword
    PutQw(mem.data(), 0x100, TagLo(3, true, 1, 3), 0);
    CHECK(XgKick::PacketSize(mem.data(), 0x100, false) == ((16 + ((3 * 3 + 1) / 2) * 16) | 0x80000000u));
    PutQw(mem.data(), 0x200, TagLo(2, false, 1, 4), 0);
    CHECK(XgKick::PacketSize(mem.data(), 0x200, false) == (16 + 4 * 16));   // no EOP bit
    // IMAGE and DISABLED: nloop quadwords whatever NREG says
    PutQw(mem.data(), 0x300, TagLo(7, true, 2, 9), 0);
    CHECK(XgKick::PacketSize(mem.data(), 0x300, false) == ((16 + 7 * 16) | 0x80000000u));
    PutQw(mem.data(), 0x400, TagLo(7, true, 3, 0), 0);
    CHECK(XgKick::PacketSize(mem.data(), 0x400, false) == ((16 + 7 * 16) | 0x80000000u));
    // NREG 0 means 16
    PutQw(mem.data(), 0x500, TagLo(1, true, 0, 0), 0);
    CHECK(XgKick::PacketSize(mem.data(), 0x500, false) == ((16 + 16 * 16) | 0x80000000u));
    // Chain walk under flush: two tags to EOP; paced sees only the first.
    std::fill(mem.begin(), mem.end(), 0);
    PutQw(mem.data(), 0x1000, TagLo(2, false, 0, 1), 0xE);
    PutQw(mem.data(), 0x1000 + 48, TagLo(1, true, 0, 1), 0xE);
    CHECK(XgKick::PacketSize(mem.data(), 0x1000, false) == 48u);
    CHECK(XgKick::PacketSize(mem.data(), 0x1000, true) == ((48 + 32) | 0x80000000u));
    // The walk wraps at 16 KB.
    PutQw(mem.data(), 0x3FF0, TagLo(1, false, 0, 1), 0xE);
    PutQw(mem.data(), 0x0010, TagLo(1, true, 0, 1), 0xE);
    CHECK(XgKick::PacketSize(mem.data(), 0x3FF0, true) == (64u | 0x80000000u));
    // A packet reaching 0x4000 bytes cancels.
    PutQw(mem.data(), 0x2000, TagLo(1023, true, 0, 1), 0xE);    // 16 + 1023*16 = 0x4000
    CHECK(XgKick::PacketSize(mem.data(), 0x2000, false) == 0u);
    PutQw(mem.data(), 0x2000, TagLo(1022, true, 0, 1), 0xE);    // 0x3FF0: still valid
    CHECK(XgKick::PacketSize(mem.data(), 0x2000, false) == (0x3FF0u | 0x80000000u));
    // Unaligned offsets read the containing quadword.
    CHECK(XgKick::PacketSize(mem.data(), 0x2004, false) == (0x3FF0u | 0x80000000u));
}

static void TestPacingSchedule() {
    std::vector<uint8_t> mem(kMem, 0);
    const uint32_t bytes = WritePackedAd(mem.data(), 0x800, 40, true, 1);  // 41 quadwords
    CHECK(bytes == 656u);
    TestXgKick c;
    XgKick xk(c.arbiter, true);
    uint64_t cycle = 1000;
    xk.Arm(mem.data(), 0x80, cycle);
    CHECK(xk.armed() && xk.addr() == 0x800u && xk.cyclecount() == 1 && xk.next_due() == 1033);
    CHECK(cycle == 1000);
    // Nothing moves before the first quantum is due.
    for (cycle = 1001; cycle < 1033; ++cycle) xk.Pace(mem.data(), cycle);
    CHECK(xk.staging_bytes() == 0 && xk.addr() == 0x800u);
    // 1033: 32 elapsed cycles -> 16 quadwords.
    xk.Pace(mem.data(), cycle);
    CHECK(xk.staging_bytes() == 16 * 16);
    CHECK(xk.addr() == 0x800u + 256);
    CHECK(xk.size_remaining() == bytes - 256);
    CHECK(xk.cyclecount() == 1 && xk.next_due() == 1065);
    // An early Pace does nothing.
    cycle = 1050;
    xk.Pace(mem.data(), cycle);
    CHECK(xk.staging_bytes() == 256);
    // 1065: 16 more.
    cycle = 1065;
    xk.Pace(mem.data(), cycle);
    CHECK(xk.staging_bytes() == 512 && xk.armed());
    CHECK(c.arbiter.PendingBytes() == 0);
    // A late Pace banks every elapsed cycle: 1130 - 1064 - 1 = 65 -> 33 QW available, 9 left -> packet complete.
    cycle = 1130;
    xk.Pace(mem.data(), cycle);
    CHECK(!xk.armed());
    CHECK(xk.staging_bytes() == 0);
    CHECK(c.arbiter.PendingBytes() == bytes);
    CHECK(cycle == 1130);                                    // pacing never charges the clock
    c.arbiter.drain();
    CHECK(c.arbiter.DrainedPackets() == 1);
    CHECK(c.ops.Ops().size() == 1 + 40 + 1);                 // Tag + 40 A+D writes + Eop
    // Pace after completion is a no-op.
    cycle = 2000;
    xk.Pace(mem.data(), cycle);
    CHECK(c.arbiter.PendingBytes() == 0);
}

static void TestDrainTimeCopy() {
    // A store issued after the kick but ahead of the drain pointer IS transferred.
    std::vector<uint8_t> mem(kMem, 0);
    WritePackedAd(mem.data(), 0, 40, true, 7);
    TestXgKick c;
    XgKick xk(c.arbiter, true);
    uint64_t cycle = 0;
    xk.Arm(mem.data(), 0, cycle);
    cycle = 33;
    xk.Pace(mem.data(), cycle);                              // first 16 QW copied
    PutQw(mem.data(), 16 * 30, 0xDEADBEEFull, 0x47u);        // rewrite QW 30 (not yet drained)
    PutQw(mem.data(), 16 * 5, 0xBADull, 0x47u);              // rewrite QW 5 (already drained)
    cycle = 200;
    xk.Pace(mem.data(), cycle);
    CHECK(!xk.armed());
    c.arbiter.drain();
    const std::vector<RegisterOp>& ops = c.ops.Ops();
    CHECK(ops.size() == 42);
    if (ops.size() == 42) {
        CHECK(ops[30].lo == 0xDEADBEEFull && ops[30].reg == 0x47);   // the late store landed
        CHECK(ops[5].lo != 0xBADull);                                // the early quadword kept its drained bytes
    }
}

static void TestSecondKickStall() {
    std::vector<uint8_t> mem(kMem, 0);
    WritePackedAd(mem.data(), 0x000, 40, true, 1);           // A: 41 QW at 0
    WritePackedAd(mem.data(), 0x1000, 3, true, 2);           // B: 4 QW at 0x1000
    TestXgKick c;
    XgKick xk(c.arbiter, true);
    uint64_t cycle = 100;
    xk.Arm(mem.data(), 0x00, cycle);
    cycle = 133;
    xk.Pace(mem.data(), cycle);                              // 16 QW drained, 25 remain
    CHECK(xk.size_remaining() == 25 * 16);
    cycle = 140;
    xk.Arm(mem.data(), 0x100, cycle);                        // second kick: finish A, stall 25 * 2
    CHECK(cycle == 140 + 50);
    CHECK(c.arbiter.PendingBytes() == 656);
    CHECK(xk.armed() && xk.addr() == 0x1000u && xk.next_due() == 190 + 33);
    // Program end completes B with no stall charge.
    xk.Flush(mem.data(), cycle, false);
    CHECK(cycle == 190);
    CHECK(!xk.armed());
    CHECK(c.arbiter.PendingBytes() == 656 + 64);
    c.arbiter.drain();
    CHECK(c.arbiter.DrainedPackets() == 2);
    // Flush while disarmed is a no-op.
    xk.Flush(mem.data(), cycle, true);
    CHECK(cycle == 190);
}

static void TestWrapCancelRunaway() {
    // Wrap: a packet straddling 0x3FF0 -> 0x0000 arrives as one contiguous submission.
    std::vector<uint8_t> mem(kMem, 0);
    PutQw(mem.data(), 0x3FF0, TagLo(2, true, 0, 1), 0xE);
    PutQw(mem.data(), 0x0000, 0x1111ull, 0x18u);
    PutQw(mem.data(), 0x0010, 0x2222ull, 0x19u);
    for (int paced = 0; paced < 2; ++paced) {
        TestXgKick c;
        XgKick xk(c.arbiter, paced != 0);
        uint64_t cycle = 0;
        xk.Arm(mem.data(), 0x3FF, cycle);
        cycle = 33;
        xk.Pace(mem.data(), cycle);
        CHECK(!xk.armed());
        CHECK(c.arbiter.PendingBytes() == 48);
        c.arbiter.drain();
        CHECK(c.ops.Ops().size() == 4);
        if (c.ops.Ops().size() == 4) {
            CHECK(c.ops.Ops()[1].lo == 0x1111ull && c.ops.Ops()[1].reg == 0x18);
            CHECK(c.ops.Ops()[2].lo == 0x2222ull && c.ops.Ops()[2].reg == 0x19);
        }
    }
    // Cancel: a tag whose span reaches 0x4000 submits nothing, in both arms.
    std::fill(mem.begin(), mem.end(), 0);
    PutQw(mem.data(), 0x100, TagLo(1023, true, 0, 1), 0xE);
    for (int paced = 0; paced < 2; ++paced) {
        TestXgKick c;
        XgKick xk(c.arbiter, paced != 0);
        uint64_t cycle = 0;
        xk.Arm(mem.data(), 0x10, cycle);
        cycle = 33;
        xk.Pace(mem.data(), cycle);
        xk.Flush(mem.data(), cycle, false);
        CHECK(!xk.armed());
        CHECK(c.arbiter.PendingBytes() == 0);
        CHECK(cycle == 33);
    }
    // Cancel after a partial chain: the bytes staged before the bad tag still go out.
    std::fill(mem.begin(), mem.end(), 0);
    WritePackedAd(mem.data(), 0x000, 2, false, 3);           // 48 bytes, no EOP
    PutQw(mem.data(), 48, TagLo(1023, true, 0, 1), 0xE);     // then an oversized tag
    {
        TestXgKick c;
        XgKick xk(c.arbiter, true);
        uint64_t cycle = 0;
        xk.Arm(mem.data(), 0, cycle);
        cycle = 33;
        xk.Pace(mem.data(), cycle);
        CHECK(!xk.armed());
        CHECK(c.arbiter.PendingBytes() == 48);
    }
    // Runaway: all-zero memory is an endless chain of empty tags; the paced
    // drain drops the staging once it passes 0x40000 bytes.
    std::fill(mem.begin(), mem.end(), 0);
    {
        TestXgKick c;
        XgKick xk(c.arbiter, true);
        uint64_t cycle = 0;
        xk.Arm(mem.data(), 0, cycle);
        for (int i = 1; i <= 1100 && xk.armed(); ++i) {
            cycle = static_cast<uint64_t>(i) * 33;
            xk.Pace(mem.data(), cycle);
        }
        CHECK(!xk.armed());
        CHECK(xk.staging_bytes() == 0);
        CHECK(c.arbiter.PendingBytes() == 0);
    }
    // Reset disarms and drops the staging.
    {
        TestXgKick c;
        XgKick xk(c.arbiter, true);
        WritePackedAd(mem.data(), 0, 40, true, 1);
        uint64_t cycle = 0;
        xk.Arm(mem.data(), 0, cycle);
        cycle = 33;
        xk.Pace(mem.data(), cycle);
        CHECK(xk.staging_bytes() == 256);
        xk.Reset();
        CHECK(!xk.armed() && xk.staging_bytes() == 0 && xk.cyclecount() == 0);
        xk.Flush(mem.data(), cycle, true);
        CHECK(c.arbiter.PendingBytes() == 0);
    }
}

static void TestBothArmsIdentical() {
    std::vector<uint8_t> mem(kMem, 0);
    // Three kicks: a two-tag chain, a REGLIST packet, a packet straddling the wrap.
    uint32_t a = 0x200;
    a += WritePackedAd(mem.data(), a, 5, false, 11);
    WritePackedAd(mem.data(), a, 3, true, 12);
    PutQw(mem.data(), 0x1000, TagLo(2, true, 1, 3), 0x510u);  // REGLIST: PRIM, RGBAQ, XYZ2 x 2 loops -> 3 QW
    PutQw(mem.data(), 0x1010, 0x1ull, 0x2ull);
    PutQw(mem.data(), 0x1020, 0x3ull, 0x4ull);
    PutQw(mem.data(), 0x1030, 0x5ull, 0x6ull);
    PutQw(mem.data(), 0x3FE0, TagLo(3, true, 0, 1), 0xE);
    PutQw(mem.data(), 0x3FF0, 0xAAull, 0x18u);
    PutQw(mem.data(), 0x0000, 0xBBull, 0x19u);
    PutQw(mem.data(), 0x0010, 0xCCull, 0x1Au);

    uint64_t hash[2] = {0, 0};
    size_t nops[2] = {0, 0};
    uint64_t bytes[2] = {0, 0};
    for (int paced = 0; paced < 2; ++paced) {
        TestXgKick c;
        XgKick xk(c.arbiter, paced != 0);
        uint64_t cycle = 10;
        xk.Arm(mem.data(), 0x20, cycle);
        for (int i = 0; i < 20; ++i) {
            cycle += 33;
            xk.Pace(mem.data(), cycle);
        }
        xk.Arm(mem.data(), 0x100, cycle);
        cycle += 5;
        xk.Arm(mem.data(), 0x3FE, cycle);                    // second kick while REGLIST drains: stall + drain
        xk.Flush(mem.data(), cycle, false);                  // program end
        CHECK(!xk.armed());
        bytes[paced] = c.arbiter.PendingBytes();
        c.arbiter.drain();
        CHECK(c.arbiter.DrainedPackets() == 3);
        hash[paced] = c.ops.Hash();
        nops[paced] = c.ops.Ops().size();
    }
    CHECK(bytes[0] == bytes[1] && bytes[0] == (16 + 80 + 16 + 48) + (16 + 48) + (16 + 48));
    CHECK(hash[0] == hash[1]);
    CHECK(nops[0] == nops[1] && nops[0] > 0);
}

int main() {
    TestPacketSize();
    TestPacingSchedule();
    TestDrainTimeCopy();
    TestSecondKickStall();
    TestWrapCancelRunaway();
    TestBothArmsIdentical();
    if (g_fail) {
        std::printf("VuXgKick: FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("VuXgKick: PASS (paced == immediate through GifArbiter -> GifParser -> RegisterOpStream)\n");
    return 0;
}
