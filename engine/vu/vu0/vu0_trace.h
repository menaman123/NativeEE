#pragma once
// Vu0Trace -- the VU0 macro-mode operand census: for every VCALLMS / VCALLMSR
// entry whose micro pc is on the armed list, the COP2 register block the
// program sees on entry (VF01..VF31, ACC, Q, I) and the block it leaves
// behind (VF01..VF31, ACC, Q), both as raw bits and as decimal floats. The
// whole VF file is recorded: a libvu0-style program takes operands in high
// registers (VF29..VF31) as readily as in low ones.
//
// PS2 rule: in macro mode the EE's COP2 registers are VU0's register file;
// a VCALLMS runs the microprogram synchronously, so the operands of the
// whole call are the register block at the entry and its result is the block
// at the E bit. Recording both at the bridge's hand-over is exact: nothing
// else touches the block while the program runs.
//
// A diagnostics mechanism: the harness arms it with
// `--vu0-trace 0xPC[,0xPC...]` (a CLI knob, never a Config key); the
// composition root binds it to VuMacroBridge and flushes <leg>/vu0_trace.log.
// Unarmed, the bridge pays one null-pointer test per entry. No micro pc, no
// title address lives here: the list is handed in.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct VUregs;
struct EmotionEngineState;
typedef EmotionEngineState CpuContext;

class Vu0Trace {
public:
    // "0xPC[,0xPC...]" (hex or decimal micro byte addresses, 8-byte aligned by
    // truncation). Replaces the current list. False + *err on a malformed item.
    bool Parse(const std::string& list, std::string* err);
    void Clear();

    bool Armed() const { return !pcs_.empty(); }
    bool Wants(uint32_t pc) const;
    const std::vector<uint32_t>& Pcs() const { return pcs_; }

    void SetBoundary(uint64_t boundary) { boundary_ = boundary; }

    // The bridge's two hooks around one VU0 run of an armed pc: the block as
    // the program sees it and the block it leaves behind. `micro` /
    // `micro_size` = the unit's micro memory: the program's first kCodeBundles
    // bundles are recorded on the first call of the pc and again whenever the
    // 16 bytes at pc change (the title re-uploaded the slot).
    void Begin(uint32_t pc, const VUregs& in, const uint8_t* micro, size_t micro_size);
    // After Begin: the EE's argument / temporary / stack registers at the
    // VCALLMS (a0..a3, t0..t3, sp), so the RAM operands a program's caller
    // loaded with LQC2 / QMTC2 can be found by address.
    void NoteEeRegs(const CpuContext& ctx);
    void End(uint32_t pc, const VUregs& out);

    uint64_t Calls() const { return calls_; }
    // "<pc>:<calls>,..." per armed pc (run.meta vu0_trace=).
    std::string Summary() const;
    // Writes the recorded lines (the first kMaxLines; later calls are counted
    // only) to `path`; true when the file was written.
    bool Flush(const std::string& path) const;

    static constexpr size_t kMaxLines = 400000;
    static constexpr int kFirstReg = 1, kLastReg = 31;  // VF01..VF31 recorded (VF00 is the constant)
    static constexpr uint32_t kCodeBundles = 64;

private:
    void AppendBlock(const char* tag, const VUregs& r, bool with_i);

    std::vector<uint32_t> pcs_;
    std::vector<uint64_t> per_pc_calls_;
    std::vector<uint64_t> last_head_lo_, last_head_hi_;  // the 16 bytes at pc last recorded (per armed pc)
    std::vector<bool> code_seen_;
    uint64_t boundary_ = 0;
    uint64_t calls_ = 0;
    std::vector<std::string> lines_;
};
