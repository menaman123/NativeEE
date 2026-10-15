// engine/ipu/mpeg_decode.h -- the IPU's MPEG-2 intra decode arithmetic as pure
// functions: VLC tables, zigzag scan, inverse quantisation, the float reference
// IDCT and the 4:2:0 YCbCr -> RGB32 conversion. No registers, no FIFO, no
// globals: every function works over a caller-owned BitCursor / block and the
// IpuDevice drives it for IDEC and CSC. The rules are the MPEG-2 Video spec
// (ISO/IEC 13818-2): Table B-12 / B-13 DC size codes, the Figure 7-2 zigzag
// scan, the default intra quantiser matrix (6.3.11), and the IPU manual's
// RGB32 output pixel (A = 0x80 opaque).
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
    static const uint8_t* Zigzag();                   // 64 entries: scan index -> raster index
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

    // DC size code + differential (7.2.1): returns the signed dc_dct_differential,
    // 0 when no code matches or the bits run out. block_idx 0 = Y, 1 = Cb, 2 = Cr.
    static int DecodeDcCoeff(BitCursor& c, int block_idx);
    // Run/level AC coefficients into `block` (raster order via the zigzag scan)
    // until the end-of-block code "10". Escape "000001" carries a 6-bit run and
    // a 12-bit signed level; the single-bit "1" + sign form is the +-1 level.
    static void DecodeAcCoeffs(BitCursor& c, int16_t block[kBlockCoeffs]);
    // Separable 8x8 float inverse DCT in place; output clamped to [-256, 255].
    static void InverseDct(int16_t block[kBlockCoeffs]);
    // One 8x8 intra block: DC prediction (dc_pred[block_idx] is updated), AC
    // coefficients, inverse quantisation with `intra_iq` and quantiser scale
    // `qsc`, DC scaled by (3 - idp), then the IDCT.
    static void DecodeBlock(BitCursor& c, int16_t block[kBlockCoeffs], int block_idx, int qsc,
                            const uint8_t intra_iq[kBlockCoeffs], int dc_pred[3], uint8_t idp);
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
