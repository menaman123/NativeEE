#pragma once
// The VU execution ladder, written once: a program start (MSCAL / MSCNT /
// MSCALF / VCALLMS / CMSAR0 / a resume) is offered to the VU1 HLE library, then
// to the MTVU redirect, then run on the configured tier -- cached (the
// default), the block tier (VU1 only: VU0 stays on the cached tier under it)
// or the interpreter reference. Every tier consumes the ONE decoder, stall
// model, flag pipeline and XGKICK; their identity is proven by the
// differential tests, never assumed.
//
// Cross-thread contract: the two hook slots are plain callbacks installed by
// the composition root (the HLE runtime, the MTVU worker); null means absent.
// A hook returning true has RUN the program (kicks submitted, memory and
// registers updated) and the tiers are not touched; false falls through with
// nothing changed. VU0 never enters the hooks.
#include <cstdint>
#include <memory>

#include "engine/vu/tiers/vu_block_tier.h"
#include "engine/vu/tiers/vu_cached_tier.h"
#include "engine/vu/tiers/vu_interpreter_tier.h"

class VuUnit;

// The fallback tier a program runs on when no hook takes it.
enum class VuTierMode : uint8_t { Cached, Interp, Block };

// Module-local settings (the Config keys `engine.vu1_mode` / the VU1 HLE gate
// are copied here at integration).
struct VuSettings {
    VuTierMode vu1_mode = VuTierMode::Cached;
    bool hle_enabled = false;  // the HLE library consult; the corpus tools run tiers only
};

class VuTierDispatch {
public:
    // The HLE library: true = the native program ran for this image/entry.
    using HleHook = bool (*)(void* user, VuUnit& unit, uint32_t start_pc, uint64_t micro_generation);
    // The MTVU EE-kick redirect: true = executed on the worker's unit.
    using MtvuHook = bool (*)(void* user, VuUnit& unit, uint32_t start_pc);

    static constexpr uint64_t kDefaultCycleCap = 1000000;  // bundles before a runaway program is cut

    explicit VuTierDispatch(VuSettings settings = VuSettings{}, uint64_t cycle_cap = kDefaultCycleCap);

    // The ladder: hle (VU1, enabled, hook set) -> mtvu (VU1, hook set) -> tier
    // (Block: VU1 on the block tier, VU0 on the cached tier).
    void Execute(VuUnit& unit, uint32_t start_pc);
    // A parked VU0 (self-spin suspend) resumes through the ladder.
    void ResumeIfWaiting(VuUnit& unit);

    void SetHleHook(HleHook fn, void* user);
    void SetMtvuHook(MtvuHook fn, void* user);

    // One cap serves every tier (the tool's --cycle-cap).
    void set_cycle_cap(uint64_t cap);
    uint64_t cycle_cap() const { return interp_.cycle_cap(); }

    VuSettings& settings() { return settings_; }
    const VuSettings& settings() const { return settings_; }
    VuInterpreterTier& interp() { return interp_; }
    VuCachedTier& cached() { return cached_; }
    VuBlockTier& block() { return block_; }
    // Bundles retired on any tier (the catalog's exec count).
    uint64_t bundles() const { return interp_.bundles() + cached_.bundles() + block_.bundles(); }

private:
    VuSettings settings_;
    VuInterpreterTier interp_;
    VuCachedTier cached_;
    VuBlockTier block_;
    HleHook hle_ = nullptr;
    void* hle_user_ = nullptr;
    MtvuHook mtvu_ = nullptr;
    void* mtvu_user_ = nullptr;
};
