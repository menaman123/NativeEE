// engine/iop/hle/intrman/intrman_module.h -- IntrmanModule: the IOP kernel's
// intrman library face used by the recompiled module: CpuSuspendIntr (idx 17)
// and CpuResumeIntr (idx 18). Saves/restores the interrupt-enable word, including
// nested suspend's KE_CPUDI. Guest code between cooperative scheduling points
// is indivisible; this face does not emulate physical interrupt lines.
#pragma once

#include <atomic>

#include "engine/iop/iop_context.h"
#include "engine/iop/iop_memory.h"

namespace ps2r::iop::hle {

class IntrmanModule {
public:
    explicit IntrmanModule(IopMemory& mem) : mem_(mem) {}

    void CpuSuspendIntr(IopContext& ctx);   // int CpuSuspendIntr(int* state)
    void CpuResumeIntr(IopContext& ctx);    // int CpuResumeIntr(int state)
    void CpuDisableIntr(IopContext& ctx);   // int CpuDisableIntr() -> 0 (idx 8)
    void CpuEnableIntr(IopContext& ctx);    // int CpuEnableIntr() -> 0 (idx 9)

    uint64_t SuspendCount() const { return suspends_.load(std::memory_order_relaxed); }
    uint64_t ResumeCount() const { return resumes_.load(std::memory_order_relaxed); }
    uint64_t NestedSuspendCount() const { return nested_.load(std::memory_order_relaxed); }
    bool Enabled() const { return enabled_.load(std::memory_order_relaxed); }

private:
    IopMemory& mem_;
    std::atomic<bool> enabled_{true};
    std::atomic<uint64_t> suspends_{0}, resumes_{0}, nested_{0};
};

}  // namespace ps2r::iop::hle
