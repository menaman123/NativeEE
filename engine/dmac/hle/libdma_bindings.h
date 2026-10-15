#pragma once
// LibdmaBindings: the sce libdma.a rows (sceDmaGetChan, sceDmaPutEnv,
// sceDmaReset, memclr) as an instance over the Dmac, plus the two DMA-idle
// sync seams (sceDmaSync, sceGifSyncDma). All six rows bind into the
// SdkBindingManifest (library "libdma.a"; the addresses are the title's
// sdk_bindings.tsv) through `Bind`, which installs file-static thunks over the
// one instance -- the manifest handler is a plain function pointer. The sync
// seams are also the bodies behind the generated TU's `hle_sceDmaSync` /
// `hle_sceGifSyncDma` names (the title glue forwards to the registered rows).
//
// SDK rules (the retail library bodies, instruction for instruction):
//   sceDmaGetChan(id)   -> the channel's REGISTER BLOCK: the SDK's sceDmaChan
//                          is a view of the hardware (CHCR +0x00, MADR +0x10,
//                          QWC +0x20, TADR +0x30, ASR0 +0x40, ASR1 +0x50,
//                          SADR +0x80) at 0x10008000 (VIF0), 0x10009000 (VIF1),
//                          0x1000A000 (GIF), 0x1000B000 (fromIPU), 0x1000B400
//                          (toIPU), 0x1000C000 (SIF0), 0x1000C400 (SIF1),
//                          0x1000C800 (SIF2), 0x1000D000 (fromSPR), 0x1000D400
//                          (toSPR); 0 for id >= 10. No descriptor lives in RAM.
//   sceDmaReset(mode)   -> every channel the library owns (all but the kernel's
//                          SIF0/SIF1/SIF2) gets SADR, CHCR, TADR, MADR, ASR1,
//                          ASR0 = 0 in that store order (QWC untouched);
//                          D_STAT <- 0xFF1F (clears CIS0-4, CIS8-9, SIS, MEIS,
//                          BEIS -- the SIF channels' CIS5-7 are the kernel's),
//                          then D_STAT <- D_STAT & 0xFF1F0000 (toggles those
//                          channels' set mask bits off); an all-zero sceDmaEnv
//                          is put (D_CTRL STS/STD/MFD/RELE cleared, RCYC kept,
//                          D_PCR, D_SQWC, D_RBOR, D_RBSR = 0); mode 1 then sets
//                          D_CTRL.DMAE. Returns the DMAE bit read at entry.
//   sceDmaPutEnv(env)   -> sceDmaEnv {u8 sts, u8 std, u8 mfd, u8 rcyc,
//                          u16 pcr_hi, u16 pcr_lo, u16 sqwc_lo, u16 sqwc_hi,
//                          u32 rbor, u32 rbsr} (20 bytes): the three channel
//                          ids become D_CTRL.STS (SIF0=1, fromSPR=2,
//                          fromIPU=3), .STD (VIF1=1, GIF=2, SIF1=3) and .MFD
//                          (VIF1=2, GIF=3); rcyc 0 clears RELE, 1..6 sets RELE
//                          with RCYC = rcyc-1; D_PCR = pcr_hi<<16 | pcr_lo,
//                          D_SQWC = sqwc_hi<<16 | sqwc_lo, D_RBOR, D_RBSR.
//                          Returns 0; -1/-2/-3 for a channel id >= 10 (sts/
//                          std/mfd), -4 for rcyc >= 7 -- nothing written then.
//   memclr(dst, n)      -> n zero bytes at dst through guest memory: RAM
//                          (KUSEG/KSEG0/KSEG1 mirrors) and the 16 KB scratchpad
//                          (0x70000000 window). A destination outside both is
//                          dropped and counted (DroppedStores), never silently.
//   sceDmaSync / sceGifSyncDma wait until every channel (respectively the GIF
//                          path) is idle: here the deferred VIF1 kick's
//                          observation point (FlushDeferredVif1), then 0 (the
//                          SDK's idle result; -1 = timeout never happens, every
//                          transfer completes in-line once run). An injected
//                          sync hook (SetSyncHook) runs FIRST: the composition
//                          root's MTVU barrier for the site (wait for the VU1
//                          worker, or note the skip while boundaries are
//                          deferred); absent, the row is exactly the flush.
//
// Register accesses go through the Dmac's MMIO face (Read / Write, 4 bytes), so
// the observable is exactly a guest store's. The Dmac register page decides
// what lands: D_PCR is the one writable global; D_CTRL / D_STAT / D_SQWC /
// D_RBSR / D_RBOR / D_STADR writes are dropped, and so are the IPU TO, SIF1,
// SIF2 and SPR TO channel pages. The bodies still issue every store the SDK
// does; a dropped store reads back unchanged (so DMAE never latches through
// sceDmaReset and its return stays the page's D_CTRL bit 0).
#include <cstddef>
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"

class Dmac;
class MainMemory;
class Scratchpad;
class SdkBindingManifest;

class LibdmaBindings {
public:
    static constexpr int kChannelCount = 10;
    static constexpr uint32_t kEnvBytes = 20;
    static constexpr uint32_t kStatClearMask = 0xFF1Fu;       // CIS0-4, CIS8-9, SIS, MEIS, BEIS (not the SIF channels')
    static constexpr uint32_t kStatToggleMask = 0xFF1F0000u;  // CIM0-4, CIM8-9, SIM, MEIM
    static constexpr uint32_t kCtrlDmae = 1u;
    static constexpr uint32_t kCtrlRele = 2u;

    LibdmaBindings(Dmac& dmac, MainMemory& ram, Scratchpad& spad);

    // The channel register block address, SDK channel order (VIF0 .. toSPR); 0 outside 0..9.
    static uint32_t ChannelBase(int id);

    // Registers the six libdma.a rows as thunks over `self`; returns the number registered.
    static int Bind(SdkBindingManifest& m, LibdmaBindings* self);
    static const char* const* RowNames(size_t* count);

    // libdma.a rows ($a0.. arguments, result in $v0 sign-extended from 32 bits).
    void SceDmaGetChan(CpuContext& ctx);   // (int channel) -> register block | 0
    void SceDmaPutEnv(CpuContext& ctx);    // (sceDmaEnv*) -> 0 | -1..-4
    void SceDmaReset(CpuContext& ctx);     // (int mode) -> DMAE bit at entry
    void Memclr(CpuContext& ctx);          // (void* dst, size_t n) -> 0

    // The DMA-idle sync seams (manifest rows; the title's hle_sce*Sync glue forwards here).
    void SceDmaSync(CpuContext& ctx);      // sync hook, flush the parked VIF1 kick, -> 0
    void SceGifSyncDma(CpuContext& ctx);   // same observation point, -> 0

    // The barrier that runs before the flush in both sync rows; `gif_sync` is
    // true for sceGifSyncDma. Null = none (the deterministic path).
    using SyncHook = void (*)(void* user, bool gif_sync);
    void SetSyncHook(SyncHook fn, void* user);

    // The env body over a guest pointer (validates, then ApplyEnv).
    int32_t PutEnvAt(uint32_t env);
    // The decoded env onto the register page (SceDmaReset applies an all-zero env).
    void ApplyEnv(uint8_t sts, uint8_t std_ch, uint8_t mfd, uint8_t rcyc, uint32_t pcr, uint32_t sqwc,
                  uint32_t rbor, uint32_t rbsr);

    // Memclr bytes whose destination was neither RAM nor scratchpad.
    uint64_t DroppedStores() const { return dropped_stores_; }

    // Evidence counters for run.meta.
    static uint64_t ResetCalls();
    static uint64_t PutEnvCalls();
    static uint64_t GetChanCalls();
    static uint64_t SyncCalls();
    static uint64_t GifSyncCalls();

private:
    uint32_t Reg(uint32_t addr);
    void SetReg(uint32_t addr, uint32_t value);
    uint8_t GuestByte(uint32_t addr);
    void ZeroGuest(uint32_t addr, uint32_t bytes);

    Dmac& dmac_;
    MainMemory& ram_;
    Scratchpad& spad_;
    uint64_t dropped_stores_ = 0;
    SyncHook sync_hook_ = nullptr;
    void* sync_hook_user_ = nullptr;
};
