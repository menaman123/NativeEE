// engine/iop/hle/thsemap/thsemap_module.h -- ThsemapModule: the IOP kernel's
// thsemap library routed to IopKernel: CreateSema (idx 4), DeleteSema (5),
// SignalSema (6), iSignalSema (7), WaitSema (8), PollSema (9). Sema ids are handed out
// round-robin over the kernel's sid space; the counts live in the kernel.
#pragma once

#include <atomic>
#include <cstdint>

#include "engine/iop/iop_context.h"
#include "engine/iop/iop_memory.h"

namespace ps2r::iop {
class IopModuleTable;
}

namespace ps2r::iop::hle {

class ThsemapModule {
public:
    static constexpr int kMaxSemas = 64;

    ThsemapModule(IopModuleTable& table, IopMemory& mem) : table_(table), mem_(mem) {}

    // int CreateSema(SemaParam*) -> sid or IOP error; +0x00 attributes,
    // +0x08 initial count, +0x0C max count.
    void CreateSema(IopContext& ctx);
    // int DeleteSema(sid) -> 0.
    void DeleteSema(IopContext& ctx);
    // int SignalSema(sid): operation result in kernel context; a host call
    // returns queue acceptance (delivery errors: Kernel().InjectedSignalErrors()).
    void SignalSema(IopContext& ctx);
    // int iSignalSema(sid) -> 0; the interrupt-context form: same rule here.
    void iSignalSema(IopContext& ctx);
    // int WaitSema(sid): cooperative park; deletion returns KE_WAIT_DELETE.
    // Host polling can consume an available token, otherwise KE_CAN_NOT_WAIT.
    void WaitSema(IopContext& ctx);
    // int PollSema(sid) -> 0 when a token was taken, KE_SEMA_ZERO when none (never parks).
    void PollSema(IopContext& ctx);

    int NextSid() const { return next_sid_; }
    uint64_t HostWaitCount() const { return host_waits_.load(std::memory_order_relaxed); }
    uint64_t ErrorCount() const { return errors_.load(std::memory_order_relaxed); }

private:
    void Return(IopContext& ctx, int result);
    IopModuleTable& table_;
    IopMemory& mem_;
    int next_sid_ = 0;
    std::atomic<uint64_t> host_waits_{0}, errors_{0};
};

}  // namespace ps2r::iop::hle
