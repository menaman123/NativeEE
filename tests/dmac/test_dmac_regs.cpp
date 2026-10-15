// DmacRegs: the DMAC register map and channel lifecycle on a MainMemory +
// Scratchpad + VIF/GIF rig -- channel pages (reads by range, writes by 256-byte
// page), dropped globals, PCR, D_ENABLER/W cold values, sub-word widening,
// CHCR.STR -> CompleteChannel, status W1C and mask toggles, handler dispatch
// once a mask bit is restored, SPR transfers, the deferred VIF1 kick's
// register-file observation points, and the TAG_DMAC codec.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/dmac/dmac.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/vif/vif.h"
#include "engine/vu/vu_unit.h"

namespace {

int g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            g_fail++;                                                        \
        }                                                                    \
    } while (0)

// One complete rig: guest memory, GS recording sink, GIF arbiter, VU units, VIF, DMAC.
struct TestDmacRegs {
    MainMemory ram;
    Scratchpad spad;
    RegisterOpStream ops;
    GifRegs gif_regs;
    GifParser parser;
    GifArbiter arbiter;
    VuUnit vu0;
    VuUnit vu1;
    Vif vif;
    Dmac dmac;
    TestDmacRegs()
        : parser(ops, gif_regs), arbiter(parser), vu0(0), vu1(1, &arbiter), vif(vu0, vu1, arbiter, gif_regs),
          dmac(ram, spad, vif, arbiter) {}
};
using Rig = TestDmacRegs;

void Put32(MainMemory& ram, uint32_t addr, uint32_t v) { std::memcpy(ram.Pointer(addr, 4), &v, 4); }
void Put64(MainMemory& ram, uint32_t addr, uint64_t v) { std::memcpy(ram.Pointer(addr, 8), &v, 8); }

uint64_t TagLo(uint32_t qwc, uint32_t id, bool irq, uint32_t addr, bool spr) {
    return static_cast<uint64_t>(qwc & 0xFFFFu) | (static_cast<uint64_t>(id & 7u) << 28) |
           (irq ? (1ull << 31) : 0ull) | (static_cast<uint64_t>(addr) << 32) | (spr ? (1ull << 63) : 0ull);
}
void PutTag(MainMemory& ram, uint32_t at, uint32_t qwc, uint32_t id, bool irq, uint32_t addr, bool spr,
            uint32_t w2 = 0, uint32_t w3 = 0) {
    Put64(ram, at, TagLo(qwc, id, irq, addr, spr));
    Put64(ram, at + 8, static_cast<uint64_t>(w2) | (static_cast<uint64_t>(w3) << 32));
}
// UNPACK V4-32, one quadword, into VU1 data qw `dst_qw`, then the 16-byte payload.
void PutUnpackQw(MainMemory& ram, uint32_t at, uint32_t dst_qw, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    Put32(ram, at + 0, (0x6Cu << 24) | (1u << 16) | (dst_qw & 0x3FFu));
    Put32(ram, at + 4, a);
    Put32(ram, at + 8, b);
    Put32(ram, at + 12, c);
    Put32(ram, at + 16, d);
    Put32(ram, at + 20, 0);
    Put32(ram, at + 24, 0);
    Put32(ram, at + 28, 0);
}
bool QwIs(const VuUnit& vu, uint32_t qw, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint32_t got[4];
    vu.ReadDataMem((qw * 16) & vu.mem_mask(), reinterpret_cast<uint8_t*>(got), 16);
    return got[0] == a && got[1] == b && got[2] == c && got[3] == d;
}

constexpr uint32_t kPage[10] = {DmacAddr::kVif0, DmacAddr::kVif1, DmacAddr::kGif,     DmacAddr::kIpuFrom,
                                DmacAddr::kIpuTo, DmacAddr::kSif0, DmacAddr::kSif1,   DmacAddr::kSif2,
                                DmacAddr::kSprFrom, DmacAddr::kSprTo};
constexpr uint32_t kChainKick = DmacChcr::kStr | (DmacChcr::kModChain << DmacChcr::kModShift) | DmacChcr::kDir;

// A one-UNPACK VIF1 chain at `base`: CNT qwc 2 (UNPACK code + 4 payload words
// -> VU1 qw `dst`, zero-padded to two quadwords) then END qwc 0 at base + 48.
void BuildVif1Chain(MainMemory& ram, uint32_t base, uint32_t dst, uint32_t marker) {
    PutTag(ram, base, 2, DmacTagId::kCnt, false, 0, false);
    PutUnpackQw(ram, base + 16, dst, marker, marker + 1, marker + 2, marker + 3);
    PutTag(ram, base + 48, 0, DmacTagId::kEnd, false, 0, false);
}

// Both IPU pages are writable; SIF1 and SIF2 remain write-dropped.
bool WritablePage(int ch) { return ch != 6 && ch != 7; }

void TestPageMap() {
    Rig rig;
    for (int ch = 0; ch < 10; ++ch) {
        const uint32_t p = kPage[ch];
        rig.dmac.Write(p + DmacRegOffset::kMadr, 4, 0x1000u + ch);
        rig.dmac.Write(p + DmacRegOffset::kQwc, 4, 0x20u + ch);
        rig.dmac.Write(p + DmacRegOffset::kTadr, 4, 0x3000u + ch);
        rig.dmac.Write(p + DmacRegOffset::kAsr0, 4, 0x4000u + ch);
        rig.dmac.Write(p + DmacRegOffset::kAsr1, 4, 0x5000u + ch);
        rig.dmac.Write(p + DmacRegOffset::kSadr, 4, 0x80u + ch);
        if (WritablePage(ch)) {
            CHECK(rig.dmac.Read(p + DmacRegOffset::kMadr, 4) == 0x1000u + ch);
            CHECK(rig.dmac.Read(p + DmacRegOffset::kQwc, 4) == 0x20u + ch);
            CHECK(rig.dmac.Read(p + DmacRegOffset::kTadr, 4) == 0x3000u + ch);
            CHECK(rig.dmac.Read(p + DmacRegOffset::kAsr0, 4) == 0x4000u + ch);
            CHECK(rig.dmac.Read(p + DmacRegOffset::kAsr1, 4) == 0x5000u + ch);
            CHECK(rig.dmac.Read(p + DmacRegOffset::kSadr, 4) == 0x80u + ch);
            CHECK(rig.dmac.channel(ch).madr == 0x1000u + ch && rig.dmac.channel(ch).sadr == 0x80u + ch);
        } else {
            // Dropped writes; the members stay 0 and a kick through MMIO never starts.
            CHECK(rig.dmac.Read(p + DmacRegOffset::kMadr, 4) == 0 && rig.dmac.channel(ch).madr == 0);
            CHECK(rig.dmac.channel(ch).qwc == 0 && rig.dmac.channel(ch).sadr == 0);
            rig.dmac.Write(p, 4, DmacChcr::kStr);
            CHECK(rig.dmac.channel(ch).chcr == 0 && (rig.dmac.globals().stat & DmacStat::Cis(ch)) == 0);
        }
        CHECK(rig.dmac.Read(p + DmacRegOffset::kChcr, 4) == 0);
        // Reads reach every channel's members regardless.
        rig.dmac.channel(ch).asr1 = 0x77u + ch;
        CHECK(rig.dmac.Read(p + DmacRegOffset::kAsr1, 4) == 0x77u + ch);
    }
    // Reads map the whole 0x8000..0x8FFF range to VIF0 but only the seven
    // register offsets carry a value; writes map by 256-byte page, so 0x8100 is dropped.
    CHECK(rig.dmac.Read(0x10008100u, 4) == 0);
    rig.dmac.Write(0x10008110u, 4, 0xDEADu);
    CHECK(rig.dmac.channel(0).madr == 0x1000u);
    CHECK(rig.dmac.Read(0x10008010u, 4) == 0x1000u);
    CHECK(Dmac::ChannelForWrite(0x10008110u) == -1);
    uint32_t off = 0;
    CHECK(Dmac::ChannelForRead(0x10008110u, &off) == 0 && off == 0x110u);
    CHECK(Dmac::ChannelForRead(0x1000B7F0u, &off) == 4 && off == 0x3F0u);
    CHECK(Dmac::ChannelForRead(0x1000DFFCu, &off) == 9);
    CHECK(Dmac::ChannelForRead(0x1000C800u, &off) == 7 && Dmac::ChannelForRead(0x1000C400u, &off) == 6);
    CHECK(Dmac::ChannelForWrite(0x1000C000u) == 5 && Dmac::ChannelForWrite(0x1000B000u) == 3 &&
          Dmac::ChannelForWrite(0x1000D000u) == 8 && Dmac::ChannelForWrite(0x1000A0F0u) == 2);
    CHECK(Dmac::ChannelForWrite(0x1000C800u) == -1 && Dmac::ChannelForWrite(0x1000C400u) == -1 &&
          Dmac::ChannelForWrite(0x1000B400u) == 4 && Dmac::ChannelForWrite(0x1000D400u) == 9 &&
          Dmac::ChannelForWrite(0x1000E000u) == -1);
}

void TestGlobals() {
    Rig rig;
    // Cold boot: D_ENABLER reads 0 (no reset ever ran in the reference), D_ENABLEW reads 0.
    CHECK(rig.dmac.Read(DmacAddr::kEnabler, 4) == 0);
    CHECK(rig.dmac.Read(DmacAddr::kEnablew, 4) == 0);
    // Globals other than PCR and STAT remain dropped on write.
    const uint32_t dropped[] = {DmacAddr::kCtrl, DmacAddr::kSqwc, DmacAddr::kRbsr,
                                DmacAddr::kRbor, DmacAddr::kStadr, DmacAddr::kEnablew, DmacAddr::kEnabler};
    for (uint32_t a : dropped) {
        rig.dmac.Write(a, 4, 0xFFFFFFFFu);
        CHECK(rig.dmac.Read(a, 4) == 0);
    }
    CHECK(rig.dmac.globals().stat == 0);
    CHECK(!rig.dmac.CheckInterrupt());
    rig.dmac.Write(DmacAddr::kPcr, 4, 0x1234u);
    CHECK(rig.dmac.Read(DmacAddr::kPcr, 4) == 0x1234u && rig.dmac.globals().pcr == 0x1234u);
    // Reads of the globals come from the members (a restored image is visible).
    rig.dmac.globals().ctrl = 1;
    rig.dmac.globals().stat = 0x00010002u;
    rig.dmac.globals().sqwc = 3;
    rig.dmac.globals().rbsr = 4;
    rig.dmac.globals().rbor = 5;
    rig.dmac.globals().stadr = 6;
    rig.dmac.globals().enabler = 0x1201u;
    CHECK(rig.dmac.Read(DmacAddr::kCtrl, 4) == 1 && rig.dmac.Read(DmacAddr::kStat, 4) == 0x00010002u);
    CHECK(rig.dmac.Read(DmacAddr::kSqwc, 4) == 3 && rig.dmac.Read(DmacAddr::kRbsr, 4) == 4);
    CHECK(rig.dmac.Read(DmacAddr::kRbor, 4) == 5 && rig.dmac.Read(DmacAddr::kStadr, 4) == 6);
    CHECK(rig.dmac.Read(DmacAddr::kEnabler, 4) == 0x1201u);
    CHECK(rig.dmac.Read(0x1000E070u, 4) == 0);
}

void TestStatAcknowledgementAndMasks() {
    Rig rig;
    int raised = 0;
    rig.dmac.SetInterruptRaise([&] { ++raised; });
    rig.dmac.CompleteChannel(8);
    rig.dmac.CompleteChannel(9);
    CHECK(!rig.dmac.CheckInterrupt());
    rig.dmac.Write(DmacAddr::kStat, 4, DmacStat::Cim(8));
    CHECK(rig.dmac.CheckInterrupt() && raised == 1);  // enabling pending CIS raises INT1
    rig.dmac.Write(DmacAddr::kStat, 4, DmacStat::Cis(8));
    CHECK(!rig.dmac.CheckInterrupt());
    CHECK(rig.dmac.globals().stat == (DmacStat::Cim(8) | DmacStat::Cis(9)));
    rig.dmac.Write(DmacAddr::kStat, 4, 0);
    CHECK(rig.dmac.globals().stat == (DmacStat::Cim(8) | DmacStat::Cis(9)));
    rig.dmac.Write(DmacAddr::kStat, 4, DmacStat::Cim(8) | DmacStat::Cis(9));
    CHECK(rig.dmac.globals().stat == 0);  // masks toggle, status clears
    rig.dmac.Write(DmacAddr::kStat, 4, 0xFFFFFFFFu);
    CHECK(rig.dmac.globals().stat == DmacStat::kMaskWriteMask);  // reserved bits cannot be set
    CHECK(!rig.dmac.CheckInterrupt());
    rig.dmac.Write(DmacAddr::kStat, 4, 0xFFFFFFFFu);
    CHECK(rig.dmac.globals().stat == 0);
}

void TestWidening() {
    Rig rig;
    const uint32_t p = DmacAddr::kSif0;
    rig.dmac.channel(5).chcr = 0x12345678u;
    rig.dmac.channel(5).madr = 0x89ABCDEFu;
    CHECK(rig.dmac.Read(p + 1, 1) == 0x56u);
    CHECK(rig.dmac.Read(p + 3, 1) == 0x12u);
    CHECK(rig.dmac.Read(p + 2, 2) == 0x1234u);
    CHECK(rig.dmac.Read(p + 0x10, 8) == 0x89ABCDEFull);  // the +4 half is nobody's register
    CHECK(rig.dmac.Read(p + 0x12, 2) == 0x89ABu);
    // A sub-word store writes the raw value as the whole 32-bit register at addr & ~3.
    rig.dmac.Write(p + DmacRegOffset::kQwc, 1, 0x12u);
    CHECK(rig.dmac.channel(5).qwc == 0x12u);
    rig.dmac.Write(p + DmacRegOffset::kQwc + 2, 2, 0xABCDu);
    CHECK(rig.dmac.channel(5).qwc == 0xABCDu);
    // 8-byte store: two halves; the +4 half lands nowhere.
    rig.dmac.Write(p + DmacRegOffset::kTadr, 8, 0x0000000100000ABCull);
    CHECK(rig.dmac.channel(5).tadr == 0xABCu && rig.dmac.channel(5).asr0 == 0);
}

void TestCompleteAndMask() {
    Rig rig;
    // SIF0 with no callback: the kick completes with no data movement.
    rig.dmac.channel(5).qwc = 5;
    rig.dmac.Write(DmacAddr::kSif0, 4, DmacChcr::kStr | 0x1u);
    CHECK((rig.dmac.channel(5).chcr & DmacChcr::kStr) == 0);
    CHECK(rig.dmac.channel(5).chcr == 0x1u);
    CHECK(rig.dmac.channel(5).qwc == 0);
    CHECK((rig.dmac.globals().stat & DmacStat::Cis(5)) != 0);
    CHECK(!rig.dmac.CheckInterrupt());  // mask 0: never pending on a cold boot
    CHECK(rig.dmac.Read(DmacAddr::kSif0, 4) == 0x1u);
    // Writing STR = 0 only stores the value.
    rig.dmac.Write(DmacAddr::kSif0, 4, 0x44u);
    CHECK(rig.dmac.channel(5).chcr == 0x44u);
    // SIF1 / SIF2 / IPU TO / SPR TO complete the same way when started directly
    // (the kernel HLE's path); a SIF1 callback sees the channel registers.
    int sif_calls = 0;
    rig.dmac.SetSifCallback([&sif_calls](int ch, const DmacChannelRegs& r) {
        sif_calls += (ch == 6 && r.madr == 0x1234u) ? 1 : 100;
    });
    rig.dmac.channel(6).madr = 0x1234u;
    rig.dmac.channel(6).chcr = DmacChcr::kStr;
    rig.dmac.StartChannel(6);
    CHECK(sif_calls == 1 && (rig.dmac.globals().stat & DmacStat::Cis(6)) != 0);
    CHECK((rig.dmac.channel(6).chcr & DmacChcr::kStr) == 0);
    rig.dmac.channel(7).chcr = DmacChcr::kStr;
    rig.dmac.StartChannel(7);
    CHECK(sif_calls == 1 && (rig.dmac.globals().stat & DmacStat::Cis(7)) != 0);
    rig.dmac.globals().stat &= ~DmacStat::Cis(6);
    // CIS accumulates without a mask (the reference's sceSifSetDma path).
    rig.dmac.globals().stat |= DmacStat::Cis(6);
    CHECK(!rig.dmac.CheckInterrupt());
    // A restored mask bit makes the channel pending.
    rig.dmac.globals().stat |= DmacStat::Cim(6);
    CHECK(rig.dmac.CheckInterrupt());
    // An INT1 slot fires from CompleteChannel only when CIS & CIM is non-zero.
    int raised = 0;
    rig.dmac.SetInterruptRaise([&raised] { raised++; });
    rig.dmac.CompleteChannel(6);
    CHECK(raised == 1);
    rig.dmac.globals().stat = 0;
    rig.dmac.CompleteChannel(5);
    CHECK(raised == 1);
}

void TestDispatch() {
    Rig rig;
    struct Call {
        uint32_t pc, a0, gp;
    };
    std::vector<Call> calls;
    int32_t ret_for_b = 0;
    rig.dmac.SetGuestCall([&](uint32_t pc, CpuContext& ctx) {
        calls.push_back({pc, ctx.cpuRegs.GPR.r[4].UL[0], ctx.cpuRegs.GPR.r[28].UL[0]});
        ctx.cpuRegs.GPR.r[2].UL[0] = static_cast<uint32_t>(pc == 0xB000u ? ret_for_b : 0);
        ctx.cpuRegs.pc = pc;
        ctx.cpuRegs.GPR.r[31].UL[0] = 0xDEADu;
        return true;
    });
    DmacHandlerRegistry& reg = rig.dmac.handlers();
    CHECK(reg.NextId() == 1);
    const int a = reg.Add(6, 0xA000u, 0x11u, 0, 0, -1);
    const int b = reg.Add(6, 0xB000u, 0x22u, 0, 0, -1);
    const int c = reg.Add(6, 0xC000u, 0x33u, 0, 0, -1);
    const int front = reg.Add(6, 0xF000u, 0x44u, 0, 0, 0);
    const int before_c = reg.Add(6, 0xD000u, 0x55u, 0, 0, c);
    CHECK(a == 1 && b == 2 && c == 3 && front == 4 && before_c == 5 && reg.NextId() == 6);
    CHECK(reg.Add(16, 0x1u, 0, 0, 0, -1) == -1 && reg.Add(6, 0, 0, 0, 0, -1) == -1);
    const std::vector<DmacHandler> chain = reg.ForCause(6);
    CHECK(chain.size() == 5 && chain[0].pc == 0xF000u && chain[1].pc == 0xA000u && chain[2].pc == 0xB000u &&
          chain[3].pc == 0xD000u && chain[4].pc == 0xC000u);
    CHECK(reg.SetActive(front, false) && !reg.SetActive(99, false));

    CpuContext ctx{};
    ctx.cpuRegs.pc = 0x100u;
    ctx.cpuRegs.GPR.r[31].UL[0] = 0x200u;
    // Pending only where CIS & CIM: channel 6 (handlers) and channel 3 (none).
    rig.dmac.globals().stat = DmacStat::Cis(6) | DmacStat::Cim(6) | DmacStat::Cis(3) | DmacStat::Cim(3) |
                              DmacStat::Cis(1) | DmacStat::Cim(2);
    rig.dmac.DispatchInterrupt(ctx);
    CHECK(calls.size() == 4);  // front disabled; A, B, D, C
    CHECK(calls[0].pc == 0xA000u && calls[0].a0 == 6 && calls[0].gp == 0x11u);
    CHECK(calls[1].pc == 0xB000u && calls[2].pc == 0xD000u && calls[3].pc == 0xC000u);
    CHECK(ctx.cpuRegs.pc == 0x100u && ctx.cpuRegs.GPR.r[31].UL[0] == 0x200u);
    CHECK(rig.dmac.globals().stat == (DmacStat::Cim(6) | DmacStat::Cim(3) | DmacStat::Cis(1) | DmacStat::Cim(2)));
    // A -1 return stops the channel's chain.
    calls.clear();
    ret_for_b = -1;
    rig.dmac.globals().stat |= DmacStat::Cis(6);
    rig.dmac.DispatchInterrupt(ctx);
    CHECK(calls.size() == 2 && calls[1].pc == 0xB000u);
    CHECK((rig.dmac.globals().stat & DmacStat::Cis(6)) == 0);
    // Remove: remaining count, -1 when absent or the cause is not a channel.
    CHECK(reg.Remove(6, b) == 4 && reg.Remove(6, b) == -1 && reg.Remove(10, a) == -1);
}

void TestSprCopy() {
    Rig rig;
    for (int ch = 8; ch <= 9; ++ch) {
        // Each direction starts with distinct bytes, so an accidental no-op
        // cannot pass after the preceding transfer made the buffers identical.
        for (uint32_t i = 0; i < 64; ++i) {
            rig.ram.Data()[0x1000 + i] = static_cast<uint8_t>(0xA0 + i);
            rig.spad.Data()[0x100 + i] = static_cast<uint8_t>(0x50 + i);
        }
        rig.dmac.channel(ch).madr = 0x1000u;
        rig.dmac.channel(ch).sadr = 0x100u;
        rig.dmac.channel(ch).qwc = 4;
        if (ch == 8) {
            rig.dmac.Write(kPage[ch], 4, DmacChcr::kStr);
        } else {
            rig.dmac.channel(ch).chcr = DmacChcr::kStr | DmacChcr::kDir;
            rig.dmac.StartChannel(ch);
        }
        CHECK((rig.dmac.channel(ch).chcr & DmacChcr::kStr) == 0);
        CHECK(rig.dmac.channel(ch).qwc == 0);
        CHECK(rig.dmac.channel(ch).madr == 0x1040u && rig.dmac.channel(ch).sadr == 0x140u);
        CHECK((rig.dmac.globals().stat & DmacStat::Cis(ch)) != 0);
        for (uint32_t i = 0; i < 64; ++i) {
            const uint8_t expected = static_cast<uint8_t>((ch == 8 ? 0x50 : 0xA0) + i);
            CHECK(rig.ram.Data()[0x1000 + i] == expected);
            CHECK(rig.spad.Data()[0x100 + i] == expected);
        }
    }
}

void TestDeferredObservationPoints() {
    // CHCR read.
    {
        Rig rig;
        BuildVif1Chain(rig.ram, 0x2000u, 5, 0x100u);
        rig.dmac.Write(DmacAddr::kVif1 + DmacRegOffset::kTadr, 4, 0x2000u);
        rig.dmac.Write(DmacAddr::kVif1, 4, kChainKick);
        CHECK((rig.dmac.channel(1).chcr & DmacChcr::kStr) != 0);  // parked: reads busy
        CHECK(rig.dmac.HasDeferredVif1());
        CHECK(QwIs(rig.vu1, 5, 0, 0, 0, 0));
        CHECK((rig.dmac.globals().stat & DmacStat::Cis(1)) == 0);
        CHECK(rig.dmac.Read(DmacAddr::kStat, 4) == 0);  // D_STAT is not an observation point
        CHECK(rig.dmac.HasDeferredVif1() && QwIs(rig.vu1, 5, 0, 0, 0, 0));
        const uint64_t chcr = rig.dmac.Read(DmacAddr::kVif1, 4);
        CHECK((chcr & DmacChcr::kStr) == 0);
        CHECK(!rig.dmac.HasDeferredVif1());
        CHECK(QwIs(rig.vu1, 5, 0x100u, 0x101u, 0x102u, 0x103u));
        CHECK((rig.dmac.globals().stat & DmacStat::Cis(1)) != 0);
        CHECK(rig.dmac.channel(1).qwc == 0 && rig.dmac.channel(1).tadr == 0x2000u + 64);
        CHECK(!rig.arbiter.hasPending());
    }
    // Any other VIF1 register read (MADR) flushes too; a VIF0 read does not.
    {
        Rig rig;
        BuildVif1Chain(rig.ram, 0x2000u, 6, 0x200u);
        rig.dmac.channel(1).tadr = 0x2000u;
        rig.dmac.Write(DmacAddr::kVif1, 4, kChainKick);
        rig.dmac.Read(DmacAddr::kVif0, 4);
        CHECK(rig.dmac.HasDeferredVif1());
        rig.dmac.Read(DmacAddr::kVif1 + DmacRegOffset::kMadr, 4);
        CHECK(!rig.dmac.HasDeferredVif1() && QwIs(rig.vu1, 6, 0x200u, 0x201u, 0x202u, 0x203u));
    }
    // PCR with CPC(VIF1) flushes; without it does not.
    {
        Rig rig;
        BuildVif1Chain(rig.ram, 0x2000u, 7, 0x300u);
        rig.dmac.channel(1).tadr = 0x2000u;
        rig.dmac.Write(DmacAddr::kVif1, 4, kChainKick);
        rig.dmac.Write(DmacAddr::kPcr, 4, 0x1u);
        CHECK(rig.dmac.HasDeferredVif1() && rig.dmac.globals().pcr == 0x1u);
        rig.dmac.Write(DmacAddr::kPcr, 4, 0x2u);
        CHECK(!rig.dmac.HasDeferredVif1() && rig.dmac.globals().pcr == 0x2u);
        CHECK(QwIs(rig.vu1, 7, 0x300u, 0x301u, 0x302u, 0x303u));
    }
    // A VIF1 register write flushes first, then the new value lands.
    {
        Rig rig;
        BuildVif1Chain(rig.ram, 0x2000u, 8, 0x400u);
        rig.dmac.channel(1).tadr = 0x2000u;
        rig.dmac.Write(DmacAddr::kVif1, 4, kChainKick);
        rig.dmac.Write(DmacAddr::kVif1 + DmacRegOffset::kQwc, 4, 0x77u);
        CHECK(!rig.dmac.HasDeferredVif1());
        CHECK(rig.dmac.channel(1).qwc == 0x77u);
        CHECK(QwIs(rig.vu1, 8, 0x400u, 0x401u, 0x402u, 0x403u));
    }
    // The flush walks the kick-time snapshot, not a later member mutation.
    {
        Rig rig;
        BuildVif1Chain(rig.ram, 0x2000u, 9, 0x500u);
        BuildVif1Chain(rig.ram, 0x3000u, 10, 0x600u);
        rig.dmac.channel(1).tadr = 0x2000u;
        rig.dmac.Write(DmacAddr::kVif1, 4, kChainKick);
        rig.dmac.channel(1).tadr = 0x3000u;
        rig.dmac.FlushDeferredVif1();
        CHECK(QwIs(rig.vu1, 9, 0x500u, 0x501u, 0x502u, 0x503u) && QwIs(rig.vu1, 10, 0, 0, 0, 0));
        // An SPR kick is not an observation point.
        rig.dmac.channel(1).tadr = 0x3000u;
        rig.dmac.Write(DmacAddr::kVif1, 4, kChainKick);
        rig.dmac.Write(DmacAddr::kSprFrom, 4, DmacChcr::kStr);
        CHECK(rig.dmac.HasDeferredVif1());
        rig.dmac.ChaseVif1Tail();  // frame sync flushes
        CHECK(!rig.dmac.HasDeferredVif1() && QwIs(rig.vu1, 10, 0x600u, 0x601u, 0x602u, 0x603u));
    }
}

void TestSavestateSection() {
    Rig rig;
    for (int ch = 0; ch < 10; ++ch) {
        rig.dmac.channel(ch) = DmacChannelRegs{0x100u + ch, 0x200u + ch, 0x300u + ch, 0x400u + ch,
                                               0x500u + ch, 0x600u + ch, 0x700u + ch};
    }
    rig.dmac.globals() = DmacGlobalRegs{1, 2, 3, 4, 5, 6, 7, 8, 9};
    rig.dmac.handlers().Add(6, 0xA000u, 0x11u, 0x22u, 0x33u, -1);
    rig.dmac.handlers().Add(2, 0xB000u, 0x44u, 0x55u, 0x66u, -1);
    rig.dmac.handlers().SetActive(2, false);
    ps2r::savestate::SectionSpec spec = rig.dmac.SavestateSection();
    CHECK(spec.tag == ps2r::savestate::TAG_DMAC && spec.required && std::string(spec.owner) == "dmac");
    std::vector<uint8_t> payload;
    spec.serialize(payload);
    CHECK(payload.size() == 324 + 25 * 2);
    // Records are grouped by cause ascending: cause 2 first, cause-then-id order.
    int32_t cause0 = 0, id0 = 0;
    std::memcpy(&cause0, payload.data() + 324, 4);
    std::memcpy(&id0, payload.data() + 328, 4);
    CHECK(cause0 == 2 && id0 == 2);
    CHECK(payload[324 + 24] == 0 && payload[324 + 25 + 24] == 1);
    int32_t next_id = 0;
    std::memcpy(&next_id, payload.data() + 316, 4);
    CHECK(next_id == 3);

    Rig other;
    std::string err;
    ps2r::savestate::Blob blob;
    blob.tag = spec.tag;
    blob.payload = payload;
    CHECK(spec.validate(blob, &err));
    ps2r::savestate::SectionSpec other_spec = other.dmac.SavestateSection();
    CHECK(other_spec.restore(blob, &err));
    for (int ch = 0; ch < 10; ++ch) {
        CHECK(other.dmac.channel(ch).chcr == 0x100u + ch && other.dmac.channel(ch).sadr == 0x700u + ch);
    }
    CHECK(other.dmac.globals().stat == 2 && other.dmac.globals().enablew == 9);
    CHECK(other.dmac.handlers().Count() == 2 && other.dmac.handlers().NextId() == 3);
    CHECK(other.dmac.handlers().ForCause(6).size() == 1 && other.dmac.handlers().ForCause(6)[0].pc == 0xA000u);
    CHECK(!other.dmac.handlers().ForCause(2)[0].active);
    std::vector<uint8_t> again;
    other.dmac.SerializeSection(again);
    CHECK(again == payload);
    // Validation: short payload, ragged records, count mismatch.
    std::vector<uint8_t> bad(payload.begin(), payload.begin() + 323);
    CHECK(!Dmac::ValidateSection(bad, &err));
    bad = payload;
    bad.push_back(0);
    CHECK(!Dmac::ValidateSection(bad, &err));
    bad = payload;
    bad[320] = 5;
    CHECK(!Dmac::ValidateSection(bad, &err));
    // A restore drops a parked kick.
    BuildVif1Chain(other.ram, 0x2000u, 3, 0x900u);
    other.dmac.channel(1).tadr = 0x2000u;
    other.dmac.Write(DmacAddr::kVif1, 4, kChainKick);
    CHECK(other.dmac.HasDeferredVif1());
    CHECK(other_spec.restore(blob, &err));
    CHECK(!other.dmac.HasDeferredVif1());
}

}  // namespace

int main() {
    std::printf("sizeof(DmacChannelRegs) = %zu\n", sizeof(DmacChannelRegs));
    CHECK(sizeof(DmacChannelRegs) == 28);
    TestPageMap();
    TestGlobals();
    TestStatAcknowledgementAndMasks();
    TestWidening();
    TestCompleteAndMask();
    TestDispatch();
    TestSprCopy();
    TestDeferredObservationPoints();
    TestSavestateSection();
    if (g_fail) {
        std::printf("DmacRegs: %d FAIL\n", g_fail);
        return 1;
    }
    std::printf("DmacRegs: PASS\n");
    return 0;
}
