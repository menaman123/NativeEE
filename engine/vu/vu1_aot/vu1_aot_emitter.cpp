// Vu1AotEmitter -- the per-block C++ emission. Rules in the header; the
// per-op text mirrors VuBlockTier::RunOps<false> (the dynamic-stall form: the
// model is consulted per op through its class-specialised entry with the
// constant slot, which folds to the lane reads the class names), arm for arm.
#include "engine/vu/vu1_aot/vu1_aot_emitter.h"

#include <algorithm>
#include <cstdio>

#include "engine/vu/tiers/vu_block_tier.h"
#include "engine/vu/vu1_aot/vu1_aot_block_gen.h"
#include "engine/vu/vu1_aot/vu1_aot_exports.h"
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

void Field(std::string& out, const char* name, long long v) {
    out += "    s." + std::string(name) + " = " + Dec(v) + ";\n";
}
void FieldBool(std::string& out, const char* name, bool v) {
    out += "    s." + std::string(name) + " = " + (v ? "true" : "false") + ";\n";
}

}  // namespace

std::string Vu1AotEmitter::Hex16(uint64_t v) {
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

std::string Vu1AotEmitter::EntrySymbol(uint64_t image_hash) { return std::string(kVu1AotExportsPrefix) + Hex16(image_hash); }

// Every field of the pinned POD, by name, in a constexpr lambda: a field the
// decoder adds later is left at its default here, which the re-verification
// gate would catch (not the compiler).
std::string Vu1AotEmitter::EmitSlot(const VuDecodedSlot& s, const std::string& name) {
    std::string o = "static constexpr VuDecodedSlot " + name + " = [] {\n    VuDecodedSlot s{};\n";
    o += "    s.upper = " + Hex(s.upper) + "u;\n    s.lower = " + Hex(s.lower) + "u;\n";
    Field(o, "upper_vf_write", s.upper_vf_write);
    FieldBool(o, "i_bit", s.i_bit);
    FieldBool(o, "e_bit", s.e_bit);
    FieldBool(o, "is_nop_pair", s.is_nop_pair);
    FieldBool(o, "discard_lower", s.discard_lower);
    FieldBool(o, "hide_upper", s.hide_upper);
    Field(o, "upper_kind", s.upper_kind);
    Field(o, "lower_kind", s.lower_kind);
    Field(o, "n_rd", s.n_rd);
    Field(o, "n_wr", s.n_wr);
    for (int i = 0; i < 5; ++i) {
        o += "    s.rd_reg[" + Dec(i) + "] = " + Dec(s.rd_reg[i]) + ";\n";
        o += "    s.rd_mask[" + Dec(i) + "] = " + Dec(s.rd_mask[i]) + ";\n";
    }
    for (int i = 0; i < 2; ++i) {
        o += "    s.wr_reg[" + Dec(i) + "] = " + Dec(s.wr_reg[i]) + ";\n";
        o += "    s.wr_mask[" + Dec(i) + "] = " + Dec(s.wr_mask[i]) + ";\n";
    }
    o += "    s.rd_regs = " + Hex(s.rd_regs) + "u;\n";
    Field(o, "ialu_wr_vi", s.ialu_wr_vi);
    Field(o, "fold_class", s.fold_class);
    Field(o, "ialu_branch_rd", s.ialu_branch_rd);
    FieldBool(o, "efu_pipe_op", s.efu_pipe_op);
    FieldBool(o, "fdiv_pipe_op", s.fdiv_pipe_op);
    Field(o, "u_fd", s.u_fd);
    Field(o, "u_fs", s.u_fs);
    Field(o, "u_ft", s.u_ft);
    Field(o, "u_dest", s.u_dest);
    Field(o, "u_bc", s.u_bc);
    Field(o, "l_fs", s.l_fs);
    Field(o, "l_ft", s.l_ft);
    Field(o, "l_fd", s.l_fd);
    Field(o, "l_dest", s.l_dest);
    Field(o, "l_fsf", s.l_fsf);
    Field(o, "l_imm11", s.l_imm11);
    Field(o, "l_imm15u", s.l_imm15u);
    Field(o, "touch_hw", s.touch_hw);
    Field(o, "stall_class", s.stall_class);
    Field(o, "run_len", s.run_len);
    o += "    return s;\n}();\n";
    return o;
}

// One block's function: its region (the block and the blocks it can reach over
// fall-through / static targets within the cap) with the register file in
// locals (Vu1AotBlockGen). The slots are emitted once per block of the TU
// (EmitBlockSlots), named by the block's pc.
std::string Vu1AotEmitter::EmitBlock(VuBlockProgram& prog, const VuBlock& blk, const std::string& fn_name,
                                     const std::vector<uint32_t>& emitted, const Vu1AotBlockGen::Policy& policy) {
    return Vu1AotBlockGen::EmitFunction(prog, blk, fn_name, emitted, policy);
}

std::string Vu1AotEmitter::PolicyName(const Vu1AotBlockGen::Policy& p) {
    return "blocks=" + Dec(p.block_cap) + ":ops=" + Dec(p.op_cap) + ":taken=" + (p.taken_targets ? "1" : "0") + ":chain=" + (p.chain ? "1" : "0") + ":mac_acc=" + (p.mac_acc ? "1" : "0");
}

std::string Vu1AotEmitter::EmitBlockSlots(const VuBlock& blk) {
    const int n = static_cast<int>(blk.ops.size());
    std::string o;
    o += "// block " + Pc4(blk.pc) + ": " + Dec(blk.n_body) + " ops" + (blk.has_ds ? " + delay slot" : "") +
         (blk.branch_idx >= 0 ? ", branch at op " + Dec(blk.branch_idx) : "") + (blk.e_bit ? ", E bit" : "") + "\n";
    for (int i = 0; i < n; ++i) o += EmitSlot(*blk.ops[static_cast<size_t>(i)].slot, Vu1AotBlockGen::SlotName(blk.pc, i));
    return o;
}

std::vector<uint32_t> Vu1AotEmitter::ReachableBlocks(VuBlockProgram& prog, const std::vector<uint32_t>& entry_pcs, size_t cap) {
    const size_t n_slots = prog.slots.size();
    std::vector<uint8_t> seen(n_slots, 0);
    std::vector<uint32_t> work;
    std::vector<uint32_t> out;
    auto push = [&](uint32_t pc) {
        pc &= VuBlockTier::kPcMask;
        if ((pc & 7u) != 0 || (pc >> 3) >= n_slots || seen[pc >> 3]) return;
        seen[pc >> 3] = 1;
        work.push_back(pc);
    };
    for (uint32_t pc : entry_pcs) push(pc);
    while (!work.empty() && out.size() < cap) {
        const uint32_t pc = work.back();
        work.pop_back();
        const VuBlock* blk = VuBlockTier::BuildBlock(prog, pc >> 3);
        // A NOP pair is a one-op block the loop's sled rule owns: walked
        // through (code may follow fewer than four), never emitted.
        if ((blk->ops[0].flags & VuBlockOp::kNopPair) == 0) out.push_back(pc);
        if (blk->e_bit) continue;
        const uint32_t after_body = (pc + 8u * static_cast<uint32_t>(blk->n_body)) & VuBlockTier::kPcMask;
        if (blk->branch_idx >= 0) {
            if (blk->static_target >= 0) push(static_cast<uint32_t>(blk->static_target));
            if (!blk->has_ds) push(after_body);  // the wrapped delay slot's block
        }
        push(blk->has_ds ? ((after_body + 8u) & VuBlockTier::kPcMask) : after_body);  // fall-through
    }
    std::sort(out.begin(), out.end());
    return out;
}

Vu1AotEmitter::Output Vu1AotEmitter::Emit(VuBlockProgram& prog, const std::vector<uint32_t>& block_pcs, const Options& opt) {
    Output out;
    out.entry_symbol = EntrySymbol(prog.hash);
    std::string& o = out.source;
    o += "// GENERATED by Vu1AotEmitter (engine/vu/vu1_aot/) for VU1 image " + Hex16(prog.hash) + " -- DO NOT EDIT.\n";
    o += "// engine build: " + opt.engine_build + "\n";
    if (!opt.note.empty()) o += "// " + opt.note + "\n";
    o += "// One function per block of the block tier's table: the block's ops with the VU register file\n"
         "// resident in locals (Vu1AotBlockGen), every slot a compile-time constant, the engine's own lane\n"
         "// kernels (VuFloat) and pipelines (FlagPipeline, StallModel, XgKick) as the primitives.\n";
    o += "// region policy: " + PolicyName(opt.region) + "\n";
    o += "#include \"engine/vu/vu1_aot/vu1_aot_prelude.h\"\n\n";
    if (opt.region.chain) {
        o += "#if !VU1_AOT_HAS_MUSTTAIL\n#error \"this TU chains its block functions: it needs a compiler with a guaranteed tail call (clang, gcc >= 15)\"\n#endif\n\n";
    }
    o += "namespace {\n\n";
    o += "constexpr VR_reg kVf0 = {{0.0f, 0.0f, 0.0f, 1.0f}};  // VF00, hard-wired\n\n";
    std::vector<uint32_t> emitted(block_pcs);
    for (uint32_t& pc : emitted) pc &= VuBlockTier::kPcMask;
    std::sort(emitted.begin(), emitted.end());
    // The slots of every block first (a region names the slots of the blocks it
    // reaches), then the declaration of every block function (a chain names
    // its successor's), then one region function per block.
    for (uint32_t pc : emitted) o += EmitBlockSlots(*VuBlockTier::BuildBlock(prog, pc >> 3));
    o += "\n";
    for (uint32_t pc : emitted) o += "int " + Vu1AotBlockGen::FunctionName(pc) + Vu1AotBlockGen::Signature() + ";\n";
    o += "\n";
    std::vector<std::string> names;
    for (uint32_t pc : block_pcs) {
        const VuBlock* blk = VuBlockTier::BuildBlock(prog, (pc & VuBlockTier::kPcMask) >> 3);
        const std::string fn = Vu1AotBlockGen::FunctionName(blk->pc);
        o += EmitBlock(prog, *blk, fn, emitted, opt.region);
        names.push_back(fn);
        ++out.blocks;
        out.ops += blk->ops.size();
    }
    o += "const Vu1AotBlockExport kBlocks[] = {\n";
    for (size_t i = 0; i < block_pcs.size(); ++i) {
        const VuBlock* blk = prog.block_at[(block_pcs[i] & VuBlockTier::kPcMask) >> 3];
        o += "    {" + Hex(blk->pc) + "u, " + Dec(static_cast<long long>(blk->ops.size())) + "u, &" + names[i] + "},\n";
    }
    if (block_pcs.empty()) o += "    {0u, 0u, nullptr},\n";
    o += "};\n\n";
    o += "const Vu1AotImageExports kExports = {\n    kVu1AotAbiVersion,\n    static_cast<uint32_t>(sizeof(VuDecodedSlot)),\n";
    o += "    0x" + Hex16(prog.hash) + "ull,\n    \"" + opt.engine_build + "\",\n    VU1_AOT_COMPILER,\n    " +
         Dec(static_cast<long long>(block_pcs.size())) + "u,\n    kBlocks,\n    FlagPipeline::kDefaultLazy ? 1u : 0u,\n};\n\n}  // namespace\n\n";
    o += "extern \"C\" const Vu1AotImageExports* " + out.entry_symbol + "() { return &kExports; }\n";
    return out;
}
