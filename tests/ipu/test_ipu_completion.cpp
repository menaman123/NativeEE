// Command-level vectors from EE User's Manual 8.2, 8.4, 8.5.3 and 8.6.
#include "engine/ipu/ipu_device.h"

#include <algorithm>
#include <cstdio>
#include <vector>

using ps2r::ipu::IpuDevice;

namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

std::vector<uint8_t> Drain(IpuDevice& ipu) {
    std::vector<uint8_t> bytes(ipu.OutputSize());
    CHECK(ipu.DrainOutputFifo(bytes.data(), bytes.size()) == bytes.size());
    return bytes;
}

uint16_t Word(const std::vector<uint8_t>& bytes, size_t pixel) {
    return static_cast<uint16_t>(bytes[pixel * 2] | (bytes[pixel * 2 + 1] << 8));
}

void FeedBits(IpuDevice& ipu, uint32_t bits) {
    const uint8_t data[] = {static_cast<uint8_t>(bits >> 24), static_cast<uint8_t>(bits >> 16),
                            static_cast<uint8_t>(bits >> 8), static_cast<uint8_t>(bits), 0, 0, 0, 0};
    ipu.FeedInputFifo(data, sizeof(data));
}

void TestCensusLifetime() {
    IpuDevice ipu;
    for (uint8_t code = 0; code < 16; ++code) CHECK(ipu.CommandCount(code) == 0);
    ipu.WriteCmd(0x20010000u);
    ipu.WriteCmd(0x28020000u);
    ipu.WriteCmd(0x11010000u);
    ipu.WriteCmd(0xf0000042u);
    ipu.WriteCmd(0x70000003u);  // input shortage, counted once despite MBC=3
    CHECK(ipu.CommandCount(2) == 2 && ipu.UnsupportedCommandCount(2) == 2);
    CHECK(ipu.LastUnsupportedCommand(2) == 0x28020000u);
    CHECK(ipu.UnsupportedVariantMask(2) == IpuDevice::kUnsupportedDecode);
    CHECK(ipu.UnsupportedCommandCount(1) == 1);  // partial IDEC is never advertised as complete
    CHECK(ipu.UnsupportedVariantMask(7) == IpuDevice::kUnsupportedInput);
    CHECK(ipu.UnsupportedVariantMask(15) == IpuDevice::kUnsupportedOpcode);
    CHECK(ipu.DivergentCommandCount() == 5);
    uint64_t unsupported_sum = 0;
    for (uint8_t code = 0; code < 16; ++code) unsupported_sum += ipu.UnsupportedCommandCount(code);
    CHECK(unsupported_sum == ipu.DivergentCommandCount());
    ipu.WriteCtrl(IpuDevice::kCtrlRst);
    ipu.WriteCmd(0x000000ffu);
    CHECK(ipu.BitPosition() == 127);  // BCLR has seven BP bits, not eight
    CHECK(ipu.CommandCount(0) == 1);
    CHECK(ipu.CommandCount(2) == 2 && ipu.UnsupportedCommandCount(2) == 2);
    CHECK(ipu.LastUnsupportedCommand(2) == 0x28020000u);
    CHECK(ipu.DivergentCommandCount() == 5);
    CHECK(ipu.CommandCount(255) == 0 && ipu.UnsupportedCommandCount(16) == 0);
    CHECK(ipu.LastUnsupportedCommand(16) == 0 && ipu.UnsupportedVariantMask(16) == 0);
    ipu.WriteCtrl(0xffffffffu);
    CHECK(ipu.ReadCtrl() == IpuDevice::kCtrlWritable);
    ipu.WriteCtrl(IpuDevice::kCtrlRst);
    CHECK(ipu.ReadCtrl() == 0 && ipu.ReadBp() == 0);
    CHECK(ipu.DivergentCommandCount() == 5);
}

void TestVdecTables() {
    struct Vector { uint32_t bits, control, command, expected; };
    // Positive/negative motion extrema, MPEG1-only stuffing, escape, and every
    // picture type. Invalid prefixes must return zero without consuming a code.
    const Vector vectors[] = {
        {0x80000000,0,0x30000000,0x00010001}, {0x60000000,0,0x30000000,0x00030002},
        {0x03000000,0,0x30000000,0x000b0021}, {0x01000000,0,0x30000000,0x000b0023},
        {0x01e00000,1u<<23,0x30000000,0x000b0022}, {0x01e00000,0,0x30000000,0},
        {0x80000000,1u<<24,0x34000000,0x00010001},
        {0x40000000,1u<<24,0x34000000,0x00020011},
        {0x10000000,2u<<24,0x34000000,0x0005001a},
        {0x08000000,3u<<24,0x34000000,0x00060016},
        {0x80000000,4u<<24,0x34000000,0x00010001},
        {0x80000000,0,0x34000000,0},
        {0x80000000,0,0x38000000,0x00010000}, {0x40000000,0,0x38000000,0x00030001},
        {0x60000000,0,0x38000000,0x0003ffff}, {0x03000000,0,0x38000000,0x000b0010},
        {0x03200000,0,0x38000000,0x000bfff0},
        {0x00000000,0,0x3c000000,0x00010000}, {0x80000000,0,0x3c000000,0x00020001},
        {0xc0000000,0,0x3c000000,0x0002ffff}, {0x00000000,0,0x30000000,0}
    };
    for (const auto& v : vectors) {
        IpuDevice ipu;
        FeedBits(ipu, v.bits);
        ipu.WriteCtrl(v.control);
        ipu.WriteCmd(v.command);
        CHECK(ipu.ReadCmd() == v.expected);
        CHECK(ipu.BitPosition() == (v.expected >> 16));
        CHECK(((ipu.ReadCtrl() >> 14) & 1u) == (v.expected == 0));
        CHECK(ipu.CommandCount(3) == 1 && ipu.UnsupportedCommandCount(3) == 0);
        const unsigned length = v.expected >> 16;
        CHECK(ipu.ReadTop() == (length ? v.bits << length : v.bits));
    }
    IpuDevice skipped;
    FeedBits(skipped, 0x08000000);  // four skipped bits then MBAI=1
    skipped.WriteCmd(0x30000004);
    CHECK(skipped.ReadCmd() == 0x00010001 && skipped.BitPosition() == 5);
    IpuDevice short_input;
    short_input.WriteCmd(0x30000000);
    CHECK(short_input.UnsupportedCommandCount(3) == 1);
}

void TestSetVqAndIndexedPack() {
    IpuDevice ipu;
    std::vector<uint8_t> table(32);
    // Palette i = red i*2. A1 is ignored by the distance calculation.
    for (unsigned i = 0; i < 16; ++i) {
        table[i * 2] = static_cast<uint8_t>(i * 2);
        table[i * 2 + 1] = 0x80;
    }
    ipu.FeedInputFifo(table.data(), 31);
    ipu.WriteCmd(0x60000000);
    CHECK(ipu.BitPosition() == 0 && ipu.VqClut()[15] == 0);
    CHECK(ipu.UnsupportedCommandCount(6) == 1);
    ipu.FeedInputFifo(table.data() + 31, 1);
    ipu.WriteCmd(0x60000000);
    CHECK(ipu.BitPosition() == 256 && ipu.VqClut()[15] == 0x801e);
    CHECK(ipu.CommandCount(6) == 2 && ipu.UnsupportedCommandCount(6) == 1);
    std::vector<uint8_t> pixels(1024);
    for (unsigned i = 0; i < 256; ++i) {
        pixels[i * 4] = static_cast<uint8_t>((i & 15) * 16);
        pixels[i * 4 + 3] = 0x40;
    }
    ipu.FeedInputFifo(pixels.data(), pixels.size());
    ipu.WriteCmd(0x80000001);
    const auto output = Drain(ipu);
    CHECK(output.size() == 128);
    if (output.size() == 128)
        for (unsigned i = 0; i < 128; ++i) CHECK(output[i] == (((i * 2) & 15) | (((i * 2 + 1) & 15) << 4)));
    CHECK(ipu.CommandCount(8) == 1 && ipu.UnsupportedCommandCount(8) == 0);
    CHECK((ipu.ReadCtrl() & 15) == 0);
    // Tables and thresholds survive a command reset; observer counts do too.
    ipu.WriteCmd(0x910001ff);
    ipu.WriteCtrl(IpuDevice::kCtrlRst);
    CHECK(ipu.VqClut()[15] == 0x801e && ipu.Threshold0() == 511 && ipu.Threshold1() == 256);
    CHECK(ipu.CommandCount(8) == 1);
    // Equidistant red=1 deterministically chooses the earlier palette entry.
    std::fill(pixels.begin(), pixels.end(), 0);
    pixels[0] = 8;
    ipu.FeedInputFifo(pixels.data(), pixels.size());
    ipu.WriteCmd(0x80000001);
    CHECK(Drain(ipu)[0] == 0);
}

void TestPackDitherAndAlpha() {
    std::vector<uint8_t> pixels(1024, 128);
    // Each pixel starts at RGB=128; vary only A8 across the first row.
    pixels[3] = 0;
    pixels[7] = 0x40;
    pixels[11] = 0x80;
    IpuDevice ipu;
    ipu.FeedInputFifo(pixels.data(), pixels.size());
    ipu.WriteCmd(0x8c000001);
    auto out = Drain(ipu);
    CHECK(out.size() == 512);
    // Manual's 4x4 ordered pattern crosses 128 -> five-bit 15/16.
    const unsigned levels[16] = {15,16,15,16, 16,15,16,15, 15,16,15,16, 16,15,16,15};
    if (out.size() == 512) {
        for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
            const unsigned level = levels[y * 4 + x];
            CHECK((Word(out, y * 16 + x) & 0x7fff) == level * 0x421);
        }
        CHECK((Word(out, 0) & 0x8000) == 0);
        CHECK((Word(out, 1) & 0x8000) != 0);
        CHECK((Word(out, 2) & 0x8000) == 0);
    }
    ipu.WriteCmd(0);  // BCLR
    ipu.FeedInputFifo(pixels.data(), pixels.size());
    ipu.WriteCmd(0x88000001);
    out = Drain(ipu);
    CHECK(Word(out, 0) == 0x4210 && Word(out, 1) == 0xc210);
}

void TestCscThresholdsAndChroma() {
    std::vector<uint8_t> mb(384, 128);
    std::fill(mb.begin(), mb.begin() + 256, 16);  // black
    IpuDevice ipu;
    ipu.WriteCmd(0x90000001);  // black strictly below TH0 -> fully transparent
    ipu.FeedInputFifo(mb.data(), mb.size());
    ipu.WriteCmd(0x70000001);
    auto out = Drain(ipu);
    CHECK(out.size() == 1024);
    for (uint8_t b : out) CHECK(b == 0);
    ipu.WriteCmd(0);
    std::fill(mb.begin(), mb.begin() + 256, 128);  // converts to RGB=130
    ipu.WriteCmd(0x90830000);  // 130 < TH1=131
    ipu.FeedInputFifo(mb.data(), mb.size());
    ipu.WriteCmd(0x78000001);
    out = Drain(ipu);
    CHECK(Word(out, 0) == 0xc210);  // alpha=64 -> A1=1
    ipu.WriteCmd(0);
    ipu.WriteCmd(0x90820000);  // equality is opaque, not translucent
    ipu.FeedInputFifo(mb.data(), mb.size());
    ipu.WriteCmd(0x78000001);
    CHECK(Word(Drain(ipu), 0) == 0x4210);
    ipu.WriteCmd(0);
    ipu.WriteCmd(0x90000100);  // ninth threshold bit: every component is below 256
    ipu.FeedInputFifo(mb.data(), mb.size());
    ipu.WriteCmd(0x70000001);
    for (uint8_t b : Drain(ipu)) CHECK(b == 0);
    // Chroma samples cover 2x2 pixels, not an entire row or an 8x8 Y block.
    ipu.WriteCmd(0);
    ipu.WriteCmd(0x90000000);
    mb[256] = 255;
    ipu.FeedInputFifo(mb.data(), mb.size());
    ipu.WriteCmd(0x70000001);
    out = Drain(ipu);
    CHECK(out[2] == 255 && out[6] == 255 && out[16 * 4 + 2] == 255);
    CHECK(out[2 * 4 + 2] == 130 && out[32 * 4 + 2] == 130);
}

void TestSetIqBitOffsetAndUnderflow() {
    IpuDevice ipu;
    // One prefix bit then 64 bytes of 0xff; command FB skips the prefix.
    std::vector<uint8_t> bytes(65, 255);
    bytes.front() = 0x7f;
    ipu.FeedInputFifo(bytes.data(), bytes.size());
    ipu.WriteCmd(0x50000001);
    CHECK(ipu.BitPosition() == 513);
    for (unsigned i = 0; i < 64; ++i) CHECK(ipu.IntraQuantMatrix()[i] == 255);
    CHECK(ipu.UnsupportedCommandCount(5) == 0);
    ipu.WriteCmd(0x58000000);
    CHECK(ipu.BitPosition() == 513 && ipu.UnsupportedCommandCount(5) == 1);
    for (unsigned i = 0; i < 64; ++i) CHECK(ipu.NonIntraQuantMatrix()[i] == 16);
}

void TestShortPackDoesNotInventOutput() {
    IpuDevice ipu;
    std::vector<uint8_t> bytes(1023, 128);
    ipu.FeedInputFifo(bytes.data(), bytes.size());
    ipu.WriteCmd(0x88000002);
    CHECK(ipu.OutputSize() == 0 && ipu.BitPosition() == 0);
    CHECK(ipu.UnsupportedCommandCount(8) == 1);
    const uint8_t last = 128;
    ipu.FeedInputFifo(&last, 1);
    ipu.WriteCmd(0x88000002);
    CHECK(ipu.OutputSize() == 512 && ipu.BitPosition() == 8192);
    CHECK(ipu.UnsupportedCommandCount(8) == 2);  // each issue counted only once
}

void TestIdecFallbackFormattingIsCounted() {
    // Six DC-only block fragments for the partial decoder, not a complete
    // hardware IDEC slice (MBT/MBAI are absent). Pin only output formatting
    // and the mandatory unsupported marker, not MPEG decode correctness.
    const uint8_t blocks[] = {0x94, 0xa5, 0x22, 0x20};
    IpuDevice rgb32, rgb16;
    rgb32.FeedInputFifo(blocks, sizeof(blocks));
    rgb16.FeedInputFifo(blocks, sizeof(blocks));
    rgb32.WriteCmd(0x10010000);
    rgb16.WriteCmd(0x18010000);
    const auto wide = Drain(rgb32);
    const auto narrow = Drain(rgb16);
    CHECK(wide.size() == 1024 && narrow.size() == 512);
    if (wide.size() == 1024 && narrow.size() == 512) {
        for (unsigned p = 0; p < 256; ++p) {
            const unsigned expected = (wide[p * 4] >> 3) | ((wide[p * 4 + 1] >> 3) << 5) |
                                      ((wide[p * 4 + 2] >> 3) << 10);
            CHECK(Word(narrow, p) == expected);
        }
    }
    CHECK(rgb32.CommandCount(1) == 1 && rgb32.UnsupportedCommandCount(1) == 1);
    CHECK(rgb16.CommandCount(1) == 1 && rgb16.UnsupportedCommandCount(1) == 1);
}

void TestOutputReadyHook() {
    IpuDevice ipu;
    std::vector<uint8_t> macroblock(384, 128);
    unsigned ready = 0;
    ipu.SetOutputReady([&] {
        ++ready;
        CHECK(ipu.OutputSize() == 512);
        CHECK(((ipu.ReadCtrl() >> 4) & 15) == 8);
        CHECK(ipu.CommandCount(7) == ready);
        CHECK(Drain(ipu).size() == 512);
        CHECK((ipu.ReadCtrl() & 0xf0) == 0);
    });
    ipu.WriteCmd(0x90000000);
    ipu.FeedInputFifo(macroblock.data(), macroblock.size());
    CHECK(ready == 0);
    ipu.WriteCmd(0x78000001);
    CHECK(ready == 1 && ipu.OutputSize() == 0);
    ipu.WriteCtrl(IpuDevice::kCtrlRst);
    ipu.WriteCmd(0);
    CHECK(ready == 1);
    ipu.FeedInputFifo(macroblock.data(), macroblock.size());
    ipu.WriteCmd(0x78000001);
    CHECK(ready == 2);  // guest reset preserves host wiring
    ipu.SetOutputReady({});
    ipu.WriteCmd(0);
    ipu.FeedInputFifo(macroblock.data(), macroblock.size());
    ipu.WriteCmd(0x78000001);
    CHECK(ready == 2 && ipu.OutputSize() == 512);
}
}  // namespace

int main() {
    TestCensusLifetime();
    TestVdecTables();
    TestSetVqAndIndexedPack();
    TestPackDitherAndAlpha();
    TestCscThresholdsAndChroma();
    TestSetIqBitOffsetAndUnderflow();
    TestShortPackDoesNotInventOutput();
    TestIdecFallbackFormattingIsCounted();
    TestOutputReadyHook();
    std::printf("test_ipu_completion: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
