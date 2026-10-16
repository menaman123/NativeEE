#pragma once
// SyscallTable: the EE kernel syscall dispatcher behind the generated
// `syscall` sites (runtime_syscall_dispatcher) and the COP0 write seam
// (handle_mtc0_write). 119 live labels + default, mapped onto the kernel
// objects: threads 32-59 -> ThreadManager, InitMainThread/InitHeap/EndOfHeap
// 60-62 -> KernlBoot/BootEnv, semaphores 64-70 -> Semaphores, event flags
// 80-88 -> EventFlags, alarms 24/25/30/31 -> Alarms (tick domain), INTC/DMAC
// handler add/remove/enable/disable -> the handler registry here (the INTC of
// 3D2 reads it), SIF register syscalls -> the idle results the EE observes
// with no IOP (documented in Phase3 plan 8.4), GS IMR/CRT -> recorded state
// (the GS-priv sink of Phase 6 consumes it).
//
// Number normalisation (R5900 SYSCALL, as PCSX2): the number comes from $v1;
// a negative value indexes the table by its absolute value; only the low 8
// bits are significant (iFlushCache = `li v1,-0x68` -> 104).
//
// Labels the kernel documents but OLD left as logged no-ops (RFU, cache, TLB,
// SbusIntc, ExecPS2 ...) keep exactly that shape: counted, $v0 untouched.
// Unknown numbers hit `default:` -- counted and logged, never fatal.
#include <cstdint>
#include <functional>
#include <vector>

#include "engine/ee/cpu/cpu_state.h"

class SyscallTable {
public:
    static SyscallTable& Instance();

    // The two generated-code seams.
    void Dispatch(uint32_t v1, CpuContext& ctx);
    void HandleMtc0(CpuContext& ctx, uint8_t rd, uint32_t value);

    // Coverage: the handler name for a normalised number (nullptr = default).
    static const char* NameOf(int num);
    static int Normalize(uint32_t v1);
    static constexpr int kLiveLabels = 119;
    // The kernel calls whose body may put the calling thread to sleep
    // (SleepThread, SuspendThread, JoinThread, WaitSema, WaitEventFlag): the
    // points where the EE gives the bus to its DMA (Dmac::ObserveAtKernelWait).
    static bool IsBlockingWait(int num);

    // Guest-code call hook (DispatchTable::Invoke in the composition root):
    // SetSysCall-redirected numbers and alarm handlers run guest code through it.
    using GuestCall = std::function<bool(uint32_t addr, CpuContext& ctx)>;
    // sceSifSetDma's data movement: copies `size` bytes from EE RAM `ee_src` to
    // IOP RAM `iop_dst` (bound by the composition root once IOP memory exists).
    using SifDmaCopy = std::function<void(uint32_t ee_src, uint32_t iop_dst, uint32_t size)>;
    void SetSifDmaCopy(SifDmaCopy copy) { sif_dma_copy_ = std::move(copy); }
    // Post-hook run at the end of every sceSifSetDma: the kernel's SIF1 DMA
    // completion raises D_STAT.CIS(6) on the DMAC and dispatches its handlers
    // (the composition root binds it to the Dmac; unbound = nothing).
    using SifDmaHook = std::function<void(CpuContext& ctx)>;
    void SetSifDmaHook(SifDmaHook hook) { sif_dma_hook_ = std::move(hook); }
    uint64_t SifDmaBytes() const { return sif_dma_bytes_; }
    void SetGuestCall(GuestCall fn);
    // Tick-domain clock for alarms (3D1 installs DerivedClocks).
    void SetTickClock(std::function<uint64_t()> now);
    // Observer of every dispatched syscall (number after Normalize, label or
    // null); the composition root records the boot leg's first calls.
    using Trace = std::function<void(int num, const char* name)>;
    void SetTrace(Trace fn) { trace_ = std::move(fn); }
    // Runs at the entry of every dispatched syscall, before its body: the
    // "interrupt taken at the next kernel call" point. A hardware event raised
    // in-line on the guest thread (a GS FINISH from a DMAC kick, a TIMn compare,
    // a DMAC channel completion) sets its STAT bit at once but has no handler
    // run until a dispatch point; on the console the ISR runs before the
    // syscall's effect is observed. The composition root binds it to
    // Intc::DispatchPending (+ Dmac::DispatchInterrupt); unbound = nothing.
    // Intc's in-dispatch guard keeps an ISR's own kernel calls from recursing.
    using PreDispatch = std::function<void(int num, CpuContext& ctx)>;
    void SetPreDispatch(PreDispatch fn) { pre_dispatch_ = std::move(fn); }

    struct KernelHandler {  // INTC / DMAC handler records (AddIntcHandler / AddDmacHandler)
        int id = 0;
        int cause = 0;
        uint32_t pc = 0;
        uint32_t gp = 0;
        uint32_t arg = 0;
        int flag = 0;
        bool active = true;
    };
    const std::vector<KernelHandler>& IntcHandlers() const { return intc_handlers_; }
    const std::vector<KernelHandler>& DmacHandlers() const { return dmac_handlers_; }
    int NextIntcHandlerId() const { return next_intc_id_; }
    // Savestate seam: replaces the INTC handler records and the id counter.
    void InstallIntcHandlers(const std::vector<KernelHandler>& handlers, int next_id);
    // The INTC_MASK word: Intc binds its register here so _EnableIntc /
    // _DisableIntc and the MMIO register are ONE state (unbound = own field).
    void BindIntcMask(uint32_t* mask) { intc_mask_ptr_ = mask ? mask : &intc_mask_; }
    uint32_t IntcMask() const { return *intc_mask_ptr_; }
    uint32_t DmacMask() const { return dmac_mask_; }
    // _EnableDmac / _DisableDmac on the hardware toggle D_STAT's CIM bit of the channel;
    // the composition root binds the Dmac's mask setter here so the kernel word and the
    // register are one state (unbound = the kernel word only).
    using DmacMaskSink = std::function<void(int channel, bool enable)>;
    void SetDmacMaskSink(DmacMaskSink sink) { dmac_mask_sink_ = std::move(sink); }
    uint64_t GsImr() const { return gs_imr_; }

    uint64_t UnknownCount() const { return unknown_count_; }
    uint64_t NotImplementedCount() const { return not_implemented_count_; }
    int LastUnknown() const { return last_unknown_; }
    uint32_t CustomSyscallAddress(int num) const;
    void ResetForTest();

private:
    SyscallTable() = default;
    void NotImplemented(const char* name);
    void RunGuest(uint32_t addr, CpuContext& ctx);

    // handler groups (bodies in syscall_table.cpp)
    void SetGsCrt(CpuContext& ctx);
    void AddHandler(std::vector<KernelHandler>& q, int& next_id, CpuContext& ctx, int max_cause);
    void RemoveHandler(std::vector<KernelHandler>& q, CpuContext& ctx, int max_cause);
    void SetHandlerActive(std::vector<KernelHandler>& q, CpuContext& ctx, bool active);
    void ToggleMask(uint32_t& mask, CpuContext& ctx, bool enable);
    void ToggleDmacMask(CpuContext& ctx, bool enable);
    void SifSetDma(CpuContext& ctx);
    void SifSetReg(CpuContext& ctx);
    void SifGetReg(CpuContext& ctx);
    void SetSysCall(CpuContext& ctx);
    void GetOsdConfigParam(CpuContext& ctx);
    void SetAlarm(CpuContext& ctx);
    void ReleaseAlarm(CpuContext& ctx);
    void Threads(int num, CpuContext& ctx);
    void Semas(int num, CpuContext& ctx);
    void EventFlagsCall(int num, CpuContext& ctx);

    GuestCall guest_call_;
    std::function<uint64_t()> tick_now_;
    Trace trace_;
    PreDispatch pre_dispatch_;
    std::vector<KernelHandler> intc_handlers_;
    std::vector<KernelHandler> dmac_handlers_;
    int next_intc_id_ = 1;   // AddIntcHandler ids (own counter, as the kernel's)
    int next_dmac_id_ = 1;   // AddDmacHandler ids
    uint32_t intc_mask_ = 0;
    uint32_t* intc_mask_ptr_ = &intc_mask_;
    uint32_t dmac_mask_ = 0;
    DmacMaskSink dmac_mask_sink_;
    uint64_t gs_imr_ = 0;
    bool crt_interlaced_ = false;
    int crt_mode_ = 0;
    bool crt_frame_mode_ = false;
    uint32_t custom_syscall_[256] = {};
    uint32_t sif_sys_reg_[3] = {};   // 0x80000000..2: subaddr, mainaddr, rpcinit
    uint32_t sif_dma_id_ = 0;
    SifDmaCopy sif_dma_copy_;
    SifDmaHook sif_dma_hook_;
    uint64_t sif_dma_bytes_ = 0;
    uint64_t unknown_count_ = 0;
    uint64_t not_implemented_count_ = 0;
    int last_unknown_ = -1;
};
