#pragma once
// YieldSection: the YLDP savestate section over EeYieldPoint -- the back-edge
// budget the generated code decrements (g_ee_backedge_budget) as it stood at
// the captured boundary, plus the reload value it counts down from.
//
// Why it is state: the budget is one global counter that only a trip reloads,
// so the back-edges REMAINING at the boundary decide when the next trip's field
// passes, i.e. at which point of the main loop the next vblank lands (the
// WaitVblank sleeper -- the title's vsync thread -- is readied mid-frame by that
// field). Restoring every other section but leaving the process's own residual
// in the counter lands the next trip elsewhere than the saved run's: the
// restored run is deterministic by itself yet differs from the uninterrupted
// one by a one-frame main-loop phase slip a few frames after the load (the
// savestate proof of the recomp-agent program, control.md 11:50-13:22).
//
// Capture: the boundary owner is the only token holder and no guest code runs
// between the boundary and the capture, so BudgetRemaining() is exact there.
// Restore: the value is STAGED (EeYieldPoint::StageRestoredBudget) and seated
// by ThreadRelauncher right before the boundary thread resumes -- after the
// waiters' re-executed wait bodies have parked, whose back-edges the saved run
// never executed. OPTIONAL in the file (a pre-v5 image restores with the
// process's residual, counted ee_yield=...:budget_absent=); a file that carries
// it is stamped kFileVersionYldp so an older reader refuses it by version
// rather than restoring an inexact splice. The reload value must equal the live
// EeYieldPoint::Budget() (refused by name otherwise: a different budget is a
// different field schedule).
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"

class EeYieldPoint;

class YieldSection {
public:
    static constexpr uint32_t kVersion = 1;
    static constexpr size_t kPayloadBytes = 8;   // u32 budget (reload value), u32 remaining

    explicit YieldSection(EeYieldPoint& point);

    void Register(ps2r::savestate::SectionRegistry& registry);
    ps2r::savestate::SectionSpec Spec();

    void Serialize(std::vector<uint8_t>& out) const;
    // Pure shape check (length, reload value non-zero, 1 <= remaining <= reload value).
    static bool Validate(const std::vector<uint8_t>& payload, std::string* err);
    // Validate + the live reload value must equal the saved one; then stages the remaining count.
    bool Restore(const std::vector<uint8_t>& payload, std::string* err);
    static bool Decode(const std::vector<uint8_t>& payload, uint32_t* budget, uint32_t* remaining);

private:
    EeYieldPoint& point_;
};
