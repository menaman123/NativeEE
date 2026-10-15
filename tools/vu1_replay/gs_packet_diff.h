#pragma once
// The HLE oracle's GIF packet decoder / differ: every XGKICK packet (a GIFtag
// chain, PACKED or REGLIST or IMAGE) is lowered to ONE canonical form -- a
// flat sequence of (GS register address, 64-bit value) writes -- so the
// interpreter's and the native program's output compare independently of
// PACKED vs REGLIST, PRE/PRIM or A+D, with per-register tolerances (vertex
// XYZ in 12.4 units, RGBA in 8-bit steps, ST/Q as floats). The formats of
// the mismatch lines, the summary and the dump are the recorded oracle
// grammar and are frozen.
//
// Harness conventions (not GS behaviour, kept for deterministic diffs): the
// Q latch resets to 1.0 per packet; a repeated identical write of a
// non-vertex state register is dropped by Normalize unless the tolerance
// says otherwise.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class GsPacketDiff {
public:
    // GS register addresses (the A+D / REGLIST namespace).
    enum : uint8_t {
        REG_PRIM = 0x00, REG_RGBAQ = 0x01, REG_ST = 0x02, REG_UV = 0x03,
        REG_XYZF2 = 0x04, REG_XYZ2 = 0x05, REG_TEX0_1 = 0x06, REG_TEX0_2 = 0x07,
        REG_CLAMP_1 = 0x08, REG_CLAMP_2 = 0x09, REG_FOG = 0x0A, REG_XYZF3 = 0x0C,
        REG_XYZ3 = 0x0D, REG_AD = 0x0E, REG_NOP = 0x0F,
        REG_TEX1_1 = 0x14, REG_TEX1_2 = 0x15, REG_TEX2_1 = 0x16, REG_TEX2_2 = 0x17,
        REG_XYOFFSET_1 = 0x18, REG_XYOFFSET_2 = 0x19, REG_PRMODECONT = 0x1A,
        REG_PRMODE = 0x1B, REG_TEXCLUT = 0x1C, REG_SCANMSK = 0x22,
        REG_MIPTBP1_1 = 0x34, REG_MIPTBP1_2 = 0x35, REG_MIPTBP2_1 = 0x36,
        REG_MIPTBP2_2 = 0x37, REG_TEXA = 0x3B, REG_FOGCOL = 0x3D, REG_TEXFLUSH = 0x3F,
        REG_SCISSOR_1 = 0x40, REG_SCISSOR_2 = 0x41, REG_ALPHA_1 = 0x42,
        REG_ALPHA_2 = 0x43, REG_DIMX = 0x44, REG_DTHE = 0x45, REG_COLCLAMP = 0x46,
        REG_TEST_1 = 0x47, REG_TEST_2 = 0x48, REG_PABE = 0x49, REG_FBA_1 = 0x4A,
        REG_FBA_2 = 0x4B, REG_FRAME_1 = 0x4C, REG_FRAME_2 = 0x4D, REG_ZBUF_1 = 0x4E,
        REG_ZBUF_2 = 0x4F, REG_BITBLTBUF = 0x50, REG_TRXPOS = 0x51, REG_TRXREG = 0x52,
        REG_TRXDIR = 0x53, REG_HWREG = 0x54, REG_SIGNAL = 0x60, REG_FINISH = 0x61,
        REG_LABEL = 0x62,
    };

    struct Write {
        uint8_t addr;
        uint64_t value;
        uint32_t tag_index;
        uint32_t packet_index;
    };
    struct TagInfo {
        uint32_t packet_index;
        uint32_t tag_index;
        uint32_t byte_offset;
        uint16_t nloop;
        bool eop;
        bool pre;
        uint16_t prim;
        uint8_t flg;  // 0 PACKED, 1 REGLIST, 2/3 IMAGE
        uint8_t nreg;
        uint8_t regs[16];
        uint32_t image_bytes;
    };
    struct Stream {
        std::vector<Write> writes;
        std::vector<TagInfo> tags;
        std::vector<std::string> errors;
        uint32_t packets = 0;
        uint32_t image_bytes = 0;
    };
    struct XYZ {
        uint32_t x, y, z;
        uint8_t f;
    };
    struct RGBAQ {
        uint8_t r, g, b, a;
        float q;
    };
    struct Tolerance {
        uint32_t xy = 1;
        uint32_t z = 1;
        uint32_t rgba = 1;
        uint32_t fog = 1;
        uint32_t uv = 1;
        float st_rel = 1e-5f;
        float st_abs = 1e-6f;
        float q_rel = 1e-5f;
        bool strict_packets = false;
        bool ignore_nop_state = true;
    };
    struct DiffResult {
        bool pass = true;
        uint32_t compared = 0;
        uint32_t mismatches = 0;
        uint32_t vertices_a = 0, vertices_b = 0;
        uint32_t kicks_a = 0, kicks_b = 0;
        std::vector<std::string> lines;
        std::string summary;
    };

    static const char* RegName(uint8_t a);
    static bool IsVertexReg(uint8_t a);
    static bool IsKickReg(uint8_t a);

    static void DecodePacket(const uint8_t* p, size_t n, Stream& s);
    static Stream Decode(const std::vector<std::vector<uint8_t>>& packets);

    static XYZ UnpackXYZ(uint8_t addr, uint64_t v);
    static RGBAQ UnpackRGBAQ(uint64_t v);
    static void UnpackST(uint64_t v, float& s, float& t);
    static std::string Format(const Write& w);

    static std::vector<Write> Normalize(const std::vector<Write>& in, const Tolerance& t);
    static DiffResult Diff(const Stream& a, const Stream& b, const Tolerance& t, uint32_t max_lines = 20);
    static void Print(const Stream& s, std::FILE* f);

private:
    static bool PackedToCanonical(uint8_t desc, const uint8_t* q, uint32_t& q_latch, uint8_t& out_addr,
                                  uint64_t& out_val);
    static bool WriteClose(const Write& a, const Write& b, const Tolerance& t, std::string* why);
};
