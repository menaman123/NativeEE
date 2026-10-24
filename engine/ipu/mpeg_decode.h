// engine/ipu/mpeg_decode.h -- the IPU's MPEG-2 block decode arithmetic as pure
// functions: VLC tables, the two coefficient scans, inverse quantisation, the
// float reference IDCT and the 4:2:0 YCbCr -> RGB32 conversion. No registers, no
// FIFO, no globals: every function works over a caller-owned BitCursor / block and
// the IpuDevice drives it for IDEC, BDEC and CSC. The rules are the MPEG-2 Video
// spec (ISO/IEC 13818-2): Table B-12 / B-13 DC size codes, Table B-9 coded block
// patterns, the Figure 7-2 zigzag and Figure 7-3 alternate scans, the default
// intra quantiser matrix (6.3.11), the 7.4 inverse quantisation and the IPU
// manual's RGB32 output pixel (A = 0x80 opaque).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ps2r::ipu {

// A read position over a byte buffer, MSB-first within each byte. A POD: the
// caller (IpuDevice) owns the bytes and mirrors `bit_pos` back into BP.
struct BitCursor {
    const uint8_t* data;
    size_t size;
    uint32_t bit_pos;
};

// One variable-length code: the code bits (right-aligned), its length in
// bits and the decoded value.
struct VlcEntry {
    uint16_t code;
    uint8_t len;
    uint8_t value;
};

class MpegDecode {
public:
    static constexpr int kBlockCoeffs = 64;
    static constexpr int kMacroblockPixels = 16 * 16;
    // RGB32 output of one 16x16 macroblock: 4 bytes per pixel.
    static constexpr size_t kMacroblockRgb32Bytes = kMacroblockPixels * 4;
    static constexpr int kDcTableEntries = 12;

    // Tables (spec constants).
    static const uint8_t* Zigzag();                   // 64 entries: scan index -> raster index (Figure 7-2)
    static const uint8_t* AlternateScan();            // 64 entries: the alternate scan (Figure 7-3)
    static const uint8_t* DefaultIntraQuantMatrix();  // 64 entries, raster order
    static const VlcEntry* DcLumTable();              // Table B-12, kDcTableEntries entries
    static const VlcEntry* DcChromTable();            // Table B-13, kDcTableEntries entries

    // Bit reading. PeekBits returns 0 when fewer than `count` bits remain;
    // ReadBits/SkipBits always advance (the cursor may run past the end, which
    // HasBits then reports).
    static bool HasBits(const BitCursor& c, int count);
    static uint32_t PeekBits(const BitCursor& c, int count);
    static uint32_t ReadBits(BitCursor& c, int count);
    static void SkipBits(BitCursor& c, int count);

    // DC size code + differential (7.2.1): returns the signed dc_dct_differential.
    // block_idx 0 = Y, 1 = Cb, 2 = Cr. When the bits run out (the size code is cut
    // off, or the differential is) the result is 0 and the cursor is left PAST the
    // end of the buffer -- the same "needs more bits" signal ReadBits gives -- so a
    // caller cannot mistake a truncated code for a complete one.
    static int DecodeDcCoeff(BitCursor& c, int block_idx);
    // Run/level AC coefficients of an INTRA block into `block` (raster order via the
    // zigzag scan, or the alternate scan when `alt_scan`) until the end-of-block code,
    // through the Table B-14 (table_one = false) or B-15 (true) codes of MpegCoeffVlc;
    // `mpeg1` selects the MPEG-1 escape. Returns false when a code did not match or the
    // bits ran out (the block holds what was decoded so far).
    static bool DecodeAcCoeffs(BitCursor& c, int16_t block[kBlockCoeffs], bool table_one = false,
                               bool mpeg1 = false, bool alt_scan = false);
    // Every coefficient of a NON-intra block (the first one included, with table zero's
    // first-coefficient rule) in quantised form, raster order. Same return as above.
    static bool DecodeNonIntraCoeffs(BitCursor& c, int16_t block[kBlockCoeffs], bool mpeg1 = false,
                                     bool alt_scan = false);
    // coded_block_pattern (6.2.6 / Table B-9) of a non-intra 4:2:0 macroblock: bit 5 = Y0
    // ... bit 0 = Cr. Returns false (cursor unchanged) when no code matches or the bits
    // run out; the longest code is kCbpCodeBits.
    static constexpr int kCbpCodeBits = 9;
    static bool DecodeCodedBlockPattern(BitCursor& c, int& cbp);
    // The quantiser_scale for a 5-bit quantiser_scale_code (7.4.2.2): linear (2 * code)
    // or the non-linear Table 7-6 when q_scale_type is set.
    static int QuantiserScale(int qsc_code, bool q_scale_type);
    // Inverse quantisation + saturation + mismatch control (7.4.2 - 7.4.4) in place.
    // Intra: F = (QF * W * qs) / 16 (DC handled by the caller); non-intra:
    // F = ((2 QF + sign QF) * W * qs) / 32. Both saturate to [-2048, 2047] and toggle
    // F[63]'s LSB when the coefficient sum is even.
    static void DequantIntra(int16_t block[kBlockCoeffs], const uint8_t w[kBlockCoeffs], int qs);
    static void DequantNonIntra(int16_t block[kBlockCoeffs], const uint8_t w[kBlockCoeffs], int qs);
    // Separable 8x8 float inverse DCT in place; output clamped to [-256, 255].
    static void InverseDct(int16_t block[kBlockCoeffs]);
    // One 8x8 intra block: DC prediction (dc_pred[block_idx] is updated), AC
    // coefficients, inverse quantisation with `intra_iq` and quantiser scale
    // `qsc`, DC scaled by (3 - idp), then the IDCT.
    static void DecodeBlock(BitCursor& c, int16_t block[kBlockCoeffs], int block_idx, int qsc,
                            const uint8_t intra_iq[kBlockCoeffs], int dc_pred[3], uint8_t idp);
    // The BDEC forms: one intra block (DC prediction, the IVF table choice, the
    // quantiser_scale already resolved) and one non-intra block (coded-block-pattern
    // bit set; the caller zeroes the uncoded ones), both through the IDCT into 16-bit
    // samples. Return false on a VLC failure (the block is left as decoded so far).
    static bool DecodeIntraBlock(BitCursor& c, int16_t block[kBlockCoeffs], int block_idx, int qs,
                                 const uint8_t intra_iq[kBlockCoeffs], int dc_pred[3], uint8_t idp,
                                 bool table_one, bool mpeg1, bool alt_scan = false);
    static bool DecodeNonIntraBlock(BitCursor& c, int16_t block[kBlockCoeffs], int qs,
                                    const uint8_t non_intra_iq[kBlockCoeffs], bool mpeg1,
                                    bool alt_scan = false);
    // One intra macroblock (Y0 Y1 Y2 Y3 Cb Cr) appended to `out` as RGB32.
    static void DecodeIntraMacroblock(BitCursor& c, int qsc, bool sgn,
                                      const uint8_t intra_iq[kBlockCoeffs], int dc_pred[3],
                                      uint8_t idp, std::vector<uint8_t>& out);
    // 4:2:0 macroblock (four Y blocks, one Cb, one Cr; coefficients already
    // through the IDCT) -> 256 RGB32 pixels {R, G, B, 0x80}. `sgn` adds the 128
    // offset the IDEC command's SGN bit requests.
    static void YCbCrToRgb32(const int16_t y_blocks[4][kBlockCoeffs], const int16_t cb[kBlockCoeffs],
                             const int16_t cr[kBlockCoeffs], uint8_t out[kMacroblockRgb32Bytes],
                             bool sgn);
    // The CSC command's per-pixel conversion of raw 8-bit samples (Cb/Cr already
    // centred on 0) to one little-endian RGB32 word: A = 0x80 in the top byte,
    // then B, G, R.
    static uint32_t CscPixelRgb32(int y, int cb_centered, int cr_centered);
};

}  // namespace ps2r::ipu
