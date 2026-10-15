#pragma once
// The VIF register file and the plain records of one VIF unit. Zero primary
// types (PODs, enums and constants only).
//
// PS2 rules: each VIF (VIF0 at 0x10003800, VIF1 at 0x10003C00) exposes 32-bit
// registers at a 16-byte stride -- STAT, FBRST, ERR, MARK, CYCLE, MODE, NUM,
// MASK, CODE, ITOPS, BASE, OFST, TOPS, ITOP, TOP -- then R0-R3 (ROW) at +0x100
// and C0-C3 (COL) at +0x140. CYCLE holds CL in bits 0-7 and WL in bits 8-15.
// The field order of `VifRegs` is the frozen savestate section layout (92 bytes
// per unit) and must not change.
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

struct VifRegs {
    uint32_t stat;
    uint32_t fbrst;
    uint32_t err;
    uint32_t mark;
    uint32_t cycle;
    uint32_t mode;
    uint32_t num;
    uint32_t mask;
    uint32_t code;
    uint32_t itops;
    uint32_t base;
    uint32_t ofst;
    uint32_t tops;
    uint32_t itop;
    uint32_t top;
    uint32_t row[4];
    uint32_t col[4];
};
static_assert(sizeof(VifRegs) == 92, "VifRegs is the frozen 23-word savestate layout");

inline uint32_t VifRegsCl(const VifRegs& r) { return r.cycle & 0xFFu; }
inline uint32_t VifRegsWl(const VifRegs& r) { return (r.cycle >> 8) & 0xFFu; }

// Register offsets from a unit's base (16-byte stride; ROW/COL at word stride).
namespace VifRegOffset {
constexpr uint32_t kStat = 0x00u;
constexpr uint32_t kFbrst = 0x10u;
constexpr uint32_t kErr = 0x20u;
constexpr uint32_t kMark = 0x30u;
constexpr uint32_t kCycle = 0x40u;
constexpr uint32_t kMode = 0x50u;
constexpr uint32_t kNum = 0x60u;
constexpr uint32_t kMask = 0x70u;
constexpr uint32_t kCode = 0x80u;
constexpr uint32_t kItops = 0x90u;
constexpr uint32_t kBase = 0xA0u;
constexpr uint32_t kOfst = 0xB0u;
constexpr uint32_t kTops = 0xC0u;
constexpr uint32_t kItop = 0xD0u;
constexpr uint32_t kTop = 0xE0u;
constexpr uint32_t kRow0 = 0x100u;  // R0..R3 at 0x100, 0x104, 0x108, 0x10C
constexpr uint32_t kCol0 = 0x140u;  // C0..C3 at 0x140, 0x144, 0x148, 0x14C
}  // namespace VifRegOffset

// VIF_STAT bits.
namespace VifStat {
constexpr uint32_t kVpsMask = 0x3u;   // bits 0-1: the command state
constexpr uint32_t kVpsIdle = 0x0u;
constexpr uint32_t kVpsWait = 0x1u;      // waiting for data
constexpr uint32_t kVpsDecode = 0x2u;    // decoding a VIFcode
constexpr uint32_t kVpsTransfer = 0x3u;  // transferring data
constexpr uint32_t kVew = 1u << 2;   // waiting for the VU program to end
constexpr uint32_t kVgw = 1u << 3;   // waiting for the GIF
constexpr uint32_t kMrk = 1u << 6;   // MARK written
constexpr uint32_t kDbf = 1u << 7;   // double-buffer flag
constexpr uint32_t kVss = 1u << 8;   // stopped by STP
constexpr uint32_t kVfs = 1u << 9;   // stopped by force break
constexpr uint32_t kVis = 1u << 10;  // stopped by an interrupt
constexpr uint32_t kInt = 1u << 11;  // i-bit interrupt
constexpr uint32_t kEr0 = 1u << 12;  // DMAtag mismatch
constexpr uint32_t kEr1 = 1u << 13;  // undefined VIFcode
constexpr uint32_t kFdr = 1u << 23;  // FIFO direction (VIF1 -> memory)
constexpr uint32_t kFqcShift = 24;   // bits 24-28: FIFO quadword count
constexpr uint32_t kFqcMask = 0x1Fu;
}  // namespace VifStat

// VIF_ERR bits.
namespace VifErr {
constexpr uint32_t kMii = 1u;  // mask the i-bit interrupt
constexpr uint32_t kMe0 = 2u;  // mask the DMAtag mismatch error
constexpr uint32_t kMe1 = 4u;  // mask the undefined-VIFcode error
}  // namespace VifErr

// VIF_FBRST bits.
namespace VifFbrst {
constexpr uint32_t kRst = 1u;  // reset the VIF
constexpr uint32_t kFbk = 2u;  // force break
constexpr uint32_t kStp = 4u;  // stop
constexpr uint32_t kStc = 8u;  // clear the stall / error bits
}  // namespace VifFbrst

// VIFcode command byte (bits 24-30 of the code word). UNPACK is 0x60-0x7F
// (bit 4 = masked, bits 0-3 = format).
enum class VifCommand : uint8_t {
    Nop = 0x00,
    Stcycl = 0x01,
    Offset = 0x02,
    Base = 0x03,
    Itop = 0x04,
    Stmod = 0x05,
    Mskpath3 = 0x06,
    Mark = 0x07,
    Flushe = 0x10,
    Flush = 0x11,
    Flusha = 0x13,
    Mscal = 0x14,
    Mscalf = 0x15,
    Mscnt = 0x17,
    Stmask = 0x20,
    Strow = 0x30,
    Stcol = 0x31,
    Mpg = 0x4A,
    Direct = 0x50,
    Directhl = 0x51,
};

// One 2-bit MASK entry: what a masked UNPACK writes to a field.
enum class MaskAction : uint8_t {
    Write = 0,    // the unpacked value
    NoWrite = 1,  // the existing VU memory content
    Row = 2,      // the ROW register of that field
    Col = 3,      // the COL register of that write cycle
};

// The command-processor state of one unit.
enum class VifState : uint8_t {
    Idle,          // decoding VIFcodes
    WaitData,      // a command's payload is incomplete
    DirectStream,  // DIRECT/DIRECTHL quadwords are streaming to PATH2
};

// Everything one VIF unit carries besides the register file.
struct VifUnitState {
    VifRegs regs{};
    // The DMA word feed accumulates 4 words into one FIFO quadword.
    std::array<uint32_t, 4> latch{};
    uint32_t latch_fill = 0;
    std::deque<std::array<uint32_t, 4>> fifo;
    // The parser's cursor inside the quadword being decoded (4 = none held).
    std::array<uint32_t, 4> current_qw{};
    int word_index = 4;
    VifState state = VifState::Idle;
    uint32_t pending_cmd = 0;
    uint32_t pending_bytes = 0;
    uint32_t pending_num = 0;
    uint32_t pending_imm = 0;
    bool pending_irq = false;
    bool pending_use_mask = false;
    bool pending_directhl = false;
    // Parity of the payload's first word inside its quadword (V2-32 / V3-32 W rule).
    uint8_t unpack_phase = 0;
    // True while the DMAC feeds a tag's TTE quadword (its pad words must not be consumed as payload).
    bool tte_feed = false;
    std::vector<uint8_t> direct_buffer;
    std::vector<uint8_t> payload;  // scratch for the command being executed
};
