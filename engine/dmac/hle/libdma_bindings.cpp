// LibdmaBindings implementation; the row table and the register rules are in
// the header.
#include "engine/dmac/hle/libdma_bindings.h"

#include <cstring>

#include "engine/dmac/dmac.h"
#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/scratchpad.h"

namespace {

LibdmaBindings* g_self = nullptr;
uint64_t g_reset_calls = 0, g_put_env_calls = 0, g_get_chan_calls = 0;
uint64_t g_sync_calls = 0, g_gif_sync_calls = 0;

// The manifest handler is a plain function pointer: thunks over the bound instance.
void ThunkMemclr(CpuContext& ctx) { g_self->Memclr(ctx); }
void ThunkGetChan(CpuContext& ctx) { g_self->SceDmaGetChan(ctx); }
void ThunkReset(CpuContext& ctx) { g_self->SceDmaReset(ctx); }
void ThunkPutEnv(CpuContext& ctx) { g_self->SceDmaPutEnv(ctx); }
void ThunkSync(CpuContext& ctx) { g_self->SceDmaSync(ctx); }
void ThunkGifSync(CpuContext& ctx) { g_self->SceGifSyncDma(ctx); }

using Handler = void (*)(CpuContext&);
struct Row {
    const char* name;
    Handler handler;
};
const Row kRows[] = {
    {"memclr", &ThunkMemclr},
    {"sceDmaGetChan", &ThunkGetChan},
    {"sceDmaReset", &ThunkReset},
    {"sceDmaPutEnv", &ThunkPutEnv},
    {"sceDmaSync", &ThunkSync},
    {"sceGifSyncDma", &ThunkGifSync},
};
constexpr size_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);
const char* g_row_names[kRowCount];

// SDK channel order: VIF0, VIF1, GIF, fromIPU, toIPU, SIF0, SIF1, SIF2, fromSPR, toSPR.
constexpr uint32_t kChannelBase[LibdmaBindings::kChannelCount] = {
    DmacAddr::kVif0, DmacAddr::kVif1, DmacAddr::kGif,     DmacAddr::kIpuFrom, DmacAddr::kIpuTo,
    DmacAddr::kSif0, DmacAddr::kSif1, DmacAddr::kSif2,    DmacAddr::kSprFrom, DmacAddr::kSprTo,
};
// D_CTRL field encodings by channel id (the hardware's; a channel that cannot
// take the role encodes 0 = none).
constexpr uint8_t kStsCode[LibdmaBindings::kChannelCount] = {0, 0, 0, 3, 0, 1, 0, 0, 2, 0};  // SIF0=1 fromSPR=2 fromIPU=3
constexpr uint8_t kStdCode[LibdmaBindings::kChannelCount] = {0, 1, 2, 0, 0, 0, 3, 0, 0, 0};  // VIF1=1 GIF=2 SIF1=3
constexpr uint8_t kMfdCode[LibdmaBindings::kChannelCount] = {0, 2, 3, 0, 0, 0, 0, 0, 0, 0};  // VIF1=2 GIF=3

// The kernel owns the SIF channels; the library's reset leaves them alone.
bool IsSifChannel(int ch) {
    return ch == static_cast<int>(DmacChannelId::Sif0) || ch == static_cast<int>(DmacChannelId::Sif1) ||
           ch == static_cast<int>(DmacChannelId::Sif2);
}

uint32_t A(const CpuContext& ctx, int i) { return ctx.cpuRegs.GPR.r[4 + i].UL[0]; }
void Ret(CpuContext& ctx, int32_t v) { ctx.cpuRegs.GPR.r[2].SD[0] = v; }

}  // namespace

LibdmaBindings::LibdmaBindings(Dmac& dmac, MainMemory& ram, Scratchpad& spad)
    : dmac_(dmac), ram_(ram), spad_(spad) {}

uint32_t LibdmaBindings::ChannelBase(int id) { return id >= 0 && id < kChannelCount ? kChannelBase[id] : 0u; }

int LibdmaBindings::Bind(SdkBindingManifest& m, LibdmaBindings* self) {
    g_self = self;
    int n = 0;
    for (const Row& r : kRows) {
        m.Register("libdma.a", r.name, r.handler);
        ++n;
    }
    return n;
}

const char* const* LibdmaBindings::RowNames(size_t* count) {
    for (size_t i = 0; i < kRowCount; i++) g_row_names[i] = kRows[i].name;
    *count = kRowCount;
    return g_row_names;
}

uint64_t LibdmaBindings::ResetCalls() { return g_reset_calls; }
uint64_t LibdmaBindings::PutEnvCalls() { return g_put_env_calls; }
uint64_t LibdmaBindings::GetChanCalls() { return g_get_chan_calls; }
uint64_t LibdmaBindings::SyncCalls() { return g_sync_calls; }
uint64_t LibdmaBindings::GifSyncCalls() { return g_gif_sync_calls; }

// ---- guest memory ------------------------------------------------------------

uint32_t LibdmaBindings::Reg(uint32_t addr) { return static_cast<uint32_t>(dmac_.Read(addr, 4)); }
void LibdmaBindings::SetReg(uint32_t addr, uint32_t value) { dmac_.Write(addr, 4, value); }

// A byte of RAM (any KUSEG/KSEG0/KSEG1 mirror) or scratchpad; 0 outside both.
uint8_t LibdmaBindings::GuestByte(uint32_t addr) {
    if (Scratchpad::Contains(addr)) {
        const uint8_t* p = spad_.Pointer(Scratchpad::Offset(addr), 1);
        return p ? *p : 0;
    }
    const uint8_t* p = ram_.Pointer(MainMemory::Phys(addr), 1);
    return p ? *p : 0;
}

// Byte-granular zero fill: each destination byte lands in RAM or in the
// scratchpad; one outside both is dropped and counted.
void LibdmaBindings::ZeroGuest(uint32_t addr, uint32_t bytes) {
    for (uint32_t i = 0; i < bytes; ++i) {
        const uint32_t a = addr + i;
        uint8_t* p = Scratchpad::Contains(a) ? spad_.Pointer(Scratchpad::Offset(a), 1)
                                             : ram_.Pointer(MainMemory::Phys(a), 1);
        if (p) *p = 0;
        else ++dropped_stores_;
    }
}

// ---- rows ---------------------------------------------------------------------

void LibdmaBindings::SceDmaGetChan(CpuContext& ctx) {
    ++g_get_chan_calls;
    const uint32_t id = A(ctx, 0);
    Ret(ctx, static_cast<int32_t>(id < static_cast<uint32_t>(kChannelCount) ? kChannelBase[id] : 0u));
}

void LibdmaBindings::Memclr(CpuContext& ctx) {
    const uint32_t dst = A(ctx, 0), n = A(ctx, 1);
    ZeroGuest(dst, n);
    Ret(ctx, 0);
}

void LibdmaBindings::ApplyEnv(uint8_t sts, uint8_t std_ch, uint8_t mfd, uint8_t rcyc, uint32_t pcr, uint32_t sqwc,
                              uint32_t rbor, uint32_t rbsr) {
    uint32_t ctrl = Reg(DmacAddr::kCtrl);
    ctrl = (ctrl & ~0x30u) | (static_cast<uint32_t>(kStsCode[sts]) << 4);
    ctrl = (ctrl & ~0xC0u) | (static_cast<uint32_t>(kStdCode[std_ch]) << 6);
    ctrl = (ctrl & ~0x0Cu) | (static_cast<uint32_t>(kMfdCode[mfd]) << 2);
    if (rcyc != 0) ctrl = ((ctrl | kCtrlRele) & ~0x300u) | (static_cast<uint32_t>(rcyc - 1) << 8);
    else ctrl &= ~kCtrlRele;
    SetReg(DmacAddr::kCtrl, ctrl);
    SetReg(DmacAddr::kPcr, pcr);
    SetReg(DmacAddr::kSqwc, sqwc);
    SetReg(DmacAddr::kRbor, rbor);
    SetReg(DmacAddr::kRbsr, rbsr);
}

int32_t LibdmaBindings::PutEnvAt(uint32_t env) {
    const uint8_t sts = GuestByte(env), std_ch = GuestByte(env + 1);
    const uint8_t mfd = GuestByte(env + 2), rcyc = GuestByte(env + 3);
    if (sts >= kChannelCount) return -1;
    if (std_ch >= kChannelCount) return -2;
    if (mfd >= kChannelCount) return -3;
    if (rcyc >= 7) return -4;
    auto u16 = [this](uint32_t a) {
        return static_cast<uint32_t>(GuestByte(a)) | (static_cast<uint32_t>(GuestByte(a + 1)) << 8);
    };
    auto u32 = [&u16](uint32_t a) { return u16(a) | (u16(a + 2) << 16); };
    const uint32_t pcr = (u16(env + 4) << 16) | u16(env + 6);
    const uint32_t sqwc = (u16(env + 10) << 16) | u16(env + 8);
    ApplyEnv(sts, std_ch, mfd, rcyc, pcr, sqwc, u32(env + 12), u32(env + 16));
    return 0;
}

void LibdmaBindings::SceDmaPutEnv(CpuContext& ctx) {
    ++g_put_env_calls;
    Ret(ctx, PutEnvAt(A(ctx, 0)));
}

void LibdmaBindings::SceDmaReset(CpuContext& ctx) {
    ++g_reset_calls;
    const uint32_t mode = A(ctx, 0);
    const uint32_t dmae_at_entry = Reg(DmacAddr::kCtrl) & kCtrlDmae;
    for (int ch = 0; ch < kChannelCount; ch++) {
        if (IsSifChannel(ch)) continue;
        const uint32_t base = kChannelBase[ch];
        SetReg(base + DmacRegOffset::kSadr, 0);
        SetReg(base + DmacRegOffset::kChcr, 0);
        SetReg(base + DmacRegOffset::kTadr, 0);
        SetReg(base + DmacRegOffset::kMadr, 0);
        SetReg(base + DmacRegOffset::kAsr1, 0);
        SetReg(base + DmacRegOffset::kAsr0, 0);
    }
    SetReg(DmacAddr::kStat, kStatClearMask);
    SetReg(DmacAddr::kStat, Reg(DmacAddr::kStat) & kStatToggleMask);
    ApplyEnv(0, 0, 0, 0, 0, 0, 0, 0);
    if (mode == 1) SetReg(DmacAddr::kCtrl, Reg(DmacAddr::kCtrl) | kCtrlDmae);
    Ret(ctx, static_cast<int32_t>(dmae_at_entry));
}

// ---- sync seams ----------------------------------------------------------------
// sceDmaSync(chan, mode, timeout) returns 0 once the channel (all channels for a
// null chan) reads idle and -1 on timeout; here every transfer completes in-line
// once the parked VIF1 kick is run, so the wait never times out. The MTVU
// barrier (`MtvuSync::Sync(SceDmaSync)`) is the injected hook's, run before
// the flush; without one (the deterministic profile) the seam is exactly the
// flush.

void LibdmaBindings::SetSyncHook(SyncHook fn, void* user) {
    sync_hook_ = fn;
    sync_hook_user_ = user;
}

void LibdmaBindings::SceDmaSync(CpuContext& ctx) {
    ++g_sync_calls;
    if (sync_hook_) sync_hook_(sync_hook_user_, false);
    dmac_.FlushDeferredVif1();
    Ret(ctx, 0);
}

void LibdmaBindings::SceGifSyncDma(CpuContext& ctx) {
    ++g_gif_sync_calls;
    if (sync_hook_) sync_hook_(sync_hook_user_, true);
    dmac_.FlushDeferredVif1();
    Ret(ctx, 0);
}
