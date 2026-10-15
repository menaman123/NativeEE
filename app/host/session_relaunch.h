#pragma once

#include <cstdint>
#include <string>

#include "app/host/session_runtime.h"
#include "engine/core/savestate/section_registry.h"

namespace ps2r::app {
class SessionWorkers;

// F8 under a composition that still retains running service state (STREAM.IRX,
// the IOP heap: `StartupOwnership` names them; `harness.live_restore` off):
// a live in-process rewind is refused by the M11 contract, so F8 is composed
// as a RELAUNCH of the same binary from the latest slot. On the completed game
// owner the slot (the browser's selection or the newest slot<N>.ps2st) is
// preflighted against the live registry -- every required owner's section
// present, a refusal names the missing one -- AND against the live composition
// (the slot's .meta composition= must equal SavestateSlots::Composition(cfg);
// a refusal names the differing field, e.g. `composition differs:
// profile(saved=deterministic,live=default)`; a differing build_id= is reported
// as a warning), and the running image is resolved; only then the game owner
// unwinds, the session closes, and main re-executes that image with the same
// arguments plus `--load-state <slot>` (the composition's own restore path, end
// to end). Nothing of the live state is mutated before the preflight passes.
class SessionRelaunch {
public:
    SessionRelaunch(SessionWorkers& workers, const savestate::SectionRegistry& registry,
                    std::string slots_directory, std::string live_composition, std::string live_build_id);
    // Binds restore/unwind_restore/restart (alive stays the caller's).
    void Bind(SessionRuntime::Hooks& hooks);

    // Process-wide handoff to main: the slot the accepted F8 asked for (empty =
    // no relaunch pending). Consumed once.
    static std::string TakePending();
    static bool Pending();

private:
    static std::string& PendingPath();

    SessionWorkers& workers_;
    const savestate::SectionRegistry& registry_;
    std::string slots_directory_;
    std::string live_composition_;
    std::string live_build_id_;
    uint64_t request_ = 0;
    uint64_t frame_ = 0;
    std::string path_;
};

}  // namespace ps2r::app
