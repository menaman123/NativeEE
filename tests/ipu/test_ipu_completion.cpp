// Command-level vectors from EE User's Manual 8.2, 8.4, 8.5.3 and 8.6, and BDEC macroblocks
// built from ISO/IEC 13818-2 7.2 bit patterns (Tables B-9, B-12..B-14).
#include "engine/ipu/ipu_device.h"

#include <algorithm>
#include <cstdio>
#include <string>
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
    // Two BDECs on an empty FIFO: both counted, neither unsupported -- they wait for
    // input (the second supersedes the first); the IDEC that follows supersedes it.
    ipu.WriteCmd(0x20010000u);
    CHECK(ipu.IsBusy() && ipu.PendingCommand() == 0x20010000u);
    ipu.WriteCmd(0x28020000u);
    CHECK(ipu.IsBusy() && ipu.PendingCommand() == 0x28020000u);
    ipu.WriteCmd(0x11010000u);
    CHECK(!ipu.IsBusy());
    ipu.WriteCmd(0xf0000042u);
    ipu.WriteCmd(0x70000003u);  // input shortage: CSC waits for its macroblocks (not divergent)
    CHECK(ipu.IsBusy() && ipu.PendingCommand() == 0x70000003u);
    CHECK(ipu.CommandCount(2) == 2 && ipu.UnsupportedCommandCount(2) == 0);
    CHECK(ipu.LastUnsupportedCommand(2) == 0 && ipu.UnsupportedVariantMask(2) == 0);
    CHECK(ipu.UnsupportedCommandCount(1) == 1);  // partial IDEC is never advertised as complete
    CHECK(ipu.UnsupportedVariantMask(7) == 0 && ipu.UnsupportedCommandCount(7) == 0);
    CHECK(ipu.UnsupportedVariantMask(15) == IpuDevice::kUnsupportedOpcode);
    CHECK(ipu.DivergentCommandCount() == 2);
    uint64_t unsupported_sum = 0;
    for (uint8_t code = 0; code < 16; ++code) unsupported_sum += ipu.UnsupportedCommandCount(code);
    CHECK(unsupported_sum == ipu.DivergentCommandCount());
    ipu.WriteCtrl(IpuDevice::kCtrlRst);
    ipu.WriteCmd(0x000000ffu);
    CHECK(ipu.BitPosition() == 127);  // BCLR has seven BP bits, not eight
    CHECK(ipu.CommandCount(0) == 1);
    CHECK(ipu.CommandCount(2) == 2 && ipu.UnsupportedCommandCount(2) == 0);
    CHECK(ipu.DivergentCommandCount() == 2);
    CHECK(ipu.CommandCount(255) == 0 && ipu.UnsupportedCommandCount(16) == 0);
    CHECK(ipu.LastUnsupportedCommand(16) == 0 && ipu.UnsupportedVariantMask(16) == 0);
    ipu.WriteCtrl(0xffffffffu);
    CHECK(ipu.ReadCtrl() == IpuDevice::kCtrlWritable);
    ipu.WriteCtrl(IpuDevice::kCtrlRst);
    CHECK(ipu.ReadCtrl() == 0 && ipu.ReadBp() == 0);
    CHECK(ipu.DivergentCommandCount() == 2);
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
    // Short input: VDEC waits (BUSY, not starvation) and completes when the bits arrive.
    IpuDevice short_input;
    short_input.WriteCmd(0x30000000);
    CHECK(short_input.IsBusy() && short_input.PendingCommand() == 0x30000000);
    CHECK(short_input.UnsupportedCommandCount(3) == 0 && short_input.CommandCount(3) == 1);
    FeedBits(short_input, 0x80000000);  // MBAI code '1' arrives: the pending VDEC completes
    CHECK(!short_input.IsBusy() && short_input.ReadCmd() == 0x00010001 && short_input.BitPosition() == 1);
    CHECK(short_input.PendingCompletions() == 1 && short_input.CommandCount(3) == 1);
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
    // 31 of 32 bytes: the command waits for the last one, loading nothing yet.
    CHECK(ipu.BitPosition() == 0 && ipu.VqClut()[15] == 0);
    CHECK(ipu.IsBusy() && ipu.UnsupportedCommandCount(6) == 0);
    ipu.FeedInputFifo(table.data() + 31, 1);
    CHECK(!ipu.IsBusy() && ipu.BitPosition() == 256 && ipu.VqClut()[15] == 0x801e);
    CHECK(ipu.CommandCount(6) == 1 && ipu.UnsupportedCommandCount(6) == 0 && ipu.PendingCompletions() == 1);
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
    // Underflow: the non-intra load waits for its 64 bytes, the matrix untouched meanwhile.
    ipu.WriteCmd(0x58000000);
    CHECK(ipu.BitPosition() == 513 && ipu.IsBusy() && ipu.UnsupportedCommandCount(5) == 0);
    for (unsigned i = 0; i < 64; ++i) CHECK(ipu.NonIntraQuantMatrix()[i] == 16);
    std::vector<uint8_t> more(64, 3);
    ipu.FeedInputFifo(more.data(), more.size());
    CHECK(!ipu.IsBusy() && ipu.BitPosition() == 513 + 512);
    // Read from bit 513: seven leftover 1-bits then the 0x03 bytes, each shifted one bit
    // left -> one 0xfe and sixty-three 0x06 (zigzag-scattered).
    unsigned fe = 0, six = 0;
    for (unsigned i = 0; i < 64; ++i) {
        fe += ipu.NonIntraQuantMatrix()[i] == 0xfe;
        six += ipu.NonIntraQuantMatrix()[i] == 0x06;
    }
    CHECK(fe == 1 && six == 63);
    // RESET drops a pending command.
    IpuDevice dropped;
    dropped.WriteCmd(0x50000000);
    CHECK(dropped.IsBusy());
    dropped.WriteCtrl(IpuDevice::kCtrlRst);
    CHECK(!dropped.IsBusy() && dropped.PendingCommand() == 0);
}

void TestShortPackWaitsForInput() {
    // 1023 of the 1024 bytes of the first macroblock: PACK (MBC=2) waits -- no output,
    // the bit pointer parked, the bytes already buffered counted as consumed (room for
    // the rest). The last byte completes macroblock 1 (512 RGB16 bytes); macroblock 2
    // keeps the command pending with MBC=1 until its 1024 bytes arrive.
    IpuDevice ipu;
    std::vector<uint8_t> bytes(1023, 128);
    ipu.FeedInputFifo(bytes.data(), bytes.size());
    ipu.WriteCmd(0x88000002);
    CHECK(ipu.OutputSize() == 0 && ipu.BitPosition() == 0);
    CHECK(ipu.IsBusy() && ipu.PendingCommand() == 0x88000002 && ipu.UnsupportedCommandCount(8) == 0);
    CHECK((ipu.ReadCtrl() & 15) == 0);  // IFC: the buffered 63 quadwords are the converter's
    CHECK(ipu.InputRoomBytes() == IpuDevice::kInputFifoBytes - 15);  // the partial quadword still held
    const uint8_t last = 128;
    ipu.FeedInputFifo(&last, 1);
    CHECK(ipu.OutputSize() == 512 && ipu.BitPosition() == 8192);
    CHECK(ipu.IsBusy() && ipu.PendingCommand() == 0x88000001);
    bytes.push_back(128);
    ipu.FeedInputFifo(bytes.data(), bytes.size());
    CHECK(!ipu.IsBusy() && ipu.OutputSize() == 1024 && ipu.BitPosition() == 2 * 8192);
    CHECK(ipu.UnsupportedCommandCount(8) == 0 && ipu.PendingCompletions() == 1);
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

// Bytes from '0'/'1' characters (MSB first), zero-padded to `min_bytes` and to a byte.
std::vector<uint8_t> Bits(const char* s, size_t min_bytes = 0) {
    std::vector<uint8_t> out;
    uint8_t cur = 0;
    int n = 0;
    for (const char* p = s; *p; ++p) {
        if (*p != '0' && *p != '1') continue;
        cur = static_cast<uint8_t>((cur << 1) | (*p == '1'));
        if (++n == 8) { out.push_back(cur); cur = 0; n = 0; }
    }
    if (n) out.push_back(static_cast<uint8_t>(cur << (8 - n)));
    if (out.size() < min_bytes) out.resize(min_bytes, 0);
    return out;
}

int16_t Sample(const std::vector<uint8_t>& raw16, size_t index) {
    return static_cast<int16_t>(raw16[index * 2] | (raw16[index * 2 + 1] << 8));
}

// RAW16 layout: Y 16x16 raster, then Cb 8x8, then Cr 8x8.
int16_t YSample(const std::vector<uint8_t>& raw16, int row, int col) { return Sample(raw16, row * 16 + col); }
int16_t CbSample(const std::vector<uint8_t>& raw16, int i) { return Sample(raw16, 256 + i); }
int16_t CrSample(const std::vector<uint8_t>& raw16, int i) { return Sample(raw16, 320 + i); }

// An intra macroblock, spec bits (ISO/IEC 13818-2 7.2.1, Tables B-12/B-13/B-14):
// Y0 "100 10": DC size 0 (predictor 128 after DCR), EOB -> flat 128.
// Y1 "01 11 10": size 2, differential 3 -> predictor 131, EOB -> flat 131.
// Y2, Y3 "100 10": predictor carries -> 131.  Cb, Cr "00 10": size 0 -> 128.
// 29 bits in all. The DC term is 8 * predictor (idp = 0) and the IDCT of a DC-only
// block is flat at DC / 8, so every sample is its predictor.
const char* const kIntraMbBits = "100 10  01 11 10  100 10  100 10  00 10  00 10";
constexpr unsigned kIntraMbBitCount = 29;

void TestBdecIntraMacroblock() {
    IpuDevice ipu;
    auto bytes = Bits(kIntraMbBits, 16);
    ipu.FeedInputFifo(bytes.data(), bytes.size());
    ipu.WriteCmd(0x2c010000);  // BDEC MBI=1 DCR=1 DT=0 QSC=1
    CHECK(!ipu.IsBusy());
    CHECK(ipu.CommandCount(2) == 1 && ipu.UnsupportedCommandCount(2) == 0);
    CHECK(ipu.BitPosition() == kIntraMbBitCount);
    CHECK(ipu.OutputSize() == IpuDevice::kBdecOutputBytes);
    CHECK(((ipu.ReadCtrl() >> 4) & 15) == 8);         // OFC saturates at eight quadwords
    CHECK(((ipu.ReadCtrl() >> 8) & 0x3f) == 0x3f);    // every block of an intra MB is coded
    CHECK((ipu.ReadCtrl() & (1u << 14)) == 0);
    const auto out = Drain(ipu);
    CHECK(out.size() == 768);
    if (out.size() == 768) {
        for (int row = 0; row < 16; ++row)
            for (int col = 0; col < 16; ++col) {
                const int16_t expect = (row < 8 && col < 8) ? 128 : 131;  // Y0 is the top-left block
                CHECK(YSample(out, row, col) == expect);
            }
        for (int i = 0; i < 64; ++i) CHECK(CbSample(out, i) == 128 && CrSample(out, i) == 128);
    }
    CHECK((ipu.ReadCtrl() & 0xf0) == 0);

    // Field DCT (DT = 1): blocks 0/1 are the top-field lines (even rows), 2/3 the
    // bottom-field lines (odd rows); Y0 at 128 therefore lands on even rows, columns 0..7.
    IpuDevice field;
    field.FeedInputFifo(bytes.data(), bytes.size());
    field.WriteCmd(0x2e010000);
    const auto interlaced = Drain(field);
    CHECK(interlaced.size() == 768);
    if (interlaced.size() == 768) {
        for (int row = 0; row < 16; ++row)
            for (int col = 0; col < 16; ++col) {
                const int16_t expect = ((row & 1) == 0 && col < 8) ? 128 : 131;
                CHECK(YSample(interlaced, row, col) == expect);
            }
    }

    // Without DCR the predictors continue from the previous macroblock (131, 128, 128):
    // the same bits again, immediately following in the stream, give 131 / 134.
    IpuDevice pair;
    const std::string two = std::string(kIntraMbBits) + " " + kIntraMbBits;
    auto two_bytes = Bits(two.c_str(), 16);
    pair.FeedInputFifo(two_bytes.data(), two_bytes.size());
    pair.WriteCmd(0x2c010000);
    CHECK(Drain(pair).size() == 768 && pair.BitPosition() == kIntraMbBitCount);
    pair.WriteCmd(0x28010000);  // MBI=1, DCR=0
    CHECK(pair.BitPosition() == 2 * kIntraMbBitCount);
    const auto second = Drain(pair);
    CHECK(second.size() == 768);
    if (second.size() == 768) {
        CHECK(YSample(second, 0, 0) == 131 && YSample(second, 0, 8) == 134 && YSample(second, 15, 15) == 134);
        CHECK(CbSample(second, 0) == 128);
    }
    // 9-bit intra DC precision (CTRL.IDP = 1): the reset predictor is 256 and the DC
    // multiplier 4, so the same bits give 128 and 128 + 3 * 4 / 8 -> 129.5 -> 130 / 129
    // (the IDCT rounds 1036 / 8 = 129.5 to the nearest).
    IpuDevice nine;
    nine.WriteCtrl(1u << 16);
    nine.FeedInputFifo(bytes.data(), bytes.size());
    nine.WriteCmd(0x2c010000);
    const auto wide = Drain(nine);
    CHECK(wide.size() == 768);
    if (wide.size() == 768) {
        CHECK(YSample(wide, 0, 0) == 128);
        CHECK(YSample(wide, 0, 8) == 129 || YSample(wide, 0, 8) == 130);
    }
}

void TestBdecNonIntraMacroblock() {
    // coded_block_pattern "1010" = 32 (Table B-9: Y0 only), then Y0 "1 0" (the non-intra
    // first coefficient: run 0 level +1) and EOB "10". Non-intra inverse quantisation with
    // the default flat-16 matrix and QSC = 31 (linear -> 62): F[0] = (3 * 16 * 62) / 32 = 93,
    // an odd sum so no mismatch toggle; the IDCT gives 93 / 8 = 11.625 -> 12 on every
    // sample of Y0. The five uncoded blocks are zero.
    IpuDevice ipu;
    auto bytes = Bits("1010  1 0  10", 16);
    ipu.FeedInputFifo(bytes.data(), bytes.size());
    ipu.WriteCmd(0x201f0000);  // BDEC MBI=0 DCR=0 QSC=31
    CHECK(!ipu.IsBusy() && ipu.UnsupportedCommandCount(2) == 0);
    CHECK(ipu.BitPosition() == 8);
    CHECK(((ipu.ReadCtrl() >> 8) & 0x3f) == 0x20);   // CTRL.CBP is the decoded pattern
    auto out = Drain(ipu);
    CHECK(out.size() == 768);
    if (out.size() == 768) {
        for (int row = 0; row < 16; ++row)
            for (int col = 0; col < 16; ++col)
                CHECK(YSample(out, row, col) == ((row < 8 && col < 8) ? 12 : 0));
        for (int i = 0; i < 64; ++i) CHECK(CbSample(out, i) == 0 && CrSample(out, i) == 0);
    }

    // CBP "111" = 60: the four Y blocks coded ("1 0 10" each), no chroma. With CTRL.QST the
    // scale is Table 7-6's 112: F[0] = (3 * 16 * 112) / 32 = 168 -- an even sum, so
    // mismatch control sets F[63] to 1; the IDCT is 168 / 8 = 21 everywhere (the F[63]
    // term contributes at most 1/4 and rounds away).
    IpuDevice qst;
    qst.WriteCtrl(1u << 22);
    bytes = Bits("111  1 0 10  1 0 10  1 0 10  1 0 10", 16);
    qst.FeedInputFifo(bytes.data(), bytes.size());
    qst.WriteCmd(0x201f0000);
    CHECK(qst.BitPosition() == 3 + 4 * 4);
    CHECK(((qst.ReadCtrl() >> 8) & 0x3f) == 60);
    out = Drain(qst);
    CHECK(out.size() == 768);
    if (out.size() == 768) {
        for (int i = 0; i < 256; ++i) CHECK(Sample(out, i) == 21);
        for (int i = 0; i < 64; ++i) CHECK(CbSample(out, i) == 0 && CrSample(out, i) == 0);
    }

    // The alternate scan (CTRL.AS) places the second coefficient of Y0 at raster 8 (a vertical
    // cosine) instead of raster 1 (horizontal): CBP 32, "1 0" then "11 0" (run 0 level +1)
    // then EOB. Under the zigzag the block varies along the row and is constant down a
    // column; under the alternate scan the opposite.
    for (bool alt : {false, true}) {
        IpuDevice dev;
        dev.WriteCtrl(alt ? (1u << 20) : 0);
        bytes = Bits("1010  1 0  11 0  10", 16);
        dev.FeedInputFifo(bytes.data(), bytes.size());
        dev.WriteCmd(0x201f0000);
        out = Drain(dev);
        CHECK(out.size() == 768);
        if (out.size() == 768) {
            const bool varies_along_row = YSample(out, 0, 0) != YSample(out, 0, 7);
            const bool varies_down_col = YSample(out, 0, 0) != YSample(out, 7, 0);
            CHECK(varies_along_row == !alt);
            CHECK(varies_down_col == alt);
        }
    }
}

void TestBdecWaitsForInput() {
    // Empty FIFO: BDEC waits (BUSY, not divergence); the bits arriving complete it.
    IpuDevice ipu;
    ipu.WriteCmd(0x2c010000);
    CHECK(ipu.IsBusy() && ipu.PendingCommand() == 0x2c010000);
    CHECK((ipu.ReadCtrl() & IpuDevice::kCtrlBusy) != 0);
    CHECK(ipu.UnsupportedCommandCount(2) == 0 && ipu.OutputSize() == 0);
    auto bytes = Bits(kIntraMbBits, 16);
    // A partial feed (the first two bytes: Y0, Y1 and part of Y2) keeps it waiting.
    ipu.FeedInputFifo(bytes.data(), 2);
    CHECK(ipu.IsBusy() && ipu.OutputSize() == 0);
    ipu.FeedInputFifo(bytes.data() + 2, bytes.size() - 2);
    CHECK(!ipu.IsBusy() && ipu.PendingCompletions() == 1);
    CHECK(ipu.OutputSize() == 768 && ipu.BitPosition() == kIntraMbBitCount);
    const auto out = Drain(ipu);
    CHECK(out.size() == 768 && YSample(out, 0, 0) == 128 && YSample(out, 15, 15) == 131);

    // A macroblock longer than the eight-quadword FIFO: Y0 carries 63 escape-coded AC
    // coefficients (24 bits each), 1517 bits for the block. Fill the FIFO (128 bytes),
    // issue BDEC: it waits, and the input it already parsed counts as consumed, so the
    // FIFO reports room (IFC 0) for the toIPU channel to continue -- as the hardware's
    // decoder drains its FIFO while it decodes. The remainder completes it.
    std::string long_mb = "100";
    for (int i = 0; i < 63; ++i) long_mb += " 000001 000000 000000000001";  // run 0, level +1
    long_mb += " 10";                                      // EOB
    long_mb += " 100 10  100 10  100 10  00 10  00 10";    // Y1..Y3, Cb, Cr
    const unsigned long_bits = 3 + 63 * 24 + 2 + 5 * 3 + 4 * 2;
    auto long_bytes = Bits(long_mb.c_str());
    long_bytes.resize((long_bytes.size() + 15) & ~size_t{15}, 0);
    CHECK(long_bytes.size() > IpuDevice::kInputFifoBytes);
    IpuDevice big;
    unsigned room_events = 0;
    big.SetInputRoomReady([&] { ++room_events; });
    big.FeedInputFifo(long_bytes.data(), IpuDevice::kInputFifoBytes);
    CHECK(big.InputRoomBytes() == 0 && (big.ReadCtrl() & 15) == 8);
    big.WriteCmd(0x2c010000);
    CHECK(big.IsBusy() && big.OutputSize() == 0);
    // The decoder stalled inside the 42nd escape at bit 3 + 42 * 24 = 1011 (13 bits short of
    // the 24 it needs): seven whole quadwords are consumed, the eighth is still held.
    CHECK(big.InputRoomBytes() == 7 * 16);
    CHECK((big.ReadCtrl() & 15) == 1);
    CHECK(room_events == 1);
    big.FeedInputFifo(long_bytes.data() + IpuDevice::kInputFifoBytes,
                      long_bytes.size() - IpuDevice::kInputFifoBytes);
    CHECK(!big.IsBusy() && big.OutputSize() == 768);
    CHECK(big.BitPosition() == long_bits);
    CHECK(big.UnsupportedCommandCount(2) == 0 && big.PendingCompletions() == 1);
    const auto long_out = Drain(big);
    CHECK(long_out.size() == 768);
    if (long_out.size() == 768) {
        // Y1..Y3 and the chroma are DC-only: flat 128. Y0 carries AC energy.
        CHECK(YSample(long_out, 0, 8) == 128 && YSample(long_out, 15, 15) == 128 && CbSample(long_out, 63) == 128);
        bool y0_varies = false;
        for (int i = 1; i < 8; ++i) y0_varies = y0_varies || YSample(long_out, 0, i) != YSample(long_out, 0, 0);
        CHECK(y0_varies);
    }

    // RESET drops a waiting BDEC.
    IpuDevice dropped;
    dropped.WriteCmd(0x2c010000);
    dropped.WriteCtrl(IpuDevice::kCtrlRst);
    CHECK(!dropped.IsBusy() && dropped.PendingCommand() == 0);
}

void TestBdecErrorCode() {
    // Y0's DC "100" then 32 zero bits: no coefficient code starts with 0000 0000 0000
    // (Table B-14's longest codes are 16 bits, the escape is "000001"), so this is a
    // bitstream error, not starvation: ECD set, no output, the command complete.
    IpuDevice ipu;
    auto bytes = Bits("100 00000000000000000000000000000000", 16);
    ipu.FeedInputFifo(bytes.data(), bytes.size());
    ipu.WriteCmd(0x2c010000);
    CHECK(!ipu.IsBusy());
    CHECK((ipu.ReadCtrl() & (1u << 14)) != 0);
    CHECK(ipu.OutputSize() == 0);
    CHECK(ipu.BitPosition() == 3);   // the bit pointer stops where the code failed
    CHECK(ipu.UnsupportedCommandCount(2) == 0 && ipu.CommandCount(2) == 1);
    // The next command clears ECD.
    ipu.WriteCmd(0x40000000);
    CHECK((ipu.ReadCtrl() & (1u << 14)) == 0);
    // A run past the last coefficient is an error too: escape run 63 at scan 1.
    IpuDevice overrun;
    bytes = Bits("100 000001 111111 000000000001 10 00000000000000000000000000000000", 16);
    overrun.FeedInputFifo(bytes.data(), bytes.size());
    overrun.WriteCmd(0x2c010000);
    CHECK(!overrun.IsBusy() && (overrun.ReadCtrl() & (1u << 14)) != 0 && overrun.OutputSize() == 0);
    // A non-intra macroblock whose coded_block_pattern is invalid (nine zero bits) errs the same way.
    IpuDevice cbp;
    bytes = Bits("000000000 00000000000000000000000000000000", 16);
    cbp.FeedInputFifo(bytes.data(), bytes.size());
    cbp.WriteCmd(0x20010000);
    CHECK(!cbp.IsBusy() && (cbp.ReadCtrl() & (1u << 14)) != 0 && cbp.OutputSize() == 0);
}
}  // namespace

int main() {
    TestCensusLifetime();
    TestVdecTables();
    TestSetVqAndIndexedPack();
    TestPackDitherAndAlpha();
    TestCscThresholdsAndChroma();
    TestSetIqBitOffsetAndUnderflow();
    TestShortPackWaitsForInput();
    TestIdecFallbackFormattingIsCounted();
    TestOutputReadyHook();
    TestBdecIntraMacroblock();
    TestBdecNonIntraMacroblock();
    TestBdecWaitsForInput();
    TestBdecErrorCode();
    std::printf("test_ipu_completion: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
