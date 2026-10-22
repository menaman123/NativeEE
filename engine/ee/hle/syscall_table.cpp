#include "engine/ee/hle/syscall_table.h"

#include "engine/core/log/ring_log.h"
#include "engine/ee/hle/alarms.h"
#include "engine/ee/hle/boot_env.h"
#include "engine/ee/hle/dyn_interp.h"
#include "engine/ee/hle/event_flags.h"
#include "engine/ee/hle/kernl_boot.h"
#include "engine/ee/hle/semaphores.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/sched/ee_yield_point.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/recomp_abi/host_app/memory.h"

namespace {
struct Label {
    int num;
    const char* name;
};
// The 119 live labels (OLD syscalls.cpp:2375-2494; `case 0x74` there is a comment).
const Label kLabels[] = {
    {0, "RFU000_FullReset"}, {1, "ResetEE"}, {2, "SetGsCrt"}, {4, "Exit"}, {6, "LoadPS2Exe"},
    {7, "ExecPS2"}, {10, "AddSbusIntcHandler"}, {11, "RemoveSbusIntcHandler"}, {12, "Interrupt2Iop"},
    {13, "SetVTLBRefillHandler"}, {14, "SetVCommonHandler"}, {15, "SetVInterruptHandler"},
    {16, "AddIntcHandler"}, {17, "RemoveIntcHandler"}, {18, "AddDmacHandler"}, {19, "RemoveDmacHandler"},
    {20, "_EnableIntc"}, {21, "_DisableIntc"}, {22, "_EnableDmac"}, {23, "_DisableDmac"},
    {24, "_SetAlarm"}, {25, "_ReleaseAlarm"}, {26, "_iEnableIntc"}, {27, "_iDisableIntc"},
    {28, "_iEnableDmac"}, {29, "_iDisableDmac"}, {30, "_iSetAlarm"}, {31, "_iReleaseAlarm"},
    {32, "CreateThread"}, {33, "DeleteThread"}, {34, "StartThread"}, {35, "ExitThread"},
    {36, "ExitDeleteThread"}, {37, "TerminateThread"}, {38, "iTerminateThread"},
    {39, "DisableDispatchThread"}, {40, "EnableDispatchThread"}, {41, "ChangeThreadPriority"},
    {42, "iChangeThreadPriority"}, {43, "RotateThreadReadyQueue"}, {44, "iRotateThreadReadyQueue"},
    {45, "ReleaseWaitThread"}, {46, "iReleaseWaitThread"}, {47, "GetThreadId"}, {48, "ReferThreadStatus"},
    {49, "SetVSyncFlag"}, {50, "SleepThread"}, {51, "WakeupThread"}, {52, "iWakeupThread"},
    {53, "CancelWakeupThread"}, {54, "iCancelWakeupThread"}, {55, "SuspendThread"}, {56, "iSuspendThread"},
    {57, "ResumeThread"}, {58, "iResumeThread"}, {59, "JoinThread"}, {60, "InitMainThread"},
    {61, "InitHeap"}, {62, "EndOfHeap"}, {64, "CreateSema"}, {65, "DeleteSema"}, {66, "SignalSema"},
    {67, "iSignalSema"}, {68, "WaitSema"}, {69, "PollSema"}, {70, "iPollSema"}, {71, "ReferSemaStatus"},
    {72, "iReferSemaStatus"}, {74, "SetOsdConfigParam"}, {75, "GetOsdConfigParam"}, {76, "GetGsHParam"},
    {77, "GetGsVParam"}, {78, "SetGsHParam"}, {79, "SetGsVParam"}, {80, "CreateEventFlag"},
    {81, "DeleteEventFlag"}, {82, "SetEventFlag"}, {83, "iSetEventFlag"}, {84, "ClearEventFlag"},
    {85, "iClearEventFlag"}, {86, "WaitEventFlag"}, {87, "PollEventFlag"}, {88, "iPollEventFlag"},
    {89, "ReferEventFlagStatus"}, {90, "iReferEventFlagStatus"}, {92, "EnableIntcHandler"},
    {93, "DisableIntcHandler"}, {94, "EnableDmacHandler"}, {95, "DisableDmacHandler"}, {96, "KSeg0"},
    {97, "EnableCache"}, {98, "DisableCache"}, {99, "GetCop0"}, {100, "FlushCache"}, {102, "CpuConfig"},
    {103, "iGetCop0"}, {104, "iFlushCache"}, {106, "iCpuConfig"}, {107, "sceSifStopDma"},
    {108, "SetCPUTimerHandler"}, {109, "SetCPUTimer"}, {110, "ForceRead"}, {111, "ForceWrite"},
    {112, "GsGetIMR"}, {113, "GsPutIMR"}, {114, "SetPgifHandler"}, {115, "SetVSyncFlag"},
    {116, "SetSysCall"}, {117, "print"}, {118, "sceSifDmaStat"}, {119, "sceSifSetDma"},
    {120, "sceSifSetDChain"}, {121, "sceSifSetReg"}, {122, "sceSifGetReg"}, {123, "ExecOSD"},
    {124, "Deci2Call"}, {125, "PSMode"}, {126, "MachineType"}, {127, "GetMemorySize"},
};
static_assert(sizeof(kLabels) / sizeof(kLabels[0]) == SyscallTable::kLiveLabels, "119 live labels");

constexpr uint32_t kKernelSyscallTable = 0x800002E0;  // kernel's syscall vector table (KSEG0)
constexpr uint32_t kSifRegBase = 0x1000F200;           // MSCOM; SMCOM +0x10, MSFLAG +0x20, SMFLAG +0x30, CTRL +0x40, BD6 +0x60

uint32_t SifRegAddr(uint32_t index) {
    switch (index) {
        case 1: return kSifRegBase + 0x00;
        case 2: return kSifRegBase + 0x10;
        case 3: return kSifRegBase + 0x20;
        case 4: return kSifRegBase + 0x30;
        case 5: return kSifRegBase + 0x40;
        case 6: return kSifRegBase + 0x60;
        default: return 0;
    }
}

inline uint32_t A(const CpuContext& ctx, int n) { return ctx.cpuRegs.GPR.r[4 + n].UL[0]; }
inline int32_t As(const CpuContext& ctx, int n) { return ctx.cpuRegs.GPR.r[4 + n].SL[0]; }
inline void RetS(CpuContext& ctx, int32_t v) { ctx.cpuRegs.GPR.r[2].SD[0] = v; }
inline void RetU(CpuContext& ctx, uint32_t v) { ctx.cpuRegs.GPR.r[2].UL[0] = v; }

bool LooksLikeGuestPtr(uint32_t v) {
    if (v == 0) return true;
    const uint32_t norm = MainMemory::Phys(v);
    return norm < BootEnv::kRamTop && norm >= 0x10000u;
}
}  // namespace

SyscallTable& SyscallTable::Instance() {
    static SyscallTable inst;
    return inst;
}

const char* SyscallTable::NameOf(int num) {
    for (const Label& l : kLabels)
        if (l.num == num) return l.name;
    return nullptr;
}

int SyscallTable::Normalize(uint32_t v1) {
    const int32_t sn = static_cast<int32_t>(v1);
    return (sn < 0) ? static_cast<int>(static_cast<uint8_t>(-sn)) : static_cast<int>(static_cast<uint8_t>(v1 & 0xff));
}

bool SyscallTable::IsBlockingWait(int num) {
    switch (num) {
        case 50:   // SleepThread
        case 55:   // SuspendThread (self)
        case 59:   // JoinThread
        case 68:   // WaitSema
        case 86:   // WaitEventFlag
            return true;
        default: return false;
    }
}

bool SyscallTable::WouldBlockNow(int num, const CpuContext& ctx) {
    switch (num) {
        case 50: {   // SleepThread: a banked WakeupThread is consumed at once (R4)
            auto info = ThreadManager::Instance().Current();
            if (!info) return false;
            std::lock_guard<std::mutex> lk(info->m);
            return info->wakeup_count <= 0;
        }
        case 55:   // SuspendThread (self)
        case 59:   // JoinThread
            return true;
        case 68: {   // WaitSema: count > 0 -> count--, return (S3)
            SemaStatus st;
            if (Semaphores::Instance().ReferStatus(As(ctx, 0), st) != KE_OK) return false;
            return st.count <= 0;
        }
        case 86: {   // WaitEventFlag: the pattern test (E2); an error return is not a park
            const uint32_t wait_bits = A(ctx, 1), mode = A(ctx, 2);
            if ((mode & ~WEF_MODE_MASK) != 0 || wait_bits == 0) return false;
            EventFlagStatus st;
            if (EventFlags::Instance().ReferStatus(As(ctx, 0), st) != KE_OK) return false;
            return !EventFlags::Satisfied(st.bits, wait_bits, mode);
        }
        default: return false;
    }
}

void SyscallTable::SetGuestCall(GuestCall fn) { guest_call_ = std::move(fn); }
void SyscallTable::SetTickClock(std::function<uint64_t()> now) { tick_now_ = std::move(now); }
uint32_t SyscallTable::CustomSyscallAddress(int num) const {
    return (num >= 0 && num < 256) ? custom_syscall_[num] : 0;
}

void SyscallTable::NotImplemented(const char* /*name*/) { ++not_implemented_count_; }

void SyscallTable::RunGuest(uint32_t addr, CpuContext& ctx) {
    ctx.cpuRegs.pc = addr;
    if (guest_call_ && guest_call_(addr, ctx)) return;
    DynInterp::Trip(addr, ctx);
}

void SyscallTable::Dispatch(uint32_t v1, CpuContext& ctx) {
    const int num = Normalize(v1);
    EeYieldPoint::Instance().NoteKernelCall();   // a scheduling point: the back-edge budget starts over (E432)
    if (trace_) trace_(num, NameOf(num));
    if (pre_dispatch_) pre_dispatch_(num, ctx);
    if (custom_syscall_[num] != 0) {  // SetSysCall redirected this number to guest code
        RunGuest(custom_syscall_[num], ctx);
        return;
    }
    switch (num) {
        case 2: SetGsCrt(ctx); break;
        case 4:  // Exit: the calling thread ends (the composition root observes the exit code)
            ThreadManager::Instance().ExitThread();
            // ExitThread never returns (ThreadExitUnwind); the fallthrough below is
            // unreachable and only satisfies the compiler's control-flow view.
            [[fallthrough]];
        case 16: AddHandler(intc_handlers_, next_intc_id_, ctx, 14); break;
        case 17: RemoveHandler(intc_handlers_, ctx, 14); break;
        case 18: AddHandler(dmac_handlers_, next_dmac_id_, ctx, 15); break;
        case 19: RemoveHandler(dmac_handlers_, ctx, 15); break;
        case 20: ToggleMask(*intc_mask_ptr_, ctx, true); break;
        case 21: ToggleMask(*intc_mask_ptr_, ctx, false); break;
        case 22: ToggleDmacMask(ctx, true); break;
        case 23: ToggleDmacMask(ctx, false); break;
        case 24: case 30: SetAlarm(ctx); break;
        case 25: case 31: ReleaseAlarm(ctx); break;
        case 32: case 33: case 34: case 35: case 36: case 37: case 38: case 39: case 40: case 41:
        case 42: case 43: case 44: case 45: case 46: case 47: case 48: case 50: case 51: case 52:
        case 53: case 54: case 55: case 56: case 57: case 58:
            Threads(num, ctx);
            break;
        case 60:
            RetU(ctx, KernlBoot::InitMainThread(A(ctx, 0), A(ctx, 1), As(ctx, 2), A(ctx, 3),
                                                ctx.cpuRegs.GPR.r[8].SL[0], ctx));
            break;
        case 61: RetU(ctx, KernlBoot::InitHeap(A(ctx, 0), As(ctx, 1), ctx)); break;
        case 62: RetU(ctx, KernlBoot::EndOfHeap()); break;
        case 64: case 65: case 66: case 67: case 68: case 69: case 70: case 71: case 72: Semas(num, ctx); break;
        case 74: RetS(ctx, 0); break;  // SetOsdConfigParam: accepted
        case 75: GetOsdConfigParam(ctx); break;
        case 80: case 81: case 82: case 83: case 84: case 85: case 86: case 87: case 88: case 89: case 90:
            EventFlagsCall(num, ctx);
            break;
        case 92: SetHandlerActive(intc_handlers_, ctx, true); break;
        case 93: SetHandlerActive(intc_handlers_, ctx, false); break;
        case 94: SetHandlerActive(dmac_handlers_, ctx, true); break;
        case 95: SetHandlerActive(dmac_handlers_, ctx, false); break;
        case 100: RetS(ctx, 0); break;  // FlushCache: caches are coherent here
        case 104: RetS(ctx, 0); break;  // iFlushCache (the VIF1 i-bit handler's write-back): the same
        case 107: RetS(ctx, 0); break;  // sceSifStopDma: SIF0 STR cleared (no DMAC yet)
        case 112: ctx.cpuRegs.GPR.r[2].UD[0] = gs_imr_.load(std::memory_order_relaxed); break;
        case 113: gs_imr_.store(ctx.cpuRegs.GPR.r[4].UD[0], std::memory_order_relaxed); break;
        case 116: SetSysCall(ctx); break;
        case 118: RetS(ctx, -1); break;  // sceSifDmaStat: -1 = transfer complete
        case 119: SifSetDma(ctx); break;
        case 120: ctx.cpuRegs.GPR.r[2].UD[0] = 1; break;  // sceSifSetDChain: started
        case 121: SifSetReg(ctx); break;
        case 122: SifGetReg(ctx); break;
        case 124: RetS(ctx, -1); break;  // Deci2Call: no DECI2 hardware on retail
        case 127: RetS(ctx, static_cast<int32_t>(BootEnv::kRamTop)); break;
        // Documented labels OLD left as logged no-ops ($v0 untouched).
        case 0: case 1: case 6: case 7: case 10: case 11: case 12: case 13: case 14: case 15:
        case 26: case 27: case 28: case 29: case 49: case 59: case 76: case 77:
        case 78: case 79: case 96: case 97: case 98: case 99: case 102: case 103:
        case 106: case 108: case 109: case 110: case 111: case 114: case 115: case 117:
        case 123: case 125: case 126:
            NotImplemented(NameOf(num));
            break;
        default:
            ++unknown_count_;
            last_unknown_ = num;
            ps2r::ringlog::RingLog::EmitV(ps2r::ringlog::CH_EE, ps2r::ringlog::TAG_INDIRECT_JUMP,
                                          static_cast<uint64_t>(num) | 0x5C00000000ull, ctx.cpuRegs.pc);
            break;
    }
}

// ---- COP0 writes (row 6). Count (rd 9) is the free-running counter: a guest
// write reseats it, reads come from the register (the timing layer advances it).
void SyscallTable::HandleMtc0(CpuContext& ctx, uint8_t rd, uint32_t value) {
    auto& cp0 = ctx.cpuRegs.CP0;
    switch (rd) {
        case 12:  // Status: writable CU/BEV/IM/KU/IE bits only
            cp0.n.Status = (value & 0xF0FF003Fu) | (cp0.n.Status & ~0xF0FF003Fu);
            break;
        case 13:  // Cause: software interrupt bits IP0/IP1 only
            cp0.n.Cause = (cp0.n.Cause & ~0x300u) | (value & 0x300u);
            break;
        case 11:  // Compare: clears the timer interrupt (IP7)
            cp0.r[11] = value;
            cp0.n.Cause &= ~0x8000u;
            break;
        default:
            cp0.r[rd] = value;
            break;
    }
}

void SyscallTable::SetGsCrt(CpuContext& ctx) {
    crt_interlaced_ = As(ctx, 0) != 0;
    crt_mode_ = As(ctx, 1);
    crt_frame_mode_ = As(ctx, 2) != 0;
    RetU(ctx, 0);
}

void SyscallTable::InstallIntcHandlers(const std::vector<KernelHandler>& handlers, int next_id) {
    intc_handlers_ = handlers;
    next_intc_id_ = next_id;
}

void SyscallTable::AddHandler(std::vector<KernelHandler>& q, int& next_id, CpuContext& ctx, int max_cause) {
    const int cause = As(ctx, 0);
    const uint32_t pc = A(ctx, 1);
    const int next = As(ctx, 2);
    if (cause < 0 || cause > max_cause || pc == 0) {
        RetS(ctx, -1);
        return;
    }
    KernelHandler h;
    h.id = next_id++;
    h.cause = cause;
    h.pc = pc;
    h.gp = ctx.cpuRegs.GPR.r[28].UL[0];
    h.arg = A(ctx, 3);
    h.flag = memory::read<int32_t>(ctx.cpuRegs.GPR.r[29].UL[0] + 16);  // 5th argument on the stack
    // `next`: 0 = head of the cause's chain, -1 = tail, else before handler id `next`.
    auto pos = q.end();
    if (next == 0) {
        pos = q.begin();
        while (pos != q.end() && pos->cause != cause) ++pos;
    } else if (next > 0) {
        for (auto it = q.begin(); it != q.end(); ++it)
            if (it->id == next) { pos = it; break; }
    }
    q.insert(pos, h);
    RetS(ctx, h.id);
}

void SyscallTable::RemoveHandler(std::vector<KernelHandler>& q, CpuContext& ctx, int max_cause) {
    const int cause = As(ctx, 0);
    const int id = As(ctx, 1);
    if (cause < 0 || cause > max_cause) {
        RetS(ctx, -1);
        return;
    }
    for (auto it = q.begin(); it != q.end(); ++it) {
        if (it->id == id && it->cause == cause) {
            q.erase(it);
            RetS(ctx, 0);
            return;
        }
    }
    RetS(ctx, -1);
}

void SyscallTable::SetHandlerActive(std::vector<KernelHandler>& q, CpuContext& ctx, bool active) {
    const int id = As(ctx, 0);
    for (KernelHandler& h : q) {
        if (h.id == id) {
            h.active = active;
            RetS(ctx, 1);
            return;
        }
    }
    RetS(ctx, -1);
}

// _EnableIntc/_DisableIntc (cause bit 0..15) and _EnableDmac/_DisableDmac
// (channel 0..15): return 1 when the call changed the mask, 0 otherwise.
void SyscallTable::ToggleMask(uint32_t& mask, CpuContext& ctx, bool enable) {
    const int bit = As(ctx, 0);
    if (bit < 0 || bit > 15) {
        RetS(ctx, 0);
        return;
    }
    const bool was_set = (mask >> bit) & 1u;
    if (enable && !was_set) mask |= (1u << bit);
    if (!enable && was_set) mask &= ~(1u << bit);
    RetS(ctx, enable ? (was_set ? 0 : 1) : (was_set ? 1 : 0));
}

void SyscallTable::ToggleDmacMask(CpuContext& ctx, bool enable) {
    const int channel = As(ctx, 0);
    const uint32_t before = dmac_mask_;
    ToggleMask(dmac_mask_, ctx, enable);
    if (dmac_mask_ != before && dmac_mask_sink_) dmac_mask_sink_(channel, enable);
}

// sceSifSetDma with no IOP: a list without a command entry (attr & 4) returns
// 0; otherwise the transfer is "accepted" and its id (1, 2, ...) returned.
// sceSifSetDma(SifDmaTransfer* sdt, int count): each 16-byte entry is
// {src (EE), dest (IOP), size, attr}; the bytes move EE -> IOP through the
// bound copy (SIF DMA is a memcpy here, never DMAC register traffic). The
// returned id follows the original: 0 unless an entry carries the command
// attribute (0x04), else the next transfer id.
void SyscallTable::SifSetDma(CpuContext& ctx) {
    const uint32_t sdt = A(ctx, 0);
    const int32_t count = As(ctx, 1);
    bool has_cmd = false;
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t e = sdt + static_cast<uint32_t>(i) * 16u;
        if (memory::read<int32_t>(e + 0x0C) & 0x04) has_cmd = true;
        const uint32_t size = memory::read<uint32_t>(e + 0x08);
        if (sif_dma_copy_ && size != 0) {
            sif_dma_copy_(memory::read<uint32_t>(e + 0x00), memory::read<uint32_t>(e + 0x04), size);
            sif_dma_bytes_ += size;
        }
    }
    if (!has_cmd) {
        RetU(ctx, 0);
    } else {
        RetU(ctx, ++sif_dma_id_);
    }
    if (sif_dma_hook_) sif_dma_hook_(ctx);
}

// sceSifSetReg: system registers 0x8000000x are kernel-side words; physical
// indices 1..6 write the SIF register word itself (memory::write -> the
// SifState block on the router: the single source of truth for the flag
// words) and, for MSFLAG, the "IOP" acknowledges SIFINIT/CMDINIT on SMFLAG at
// once (no IOP exists to do it later).
void SyscallTable::SifSetReg(CpuContext& ctx) {
    const uint32_t index = A(ctx, 0);
    const uint32_t value = A(ctx, 1);
    if (index & 0x80000000u) {
        const uint32_t k = index & 0x7FFFFFFFu;
        if (k < 3) sif_sys_reg_[k] = value;
        RetU(ctx, value);
        return;
    }
    const uint32_t paddr = SifRegAddr(index);
    if (paddr == 0) {
        RetU(ctx, 0);
        return;
    }
    memory::write<uint32_t>(paddr, value);
    if (index == 3) {
        const uint32_t ack = value & 0x30000u;  // SIF_STAT_SIFINIT | SIF_STAT_CMDINIT
        if (ack) {
            const uint32_t smflag_addr = SifRegAddr(4);
            memory::write<uint32_t>(smflag_addr, memory::read<uint32_t>(smflag_addr) | ack);
        }
    }
    RetU(ctx, value);
}

void SyscallTable::SifGetReg(CpuContext& ctx) {
    const uint32_t index = A(ctx, 0);
    uint32_t result = 0;
    if (index & 0x80000000u) {
        const uint32_t k = index & 0x7FFFFFFFu;
        if (k < 3) result = sif_sys_reg_[k];
    } else {
        const uint32_t paddr = SifRegAddr(index);
        if (paddr != 0) result = memory::read<uint32_t>(paddr);  // the SIF word itself (SifState)
    }
    RetU(ctx, result);
}

void SyscallTable::SetSysCall(CpuContext& ctx) {
    const int index = As(ctx, 0);
    const uint32_t addr = A(ctx, 1);
    if (index >= 0 && index < 256) {
        custom_syscall_[index] = addr;
        memory::write<uint32_t>(kKernelSyscallTable + static_cast<uint32_t>(index) * 4u, addr);
    }
}

// GetOsdConfigParam: spdif off, 4:3, component out, English, GMT, standard time,
// PS1 driver defaults, version 1.
void SyscallTable::GetOsdConfigParam(CpuContext& ctx) {
    const uint32_t p = A(ctx, 0);
    static const int32_t kWords[8] = {0, 0, 1, 1, 0, 0, 0, 1};
    for (uint32_t i = 0; i < 8; ++i) memory::write<int32_t>(p + i * 4u, kWords[i]);
    RetS(ctx, 0);
}

void SyscallTable::SetAlarm(CpuContext& ctx) {
    const uint32_t ticks = A(ctx, 0);
    const uint32_t handler = A(ctx, 1);
    const uint32_t arg = A(ctx, 2);
    const uint64_t now = tick_now_ ? tick_now_() : 0;
    const int id = Alarms::Instance().SetAlarm(
        now, ticks,
        [this, handler](int alarm_id, uint32_t time, uint32_t a) {
            CpuContext* cur = nullptr;
            if (auto info = ThreadManager::Instance().Current()) cur = info->ctx;
            if (!cur) return;
            cur->cpuRegs.GPR.r[4].SD[0] = alarm_id;
            cur->cpuRegs.GPR.r[5].UL[0] = time;
            cur->cpuRegs.GPR.r[6].UL[0] = a;
            RunGuest(handler, *cur);
        },
        arg);
    RetS(ctx, id);
}

void SyscallTable::ReleaseAlarm(CpuContext& ctx) { RetS(ctx, Alarms::Instance().ReleaseAlarm(As(ctx, 0))); }

void SyscallTable::Threads(int num, CpuContext& ctx) {
    auto& tm = ThreadManager::Instance();
    switch (num) {
        case 32: {  // CreateThread(ThreadParam*): 7 words; gp/priority decoded as the SDK lays them out
            const uint32_t p = A(ctx, 0);
            if (p == 0) { RetS(ctx, KE_ERROR); return; }
            ThreadParam tp;
            tp.attr = memory::read<uint32_t>(p + 0x00);
            tp.entry = memory::read<uint32_t>(p + 0x04);
            tp.stack = memory::read<uint32_t>(p + 0x08);
            tp.stack_size = memory::read<uint32_t>(p + 0x0C);
            const uint32_t w4 = memory::read<uint32_t>(p + 0x10);
            const uint32_t w5 = memory::read<uint32_t>(p + 0x14);
            tp.option = memory::read<uint32_t>(p + 0x18);
            if (LooksLikeGuestPtr(w4) && w5 <= 0x400u) { tp.gp = w4; tp.priority = static_cast<int>(w5); }
            else if (LooksLikeGuestPtr(w5) && w4 <= 0x400u) { tp.gp = w5; tp.priority = static_cast<int>(w4); }
            else { tp.gp = w4; tp.priority = static_cast<int>(w5); }
            RetS(ctx, tm.CreateThread(tp));
            return;
        }
        case 33: RetS(ctx, tm.DeleteThread(As(ctx, 0))); return;
        case 34:
            RetS(ctx, tm.StartThread(As(ctx, 0), A(ctx, 1), ctx.cpuRegs.GPR.r[29].UL[0],
                                     ctx.cpuRegs.GPR.r[28].UL[0]));
            return;
        case 35: tm.ExitThread();
        case 36: tm.ExitDeleteThread();
        case 37: case 38: RetS(ctx, tm.TerminateThread(As(ctx, 0))); return;
        case 39: RetS(ctx, tm.DisableDispatchThread()); return;
        case 40: RetS(ctx, tm.EnableDispatchThread()); return;
        case 41: case 42: RetS(ctx, tm.ChangeThreadPriority(As(ctx, 0), As(ctx, 1))); return;
        case 43: case 44: RetS(ctx, tm.RotateThreadReadyQueue(As(ctx, 0))); return;
        case 45: case 46: RetS(ctx, tm.ReleaseWaitThread(As(ctx, 0))); return;
        case 47: RetS(ctx, tm.CurrentTid()); return;
        case 48: {  // ReferThreadStatus(tid, ThreadStatus*): 12 words; tid 0 = self; returns tid
            int tid = As(ctx, 0);
            if (tid == 0) tid = tm.CurrentTid();
            ThreadStatus st;
            const int r = tm.ReferThreadStatus(tid, st);
            if (r < 0) { RetS(ctx, r); return; }
            const uint32_t p = A(ctx, 1);
            memory::write<int32_t>(p + 0x00, st.status);
            memory::write<uint32_t>(p + 0x04, st.func);
            memory::write<uint32_t>(p + 0x08, st.stack);
            memory::write<int32_t>(p + 0x0C, st.stack_size);
            memory::write<uint32_t>(p + 0x10, st.gp_reg);
            memory::write<int32_t>(p + 0x14, st.initial_priority);
            memory::write<int32_t>(p + 0x18, st.current_priority);
            memory::write<uint32_t>(p + 0x1C, st.attr);
            memory::write<uint32_t>(p + 0x20, st.option);
            memory::write<uint32_t>(p + 0x24, st.wait_type);
            memory::write<uint32_t>(p + 0x28, st.wait_id);
            memory::write<uint32_t>(p + 0x2C, st.wakeup_count);
            RetS(ctx, tid);
            return;
        }
        case 50: RetS(ctx, tm.SleepThread()); return;
        case 51: case 52: RetS(ctx, tm.WakeupThread(As(ctx, 0))); return;
        case 53: case 54: RetS(ctx, tm.CancelWakeupThread(As(ctx, 0))); return;
        case 55: case 56: RetS(ctx, tm.SuspendThread(As(ctx, 0))); return;
        case 57: case 58: RetS(ctx, tm.ResumeThread(As(ctx, 0))); return;
        default: return;
    }
}

void SyscallTable::Semas(int num, CpuContext& ctx) {
    auto& sm = Semaphores::Instance();
    switch (num) {
        case 64: {  // CreateSema(SemaParam*): attr, maxCount, initCount, option (clamped as the kernel does)
            const uint32_t p = A(ctx, 0);
            if (p == 0) { RetS(ctx, KE_ERROR); return; }
            SemaParam sp;
            sp.attr = memory::read<uint32_t>(p + 0x00);
            sp.max_count = memory::read<int32_t>(p + 0x04);
            sp.init_count = memory::read<int32_t>(p + 0x08);
            sp.option = memory::read<uint32_t>(p + 0x0C);
            if (sp.max_count <= 0) sp.max_count = 1;
            if (sp.init_count < 0) sp.init_count = 0;
            if (sp.init_count > sp.max_count) sp.init_count = sp.max_count;
            RetS(ctx, sm.Create(sp));
            return;
        }
        case 65: RetS(ctx, sm.Delete(As(ctx, 0))); return;
        case 66: case 67: RetS(ctx, sm.Signal(As(ctx, 0))); return;
        case 68: RetS(ctx, sm.Wait(As(ctx, 0))); return;
        case 69: case 70: RetS(ctx, sm.Poll(As(ctx, 0))); return;
        case 71: case 72: {  // ReferSemaStatus / iReferSemaStatus(sid, SemaParam*): currentCount, maxCount,
            // initCount, numWaitThreads, attr, option (SDK eekernel.h order); returns the id, or KE_UNKNOWN_SEMID.
            const int sid = As(ctx, 0);
            const uint32_t p = A(ctx, 1);
            SemaStatus st;
            const int r = sm.ReferStatus(sid, st);
            if (r != KE_OK) { RetS(ctx, r); return; }
            if (p != 0) {
                memory::write<int32_t>(p + 0x00, st.count);
                memory::write<int32_t>(p + 0x04, st.max_count);
                memory::write<int32_t>(p + 0x08, st.init_count);
                memory::write<int32_t>(p + 0x0C, st.wait_threads);
                memory::write<uint32_t>(p + 0x10, st.attr);
                memory::write<uint32_t>(p + 0x14, st.option);
            }
            RetS(ctx, sid);
            return;
        }
        default: return;
    }
}

void SyscallTable::EventFlagsCall(int num, CpuContext& ctx) {
    auto& ef = EventFlags::Instance();
    switch (num) {
        case 80: {  // CreateEventFlag(EventFlagParam*): attr, option, initPattern
            const uint32_t p = A(ctx, 0);
            if (p == 0) { RetS(ctx, KE_ERROR); return; }
            EventFlagParam ep;
            ep.attr = memory::read<uint32_t>(p + 0x00);
            ep.option = memory::read<uint32_t>(p + 0x04);
            ep.init_bits = memory::read<uint32_t>(p + 0x08);
            RetS(ctx, ef.Create(ep));
            return;
        }
        case 81: RetS(ctx, ef.Delete(As(ctx, 0))); return;
        case 82: case 83: RetS(ctx, ef.Set(As(ctx, 0), A(ctx, 1))); return;
        case 84: case 85: RetS(ctx, ef.Clear(As(ctx, 0), A(ctx, 1))); return;
        case 86: case 87: case 88: {
            uint32_t result = 0;
            const int r = (num == 86) ? ef.Wait(As(ctx, 0), A(ctx, 1), A(ctx, 2), &result)
                                      : ef.Poll(As(ctx, 0), A(ctx, 1), A(ctx, 2), &result);
            const uint32_t out = A(ctx, 3);
            if (out != 0) memory::write<uint32_t>(out, result);
            RetS(ctx, r);
            return;
        }
        case 89: case 90: {  // ReferEventFlagStatus / iReferEventFlagStatus(eid, EventFlagParam*): attr, option,
            // initPattern, currentPattern, numWaitThreads (SDK eekernel.h order); returns the id, or KE_UNKNOWN_EVFID.
            const int eid = As(ctx, 0);
            const uint32_t p = A(ctx, 1);
            EventFlagStatus st;
            const int r = ef.ReferStatus(eid, st);
            if (r != KE_OK) { RetS(ctx, r); return; }
            if (p != 0) {
                memory::write<uint32_t>(p + 0x00, st.attr);
                memory::write<uint32_t>(p + 0x04, st.option);
                memory::write<uint32_t>(p + 0x08, st.init_bits);
                memory::write<uint32_t>(p + 0x0C, st.bits);
                memory::write<int32_t>(p + 0x10, st.wait_threads);
            }
            RetS(ctx, eid);
            return;
        }
        default: return;
    }
}

void SyscallTable::ResetForTest() {
    intc_handlers_.clear();
    dmac_handlers_.clear();
    next_intc_id_ = 1;
    next_dmac_id_ = 1;
    intc_mask_ = dmac_mask_ = 0;
    *intc_mask_ptr_ = 0;
    gs_imr_.store(0, std::memory_order_relaxed);
    for (uint32_t& c : custom_syscall_) c = 0;
    for (uint32_t& r : sif_sys_reg_) r = 0;
    sif_dma_id_ = 0;
    sif_dma_copy_ = nullptr;
    sif_dma_hook_ = nullptr;
    pre_dispatch_ = nullptr;
    sif_dma_bytes_ = 0;
    unknown_count_ = not_implemented_count_ = 0;
    last_unknown_ = -1;
}
