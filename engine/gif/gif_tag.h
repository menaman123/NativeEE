#pragma once
// The GIFtag (the 128-bit header of every GIF packet) and the GS register
// numbers it names. NLOOP/EOP/PRE/PRIM/FLG/NREG come from the low 64 bits;
// the 16 register descriptors are the nibbles of the high 64 bits. Bits 16-45
// of the low word are reserved and ignored.
#include <cstdint>

// The three GIF input paths: PATH1 = VU1 XGKICK, PATH2 = VIF1 DIRECT/DIRECTHL,
// PATH3 = the GIF DMA channel.
enum class GifPath : uint8_t { Path1 = 1, Path2 = 2, Path3 = 3 };

// GIFtag.FLG: how the NLOOP x NREG payload is encoded.
enum class GifFormat : uint8_t { Packed = 0, Reglist = 1, Image = 2, Disabled = 3 };

// GS general-purpose register numbers (the A+D address byte and the REGLIST /
// PACKED descriptor values). Only registers the GS defines appear here; 0x0B
// is the reserved descriptor the dispatcher treats as a no-op.
enum class GsReg : uint8_t {
    Prim = 0x00,
    Rgbaq = 0x01,
    St = 0x02,
    Uv = 0x03,
    Xyzf2 = 0x04,   // vertex kick with fog
    Xyz2 = 0x05,    // vertex kick
    Tex0_1 = 0x06,
    Tex0_2 = 0x07,
    Clamp_1 = 0x08,
    Clamp_2 = 0x09,
    Fog = 0x0A,
    Reserved0B = 0x0B,
    Xyzf3 = 0x0C,   // vertex without draw
    Xyz3 = 0x0D,    // vertex without draw
    Ad = 0x0E,      // PACKED address + data
    Nop = 0x0F,
    Tex1_1 = 0x14,
    Tex1_2 = 0x15,
    Tex2_1 = 0x16,
    Tex2_2 = 0x17,
    XyOffset_1 = 0x18,
    XyOffset_2 = 0x19,
    Prmodecont = 0x1A,
    Prmode = 0x1B,
    Texclut = 0x1C,
    Scanmsk = 0x22,
    Miptbp1_1 = 0x34,
    Miptbp1_2 = 0x35,
    Miptbp2_1 = 0x36,
    Miptbp2_2 = 0x37,
    Texa = 0x3B,
    Fogcol = 0x3D,
    Texflush = 0x3F,
    Scissor_1 = 0x40,
    Scissor_2 = 0x41,
    Alpha_1 = 0x42,
    Alpha_2 = 0x43,
    Dimx = 0x44,
    Dthe = 0x45,
    Colclamp = 0x46,
    Test_1 = 0x47,
    Test_2 = 0x48,
    Pabe = 0x49,
    Fba_1 = 0x4A,
    Fba_2 = 0x4B,
    Frame_1 = 0x4C,
    Frame_2 = 0x4D,
    Zbuf_1 = 0x4E,
    Zbuf_2 = 0x4F,
    Bitbltbuf = 0x50,
    Trxpos = 0x51,
    Trxreg = 0x52,
    Trxdir = 0x53,
    Hwreg = 0x54,
    Signal = 0x60,
    Finish = 0x61,
    Label = 0x62,
};

// The parsed GIFtag. Field order is the 24-byte layout the register readback
// (GIF_TAG0..3) and the savestate observe: nloop 0, eop 2, pre 3, prim 4,
// flg 6, nregs 7, regs 8..23. Parse() normalises NREG 0 to 16; a zeroed tag
// (after reset) therefore reads nregs 0, not 16.
struct GifTag {
    uint16_t nloop;     // lo bits 0-14
    bool eop;           // lo bit 15
    bool pre;           // lo bit 46
    uint16_t prim;      // lo bits 47-57
    GifFormat flg;      // lo bits 58-59
    uint8_t nregs;      // lo bits 60-63, 0 -> 16
    uint8_t regs[16];   // hi nibbles, regs[i] = (hi >> 4i) & 0xF, all 16 always filled

    void Parse(uint64_t lo, uint64_t hi);
};

// Descriptor-set classification of a PACKED tag, computed once per accepted
// tag and consulted only on the PACKED branch: which bulk lane may consume it.
struct GifTagClass {
    bool has_ad;      // any descriptor is A+D (0x0E)
    bool all_known;   // every descriptor is PRIM/RGBAQ/ST/UV/XYZF2/XYZ2/FOG/XYZF3/XYZ3/NOP
    bool strip3;      // exactly {ST, RGBAQ, XYZ2|XYZF2}
    bool rowable;     // all_known && !has_ad && !strip3
};

GifTagClass ClassifyGifTag(const GifTag& tag);

// The in-flight transfer the register file reads back through GIF_TAG0-3,
// GIF_CNT and GIF_STAT.OPH/APATH: the accepted tag, loops completed and the
// next descriptor index. Owned by GifRegs, advanced by the parser.
struct GifTransferState {
    GifTag tag;
    uint32_t loop;        // loops completed for `tag`
    uint32_t reg;         // next descriptor index within the loop
    bool in_transfer;     // a tag is accepted and not yet complete
};
