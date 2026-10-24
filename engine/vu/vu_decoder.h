#pragma once
// THE ONE VU instruction decode. Every tier (interpreter, cached) reasons
// over the same decoded slot: the raw 64-bit bundle split into its upper
// (FMAC) and lower (LSU/IALU/FDIV/EFU/branch) words, the fast-path kinds, the
// VF read/write sets with lane masks (the FMAC read-stall model keys on them),
// the VI write / branch-read sets (the IALU stall model), the FDIV/EFU pipe
// markers, and the same-bundle hazards between the two words.
//
// PS2 encoding rules (VU User's Manual ch. 3): upper `func = bits 0-5`,
// `fd = 6-10`, `fs = 11-15`, `ft = 16-20`, `dest = 21-24` (X=8 Y=4 Z=2 W=1),
// bit 30 = E (end after the next bundle), bit 31 = I (the lower word is a
// 32-bit immediate loaded into I); broadcast field `bc = func & 3`. Lower
// `opcode = bits 25-31`, `it/ft = 16-20`, `is/fs = 11-15`, `id/fd = 6-10`,
// `dest = 21-24`, `fsf = 21-22`, `ftf = 23-24`, `imm11 = 0-10` (signed),
// `imm15 = {bits 21-24, bits 0-10}`; the 0x40 group dispatches on `fn = bits
// 0-5` and, for fn >= 0x3C, on `funct2 = bits 6-10` with `t3 = fn - 0x3C`.
#include <cstddef>
#include <cstdint>

// Dest-mask bits and the broadcast lane selector (bc 0..3 = x,y,z,w).
namespace vu_dest {
constexpr uint8_t X = 0x8, Y = 0x4, Z = 0x2, W = 0x1, XYZW = 0xF;
}

// STATUS flag bits: 0-5 Z S U O I D (current), 6-11 the sticky twins.
namespace vu_status {
constexpr uint16_t Z = 1u << 0, S = 1u << 1, U = 1u << 2, O = 1u << 3, I = 1u << 4, D = 1u << 5;
constexpr uint16_t ZS = 1u << 6, SS = 1u << 7, US = 1u << 8, OS = 1u << 9, IS = 1u << 10, DS = 1u << 11;
}

// MAC flag nibbles: bits 0-3 Z, 4-7 S, 8-11 U, 12-15 O; within each nibble
// bit0 = W, bit1 = Z, bit2 = Y, bit3 = X.
namespace vu_mac {
constexpr uint16_t ZW = 1u << 0, ZZ = 1u << 1, ZY = 1u << 2, ZX = 1u << 3;
constexpr uint16_t SW = 1u << 4, SZ = 1u << 5, SY = 1u << 6, SX = 1u << 7;
constexpr uint16_t UW = 1u << 8, UZ = 1u << 9, UY = 1u << 10, UX = 1u << 11;
constexpr uint16_t OW = 1u << 12, OZ = 1u << 13, OY = 1u << 14, OX = 1u << 15;
}

// Fast-path kinds of the upper word (what the cached tier dispatches on).
enum : uint8_t {
    VUK_U_GENERIC = 0,
    VUK_U_NOP,       // func 0x3F, op 0x0B
    VUK_U_ADD_BC,    // 0x00-0x03
    VUK_U_SUB_BC,    // 0x04-0x07
    VUK_U_MUL_BC,    // 0x18-0x1B
    VUK_U_MAX_BC,    // 0x10-0x13
    VUK_U_MINI_BC,   // 0x14-0x17
    VUK_U_FTOI0,     // func 0x3C, op 0x05
    VUK_U_ADD,       // 0x28
    VUK_U_MADD_BC,   // 0x08-0x0B
    VUK_U_MSUB_BC,   // 0x0C-0x0F
    VUK_U_MADD,      // 0x29
    VUK_U_MSUB,      // 0x2D
    VUK_U_MUL,       // 0x2A
    VUK_U_SUB,       // 0x2C
    VUK_U_MULA_BC,   // special2 op 0x06
    VUK_U_MADDA_BC,  // special2 op 0x02
    VUK_U_MSUBA_BC,  // special2 op 0x03
    VUK_U_MULA,      // special2 op 0x0A, bc 2
    VUK_U_MADDA,     // special2 op 0x0A, bc 1
    VUK_U_MULQ,      // 0x1C
    VUK_U_MAX,       // 0x2B
    VUK_U_MINI_I,    // 0x1F
    VUK_U_MUL_I,     // 0x1E
    VUK_U_ADD_I,     // 0x22
    VUK_U_FTOI4,     // special2 op 0x05, bc 1
    VUK_U_CLIP,      // special2 op 0x07, bc 3
    VUK_U_ITOF0,     // special2 op 0x04, bc 0
    VUK_U_MINI,      // 0x2F
    VUK_U_ADDA_BC,   // special2 op 0x00
    VUK_U_SUBA_BC,   // special2 op 0x01
    VUK_U_ITOF_N,    // special2 op 0x04, bc 1..3 (ITOF4 / ITOF12 / ITOF15: u_bc selects the scale)
    VUK_U_FTOI_N,    // special2 op 0x05, bc 2..3 (FTOI12 / FTOI15: u_bc selects the scale)
    VUK_U_COUNT
};

// Fast-path kinds of the lower word.
enum : uint8_t {
    VUK_L_GENERIC = 0,
    VUK_L_NOP,     // MOVE with ft == 0
    VUK_L_LQ,      // 0x00
    VUK_L_SQ,      // 0x01
    VUK_L_ILW,     // 0x04
    VUK_L_IADDIU,  // 0x08
    VUK_L_IBEQ,    // 0x28
    VUK_L_IBNE,    // 0x29
    VUK_L_IADD,    // 0x40 fn 0x30
    VUK_L_IAND,    // fn 0x34
    VUK_L_MOVE,    // fn 0x3C funct2 0x0C, ft != 0
    VUK_L_MTIR,    // fn 0x3C funct2 0x0F
    VUK_L_ISUBIU,  // 0x09
    VUK_L_ISUB,    // fn 0x31
    VUK_L_LQI,     // fn 0x3C funct2 0x0D
    VUK_L_MR32,    // fn 0x3D funct2 0x0C, ft != 0
    VUK_L_MFIR,    // fn 0x3D funct2 0x0F
    VUK_L_RGET,    // fn 0x3D funct2 0x10
    VUK_L_RINIT,   // fn 0x3E funct2 0x10
    VUK_L_ISW,     // 0x05
    VUK_L_SQI,     // fn 0x3D funct2 0x0D
    VUK_L_JR,      // 0x24
    VUK_L_JALR,    // 0x25
    VUK_L_B,       // 0x20
    VUK_L_BAL,     // 0x21
    VUK_L_IOR,     // fn 0x35
    VUK_L_IADDI,   // fn 0x32
    VUK_L_DIV,     // fn 0x3C funct2 0x0E
    VUK_L_FCGET,   // 0x1C
    VUK_L_WAITQ,   // fn 0x3F funct2 0x0E
    VUK_L_FCEQ,    // 0x10 (imm24 = lower & 0xFFFFFF, read at execution)
    VUK_L_FCSET,   // 0x11
    VUK_L_FCAND,   // 0x12
    VUK_L_FCOR,    // 0x13
    VUK_L_FSEQ,    // 0x14 (imm12 = {bit 21, bits 0-10}, read at execution)
    VUK_L_FSSET,   // 0x15
    VUK_L_FSAND,   // 0x16
    VUK_L_FSOR,    // 0x17
    VUK_L_FMEQ,    // 0x18
    VUK_L_FMAND,   // 0x1A
    VUK_L_FMOR,    // 0x1B
    VUK_L_IBLTZ,   // 0x2C
    VUK_L_IBGTZ,   // 0x2D
    VUK_L_IBLEZ,   // 0x2E
    VUK_L_IBGEZ,   // 0x2F
    VUK_L_XTOP,    // fn 0x3C funct2 0x1A
    VUK_L_XITOP,   // fn 0x3D funct2 0x1A
    VUK_L_XGKICK,  // fn 0x3C funct2 0x1B
    VUK_L_COUNT
};

// One decoded bundle. Field order and size are pinned (60 bytes): the cached
// tier's program tables and its superblock pass reason over this layout.
struct VuDecodedSlot {
    uint32_t upper = 0;
    uint32_t lower = 0;
    int8_t upper_vf_write = -1;  // VF the upper writes, -1 none (VF0 never counts)
    bool i_bit = false;          // upper bit 31: lower word is an immediate
    bool e_bit = false;          // upper bit 30: end after the next bundle
    bool is_nop_pair = false;    // both words zero
    bool discard_lower = false;  // upper and lower write the same VF: the lower's write is dropped
    bool hide_upper = false;     // the lower reads the VF the upper writes: it sees the old value
    uint8_t upper_kind = VUK_U_GENERIC;
    uint8_t lower_kind = VUK_L_GENERIC;
    uint8_t n_rd = 0;            // VF read-set size (<= 5)
    uint8_t n_wr = 0;            // VF write-set size (<= 2)
    uint8_t rd_reg[5] = {};
    uint8_t rd_mask[5] = {};     // lane masks XYZW = 8/4/2/1
    uint8_t wr_reg[2] = {};
    uint8_t wr_mask[2] = {};
    uint32_t rd_regs = 0;        // OR of 1u << rd_reg[i] (set even past the 5-entry table)
    int8_t ialu_wr_vi = -1;      // VI written by ILW / ILWR (the 4-cycle IALU load latency), -1 none
    uint8_t fold_class = 0;      // superblock fold class: 0 none, 15 full fold, 16 writer fold
    uint16_t ialu_branch_rd = 0; // VI bitmask a branch reads
    bool efu_pipe_op = false;    // lower is an EFU op or WAITP
    bool fdiv_pipe_op = false;   // lower is DIV/SQRT/RSQRT or WAITQ
    uint8_t u_fd = 0, u_fs = 0, u_ft = 0, u_dest = 0, u_bc = 0;
    uint8_t l_fs = 0, l_ft = 0, l_fd = 0, l_dest = 0, l_fsf = 0;
    int16_t l_imm11 = 0;         // sign-extended 11-bit immediate
    uint16_t l_imm15u = 0;       // IADDIU / ISUBIU 15-bit immediate
    uint8_t touch_hw = 1;        // may write VF0 / VI0 (superblock pass)
    uint8_t stall_class = 0;     // static stall class, 0 = dynamic
    uint16_t run_len = 0;        // straight-line bundles from here
};

static_assert(sizeof(VuDecodedSlot) == 60, "VuDecodedSlot layout is pinned");
static_assert(offsetof(VuDecodedSlot, rd_regs) == 32, "VuDecodedSlot.rd_regs offset");
static_assert(offsetof(VuDecodedSlot, ialu_branch_rd) == 38, "VuDecodedSlot.ialu_branch_rd offset");
static_assert(offsetof(VuDecodedSlot, l_imm11) == 52, "VuDecodedSlot.l_imm11 offset");
static_assert(offsetof(VuDecodedSlot, run_len) == 58, "VuDecodedSlot.run_len offset");

class VuDecoder {
public:
    // The full decode of one bundle; NOTE the argument order (lower word
    // first: it is the low 32 bits at the bundle's byte address).
    static VuDecodedSlot DecodeVuSlot(uint32_t lower, uint32_t upper);

    // VF read/write sets, VI write / branch-read sets, pipe markers.
    static void DecodeStallStatics(VuDecodedSlot& s);
    // Operand fields, immediates and the fast-path kinds.
    static void ClassifyFastKinds(VuDecodedSlot& s);

    // May the word write the hard-wired VF0 / VI0 (or is it unrecognised)?
    static bool VuUpperCanTouchHardwired(uint32_t u);
    static bool VuLowerCanTouchHardwired(uint32_t l);
    static bool VuSlotCanTouchHardwired(const VuDecodedSlot& s);

    // Every lower kind that takes a branch (B/BAL/JR/JALR and the six conditional forms).
    static bool IsBranchKind(uint8_t lower_kind);
    // Lower kinds that end a cached-tier superblock run: the branches plus the
    // run-state side effects the per-bundle path owns (XTOP/XITOP's yield
    // rule, XGKICK). GENERIC is terminal by itself.
    static bool IsRunTerminalLowerKind(uint8_t lower_kind);
};
