// engine/iop/iop_module_table.h -- IopModuleTable: the runtime facade over the
// cooperative IOP kernel -- the kernel, the SIF RPC bridge and the (REALTIME)
// host thread; registration of the loaded IOP modules (the run.meta
// `iop_modules=` census); boot sequencing (module init runs to its first block
// before the EE continues); the dispatch-policy selection (Config iop.dispatch);
// and the FRAME-policy pump points the EE thread calls:
//
//   PumpAtBoundary     -> BoundarySequencer::Step::IopDispatch (slot 5):
//                         advance the frame clock by one quantum, then dispatch
//                         with forced advances bounded by the frame ceiling.
//   PumpAfterEeService -> Step::EeStreamRpc (slot 6): after the EE services a
//                         worker marshal (reply copy-back + NotifyEeReply), the
//                         woken parkee runs within the same boundary.
//   PumpFromParkLoop   -> EE park loops that reach no boundary: one dispatch,
//                         then a forced advance capped at ONE frame quantum.
//   PumpWithinFrame    -> the EE's poll inside a frame (the title's load-wait
//                         loop asking whether the IOP progressed): runs the
//                         work due at or before the frame ceiling WITHOUT
//                         advancing the frame base -- the IOP's threads run on
//                         their own clock while the EE spins, and the EE's next
//                         poll observes what they did. Answers whether anything
//                         ran; nothing due inside the frame = false (the caller
//                         lets the frame end).
//   PumpToInstant      -> the EE's poll right after a round trip the IOP's
//                         clock was charged for: runs only the work due AT OR
//                         BEFORE the IOP's current instant (what the IOP did
//                         while the EE waited for the reply), never a due later
//                         in the frame, never a base advance. Answers whether
//                         anything ran.
//
// The ceiling of a boundary pump is INCLUSIVE: the drain stops only when the
// next due is PAST the ceiling, so a due exactly on the next boundary's tick is
// serviced one pump early (P4-CEILING-INCLUSIVE). Every pump is a no-op until
// OnModuleLoaded and under REALTIME (the host thread dispatches there).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/config/config.h"
#include "engine/iop/iop_host_thread.h"
#include "engine/iop/iop_kernel.h"
#include "engine/iop/sif_rpc_bridge.h"

namespace ps2r::iop {

class IopModuleTable {
public:
    struct ModuleEntry {
        std::string name;      // RegisterLibraryEntries name (e.g. "sio2man") or the IRX file stem
        uint32_t    version = 0;
        int         rpc_tid = 0;   // the module's RPC-dispatch thread, 0 = none
        // The image as loaded (modload's ReferModuleStatus answers from these; 0 = no image hosted).
        uint32_t    entry = 0, gp = 0, text_addr = 0, text_size = 0, data_size = 0, bss_size = 0;
    };

    explicit IopModuleTable(IopClock::Policy policy);
    explicit IopModuleTable(const Config& cfg);   // policy = cfg.iop.dispatch
    ~IopModuleTable();
    IopModuleTable(const IopModuleTable&) = delete;
    IopModuleTable& operator=(const IopModuleTable&) = delete;

    IopKernel&     Kernel() { return kernel_; }
    SifRpcBridge&  Rpc() { return rpc_; }
    IopHostThread& Host() { return host_; }
    bool FramePolicy() const { return frame_; }
    IopClock::Policy Policy() const { return frame_ ? IopClock::Policy::Frame : IopClock::Policy::Realtime; }

    // Module registration (the loader records each module it brought up).
    // Returns the row index; module ids handed to guests are index + 1.
    int  RegisterModule(const std::string& name, uint32_t version, int rpc_tid = 0);
    int  RegisterModule(const ModuleEntry& entry);
    const std::vector<ModuleEntry>& Modules() const { return modules_; }
    const ModuleEntry* FindModule(const std::string& name) const;
    size_t ModuleCount() const { return modules_.size(); }
    // Comma-separated "name:version" list in registration order (run.meta iop_modules=).
    std::string ModulesCensus() const;

    // Called after a module entry has created and started its threads: run
    // every started thread to its first block on the caller's thread; the first
    // call also records the RPC tid and starts the host thread under REALTIME.
    // Callable once per hosted module (each boots to its first block in turn).
    void OnModuleLoaded(int rpc_tid);
    bool Booted() const { return booted_; }

    // FRAME-policy pump points (no-ops otherwise / before boot).
    void PumpAtBoundary();
    void PumpAfterEeService();
    void PumpFromParkLoop();
    bool PumpWithinFrame();
    bool PumpToInstant();

    // EE-side nudge (REALTIME: wake the host thread).
    void NotifyHost();

    void Shutdown();

    // The kernel-state watch line (diag category iopsched, `watch`): thread
    // states/wait kinds of the first four tids + the unparked census.
    // Grammar: the IOPK-WATCH prefix, then " t<i>=<state>/<wait>" x4, then " unparked=<n>". Returns chars written.
    int FormatWatchLine(char* buf, size_t n) const;

private:
    // Returns whether any dispatch pass ran something (a thread, an event, a beat).
    bool DrainQuantum(bool advance_frame);
    // The drain to an inclusive ceiling (no base advance): dispatch, step the
    // clock to each due at or before `ceiling_us`, stop when the next due is past it.
    bool DrainToUs(uint64_t ceiling_us);

    IopKernel kernel_;
    SifRpcBridge rpc_;
    IopHostThread host_;
    std::vector<ModuleEntry> modules_;
    bool frame_ = true;
    bool booted_ = false;
};

}  // namespace ps2r::iop
