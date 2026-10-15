// Real DMAC + IPU + libipu adapter, without the host or a guest game.
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "engine/dmac/dmac.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/ipu/hle/dmac_channel_face_adapter.h"
#include "engine/ipu/hle/libipu_api.h"
#include "engine/ipu/ipu_device.h"
#include "engine/vif/vif.h"
#include "engine/vu/vu_unit.h"

namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

struct TestIpuDmaCoupling {
    MainMemory ram;
    Scratchpad spad;
    RegisterOpStream ops;
    GifRegs regs;
    GifParser parser{ops, regs};
    GifArbiter arbiter{parser};
    VuUnit vu0{0}, vu1{1, &arbiter};
    Vif vif{vu0, vu1, arbiter, regs};
    Dmac dmac{ram, spad, vif, arbiter};
    ps2r::ipu::IpuDevice ipu;
    ps2r::ipu::DmacChannelFaceAdapter face;
    ps2r::ipu::LibipuApi libipu{ipu, face};
    unsigned ready_calls = 0;

    TestIpuDmaCoupling()
        : face(dmac.channel(3), dmac.channel(4), [this](uint32_t ch) { dmac.StartChannel(ch); }) {
        dmac.SetIpuFeedInput([this](uint8_t* data, size_t size) { ipu.FeedInputFifo(data, size); });
        dmac.SetIpuDrainOutput([this](uint8_t* data, size_t size) { return ipu.DrainOutputFifo(data, size); });
        ipu.SetOutputReady([this] {
            ++ready_calls;
            if (dmac.channel(3).chcr & DmacChcr::kStr) dmac.StartChannel(3);
        });
    }
};

constexpr uint32_t kSource = 0x4000, kDestination = 0x6000;

void Macroblock(TestIpuDmaCoupling& rig, uint8_t y) {
    uint8_t* data = rig.ram.Pointer(kSource, 384);
    std::memset(data, y, 256);
    std::memset(data + 256, 128, 128);
}

void StartInput(TestIpuDmaCoupling& rig) {
    rig.dmac.Write(DmacAddr::kIpuTo + DmacRegOffset::kMadr, 4, kSource);
    rig.dmac.Write(DmacAddr::kIpuTo + DmacRegOffset::kQwc, 4, 24);
    rig.dmac.Write(DmacAddr::kIpuTo + DmacRegOffset::kChcr, 4, DmacChcr::kStr | DmacChcr::kDir);
}

void StartOutput(TestIpuDmaCoupling& rig, uint32_t qwc) {
    rig.dmac.Write(DmacAddr::kIpuFrom + DmacRegOffset::kMadr, 4, kDestination);
    rig.dmac.Write(DmacAddr::kIpuFrom + DmacRegOffset::kQwc, 4, qwc);
    rig.dmac.Write(DmacAddr::kIpuFrom + DmacRegOffset::kChcr, 4, DmacChcr::kStr);
}

bool AllBytes(const uint8_t* data, size_t size, uint8_t value) {
    return std::all_of(data, data + size, [value](uint8_t byte) { return byte == value; });
}

bool GreyRgb32(const uint8_t* data, uint8_t grey) {
    for (unsigned p = 0; p < 256; ++p) {
        if (data[p * 4] != grey || data[p * 4 + 1] != grey ||
            data[p * 4 + 2] != grey || data[p * 4 + 3] != 128) return false;
    }
    return true;
}

void TestInputOutputDirectionAndReady() {
    TestIpuDmaCoupling rig;
    Macroblock(rig, 128);
    uint8_t* dst = rig.ram.Pointer(kDestination, 1024);
    std::memset(dst, 0x5a, 1024);
    StartOutput(rig, 64);
    CHECK(rig.dmac.channel(3).qwc == 64 && (rig.dmac.channel(3).chcr & DmacChcr::kStr));
    CHECK((rig.dmac.globals().stat & DmacStat::Cis(3)) == 0);
    CHECK(AllBytes(dst, 1024, 0x5a) && rig.ipu.InputSize() == 0);
    StartInput(rig);
    CHECK(rig.ipu.InputSize() == 384);
    CHECK(rig.dmac.channel(4).qwc == 0 && rig.dmac.channel(4).madr == kSource + 384);
    CHECK((rig.dmac.channel(4).chcr & DmacChcr::kStr) == 0);
    CHECK(rig.dmac.channel(3).qwc == 64 && AllBytes(dst, 1024, 0x5a));
    rig.ipu.WriteCmd(0x70000001);
    CHECK(rig.ready_calls == 1 && rig.ipu.OutputSize() == 0);
    CHECK(rig.dmac.channel(3).qwc == 0 && rig.dmac.channel(3).madr == kDestination + 1024);
    CHECK((rig.dmac.channel(3).chcr & DmacChcr::kStr) == 0);
    CHECK((rig.dmac.globals().stat & DmacStat::Cis(3)) != 0);
    CHECK(GreyRgb32(dst, 130));
    CHECK(AllBytes(rig.ram.Pointer(kSource, 256), 256, 128));  // input DMA is a read
}

void TestPartialDrainCancelAndRearm() {
    TestIpuDmaCoupling rig;
    uint8_t* dst = rig.ram.Pointer(kDestination, 2048);
    std::memset(dst, 0x5a, 2048);
    StartOutput(rig, 128);
    Macroblock(rig, 128);
    StartInput(rig);
    rig.ipu.WriteCmd(0x70000001);
    CHECK(rig.dmac.channel(3).qwc == 64 && rig.dmac.channel(3).madr == kDestination + 1024);
    CHECK((rig.dmac.channel(3).chcr & DmacChcr::kStr) != 0);
    CHECK((rig.dmac.globals().stat & DmacStat::Cis(3)) == 0);
    CHECK(GreyRgb32(dst, 130) && AllBytes(dst + 1024, 1024, 0x5a));
    rig.dmac.Write(DmacAddr::kIpuFrom, 4, 0);
    Macroblock(rig, 235);
    StartInput(rig);
    rig.ipu.WriteCmd(0x70000001);
    CHECK(rig.ready_calls == 2 && rig.ipu.OutputSize() == 1024);
    CHECK(rig.dmac.channel(3).qwc == 64 && AllBytes(dst + 1024, 1024, 0x5a));
    // HLE adapter CHCR writes must kick the actual DMAC, including output
    // which is already available rather than awaiting the next command.
    rig.face.WriteChcr(3, DmacChcr::kStr);
    CHECK(rig.ipu.OutputSize() == 0 && rig.dmac.channel(3).qwc == 0);
    CHECK(rig.dmac.channel(3).madr == kDestination + 2048);
    CHECK(GreyRgb32(dst, 130) && GreyRgb32(dst + 1024, 255));
}

void TestHleResumePreservesDestinationWithoutOutput() {
    TestIpuDmaCoupling rig;
    std::memset(rig.ram.Pointer(kSource, 128), 0xaa, 128);
    uint8_t* dst = rig.ram.Pointer(kDestination, 16);
    std::memset(dst, 0x5a, 16);  // stands in for a plane already produced by HLE
    ps2r::ipu::LibipuDmaSnapshot snapshot;
    snapshot.ipu_from_madr = kDestination;
    snapshot.ipu_from_qwc = 1;
    snapshot.ipu_to_madr = kSource + 128;
    snapshot.ipu_to_chcr = DmacChcr::kDir;
    snapshot.ipu_bp = (8u << 8) | 5u;
    snapshot.ipu_ctrl = 0x07810107;  // deliberately different from BP
    rig.libipu.RestartDma(snapshot);
    CHECK(rig.ipu.InputSize() == 128 && rig.ipu.BitPosition() == 5);
    CHECK(rig.ipu.ReadTop() == 0x55555555u);
    CHECK(rig.dmac.channel(4).madr == kSource + 128 && rig.dmac.channel(4).qwc == 0);
    CHECK(rig.dmac.channel(3).qwc == 1 && (rig.dmac.channel(3).chcr & DmacChcr::kStr));
    CHECK(AllBytes(dst, 16, 0x5a) && rig.ready_calls == 0);
    CHECK(rig.ipu.DivergentCommandCount() == 0);
    ps2r::ipu::LibipuDmaSnapshot stopped;
    rig.libipu.StopDma(stopped);
    CHECK(stopped.ipu_from_qwc == 1 && (rig.dmac.channel(3).chcr & DmacChcr::kStr) == 0);
    CHECK(AllBytes(dst, 16, 0x5a));
}

void TestOutputResumeAtZero() {
    TestIpuDmaCoupling rig;
    Macroblock(rig, 128);
    StartInput(rig);
    rig.ipu.WriteCmd(0x70000001);
    CHECK(rig.ipu.OutputSize() == 1024);
    std::memset(rig.ram.Pointer(0, 1024), 0x5a, 1024);
    ps2r::ipu::LibipuDmaSnapshot snapshot;
    snapshot.ipu_from_qwc = 64;  // Physical RAM address zero is a valid destination.
    rig.libipu.RestartDma(snapshot);
    CHECK(GreyRgb32(rig.ram.Pointer(0, 1024), 130));
    CHECK(rig.ipu.OutputSize() == 0 && rig.dmac.channel(3).qwc == 0);
    CHECK(rig.dmac.channel(3).madr == 1024 && (rig.dmac.channel(3).chcr & DmacChcr::kStr) == 0);
    CHECK((rig.dmac.globals().stat & DmacStat::Cis(3)) != 0);
}

void TestBufferedInputResume(uint32_t source, uint32_t consumed_bits) {
    TestIpuDmaCoupling rig;
    uint8_t* src = rig.ram.Pointer(source, 384);
    for (unsigned i = 0; i < 384; ++i) src[i] = static_cast<uint8_t>(i * 37 + i / 16);
    rig.dmac.Write(DmacAddr::kIpuTo + DmacRegOffset::kMadr, 4, source);
    rig.dmac.Write(DmacAddr::kIpuTo + DmacRegOffset::kQwc, 4, 24);
    rig.dmac.Write(DmacAddr::kIpuTo, 4, DmacChcr::kStr | DmacChcr::kDir);
    for (uint32_t bits = consumed_bits; bits != 0;) {
        const uint32_t skip = std::min(bits, 63u);
        rig.ipu.WriteCmd(0x40000000u | skip);
        bits -= skip;
    }
    rig.ipu.WriteCmd(0x40000000);
    const uint32_t next = rig.ipu.ReadCmd();
    const uint32_t unread = 24 - consumed_bits / 128;
    const uint32_t reported = std::min(unread, 8u);
    const uint32_t excess = unread - reported;
    ps2r::ipu::LibipuDmaSnapshot snapshot;
    rig.libipu.StopDma(snapshot);
    CHECK(snapshot.ipu_to_madr == source + 384 - excess * 16);
    CHECK(snapshot.ipu_to_qwc == excess);
    CHECK(snapshot.ipu_bp == ((reported << 8) | (consumed_bits % 128)));
    CHECK(rig.libipu.DmaSnapshotErrorCount() == 0);
    // Resume into a fresh device using only a copy of the nine guest words.
    TestIpuDmaCoupling restored;
    std::memcpy(restored.ram.Pointer(source, 384), src, 384);
    restored.libipu.RestartDma(snapshot);
    CHECK(restored.ipu.InputSize() == unread * 16);
    CHECK(restored.ipu.BitPosition() == consumed_bits % 128);
    restored.ipu.WriteCmd(0x40000000);
    CHECK(restored.ipu.ReadCmd() == next);
    CHECK(restored.dmac.channel(4).madr == source + 384 && restored.dmac.channel(4).qwc == 0);
    CHECK(restored.libipu.DmaSnapshotErrorCount() == 0);
    // A second stop/restart must not accumulate extra rewind.
    restored.libipu.StopDma(snapshot);
    restored.libipu.RestartDma(snapshot);
    restored.ipu.WriteCmd(0x40000000);
    CHECK(restored.ipu.ReadCmd() == next && restored.ipu.InputSize() == unread * 16);
}

void TestSnapshotArithmeticGuards() {
    for (bool count_overflow : {false, true}) {
        TestIpuDmaCoupling rig;
        Macroblock(rig, 128);
        StartInput(rig);
        rig.ipu.WriteCmd(0x40000005);
        const uint32_t next = rig.ipu.ReadCmd();
        if (count_overflow) rig.dmac.channel(4).qwc = ps2r::ipu::LibipuApi::kMaxDmaQwc - 23;
        else rig.dmac.channel(4).madr = 383;  // insufficient room to rewind 24 qwords
        ps2r::ipu::LibipuDmaSnapshot snapshot;
        rig.libipu.StopDma(snapshot);
        CHECK(snapshot.ipu_to_qwc == ps2r::ipu::LibipuApi::kMaxDmaQwc + 1);
        CHECK(rig.libipu.DmaSnapshotErrorCount() == 1);
        const uint64_t commands = rig.ipu.CommandCount(0);
        rig.libipu.RestartDma(snapshot);
        CHECK(rig.libipu.DmaSnapshotErrorCount() == 2);
        CHECK(rig.ipu.CommandCount(0) == commands && rig.ipu.InputSize() == 384);
        CHECK(rig.ipu.BitPosition() == 5 && rig.ipu.ReadTop() == next);
        // Guest-supplied snapshots get the same guards before touching output.
        snapshot.ipu_to_qwc = count_overflow ? ps2r::ipu::LibipuApi::kMaxDmaQwc : 0;
        snapshot.ipu_to_madr = count_overflow ? kSource : 127;
        snapshot.ipu_from_madr = kDestination;
        snapshot.ipu_from_qwc = 1;
        rig.libipu.RestartDma(snapshot);
        CHECK(rig.libipu.DmaSnapshotErrorCount() == 3);
        CHECK(rig.dmac.channel(3).qwc == 0 && rig.ipu.CommandCount(0) == commands);
        rig.libipu.Init();
        CHECK(rig.libipu.DmaSnapshotErrorCount() == 3);
    }
}
}  // namespace

int main() {
    TestInputOutputDirectionAndReady();
    TestPartialDrainCancelAndRearm();
    TestHleResumePreservesDestinationWithoutOutput();
    TestOutputResumeAtZero();
    for (uint32_t bits : {0u, 5u, 127u, 128u, 133u, 2047u, 2048u, 2053u})
        TestBufferedInputResume(kSource, bits);
    TestBufferedInputResume(0, 5);  // exact address-zero rewind is valid, not underflow
    TestSnapshotArithmeticGuards();
    std::printf("test_ipu_dma_coupling: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
