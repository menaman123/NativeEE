#include "engine/ipu/hle/libipu_bindings.h"

#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ipu/hle/guest_memory.h"
#include "engine/ipu/hle/libipu_api.h"

namespace ps2r::ipu {

namespace {

LibipuBindings* g_self = nullptr;

uint32_t A0(const CpuContext& ctx) { return ctx.cpuRegs.GPR.r[4].UL[0]; }
void Ret(CpuContext& ctx, int32_t v) { ctx.cpuRegs.GPR.r[2].SD[0] = v; }

void ThunkInit(CpuContext& ctx) { if (g_self) g_self->SceIpuInit(ctx); }
void ThunkSync(CpuContext& ctx) { if (g_self) g_self->SceIpuSync(ctx); }
void ThunkStop(CpuContext& ctx) { if (g_self) g_self->SceIpuStopDma(ctx); }
void ThunkRestart(CpuContext& ctx) { if (g_self) g_self->SceIpuRestartDma(ctx); }

// The nine-word save buffer in the SDK's order (LibipuDmaSnapshot's field order).
void StoreSnapshot(GuestMemory& mem, uint32_t save, const LibipuDmaSnapshot& s) {
    const uint32_t words[LibipuDmaSnapshot::kWords] = {
        s.ipu_to_madr, s.ipu_to_tadr, s.ipu_to_qwc, s.ipu_to_chcr,
        s.ipu_from_madr, s.ipu_from_qwc, s.ipu_from_chcr, s.ipu_bp, s.ipu_ctrl};
    for (uint32_t i = 0; i < LibipuDmaSnapshot::kWords; ++i) mem.Write32(save + 4 * i, words[i]);
}

LibipuDmaSnapshot LoadSnapshot(GuestMemory& mem, uint32_t save) {
    LibipuDmaSnapshot s;
    s.ipu_to_madr = mem.Read32(save + 0x00);
    s.ipu_to_tadr = mem.Read32(save + 0x04);
    s.ipu_to_qwc = mem.Read32(save + 0x08);
    s.ipu_to_chcr = mem.Read32(save + 0x0C);
    s.ipu_from_madr = mem.Read32(save + 0x10);
    s.ipu_from_qwc = mem.Read32(save + 0x14);
    s.ipu_from_chcr = mem.Read32(save + 0x18);
    s.ipu_bp = mem.Read32(save + 0x1C);
    s.ipu_ctrl = mem.Read32(save + 0x20);
    return s;
}

}  // namespace

LibipuBindings::LibipuBindings(LibipuApi& api, GuestMemory& memory) : api_(api), memory_(memory) {}

int LibipuBindings::Bind(SdkBindingManifest& m, LibipuBindings* self) {
    g_self = self;
    m.Register(kLibrary, "sceIpuInit", &ThunkInit);
    m.Register(kLibrary, "sceIpuSync", &ThunkSync);
    m.Register(kLibrary, "sceIpuStopDMA", &ThunkStop);
    m.Register(kLibrary, "sceIpuRestartDMA", &ThunkRestart);
    return 4;
}

void LibipuBindings::SceIpuInit(CpuContext&) {
    ++init_calls_;
    api_.Init();
}

void LibipuBindings::SceIpuSync(CpuContext& ctx) {
    ++sync_calls_;
    Ret(ctx, api_.Sync(static_cast<int32_t>(A0(ctx))));
}

void LibipuBindings::SceIpuStopDma(CpuContext& ctx) {
    ++stop_calls_;
    const uint32_t save = A0(ctx);
    if (save == 0) return;
    LibipuDmaSnapshot snap;
    api_.StopDma(snap);
    StoreSnapshot(memory_, save, snap);
}

void LibipuBindings::SceIpuRestartDma(CpuContext& ctx) {
    ++restart_calls_;
    const uint32_t save = A0(ctx);
    if (save == 0) return;
    api_.RestartDma(LoadSnapshot(memory_, save));
}

}  // namespace ps2r::ipu
