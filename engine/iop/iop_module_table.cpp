// engine/iop/iop_module_table.cpp -- IopModuleTable implementation.
#include "engine/iop/iop_module_table.h"

#include <cstdio>

namespace ps2r::iop {

IopModuleTable::IopModuleTable(IopClock::Policy policy)
    : rpc_(kernel_), host_(kernel_), frame_(policy == IopClock::Policy::Frame) {
    kernel_.Clock().SetPolicy(policy);
    rpc_.SetFrameDrive(frame_);
    if (!frame_) {
        // REALTIME: injections and EE RPC submissions poke the host thread awake.
        kernel_.SetWakeHook([this] { host_.Notify(); });
        rpc_.SetHostWake([this] { host_.Notify(); });
    }
}

IopModuleTable::IopModuleTable(const Config& cfg)
    : IopModuleTable(cfg.iop.dispatch == IopDispatch::Frame ? IopClock::Policy::Frame
                                                             : IopClock::Policy::Realtime) {}

IopModuleTable::~IopModuleTable() { Shutdown(); }

int IopModuleTable::RegisterModule(const std::string& name, uint32_t version, int rpc_tid) {
    ModuleEntry e;
    e.name = name;
    e.version = version;
    e.rpc_tid = rpc_tid;
    return RegisterModule(e);
}

int IopModuleTable::RegisterModule(const ModuleEntry& entry) {
    modules_.push_back(entry);
    return static_cast<int>(modules_.size()) - 1;
}

const IopModuleTable::ModuleEntry* IopModuleTable::FindModule(const std::string& name) const {
    for (const ModuleEntry& m : modules_)
        if (m.name == name) return &m;
    return nullptr;
}

std::string IopModuleTable::ModulesCensus() const {
    std::string out;
    for (const ModuleEntry& m : modules_) {
        if (!out.empty()) out += ',';
        out += m.name;
        out += ':';
        out += std::to_string(m.version);
    }
    return out;
}

void IopModuleTable::OnModuleLoaded(int rpc_tid) {
    const bool first = !booted_;
    booted_ = true;
    if (first) rpc_.SetRpcTid(rpc_tid);
    // Boot contract: IOP module init runs to its first block before the EE
    // continues -- run every thread the entry started until none is Ready.
    // (A later module's threads are run the same way; the host thread, under
    // REALTIME, is already dispatching them.)
    if (first || frame_) kernel_.Dispatch();
    if (first && !frame_) host_.Start();
    else if (!frame_) host_.Notify();
}

bool IopModuleTable::DrainQuantum(bool advance_frame) {
    IopClock& clock = kernel_.Clock();
    if (advance_frame) clock.OnFrameBoundary();
    return DrainToUs(clock.FrameCeilingUs());
}

bool IopModuleTable::DrainToUs(uint64_t ceiling) {
    IopClock& clock = kernel_.Clock();
    bool ran = false;
    for (int i = 0; i < 4096; i++) {
        const IopKernel::DispatchResult r = kernel_.Dispatch();
        ran = ran || r.ran_something;
        // Inclusive ceiling: stop only when the next due is PAST the ceiling.
        if (!r.next_due_us || *r.next_due_us > ceiling) break;
        clock.AdvanceToUs(*r.next_due_us);
        if (!r.ran_something && i > 0) {
            // The deadline advanced but nothing ran twice: avoid a hot loop on
            // a due that a fired handler keeps re-arming at the ceiling.
            if (*r.next_due_us >= ceiling) break;
        }
    }
    return ran;
}

void IopModuleTable::PumpAtBoundary() {
    if (!frame_ || !booted_) return;
    DrainQuantum(/*advance_frame=*/true);
}

void IopModuleTable::PumpAfterEeService() {
    if (!frame_ || !booted_) return;
    DrainQuantum(/*advance_frame=*/false);
}

bool IopModuleTable::PumpWithinFrame() {
    if (!frame_ || !booted_) return false;
    // The same ceiling-bounded drain as PumpAfterEeService: the frame base is
    // untouched (the boundary advances it), so a due past the ceiling waits for
    // the next boundary's pump -- the poll sees exactly the IOP work the frame
    // contains.
    return DrainQuantum(/*advance_frame=*/false);
}

bool IopModuleTable::PumpToInstant() {
    if (!frame_ || !booted_) return false;
    // The ceiling is the IOP's own instant: what became due while the EE waited
    // for a charged round trip fires now; a due later in the frame waits for the
    // instant to reach it (another round trip) or for the frame's pumps.
    return DrainToUs(kernel_.Clock().NowUs());
}

void IopModuleTable::PumpFromParkLoop() {
    if (!frame_ || !booted_) return;
    // Dispatch, then advance the virtual clock to the next deadline (capped at
    // one frame quantum) when everything is time-parked: the caller gates this
    // to genuine EE park spins, where guest delays MUST expire for progress.
    IopClock& clock = kernel_.Clock();
    const IopKernel::DispatchResult r = kernel_.Dispatch();
    if (r.next_due_us && !r.ran_something) {
        const uint64_t cap = clock.NowUs() + IopClock::kFrameUs;
        clock.AdvanceToUs(*r.next_due_us < cap ? *r.next_due_us : cap);
    }
}

void IopModuleTable::NotifyHost() {
    if (!frame_) host_.Notify();
}

void IopModuleTable::Shutdown() {
    kernel_.Shutdown();
    host_.Stop();
    // The host thread is joined: detach the wake hooks so the kernel's own
    // destructor (members die in reverse order: host_ before kernel_) cannot
    // poke a destroyed IopHostThread.
    kernel_.SetWakeHook(nullptr);
    rpc_.SetHostWake(nullptr);
}

int IopModuleTable::FormatWatchLine(char* buf, size_t n) const {
    int off = std::snprintf(buf, n, "[IOPK-WATCH]");
    for (int i = 1; i <= 4 && off >= 0 && static_cast<size_t>(off) + 32 < n; i++) {
        off += std::snprintf(buf + off, n - static_cast<size_t>(off), " t%d=%d/%d", i,
                             static_cast<int>(kernel_.ThreadState(i)),
                             static_cast<int>(kernel_.ThreadWaitKind(i)));
    }
    if (off >= 0 && static_cast<size_t>(off) < n)
        off += std::snprintf(buf + off, n - static_cast<size_t>(off), " unparked=%d", kernel_.UnparkedCount());
    return off;
}

}  // namespace ps2r::iop
