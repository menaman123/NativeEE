#pragma once
// LiveRestoreGate: the F8 LIVE restore's admission (track 3a, Bernoulli;
// docs/design/F8_LIVE_RESTORE.md sections 2.5 and 3.1) as a pure function over
// the composition's probes, so the root's lifecycle lambda is one call and the
// gate is pinned by a unit test with a fake composition per term.
//
// Gate (2) of the former lifecycle -- "A remaining asynchronous owner has no
// approved live-restore contract." -- is split into its four owners, each a
// refusal BY NAME with what it holds. Nothing is dropped to admit a boundary:
// a boundary one of them refuses is simply not admissible and the user presses
// F8 again at the next one.
//
//   mtvu        the MTVU worker: the pipeline and the arbiter must run empty
//               (MtvuStack::Quiesce, the F5 predicate); the restored VU1 state
//               is then mirrored into the worker's unit by the sections.
//   events      kernel events queued for a later boundary (EeEventQueue) --
//               the Savestate slot runs after EventDrain, so normally empty.
//   rpc         native services' calls in flight (RpcRouter::PendingCount) --
//               a call spanning boundaries.
//   deliveries  modelled disc deliveries due at a later tick (DeliveryMeter).
//   owners      the F5 owners' predicates (Input/Save, Movie, the services).
//
// The route (section 3.5): LIVE when the setting (save.restore_mode=live, or
// the root opt-in harness.live_restore) asks for it AND the composition owns
// its startup (StartupOwnership: no retained IOP module, the IOP never booted);
// RELAUNCH otherwise -- by setting, or as the named FALLBACK when the
// composition still retains IOP state (its running kernel threads, the
// streamer's ring, SDR's voice state have no section and no live rewind).
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "engine/core/config/config.h"

namespace ps2r::app {

class LiveRestoreGate {
public:
    struct Probes {
        // Null = the composition runs no MTVU worker (the deterministic profile).
        std::function<bool(std::string*)> mtvu_quiesce;
        std::function<bool()> events_pending;
        std::function<size_t()> rpc_pending;
        std::function<size_t()> deliveries_pending;
        // The F5 owners' predicates; the error they name is the refusal.
        std::function<bool(std::string&)> owners;
    };
    struct Census {
        uint64_t mtvu = 0, events = 0, rpc = 0, deliveries = 0, owners = 0;
        // run.meta `live_restore_refusals=mtvu=N,events=N,rpc=N,deliveries=N,owners=N`
        std::string Render() const;
    };
    // The observational check (never drains, resets or stops anything but the
    // MTVU quiesce, which is the F5 predicate's own wait). False names the
    // refusing owner in `error` and counts it in `census`.
    static bool Inspect(const Probes& probes, Census& census, std::string& error);

    enum class Route { Live, RelaunchBySetting, RelaunchFallback };
    static Route Choose(RestoreMode mode, bool harness_live_restore, bool startup_owned, bool irx_entry_ran);
    static bool Live(Route route) { return route == Route::Live; }
    // run.meta `f8=live` | `f8=relaunch,setting` | `f8=relaunch,fallback=startup_retained`
    static const char* RouteCensus(Route route);
};

}  // namespace ps2r::app
