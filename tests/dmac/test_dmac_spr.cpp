// Scratchpad DMA data, register, completion and rejection contracts.
// Synthetic addresses only; title reachability evidence belongs in the report.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/core/log/logger.h"
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
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_fail; \
} } while (0)

struct Rig {
    MainMemory ram;
    Scratchpad spad;
    RegisterOpStream ops;
    GifRegs gif_regs;
    GifParser parser{ops, gif_regs};
    GifArbiter arbiter{parser};
    VuUnit vu0{0};
    VuUnit vu1{1, &arbiter};
    Vif vif{vu0, vu1, arbiter, gif_regs};
    Dmac dmac{ram, spad, vif, arbiter};
};
constexpr int kFrom = static_cast<int>(DmacChannelId::SprFrom);
constexpr int kTo = static_cast<int>(DmacChannelId::SprTo);
constexpr uint32_t kChain = DmacChcr::kStr | (DmacChcr::kModChain << DmacChcr::kModShift);

void Fill(Rig& rig) {
    for (uint32_t i = 0; i < 0x20000; ++i)
        rig.ram.Data()[i] = static_cast<uint8_t>((i * 7u) ^ (i >> 8) ^ 0xA0u);
    for (uint32_t i = 0; i < Scratchpad::kSize; ++i)
        rig.spad.Data()[i] = static_cast<uint8_t>((i * 13u) ^ (i >> 8) ^ 0x30u);
}

void PutTag(Rig& rig, uint32_t at, uint32_t id, uint32_t qwc, uint32_t address, bool irq = false) {
    const uint32_t words[] = {(id << 28) | qwc | (irq ? 0x80000000u : 0), address, 0xA1B2C3D4u, 0x98765432u};
    std::memcpy(rig.ram.Pointer(at, 16), words, 16);
}

const DmacTransferCount& Count(const Rig& rig, int ch, uint32_t mode) {
    return ch == kFrom ? rig.dmac.TransferStats().spr_from[mode] : rig.dmac.TransferStats().spr_to[mode];
}

void CheckComplete(Rig& rig, int ch) {
    const auto& r = rig.dmac.channel(ch);
    CHECK(!(r.chcr & DmacChcr::kStr));
    CHECK(r.qwc == 0);
    CHECK(rig.dmac.globals().stat & DmacStat::Cis(ch));
    CHECK(!(rig.dmac.globals().stat & DmacStat::kBeis));
    CHECK(!rig.arbiter.hasPending() && rig.ops.Count() == 0);
}

void TestNormalDirectionsAndWrap() {
    // Whole images prove that only the destination bytes change, including two
    // wraps and a repeated destination overwrite when QWC exceeds scratch size.
    for (int ch : {kFrom, kTo}) {
        for (uint32_t qwc : {1u, 5u, 2051u}) {
            Rig rig;
            Fill(rig);
            std::vector<uint8_t> expected_ram(rig.ram.Data(), rig.ram.Data() + rig.ram.Size());
            std::vector<uint8_t> expected_spr(rig.spad.Data(), rig.spad.Data() + rig.spad.Size());
            for (uint32_t i = 0; i < qwc * 16; ++i) {
                const auto s = (0x3FF0u + i) % Scratchpad::kSize;
                if (ch == kFrom) expected_ram[0x8000 + i] = rig.spad.Data()[s];
                else expected_spr[s] = rig.ram.Data()[0x8000 + i];
            }
            auto& r = rig.dmac.channel(ch);
            r = {0, 0x8000800Fu, qwc | 0xABCD0000u, 0x700u, 0x111u, 0x222u, 0x70003FFFu};
            // DIR has no effect on channels 8/9. TTE/TIE do not alter normal mode.
            const uint32_t kick = DmacChcr::kStr | DmacChcr::kTte | DmacChcr::kTie |
                                  (ch == kFrom ? DmacChcr::kDir : 0u);
            rig.dmac.Write(ch == kFrom ? DmacAddr::kSprFrom : DmacAddr::kSprTo, 4, kick);
            CHECK(std::memcmp(rig.ram.Data(), expected_ram.data(), expected_ram.size()) == 0);
            CHECK(std::memcmp(rig.spad.Data(), expected_spr.data(), expected_spr.size()) == 0);
            CHECK(r.madr == 0x8000u + qwc * 16u);
            CHECK(r.sadr == (0x3FF0u + qwc * 16u) % Scratchpad::kSize);
            CHECK(r.tadr == 0x700u && r.asr0 == 0x111u && r.asr1 == 0x222u);
            CHECK(r.chcr == (kick & ~DmacChcr::kStr));
            CheckComplete(rig, ch);
            const auto& count = Count(rig, ch, 0);
            CHECK(count.starts == 1 && count.completed == 1 && count.quadwords == qwc && count.rejected == 0);
        }
    }
}

void TestMmioAndRamBoundaries() {
    for (int ch : {kFrom, kTo}) {
        Rig rig;
        Fill(rig);
        const uint32_t page = ch == kFrom ? DmacAddr::kSprFrom : DmacAddr::kSprTo;
        for (uint32_t address : {0u, MainMemory::kSize - 16}) {
            rig.dmac.Write(page + DmacRegOffset::kMadr, 4, address);
            rig.dmac.Write(page + DmacRegOffset::kSadr, 4, 0x20);
            rig.dmac.Write(page + DmacRegOffset::kQwc, 4, 1);
            rig.dmac.Write(page, 4, DmacChcr::kStr);
            CheckComplete(rig, ch);
            CHECK(rig.dmac.Read(page + DmacRegOffset::kMadr, 4) == address + 16u);
            CHECK(rig.dmac.Read(page + DmacRegOffset::kSadr, 4) == 0x30);
            CHECK(std::memcmp(rig.ram.Pointer(address, 16), rig.spad.Pointer(0x20, 16), 16) == 0);
        }
        CHECK(Count(rig, ch, 0).completed == 2);
    }
}

void TestRefChain() {
    Rig rig;
    Fill(rig);
    PutTag(rig, 0x1000, DmacTagId::kRef, 0x30, 0x4000);
    PutTag(rig, 0x1010, DmacTagId::kRefe, 0x30, 0x5000);
    auto& r = rig.dmac.channel(kTo);
    r = {kChain | DmacChcr::kDir, 0, 0, 0x1000, 0, 0, 0x3F00};
    rig.dmac.StartChannel(kTo);
    for (uint32_t i = 0; i < 0x600; ++i) {
        const uint32_t source = i < 0x300 ? 0x4000 + i : 0x5000 + i - 0x300;
        CHECK(rig.spad.Data()[(0x3F00 + i) & 0x3FFF] == rig.ram.Data()[source]);
    }
    CheckComplete(rig, kTo);
    CHECK(r.madr == 0x5300 && r.sadr == 0x500);
    CHECK((r.chcr & DmacChcr::kTagMask) == 0);  // REFE
    CHECK(Count(rig, kTo, 1).starts == 1 && Count(rig, kTo, 1).quadwords == 0x60);
}

void TestRoutingTagsAndTte() {
    for (bool tte : {false, true}) {
        Rig rig;
        Fill(rig);
        PutTag(rig, 0x1000, DmacTagId::kCnt, 1, 0xBAD00000);
        PutTag(rig, 0x1020, DmacTagId::kNext, 1, 0x1200);
        PutTag(rig, 0x1200, DmacTagId::kRef, 1, 0x6000);
        PutTag(rig, 0x1210, DmacTagId::kEnd, 1, 0xBAD00000);
        auto& r = rig.dmac.channel(kTo);
        r = {kChain | (tte ? DmacChcr::kTte : 0u), 0, 0, 0x1000, 0, 0, 0x3FF0};
        rig.dmac.StartChannel(kTo);
        std::vector<uint8_t> expected;
        const uint32_t tags[] = {0x1000, 0x1020, 0x1200, 0x1210};
        const uint32_t data[] = {0x1010, 0x1030, 0x6000, 0x1220};
        for (int i = 0; i < 4; ++i) {
            if (tte) expected.insert(expected.end(), rig.ram.Data() + tags[i], rig.ram.Data() + tags[i] + 16);
            expected.insert(expected.end(), rig.ram.Data() + data[i], rig.ram.Data() + data[i] + 16);
        }
        for (size_t i = 0; i < expected.size(); ++i)
            CHECK(rig.spad.Data()[(0x3FF0 + i) & 0x3FFF] == expected[i]);
        CHECK(r.sadr == (0x3FF0u + expected.size()) % Scratchpad::kSize);
        CHECK(r.madr == 0x1230 && (r.chcr & DmacChcr::kTagMask) == 0x70000000u);
        CHECK(Count(rig, kTo, 1).quadwords == (tte ? 8u : 4u));
        CheckComplete(rig, kTo);
    }
}

void TestTagInterruptAndResume() {
    for (bool tie : {false, true}) {
        Rig rig;
        Fill(rig);
        PutTag(rig, 0x1000, DmacTagId::kRef, 2, 0x6000, true);
        PutTag(rig, 0x1010, DmacTagId::kEnd, 0, 0);
        auto& r = rig.dmac.channel(kTo);
        r = {kChain | (tie ? DmacChcr::kTie : 0u), 0, 0, 0x1000, 0, 0, 0x100};
        rig.dmac.StartChannel(kTo);
        CheckComplete(rig, kTo);
        CHECK(std::memcmp(rig.spad.Pointer(0x100, 32), rig.ram.Pointer(0x6000, 32), 32) == 0);
        CHECK(r.sadr == 0x120);
        if (tie) {
            CHECK(r.tadr == 0x1010);  // IRQ keeps the next tag for restart.
            CHECK((r.chcr & DmacChcr::kTagMask) == 0xB0000000u);
            r.chcr |= DmacChcr::kStr;
            rig.dmac.StartChannel(kTo);
            CHECK(Count(rig, kTo, 1).completed == 2);
        }
        CHECK((r.chcr & DmacChcr::kTagMask) == 0x70000000u);
    }
    // An in-progress packet has data before the next tag. A terminating TAG
    // ends that packet without fetching the (deliberately invalid) TADR.
    for (uint32_t id : {DmacTagId::kRef, DmacTagId::kRefe, DmacTagId::kEnd}) {
        Rig rig;
        Fill(rig);
        PutTag(rig, 0x1000, DmacTagId::kEnd, 0, 0);
        auto& r = rig.dmac.channel(kTo);
        r = {kChain | (id << 28), 0x5000, 2, id == DmacTagId::kRef ? 0x1000u : 0x4000000u, 0, 0, 0};
        rig.dmac.StartChannel(kTo);
        CHECK(std::memcmp(rig.spad.Data(), rig.ram.Pointer(0x5000, 32), 32) == 0);
        CheckComplete(rig, kTo);
        CHECK(Count(rig, kTo, 1).quadwords == 2);
    }
}

void TestUnsupportedModes() {
    for (int ch : {kFrom, kTo}) {
        for (uint32_t mode : {1u, 2u, 3u}) {
            if (ch == kTo && mode == 1) continue;
            Rig rig;
            Fill(rig);
            auto& r = rig.dmac.channel(ch);
            r = {DmacChcr::kStr | (mode << 2), 0x5000, 2, 0x1000, 0, 0, 0x200};
            const auto before = r;
            const std::vector<uint8_t> ram(rig.ram.Data(), rig.ram.Data() + rig.ram.Size());
            const std::vector<uint8_t> spr(rig.spad.Data(), rig.spad.Data() + rig.spad.Size());
            rig.dmac.Write(ch == kFrom ? DmacAddr::kSprFrom : DmacAddr::kSprTo, 4, r.chcr);
            CHECK(std::memcmp(&r, &before, sizeof(r)) == 0);
            CHECK(std::memcmp(rig.ram.Data(), ram.data(), ram.size()) == 0);
            CHECK(std::memcmp(rig.spad.Data(), spr.data(), spr.size()) == 0);
            CHECK(rig.dmac.globals().stat == 0);
            const auto& count = Count(rig, ch, mode);
            CHECK(count.starts == 1 && count.rejected == 1 && count.unsupported == 1);
            CHECK(count.completed == 0 && count.faults == 0 && count.quadwords == 0);
        }
    }
    // Zero normal QWC is explicitly forbidden by the manual, not a 64K copy.
    Rig rig;
    auto& r = rig.dmac.channel(kFrom);
    r = {DmacChcr::kStr, 0x5000, 0, 0, 0, 0, 0};
    rig.dmac.StartChannel(kFrom);
    CHECK(r.chcr & DmacChcr::kStr);
    CHECK(rig.dmac.globals().stat == 0 && Count(rig, kFrom, 0).unsupported == 1);
    for (uint32_t ctrl : {4u, 0x20u}) {
        r.qwc = 1;
        rig.dmac.globals().ctrl = ctrl;
        rig.dmac.StartChannel(kFrom);
        CHECK(r.chcr & DmacChcr::kStr);
    }
    rig.dmac.globals().ctrl = 0;
    r.madr = 0x11004000;  // legal VU memory, outside this implementation
    rig.dmac.StartChannel(kFrom);
    CHECK(r.chcr & DmacChcr::kStr);
    CHECK(Count(rig, kFrom, 0).unsupported == 4 && Count(rig, kFrom, 0).faults == 0);
}

void TestChainRejections() {
    // Ch9 has no REFS stall operation or CALL/RET address stack. The documented
    // undefined-tag termination raises CIS without transferring its payload.
    for (uint32_t id : {DmacTagId::kRefs, DmacTagId::kCall, DmacTagId::kRet}) {
        Rig rig;
        Fill(rig);
        PutTag(rig, 0x1000, id, 2, 0x5000);
        auto& r = rig.dmac.channel(kTo);
        r = {kChain, 0, 0, 0x1000, 0, 0, 0};
        const uint8_t first = rig.spad.Data()[0];
        rig.dmac.StartChannel(kTo);
        CHECK(!(r.chcr & DmacChcr::kStr));
        CHECK(r.tadr == 0x1000 && (r.chcr & DmacChcr::kTagMask) == id << 28);
        CHECK(rig.spad.Data()[0] == first);
        CHECK(rig.dmac.globals().stat == DmacStat::Cis(kTo));
        CHECK(Count(rig, kTo, 1).faults == 1 && Count(rig, kTo, 1).completed == 0);
    }
    Rig rig;
    PutTag(rig, 0x1000, DmacTagId::kNext, 0, 0x1000);
    auto& r = rig.dmac.channel(kTo);
    r = {kChain, 0, 0, 0x1000, 0, 0, 0};
    rig.dmac.StartChannel(kTo);  // self-loop hits a bounded walk limit
    CHECK(r.chcr & DmacChcr::kStr);
    CHECK(rig.dmac.globals().stat == 0 && Count(rig, kTo, 1).unsupported == 1);
    CHECK(Count(rig, kTo, 1).quadwords == 0);
    r = {kChain, 0, 0, 0x80001000, 0, 0, 0};
    rig.dmac.StartChannel(kTo);  // scratchpad tag source
    CHECK(Count(rig, kTo, 1).unsupported == 2);
    PutTag(rig, 0x1000, DmacTagId::kRef, 1, 0x80001000);
    r = {kChain, 0, 0, 0x1000, 0, 0, 0};
    rig.dmac.StartChannel(kTo);  // scratchpad payload source
    CHECK(Count(rig, kTo, 1).unsupported == 3);
    PutTag(rig, 0x1000, DmacTagId::kEnd, 1u << 26, 0);  // reserved PCE
    r = {kChain, 0, 0, 0x1000, 0, 0, 0};
    rig.dmac.StartChannel(kTo);
    CHECK(Count(rig, kTo, 1).unsupported == 4);
}

void TestDroppedControlCensus() {
    Rig rig;
    Fill(rig);
    // Default DMA enable and non-SPR stall-source selection do not request the
    // missing SPR modes. Count every special request, even without any start.
    rig.dmac.Write(DmacAddr::kCtrl, 4, 1);
    rig.dmac.Write(DmacAddr::kCtrl, 4, 0x11);
    CHECK(rig.dmac.TransferStats().unsupported_spr_ctrl_writes == 0);
    for (uint32_t ctrl : {0x5u, 0x9u, 0xDu, 0x21u}) rig.dmac.Write(DmacAddr::kCtrl, 4, ctrl);
    CHECK(rig.dmac.Read(DmacAddr::kCtrl, 4) == 0);
    CHECK(rig.dmac.TransferStats().unsupported_spr_ctrl_writes == 4);
    rig.dmac.channel(kFrom) = {DmacChcr::kStr, 0x6000, 1, 0, 0, 0, 0};
    rig.dmac.StartChannel(kFrom);
    CHECK(Count(rig, kFrom, 0).completed == 1);
    // An ordinary copy under the retained CTRL does not erase the rejected setup.
    CHECK(rig.dmac.TransferStats().unsupported_spr_ctrl_writes == 4);
    std::vector<uint8_t> state;
    rig.dmac.SerializeSection(state);
    std::string err;
    CHECK(rig.dmac.RestoreSection(state, &err));
    CHECK(rig.dmac.TransferStats().unsupported_spr_ctrl_writes == 4);
}

void TestBusErrors() {
    for (int ch : {kFrom, kTo}) {
        Rig rig;
        Fill(rig);
        auto& r = rig.dmac.channel(ch);
        r = {DmacChcr::kStr, MainMemory::kSize - 16, 2, 0, 0, 0, 0x3FF0};
        int raised = 0;
        rig.dmac.SetInterruptRaise([&] { ++raised; });
        rig.dmac.StartChannel(ch);
        CHECK(r.madr == MainMemory::kSize && r.sadr == 0 && r.qwc == 1);
        CHECK(!(r.chcr & DmacChcr::kStr));
        CHECK(rig.dmac.globals().stat == (DmacStat::kBeis | DmacStat::Cis(ch)));
        CHECK(raised == 1 && rig.dmac.CheckInterrupt());  // BEIS is unmaskable
        CHECK(Count(rig, ch, 0).faults == 1 && Count(rig, ch, 0).quadwords == 1);
        CHECK(Count(rig, ch, 0).completed == 0 && Count(rig, ch, 0).rejected == 1);
        // A dispatch cannot acknowledge BEIS. Software clears it through D_STAT
        // before retrying; QWC still describes the part that did not transfer.
        CpuContext ctx{};
        rig.dmac.DispatchInterrupt(ctx);
        CHECK(rig.dmac.CheckInterrupt());
        rig.dmac.Write(DmacAddr::kStat, 4, 0);
        CHECK(rig.dmac.CheckInterrupt());
        rig.dmac.Write(DmacAddr::kStat, 4, DmacStat::kBeis | DmacStat::Cis(ch));
        CHECK(!rig.dmac.CheckInterrupt() && rig.dmac.globals().stat == 0);
        rig.dmac.Write(DmacAddr::kStat, 4, DmacStat::Cim(ch));
        r.madr = 0x6000;
        r.chcr |= DmacChcr::kStr;
        rig.dmac.StartChannel(ch);
        CheckComplete(rig, ch);
        CHECK(r.madr == 0x6010 && r.sadr == 0x10);
        CHECK(raised == 2 && rig.dmac.CheckInterrupt());
        CHECK(Count(rig, ch, 0).starts == 2 && Count(rig, ch, 0).completed == 1);
        CHECK(Count(rig, ch, 0).faults == 1 && Count(rig, ch, 0).quadwords == 2);
        rig.dmac.Write(DmacAddr::kStat, 4, DmacStat::Cis(ch));
        CHECK(!rig.dmac.CheckInterrupt());
    }
    Rig rig;
    rig.dmac.channel(kTo) = {kChain, 0, 0, MainMemory::kSize, 0, 0, 0};
    rig.dmac.StartChannel(kTo);
    CHECK(rig.dmac.globals().stat & DmacStat::kBeis);
    CHECK(Count(rig, kTo, 1).faults == 1 && Count(rig, kTo, 1).quadwords == 0);
}

void TestInterruptAndDeferredIsolation() {
    Rig rig;
    Fill(rig);
    PutTag(rig, 0x1000, DmacTagId::kEnd, 0, 0);
    rig.dmac.channel(1).tadr = 0x1000;
    rig.dmac.Write(DmacAddr::kVif1, 4, kChain);
    int raised = 0;
    rig.dmac.SetInterruptRaise([&] {
        ++raised;
        CHECK(std::memcmp(rig.ram.Pointer(0x6000, 32), rig.spad.Pointer(0x100, 32), 32) == 0);
    });
    for (int ch : {kFrom, kTo}) {
        rig.dmac.globals().stat = DmacStat::Cim(ch);
        rig.dmac.channel(ch) = {DmacChcr::kStr, 0x6000, 2, 0, 0, 0, 0x100};
        rig.dmac.StartChannel(ch);
        CHECK(rig.dmac.HasDeferredVif1() && rig.dmac.CheckInterrupt());
        CHECK(!(rig.dmac.globals().stat & DmacStat::Cis(1)));
        CpuContext ctx{};
        rig.dmac.DispatchInterrupt(ctx);
        CHECK(!rig.dmac.CheckInterrupt() && !(rig.dmac.globals().stat & DmacStat::Cis(ch)));
    }
    CHECK(raised == 2);
    rig.dmac.SetInterruptRaise({});
    rig.dmac.FlushDeferredVif1();
    CHECK(!rig.dmac.HasDeferredVif1() && (rig.dmac.globals().stat & DmacStat::Cis(1)));
}

void TestCensusSavestateIsolation() {
    Rig rig;
    Fill(rig);
    std::vector<uint8_t> before;
    rig.dmac.SerializeSection(before);
    rig.dmac.channel(kTo) = {DmacChcr::kStr, 0x5000, 2, 0, 0, 0, 0};
    rig.dmac.StartChannel(kTo);
    std::string err;
    CHECK(rig.dmac.RestoreSection(before, &err));
    std::vector<uint8_t> after;
    rig.dmac.SerializeSection(after);
    CHECK(after == before && after.size() == Dmac::kSectionFixedBytes);
    CHECK(Count(rig, kTo, 0).starts == 1 && Count(rig, kTo, 0).completed == 1);
    Rig other;
    CHECK(other.dmac.RestoreSection(after, &err));
    CHECK(Count(other, kTo, 0).starts == 0);
}

void TestBoundedRejectionLog() {
    auto& logger = ps2r::log::Logger::Instance();
    const auto saved_level = logger.GetLevel();
    const auto before = logger.LinesWritten();
    logger.SetLevel(ps2r::log::Level::Error);
    Rig rig;
    for (int ch : {kFrom, kTo}) {
        for (int repeat = 0; repeat < 3; ++repeat) {
            rig.dmac.channel(ch) = {DmacChcr::kStr | (2u << 2), 0x5000, 1, 0, 0, 0, 0};
            rig.dmac.StartChannel(ch);
        }
        CHECK(Count(rig, ch, 2).unsupported == 3);  // census is never rate-limited
    }
    CHECK(logger.LinesWritten() == before + 2);  // distinct directions, same reason
    for (int repeat = 0; repeat < 3; ++repeat) {
        rig.dmac.channel(kTo) = {DmacChcr::kStr, MainMemory::kSize, 1, 0, 0, 0, 0};
        rig.dmac.StartChannel(kTo);
    }
    CHECK(logger.LinesWritten() == before + 3);  // faults also log once
    CHECK(Count(rig, kTo, 0).faults == 3);
    rig.dmac.channel(kTo) = {DmacChcr::kStr, 0x5000, 0, 0, 0, 0, 0};
    rig.dmac.StartChannel(kTo);
    CHECK(logger.LinesWritten() == before + 4);  // another reason in the same mode
    CHECK(Count(rig, kTo, 0).rejected == 4);
    for (int repeat = 0; repeat < 3; ++repeat) rig.dmac.Write(DmacAddr::kCtrl, 4, 0x9);
    CHECK(logger.LinesWritten() == before + 5);  // ignored setup diagnostics are bounded too
    CHECK(rig.dmac.TransferStats().unsupported_spr_ctrl_writes == 3);
    logger.SetLevel(saved_level);
}
}  // namespace

int main() {
    TestNormalDirectionsAndWrap();
    TestMmioAndRamBoundaries();
    TestRefChain();
    TestRoutingTagsAndTte();
    TestTagInterruptAndResume();
    TestUnsupportedModes();
    TestChainRejections();
    TestDroppedControlCensus();
    TestBusErrors();
    TestInterruptAndDeferredIsolation();
    TestCensusSavestateIsolation();
    TestBoundedRejectionLog();
    std::printf("DmacSpr: %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
