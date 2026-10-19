// Vu1AotBlockGen -- one block function with the register file in locals.
// Rules in the header; each op's text is the ExecUpperK / ExecLowerK body of
// its kind (vu_cached_tier_exec.inl) with the operands named; a region is the
// head block plus its static successors, each a labelled scope.
#include "engine/vu/vu1_aot/vu1_aot_block_gen.h"

#include <algorithm>
#include <cstdio>

#include "engine/vu/flag_pipeline.h"
#include "engine/vu/stall_model.h"
#include "engine/vu/tiers/vu_block_tier.h"
#include "engine/vu/vu_decoder.h"

namespace {

std::string Hex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%x", v);
    return buf;
}
std::string Pc4(uint32_t pc) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04x", pc);
    return buf;
}
std::string Dec(long long v) { return std::to_string(v); }

// The 12-bit immediate of the FS* instructions and the 24-bit one of the FC* instructions.
uint32_t Imm12(uint32_t l) { return ((l >> 10) & 0x800u) | (l & 0x7FFu); }
uint32_t Imm24(uint32_t l) { return l & 0xFFFFFFu; }
int32_t SignExtend5(uint32_t v) {
    v &= 0x1Fu;
    return static_cast<int32_t>(static_cast<int8_t>((v & 0x10u) ? (v | 0xE0u) : v));
}

bool IsFmacBc(uint8_t k) {
    return k == VUK_U_ADD_BC || k == VUK_U_SUB_BC || k == VUK_U_MUL_BC || k == VUK_U_MADD_BC || k == VUK_U_MSUB_BC;
}
bool IsFmacVec(uint8_t k) { return k == VUK_U_ADD || k == VUK_U_SUB || k == VUK_U_MUL || k == VUK_U_MADD || k == VUK_U_MSUB; }
bool IsFmac(uint8_t k) { return IsFmacBc(k) || IsFmacVec(k) || k == VUK_U_MULQ || k == VUK_U_MUL_I || k == VUK_U_ADD_I; }
bool IsAcc(uint8_t k) {
    return k == VUK_U_ADDA_BC || k == VUK_U_SUBA_BC || k == VUK_U_MULA_BC || k == VUK_U_MADDA_BC || k == VUK_U_MSUBA_BC ||
           k == VUK_U_MULA || k == VUK_U_MADDA;
}
const char* UpperName(uint8_t k) {
    static const char* const kNames[VUK_U_COUNT] = {
        "GENERIC", "NOP",     "ADD_BC",  "SUB_BC",   "MUL_BC",   "MAX_BC",  "MINI_BC", "FTOI0", "ADD",   "MADD_BC", "MSUB_BC",
        "MADD",    "MSUB",    "MUL",     "SUB",      "MULA_BC",  "MADDA_BC", "MSUBA_BC", "MULA", "MADDA", "MULQ",    "MAX",
        "MINI_I",  "MUL_I",   "ADD_I",   "FTOI4",    "CLIP",     "ITOF0",   "MINI",    "ADDA_BC", "SUBA_BC", "ITOF_N", "FTOI_N"};
    return k < VUK_U_COUNT ? kNames[k] : "?";
}
const char* LowerName(uint8_t k) {
    static const char* const kNames[VUK_L_COUNT] = {
        "GENERIC", "NOP",   "LQ",    "SQ",    "ILW",   "IADDIU", "IBEQ",  "IBNE",  "IADD",  "IAND",  "MOVE",  "MTIR",
        "ISUBIU",  "ISUB",  "LQI",   "MR32",  "MFIR",  "RGET",   "RINIT", "ISW",   "SQI",   "JR",    "JALR",  "B",
        "BAL",     "IOR",   "IADDI", "DIV",   "FCGET", "WAITQ",  "FCEQ",  "FCSET", "FCAND", "FCOR",  "FSEQ",  "FSSET",
        "FSAND",   "FSOR",  "FMEQ",  "FMAND", "FMOR",  "IBLTZ",  "IBGTZ", "IBLEZ", "IBGEZ", "XTOP",  "XITOP", "XGKICK"};
    return k < VUK_L_COUNT ? kNames[k] : "?";
}
std::string DestName(int d) {
    std::string s;
    if (d & 8) s += 'x';
    if (d & 4) s += 'y';
    if (d & 2) s += 'z';
    if (d & 1) s += 'w';
    return s.empty() ? "-" : s;
}

}  // namespace

// ---- the region --------------------------------------------------------------

std::string Vu1AotBlockGen::SlotName(uint32_t block_pc, int k) { return "S_" + Pc4(block_pc) + "_" + Dec(k); }

std::vector<const VuBlock*> Vu1AotBlockGen::Region(VuBlockProgram& prog, const VuBlock& head, const std::vector<uint32_t>& emitted) {
    std::vector<const VuBlock*> out{&head};
    int ops = static_cast<int>(head.ops.size());
    auto in_region = [&](uint32_t pc) {
        for (const VuBlock* b : out) {
            if (b->pc == pc) return true;
        }
        return false;
    };
    auto consider = [&](uint32_t pc) {
        pc &= VuBlockTier::kPcMask;
        if (in_region(pc)) return;
        if (static_cast<int>(out.size()) >= kRegionBlockCap) return;
        if (!std::binary_search(emitted.begin(), emitted.end(), pc)) return;  // no slots / no function of its own
        const VuBlock* b = VuBlockTier::BuildBlock(prog, pc >> 3);
        if (b->ops[0].flags & VuBlockOp::kNopPair) return;  // the sled rule is the loop's
        if (ops + static_cast<int>(b->ops.size()) > kRegionOpCap) return;
        ops += static_cast<int>(b->ops.size());
        out.push_back(b);
    };
    // Breadth-first over the successors the function can take itself: the
    // fall-through and, when kRegionTakenTargets, the static branch target
    // (a taken branch to a block outside the region is an exit).
    for (size_t i = 0; i < out.size(); ++i) {
        const VuBlock* b = out[i];
        if (b->e_bit) continue;
        const uint32_t after_body = b->pc + 8u * static_cast<uint32_t>(b->n_body);
        const uint32_t fall = b->has_ds ? after_body + 8u : after_body;
        if (fall <= VuBlockTier::kPcMask) consider(fall);  // the wrap to 0 is the loop's
        if (kRegionTakenTargets && b->branch_idx >= 0 && b->static_target >= 0 && b->has_ds) {
            consider(static_cast<uint32_t>(b->static_target));
        }
    }
    return out;
}

std::string Vu1AotBlockGen::EmitFunction(VuBlockProgram& prog, const VuBlock& head, const std::string& fn_name,
                                         const std::vector<uint32_t>& emitted) {
    Vu1AotBlockGen gen(Region(prog, head, emitted), fn_name);
    gen.analysing_ = true;
    gen.EmitBody();  // pass 1: the sets
    gen.body_.clear();
    gen.indent_ = 1;
    gen.ResetPassState();
    gen.analysing_ = false;
    gen.EmitBody();  // pass 2: the text
    std::string o;
    o += "// region of " + Pc4(head.pc) + ": " + Dec(static_cast<long long>(gen.blocks_.size())) + " block(s)";
    for (const VuBlock* b : gen.blocks_) o += " " + Pc4(b->pc);
    o += "\n";
    o += "int " + fn_name + "(VuUnit& unit, VuInterpreterTier& g, bool& taken, uint32_t& target) {\n";
    o += gen.Prologue();
    o += gen.body_;
    o += "}\n\n";
    return o;
}

Vu1AotBlockGen::Vu1AotBlockGen(std::vector<const VuBlock*> blocks, const std::string& fn_name)
    : blocks_(std::move(blocks)), fn_(fn_name) {
    ResetPassState();
}

void Vu1AotBlockGen::ResetPassState() {
    for (int n = 0; n < 32; ++n) {
        vfc_valid_[n] = false;
        for (int l = 0; l < 4; ++l) last_writer_[n][l] = -1;
    }
    acc_clean_ = false;
    hide_reg_ = -1;
    cur_ = 0;
}

int Vu1AotBlockGen::BlockIndex(uint32_t pc) const {
    for (size_t i = 0; i < blocks_.size(); ++i) {
        if (blocks_[i]->pc == pc) return static_cast<int>(i);
    }
    return -1;
}

// The MAC flag pushes, deferred. UpdateMACFlags pushes a result into the ring
// at its issue; a generated block keeps its results in locals (`m<i>`) and
// delivers them in issue order at the next point that observes the ring (a
// STATUS / MAC reader, a GENERIC word) or at its edges and exits -- with the
// ring arm decided at generation (the generating build's kDefaultLazy,
// recorded in the exports and checked by the installer against the unit).
// RetireDue first (the older entries fold before any of ours), then a result
// issued >= 4 ops before the reference op has retired for sure and folds
// (what RetireDue / the ring overflow would do with it); a younger one is
// pushed with its issue cycle. Every result is folded exactly once in both
// schemes and in the same order, so the sticky bits and the last-retired MAC
// word a reader sees are the same.
void Vu1AotBlockGen::NoteMac(int i, int mask) { pending_.push_back({i, mask}); }

void Vu1AotBlockGen::Deliver(int ref) {
    if (pending_.empty()) return;
    Line("pipes.RetireDue(regs);");
    for (const auto& pm : pending_) {
        const std::string m = "m" + Dec(pm.first) + ".I", mk = Dec(pm.second), c = "c" + Dec(pm.first);
        std::string fold, push;
        if (FlagPipeline::kDefaultLazy) {
            fold = "pipes.FoldLazyRetired(" + m + ", " + mk + ");";
            push = "pipes.PushMacRawAt(" + m + ", " + mk + ", " + c + ");";
        } else {
            fold = "pipes.CommitEagerRetired(regs, VuFloat::PackMacBits(" + m + ", " + mk + "));";
            push = "pipes.PushMacAt(regs, VuFloat::PackMacBits(" + m + ", " + mk + "), " + c + ");";
        }
        if (pm.first <= ref - FlagPipeline::kMacDelay) {
            Line(fold + "  // retired: issued " + Dec(ref - pm.first) + " ops back");
        } else {
            Line(push);  // may or may not have retired: the ring's (no data-dependent branch here)
        }
    }
    pending_.clear();
}

void Vu1AotBlockGen::Line(const std::string& s) {
    if (!s.empty() && s[0] == '}' && indent_ > 1) --indent_;
    body_ += std::string(static_cast<size_t>(indent_) * 4, ' ') + s + "\n";
    if (!s.empty() && s.back() == '{') ++indent_;
}

// ---- registers ------------------------------------------------------------

std::string Vu1AotBlockGen::Vf(int n) {
    if (n == 0) return "kVf0";
    vf_touched_[n] = true;
    if (n == hide_reg_) return "pre";
    return "v" + Dec(n);
}

std::string Vu1AotBlockGen::VfC(int n) {
    if (n == 0) return "kVf0";  // {0,0,0,1} is clamped
    vf_touched_[n] = true;
    if (analysing_) ++clamped_reads_[n];
    vfc_declared_[n] = true;
    if (!vfc_valid_[n]) {
        Line("v" + Dec(n) + "c = VuFloat::ClampVec(" + Vf(n) + ");");
        vfc_valid_[n] = true;
    }
    return "v" + Dec(n) + "c";
}

std::string Vu1AotBlockGen::VfBcC(int n, int bc) {
    if (n == 0) return "VuFloat::Splat4(kVf0.F[" + Dec(bc) + "])";
    vf_touched_[n] = true;
    if (analysing_) ++clamped_reads_[n];
    // One clamped lane of a register read clamped nowhere else in the region:
    // the scalar lane clamp (the body's ClampBits1 of the lane); otherwise the
    // vector view (one ClampVec shared by every read).
    if (!vfc_valid_[n] && clamped_reads_[n] < 2 && !analysing_) {
        return "VuFloat::Splat4(VuFloat::Bits2F(VuFloat::ClampBits1(" + Vf(n) + ".I[" + Dec(bc) + "])))";
    }
    return "VuFloat::Splat4(" + VfC(n) + ".F[" + Dec(bc) + "])";
}

std::string Vu1AotBlockGen::Vi(int n) {
    if (n == 0) return "static_cast<uint16_t>(0)";
    vi_touched_[n] = true;
    return "i" + Dec(n);
}

void Vu1AotBlockGen::WroteVf(int n, bool clamped_full, bool clamped_lanes) {
    if (n == 0) return;
    vf_touched_[n] = true;
    vf_written_[n] = true;
    const bool clean = clamped_full || (clamped_lanes && vfc_valid_[n]);
    if (clean && vfc_declared_[n]) {
        Line("v" + Dec(n) + "c = v" + Dec(n) + ";");
        vfc_valid_[n] = true;
    } else {
        vfc_valid_[n] = false;
    }
}

void Vu1AotBlockGen::WroteVi(int n) {
    if (n == 0) return;
    vi_touched_[n] = true;
    vi_written_[n] = true;
}

void Vu1AotBlockGen::EnsureAccClean() {
    acc_used_ = true;
    if (acc_clean_) return;
    Line("VuFloat::ClampStoreACC(acc);");
    acc_clean_ = true;
}

void Vu1AotBlockGen::WriteLanes(const std::string& dst, const std::string& src, int mask) {
    if (mask == 0xF) {
        Line(dst + " = " + src + ";");
        return;
    }
    for (int k = 0; k < 4; ++k) {
        if (mask & (8 >> k)) Line(dst + ".I[" + Dec(k) + "] = " + src + ".I[" + Dec(k) + "];");
    }
}

void Vu1AotBlockGen::SetVi(int n, const std::string& value_u32) {
    if (n == 0) return;
    WroteVi(n);
    Line("i" + Dec(n) + " = static_cast<uint16_t>(" + value_u32 + ");");
}

void Vu1AotBlockGen::FlushAll() {
    for (int n = 1; n < 32; ++n) {
        if (vf_touched_[n]) Line("regs.VF[" + Dec(n) + "] = v" + Dec(n) + ";");
    }
    for (int n = 1; n < 16; ++n) {
        if (vi_touched_[n]) Line("regs.VI[" + Dec(n) + "] = i" + Dec(n) + ";");
    }
    if (acc_used_) Line("regs.ACC = acc;");
}

void Vu1AotBlockGen::ReloadAll() {
    for (int n = 1; n < 32; ++n) {
        if (vf_touched_[n]) {
            Line("v" + Dec(n) + " = regs.VF[" + Dec(n) + "];");
            vfc_valid_[n] = false;
        }
    }
    for (int n = 1; n < 16; ++n) {
        if (vi_touched_[n]) Line("i" + Dec(n) + " = regs.VI[" + Dec(n) + "];");
    }
    if (acc_used_) Line("acc = regs.ACC;");
    acc_clean_ = false;
}

std::string Vu1AotBlockGen::Prologue() const {
    std::string o;
    o += "    Vu1State& st = unit.state();\n    VUregs& regs = st.regs;\n    FlagPipeline& pipes = unit.flags();\n"
         "    StallModel& stalls = unit.stalls();\n    XgKick* const xgk = unit.xgkick();\n    uint64_t& cycle = pipes.cycle();\n"
         "    uint8_t* const data = st.data;\n    const uint32_t mask = unit.mem_mask();\n"
         "    const uint32_t budget = st.native_budget;\n    int done = 0;\n"
         "    taken = false;\n    bool t = false;\n    uint32_t tg = 0;\n"
         "    constexpr uint64_t kNever = ~0ull;\n"
         "    uint64_t pace_due = (xgk != nullptr && xgk->armed()) ? xgk->next_due() : kNever;\n"
         "    (void)g; (void)stalls; (void)t; (void)tg; (void)target; (void)data; (void)mask; (void)budget;\n";
    // The resident registers: loaded at entry (a dead load is the compiler's to drop).
    for (int n = 1; n < 32; ++n) {
        if (!vf_touched_[n]) continue;
        o += "    VR_reg v" + Dec(n) + " = regs.VF[" + Dec(n) + "];\n";
        if (vfc_declared_[n]) o += "    VR_reg v" + Dec(n) + "c;\n    (void)v" + Dec(n) + "c;\n";
    }
    for (int n = 1; n < 16; ++n) {
        if (vi_touched_[n]) o += "    uint16_t i" + Dec(n) + " = regs.VI[" + Dec(n) + "];\n";
    }
    if (acc_used_) o += "    VR_reg acc = regs.ACC;\n";
    o += StoreBack();
    return o;
}

std::string Vu1AotBlockGen::StoreBack() const {
    std::string o = "    auto store_back = [&]() {\n";
    for (int n = 1; n < 32; ++n) {
        if (vf_written_[n]) o += "        regs.VF[" + Dec(n) + "] = v" + Dec(n) + ";\n";
    }
    for (int n = 1; n < 16; ++n) {
        if (vi_written_[n]) o += "        regs.VI[" + Dec(n) + "] = i" + Dec(n) + ";\n";
    }
    if (acc_used_) o += "        regs.ACC = acc;\n";
    o += "        st.native_resume = false;\n    };\n";
    return o;
}

// ---- the body -------------------------------------------------------------

void Vu1AotBlockGen::EmitBody() {
    for (size_t bi = 0; bi < blocks_.size(); ++bi) EmitBlock(static_cast<int>(bi));
}

// One block of the region: a labelled scope. A label is a fresh entry (the
// caches and the stall locals start over); the end writes TPC as the loop does
// and takes the edge the verdict names, or exits.
void Vu1AotBlockGen::EmitBlock(int bi) {
    const VuBlock& blk = *blocks_[static_cast<size_t>(bi)];
    cur_ = bi;
    for (int n = 0; n < 32; ++n) {
        vfc_valid_[n] = false;
        for (int l = 0; l < 4; ++l) last_writer_[n][l] = -1;
    }
    acc_clean_ = false;
    folded_.assign(blk.ops.size(), 0);
    pending_.clear();
    const int n = static_cast<int>(blk.ops.size());
    body_ += "L_" + Pc4(blk.pc) + ": {\n";
    indent_ = 2;
    body_ += "    // block " + Pc4(blk.pc) + ": " + Dec(blk.n_body) + " ops" + (blk.has_ds ? " + delay slot" : "") +
             (blk.branch_idx >= 0 ? ", branch at op " + Dec(blk.branch_idx) : "") + (blk.e_bit ? ", E bit" : "") + "\n";
    // The budget: the loop's cycle cap must hold the whole block (else the loop
    // runs it, cut or whole, from its head). The head is exempt at the function
    // entry (done == 0: the loop checked it) and checked when a back edge
    // returns to it.
    Line(std::string(bi == 0 ? "if (PS2R_UNLIKELY(done != 0 && " : "if (PS2R_UNLIKELY(") + "static_cast<uint32_t>(done) + " + Dec(n) + "u > budget)) {");
    EmitExitHead(bi);
    Line("}");
    Line("t = false;");
    for (int i = 0; i < n; ++i) EmitOp(bi, i, (blk.pc + 8u * static_cast<uint32_t>(i)) & VuBlockTier::kPcMask);
    Line("done += " + Dec(n) + ";");
    Deliver(n);  // every edge and exit below leaves the block with the ring whole
    // The terminal: TPC as the loop writes it, then the edge.
    const uint32_t after_body = (blk.pc + 8u * static_cast<uint32_t>(blk.n_body)) & VuBlockTier::kPcMask;
    if (blk.e_bit) {
        EmitExitEnd(n - 1);
    } else {
        Line("regs.TPC = " + Hex(after_body) + "u;");
        // The fall-through pc; one that wraps past the image end is the loop's.
        const uint32_t fall_raw = blk.pc + 8u * static_cast<uint32_t>(blk.n_body) + (blk.has_ds ? 8u : 0u);
        const uint32_t fall = fall_raw & VuBlockTier::kPcMask;
        const int fall_bi = fall_raw <= VuBlockTier::kPcMask ? BlockIndex(fall) : -1;
        if (blk.branch_idx >= 0) {
            Line("if (t) {");
            const int target_bi = (blk.static_target >= 0 && blk.has_ds) ? BlockIndex(static_cast<uint32_t>(blk.static_target)) : -1;
            if (target_bi >= 0) {
                EmitMaterialize(n - VuBlockTier::kRingWindow, n - 1);
                Line("goto L_" + Pc4(static_cast<uint32_t>(blk.static_target)) + ";");
            } else {
                EmitExitEnd(n - 1);
            }
            Line("}");
        }
        // Not taken (or no branch): the delay-slot bundle was an ordinary advance.
        Line("regs.TPC = " + Hex(fall) + "u;");
        if (fall_bi >= 0) {
            EmitMaterialize(n - VuBlockTier::kRingWindow, n - 1);
            Line("goto L_" + Pc4(fall) + ";");
        } else {
            EmitExitEnd(n - 1);
        }
    }
    indent_ = 1;
    body_ += "}\n";
}

void Vu1AotBlockGen::EmitOp(int bi, int i, uint32_t pc) {
    const VuBlock& blk = *blocks_[static_cast<size_t>(bi)];
    const VuBlockOp& op = blk.ops[static_cast<size_t>(i)];
    const VuDecodedSlot& s = *op.slot;
    const uint16_t f = op.flags;
    body_ += "        // op " + Dec(i) + " @ " + Pc4(pc) + "  upper=" + Hex(s.upper) + " lower=" + Hex(s.lower) + "  " +
             (op.upper ? UpperName(s.upper_kind) : "nop") + "." + DestName(s.u_dest) + " | " +
             (op.lower ? LowerName(s.lower_kind) : (f & VuBlockOp::kIBit) ? "I" : "-") + "\n";
    cur_op_ = i;
    Line("++cycle;");
    EmitStall(i, op, s);
    Line("if (PS2R_UNLIKELY(cycle >= pace_due)) {");
    Line("xgk->Pace(st.data, cycle);");
    Line("pace_due = xgk->armed() ? xgk->next_due() : kNever;");
    Line("}");
    // The ring's observers: the STATUS / MAC readers (RetireMacStatus) and the
    // GENERIC words (the interpreter's own pushes) see every earlier result.
    const bool observes = (op.upper != nullptr && s.upper_kind == VUK_U_GENERIC) ||
                          (op.lower != nullptr && (s.lower_kind == VUK_L_GENERIC || s.lower_kind == VUK_L_FSEQ ||
                                                   s.lower_kind == VUK_L_FSSET || s.lower_kind == VUK_L_FSAND ||
                                                   s.lower_kind == VUK_L_FSOR || s.lower_kind == VUK_L_FMEQ ||
                                                   s.lower_kind == VUK_L_FMAND || s.lower_kind == VUK_L_FMOR));
    if (observes) Deliver(i);
    if (op.upper != nullptr && (IsFmac(s.upper_kind) || IsAcc(s.upper_kind))) Line("VR_reg m" + Dec(i) + ";");

    // The two words (one scope per op: `pre`, `r`, `a` are per-op names). The
    // lower of a hide-upper bundle reads the pre-upper value of the VF the
    // upper writes: copied before the upper, named `pre` for the lower's reads.
    Line("{");
    int hide = -1;
    hide_reg_ = -1;
    if ((f & VuBlockOp::kHideUpper) && op.lower != nullptr) {
        hide = op.upper_vf_write;
        Line("const VR_reg pre = " + Vf(hide) + ";");
    }
    EmitUpper(op, s);
    if (f & VuBlockOp::kIBit) Line("regs.I = " + Hex(s.lower) + "u;  // the upper read the old I");
    hide_reg_ = hide;
    if (op.lower != nullptr) EmitLower(op, s, pc);
    hide_reg_ = -1;
    Line("}");

    if (f & VuBlockOp::kXgkick) Line("pace_due = xgk->armed() ? xgk->next_due() : kNever;");
    if (f & VuBlockOp::kXtop) {
        Line("if (PS2R_UNLIKELY(st.xtop_exit_requested)) {");
        Line("done += " + Dec(i + 1) + ";");
        const std::vector<std::pair<int, int>> keep = pending_;  // the yield delivers; the fall-through text still owns them
        Deliver(i + 1);
        EmitExitEnd(i);
        Line("}");
        pending_ = keep;
    }
}

// ---- the stall model, folded ---------------------------------------------
//
// The model's step 3 (an FMAC read lane waits for the in-flight write to it)
// over the block's OWN writes needs no table: op k issues at `c<k>` (a local),
// a write at op j retires at c<j> + 4, and the stall is max(0, c<j> + 4 -
// cycle) for the most recent in-block writer j of the lanes read (c<j> grows
// with j, so the largest j is the binding one; a writer more than 3 ops back
// has retired). A write from BEFORE the block is inert from op 3 on (it
// retires <= entry cycle + 4 <= c<3>), so ops 0..2 read the model's lanes and
// push their own writes into it; from op 3 on the writes stay in the locals
// and every edge / exit materialises the last <= 3 ops' writes (VuBlockTier::
// RunOps's own `materialize` on leaving static mode) for the next block's ops
// 0..2 and for the drain (FlushFmacWb reads the newest retire). The IALU
// side (ILW writebacks, the branch stall) stays in the model.

void Vu1AotBlockGen::EmitMaterialize(int first, int last) {
    const VuBlock& blk = *blocks_[static_cast<size_t>(cur_)];
    if (first < 0) first = 0;
    bool any = false;
    for (int j = first; j <= last; ++j) any = any || folded_[static_cast<size_t>(j)] != 0;
    if (!any) return;  // every write of the window is in the model already
    // MaterializeFmacIssued's loop unrolled: one QueueFmac per write, oldest
    // first (the newest write owns its lanes and the newest-retire marker).
    for (int j = first; j <= last; ++j) {
        const VuDecodedSlot& q = *blk.ops[static_cast<size_t>(j)].slot;
        for (int w = 0; w < q.n_wr; ++w) {
            Line("stalls.QueueFmac(" + Dec(q.wr_reg[w]) + ", " + Dec(q.wr_mask[w]) + ", c" + Dec(j) + ");");
        }
    }
}

void Vu1AotBlockGen::EmitStall(int i, const VuBlockOp& op, const VuDecodedSlot& s) {
    const VuBlock& blk = *blocks_[static_cast<size_t>(cur_)];
    const std::string ks = SlotName(blk.pc, i);
    // StallModel::ApplyBody, step by step, with the facts of this slot folded:
    //  3. the FMAC read stall -- the model's lanes for ops 0..2 (a pre-block
    //     write may be in flight), the issue-cycle locals from op 3 on;
    //  4. the IALU branch stall -- the model's (ILW writebacks are queued live);
    //  5./6. the EFU / FDIV pipe waits (class-0 bundles);
    //  7. the VF writebacks -- pushed into the model for ops 0..2 (the next
    //     block's ops 0..2 read them there), folded into locals from op 3 on
    //     (materialised at the edges and exits for the last <= 3 ops);
    //  8. the ILW / ILWR VI writeback.
    const bool warm = i >= VuBlockTier::kWarmupOps;
    int m = 0;
    if (!warm && s.n_rd != 0) m |= StallModel::kRd;
    if (s.ialu_branch_rd != 0) m |= StallModel::kBranch;
    if (m != 0) Line("stalls.ApplySpec<" + Dec(m) + ">(" + ks + ", regs, pipes);  // the model's" +
                     std::string((m & StallModel::kRd) ? " read" : "") + std::string((m & StallModel::kBranch) ? " branch" : "") + " stall");
    if (warm && s.n_rd != 0) {
        int jmax = -1;
        for (int r = 0; r < s.n_rd; ++r) {
            const int reg = s.rd_reg[r] & 31;
            for (int l = 0; l < 4; ++l) {
                if ((s.rd_mask[r] & (8 >> l)) && last_writer_[reg][l] > jmax) jmax = last_writer_[reg][l];
            }
        }
        if (jmax >= i - VuBlockTier::kRingWindow && jmax >= 0) {
            Line("if (c" + Dec(jmax) + " + " + Dec(StallModel::kFmacWbLatency) + " > cycle) cycle = c" + Dec(jmax) + " + " +
                 Dec(StallModel::kFmacWbLatency) + ";  // RAW on op " + Dec(jmax) + "'s write");
        }
    }
    if (s.efu_pipe_op) Line("pipes.ReleasePEarly(regs);  // EFU busy-wait");
    if (s.fdiv_pipe_op) Line("pipes.FlushQPipe(regs);  // FDIV busy-wait");
    if (!warm) {
        for (int w = 0; w < s.n_wr; ++w) Line("stalls.QueueFmac(" + Dec(s.wr_reg[w]) + ", " + Dec(s.wr_mask[w]) + ", cycle);");
    }
    if (s.ialu_wr_vi >= 0) Line("stalls.QueueIalu(" + Dec(s.ialu_wr_vi) + ", cycle);");
    Line("const uint64_t c" + Dec(i) + " = cycle;");
    Line("(void)c" + Dec(i) + ";");
    for (int w = 0; w < s.n_wr; ++w) {
        const int reg = s.wr_reg[w] & 31;
        for (int l = 0; l < 4; ++l) {
            if (s.wr_mask[w] & (8 >> l)) last_writer_[reg][l] = i;
        }
    }
    folded_[static_cast<size_t>(i)] = (warm && s.n_wr != 0) ? 1 : 0;
    (void)op;
}

// The end of the current block after op `last` (its last op, or the XTOP that
// yielded): the model made whole, the registers stored, the loop told which
// block ended where (native_exit_pc / cur_bundle_pc) and its branch verdict.
void Vu1AotBlockGen::EmitExitEnd(int last) {
    const VuBlock& blk = *blocks_[static_cast<size_t>(cur_)];
    EmitMaterialize(last - VuBlockTier::kRingWindow + 1, last);
    Line("store_back();");
    Line("st.native_exit_pc = " + Hex(blk.pc) + "u;");
    Line("st.cur_bundle_pc = " + Hex((blk.pc + 8u * static_cast<uint32_t>(last)) & VuBlockTier::kPcMask) + "u;");
    Line("taken = t;");
    Line("target = tg;");
    Line("return done;");
}

// The head of block `bi` reached with no budget for it: the block has not run
// (the incoming edge materialised its writes already); the loop resumes there.
void Vu1AotBlockGen::EmitExitHead(int bi) {
    const VuBlock& blk = *blocks_[static_cast<size_t>(bi)];
    Line("store_back();");
    Line("st.native_resume = true;");
    Line("st.native_exit_pc = " + Hex(blk.pc) + "u;");
    Line("return done;");
}

void Vu1AotBlockGen::EmitUpper(const VuBlockOp& op, const VuDecodedSlot& s) {
    if (op.upper == nullptr) {  // the NOP arm: the ACC re-clamp only
        EnsureAccClean();
        return;
    }
    const uint8_t K = s.upper_kind;
    const int fd = s.u_fd, fs = s.u_fs, ft = s.u_ft, bc = s.u_bc, D = s.u_dest;
    const std::string Ds = Dec(D);
    if (K == VUK_U_GENERIC) {
        FlushAll();
        Line("g.RunUpper(unit, " + Hex(s.upper) + "u);");
        if (op.flags & VuBlockOp::kTouchHw) Line("VuBlockTier::PinHardwired(regs);");
        ReloadAll();
        return;
    }
    EnsureAccClean();
    if (K == VUK_U_NOP) return;
    if (IsFmac(K)) {
        const std::string s1 = VfC(fs);
        std::string b;
        if (IsFmacBc(K)) b = VfBcC(ft, bc);
        else if (IsFmacVec(K)) b = VfC(ft);
        else if (K == VUK_U_MULQ) b = "VuFloat::Splat4(pipes.VisibleQ(regs))";
        else b = "VuFloat::Splat4(VuFloat::ClampFloat(VuFloat::Bits2F(regs.I)))";  // MUL_I / ADD_I
        std::string kern;
        switch (K) {
            case VUK_U_ADD_BC: case VUK_U_ADD: case VUK_U_ADD_I: kern = "VuFloat::Add4(" + s1 + ", " + b + ")"; break;
            case VUK_U_SUB_BC: case VUK_U_SUB: kern = "VuFloat::Sub4(" + s1 + ", " + b + ")"; break;
            case VUK_U_MUL_BC: case VUK_U_MUL: case VUK_U_MULQ: case VUK_U_MUL_I: kern = "VuFloat::Mul4(" + s1 + ", " + b + ")"; break;
            case VUK_U_MADD_BC: case VUK_U_MADD: kern = "VuFloat::Fma4(" + s1 + ", " + b + ", acc)"; break;
            default: kern = "VuFloat::Fms4(" + s1 + ", " + b + ", acc)"; break;  // MSUB_BC / MSUB
        }
        const std::string m = "m" + Dec(cur_op_);
        Line(m + " = " + kern + ";");
        if (fd != 0) {
            Line("vu_cached_exec::WriteMaskClampD<" + Ds + ">(v" + Dec(fd) + ", " + m + ", " + Ds + ");");
        }
        NoteMac(cur_op_, D);
        if (fd != 0) WroteVf(fd, D == 0xF, true);
        return;
    }
    if (IsAcc(K)) {
        const std::string s1 = VfC(fs);
        std::string b;
        if (K == VUK_U_MULA || K == VUK_U_MADDA) b = VfC(ft);
        else b = VfBcC(ft, bc);
        std::string kern;
        switch (K) {
            case VUK_U_ADDA_BC: kern = "VuFloat::Add4(" + s1 + ", " + b + ")"; break;
            case VUK_U_SUBA_BC: kern = "VuFloat::Sub4(" + s1 + ", " + b + ")"; break;
            case VUK_U_MULA_BC: case VUK_U_MULA: kern = "VuFloat::Mul4(" + s1 + ", " + b + ")"; break;
            case VUK_U_MADDA_BC: case VUK_U_MADDA: kern = "VuFloat::Fma4(" + s1 + ", " + b + ", acc)"; break;
            default: kern = "VuFloat::Fms4(" + s1 + ", " + b + ", acc)"; break;  // MSUBA_BC
        }
        // Lanes outside dest: the flag fold masks them (PackMacBits / the lazy
        // lane mask) and the clamped store leaves them as the incoming ACC.
        const std::string m = "m" + Dec(cur_op_);
        Line(m + " = " + kern + ";");
        NoteMac(cur_op_, D);
        Line("vu_cached_exec::WriteMaskClampD<" + Ds + ">(acc, " + m + ", " + Ds + ");");
        return;
    }
    switch (K) {
        case VUK_U_MAX_BC: case VUK_U_MINI_BC: case VUK_U_MINI_I: {
            if (fd == 0) return;  // protect_vf0
            const std::string bits = K == VUK_U_MINI_I ? "regs.I" : Vf(ft) + ".I[" + Dec(bc) + "]";
            const std::string src = Vf(fs);  // read before the write below (fs may equal fd)
            Line("{");
            Line("VR_reg b4;");
            Line("b4.I[0] = b4.I[1] = b4.I[2] = b4.I[3] = " + bits + ";");
            Line("VuFloat::ApplyMinMax4(v" + Dec(fd) + ", " + src + ", b4, " + Ds + ", false, " +
                 (K == VUK_U_MAX_BC ? "true" : "false") + ");");
            Line("}");
            WroteVf(fd, false, false);
            return;
        }
        case VUK_U_MAX: case VUK_U_MINI: {
            if (fd == 0) return;
            const std::string a = Vf(fs), b = Vf(ft);
            Line("VuFloat::ApplyMinMax4(v" + Dec(fd) + ", " + a + ", " + b + ", " + Ds + ", false, " +
                 (K == VUK_U_MAX ? "true" : "false") + ");");
            WroteVf(fd, false, false);
            return;
        }
        case VUK_U_FTOI0: case VUK_U_FTOI4: case VUK_U_FTOI_N:
        case VUK_U_ITOF0: case VUK_U_ITOF_N: {
            if (ft == 0) return;
            const bool ftoi = K == VUK_U_FTOI0 || K == VUK_U_FTOI4 || K == VUK_U_FTOI_N;
            Line("{");
            Line("const VR_reg r = VuFloat::" + std::string(ftoi ? "Ftoi4Scaled" : "Itof4Scaled") + "(" + Vf(fs) +
                 ", vu_cached_exec::kItofFtoiScale[" + Dec(bc) + "]);");
            // (the write lands on ft: Vf(ft) names the local, never `pre`, since the lower reads it as pre)
            const std::string dst = "v" + Dec(ft);
            if (D == 0xF) Line("" + dst + " = r;");
            else {
                for (int k = 0; k < 4; ++k) {
                    if (D & (8 >> k)) Line("" + dst + ".I[" + Dec(k) + "] = r.I[" + Dec(k) + "];");
                }
            }
            Line("}");
            WroteVf(ft, false, false);
            return;
        }
        case VUK_U_CLIP: {
            const std::string a = Vf(fs), b = Vf(ft);
            Line("g.RunClip(unit, " + a + ", " + b + ");");
            return;
        }
        default: break;
    }
    // Unreachable: every fast upper kind has an arm above (the resolver gave the op a body).
    FlushAll();
    Line("g.RunUpper(unit, " + Hex(s.upper) + "u);  // kind " + Dec(K) + " without an arm");
    ReloadAll();
}

void Vu1AotBlockGen::EmitLower(const VuBlockOp& op, const VuDecodedSlot& s, uint32_t pc) {
    const uint8_t K = s.lower_kind;
    const int ft = s.l_ft, fs = s.l_fs;
    const int vit = ft & 0xF, vis = fs & 0xF, vid = s.l_fd & 0xF;
    const int dest = s.l_dest;
    const int32_t imm11 = s.l_imm11;
    const uint32_t rel_target = static_cast<uint32_t>(static_cast<int32_t>(pc + 8) + imm11 * 8) & VuBlockTier::kPcMask;
    const uint32_t link = ((pc + 16) / 8) & VuInterpreterTier::kLinkMask;
    // The branch VERDICT (t / tg) is the block's branch op's alone (VuBlockOp
    // branch_idx: the loop captures `taken` right after that op). Any other
    // op's verdict is discarded as the loop discards it -- a branch kind on
    // an E-bit bundle, a branch in a delay slot -- and, the case that made a
    // game diverge: a GENERIC lower in the delay slot, whose interpreter body
    // starts with `branch_taken = false` and would have CLEARED the taken
    // branch before the block end read it (E385).
    const bool verdict_op = cur_op_ == blocks_[static_cast<size_t>(cur_)]->branch_idx;
    auto cond_branch = [&](const std::string& cond) {
        if (!verdict_op) return;
        Line("if (" + cond + ") {");
        Line("t = true;");
        Line("tg = " + Hex(rel_target) + "u;");
        Line("}");
    };
    auto qw_addr = [&](int vi) { return "vu_cached_exec::QwAddr(" + Vi(vi) + ", " + Dec(imm11) + ", mask)"; };
    auto load_masked = [&](int reg, const std::string& addr) {
        if (reg == 0) return;
        const std::string dst = "v" + Dec(reg);
        if (dest == 0xF) Line("std::memcpy(" + dst + ".I, data + " + addr + ", 16);");
        else {
            for (int k = 0; k < 4; ++k) {
                if (dest & (8 >> k)) Line(dst + ".I[" + Dec(k) + "] = vu_cached_exec::Le32(data + " + addr + " + " + Dec(4 * k) + ");");
            }
        }
        WroteVf(reg, false, false);
    };
    auto store_masked = [&](const std::string& addr, const std::string& src) {
        if (dest == 0xF) Line("std::memcpy(data + " + addr + ", " + src + ".I, 16);");
        else {
            for (int k = 0; k < 4; ++k) {
                if (dest & (8 >> k)) Line("vu_cached_exec::StoreLe32(data + " + addr + " + " + Dec(4 * k) + ", " + src + ".I[" + Dec(k) + "]);");
            }
        }
    };
    auto write_lanes_all = [&](int reg, const std::string& bits) {
        if (reg == 0) return;
        const std::string dst = "v" + Dec(reg);
        for (int k = 0; k < 4; ++k) {
            if (dest & (8 >> k)) Line(dst + ".I[" + Dec(k) + "] = " + bits + ";");
        }
        WroteVf(reg, false, false);
    };
    auto u32 = [&](int vi) { return "static_cast<uint32_t>(" + Vi(vi) + ")"; };
    auto s16 = [&](int vi) { return "static_cast<int16_t>(" + Vi(vi) + ")"; };

    switch (K) {
        case VUK_L_NOP: return;
        case VUK_L_LQ: {
            if (ft == 0) return;  // load_masked of VF00: nothing (no side effect in the address)
            Line("{");
            Line("const uint32_t a = " + qw_addr(vis) + ";");
            load_masked(ft, "a");
            Line("}");
            return;
        }
        case VUK_L_SQ: {
            const std::string src = Vf(fs);
            Line("{");
            Line("const uint32_t a = " + qw_addr(vit) + ";");
            store_masked("a", src);
            Line("}");
            return;
        }
        case VUK_L_ILW: {
            Line("{");
            Line("const uint32_t a = " + qw_addr(vis) + ";");
            SetVi(vit, "vu_cached_exec::Le32(data + a + vu_cached_exec::IlwOffset(" + Dec(dest) + "))");
            Line("}");
            return;
        }
        case VUK_L_ISW: {
            Line("{");
            Line("const uint32_t a = " + qw_addr(vis) + ";");
            Line("const uint32_t word = " + u32(vit) + ";");
            for (int k = 0; k < 4; ++k) {
                if (dest & (8 >> k)) Line("vu_cached_exec::StoreLe32(data + a + " + Dec(4 * k) + ", word);");
            }
            Line("}");
            return;
        }
        case VUK_L_IADDIU: SetVi(vit, u32(vis) + " + " + Dec(s.l_imm15u) + "u"); return;
        case VUK_L_ISUBIU: SetVi(vit, u32(vis) + " - " + Dec(s.l_imm15u) + "u"); return;
        case VUK_L_IBEQ: cond_branch(Vi(vis) + " == " + Vi(vit)); return;
        case VUK_L_IBNE: cond_branch(Vi(vis) + " != " + Vi(vit)); return;
        case VUK_L_IADD: SetVi(vid, u32(vis) + " + " + u32(vit)); return;
        case VUK_L_ISUB: SetVi(vid, u32(vis) + " - " + u32(vit)); return;
        case VUK_L_IAND: SetVi(vid, u32(vis) + " & " + u32(vit)); return;
        case VUK_L_IOR: SetVi(vid, u32(vis) + " | " + u32(vit)); return;
        case VUK_L_IADDI:
            SetVi(vit, "static_cast<uint32_t>(static_cast<int32_t>(" + s16(vis) + ") + (" + Dec(SignExtend5(s.l_fd)) + "))");
            return;
        case VUK_L_MOVE: {
            if (ft == 0) return;
            const std::string src = Vf(fs);
            WriteLanes("v" + Dec(ft), src, dest);
            WroteVf(ft, false, false);
            return;
        }
        case VUK_L_MR32: {
            if (ft == 0) return;
            const std::string src = Vf(fs);
            Line("{");
            Line("const VR_reg src = " + src + ";  // copied first: fs may equal ft");
            Line("VR_reg v = {};");
            Line("v.x = src.y; v.y = src.z; v.z = src.w; v.w = src.x;");
            if (dest == 0xF) Line("v" + Dec(ft) + " = v;");
            else {
                for (int k = 0; k < 4; ++k) {
                    if (dest & (8 >> k)) Line("v" + Dec(ft) + ".I[" + Dec(k) + "] = v.I[" + Dec(k) + "];");
                }
            }
            Line("}");
            WroteVf(ft, false, false);
            return;
        }
        case VUK_L_MTIR: SetVi(vit, Vf(fs) + ".I[" + Dec(s.l_fsf) + "] & 0xFFFFu"); return;
        case VUK_L_MFIR:
            write_lanes_all(ft, "static_cast<uint32_t>(static_cast<int32_t>(" + s16(vis) + "))");
            return;
        case VUK_L_LQI: {
            if (ft != 0) {
                Line("{");
                Line("const uint32_t a = (" + u32(vis) + " * 16u) & mask;");
                load_masked(ft, "a");
                Line("}");
            }
            SetVi(vis, u32(vis) + " + 1u");
            return;
        }
        case VUK_L_SQI: {
            const std::string src = Vf(fs);
            Line("{");
            Line("const uint32_t a = (" + u32(vit) + " * 16u) & mask;");
            store_masked("a", src);
            Line("}");
            SetVi(vit, u32(vit) + " + 1u");
            return;
        }
        case VUK_L_RGET: write_lanes_all(ft, "regs.R"); return;
        case VUK_L_RINIT: Line("regs.R = (" + Vf(fs) + ".I[" + Dec(s.l_fsf) + "] & 0x7FFFFFu) | 0x3F800000u;"); return;
        case VUK_L_B: cond_branch("true"); return;
        case VUK_L_BAL:
            SetVi(vit, Dec(link) + "u");
            cond_branch("true");
            return;
        case VUK_L_JR:
            if (verdict_op) {
                Line("t = true;");
                Line("tg = (" + u32(vis) + " * 8u) & " + Hex(VuBlockTier::kPcMask) + "u;");
            }
            return;
        case VUK_L_JALR:  // the target is read before the link is written
            if (verdict_op) Line("tg = (" + u32(vis) + " * 8u) & " + Hex(VuBlockTier::kPcMask) + "u;");
            SetVi(vit, Dec(link) + "u");
            if (verdict_op) Line("t = true;");
            return;
        case VUK_L_DIV: {
            const std::string a = Vf(fs), b = Vf(ft);
            Line("g.DivLanes(unit, " + a + ", " + Dec(s.l_fsf) + ", " + b + ", " + Dec((s.lower >> 23) & 3u) + ");");
            return;
        }
        case VUK_L_FCGET:
            Line("{");
            Line("const uint32_t c = pipes.VisibleClip(regs);");
            SetVi(vit, "c & 0xFFFu");
            Line("}");
            return;
        case VUK_L_WAITQ: Line("pipes.FlushQPipe(regs);"); return;
        case VUK_L_FCEQ:
            WroteVi(1);
            Line("i1 = (pipes.VisibleClip(regs) == " + Hex(Imm24(s.lower)) + "u) ? 1 : 0;");
            return;
        case VUK_L_FCSET: Line("pipes.SetClipImmediate(regs, " + Hex(Imm24(s.lower)) + "u);"); return;
        case VUK_L_FCAND:
            WroteVi(1);
            Line("i1 = ((pipes.VisibleClip(regs) & " + Hex(Imm24(s.lower)) + "u) != 0) ? 1 : 0;");
            return;
        case VUK_L_FCOR:
            WroteVi(1);
            Line("i1 = ((pipes.VisibleClip(regs) | " + Hex(Imm24(s.lower)) + "u) == 0xFFFFFFu) ? 1 : 0;");
            return;
        case VUK_L_FSEQ:
            Line("pipes.RetireMacStatus(regs);");
            SetVi(vit, "((regs.status_flag & 0xFFFu) == " + Hex(Imm12(s.lower)) + "u) ? 1 : 0");
            return;
        case VUK_L_FSSET:
            Line("pipes.RetireMacStatus(regs);");
            Line("regs.status_flag = static_cast<uint16_t>((regs.status_flag & 0x3Fu) | " + Hex(Imm12(s.lower) & 0xFC0u) + "u);");
            return;
        case VUK_L_FSAND:
            Line("pipes.RetireMacStatus(regs);");
            SetVi(vit, "(regs.status_flag & 0xFFFu) & " + Hex(Imm12(s.lower)) + "u");
            return;
        case VUK_L_FSOR:
            Line("pipes.RetireMacStatus(regs);");
            SetVi(vit, "(regs.status_flag & 0xFFFu) | " + Hex(Imm12(s.lower)) + "u");
            return;
        case VUK_L_FMEQ:
            Line("pipes.RetireMacStatus(regs);");
            SetVi(vit, "(regs.mac_flag == " + Vi(vis) + ") ? 1 : 0");
            return;
        case VUK_L_FMAND:
            Line("pipes.RetireMacStatus(regs);");
            SetVi(vit, "regs.mac_flag & " + Vi(vis));
            return;
        case VUK_L_FMOR:
            Line("pipes.RetireMacStatus(regs);");
            SetVi(vit, "regs.mac_flag | " + Vi(vis));
            return;
        case VUK_L_IBLTZ: cond_branch(s16(vis) + " < 0"); return;
        case VUK_L_IBGTZ: cond_branch(s16(vis) + " > 0"); return;
        case VUK_L_IBLEZ: cond_branch(s16(vis) + " <= 0"); return;
        case VUK_L_IBGEZ: cond_branch(s16(vis) + " >= 0"); return;
        case VUK_L_XTOP:  // + the VU1 yield rule
            SetVi(vit, "unit.vif_top()");
            Line("if (unit.id() == 1 && ++st.xtop_read_count > VuInterpreterTier::kXtopYieldReads) st.xtop_exit_requested = true;");
            return;
        case VUK_L_XITOP: SetVi(vit, "unit.vif_itop()"); return;
        case VUK_L_XGKICK:  // VU1 only
            Line("if (xgk != nullptr) {");
            Line("xgk->Arm(data, " + Vi(vis) + ", cycle);");
            Line("++st.xgkick_count;");
            Line("st.xgkick_has_fired = true;");
            Line("st.xtop_read_count = 0;");
            Line("}");
            return;
        default: break;
    }
    // GENERIC (and any kind without an arm): the interpreter's body against the
    // unit, its verdict in scratch locals (the body clears them at entry; only
    // the branch op's verdict reaches t / tg).
    FlushAll();
    Line("bool gt = false;");
    Line("uint32_t gtg = 0;");
    Line("g.RunLower(unit, " + Hex(s.lower) + "u, " + Hex(pc) + "u, gt, gtg);");
    if (verdict_op) {
        Line("t = gt;");
        Line("tg = gtg;");
    } else {
        Line("(void)gt; (void)gtg;");
    }
    if (op.flags & VuBlockOp::kTouchHw) Line("VuBlockTier::PinHardwired(regs);");
    ReloadAll();
}
