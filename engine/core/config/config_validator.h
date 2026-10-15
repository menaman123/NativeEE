// core/config/config_validator.h -- the explicit dependency DAG between fields
// (what the frozen original expressed as a hidden env cascade):
//   time.tick_sync            => engine.mtvu must be off (refused otherwise)
//   engine.pacing = freerun   => cdvd.latency_model defaults to instant unless
//                                the user set it explicitly
//   engine.mtvu_depth >= 1    and only meaningful with engine.mtvu
//   video.scale in 1..4; video.custom_scale_pct in 25..400;
//   video.custom_offset_x/y in -8192..8192; audio.volume in 0..100;
//   engine.shutdown_wait_s in 0..600; log.ring_events pow2 in 1024..4194304
//   harness.* (out_dir/frames/...) requires harness.headless or an explicit
//                                window request
//   harness.inject_fault_frame   > 0 requires harness.out_dir (a harness leg)
//   hle.* dependency rules       (movie_step => movie_lifecycle + graphics_movie,
//                                audio_mix => music + movie_lifecycle + iop.dispatch=frame,
//                                Graphics producers => video.backend=gl, ...):
//                                Validate REFUSES a composition that breaks one;
//                                DegradeDependents first turns the dependent key
//                                off with one note per rule, so a user's
//                                `--set hle.<key>=off` degrades the chain instead
//                                of refusing to start (ALL-ON, NO EXCEPTIONS).
#pragma once

#include <string>
#include <vector>

#include "engine/core/config/config.h"

namespace ps2r {

class ConfigValidator {
public:
    // Applies the defaulting rules that depend on other fields.
    // `latency_model_explicit` = the user set cdvd.latency_model themselves.
    static void ApplyDependentDefaults(Config& c, bool latency_model_explicit);

    // Turns every hle.* key whose prerequisite is off OFF itself and returns
    // one note per degraded key ("hle.movie_step disabled: requires
    // hle.graphics_movie"); the loader logs each as `[config] <note>`. After it
    // the hle.* rules of Validate hold by construction.
    // `retained_startup_available` = the IOP-booting startup arm is compiled in
    // (ConfigLoader::SetRetainedStartupAvailable). When it is NOT, an unmet
    // consumer of hle.startup=native cannot fall back to `retained`: nothing is
    // changed and *startup_refusal names the key ("hle.startup=native requires
    // hle.music=on: ...") -- the loader refuses the run with it (M12).
    static std::vector<std::string> DegradeDependents(Config& c, bool retained_startup_available = true,
                                                      std::string* startup_refusal = nullptr);

    // Checks every constraint; returns false with *err naming the first
    // violated rule.
    static bool Validate(const Config& c, bool harness_window_requested, std::string* err);
};

}  // namespace ps2r
