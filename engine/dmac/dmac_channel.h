#pragma once
// Types only (no primary class): the EE DMAC channel register POD, the channel
// ids, and the CHCR / D_STAT bit constants and register-map offsets shared by
// the register file, the chain walker and the savestate codec.
//
// PS2 rules: ten channels, each with CHCR (DIR, MOD, ASP, TTE, TIE, STR and
// the mirrored TAG half in bits 16-31), MADR, QWC, TADR, ASR0/1 and, for the
// scratchpad channels, SADR; D_STAT holds the channel-interrupt status bits
// (CIS, low half) and their masks (CIM, high half).
#include <cstdint>

enum class DmacChannelId : int {
    Vif0 = 0,
    Vif1 = 1,
    Gif = 2,
    IpuFrom = 3,  // IPU output FIFO -> RAM
    IpuTo = 4,    // RAM -> IPU input FIFO
    Sif0 = 5,     // IOP -> EE
    Sif1 = 6,     // EE -> IOP
    Sif2 = 7,
    SprFrom = 8,  // scratchpad -> RAM
    SprTo = 9,    // RAM -> scratchpad
    Count = 10
};
constexpr int kDmacChannelCount = 10;

// One channel's register file; 7 x u32, no padding -- the savestate raw layout.
struct DmacChannelRegs {
    uint32_t chcr = 0;
    uint32_t madr = 0;
    uint32_t qwc = 0;
    uint32_t tadr = 0;
    uint32_t asr0 = 0;
    uint32_t asr1 = 0;
    uint32_t sadr = 0;
};
static_assert(sizeof(DmacChannelRegs) == 28, "DmacChannelRegs is the frozen 28-byte savestate record");

// CHCR bits.
namespace DmacChcr {
constexpr uint32_t kDir = 1u << 0;
constexpr uint32_t kModShift = 2;
constexpr uint32_t kModMask = 3u << kModShift;
constexpr uint32_t kModNormal = 0;
constexpr uint32_t kModChain = 1;
constexpr uint32_t kAspShift = 4;
constexpr uint32_t kAspMask = 3u << kAspShift;
constexpr uint32_t kTte = 1u << 6;
constexpr uint32_t kTie = 1u << 7;
constexpr uint32_t kStr = 1u << 8;
constexpr uint32_t kTagMask = 0xFFFF0000u;
constexpr uint32_t Mode(uint32_t chcr) { return (chcr >> kModShift) & 3u; }
constexpr uint32_t Asp(uint32_t chcr) { return (chcr >> kAspShift) & 3u; }
constexpr uint32_t WithAsp(uint32_t chcr, uint32_t asp) { return (chcr & ~kAspMask) | (asp << kAspShift); }
}  // namespace DmacChcr

// D_STAT bits: CIS(ch) low half, CIM(ch) high half.
namespace DmacStat {
constexpr uint32_t kBeis = 1u << 15;  // bus error interrupt (not maskable)
constexpr uint32_t kStatusWriteMask = 0x0000E3FFu;  // CIS0..9, SIS, MEIS, BEIS
constexpr uint32_t kMaskWriteMask = 0x63FF0000u;    // CIM0..9, SIM, MEIM
constexpr uint32_t kCisMask = 0xFFFFu;
constexpr uint32_t kCimShift = 16;
constexpr uint32_t Cis(int ch) { return 1u << ch; }
constexpr uint32_t Cim(int ch) { return 1u << (kCimShift + ch); }
}  // namespace DmacStat

// Register offsets inside a channel page.
namespace DmacRegOffset {
constexpr uint32_t kChcr = 0x00u;
constexpr uint32_t kMadr = 0x10u;
constexpr uint32_t kQwc = 0x20u;
constexpr uint32_t kTadr = 0x30u;
constexpr uint32_t kAsr0 = 0x40u;
constexpr uint32_t kAsr1 = 0x50u;
constexpr uint32_t kSadr = 0x80u;
}  // namespace DmacRegOffset

// Physical register addresses (the 0x1000xxxx page).
namespace DmacAddr {
constexpr uint32_t kBase = 0x10008000u;
constexpr uint32_t kVif0 = 0x10008000u;
constexpr uint32_t kVif1 = 0x10009000u;
constexpr uint32_t kGif = 0x1000A000u;
constexpr uint32_t kIpuFrom = 0x1000B000u;
constexpr uint32_t kIpuTo = 0x1000B400u;
constexpr uint32_t kSif0 = 0x1000C000u;
constexpr uint32_t kSif1 = 0x1000C400u;
constexpr uint32_t kSif2 = 0x1000C800u;
constexpr uint32_t kSprFrom = 0x1000D000u;
constexpr uint32_t kSprTo = 0x1000D400u;
constexpr uint32_t kCtrl = 0x1000E000u;
constexpr uint32_t kStat = 0x1000E010u;
constexpr uint32_t kPcr = 0x1000E020u;
constexpr uint32_t kSqwc = 0x1000E030u;
constexpr uint32_t kRbsr = 0x1000E040u;
constexpr uint32_t kRbor = 0x1000E050u;
constexpr uint32_t kStadr = 0x1000E060u;
constexpr uint32_t kEnabler = 0x1000F520u;
constexpr uint32_t kEnablew = 0x1000F590u;
}  // namespace DmacAddr

// DMAtag ids (bits 28-30 of the low tag word).
namespace DmacTagId {
constexpr uint32_t kRefe = 0;
constexpr uint32_t kCnt = 1;
constexpr uint32_t kNext = 2;
constexpr uint32_t kRef = 3;
constexpr uint32_t kRefs = 4;
constexpr uint32_t kCall = 5;
constexpr uint32_t kRet = 6;
constexpr uint32_t kEnd = 7;
}  // namespace DmacTagId
