#pragma once
// SessionCapture -- the F5 save of the play session: the composition's section registry
// captured on the completed game owner (SessionCheckpoint over GameBoundary::FreshThreads +
// DispatchTable::CanResumeAt) and written off-thread to the product's slot
// (SavestateSlots: <save root>/savestates/slot<save.slot>.ps2st + the .meta sidecar). The
// admission is a Quiescence of named owners (Conditions(): the composition registers the
// asynchronous owners that hold state no section carries -- the MTVU worker's quiesce is
// registered here when a stack exists; the GS queue, the IOP, the disc deliveries are the
// composition's rows); a refusal names its owner, and the runtime retries at the following
// boundaries (120) before refusing loudly. Title-neutral: no hardware model, no title hook.
#include <cstdint>
#include <optional>
#include <string>

#include "app/host/session_checkpoint.h"
#include "app/host/session_runtime.h"
#include "engine/core/savestate/quiescence.h"
#include "engine/core/savestate/section_registry.h"

class GameBoundary;
class MtvuStack;
namespace ps2r { class Config; }

namespace ps2r::app {

class SessionCapture {
public:
    // `build_id` is stamped into the .meta sidecar (SavestateSlots::Meta::build_id).
    SessionCapture(const savestate::SectionRegistry& sections, GameBoundary& boundary, const Config& config,
                   MtvuStack* mtvu, std::string build_id);
    // The admission owners (register before the first boundary).
    savestate::Quiescence& Conditions();
    // Game owner, inside the completed boundary: Working = admitted (the writer runs), Waiting =
    // an owner refused this boundary (the runtime retries), Refused / Failed otherwise.
    SessionCheckpoint::Result Capture(uint64_t request, uint64_t frame, std::string path);
    // Any host owner: the writer's terminal result once, with the sidecar written on success.
    std::optional<SessionCheckpoint::Result> Poll();
    // hooks.capture / hooks.poll_capture.
    void Bind(SessionRuntime::Hooks& hooks);
    // Supervisor, after admission stopped: joins the writer.
    void Join();
    // Census: captures admitted / written / refused (run.meta savestate_capture=).
    std::string Census() const;
    static std::string ReproductionsDirectory(const Config& config);

private:
    const Config& config_;
    std::string build_id_;
    savestate::Quiescence conditions_;
    SessionCheckpoint checkpoint_;
    uint64_t admitted_ = 0, written_ = 0, refused_ = 0, failed_ = 0;
};

}  // namespace ps2r::app
