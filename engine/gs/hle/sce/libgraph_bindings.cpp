#include "engine/gs/hle/sce/libgraph_bindings.h"

#include "engine/ee/hle/sdk_binding_manifest.h"
#include <vector>

#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/gs/state/gs_priv_regs.h"

namespace ps2r {
namespace gs {

namespace {
constexpr uint32_t kPrivBase = 0x12000000u;   // the GS privileged register page (guest address)
constexpr int kIntcVblankStart = 2;   // INTC cause VBON
constexpr uint32_t kSyscallEnableIntc = 20;
constexpr uint32_t kSyscallDisableIntc = 21;

uint32_t Arg(const CpuContext& ctx, int i) { return ctx.cpuRegs.GPR.r[4 + i].UL[0]; }

// The console's power-on display environment as the BIOS leaves it for an
// NTSC title: DISPFB base 0x70000 (FBP 0x38), width 512 (FBW 8), PSMCT32;
// DISPLAY DX 0x27C, DY 0x32/0x33 (the two fields), MAGH 4, DW 2559, DH 447.
uint64_t PowerOnDispfb() {
    const uint64_t fbp = 0x38, fbw = 8, psm = 0;
    return fbp | (fbw << 9) | (psm << 15);
}
uint64_t PowerOnDisplay(uint32_t dy) {
    const uint64_t dx = 0x27C, magh = 4, magv = 0, dw = 2559, dh = 447;
    return dx | (static_cast<uint64_t>(dy & 0x7FF) << 12) | (magh << 23) | (magv << 27) | (dw << 32) | (dh << 44);
}
}  // namespace

GsPrivRegs* LibgraphBindings::shadow_ = nullptr;
LibgraphBindings::Statics LibgraphBindings::statics_;
uint64_t LibgraphBindings::reset_graph_calls_ = 0;
uint64_t LibgraphBindings::syncv_callback_calls_ = 0;

int LibgraphBindings::Bind(SdkBindingManifest& m, GsPrivRegs* shadow, const Statics& statics) {
    shadow_ = shadow;
    statics_ = statics;
    m.Register("libgraph.a", "sceGsResetGraph", &ResetGraph);
    m.Register("libgraph.a", "sceGsSyncVCallback", &SyncVCallback);
    return 2;
}

uint64_t LibgraphBindings::ResetGraphCalls() { return reset_graph_calls_; }
uint64_t LibgraphBindings::SyncVCallbackCalls() { return syncv_callback_calls_; }

void LibgraphBindings::ResetForTest() {
    shadow_ = nullptr;
    statics_ = Statics{};
    reset_graph_calls_ = 0;
    syncv_callback_calls_ = 0;
}

// `reg` is a GsPrivRegs::Addr offset; the guest store goes to the page (the MMIO
// route answers it), the shadow records the same register.
void LibgraphBindings::WritePriv(uint32_t reg, uint64_t value) {
    memory::write<uint64_t>(kPrivBase + reg, value);
    if (shadow_) shadow_->Write(reg, value);
}

// int sceGsResetGraph(short mode, short interlace, short omode, short ffmode) --
// the SDK 3.0 libgraph shape (the title's copy is the manifest row): mode 0 = CSR RESET,
// mode 1 = CSR FLUSH; GsGParam = four shorts {interlace, omode, ffmode != 0,
// GS revision = CSR bits 48..55} then the SyncV callback fn (+8) and its handler
// id (+0xC), which a reset tears down (DisableIntc(2), RemoveIntcHandler(2, id),
// both words zeroed); GsPutIMR(0xff00) masks every GS interrupt; the tail call is
// SetGsCrt(interlace & 1, omode & 0xff, ffmode & 1), whose v0 is the result.
void LibgraphBindings::ResetGraph(CpuContext& ctx) {
    ++reset_graph_calls_;
    const uint32_t mode = Arg(ctx, 0) & 0xffff;
    const uint32_t interlace = Arg(ctx, 1) & 0xffff, omode = Arg(ctx, 2) & 0xffff, ffmode = Arg(ctx, 3) & 0xffff;
    WritePriv(GsPrivRegs::kCsr, mode == 1 ? GsPrivRegs::kCsrFlush : GsPrivRegs::kCsrReset);
    const uint32_t gparam = statics_.gparam_addr;
    if (gparam) {
        const uint64_t csr = shadow_ ? shadow_->Read(GsPrivRegs::kCsr) : 0;
        memory::write<uint16_t>(gparam + 0x0, static_cast<uint16_t>(interlace));
        memory::write<uint16_t>(gparam + 0x2, static_cast<uint16_t>(omode));
        memory::write<uint16_t>(gparam + 0x4, static_cast<uint16_t>(ffmode != 0));
        memory::write<uint16_t>(gparam + 0x6, static_cast<uint16_t>((csr >> 48) & 0xff));
    }
    {
        // GsPutIMR(0xff00) as the kernel row on a scratch context.
        CpuContext k = ctx;
        k.cpuRegs.GPR.r[4].UD[0] = 0xff00;
        SyscallTable::Instance().Dispatch(113, k);
    }
    if (gparam && memory::read<uint32_t>(gparam + 0x08) != 0) {
        const int id = static_cast<int>(memory::read<uint32_t>(gparam + 0x0C));
        CpuContext k = ctx;
        k.cpuRegs.GPR.r[4].UD[0] = 2;
        SyscallTable::Instance().Dispatch(21, k);   // _DisableIntc(2) (VBON)
        k = ctx;
        k.cpuRegs.GPR.r[4].UD[0] = 2;
        k.cpuRegs.GPR.r[5].UD[0] = static_cast<uint32_t>(id);
        SyscallTable::Instance().Dispatch(17, k);   // RemoveIntcHandler(2, id)
        memory::write<uint32_t>(gparam + 0x0C, 0);
        memory::write<uint32_t>(gparam + 0x08, 0);
    }
    // The BIOS power-on display environment (SMODE2/DISPLAY/DISPFB) the original
    // runtime programmed here is NOT part of the SDK row (SetGsCrt programs the sync
    // registers, the title programs its own display environment later); it stays
    // for OLD parity until Phase 6's GS owns scanout -- plan row P4-LIBGRAPH-SDK-SHAPE.
    WritePriv(GsPrivRegs::kSmode2, 1);   // INT=1, FFMD=0: interlaced field mode
    WritePriv(GsPrivRegs::kDisplay1, PowerOnDisplay(0x32));
    WritePriv(GsPrivRegs::kDisplay2, PowerOnDisplay(0x33));
    WritePriv(GsPrivRegs::kDispfb1, PowerOnDispfb());
    WritePriv(GsPrivRegs::kDispfb2, PowerOnDispfb());
    // Tail call: SetGsCrt(interlace & 1, omode & 0xff, ffmode & 1); its v0 is ours.
    ctx.cpuRegs.GPR.r[4].UD[0] = interlace & 1;
    ctx.cpuRegs.GPR.r[5].UD[0] = omode & 0xff;
    ctx.cpuRegs.GPR.r[6].UD[0] = ffmode & 1;
    SyscallTable::Instance().Dispatch(2, ctx);
}

void LibgraphBindings::SyncVCallback(CpuContext& ctx) {
    ++syncv_callback_calls_;
    const uint32_t func = Arg(ctx, 0);
    const uint32_t gparam = statics_.gparam_addr;
    const uint32_t prev_fn = gparam ? memory::read<uint32_t>(gparam + 0x08) : 0;
    const int prev_id = gparam ? static_cast<int>(memory::read<uint32_t>(gparam + 0x0C)) : 0;
    SyscallTable& kernel = SyscallTable::Instance();

    auto toggle_cause = [&](uint32_t syscall) {
        CpuContext k = ctx;   // the kernel row reads a0 = cause; its v0 is not the row's
        k.cpuRegs.GPR.r[4].UD[0] = kIntcVblankStart;
        kernel.Dispatch(syscall, k);
    };
    auto remove_existing = [&]() {
        if (prev_id == 0) return;
        std::vector<SyscallTable::KernelHandler> q = kernel.IntcHandlers();
        for (auto it = q.begin(); it != q.end(); ++it) {
            if (it->id == prev_id) { q.erase(it); break; }
        }
        kernel.InstallIntcHandlers(q, kernel.NextIntcHandlerId());
    };

    if (func == 0) {
        toggle_cause(kSyscallDisableIntc);
        remove_existing();
        if (gparam) {
            memory::write<uint32_t>(gparam + 0x08, 0);
            memory::write<uint32_t>(gparam + 0x0C, 0);
        }
    } else {
        if (prev_fn != 0) {
            toggle_cause(kSyscallDisableIntc);
            remove_existing();
        }
        std::vector<SyscallTable::KernelHandler> q = kernel.IntcHandlers();
        SyscallTable::KernelHandler h;
        h.id = kernel.NextIntcHandlerId();
        h.cause = kIntcVblankStart;
        h.pc = func;
        h.gp = ctx.cpuRegs.GPR.r[28].UL[0];
        h.arg = 0;
        h.flag = 0;
        h.active = true;
        q.push_back(h);   // appended: the tail of the cause's chain
        kernel.InstallIntcHandlers(q, h.id + 1);
        if (gparam) {
            memory::write<uint32_t>(gparam + 0x08, func);
            memory::write<uint32_t>(gparam + 0x0C, static_cast<uint32_t>(h.id));
        }
        toggle_cause(kSyscallEnableIntc);
    }
    ctx.cpuRegs.GPR.r[2].SD[0] = static_cast<int32_t>(prev_fn);
}

}  // namespace gs
}  // namespace ps2r
