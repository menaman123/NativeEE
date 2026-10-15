#pragma once

#include "app/host/session_restart.h"
#include "app/host/session_runtime.h"
#include "app/host/session_clock.h"

class StateRestorer;
class GameBoundary;
namespace ps2r::verify { class VerifyHost; }
namespace ps2r::app {
class SessionFiles;
class SessionWorkers;

// Application F8 transaction over the actual selected registry and recorder.
// Providers keep ownership of their state. This owner coordinates their
// admission, unpublished preparation, joined publication and guest relaunch.
class SessionRestore {
public:
    struct Lifecycle {
        // Observational current-owner checks. Never drain, reset or stop.
        std::function<bool(std::string&)> inspect;
        // Complete preceding graphics work and check resource staging scope.
        // Live owners remain usable on failure.
        std::function<bool(std::string&)> prepare;
        // Called after guest workers/ticker joined. Join the capture writer
        // and stop external output consumers before discarding queued output.
        std::function<bool(std::string&)> joined;
        // Process resources only: input edges, output cursor, device/ticker.
        // Must preserve restored Movie, Graphics, Audio and game-save owners.
        std::function<bool(uint32_t frame, std::string&)> resumed;
    };
    SessionRestore(SessionRuntime&, SessionWorkers&, StateRestorer&,
                   SessionClock&, GameBoundary&, verify::VerifyHost&, SessionFiles&, Lifecycle);
    // Root leaves the capability off until its composed acceptance is approved.
    bool Bind(SessionRuntime::Hooks&, bool approved);

private:
    SessionRestart::Sources Sources();
    SessionRestart::Hooks RestartHooks();
    SessionRestart::Current Current() const;
    bool ReadImage(const savestate::FileMeta&, const std::vector<savestate::Blob>&, std::string&);

    SessionRuntime& session_;
    SessionWorkers& workers_;
    StateRestorer& restorer_;
    SessionClock& clock_;
    GameBoundary& boundary_;
    verify::VerifyHost& output_;
    SessionFiles& files_;
    Lifecycle lifecycle_;
    SessionClock::Snapshot incoming_clock_;
    int incoming_boundary_ = 0;
    std::atomic<bool> resume_gate_{false};
    SessionRestart restart_;
};
} // namespace ps2r::app
