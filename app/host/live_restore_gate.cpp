#include "app/host/live_restore_gate.h"

namespace ps2r::app {

std::string LiveRestoreGate::Census::Render() const {
    return "mtvu=" + std::to_string(mtvu) + ",events=" + std::to_string(events) + ",rpc=" + std::to_string(rpc) +
           ",deliveries=" + std::to_string(deliveries) + ",owners=" + std::to_string(owners);
}

bool LiveRestoreGate::Inspect(const Probes& probes, Census& census, std::string& error) {
    std::string why;
    if (probes.mtvu_quiesce && !probes.mtvu_quiesce(&why)) {
        ++census.mtvu;
        error = "live restore refused: mtvu worker holds state: " + why;
        return false;
    }
    if (probes.events_pending && probes.events_pending()) {
        ++census.events;
        error = "live restore refused: ee events pending";
        return false;
    }
    if (probes.rpc_pending) {
        if (const size_t pending = probes.rpc_pending()) {
            ++census.rpc;
            error = "live restore refused: rpc pending=" + std::to_string(pending);
            return false;
        }
    }
    if (probes.deliveries_pending) {
        if (const size_t pending = probes.deliveries_pending()) {
            ++census.deliveries;
            error = "live restore refused: disc deliveries pending=" + std::to_string(pending);
            return false;
        }
    }
    if (probes.owners && !probes.owners(error)) {
        ++census.owners;
        if (error.empty()) error = "live restore refused: an owner's predicate";
        return false;
    }
    return true;
}

LiveRestoreGate::Route LiveRestoreGate::Choose(RestoreMode mode, bool harness_live_restore, bool startup_owned,
                                               bool irx_entry_ran) {
    const bool requested = mode == RestoreMode::Live || harness_live_restore;
    if (!requested) return Route::RelaunchBySetting;
    if (!startup_owned || irx_entry_ran) return Route::RelaunchFallback;
    return Route::Live;
}

const char* LiveRestoreGate::RouteCensus(Route route) {
    switch (route) {
        case Route::Live: return "live";
        case Route::RelaunchBySetting: return "relaunch,setting";
        case Route::RelaunchFallback: return "relaunch,fallback=startup_retained";
    }
    return "?";
}

}  // namespace ps2r::app
