// tests/ipu/test_ipu_device.cpp -- the IpuDevice register / FIFO / command
// contract: RST clears state; BCLR resets the bit
// pointer and input FIFO; BUSY never reads 1; IFC/OFC = min(bytes / 16, 8);
// SETIQ loads 64 bytes through the zigzag scan; SETTH latches; FDEC skips then
// peeks 32 bits; CSC of a constant-colour macroblock matches a reference computed
// from manual-derived colour vectors; the MmioHandler face decodes the four
// registers from the low address bits at any 64-byte-aligned base.
#include "engine/ipu/ipu_device.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/ipu/mpeg_decode.h"

using ps2r::ipu::IpuCommand;
using ps2r::ipu::IpuDevice;
using ps2r::ipu::MpegDecode;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

uint32_t Cmd(IpuCommand c, uint32_t low) { return (static_cast<uint32_t>(c) << 28) | low; }

// A test-chosen 64-byte-aligned window base (an arbitrary I/O-range value; the
// device itself never sees or names one).
constexpr uint32_t kBase = 0x10004000u;

std::vector<uint8_t> Bytes(size_t n, uint8_t first) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(first + i);
    return v;
}

void TestFreshState() {
    IpuDevice ipu;
    CHECK(ipu.ReadCtrl() == 0);
    CHECK(ipu.ReadCmd() == 0);
    CHECK(ipu.ReadBp() == 0);
    CHECK(ipu.ReadTop() == 0);
    CHECK(!ipu.IsBusy());
    CHECK(ipu.InputSize() == 0 && ipu.OutputSize() == 0 && !ipu.HasOutputData());
    CHECK(std::memcmp(ipu.IntraQuantMatrix(), MpegDecode::DefaultIntraQuantMatrix(), 64) == 0);
    for (int i = 0; i < 64; ++i) CHECK(ipu.NonIntraQuantMatrix()[i] == 16);
}

void TestRstClearsState() {
    IpuDevice ipu;
    const std::vector<uint8_t> in = Bytes(160, 1);
    ipu.FeedInputFifo(in.data(), in.size());
    ipu.WriteCmd(Cmd(IpuCommand::Fdec, 8));  // moves the bit pointer
    CHECK(ipu.BitPosition() == 8);
    CHECK(ipu.InputSize() == 160);
    CHECK((ipu.ReadCtrl() & 0xF) == 8);  // IFC saturates at 8 quadwords (160 / 16 = 10)

    // Writable fields latch; read-only status bits cannot be overwritten.
    ipu.WriteCtrl(0x3FFFFFFFu);
    CHECK(ipu.InputSize() == 160);
    CHECK(ipu.BitPosition() == 8);
    CHECK((ipu.ReadCtrl() & IpuDevice::kCtrlWritable) == IpuDevice::kCtrlWritable);

    ipu.WriteCtrl(IpuDevice::kCtrlRst);
    CHECK(ipu.InputSize() == 0);
    CHECK(ipu.OutputSize() == 0);
    CHECK(ipu.BitPosition() == 0);
    CHECK(!ipu.IsBusy());
    CHECK(ipu.ReadTop() == 0);
    // RST clears FIFO counts immediately.
    CHECK(ipu.ReadCtrl() == 0);
}

void TestBclr() {
    IpuDevice ipu;
    const std::vector<uint8_t> in = Bytes(32, 0x40);
    ipu.FeedInputFifo(in.data(), in.size());
    CHECK((ipu.ReadCtrl() & 0xF) == 2);
    ipu.WriteCmd(Cmd(IpuCommand::Bclr, 0x25));
    CHECK(ipu.InputSize() == 0);
    CHECK(ipu.BitPosition() == 0x25);
    CHECK((ipu.ReadBp() & 0x7F) == 0x25);
    CHECK(((ipu.ReadBp() >> 8) & 0xF) == 0);  // IFC cleared by BCLR
    CHECK(ipu.ReadCtrl() == 0);
    CHECK(!ipu.IsBusy());
    // BCLR leaves the output FIFO alone.
    IpuDevice ipu2;
    const std::vector<uint8_t> mb(IpuDevice::kCscInputBytesPerMb, 0x80);
    ipu2.FeedInputFifo(mb.data(), mb.size());
    ipu2.WriteCmd(Cmd(IpuCommand::Csc, 1));
    CHECK(ipu2.OutputSize() == 1024);
    ipu2.WriteCmd(Cmd(IpuCommand::Bclr, 0));
    CHECK(ipu2.OutputSize() == 1024);
    CHECK(ipu2.InputSize() == 0);
}

void TestBusyNeverSet() {
    IpuDevice ipu;
    const std::vector<uint8_t> in = Bytes(IpuDevice::kCscInputBytesPerMb * 2, 7);
    ipu.FeedInputFifo(in.data(), in.size());
    const uint32_t cmds[] = {
        Cmd(IpuCommand::Bclr, 0), Cmd(IpuCommand::Idec, 0), Cmd(IpuCommand::Bdec, 0),
        Cmd(IpuCommand::Vdec, 0), Cmd(IpuCommand::Fdec, 0), Cmd(IpuCommand::SetIq, 0),
        Cmd(IpuCommand::SetVq, 0), Cmd(IpuCommand::Csc, 1), Cmd(IpuCommand::Pack, 0),
        Cmd(IpuCommand::SetTh, 0), 0xF0000000u,
    };
    for (uint32_t c : cmds) {
        ipu.WriteCmd(c);
        CHECK(!ipu.IsBusy());
        CHECK((ipu.ReadCtrl() & IpuDevice::kCtrlBusy) == 0);
        CHECK((ipu.ReadCmd() & IpuDevice::kCtrlBusy) == 0);
    }
    // IDEC/BDEC and unknown opcodes remain unsupported; supplied-input commands
    // issued on this empty FIFO report starvation instead of false success.
    CHECK(ipu.DivergentCommandCount() == 8);
}

void TestIfcOfc() {
    IpuDevice ipu;
    // IFC: 0..8 quadwords, saturating.
    for (size_t bytes : {size_t{0}, size_t{15}, size_t{16}, size_t{47}, size_t{128}, size_t{129}, size_t{4096}}) {
        IpuDevice d;
        const std::vector<uint8_t> in(bytes, 0);
        if (bytes) d.FeedInputFifo(in.data(), in.size());
        const uint32_t expect = static_cast<uint32_t>(std::min<size_t>(bytes / 16, 8));
        CHECK((d.ReadCtrl() & 0xF) == expect);
        CHECK(((d.ReadBp() >> 8) & 0xF) == expect);
    }
    // OFC: one CSC macroblock = 1024 output bytes = 64 quadwords -> saturates at 8;
    // draining recomputes.
    const std::vector<uint8_t> mb(IpuDevice::kCscInputBytesPerMb, 0x80);
    ipu.FeedInputFifo(mb.data(), mb.size());
    ipu.WriteCmd(Cmd(IpuCommand::Csc, 1));
    CHECK(ipu.HasOutputData());
    CHECK(((ipu.ReadCtrl() >> 4) & 0xF) == 8);  // current count on the first read
    CHECK((ipu.ReadCtrl() & 0xF) == 0);  // consumed input is no longer counted
    uint8_t out[1024 - 40];
    CHECK(ipu.DrainOutputFifo(out, sizeof(out)) == sizeof(out));
    CHECK(ipu.OutputSize() == 40);
    CHECK(((ipu.ReadCtrl() >> 4) & 0xF) == 2);  // drain recomputed: 40 / 16 = 2
    CHECK(ipu.DrainOutputFifo(out, sizeof(out)) == 40);
    CHECK(!ipu.HasOutputData());
    CHECK(ipu.DrainOutputFifo(out, sizeof(out)) == 0);
}

void TestSetIqZigzag() {
    IpuDevice ipu;
    std::vector<uint8_t> matrix = Bytes(64, 100);  // scan-order values 100..163
    ipu.FeedInputFifo(matrix.data(), matrix.size());
    ipu.WriteCmd(Cmd(IpuCommand::SetIq, 0));
    const uint8_t* zz = MpegDecode::Zigzag();
    for (int i = 0; i < 64; ++i) CHECK(ipu.IntraQuantMatrix()[zz[i]] == matrix[i]);
    CHECK(ipu.BitPosition() == 64 * 8);
    // Non-intra select (bit 27) with a second table appended.
    std::vector<uint8_t> matrix2 = Bytes(64, 1);
    ipu.FeedInputFifo(matrix2.data(), matrix2.size());
    ipu.WriteCmd(Cmd(IpuCommand::SetIq, 1u << 27));
    for (int i = 0; i < 64; ++i) CHECK(ipu.NonIntraQuantMatrix()[zz[i]] == matrix2[i]);
    for (int i = 0; i < 64; ++i) CHECK(ipu.IntraQuantMatrix()[zz[i]] == matrix[i]);
    CHECK(ipu.BitPosition() == 128 * 8);
    // Fewer than 64 bytes past the cursor: no load, no advance.
    IpuDevice short_ipu;
    std::vector<uint8_t> few = Bytes(63, 9);
    short_ipu.FeedInputFifo(few.data(), few.size());
    short_ipu.WriteCmd(Cmd(IpuCommand::SetIq, 0));
    CHECK(std::memcmp(short_ipu.IntraQuantMatrix(), MpegDecode::DefaultIntraQuantMatrix(), 64) == 0);
    CHECK(short_ipu.BitPosition() == 0);
}

void TestSetTh() {
    IpuDevice ipu;
    ipu.WriteCmd(Cmd(IpuCommand::SetTh, (0x0A5u << 16) | 0x03Cu));
    CHECK(ipu.Threshold0() == 0x3C);
    CHECK(ipu.Threshold1() == 0xA5);
    CHECK(!ipu.IsBusy());
}

void TestFdec() {
    IpuDevice ipu;
    const uint8_t in[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    ipu.FeedInputFifo(in, sizeof(in));
    ipu.WriteCmd(Cmd(IpuCommand::Fdec, 0));
    CHECK(ipu.ReadCmd() == 0x12345678u);
    CHECK(ipu.BitPosition() == 0);
    CHECK(ipu.ReadTop() == 0x12345678u);
    // Skip 4 bits then peek: the window slides one nibble.
    ipu.WriteCmd(Cmd(IpuCommand::Fdec, 4));
    CHECK(ipu.ReadCmd() == 0x23456789u);
    CHECK(ipu.BitPosition() == 4);
    CHECK(ipu.ReadTop() == 0x23456789u);
    CHECK((ipu.ReadBp() & 0x7F) == 4);
    // Skip to where fewer than 32 bits remain (36 + 32 > 64): result 0, TOP 0,
    // the pointer still moves.
    ipu.WriteCmd(Cmd(IpuCommand::Fdec, 32));
    CHECK(ipu.ReadCmd() == 0);
    CHECK(ipu.ReadTop() == 0);
    CHECK(ipu.BitPosition() == 36);
    // Exactly 32 bits left peeks the tail word.
    IpuDevice tail;
    tail.FeedInputFifo(in, sizeof(in));
    tail.WriteCmd(Cmd(IpuCommand::Fdec, 32));
    CHECK(tail.ReadCmd() == 0x9ABCDEF0u);
}

void CheckConstantCsc(uint8_t y, uint8_t cb, uint8_t cr, uint32_t expect) {
    IpuDevice ipu;
    std::vector<uint8_t> mb(IpuDevice::kCscInputBytesPerMb);
    std::fill(mb.begin(), mb.begin() + 256, y);
    std::fill(mb.begin() + 256, mb.begin() + 320, cb);
    std::fill(mb.begin() + 320, mb.end(), cr);
    ipu.FeedInputFifo(mb.data(), mb.size());
    ipu.WriteCmd(Cmd(IpuCommand::Csc, 1));
    CHECK(ipu.OutputSize() == 256 * 4);
    CHECK(ipu.BitPosition() == IpuDevice::kCscInputBytesPerMb * 8);
    std::vector<uint8_t> out(256 * 4);
    CHECK(ipu.DrainOutputFifo(out.data(), out.size()) == out.size());
    int mismatches = 0;
    for (int i = 0; i < 256; ++i) {
        uint32_t px;
        std::memcpy(&px, out.data() + i * 4, 4);
        if (px != expect) ++mismatches;
    }
    CHECK(mismatches == 0);
    // Alpha byte (top byte, little-endian) is 0x80 and the class reports the same word.
    CHECK(out[3] == 0x80);
    if (mismatches) std::fprintf(stderr, "  csc y=%u cb=%u cr=%u expect=%08x\n", y, cb, cr, expect);
}

void TestCscConstantMacroblock() {
    // EE User's Manual 8.6.1 fixed-point coefficients and rounding.
    CheckConstantCsc(16, 128, 128, 0x80000000u);
    CheckConstantCsc(128, 128, 128, 0x80828282u);
    CheckConstantCsc(235, 128, 128, 0x80ffffffu);
    CheckConstantCsc(81, 90, 240, 0x800000feu);
    CheckConstantCsc(145, 54, 34, 0x8001ff00u);
    CheckConstantCsc(41, 240, 110, 0x80ff0100u);
    // Two complete macroblocks are consumed; missing input is counted once.
    IpuDevice ipu;
    std::vector<uint8_t> two(IpuDevice::kCscInputBytesPerMb * 2, 0x60);
    ipu.FeedInputFifo(two.data(), two.size());
    ipu.WriteCmd(Cmd(IpuCommand::Csc, 3));
    CHECK(ipu.OutputSize() == 2 * 1024);
    CHECK(ipu.BitPosition() == 2 * IpuDevice::kCscInputBytesPerMb * 8);
    CHECK(ipu.UnsupportedCommandCount(7) == 1);
    IpuDevice ipu16;
    ipu16.FeedInputFifo(two.data(), two.size());
    ipu16.WriteCmd(Cmd(IpuCommand::Csc, (1u << 27) | 1));
    CHECK(ipu16.OutputSize() == 512);
    CHECK(ipu16.UnsupportedCommandCount(7) == 0);
}

void TestMmioFace() {
    IpuDevice ipu;
    ::MmioHandler& h = ipu;
    const uint8_t in[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    ipu.FeedInputFifo(in, sizeof(in));

    // TOP at +0x30 through the bus, all widths.
    CHECK(h.Read(kBase + IpuDevice::kRegTop, 4) == 0xDEADBEEFull);
    CHECK(h.Read(kBase + IpuDevice::kRegTop, 2) == 0xBEEFull);
    CHECK(h.Read(kBase + IpuDevice::kRegTop + 2, 2) == 0xDEADull);
    CHECK(h.Read(kBase + IpuDevice::kRegTop + 3, 1) == 0xDEull);
    CHECK(h.Read(kBase + IpuDevice::kRegTop, 8) == 0xDEADBEEFull);  // upper word reads 0
    // A CMD write through the bus issues FDEC; the result reads back at +0x00.
    h.Write(kBase + IpuDevice::kRegCmd, 4, Cmd(IpuCommand::Fdec, 8));
    CHECK(h.Read(kBase + IpuDevice::kRegCmd, 4) == 0xADBEEF01ull);
    CHECK(h.Read(kBase + IpuDevice::kRegBp, 4) == 8);
    // 64-bit CMD write: the low word is the command, the high word is dropped.
    h.Write(kBase + IpuDevice::kRegCmd, 8, (0xFFFFFFFFull << 32) | Cmd(IpuCommand::Bclr, 3));
    CHECK(ipu.InputSize() == 0);
    CHECK(ipu.BitPosition() == 3);
    CHECK(ipu.UnhandledWriteCount() == 0);
    // CTRL RST through the bus.
    ipu.FeedInputFifo(in, sizeof(in));
    h.Write(kBase + IpuDevice::kRegCtrl, 4, IpuDevice::kCtrlRst);
    CHECK(ipu.InputSize() == 0 && ipu.BitPosition() == 0);
    // Writes to BP / TOP / unused offsets are dropped and counted.
    h.Write(kBase + IpuDevice::kRegBp, 4, 1);
    h.Write(kBase + IpuDevice::kRegTop, 4, 1);
    h.Write(kBase + 0x3C, 4, 1);
    CHECK(ipu.UnhandledWriteCount() == 3);
    CHECK(h.Read(kBase + 0x3C, 4) == 0);
    // The same device answers at any other aligned base: no absolute address inside.
    IpuDevice ipu2;
    ipu2.FeedInputFifo(in, sizeof(in));
    CHECK(static_cast<::MmioHandler&>(ipu2).Read(0x00000030u, 4) == 0xDEADBEEFull);
    CHECK(static_cast<::MmioHandler&>(ipu2).Read(0x7FFFFF80u + 0x30u, 4) == 0xDEADBEEFull);
    CHECK(static_cast<::MmioHandler&>(ipu2).Read(0x7FFFFF80u + 0x20u, 4) == 0);  // BP: bp 0, ifc 0
}

}  // namespace

int main() {
    TestFreshState();
    TestRstClearsState();
    TestBclr();
    TestBusyNeverSet();
    TestIfcOfc();
    TestSetIqZigzag();
    TestSetTh();
    TestFdec();
    TestCscConstantMacroblock();
    TestMmioFace();
    if (g_failures != 0) {
        std::fprintf(stderr, "test_ipu_device: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_ipu_device: PASS (fresh, rst, bclr, busy, ifc/ofc, setiq, setth, fdec, csc, mmio)\n");
    return 0;
}
