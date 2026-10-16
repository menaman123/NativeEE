// VuDecodeTable: the ONE decode on hand-encoded bundles -- operand fields,
// fast kinds, VF read/write sets with lane masks, VI write / branch-read sets,
// FDIV/EFU pipe markers, same-bundle discard/hide hazards, the hard-wired
// predicates, IsBranchKind, the 60-byte layout pin; and the SpanClassifier
// passes over a small straight-line program.
#include <cstddef>
#include <cstdio>
#include <vector>

#include "engine/vu/vu_decoder.h"
#include "engine/vu/span_classifier.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

static constexpr uint32_t kUNop = 0x000002FFu;  // func 0x3F op 0x0B
static constexpr uint32_t kLNop = 0x8000033Cu;  // MOVE vf0,vf0 -> classified NOP
static constexpr uint32_t kEBit = 0x40000000u;
static constexpr uint32_t kIBit = 0x80000000u;

static uint32_t Up(uint32_t func, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t dest) {
    return (func & 0x3Fu) | (fd << 6) | (fs << 11) | (ft << 16) | (dest << 21);
}
// Lower with the 7-bit opcode; `low` fills bits 0-10 (imm11 or the 0x40 group's fd/fn bits).
static uint32_t Lo(uint32_t opcode, uint32_t ft, uint32_t fs, uint32_t dest, uint32_t low11) {
    return (opcode << 25) | (dest << 21) | (ft << 16) | (fs << 11) | (low11 & 0x7FFu);
}
// The 0x40 group: fn in bits 0-5, funct2 (or fd) in bits 6-10.
static uint32_t Lo40(uint32_t ft, uint32_t fs, uint32_t dest, uint32_t funct2, uint32_t fn) {
    return Lo(0x40, ft, fs, dest, (funct2 << 6) | fn);
}
static uint32_t LoDiv(uint32_t fs, uint32_t fsf, uint32_t ft, uint32_t ftf, uint32_t fn) {
    return (0x40u << 25) | (ftf << 23) | (fsf << 21) | (ft << 16) | (fs << 11) | (0x0Eu << 6) | fn;
}

static VuDecodedSlot Slot(uint32_t lower, uint32_t upper) { return VuDecoder::DecodeVuSlot(lower, upper); }

static bool HasRead(const VuDecodedSlot& s, uint32_t reg, uint32_t mask) {
    for (int i = 0; i < s.n_rd; ++i)
        if (s.rd_reg[i] == reg && s.rd_mask[i] == mask) return true;
    return false;
}
static bool HasWrite(const VuDecodedSlot& s, uint32_t reg, uint32_t mask) {
    for (int i = 0; i < s.n_wr; ++i)
        if (s.wr_reg[i] == reg && s.wr_mask[i] == mask) return true;
    return false;
}

static void TestLayout() {
    CHECK(sizeof(VuDecodedSlot) == 60);
    CHECK(offsetof(VuDecodedSlot, upper) == 0);
    CHECK(offsetof(VuDecodedSlot, upper_vf_write) == 8);
    CHECK(offsetof(VuDecodedSlot, upper_kind) == 14);
    CHECK(offsetof(VuDecodedSlot, rd_reg) == 18);
    CHECK(offsetof(VuDecodedSlot, wr_mask) == 30);
    CHECK(offsetof(VuDecodedSlot, rd_regs) == 32);
    CHECK(offsetof(VuDecodedSlot, ialu_wr_vi) == 36);
    CHECK(offsetof(VuDecodedSlot, efu_pipe_op) == 40);
    CHECK(offsetof(VuDecodedSlot, u_fd) == 42);
    CHECK(offsetof(VuDecodedSlot, l_fs) == 47);
    CHECK(offsetof(VuDecodedSlot, l_imm15u) == 54);
    CHECK(offsetof(VuDecodedSlot, touch_hw) == 56);
    CHECK(offsetof(VuDecodedSlot, run_len) == 58);
    CHECK(VUK_U_COUNT == 33 && VUK_L_COUNT == 48);  // appended kinds keep every earlier number
    CHECK(VUK_U_ITOF0 == 27 && VUK_L_WAITQ == 29 && VUK_U_NOP == 1 && VUK_L_NOP == 1);
}

static void TestNops() {
    VuDecodedSlot z = Slot(0, 0);
    CHECK(z.is_nop_pair && !z.i_bit && !z.e_bit);
    CHECK(z.upper_kind == VUK_U_ADD_BC);   // func 0 = ADDx with every field 0
    CHECK(z.lower_kind == VUK_L_LQ);        // opcode 0 = LQ vf0
    CHECK(z.n_rd == 0 && z.n_wr == 0 && z.upper_vf_write == -1);
    VuDecodedSlot n = Slot(kLNop, kUNop);
    CHECK(!n.is_nop_pair);
    CHECK(n.upper_kind == VUK_U_NOP && n.lower_kind == VUK_L_NOP);
    CHECK(n.n_rd == 0 && n.n_wr == 0 && n.upper_vf_write == -1 && !n.discard_lower && !n.hide_upper);
    CHECK(!n.efu_pipe_op && !n.fdiv_pipe_op && n.ialu_wr_vi == -1 && n.ialu_branch_rd == 0);
    VuDecodedSlot e = Slot(kLNop, kUNop | kEBit);
    CHECK(e.e_bit && e.upper_kind == VUK_U_NOP);
}

static void TestUpperFamilies() {
    // ADDx.xyzw vf3, vf1, vf2
    VuDecodedSlot a = Slot(kLNop, Up(0x00, 3, 1, 2, 0xF));
    CHECK(a.upper_kind == VUK_U_ADD_BC && a.u_fd == 3 && a.u_fs == 1 && a.u_ft == 2 && a.u_dest == 0xF && a.u_bc == 0);
    CHECK(a.n_rd == 2 && HasRead(a, 1, 0xF) && HasRead(a, 2, 0x8));
    CHECK(a.n_wr == 1 && HasWrite(a, 3, 0xF) && a.rd_regs == 0x6u && a.upper_vf_write == 3);
    // MADDw.xyz vf4, vf5, vf6 -> broadcast lane W
    VuDecodedSlot m = Slot(kLNop, Up(0x0B, 4, 5, 6, 0xE));
    CHECK(m.upper_kind == VUK_U_MADD_BC && m.u_bc == 3 && HasRead(m, 5, 0xE) && HasRead(m, 6, 0x1) && HasWrite(m, 4, 0xE));
    CHECK(Slot(kLNop, Up(0x04, 1, 2, 3, 0xF)).upper_kind == VUK_U_SUB_BC);
    CHECK(Slot(kLNop, Up(0x0C, 1, 2, 3, 0xF)).upper_kind == VUK_U_MSUB_BC);
    CHECK(Slot(kLNop, Up(0x10, 1, 2, 3, 0xF)).upper_kind == VUK_U_MAX_BC);
    CHECK(Slot(kLNop, Up(0x14, 1, 2, 3, 0xF)).upper_kind == VUK_U_MINI_BC);
    CHECK(Slot(kLNop, Up(0x18, 1, 2, 3, 0xF)).upper_kind == VUK_U_MUL_BC);
    // MULq.xyz vf9, vf9 (0x1C): q form reads fs only, writes fd
    VuDecodedSlot q = Slot(kLNop, Up(0x1C, 9, 9, 0, 0xE));
    CHECK(q.upper_kind == VUK_U_MULQ && q.n_rd == 1 && HasRead(q, 9, 0xE) && HasWrite(q, 9, 0xE));
    CHECK(Slot(kLNop, Up(0x1D, 1, 2, 0, 0xF)).upper_kind == VUK_U_GENERIC);  // MAXi
    CHECK(Slot(kLNop, Up(0x1E, 1, 2, 0, 0xF)).upper_kind == VUK_U_MUL_I);
    CHECK(Slot(kLNop, Up(0x1F, 1, 2, 0, 0xF)).upper_kind == VUK_U_MINI_I);
    CHECK(Slot(kLNop, Up(0x22, 1, 2, 0, 0xF)).upper_kind == VUK_U_ADD_I);
    CHECK(Slot(kLNop, Up(0x27, 1, 2, 0, 0xF)).upper_kind == VUK_U_GENERIC);  // MSUBi
    // Vector forms 0x28-0x2F
    CHECK(Slot(kLNop, Up(0x28, 1, 2, 3, 0xF)).upper_kind == VUK_U_ADD);
    CHECK(Slot(kLNop, Up(0x29, 1, 2, 3, 0xF)).upper_kind == VUK_U_MADD);
    CHECK(Slot(kLNop, Up(0x2A, 1, 2, 3, 0xF)).upper_kind == VUK_U_MUL);
    CHECK(Slot(kLNop, Up(0x2B, 1, 2, 3, 0xF)).upper_kind == VUK_U_MAX);
    CHECK(Slot(kLNop, Up(0x2C, 1, 2, 3, 0xF)).upper_kind == VUK_U_SUB);
    CHECK(Slot(kLNop, Up(0x2D, 1, 2, 3, 0xF)).upper_kind == VUK_U_MSUB);
    CHECK(Slot(kLNop, Up(0x2F, 1, 2, 3, 0xF)).upper_kind == VUK_U_MINI);
    VuDecodedSlot v = Slot(kLNop, Up(0x28, 7, 1, 2, 0x9));
    CHECK(HasRead(v, 1, 0x9) && HasRead(v, 2, 0x9) && HasWrite(v, 7, 0x9));
    // OPMSUB always touches xyz regardless of dest
    VuDecodedSlot o = Slot(kLNop, Up(0x2E, 4, 5, 6, 0xF));
    CHECK(o.upper_kind == VUK_U_GENERIC && HasRead(o, 5, 0xE) && HasRead(o, 6, 0xE) && HasWrite(o, 4, 0xE));
    // Unused 0x30-0x3B: reads only
    VuDecodedSlot un = Slot(kLNop, Up(0x31, 4, 5, 6, 0xF));
    CHECK(un.upper_kind == VUK_U_GENERIC && un.n_rd == 2 && un.n_wr == 0 && un.upper_vf_write == -1);
    // VF0 never enters a set; fd == 0 is no write
    VuDecodedSlot z = Slot(kLNop, Up(0x28, 0, 0, 2, 0xF));
    CHECK(z.n_rd == 1 && HasRead(z, 2, 0xF) && z.n_wr == 0 && z.upper_vf_write == -1 && z.rd_regs == 0x4u);
    // The 5-entry read table saturates but rd_regs keeps counting (CLIP + 4 more is impossible; use OPMSUB + lower DIV)
    VuDecodedSlot many = Slot(LoDiv(10, 3, 11, 0, 0x3C), Up(0x2E, 4, 5, 6, 0xF));
    CHECK(many.n_rd == 4 && (many.rd_regs & ((1u << 5) | (1u << 6) | (1u << 10) | (1u << 11))) == ((1u << 5) | (1u << 6) | (1u << 10) | (1u << 11)));
}

static void TestSpecial2() {
    // FTOI0.xyz vf7, vf9 (func 0x3C, op 0x05)
    VuDecodedSlot f0 = Slot(kLNop, Up(0x3C, 0x05, 9, 7, 0xE));
    CHECK(f0.upper_kind == VUK_U_FTOI0 && HasRead(f0, 9, 0xE) && HasWrite(f0, 7, 0xE) && f0.upper_vf_write == 7);
    CHECK(Slot(kLNop, Up(0x3D, 0x05, 9, 7, 0xE)).upper_kind == VUK_U_FTOI4);
    CHECK(Slot(kLNop, Up(0x3E, 0x05, 9, 7, 0xE)).upper_kind == VUK_U_FTOI_N);  // FTOI12 (u_bc 2 = the scale)
    CHECK(Slot(kLNop, Up(0x3F, 0x05, 9, 7, 0xE)).upper_kind == VUK_U_FTOI_N && Slot(kLNop, Up(0x3F, 0x05, 9, 7, 0xE)).u_bc == 3);  // FTOI15
    CHECK(Slot(kLNop, Up(0x3E, 0x05, 9, 7, 0xE)).upper_vf_write == 7);
    CHECK(Slot(kLNop, Up(0x3C, 0x04, 9, 7, 0xE)).upper_kind == VUK_U_ITOF0);
    CHECK(Slot(kLNop, Up(0x3D, 0x04, 9, 7, 0xE)).upper_kind == VUK_U_ITOF_N);  // ITOF4 (u_bc 1)
    CHECK(Slot(kLNop, Up(0x3E, 0x04, 9, 7, 0xE)).upper_kind == VUK_U_ITOF_N);  // ITOF12
    // ADDAx / SUBAy (special2 ops 0x00 / 0x01) have fast forms; the vector ADDA (op 0x0A, func 0x3C) stays generic
    CHECK(Slot(kLNop, Up(0x3C, 0x00, 1, 2, 0xF)).upper_kind == VUK_U_ADDA_BC);
    CHECK(Slot(kLNop, Up(0x3D, 0x01, 1, 2, 0xF)).upper_kind == VUK_U_SUBA_BC);
    // CLIPw.xyz vf1, vf2 (func 0x3F op 0x07): reads fs.xyz, ft.w; writes nothing
    VuDecodedSlot c = Slot(kLNop, Up(0x3F, 0x07, 1, 2, 0xE));
    CHECK(c.upper_kind == VUK_U_CLIP && HasRead(c, 1, 0xE) && HasRead(c, 2, 0x1) && c.n_wr == 0 && c.upper_vf_write == -1);
    // ABS (func 0x3D op 0x07) writes ft; the func 0x3C encoding is recorded as a read-only MULAq
    VuDecodedSlot ab = Slot(kLNop, Up(0x3D, 0x07, 1, 2, 0xF));
    CHECK(ab.upper_kind == VUK_U_GENERIC && HasRead(ab, 1, 0xF) && HasWrite(ab, 2, 0xF) && ab.upper_vf_write == 2);
    VuDecodedSlot ab0 = Slot(kLNop, Up(0x3C, 0x07, 1, 2, 0xF));
    CHECK(ab0.n_rd == 1 && HasRead(ab0, 1, 0xF) && ab0.n_wr == 0 && ab0.upper_vf_write == -1);
    // MULAx (0x3C op 0x06), MADDAy (0x3D op 0x02), MSUBAz (0x3E op 0x03)
    VuDecodedSlot mula = Slot(kLNop, Up(0x3C, 0x06, 1, 2, 0xF));
    CHECK(mula.upper_kind == VUK_U_MULA_BC && HasRead(mula, 1, 0xF) && HasRead(mula, 2, 0x8) && mula.n_wr == 0);
    CHECK(Slot(kLNop, Up(0x3D, 0x02, 1, 2, 0xF)).upper_kind == VUK_U_MADDA_BC);
    CHECK(Slot(kLNop, Up(0x3E, 0x03, 1, 2, 0xF)).upper_kind == VUK_U_MSUBA_BC);
    // op 0x0A: ADDA (0x3C) generic, MADDA (0x3D), MULA (0x3E); reads fs, ft with dest
    VuDecodedSlot madda = Slot(kLNop, Up(0x3D, 0x0A, 1, 2, 0xF));
    CHECK(madda.upper_kind == VUK_U_MADDA && HasRead(madda, 1, 0xF) && HasRead(madda, 2, 0xF));
    CHECK(Slot(kLNop, Up(0x3E, 0x0A, 1, 2, 0xF)).upper_kind == VUK_U_MULA);
    CHECK(Slot(kLNop, Up(0x3C, 0x0A, 1, 2, 0xF)).upper_kind == VUK_U_GENERIC);
    CHECK(Slot(kLNop, Up(0x3F, 0x0A, 1, 2, 0xF)).n_rd == 0);
    // op 0x0B: SUBA (0x3C) / MSUBA (0x3D) read both; OPMULA (0x3E) xyz; NOP (0x3F) nothing
    CHECK(Slot(kLNop, Up(0x3C, 0x0B, 1, 2, 0x3)).n_rd == 2);
    VuDecodedSlot opm = Slot(kLNop, Up(0x3E, 0x0B, 1, 2, 0xF));
    CHECK(HasRead(opm, 1, 0xE) && HasRead(opm, 2, 0xE) && opm.upper_kind == VUK_U_GENERIC);
    CHECK(Slot(kLNop, Up(0x3F, 0x0B, 1, 2, 0xF)).upper_kind == VUK_U_NOP);
    CHECK(Slot(kLNop, Up(0x3E, 0x0B, 1, 2, 0xF)).upper_kind == VUK_U_GENERIC);
    // ops 0x08/0x09 read fs only
    VuDecodedSlot aq = Slot(kLNop, Up(0x3C, 0x08, 1, 2, 0xF));
    CHECK(aq.n_rd == 1 && HasRead(aq, 1, 0xF));
}

static void TestLowerFamilies() {
    // LQ.xyzw vf5, 4(vi3)
    VuDecodedSlot lq = Slot(Lo(0x00, 5, 3, 0xF, 4), kUNop);
    CHECK(lq.lower_kind == VUK_L_LQ && HasWrite(lq, 5, 0xF) && lq.l_ft == 5 && lq.l_fs == 3 && lq.l_dest == 0xF && lq.l_imm11 == 4);
    CHECK(Slot(Lo(0x00, 5, 3, 0xF, 0x7FF), kUNop).l_imm11 == -1);
    CHECK(Slot(Lo(0x00, 5, 3, 0xF, 0x400), kUNop).l_imm11 == -1024);
    // SQ.xy vf5, 0(vi3)
    VuDecodedSlot sq = Slot(Lo(0x01, 3, 5, 0xC, 0), kUNop);
    CHECK(sq.lower_kind == VUK_L_SQ && HasRead(sq, 5, 0xC) && sq.n_wr == 0);
    // ILW / ISW
    VuDecodedSlot ilw = Slot(Lo(0x04, 2, 3, 0x8, 1), kUNop);
    CHECK(ilw.lower_kind == VUK_L_ILW && ilw.ialu_wr_vi == 2 && ilw.n_rd == 0 && ilw.n_wr == 0);
    VuDecodedSlot isw = Slot(Lo(0x05, 2, 3, 0x8, 1), kUNop);
    CHECK(isw.lower_kind == VUK_L_ISW && isw.ialu_wr_vi == -1);
    // IADDIU vi4, vi0, 0x7FFF / ISUBIU
    VuDecodedSlot iu = Slot(Lo(0x08, 4, 0, 0xF, 0x7FF), kUNop);
    CHECK(iu.lower_kind == VUK_L_IADDIU && iu.l_imm15u == 0x7FFF);
    CHECK(Slot(Lo(0x08, 4, 0, 0x5, 0x123), kUNop).l_imm15u == 0x2923);
    CHECK(Slot(Lo(0x09, 4, 0, 0x0, 0x10), kUNop).lower_kind == VUK_L_ISUBIU);
    // Flag ops
    CHECK(Slot(Lo(0x1C, 1, 0, 0, 0), kUNop).lower_kind == VUK_L_FCGET);
    CHECK(Slot(Lo(0x11, 0, 0, 0, 0), kUNop).lower_kind == VUK_L_FCSET);
    CHECK(Slot(Lo(0x12, 0, 0, 0, 0), kUNop).lower_kind == VUK_L_FCAND && Slot(Lo(0x13, 0, 0, 0, 0), kUNop).lower_kind == VUK_L_FCOR);
    CHECK(Slot(Lo(0x16, 3, 0, 0, 0x55), kUNop).lower_kind == VUK_L_FSAND && Slot(Lo(0x1A, 3, 2, 0, 0), kUNop).lower_kind == VUK_L_FMAND);
    // Branches
    VuDecodedSlot ibne = Slot(Lo(0x29, 2, 1, 0, 5), kUNop);
    CHECK(ibne.lower_kind == VUK_L_IBNE && ibne.ialu_branch_rd == 0x6 && VuDecoder::IsBranchKind(ibne.lower_kind));
    CHECK(Slot(Lo(0x28, 2, 1, 0, 5), kUNop).lower_kind == VUK_L_IBEQ);
    VuDecodedSlot ibgez = Slot(Lo(0x2F, 0, 3, 0, 5), kUNop);
    CHECK(ibgez.lower_kind == VUK_L_IBGEZ && ibgez.ialu_branch_rd == 0x8 && VuDecoder::IsBranchKind(ibgez.lower_kind));
    CHECK(Slot(Lo(0x2C, 0, 3, 0, 5), kUNop).ialu_branch_rd == 0x8 && Slot(Lo(0x2C, 0, 3, 0, 5), kUNop).lower_kind == VUK_L_IBLTZ);
    CHECK(Slot(Lo(0x20, 0, 0, 0, 5), kUNop).lower_kind == VUK_L_B);
    CHECK(Slot(Lo(0x21, 7, 0, 0, 5), kUNop).lower_kind == VUK_L_BAL);
    VuDecodedSlot jr = Slot(Lo(0x24, 0, 9, 0, 0), kUNop);
    CHECK(jr.lower_kind == VUK_L_JR && jr.ialu_branch_rd == (1u << 9));
    CHECK(Slot(Lo(0x25, 7, 9, 0, 0), kUNop).lower_kind == VUK_L_JALR);
    CHECK(VuDecoder::IsBranchKind(VUK_L_B) && VuDecoder::IsBranchKind(VUK_L_BAL) && VuDecoder::IsBranchKind(VUK_L_JR) &&
          VuDecoder::IsBranchKind(VUK_L_JALR) && VuDecoder::IsBranchKind(VUK_L_IBEQ));
    CHECK(!VuDecoder::IsBranchKind(VUK_L_GENERIC) && !VuDecoder::IsBranchKind(VUK_L_NOP) && !VuDecoder::IsBranchKind(VUK_L_LQ));
    // Run terminals: every branch, XTOP / XITOP / XGKICK; the flag readers are not
    CHECK(VuDecoder::IsRunTerminalLowerKind(VUK_L_IBLTZ) && VuDecoder::IsRunTerminalLowerKind(VUK_L_XTOP) &&
          VuDecoder::IsRunTerminalLowerKind(VUK_L_XITOP) && VuDecoder::IsRunTerminalLowerKind(VUK_L_XGKICK));
    CHECK(!VuDecoder::IsRunTerminalLowerKind(VUK_L_FSAND) && !VuDecoder::IsRunTerminalLowerKind(VUK_L_FCAND) &&
          !VuDecoder::IsRunTerminalLowerKind(VUK_L_LQ) && !VuDecoder::IsBranchKind(VUK_L_XTOP));
}

static void TestLowerGroup40() {
    // IADD vi1, vi2, vi3 (fn 0x30, fd in the funct2 position)
    VuDecodedSlot iadd = Slot(Lo40(3, 2, 0, 1, 0x30), kUNop);
    CHECK(iadd.lower_kind == VUK_L_IADD && iadd.l_fd == 1 && iadd.l_fs == 2 && iadd.l_ft == 3);
    CHECK(Slot(Lo40(3, 2, 0, 1, 0x31), kUNop).lower_kind == VUK_L_ISUB);
    CHECK(Slot(Lo40(3, 2, 0, 1, 0x32), kUNop).lower_kind == VUK_L_IADDI);
    CHECK(Slot(Lo40(3, 2, 0, 1, 0x34), kUNop).lower_kind == VUK_L_IAND);
    CHECK(Slot(Lo40(3, 2, 0, 1, 0x35), kUNop).lower_kind == VUK_L_IOR);
    // MOVE / MR32
    VuDecodedSlot mv = Slot(Lo40(3, 4, 0xF, 0x0C, 0x3C), kUNop);
    CHECK(mv.lower_kind == VUK_L_MOVE && HasRead(mv, 4, 0xF) && HasWrite(mv, 3, 0xF));
    CHECK(Slot(Lo40(0, 4, 0xF, 0x0C, 0x3C), kUNop).lower_kind == VUK_L_NOP);
    VuDecodedSlot mr = Slot(Lo40(3, 4, 0xE, 0x0C, 0x3D), kUNop);
    CHECK(mr.lower_kind == VUK_L_MR32 && HasRead(mr, 4, 0x7) && HasWrite(mr, 3, 0xE));
    CHECK(Slot(Lo40(0, 4, 0xE, 0x0C, 0x3D), kUNop).lower_kind == VUK_L_NOP);
    // LQI / SQI / LQD / SQD
    VuDecodedSlot lqi = Slot(Lo40(6, 1, 0xF, 0x0D, 0x3C), kUNop);
    CHECK(lqi.lower_kind == VUK_L_LQI && HasWrite(lqi, 6, 0xF) && lqi.n_rd == 0);
    VuDecodedSlot sqi = Slot(Lo40(1, 6, 0xF, 0x0D, 0x3D), kUNop);
    CHECK(sqi.lower_kind == VUK_L_SQI && HasRead(sqi, 6, 0xF) && sqi.n_wr == 0);
    CHECK(Slot(Lo40(6, 1, 0xF, 0x0D, 0x3E), kUNop).n_wr == 1);  // LQD writes
    CHECK(Slot(Lo40(1, 6, 0xF, 0x0D, 0x3F), kUNop).n_rd == 1);  // SQD reads
    // DIV Q, vf1w, vf2x / SQRT / RSQRT / WAITQ
    VuDecodedSlot dv = Slot(LoDiv(1, 3, 2, 0, 0x3C), kUNop);
    CHECK(dv.lower_kind == VUK_L_DIV && dv.fdiv_pipe_op && HasRead(dv, 1, 0x1) && HasRead(dv, 2, 0x8) && dv.l_fsf == 3);
    VuDecodedSlot sqrt = Slot(LoDiv(1, 3, 2, 1, 0x3D), kUNop);
    CHECK(sqrt.lower_kind == VUK_L_GENERIC && sqrt.fdiv_pipe_op && sqrt.n_rd == 1 && HasRead(sqrt, 2, 0x4));
    VuDecodedSlot rsq = Slot(LoDiv(1, 0, 2, 2, 0x3E), kUNop);
    CHECK(rsq.fdiv_pipe_op && HasRead(rsq, 1, 0x8) && HasRead(rsq, 2, 0x2));
    VuDecodedSlot wq = Slot(LoDiv(0, 0, 0, 0, 0x3F), kUNop);
    CHECK(wq.lower_kind == VUK_L_WAITQ && wq.fdiv_pipe_op && wq.n_rd == 0);
    // MTIR / MFIR / ILWR / ISWR
    VuDecodedSlot mtir = Slot(Lo40(2, 5, 0x2 /*fsf=2 -> z*/, 0x0F, 0x3C), kUNop);
    CHECK(mtir.lower_kind == VUK_L_MTIR && HasRead(mtir, 5, 0x2) && mtir.n_wr == 0);
    VuDecodedSlot mfir = Slot(Lo40(5, 2, 0xF, 0x0F, 0x3D), kUNop);
    CHECK(mfir.lower_kind == VUK_L_MFIR && HasWrite(mfir, 5, 0xF));
    VuDecodedSlot ilwr = Slot(Lo40(7, 2, 0x8, 0x0F, 0x3E), kUNop);
    CHECK(ilwr.lower_kind == VUK_L_GENERIC && ilwr.ialu_wr_vi == 7 && ilwr.n_rd == 0 && ilwr.n_wr == 0);
    CHECK(Slot(Lo40(7, 2, 0x8, 0x0F, 0x3F), kUNop).ialu_wr_vi == -1);
    // RNEXT / RGET / RINIT / RXOR
    CHECK(Slot(Lo40(5, 0, 0xF, 0x10, 0x3C), kUNop).n_wr == 1);
    VuDecodedSlot rget = Slot(Lo40(5, 0, 0xF, 0x10, 0x3D), kUNop);
    CHECK(rget.lower_kind == VUK_L_RGET && HasWrite(rget, 5, 0xF));
    VuDecodedSlot rinit = Slot(Lo40(0, 5, 0x1 /*fsf=1 -> y*/, 0x10, 0x3E), kUNop);
    CHECK(rinit.lower_kind == VUK_L_RINIT && HasRead(rinit, 5, 0x4));
    CHECK(Slot(Lo40(0, 5, 0x0, 0x10, 0x3F), kUNop).n_rd == 1);
    // MFP
    VuDecodedSlot mfp = Slot(Lo40(5, 0, 0xF, 0x19, 0x3C), kUNop);
    CHECK(mfp.lower_kind == VUK_L_GENERIC && HasWrite(mfp, 5, 0xF));
    CHECK(Slot(Lo40(5, 0, 0xF, 0x19, 0x3D), kUNop).n_wr == 0);
    // EFU: ESADD/ERSADD/ELENG/ERLENG read fs with dest; EATANxy/xz/ESUM; ESQRT/ERSQRT/ERCPR fsf; WAITP; ESIN/EATAN/EEXP
    VuDecodedSlot eleng = Slot(Lo40(0, 5, 0xE, 0x1C, 0x3E), kUNop);
    CHECK(eleng.efu_pipe_op && HasRead(eleng, 5, 0xE) && !eleng.fdiv_pipe_op);
    VuDecodedSlot esum = Slot(Lo40(0, 5, 0xF, 0x1D, 0x3E), kUNop);
    CHECK(esum.efu_pipe_op && HasRead(esum, 5, 0xF));
    CHECK(!Slot(Lo40(0, 5, 0xF, 0x1D, 0x3F), kUNop).efu_pipe_op);
    VuDecodedSlot esqrt = Slot(Lo40(0, 5, 0x3, 0x1E, 0x3C), kUNop);
    CHECK(esqrt.efu_pipe_op && HasRead(esqrt, 5, 0x1));
    VuDecodedSlot waitp = Slot(Lo40(0, 0, 0, 0x1E, 0x3F), kUNop);
    CHECK(waitp.efu_pipe_op && waitp.n_rd == 0);
    CHECK(Slot(Lo40(0, 5, 0x0, 0x1F, 0x3C), kUNop).efu_pipe_op);   // ESIN
    CHECK(Slot(Lo40(0, 5, 0x0, 0x1F, 0x3D), kUNop).efu_pipe_op);   // EATAN
    CHECK(!Slot(Lo40(0, 5, 0x0, 0x1F, 0x3E), kUNop).efu_pipe_op);  // unassigned
    VuDecodedSlot eexp = Slot(Lo40(0, 5, 0x0, 0x1F, 0x3F), kUNop);
    CHECK(eexp.efu_pipe_op && HasRead(eexp, 5, 0x8));
    // XGKICK vi3 (funct2 0x1B t3 0) and XITOP (0x1A t3 1): fast kinds, no VF sets, no pipe
    VuDecodedSlot xgk = Slot(Lo40(0, 3, 0, 0x1B, 0x3C), kUNop);
    CHECK(xgk.lower_kind == VUK_L_XGKICK && xgk.n_rd == 0 && xgk.n_wr == 0 && !xgk.efu_pipe_op);
    VuDecodedSlot xtop = Slot(Lo40(4, 0, 0, 0x1A, 0x3D), kUNop);
    CHECK(xtop.lower_kind == VUK_L_XITOP && xtop.ialu_wr_vi == -1);
    CHECK(Slot(Lo40(4, 0, 0, 0x1A, 0x3C), kUNop).lower_kind == VUK_L_XTOP);
}

static void TestIBitAndHazards() {
    // I-bit: the lower word is an immediate -- nothing lower is decoded
    VuDecodedSlot ib = Slot(0x3F800000u, Up(0x22, 3, 1, 0, 0xF) | kIBit);
    CHECK(ib.i_bit && ib.upper_kind == VUK_U_ADD_I && ib.lower_kind == VUK_L_GENERIC);
    CHECK(ib.l_ft == 0 && ib.l_fs == 0 && ib.l_imm11 == 0 && ib.n_rd == 1 && ib.n_wr == 1);
    CHECK(ib.upper_vf_write == -1 && !ib.discard_lower && !ib.hide_upper && ib.ialu_branch_rd == 0);
    // discard_lower: ADD vf3 / LQ vf3
    VuDecodedSlot d = Slot(Lo(0x00, 3, 1, 0xF, 0), Up(0x28, 3, 1, 2, 0xF));
    CHECK(d.discard_lower && !d.hide_upper && d.upper_vf_write == 3);
    // hide_upper: ADD vf3 / SQ vf3
    VuDecodedSlot h = Slot(Lo(0x01, 1, 3, 0xF, 0), Up(0x28, 3, 1, 2, 0xF));
    CHECK(h.hide_upper && !h.discard_lower);
    // hide_upper through DIV's second operand
    VuDecodedSlot h2 = Slot(LoDiv(1, 0, 3, 0, 0x3C), Up(0x28, 3, 1, 2, 0xF));
    CHECK(h2.hide_upper);
    // MOVE vf3, vf4 with upper writing vf3 -> discard; with upper writing vf4 -> hide
    CHECK(Slot(Lo40(3, 4, 0xF, 0x0C, 0x3C), Up(0x28, 3, 1, 2, 0xF)).discard_lower);
    CHECK(Slot(Lo40(3, 4, 0xF, 0x0C, 0x3C), Up(0x28, 4, 1, 2, 0xF)).hide_upper);
    // FTOI0 writes ft: hazards key on ft, not fd
    CHECK(Slot(Lo(0x00, 7, 1, 0xF, 0), Up(0x3C, 0x05, 9, 7, 0xE)).discard_lower);
    // ABS with func 0x3C creates no hazard (recorded as a read-only op)
    CHECK(!Slot(Lo(0x00, 2, 1, 0xF, 0), Up(0x3C, 0x07, 1, 2, 0xF)).discard_lower);
    CHECK(Slot(Lo(0x00, 2, 1, 0xF, 0), Up(0x3D, 0x07, 1, 2, 0xF)).discard_lower);
    // VF0 never hazards
    CHECK(!Slot(Lo(0x00, 0, 1, 0xF, 0), Up(0x28, 0, 1, 2, 0xF)).discard_lower);
    // The NOP lower (MOVE vf0,vf0) never hides or discards
    CHECK(!Slot(kLNop, Up(0x28, 3, 1, 2, 0xF)).hide_upper && !Slot(kLNop, Up(0x28, 3, 1, 2, 0xF)).discard_lower);
}

static void TestHardwired() {
    CHECK(VuDecoder::VuUpperCanTouchHardwired(Up(0x28, 0, 1, 2, 0xF)));
    CHECK(!VuDecoder::VuUpperCanTouchHardwired(Up(0x28, 3, 1, 2, 0xF)));
    CHECK(VuDecoder::VuUpperCanTouchHardwired(Up(0x31, 3, 1, 2, 0xF)));          // unused funcs
    CHECK(!VuDecoder::VuUpperCanTouchHardwired(Up(0x3C, 0x06, 1, 2, 0xF)));      // MULAx
    CHECK(VuDecoder::VuUpperCanTouchHardwired(Up(0x3C, 0x05, 1, 0, 0xF)));       // FTOI0 vf0
    CHECK(!VuDecoder::VuUpperCanTouchHardwired(Up(0x3C, 0x05, 1, 7, 0xF)));
    CHECK(VuDecoder::VuUpperCanTouchHardwired(Up(0x3D, 0x07, 1, 0, 0xF)));       // ABS vf0
    CHECK(!VuDecoder::VuUpperCanTouchHardwired(Up(0x3C, 0x07, 1, 0, 0xF)));      // MULAq writes ACC (func 0x3C is the Q column)
    CHECK(!VuDecoder::VuUpperCanTouchHardwired(Up(0x3C, 0x08, 1, 0, 0xF)));      // ADDAq
    CHECK(!VuDecoder::VuUpperCanTouchHardwired(Up(0x3C, 0x09, 1, 0, 0xF)));      // SUBAq
    CHECK(!VuDecoder::VuUpperCanTouchHardwired(Up(0x3F, 0x07, 1, 0, 0xF)));      // CLIP
    CHECK(VuDecoder::VuUpperCanTouchHardwired(Up(0x3F, 0x0C, 1, 0, 0xF)));       // undefined special2
    CHECK(!VuDecoder::VuUpperCanTouchHardwired(kUNop));

    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo(0x00, 0, 1, 0xF, 0)));           // LQ vf0
    CHECK(!VuDecoder::VuLowerCanTouchHardwired(Lo(0x00, 5, 1, 0xF, 0)));
    CHECK(!VuDecoder::VuLowerCanTouchHardwired(Lo(0x01, 0, 5, 0xF, 0)));          // SQ
    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo(0x04, 0, 1, 0x8, 0)));           // ILW vi0
    CHECK(!VuDecoder::VuLowerCanTouchHardwired(Lo(0x05, 0, 1, 0x8, 0)));          // ISW
    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo(0x08, 0, 1, 0, 0)));             // IADDIU vi0
    CHECK(!VuDecoder::VuLowerCanTouchHardwired(Lo(0x11, 0, 0, 0, 0)));            // FCSET
    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo(0x1C, 0, 0, 0, 0)));             // FCGET vi0
    CHECK(!VuDecoder::VuLowerCanTouchHardwired(Lo(0x20, 0, 0, 0, 0)));            // B
    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo(0x21, 0, 0, 0, 0)));             // BAL vi0
    CHECK(!VuDecoder::VuLowerCanTouchHardwired(Lo(0x29, 1, 2, 0, 0)));            // IBNE
    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo40(3, 2, 0, 0, 0x30)));           // IADD vi0
    CHECK(!VuDecoder::VuLowerCanTouchHardwired(Lo40(3, 2, 0, 1, 0x30)));
    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo40(0, 2, 0, 1, 0x32)));           // IADDI vi0
    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo40(3, 2, 0, 1, 0x33)));           // fn < 0x3C unrecognised
    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo40(0, 4, 0xF, 0x0C, 0x3C)));      // MOVE vf0
    CHECK(!VuDecoder::VuLowerCanTouchHardwired(Lo40(3, 4, 0xF, 0x0C, 0x3C)));
    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo40(3, 4, 0xF, 0x0C, 0x3E)));      // funct2 0x0C t3 2: unrecognised
    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo40(6, 0, 0xF, 0x0D, 0x3C)));      // LQI (vi0++)
    CHECK(!VuDecoder::VuLowerCanTouchHardwired(Lo40(6, 1, 0xF, 0x0D, 0x3C)));
    CHECK(!VuDecoder::VuLowerCanTouchHardwired(Lo40(6, 0, 0xF, 0x0D, 0x3D)));     // SQI: is == 0 not a touch
    CHECK(!VuDecoder::VuLowerCanTouchHardwired(LoDiv(1, 0, 2, 0, 0x3C)));         // DIV
    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo40(0, 5, 0, 0x0F, 0x3C)));        // MTIR vi0
    CHECK(!VuDecoder::VuLowerCanTouchHardwired(Lo40(0, 5, 0, 0x0F, 0x3F)));       // ISWR
    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo40(0, 5, 0xF, 0x10, 0x3D)));      // RGET vf0
    CHECK(!VuDecoder::VuLowerCanTouchHardwired(Lo40(0, 5, 0xF, 0x10, 0x3E)));     // RINIT
    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo40(0, 5, 0xF, 0x19, 0x3C)));      // MFP vf0
    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo40(3, 5, 0xF, 0x19, 0x3D)));      // 0x19 t3 1: unrecognised
    CHECK(!VuDecoder::VuLowerCanTouchHardwired(Lo40(3, 5, 0xF, 0x1B, 0x3C)));     // XGKICK
    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo40(3, 5, 0xF, 0x1B, 0x3D)));      // 0x1B t3 1: unrecognised
    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo40(0, 5, 0xF, 0x1A, 0x3D)));      // XTOP vi0
    CHECK(!VuDecoder::VuLowerCanTouchHardwired(Lo40(4, 5, 0xF, 0x1A, 0x3D)));     // XTOP vi4
    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo40(4, 5, 0xF, 0x1A, 0x3E)));      // 0x1A t3 2: unrecognised
    CHECK(!VuDecoder::VuLowerCanTouchHardwired(Lo40(0, 5, 0xE, 0x1C, 0x3E)));     // ELENG
    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo40(0, 3, 0, 0x0C, 0x3F)));        // 0x0C t3 3: unrecognised
    CHECK(VuDecoder::VuLowerCanTouchHardwired(Lo(0x7F, 0, 0, 0, 0)));             // undefined opcode

    VuDecodedSlot s = Slot(kLNop, Up(0x28, 3, 1, 2, 0xF));
    CHECK(VuDecoder::VuSlotCanTouchHardwired(s));                                   // MOVE vf0,vf0 lower touches
    VuDecodedSlot s2 = Slot(Lo(0x01, 1, 3, 0xF, 0), Up(0x28, 3, 1, 2, 0xF));
    CHECK(!VuDecoder::VuSlotCanTouchHardwired(s2));
    VuDecodedSlot s3 = Slot(0, Up(0x28, 3, 1, 2, 0xF) | kIBit);
    CHECK(!VuDecoder::VuSlotCanTouchHardwired(s3));                                 // I-bit: only the upper counts
    VuDecodedSlot s4 = Slot(0, Up(0x28, 0, 1, 2, 0xF) | kIBit);
    CHECK(VuDecoder::VuSlotCanTouchHardwired(s4));
}

static void TestSpanClassifier() {
    std::vector<VuDecodedSlot> p;
    p.push_back(Slot(kLNop, Up(0x28, 3, 1, 2, 0xF)));  // ADD vf3 = vf1 + vf2
    p.push_back(Slot(kLNop, Up(0x2A, 4, 3, 1, 0xF)));  // MUL vf4 = vf3 * vf1 (reads the previous write)
    p.push_back(Slot(kLNop, Up(0x28, 5, 1, 2, 0xF)));  // ADD vf5 = vf1 + vf2
    p.push_back(Slot(kLNop, kUNop | kEBit));            // NOP / NOP, E
    SpanClassifier::ComputeRunLenTouchHw(p.data(), p.size());
    CHECK(p[3].run_len == 1 && p[2].run_len == 2 && p[1].run_len == 3 && p[0].run_len == 4);
    CHECK(p[0].touch_hw == 1);  // the MOVE vf0,vf0 lower may write VF0
    SpanClassifier::ComputeStallClasses(p.data(), p.size());
    CHECK(p[0].stall_class == 1 && p[1].stall_class == 1 && p[2].stall_class == 1 && p[3].stall_class == 6);
    SpanClassifier::ComputeFoldClasses(p.data(), p.size());
    CHECK(p[0].fold_class == 16 && p[1].fold_class == 0 && p[2].fold_class == 16 && p[3].fold_class == 0);
    std::vector<uint8_t> rem;
    SpanClassifier::ComputeSpanRem(p.data(), p.size(), rem);
    CHECK(rem.size() == 4 && rem[0] == 1 && rem[1] == 0 && rem[2] == 1 && rem[3] == 0);

    // Stall class table corners
    CHECK(Slot(0, Up(0x28, 3, 1, 2, 0xF) | kIBit).n_rd == 2);
    std::vector<VuDecodedSlot> c;
    c.push_back(Slot(0, Up(0x28, 3, 1, 2, 0xF) | kIBit));             // no lower, rd, 1 wr -> 7
    c.push_back(Slot(0, Up(0x3C, 0x06, 1, 2, 0xF) | kIBit));          // no lower, rd, 0 wr -> 8
    c.push_back(Slot(0, Up(0x3C, 0x05, 0, 7, 0xF) | kIBit));          // no lower, no rd, 1 wr -> 9
    c.push_back(Slot(0, kUNop | kIBit));                              // no lower, no rd, no wr -> 10
    c.push_back(Slot(Lo(0x04, 2, 3, 0x8, 1), Up(0x28, 3, 1, 2, 0xF)));  // ILW + rd + 1 wr -> 11
    c.push_back(Slot(Lo(0x04, 2, 3, 0x8, 1), kUNop));                 // ILW, no rd, no wr -> 14
    c.push_back(Slot(Lo(0x29, 2, 1, 0, 5), kUNop));                   // branch reads, no rd, no wr -> 15
    c.push_back(Slot(LoDiv(1, 3, 2, 0, 0x3C), kUNop));                // FDIV -> 0
    c.push_back(Slot(Lo(0x00, 5, 3, 0xF, 4), Up(0x28, 3, 1, 2, 0xF)));  // lower LQ: rd, 2 wr -> 2
    c.push_back(Slot(Lo(0x00, 5, 3, 0xF, 4), kUNop));                 // lower, no rd, 1 wr -> 4
    c.push_back(Slot(Lo(0x00, 5, 3, 0xF, 4), Up(0x3C, 0x05, 0, 7, 0xF)));  // lower, no rd, 2 wr -> 5
    c.push_back(Slot(Lo(0x00, 5, 3, 0xF, 4), Up(0x28, 3, 1, 2, 0xF) | kIBit));  // no lower, 1 wr, rd -> 7 (I-bit hides the LQ)
    SpanClassifier::ComputeStallClasses(c.data(), c.size());
    CHECK(c[0].stall_class == 7 && c[1].stall_class == 8 && c[2].stall_class == 9 && c[3].stall_class == 10);
    CHECK(c[4].stall_class == 11 && c[5].stall_class == 14 && c[6].stall_class == 15 && c[7].stall_class == 0);
    CHECK(c[8].stall_class == 2 && c[9].stall_class == 4 && c[10].stall_class == 5 && c[11].stall_class == 7);
    // Branch bundles: 16 with VF reads, 17 with one VF write, 18 with both; two writes stay dynamic
    std::vector<VuDecodedSlot> b;
    b.push_back(Slot(Lo(0x29, 2, 1, 0, 5), Up(0x3C, 0x06, 1, 2, 0xF)));   // IBNE + MULAx (rd, no wr) -> 16
    b.push_back(Slot(Lo(0x29, 2, 1, 0, 5), Up(0x3C, 0x05, 0, 7, 0xF)));   // IBNE + FTOI0 vf7, vf0 (wr, no rd) -> 17
    b.push_back(Slot(Lo(0x29, 2, 1, 0, 5), Up(0x28, 3, 1, 2, 0xF)));      // IBNE + ADD (rd, wr) -> 18
    b.push_back(Slot(Lo(0x2C, 0, 3, 0, 5), kUNop));                       // IBLTZ -> 15
    SpanClassifier::ComputeStallClasses(b.data(), b.size());
    CHECK(b[0].stall_class == 16 && b[1].stall_class == 17 && b[2].stall_class == 18 && b[3].stall_class == 15);

    // run_len cap and the fold read-window of 3
    std::vector<VuDecodedSlot> w;
    w.push_back(Slot(kLNop, Up(0x28, 3, 1, 2, 0xF)));  // writes vf3
    w.push_back(Slot(kLNop, Up(0x28, 6, 1, 2, 0xF)));
    w.push_back(Slot(kLNop, Up(0x28, 7, 1, 2, 0xF)));
    w.push_back(Slot(kLNop, Up(0x28, 8, 1, 2, 0xF)));
    w.push_back(Slot(kLNop, Up(0x28, 9, 3, 2, 0xF)));  // reads vf3 four slots back: outside the window -> folds
    w.push_back(Slot(kLNop, Up(0x28, 10, 9, 2, 0xF))); // reads vf9 one back -> disqualified
    w.push_back(Slot(kLNop, kUNop | kEBit));
    SpanClassifier::ComputeRunLenTouchHw(w.data(), w.size());
    SpanClassifier::ComputeStallClasses(w.data(), w.size());
    SpanClassifier::ComputeFoldClasses(w.data(), w.size());
    CHECK(w[4].fold_class == 16 && w[5].fold_class == 0 && w[6].fold_class == 0);
    std::vector<VuDecodedSlot> big(300, Slot(kLNop, Up(0x28, 3, 1, 2, 0xF)));
    SpanClassifier::ComputeRunLenTouchHw(big.data(), big.size());
    CHECK(big[0].run_len == 255 && big[299].run_len == 1 && big[298].run_len == 2);
    SpanClassifier::ComputeStallClasses(big.data(), big.size());
    SpanClassifier::ComputeFoldClasses(big.data(), big.size());
    CHECK(big[0].fold_class == 0 && big[50].fold_class == 0);   // run_len >= 250 disqualifies
    CHECK(big[51].fold_class == 16 && big[298].fold_class == 16 && big[299].fold_class == 0);  // no vf3 read: folds
}

int main() {
    TestLayout();
    TestNops();
    TestUpperFamilies();
    TestSpecial2();
    TestLowerFamilies();
    TestLowerGroup40();
    TestIBitAndHazards();
    TestHardwired();
    TestSpanClassifier();
    if (g_fail) {
        std::printf("VuDecodeTable: %d FAILED\n", g_fail);
        return 1;
    }
    std::printf("VuDecodeTable: PASS (sizeof(VuDecodedSlot)=%zu, %u upper kinds, %u lower kinds)\n",
                sizeof(VuDecodedSlot), unsigned(VUK_U_COUNT), unsigned(VUK_L_COUNT));
    return 0;
}
