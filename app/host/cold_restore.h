#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/ee/savestate/thread_relauncher.h"
#include "engine/ee/savestate/thread_section.h"

class StateRestorer;
class ThreadManager;

namespace ps2r::app {

// The --load-state route of a composition: a COLD restore. The process boots
// its composition (every device, service and section owner armed, no guest
// instruction run), then -- on the supervisor, before the first field --
// restores EVERY section of the slot in place of running from the ELF entry,
// and relaunches the THRD records' continuations (ThreadRelauncher: the
// parked kernel waits re-entered, the boundary owner at its post-HLE
// continuation). The first title's RunLeg did this inline (private
// ee_bootstrap_leg.cpp); this owner is the title-neutral shape of it:
//   Preflight  -- pure: the file's envelope against the live registry (every
//                 required owner present, SavestateSlots::Preflight), the
//                 sidecar's composition against the live one (a differing
//                 build_id= is a warning; a missing sidecar is a warning), the
//                 THRD section's boundary owner (exactly one).
//   Apply      -- StateRestorer::RestoreFile: parse + validate, the pre-restore
//                 step, every owner's restore in registration order. Nothing of
//                 the live composition is touched when a step refuses.
//   Relaunch   -- every started record with a context gets its worker; the
//                 caller's EnterFn enters generated code (DispatchTable::InvokeAt).
// Report() carries what run.meta's restore rows need (restore_route=cold).
// This owner (its relauncher) and the ThreadSection's records must outlive
// every worker Relaunch started: the workers walk their chains through them.
class ColdRestore {
public:
    struct Report {
        std::string path;
        uint32_t vsync_frame = 0;
        int boundary_tid = 0;
        std::string sections;   // the restored tags, file order, comma-separated
        uint64_t mhash = 0;
        std::string warning;
        int relaunched = 0;
    };

    ColdRestore(StateRestorer& restorer, ThreadSection& threads, ThreadManager& manager);
    ColdRestore(const ColdRestore&) = delete;
    ColdRestore& operator=(const ColdRestore&) = delete;

    bool Preflight(const std::string& path, const std::string& live_composition, const std::string& live_build_id,
                   std::string* err);
    bool Apply(std::string* err);
    int Relaunch(const ThreadRelauncher::EnterFn& enter, std::string* err);

    const Report& Result() const { return report_; }
    const std::vector<ThreadSection::Record>& Records() const { return threads_.Restored(); }

private:
    StateRestorer& restorer_;
    ThreadSection& threads_;
    ThreadManager& manager_;
    std::unique_ptr<ThreadRelauncher> relauncher_;
    Report report_;
    bool preflighted_ = false, applied_ = false;
};

}  // namespace ps2r::app
