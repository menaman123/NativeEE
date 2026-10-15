#pragma once
// Vu1RegisterSection: the VU1R savestate section -- the VU1 register file
// (VF0-31, VI0-15, ACC, Q, P, I, R, the flag and control words: the 608-byte
// VUregs block) plus the reference's "worker running" flag -- over the VU1
// VuUnit. OPTIONAL in the reference loader (a file without it keeps the
// unit's live registers), so optional here: absent leaves vu1.regs() as is.
//
// Wire layout (1605 bytes, little-endian), the reference writer's section:
//   +0x000 u8   running   1 = the reference's MTVU worker thread was running
//   +0x001 u32  size      sizeof(CpuContext) = 1600, checked on load
//   +0x005 u8[1600]       the worker's private EE register image; the VU1
//                         register file is the VUregs block at image offset
//                         992 (608 bytes). The other 992 bytes are the EE
//                         GPR/FPU/CP0 fields of that private context -- they
//                         are not VU state and the unit has no counterpart.
// Serialize writes running = 0 (the deterministic profile runs VU1 inline), a
// zeroed image and vu1.regs() at offset 992. Restore copies the VUregs block
// into vu1.regs() whatever `running` says (the worker is play-mode only; the
// flag is counted for run.meta).
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"

class VuUnit;

class Vu1RegisterSection {
public:
    static constexpr size_t kContextBytes = 1600;
    static constexpr size_t kVuRegsOffset = 992;
    static constexpr size_t kVuRegsBytes = 608;
    static constexpr size_t kPayloadBytes = 1 + 4 + kContextBytes;

    using OnRestored = std::function<void()>;

    explicit Vu1RegisterSection(VuUnit& vu1);
    // Runs after a VU1R restore landed in vu1.regs() (the MTVU worker mirrors it).
    void SetOnRestored(OnRestored fn) { on_restored_ = std::move(fn); }

    void Register(ps2r::savestate::SectionRegistry& registry);
    ps2r::savestate::SectionSpec Spec();

    void Serialize(std::vector<uint8_t>& out) const;
    static bool Validate(const std::vector<uint8_t>& payload, std::string* err);
    bool Restore(const std::vector<uint8_t>& payload, std::string* err);

    // Restores seen / restores whose file said the worker was running.
    unsigned RestoredCount() const { return restored_; }
    unsigned RestoredRunningCount() const { return restored_running_; }

private:
    VuUnit& vu1_;
    OnRestored on_restored_;
    unsigned restored_ = 0;
    unsigned restored_running_ = 0;
};
