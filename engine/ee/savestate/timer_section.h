#pragma once
// TimerSection: the TIMR savestate section over EeTimers (TimerCodec wire
// layout, 4 x 34 bytes). OPTIONAL: a file without TIMR (pre-v3) seeds the
// timers with the title's boot configuration (EeTimers::SeedBootDefault with
// the profile's boot T0_MODE, every baseline at the current tick) -- the
// game-visible accumulator is guest RAM and rides RAM0. Restored after the
// domain re-anchor so the saved baseline ticks are rebased coherently
// (EeTimers::Restore).
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"

class EeTimers;

class TimerSection {
public:
    // boot_t0_mode: the T0_MODE value the title's boot init programs (title
    // profile data; used only when the file carries no TIMR section).
    TimerSection(EeTimers& timers, uint16_t boot_t0_mode);

    void Register(ps2r::savestate::SectionRegistry& registry);
    ps2r::savestate::SectionSpec Spec();

    void Serialize(std::vector<uint8_t>& out) const;
    static bool Validate(const std::vector<uint8_t>& payload, std::string* err);
    bool Restore(const std::vector<uint8_t>& payload, std::string* err);
    // The absent-TIMR path (OLD seeded the known boot timer config).
    void SeedBootDefault();

private:
    EeTimers& timers_;
    uint16_t boot_t0_mode_;
};
