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

// ---- the GENERIC words with a native body here -------------------------------
//
// The decoder gives these no fast kind (the block tier runs the interpreter's
// body for them); the generated text runs the same arithmetic on the resident
// registers instead of a FlushAll / interpreter call / ReloadAll per word. Each
// shape is the interpreter's body (vu_interpreter_tier_upper.cpp RunUpper /
// RunSpecial2Upper, _lower.cpp RunEfu / RunLowerSpecial) with the operands
// named; the MAC results join the deferred delivery like the FMAC / ACC kinds.
struct GenUpper {
    enum Shape : uint8_t { kNone, kFmacQ, kFmacI, kAccQ, kAccI, kAccVec, kOpmsub, kOpmula, kAbs, kMaxI };
    enum Arith : uint8_t { kAdd, kSub, kMul, kMadd, kMsub };
    Shape shape = kNone;
    Arith arith = kAdd;
    bool mac() const { return shape != kNone && shape != kAbs && shape != kMaxI; }  // delivers a MAC result
};

GenUpper ClassifyGenericUpper(uint32_t u) {
    GenUpper g;
    const uint32_t func = u & 0x3Fu;
    const uint32_t bc = func & 3u;
    auto set = [&](GenUpper::Shape sh, GenUpper::Arith ar) { g.shape = sh; g.arith = ar; };
    if (func >= 0x3C) {
        switch ((u >> 6) & 0x1Fu) {  // the special-2 row; the column is the func's low two bits
            case 0x07:
                if (bc == 0) set(GenUpper::kAccQ, GenUpper::kMul);       // MULAq
                else if (bc == 1) set(GenUpper::kAbs, GenUpper::kAdd);   // ABS
                else if (bc == 2) set(GenUpper::kAccI, GenUpper::kMul);  // MULAi (bc 3 = CLIP: a kind)
                break;
            case 0x08:
                if (bc == 0) set(GenUpper::kAccQ, GenUpper::kAdd);        // ADDAq
                else if (bc == 1) set(GenUpper::kAccQ, GenUpper::kMadd);  // MADDAq
                else if (bc == 2) set(GenUpper::kAccI, GenUpper::kAdd);   // ADDAi
                else set(GenUpper::kAccI, GenUpper::kMadd);               // MADDAi
                break;
            case 0x09:
                if (bc == 0) set(GenUpper::kAccQ, GenUpper::kSub);        // SUBAq
                else if (bc == 1) set(GenUpper::kAccQ, GenUpper::kMsub);  // MSUBAq
                else if (bc == 2) set(GenUpper::kAccI, GenUpper::kSub);   // SUBAi
                else set(GenUpper::kAccI, GenUpper::kMsub);               // MSUBAi
                break;
            case 0x0A:
                if (bc == 0) set(GenUpper::kAccVec, GenUpper::kAdd);  // ADDA (MADDA / MULA are kinds; bc 3 stays generic)
                break;
            case 0x0B:
                if (bc == 0) set(GenUpper::kAccVec, GenUpper::kSub);        // SUBA
                else if (bc == 1) set(GenUpper::kAccVec, GenUpper::kMsub);  // MSUBA
                else if (bc == 2) set(GenUpper::kOpmula, GenUpper::kMul);   // OPMULA (bc 3 = NOP: a kind)
                break;
            default: break;
        }
        return g;
    }
    switch (func) {
        case 0x1D: set(GenUpper::kMaxI, GenUpper::kAdd); break;    // MAXi
        case 0x20: set(GenUpper::kFmacQ, GenUpper::kAdd); break;   // ADDq
        case 0x21: set(GenUpper::kFmacQ, GenUpper::kMadd); break;  // MADDq
        case 0x23: set(GenUpper::kFmacI, GenUpper::kMadd); break;  // MADDi
        case 0x24: set(GenUpper::kFmacQ, GenUpper::kSub); break;   // SUBq
        case 0x25: set(GenUpper::kFmacQ, GenUpper::kMsub); break;  // MSUBq
        case 0x26: set(GenUpper::kFmacI, GenUpper::kSub); break;   // SUBi
        case 0x27: set(GenUpper::kFmacI, GenUpper::kMsub); break;  // MSUBi
        case 0x2E: set(GenUpper::kOpmsub, GenUpper::kMsub); break;  // OPMSUB
        default: break;
    }
    return g;
}

// The lane kernel of an arithmetic on (s1, b, acc): the FMAC kinds' text.
std::string ArithKernel(GenUpper::Arith a, const std::string& s1, const std::string& b) {
    switch (a) {
        case GenUpper::kAdd: return "VuFloat::Add4(" + s1 + ", " + b + ")";
        case GenUpper::kSub: return "VuFloat::Sub4(" + s1 + ", " + b + ")";
        case GenUpper::kMul: return "VuFloat::Mul4(" + s1 + ", " + b + ")";
        case GenUpper::kMadd: return "VuFloat::Fma4(" + s1 + ", " + b + ", acc)";
        default: return "VuFloat::Fms4(" + s1 + ", " + b + ", acc)";
    }
}

// The GENERIC lower words with a native body: the P-pipe family (MFP, WAITP
// and the EFU ops whose bodies are plain lane arithmetic; the libm ones --
// EATAN* / ESIN / EEXP -- stay generic).
enum GenLower : uint8_t { GL_NONE, GL_MFP, GL_WAITP, GL_ESADD, GL_ERSADD, GL_ELENG, GL_ERLENG, GL_ESUM, GL_ESQRT, GL_ERSQRT, GL_ERCPR };

GenLower ClassifyGenericLower(uint32_t l) {
    if (((l >> 25) & 0x7Fu) != 0x40u) return GL_NONE;
    const uint32_t fn = l & 0x3Fu;
    if (fn < 0x3Cu) return GL_NONE;
    const uint32_t t3 = fn - 0x3Cu;
    switch ((l >> 6) & 0x1Fu) {
        case 0x19: return t3 == 0 ? GL_MFP : GL_NONE;
        case 0x1C: return t3 == 0 ? GL_ESADD : t3 == 1 ? GL_ERSADD : t3 == 2 ? GL_ELENG : GL_ERLENG;
        case 0x1D: return t3 == 2 ? GL_ESUM : GL_NONE;
        case 0x1E: return t3 == 0 ? GL_ESQRT : t3 == 1 ? GL_ERSQRT : t3 == 2 ? GL_ERCPR : GL_WAITP;
        default: return GL_NONE;
    }
}

const char* GenLowerName(GenLower g) {
    static const char* const kNames[] = {"GENERIC", "MFP", "WAITP", "ESADD", "ERSADD", "ELENG", "ERLENG", "ESUM", "ESQRT", "ERSQRT", "ERCPR"};
    return kNames[g];
}
std::string GenUpperName(const GenUpper& g) {
    static const char* const kArith[] = {"ADD", "SUB", "MUL", "MADD", "MSUB"};
    switch (g.shape) {
        case GenUpper::kFmacQ: return std::string(kArith[g.arith]) + "q";
        case GenUpper::kFmacI: return std::string(kArith[g.arith]) + "i";
        case GenUpper::kAccQ: return std::string(kArith[g.arith]) + "Aq";
        case GenUpper::kAccI: return std::string(kArith[g.arith]) + "Ai";
        case GenUpper::kAccVec: return std::string(kArith[g.arith]) + "A";
        case GenUpper::kOpmsub: return "OPMSUB";
        case GenUpper::kOpmula: return "OPMULA";
        case GenUpper::kAbs: return "ABS";
        case GenUpper::kMaxI: return "MAXi";
        default: return "GENERIC";
    }
}

}  // namespace

// ---- the region --------------------------------------------------------------

std::string Vu1AotBlockGen::SlotName(uint32_t block_pc, int k) { return "S_" + Pc4(block_pc) + "_" + Dec(k); }
std::string Vu1AotBlockGen::FunctionName(uint32_t block_pc) { return "Blk_" + Pc4(block_pc & VuBlockTier::kPcMask); }
const char* Vu1AotBlockGen::Signature() { return "(VuUnit& unit, VuInterpreterTier& g, bool& taken, uint32_t& target, int done)"; }

std::vector<const VuBlock*> Vu1AotBlockGen::Region(VuBlockProgram& prog, const VuBlock& head, const std::vector<uint32_t>& emitted,
                                                   const Policy& policy) {
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
        if (static_cast<int>(out.size()) >= policy.block_cap) return;
        if (!std::binary_search(emitted.begin(), emitted.end(), pc)) return;  // no slots / no function of its own
        const VuBlock* b = VuBlockTier::BuildBlock(prog, pc >> 3);
        if (b->ops[0].flags & VuBlockOp::kNopPair) return;  // the sled rule is the loop's
        if (ops + static_cast<int>(b->ops.size()) > policy.op_cap) return;
        ops += static_cast<int>(b->ops.size());
        out.push_back(b);
    };
    // Breadth-first over the successors the function can take itself: the
    // fall-through and, when taken_targets, the static branch target (a taken
    // branch to a block outside the region is an exit, or a chain).
    for (size_t i = 0; i < out.size(); ++i) {
        const VuBlock* b = out[i];
        if (b->e_bit) continue;
        const uint32_t after_body = b->pc + 8u * static_cast<uint32_t>(b->n_body);
        const uint32_t fall = b->has_ds ? after_body + 8u : after_body;
        if (fall <= VuBlockTier::kPcMask) consider(fall);  // the wrap to 0 is the loop's
        if (policy.taken_targets && b->branch_idx >= 0 && b->static_target >= 0 && b->has_ds) {
            consider(static_cast<uint32_t>(b->static_target));
        }
    }
    return out;
}

std::string Vu1AotBlockGen::EmitFunction(VuBlockProgram& prog, const VuBlock& head, const std::string& fn_name,
                                         const std::vector<uint32_t>& emitted, const Policy& policy) {
    Vu1AotBlockGen gen(prog, Region(prog, head, emitted, policy), fn_name, emitted, policy);
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
    o += "int " + fn_name + Signature() + " {\n";
    o += gen.Prologue();
    o += gen.body_;
    o += "}\n\n";
    return o;
}

Vu1AotBlockGen::Vu1AotBlockGen(VuBlockProgram& prog, std::vector<const VuBlock*> blocks, const std::string& fn_name,
                               const std::vector<uint32_t>& emitted, const Policy& policy)
    : prog_(prog), blocks_(std::move(blocks)), fn_(fn_name), emitted_(emitted), policy_(policy) {
    mac_pushed_.resize(blocks_.size());
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
//
// Policy::mac_acc (the lazy ring): a result that is FOLDED at every delivery
// point that can take it (pass 1 marks the ones some point pushes: the block
// end folds what an XTOP yield 3 ops after the result had to push) is folded
// into the block-local accumulator `macc` at its op instead (LazyAcc::Fold:
// the same lane sets, OR-ed in registers), and a delivery hands the
// accumulator over once (FoldLazyBatch) with the newest accumulated result
// as the MAC word. The sticky fold is an OR, so the order between the
// accumulated and the explicitly folded results does not matter; the last
// one folded overall must still be the newest, so the batch goes before the
// explicit folds unless the newest folded result is accumulated.
bool Vu1AotBlockGen::MacAccumulates() const { return policy_.mac_acc && FlagPipeline::kDefaultLazy; }

void Vu1AotBlockGen::NoteMac(int i, int mask) {
    pending_.push_back({i, mask});
    if (MacAccumulates() && !analysing_ && !mac_pushed_[static_cast<size_t>(cur_)][static_cast<size_t>(i)]) {
        Line("macc.Fold<" + Dec(mask) + ">(m" + Dec(i) + ".I);  // retired for sure at every delivery");
    }
}

void Vu1AotBlockGen::Deliver(int ref) {
    if (pending_.empty()) return;
    Line("pipes.RetireDue(regs);");
    const bool accumulate = MacAccumulates();
    std::vector<uint8_t>& pushed = mac_pushed_[static_cast<size_t>(cur_)];
    auto fold_of = [&](const std::pair<int, int>& pm) {
        const std::string m = "m" + Dec(pm.first) + ".I", mk = Dec(pm.second);
        if (FlagPipeline::kDefaultLazy) return "pipes.FoldLazyRetired(" + m + ", " + mk + ");";
        return "pipes.CommitEagerRetired(regs, VuFloat::PackMacBits(" + m + ", " + mk + "));";
    };
    auto push_of = [&](const std::pair<int, int>& pm) {
        const std::string m = "m" + Dec(pm.first) + ".I", mk = Dec(pm.second), c = "c" + Dec(pm.first);
        if (FlagPipeline::kDefaultLazy) return "pipes.PushMacRawAt(" + m + ", " + mk + ", " + c + ");";
        return "pipes.PushMacAt(regs, VuFloat::PackMacBits(" + m + ", " + mk + "), " + c + ");";
    };
    // The folded prefix: the newest accumulated one (the batch's MAC word) and
    // whether the newest folded overall is accumulated.
    int newest_acc = -1, newest_acc_mask = 0, newest_folded = -1;
    for (const auto& pm : pending_) {
        if (pm.first > ref - FlagPipeline::kMacDelay) continue;
        newest_folded = pm.first;
        if (accumulate && !pushed[static_cast<size_t>(pm.first)]) {
            newest_acc = pm.first;
            newest_acc_mask = pm.second;
        }
    }
    auto batch = [&]() {
        if (newest_acc < 0) return;
        Line("pipes.FoldLazyBatch(macc, m" + Dec(newest_acc) + ".I, " + Dec(newest_acc_mask) + ");  // the accumulated folds, newest = the MAC word");
    };
    if (newest_acc >= 0 && newest_acc != newest_folded) batch();
    for (const auto& pm : pending_) {
        if (pm.first <= ref - FlagPipeline::kMacDelay) {
            if (accumulate && !pushed[static_cast<size_t>(pm.first)]) continue;  // in the batch
            Line(fold_of(pm) + "  // retired: issued " + Dec(ref - pm.first) + " ops back");
        } else {
            pushed[static_cast<size_t>(pm.first)] = 1;  // pass 1: this result cannot accumulate
            Line(push_of(pm));  // may or may not have retired: the ring's (no data-dependent branch here)
        }
    }
    if (newest_acc >= 0 && newest_acc == newest_folded) batch();
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
         "    const uint32_t budget = st.native_budget;\n"
         "    taken = false;\n    bool t = false;\n    uint32_t tg = 0;\n"
         "    constexpr uint64_t kNever = ~0ull;\n"
         "    uint64_t pace_due = (xgk != nullptr && xgk->armed()) ? xgk->next_due() : kNever;\n"
         "    (void)g; (void)stalls; (void)t; (void)tg; (void)target; (void)data; (void)mask; (void)budget;\n";
    if (MacAccumulates()) o += "    FlagPipeline::LazyAcc macc;\n    macc.Reset();\n";
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
    if (analysing_) mac_pushed_[static_cast<size_t>(bi)].assign(blk.ops.size(), 0);  // pass 2 reads pass 1's marks
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
            } else if (!(blk.static_target >= 0 && blk.has_ds && EmitChain(static_cast<uint32_t>(blk.static_target), n - 1))) {
                EmitExitEnd(n - 1);
            }
            Line("}");
        }
        // Not taken (or no branch): the delay-slot bundle was an ordinary advance.
        Line("regs.TPC = " + Hex(fall) + "u;");
        if (fall_bi >= 0) {
            EmitMaterialize(n - VuBlockTier::kRingWindow, n - 1);
            Line("goto L_" + Pc4(fall) + ";");
        } else if (!(fall_raw <= VuBlockTier::kPcMask && EmitChain(fall, n - 1))) {
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
    // The GENERIC words with a native body here (see ClassifyGeneric*): they
    // neither observe the ring nor run the interpreter.
    const GenUpper gu = (op.upper != nullptr && s.upper_kind == VUK_U_GENERIC) ? ClassifyGenericUpper(s.upper) : GenUpper{};
    const GenLower gl = (op.lower != nullptr && s.lower_kind == VUK_L_GENERIC) ? ClassifyGenericLower(s.lower) : GL_NONE;
    body_ += "        // op " + Dec(i) + " @ " + Pc4(pc) + "  upper=" + Hex(s.upper) + " lower=" + Hex(s.lower) + "  " +
             (op.upper ? (s.upper_kind == VUK_U_GENERIC ? GenUpperName(gu) : UpperName(s.upper_kind)) : "nop") + "." +
             DestName(s.u_dest) + " | " +
             (op.lower ? (s.lower_kind == VUK_L_GENERIC ? GenLowerName(gl) : LowerName(s.lower_kind)) : (f & VuBlockOp::kIBit) ? "I" : "-") + "\n";
    cur_op_ = i;
    Line("++cycle;");
    EmitStall(i, op, s);
    Line("if (PS2R_UNLIKELY(cycle >= pace_due)) {");
    Line("xgk->Pace(st.data, cycle);");
    Line("pace_due = xgk->armed() ? xgk->next_due() : kNever;");
    Line("}");
    // The ring's observers: the STATUS / MAC readers (RetireMacStatus) and the
    // GENERIC words (the interpreter's own pushes) see every earlier result.
    const bool observes = (op.upper != nullptr && s.upper_kind == VUK_U_GENERIC && gu.shape == GenUpper::kNone) ||
                          (op.lower != nullptr && ((s.lower_kind == VUK_L_GENERIC && gl == GL_NONE) || s.lower_kind == VUK_L_FSEQ ||
                                                   s.lower_kind == VUK_L_FSSET || s.lower_kind == VUK_L_FSAND ||
                                                   s.lower_kind == VUK_L_FSOR || s.lower_kind == VUK_L_FMEQ ||
                                                   s.lower_kind == VUK_L_FMAND || s.lower_kind == VUK_L_FMOR));
    if (observes) Deliver(i);
    if (op.upper != nullptr && (IsFmac(s.upper_kind) || IsAcc(s.upper_kind) || gu.mac())) Line("VR_reg m" + Dec(i) + ";");

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

// The chain (Policy::chain): the successor's own function continues the run
// with the registers stored back and the model made whole -- exactly the state
// an exit leaves for the loop, minus the exit's bookkeeping and the loop's
// round trip. The successor must be a block of the TU (it has a function),
// outside this region (inside, the edge is a goto) and not a NOP pair (the
// sled rule is the loop's). Its head check is the budget check the loop would
// have made before calling it (`done + n > budget` with the carried `done` is
// the loop's `n <= cycle_limit` on the remaining cap).
bool Vu1AotBlockGen::Chainable(uint32_t pc) const {
    if (!policy_.chain) return false;
    pc &= VuBlockTier::kPcMask;
    if (BlockIndex(pc) >= 0) return false;
    if (!std::binary_search(emitted_.begin(), emitted_.end(), pc)) return false;
    const VuBlock* b = VuBlockTier::BuildBlock(prog_, pc >> 3);
    return (b->ops[0].flags & VuBlockOp::kNopPair) == 0;
}

bool Vu1AotBlockGen::EmitChain(uint32_t pc, int last) {
    if (!Chainable(pc)) return false;
    pc &= VuBlockTier::kPcMask;
    EmitMaterialize(last - VuBlockTier::kRingWindow + 1, last);
    Line("store_back();");
    Line("VU1_AOT_MUSTTAIL return " + FunctionName(pc) + "(unit, g, taken, target, done);  // chain");
    return true;
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
        const GenUpper g = ClassifyGenericUpper(s.upper);
        if (g.shape == GenUpper::kNone) {
            FlushAll();
            Line("g.RunUpper(unit, " + Hex(s.upper) + "u);");
            if (op.flags & VuBlockOp::kTouchHw) Line("VuBlockTier::PinHardwired(regs);");
            ReloadAll();
            return;
        }
        // RunUpper's prologue: the sources read clamped, the ACC clamped for
        // every upper word (ABS / MAXi included).
        EnsureAccClean();
        const std::string m = "m" + Dec(cur_op_);
        const std::string q = "VuFloat::Splat4(pipes.VisibleQ(regs))";
        const std::string iv = "VuFloat::Splat4(VuFloat::ClampFloat(VuFloat::Bits2F(regs.I)))";
        switch (g.shape) {
            case GenUpper::kFmacQ: case GenUpper::kFmacI: {
                // write_fd: the clamped result into fd's dest lanes (VF00 protected), the flags on the raw one.
                const std::string s1 = VfC(fs);
                Line(m + " = " + ArithKernel(g.arith, s1, g.shape == GenUpper::kFmacQ ? q : iv) + ";");
                if (fd != 0) Line("vu_cached_exec::WriteMaskClampD<" + Ds + ">(v" + Dec(fd) + ", " + m + ", " + Ds + ");");
                NoteMac(cur_op_, D);
                if (fd != 0) WroteVf(fd, D == 0xF, true);
                return;
            }
            case GenUpper::kAccQ: case GenUpper::kAccI: case GenUpper::kAccVec: {
                // acc_s / acc_v + acc_tail: every lane computed, the flags judge the dest lanes, the
                // dest lanes of the ACC take the clamped result and the others keep the incoming ACC.
                const std::string s1 = VfC(fs);
                const std::string b = g.shape == GenUpper::kAccQ ? q : g.shape == GenUpper::kAccI ? iv : VfC(ft);
                Line(m + " = " + ArithKernel(g.arith, s1, b) + ";");
                NoteMac(cur_op_, D);
                Line("vu_cached_exec::WriteMaskClampD<" + Ds + ">(acc, " + m + ", " + Ds + ");");
                return;
            }
            case GenUpper::kOpmula: {  // ACC.xyz = the outer product of the clamped sources, w untouched
                const std::string a = VfC(fs), b = VfC(ft);
                Line(m + ".F[0] = " + a + ".F[1] * " + b + ".F[2];");
                Line(m + ".F[1] = " + a + ".F[2] * " + b + ".F[0];");
                Line(m + ".F[2] = " + a + ".F[0] * " + b + ".F[1];");
                Line(m + ".I[3] = acc.I[3];");
                NoteMac(cur_op_, D);
                Line("vu_cached_exec::WriteMaskClampD<" + Ds + ">(acc, " + m + ", " + Ds + ");");
                return;
            }
            case GenUpper::kOpmsub: {  // fd.xyz = ACC - the outer product (one rounding per lane), w = ACC.w
                const std::string a = VfC(fs), b = VfC(ft);
                Line(m + ".F[0] = std::fmaf(-" + a + ".F[1], " + b + ".F[2], acc.F[0]);");
                Line(m + ".F[1] = std::fmaf(-" + a + ".F[2], " + b + ".F[0], acc.F[1]);");
                Line(m + ".F[2] = std::fmaf(-" + a + ".F[0], " + b + ".F[1], acc.F[2]);");
                Line(m + ".I[3] = acc.I[3];");
                if (fd != 0) Line("vu_cached_exec::WriteMaskClampD<" + Ds + ">(v" + Dec(fd) + ", " + m + ", " + Ds + ");");
                NoteMac(cur_op_, D);
                if (fd != 0) WroteVf(fd, D == 0xF, true);
                return;
            }
            case GenUpper::kAbs: {  // the raw source with the sign cleared (fabs is the bit operation), no flags
                if (ft == 0) return;
                Line("{");
                Line("const VR_reg src = " + Vf(fs) + ";  // copied first: fs may equal ft");
                for (int k = 0; k < 4; ++k) {
                    if (D & (8 >> k)) Line("v" + Dec(ft) + ".I[" + Dec(k) + "] = src.I[" + Dec(k) + "] & 0x7FFFFFFFu;");
                }
                Line("}");
                WroteVf(ft, false, false);
                return;
            }
            case GenUpper::kMaxI: {  // MINI_I's body with the max rule
                if (fd == 0) return;
                const std::string src = Vf(fs);
                Line("{");
                Line("VR_reg b4;");
                Line("b4.I[0] = b4.I[1] = b4.I[2] = b4.I[3] = regs.I;");
                Line("VuFloat::ApplyMinMax4(v" + Dec(fd) + ", " + src + ", b4, " + Ds + ", false, true);");
                Line("}");
                WroteVf(fd, false, false);
                return;
            }
            default: break;
        }
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
        case VUK_U_CLIP: {  // RunClip's body inline: the raw operands judged, the clip ring pushed
            const std::string a = Vf(fs), b = Vf(ft);
            Line("pipes.PushClip(regs, (pipes.clip_latest() << 6) | VuInterpreterTier::ClipJudge(" + a + ", " + b + "));");
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
        case VUK_L_DIV: {  // DivLanes' body inline: the clamped lanes, the quotient + status into the Q pipe
            const std::string a = Vf(fs), b = Vf(ft);
            Line("{");
            Line("uint16_t qst;");
            Line("const float q = VuInterpreterTier::DivQuotient(VuFloat::ClampFloat(" + a + ".F[" + Dec(s.l_fsf) + "]), VuFloat::ClampFloat(" +
                 b + ".F[" + Dec((s.lower >> 23) & 3u) + "]), qst);");
            Line("pipes.PushQ(regs, q, FlagPipeline::kDivDelay, qst);");
            Line("}");
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
    if (K == VUK_L_GENERIC) {
        const GenLower g = ClassifyGenericLower(s.lower);
        if (g != GL_NONE) {
            // RunEfu's bodies: the source read clamped (the vector for the sum
            // family, the fsf lane otherwise), the result into the P pipe with
            // the op's latency; WAITP's wait is the stall model's (EmitStall:
            // ReleasePEarly), its body is empty; MFP reads the visible P.
            switch (g) {
                case GL_WAITP: return;
                case GL_MFP: write_lanes_all(ft, "VuFloat::F2Bits(pipes.VisibleP(regs))"); return;
                case GL_ESADD: case GL_ERSADD: case GL_ELENG: case GL_ERLENG: case GL_ESUM: {
                    // The raw read (the `pre` rule applies: a lower never reads the clamp cache the upper refreshed).
                    Line("{");
                    Line("const VR_reg e = VuFloat::ClampVec(" + Vf(fs) + ");");
                    if (g == GL_ESUM) {
                        Line("pipes.PushP(regs, e.F[0] + e.F[1] + e.F[2] + e.F[3], FlagPipeline::kEfuEsum);");
                    } else {
                        Line("const float ss = VuInterpreterTier::EfuSumSq(e);");
                        if (g == GL_ESADD) Line("pipes.PushP(regs, ss, FlagPipeline::kEfuEsadd);");
                        else if (g == GL_ERSADD) Line("pipes.PushP(regs, VuInterpreterTier::EfuRecip(ss), FlagPipeline::kEfuErsadd);");
                        else if (g == GL_ELENG) Line("pipes.PushP(regs, std::sqrt(ss), FlagPipeline::kEfuEleng);");
                        else Line("pipes.PushP(regs, VuInterpreterTier::EfuRecip(std::sqrt(ss)), FlagPipeline::kEfuErleng);");
                    }
                    Line("}");
                    return;
                }
                default: {  // ESQRT / ERSQRT / ERCPR: the clamped fsf lane
                    Line("{");
                    Line("const float lane = VuFloat::ClampFloat(" + Vf(fs) + ".F[" + Dec(s.l_fsf) + "]);");
                    if (g == GL_ESQRT) Line("pipes.PushP(regs, std::sqrt(std::fabs(lane)), FlagPipeline::kEfuEsqrt);");
                    else if (g == GL_ERSQRT) Line("pipes.PushP(regs, VuInterpreterTier::EfuRecip(std::sqrt(std::fabs(lane))), FlagPipeline::kEfuErsqrt);");
                    else Line("pipes.PushP(regs, VuInterpreterTier::EfuRecip(lane), FlagPipeline::kEfuErcpr);");
                    Line("}");
                    return;
                }
            }
        }
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
