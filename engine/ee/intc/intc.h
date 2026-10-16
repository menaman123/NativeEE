#pragma once
// Intc: the EE interrupt controller. INTC_STAT (0x1000F000) and INTC_MASK
// (0x1000F010) on the MmioRouter (displacing the sink for 0x1000F000-F01F),
// the 15 causes, and dispatch of the guest handlers the kernel registered
// through AddIntcHandler (SyscallTable's registry).
//
// Register semantics (EE Users Manual, INTC):
//  * STAT: a raised cause sets its bit; the guest acknowledges by writing 1
//    to the bit (write-1-to-clear); reads return the pending set.
//  * MASK: a write of 1 REVERSES the bit (toggle); _EnableIntc/_DisableIntc
//    are the kernel's masked toggles. The mask word is owned HERE and bound
//    into SyscallTable so the syscall and the register are one state.
//  * INT0 is asserted while (STAT & MASK) != 0; Dispatch(cause) runs the
//    active handlers of ONE cause iff its bit is set in both, then acks it.
//
// Raise from a foreign host thread never touches STAT directly: it is the
// seventh routing site of the shared predicate (EeEventQueue::RouteIfForeign,
// event kIntcRaise, applied by ThreadManager's applier at the holder's next
// scheduling point -- the boundary's drain step).
//
// Handlers run through the installed GuestCall (the composition root binds
// DispatchTable::Invoke; tests bind a model): $a0 = handler arg, $gp = its
// gp, $ra = the kernel return stub; the caller's PC and GPR file are saved
// around the call and restored (an ISR never leaks registers into the
// interrupted stream).
#include <atomic>
#include <cstdint>
#include <functional>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/mem/mmio_handler.h"

class MmioRouter;

class Intc : public MmioHandler {
public:
    enum Cause : int {
        kGs = 0, kSbus, kVbOn, kVbOf, kVif0, kVif1, kVu0, kVu1, kIpu,
        kTim0, kTim1, kTim2, kTim3, kSfifo, kVu0Wd, kCount
    };
    static constexpr uint32_t kStatAddr = 0x1000F000u;
    static constexpr uint32_t kMaskAddr = 0x1000F010u;
    static constexpr uint32_t kIsrReturnStub = 0x80001000u;  // kernel ISR return address
    using GuestCall = std::function<bool(uint32_t addr, CpuContext& ctx)>;

    static Intc& Instance();
    Intc() = default;

    // Claims 0x1000F000-0x1000F01F and binds the mask word into SyscallTable.
    void Register(MmioRouter& router);
    void SetGuestCall(GuestCall fn) { guest_call_ = std::move(fn); }

    uint64_t Read(uint32_t addr, unsigned size) override;
    void Write(uint32_t addr, unsigned size, uint64_t value) override;

    // Any thread: sets STAT[cause] in place when the caller may mutate guest
    // state; otherwise queues a kIntcRaise event (applied later, in order).
    void Raise(int cause);
    // Holder only (the queue applier / tests): the in-place half of Raise.
    void RaiseInPlace(int cause);
    bool Pending() const { return (Stat() & Mask()) != 0; }
    bool Pending(int cause) const { return ((Stat() & Mask()) >> cause) & 1u; }
    uint32_t Stat() const { return stat_.load(std::memory_order_acquire); }
    uint32_t Mask() const { return mask_; }
    uint32_t* MaskWord() { return &mask_; }

    // Runs the active handlers of `cause` iff STAT & MASK has its bit, then
    // acknowledges that bit only. Returns true when a handler ran.
    bool Dispatch(CpuContext& ctx, int cause);
    // Every pending, enabled cause in ascending order.
    int DispatchPending(CpuContext& ctx);
    // True while a handler runs on this controller. Dispatch / DispatchPending
    // refuse (return false / 0) while set: an ISR's own kernel calls (iSignalSema,
    // the syscall-entry delivery, a yield inside the handler) must not re-enter the
    // dispatch with the cause still pending -- the hardware's "interrupts are
    // masked while the handler runs" (EXL), counted in NestedRefusals().
    bool InDispatch() const { return in_dispatch_; }
    uint64_t NestedRefusals() const { return nested_refusals_; }

    uint64_t RaiseCount() const { return raises_; }
    uint64_t DispatchCount() const { return dispatches_; }
    uint64_t QueuedRaiseCount() const { return queued_raises_.load(std::memory_order_relaxed); }
    void ResetForTest();
    // Savestate seam: the INTC section restores STAT and MASK wholesale
    // (no edge semantics -- the words are the saved state).
    void RestoreRegisters(uint32_t stat, uint32_t mask);

private:
    std::atomic<uint32_t> stat_{0};
    uint32_t mask_ = 0;
    GuestCall guest_call_;
    uint64_t raises_ = 0;
    uint64_t dispatches_ = 0;
    std::atomic<uint64_t> queued_raises_{0};
    bool in_dispatch_ = false;
    uint64_t nested_refusals_ = 0;
};
