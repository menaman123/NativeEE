// engine/ipu/mpeg_decode.cpp -- see mpeg_decode.h.
#include "engine/ipu/mpeg_decode.h"
#include "engine/ipu/mpeg_coeff_vlc.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ps2r::ipu {

namespace {

// MPEG-2 default intra quantiser matrix (ISO/IEC 13818-2 6.3.11), raster order.
const uint8_t kDefaultIntraIq[MpegDecode::kBlockCoeffs] = {
     8, 16, 19, 22, 26, 27, 29, 34,
    16, 16, 22, 24, 27, 29, 34, 37,
    19, 22, 26, 27, 29, 34, 34, 38,
    22, 22, 26, 27, 29, 34, 37, 40,
    22, 26, 27, 29, 32, 35, 40, 48,
    26, 27, 29, 32, 35, 40, 48, 58,
    26, 27, 29, 34, 38, 46, 56, 69,
    27, 29, 35, 38, 46, 56, 69, 83
};

// Zigzag scan (Figure 7-2): scan position -> raster index.
const uint8_t kZigzag[MpegDecode::kBlockCoeffs] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

// Alternate scan (Figure 7-3, alternate_scan = 1): scan position -> raster index.
const uint8_t kAlternateScan[MpegDecode::kBlockCoeffs] = {
     0,  8, 16, 24,  1,  9,  2, 10,
    17, 25, 32, 40, 48, 56, 57, 49,
    41, 33, 26, 18,  3, 11,  4, 12,
    19, 27, 34, 42, 50, 58, 35, 43,
    51, 59, 20, 28,  5, 13,  6, 14,
    21, 29, 36, 44, 52, 60, 37, 45,
    53, 61, 22, 30,  7, 15, 23, 31,
    38, 46, 54, 62, 39, 47, 55, 63
};

// Table B-9: coded_block_pattern. The code bits as written in the spec (MSB first)
// and the pattern (bit 5 = Y0, 4 = Y1, 3 = Y2, 2 = Y3, 1 = Cb, 0 = Cr). The last row,
// "0000 0000 1" -> 0, is the MPEG-2 (4:2:2 / 4:4:4) entry; it is decodable in 4:2:0 too.
struct CbpCode {
    const char* bits;
    uint8_t cbp;
};
const CbpCode kCbpTable[] = {
    {"111", 60},       {"1101", 4},       {"1100", 8},       {"1011", 16},      {"1010", 32},
    {"10011", 12},     {"10010", 48},     {"10001", 20},     {"10000", 40},     {"01111", 28},
    {"01110", 44},     {"01101", 52},     {"01100", 56},     {"01011", 1},      {"01010", 61},
    {"01001", 2},      {"01000", 62},     {"001111", 24},    {"001110", 36},    {"001101", 3},
    {"001100", 63},    {"0010111", 5},    {"0010110", 9},    {"0010101", 17},   {"0010100", 33},
    {"0010011", 6},    {"0010010", 10},   {"0010001", 18},   {"0010000", 34},   {"00011111", 7},
    {"00011110", 11},  {"00011101", 19},  {"00011100", 35},  {"00011011", 13},  {"00011010", 49},
    {"00011001", 21},  {"00011000", 41},  {"00010111", 14},  {"00010110", 50},  {"00010101", 22},
    {"00010100", 42},  {"00010011", 15},  {"00010010", 51},  {"00010001", 23},  {"00010000", 43},
    {"00001111", 25},  {"00001110", 37},  {"00001101", 26},  {"00001100", 38},  {"00001011", 29},
    {"00001010", 45},  {"00001001", 53},  {"00001000", 57},  {"00000111", 30},  {"00000110", 46},
    {"00000101", 54},  {"00000100", 58},  {"000000111", 31}, {"000000110", 47}, {"000000101", 55},
    {"000000100", 59}, {"000000011", 27}, {"000000010", 39}, {"000000001", 0},
};

// Table B-12: dct_dc_size_luminance.
const VlcEntry kDcLumTable[MpegDecode::kDcTableEntries] = {
    {0b100,       3, 0}, {0b00,        2, 1}, {0b01,        2, 2},
    {0b101,       3, 3}, {0b110,       3, 4}, {0b1110,      4, 5},
    {0b11110,     5, 6}, {0b111110,    6, 7}, {0b1111110,   7, 8},
    {0b11111110,  8, 9}, {0b111111110, 9, 10}, {0b111111111, 9, 11},
};

// Table B-13: dct_dc_size_chrominance.
const VlcEntry kDcChromTable[MpegDecode::kDcTableEntries] = {
    {0b00,        2, 0}, {0b01,        2, 1}, {0b10,        2, 2},
    {0b110,       3, 3}, {0b1110,      4, 4}, {0b11110,     5, 5},
    {0b111110,    6, 6}, {0b1111110,   7, 7}, {0b11111110,  8, 8},
    {0b111111110, 9, 9}, {0b1111111110, 10, 10}, {0b1111111111, 10, 11},
};

constexpr double kPi = 3.14159265358979323846;

}  // namespace

const uint8_t* MpegDecode::Zigzag() { return kZigzag; }
const uint8_t* MpegDecode::AlternateScan() { return kAlternateScan; }
const uint8_t* MpegDecode::DefaultIntraQuantMatrix() { return kDefaultIntraIq; }
const VlcEntry* MpegDecode::DcLumTable() { return kDcLumTable; }
const VlcEntry* MpegDecode::DcChromTable() { return kDcChromTable; }

// ---------------------------------------------------------------------------
// Bit reading
// ---------------------------------------------------------------------------
bool MpegDecode::HasBits(const BitCursor& c, int count) {
    return (static_cast<uint64_t>(c.bit_pos) + static_cast<uint64_t>(count)) <=
           static_cast<uint64_t>(c.size) * 8u;
}

uint32_t MpegDecode::PeekBits(const BitCursor& c, int count) {
    if (count == 0 || !HasBits(c, count)) return 0;
    uint32_t result = 0;
    for (int i = 0; i < count; i++) {
        const uint32_t byte_pos = (c.bit_pos + static_cast<uint32_t>(i)) / 8;
        const uint32_t bit_pos = 7 - ((c.bit_pos + static_cast<uint32_t>(i)) % 8);  // MSB first
        if (byte_pos < c.size) {
            result = (result << 1) | ((c.data[byte_pos] >> bit_pos) & 1u);
        }
    }
    return result;
}

uint32_t MpegDecode::ReadBits(BitCursor& c, int count) {
    const uint32_t val = PeekBits(c, count);
    c.bit_pos += static_cast<uint32_t>(count);
    return val;
}

void MpegDecode::SkipBits(BitCursor& c, int count) { c.bit_pos += static_cast<uint32_t>(count); }

// ---------------------------------------------------------------------------
// DC coefficient (7.2.1): size code from B-12 / B-13, then dc_size bits of
// differential; values below the half range are negative. Both tables are
// complete prefix codes: with the longest code's worth of bits present one
// entry always matches, so "nothing matched" means the code is cut off and the
// cursor is pushed past the end (the longest code's length) to say so.
// ---------------------------------------------------------------------------
int MpegDecode::DecodeDcCoeff(BitCursor& c, int block_idx) {
    const VlcEntry* table = (block_idx == 0) ? kDcLumTable : kDcChromTable;

    int dc_size = -1;
    int longest = 0;
    for (int i = 0; i < kDcTableEntries; i++) {
        if (table[i].len > longest) longest = table[i].len;
        if (HasBits(c, table[i].len)) {
            const uint32_t bits = PeekBits(c, table[i].len);
            if (bits == table[i].code) {
                dc_size = table[i].value;
                SkipBits(c, table[i].len);
                break;
            }
        }
    }

    if (dc_size < 0) {
        SkipBits(c, longest);  // cut-off size code: runs past the end
        return 0;
    }
    if (dc_size == 0) return 0;

    int dc_diff = static_cast<int>(ReadBits(c, dc_size));  // past the end when cut off
    if (!HasBits(c, 0)) return 0;

    if (dc_diff < (1 << (dc_size - 1))) {
        dc_diff -= (1 << dc_size) - 1;
    }
    return dc_diff;
}

// ---------------------------------------------------------------------------
// AC coefficients through MpegCoeffVlc: each (run, level) lands at the next scan
// position + run; the end-of-block code ends the block.
// ---------------------------------------------------------------------------
bool MpegDecode::DecodeAcCoeffs(BitCursor& c, int16_t block[kBlockCoeffs], bool table_one, bool mpeg1,
                                bool alt_scan) {
    const uint8_t* scan = alt_scan ? kAlternateScan : kZigzag;
    int pos = 1;  // after DC
    while (pos < kBlockCoeffs) {
        MpegCoeffVlc::Symbol sym;
        if (!MpegCoeffVlc::Decode(c, table_one, false, mpeg1, sym)) return false;
        if (sym.eob) return true;
        pos += sym.run;
        if (pos >= kBlockCoeffs) return false;
        block[scan[pos]] = static_cast<int16_t>(sym.level);
        ++pos;
    }
    // 64 coefficients filled: the EOB still follows.
    MpegCoeffVlc::Symbol sym;
    if (!MpegCoeffVlc::Decode(c, table_one, false, mpeg1, sym)) return false;
    return sym.eob;
}

bool MpegDecode::DecodeNonIntraCoeffs(BitCursor& c, int16_t block[kBlockCoeffs], bool mpeg1, bool alt_scan) {
    const uint8_t* scan = alt_scan ? kAlternateScan : kZigzag;
    int pos = 0;
    bool first = true;
    while (pos < kBlockCoeffs) {
        MpegCoeffVlc::Symbol sym;
        if (!MpegCoeffVlc::Decode(c, false, first, mpeg1, sym)) return false;
        first = false;
        if (sym.eob) return true;
        pos += sym.run;
        if (pos >= kBlockCoeffs) return false;
        block[scan[pos]] = static_cast<int16_t>(sym.level);
        ++pos;
    }
    MpegCoeffVlc::Symbol sym;
    if (!MpegCoeffVlc::Decode(c, false, false, mpeg1, sym)) return false;
    return sym.eob;
}

bool MpegDecode::DecodeCodedBlockPattern(BitCursor& c, int& cbp) {
    for (const CbpCode& entry : kCbpTable) {
        uint32_t code = 0;
        int len = 0;
        for (const char* p = entry.bits; *p; ++p) {
            code = (code << 1) | (*p == '1' ? 1u : 0u);
            ++len;
        }
        if (!HasBits(c, len)) continue;
        if (PeekBits(c, len) == code) {
            SkipBits(c, len);
            cbp = entry.cbp;
            return true;
        }
    }
    return false;
}

int MpegDecode::QuantiserScale(int qsc_code, bool q_scale_type) {
    static const int kNonLinear[32] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  10, 12,  14,  16,  18,  20,  22,
                                       24, 28, 32, 36, 40, 44, 48, 52, 56, 64, 72,  80,  88,  96,  104, 112};
    qsc_code &= 31;
    return q_scale_type ? kNonLinear[qsc_code] : 2 * qsc_code;
}

namespace {
void MismatchControl(int16_t block[MpegDecode::kBlockCoeffs]) {
    int sum = 0;
    for (int i = 0; i < MpegDecode::kBlockCoeffs; ++i) sum += block[i];
    if ((sum & 1) == 0) block[63] = static_cast<int16_t>(block[63] ^ 1);
}
int16_t Saturate(int v) { return static_cast<int16_t>(v < -2048 ? -2048 : (v > 2047 ? 2047 : v)); }
}  // namespace

void MpegDecode::DequantIntra(int16_t block[kBlockCoeffs], const uint8_t w[kBlockCoeffs], int qs) {
    for (int i = 1; i < kBlockCoeffs; ++i) {
        if (block[i] == 0) continue;
        block[i] = Saturate((block[i] * w[i] * qs) / 16);
    }
    block[0] = Saturate(block[0]);
    MismatchControl(block);
}

void MpegDecode::DequantNonIntra(int16_t block[kBlockCoeffs], const uint8_t w[kBlockCoeffs], int qs) {
    for (int i = 0; i < kBlockCoeffs; ++i) {
        const int qf = block[i];
        if (qf == 0) continue;
        const int sign = qf > 0 ? 1 : -1;
        block[i] = Saturate(((2 * qf + sign) * w[i] * qs) / 32);
    }
    MismatchControl(block);
}

bool MpegDecode::DecodeIntraBlock(BitCursor& c, int16_t block[kBlockCoeffs], int block_idx, int qs,
                                  const uint8_t intra_iq[kBlockCoeffs], int dc_pred[3], uint8_t idp,
                                  bool table_one, bool mpeg1, bool alt_scan) {
    memset(block, 0, sizeof(int16_t) * kBlockCoeffs);
    const int dc_diff = DecodeDcCoeff(c, block_idx);
    if (!HasBits(c, 0)) return false;  // the DC code was cut off
    dc_pred[block_idx] += dc_diff;
    const bool ok = DecodeAcCoeffs(c, block, table_one, mpeg1, alt_scan);
    block[0] = 0;
    DequantIntra(block, intra_iq, qs);
    block[0] = static_cast<int16_t>(dc_pred[block_idx] << (3 - (idp > 3 ? 3 : idp)));
    InverseDct(block);
    return ok;
}

bool MpegDecode::DecodeNonIntraBlock(BitCursor& c, int16_t block[kBlockCoeffs], int qs,
                                     const uint8_t non_intra_iq[kBlockCoeffs], bool mpeg1, bool alt_scan) {
    memset(block, 0, sizeof(int16_t) * kBlockCoeffs);
    const bool ok = DecodeNonIntraCoeffs(c, block, mpeg1, alt_scan);
    DequantNonIntra(block, non_intra_iq, qs);
    InverseDct(block);
    return ok;
}

// ---------------------------------------------------------------------------
// Separable float IDCT (the reference form of Annex A): rows then columns,
// C(0) = 1/sqrt(2), each pass scaled by 1/2; result rounded and clamped.
// ---------------------------------------------------------------------------
void MpegDecode::InverseDct(int16_t block[kBlockCoeffs]) {
    float temp[kBlockCoeffs];
    float result[kBlockCoeffs];

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            float sum = 0;
            for (int u = 0; u < 8; u++) {
                const float cu = (u == 0) ? (1.0f / sqrtf(2.0f)) : 1.0f;
                sum += cu * block[y * 8 + u] * cosf((2.0f * x + 1.0f) * u * kPi / 16.0f);
            }
            temp[y * 8 + x] = sum / 2.0f;
        }
    }

    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            float sum = 0;
            for (int v = 0; v < 8; v++) {
                const float cv = (v == 0) ? (1.0f / sqrtf(2.0f)) : 1.0f;
                sum += cv * temp[v * 8 + x] * cosf((2.0f * y + 1.0f) * v * kPi / 16.0f);
            }
            result[y * 8 + x] = sum / 2.0f;
        }
    }

    for (int i = 0; i < kBlockCoeffs; i++) {
        block[i] = static_cast<int16_t>(std::clamp(static_cast<int>(roundf(result[i])), -256, 255));
    }
}

// ---------------------------------------------------------------------------
// One 8x8 intra block.
// ---------------------------------------------------------------------------
void MpegDecode::DecodeBlock(BitCursor& c, int16_t block[kBlockCoeffs], int block_idx, int qsc,
                             const uint8_t intra_iq[kBlockCoeffs], int dc_pred[3], uint8_t idp) {
    memset(block, 0, sizeof(int16_t) * kBlockCoeffs);

    const int dc_diff = DecodeDcCoeff(c, block_idx);
    dc_pred[block_idx] += dc_diff;
    block[0] = static_cast<int16_t>(dc_pred[block_idx]);

    DecodeAcCoeffs(c, block);

    // Inverse quantisation: coefficient * matrix entry * quantiser scale, /8;
    // the product is stored through the 16-bit coefficient before the clamp
    // to [-2048, 2047].
    for (int i = 0; i < kBlockCoeffs; i++) {
        if (block[i] != 0) {
            block[i] = static_cast<int16_t>((block[i] * intra_iq[i] * qsc) >> 3);
            block[i] = std::clamp(block[i], static_cast<int16_t>(-2048), static_cast<int16_t>(2047));
        }
    }
    // The DC term is scaled by the intra DC precision instead of the matrix.
    block[0] = static_cast<int16_t>(dc_pred[block_idx] << (3 - idp));

    InverseDct(block);
}

// ---------------------------------------------------------------------------
// One intra macroblock: 4 Y blocks + Cb + Cr, converted and appended as RGB32.
// ---------------------------------------------------------------------------
void MpegDecode::DecodeIntraMacroblock(BitCursor& c, int qsc, bool sgn,
                                       const uint8_t intra_iq[kBlockCoeffs], int dc_pred[3],
                                       uint8_t idp, std::vector<uint8_t>& out) {
    int16_t y_blocks[4][kBlockCoeffs];
    int16_t cb_block[kBlockCoeffs];
    int16_t cr_block[kBlockCoeffs];

    for (int i = 0; i < 4; i++) DecodeBlock(c, y_blocks[i], 0, qsc, intra_iq, dc_pred, idp);
    DecodeBlock(c, cb_block, 1, qsc, intra_iq, dc_pred, idp);
    DecodeBlock(c, cr_block, 2, qsc, intra_iq, dc_pred, idp);

    uint8_t pixels[kMacroblockRgb32Bytes];
    YCbCrToRgb32(y_blocks, cb_block, cr_block, pixels, sgn);
    out.insert(out.end(), pixels, pixels + sizeof(pixels));
}

// ---------------------------------------------------------------------------
// 4:2:0 YCbCr -> RGB32. Y blocks are laid out Y0 Y1 over Y2 Y3; one Cb/Cr
// sample covers a 2x2 pixel area. Fixed-point BT.601 coefficients (x1024):
// R = Y + 1.402 Cr, G = Y - 0.344 Cb - 0.714 Cr, B = Y + 1.772 Cb.
// diverges: the alpha byte is the constant 0x80; the hardware's TH0/TH1 alpha
// thresholds (SETTH) are latched by IpuDevice but not applied here.
// ---------------------------------------------------------------------------
void MpegDecode::YCbCrToRgb32(const int16_t y_blocks[4][kBlockCoeffs], const int16_t cb[kBlockCoeffs],
                              const int16_t cr[kBlockCoeffs], uint8_t out[kMacroblockRgb32Bytes],
                              bool sgn) {
    for (int py = 0; py < 16; py++) {
        for (int px = 0; px < 16; px++) {
            const int yb = (py / 8) * 2 + (px / 8);
            const int yx = px % 8;
            const int yy = py % 8;

            const int y_val = y_blocks[yb][yy * 8 + yx];

            const int cx = px / 2;
            const int cy = py / 2;
            const int cb_val = cb[cy * 8 + cx];
            const int cr_val = cr[cy * 8 + cx];

            int r = y_val + ((cr_val * 1436) >> 10);
            int g = y_val - ((cb_val * 352 + cr_val * 731) >> 10);
            int b = y_val + ((cb_val * 1815) >> 10);

            if (sgn) {
                r += 128;
                g += 128;
                b += 128;
            }

            r = std::clamp(r, 0, 255);
            g = std::clamp(g, 0, 255);
            b = std::clamp(b, 0, 255);

            const int idx = (py * 16 + px) * 4;
            out[idx + 0] = static_cast<uint8_t>(r);
            out[idx + 1] = static_cast<uint8_t>(g);
            out[idx + 2] = static_cast<uint8_t>(b);
            out[idx + 3] = 0x80;
        }
    }
}

// The CSC command's conversion: float BT.601 coefficients truncated toward
// zero per term, clamped, packed A(0x80) B G R from the top byte down.
uint32_t MpegDecode::CscPixelRgb32(int y, int cb_centered, int cr_centered) {
    const int r = std::clamp(y + static_cast<int>(1.402f * cr_centered), 0, 255);
    const int g = std::clamp(y - static_cast<int>(0.344f * cb_centered) -
                                 static_cast<int>(0.714f * cr_centered), 0, 255);
    const int b = std::clamp(y + static_cast<int>(1.772f * cb_centered), 0, 255);
    return (0x80u << 24) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(r);
}

}  // namespace ps2r::ipu
