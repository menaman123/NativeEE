// core/config/deterministic_profile.h -- the `deterministic` preset: the
// verify/PROFILE.md pin set expressed as Config field values. Applying it
// removes every wall-clock / host-scheduler input the harness has identified;
// the resulting fingerprint is what every frozen leg's run.meta compares to.
#pragma once

#include "engine/core/config/config.h"

namespace ps2r {

class DeterministicProfile {
public:
    static constexpr const char* kName = "deterministic";

    // Applies every pin (PROFILE.md rows) and sets meta.profile.
    static void Apply(Config& c);

    // Applies a named profile ("default" = no-op, "deterministic" = Apply).
    // Returns false for an unknown name.
    static bool ApplyNamed(const char* name, Config& c);
};

}  // namespace ps2r
