#pragma once
// The VU interpreter tier: the instrumented reference execution of a
// microprogram, one 64-bit bundle per VU cycle, over a VuUnit.
//
// PS2 rules carried here: a bundle's upper (FMAC) and lower (LSU/IALU/FDIV/
// EFU/branch) words issue together and read their operands at the same stage,
// so the lower sees the PRE-upper value of a VF the upper writes and, when
// both write the same VF, the upper's result wins; the I bit turns the lower
// word into a 32-bit immediate loaded into I after the upper has read the old
// I; VF00 = {0,0,0,1} and VI00 = 0 are re-pinned after every bundle; every
// branch and the E bit take effect after ONE delay-slot bundle, the program
// ending with TPC = end address + 16; program end drains every result pipe
// (flags, Q, P, the in-flight writebacks) and completes a pending XGKICK; the
// FMAC rounds toward zero for the whole run.
//
// Kept divergences from hardware (bit-identity with the recorded baselines):
// a run stops after four all-zero bundles; the XTOP yield rule ends a VU1 run
// that has read XTOP more than 1024 times without kicking; a VU0 program
// spinning on a self-branch gated by a VI the EE owns is parked (`suspended`)
// until `ResumeIfWaiting`; VU0's pc is masked with 0x3FFF and a run silently
// ends past its 4 KB micro memory; a branch inside a delay slot is ignored;
// a discarded lower op performs none of its side effects.
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/vu/vu_float.h"

class VuUnit;

class VuInterpreterTier {
public:
    struct RunOptions {
        uint64_t cycle_limit = 1000000;  // bundles (stalled ones included) before the run is cut
    };

    static constexpr uint32_t kPcMask = 0x3FFF;
    static constexpr uint32_t kLinkMask = 0x7FF;  // BAL/JALR link: the 11-bit instruction index
    static constexpr int kXtopYieldReads = 1024;

    explicit VuInterpreterTier(uint64_t cycle_cap = 1000000);

    // Run the unit's microprogram from `start_pc` (byte address) to its end.
    void Run(VuUnit& unit, uint32_t start_pc);
    void Run(VuUnit& unit, uint32_t start_pc, const RunOptions& opt);
    // A parked VU0 (self-spin suspend) resumes at its recorded bundle.
    void ResumeIfWaiting(VuUnit& unit);

    uint64_t cycle_cap() const { return cycle_cap_; }
    void set_cycle_cap(uint64_t cap) { cycle_cap_ = cap; }
    // Bundles retired across every run of this tier (the catalog's exec count).
    uint64_t bundles() const { return bundles_; }

    // The bodies the other dispatchers share (ONE semantics): the cached tier
    // for the kinds it has no fast form of, the block tier through the cached
    // tier's resolved bodies, and the GENERATED blocks (engine/vu/vu1_aot/,
    // free functions in a generated TU -- hence public) for CLIP, DIV, the
    // GENERIC words and the program-end drain.
    // The upper word (RunUpper dispatches func >= 0x3C to the special-2 table).
    void RunUpper(VuUnit& u, uint32_t upper);
    void RunClip(VuUnit& u, const VR_reg& fs_raw, const VR_reg& ft_raw);
    // The lower word; `pc` is the bundle's byte address.
    void RunLower(VuUnit& u, uint32_t lower, uint32_t pc, bool& branch_taken, uint32_t& branch_target);
    // FDIV-pipe bodies shared by the lower word and the legacy upper encodings.
    void DoDiv(VuUnit& u, uint32_t fs, uint32_t fsf, uint32_t ft, uint32_t ftf);
    // DIV over operand VALUES (the register-resident generated blocks pass
    // their locals): DoDiv's body with the two source registers named.
    void DivLanes(VuUnit& u, const VR_reg& fs_raw, uint32_t fsf, const VR_reg& ft_raw, uint32_t ftf);
    // Program end / yield / park: every pipe drained, a pending kick completed.
    void DrainAll(VuUnit& u);

private:
    friend class VuCachedTier;
    friend class VuBlockTier;

    void RunSpecial2Upper(VuUnit& u, uint32_t upper);
    void RunLowerSpecial(VuUnit& u, uint32_t lower);
    void RunEfu(VuUnit& u, uint32_t lower, uint32_t funct2, uint32_t t3);
    void DoSqrt(VuUnit& u, uint32_t ft, uint32_t ftf);
    void DoRsqrt(VuUnit& u, uint32_t fs, uint32_t fsf, uint32_t ft, uint32_t ftf);

    static VR_reg Clamp4(const VR_reg& v);
    static void PinHardwired(VUregs& regs);

    uint64_t cycle_cap_;
    uint64_t bundles_ = 0;
};

// The four-lane clamp every GENERIC / special upper body reads its sources
// through (two to three per word); inline so the generated blocks' calls into
// those bodies do not pay a call per lane set.
inline VR_reg VuInterpreterTier::Clamp4(const VR_reg& v) {
    VR_reg r;
    for (int k = 0; k < 4; ++k) r.I[k] = VuFloat::ClampBits1(v.I[k]);
    return r;
}
