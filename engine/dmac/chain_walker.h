#pragma once
// ChainWalker: the DMAtag chain walks and normal-mode transfers of the VIF0,
// VIF1 and GIF channels. Reads tags and data spans from guest RAM / scratchpad
// and feeds them to the VIF command processor (VIF0/1) or submits ONE PATH3
// packet per chain to the GIF arbiter (GIF). Owns no channel state: the caller
// passes the channel's register file and completes the channel afterwards.
//
// PS2 rules: a DMAtag is one quadword {QWC, PCE, ID, IRQ | ADDR, SPR}; CNT/NEXT/
// CALL/RET/END take their data from the quadwords after the tag, REFE/REF/REFS
// from ADDR; CALL pushes the return address into ASR0/ASR1 (ASP), RET pops;
// REFE/END end the chain, IRQ ends it only with CHCR.TIE; TTE feeds the tag's
// upper two words as one quadword before the data; the SPR bit / the
// 0x70000000 prefix route a fetch to the scratchpad. The VIF and GIF walks keep
// their own terminator and scratchpad rules (they are transcribed separately).
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "engine/dmac/dmac_channel.h"

class MainMemory;
class Scratchpad;
class Vif;
class GifArbiter;

// Outcome of a VIF chain walk. `completed` false = the walk returned early
// (FDR set, mode not chain, VIF1 TADR 0): STR cleared, no interrupt status.
// The END-slot fields describe the last tag decoded, for the VIF1 tail record.
struct DmacVifWalkResult {
    bool completed = false;
    int tag_count = 0;
    uint32_t last_id = 0xFF;      // id of the last tag decoded (0xFF = none)
    uint64_t last_tag_lo = 0;     // low word of the last tag read (terminator included)
    uint32_t last_tag_tadr = 0;   // masked address that tag was read from
    bool last_tag_from_spr = false;
};

class ChainWalker {
public:
    static constexpr int kMaxTags = 10000;
    static constexpr uint32_t kRamMask = 0x01FFFFFFu;    // VIF walk: 25-bit RAM window
    static constexpr uint32_t kTadrMask = 0x0FFFFFFFu;   // VIF walk: TADR without the SPR prefix

    // The MTVU staged feed for VIF1 (stage 5G installs it): every span and TTE
    // quadword goes to `feed`, `flush` runs once at every walk exit, `reset`
    // replaces the latch reset. Null = the inline Vif feed.
    using StagedFeed = void (*)(void* user, const uint8_t* data, size_t size, bool tte);
    using StagedFlush = void (*)(void* user);
    using StagedResetLatch = void (*)(void* user);

    ChainWalker(MainMemory& ram, Scratchpad& spad, Vif& vif, GifArbiter& arbiter);

    void SetVif1StagedFeed(StagedFeed feed, StagedFlush flush, StagedResetLatch reset, void* user);
    bool HasVif1StagedFeed() const { return staged_feed_ != nullptr; }
    // Migration seam: a library's owned item can follow an existing producer
    // span without manufacturing a DMA/VIF command. The observer runs after
    // this tag's input was fed, before the next tag. Address is physical RAM
    // or a scratchpad offset, distinguished by the second argument.
    // Third argument identifies the first tag of this walk, so consumers can
    // distinguish sealed submissions and resumed tails that reuse addresses.
    using TagObserver = std::function<void(uint32_t, bool, bool)>;
    void SetVif1TagObserver(TagObserver observer, std::function<void()> end = {});
    void FlushVif1StagedFeed();

    // VIF0/VIF1 chain-mode walk over `regs` (CHCR.STR set by the caller).
    DmacVifWalkResult WalkVifChain(int ch, DmacChannelRegs& regs);
    // VIF0/VIF1 normal mode: QWC quadwords from MADR (RAM only), MADR advanced, QWC 0.
    void VifNormal(int ch, DmacChannelRegs& regs);
    // GIF chain-mode walk: one PATH3 submit of every span in tag order. Returns
    // false when CHCR.MOD is not chain (caller completes the channel).
    bool WalkGifChain(DmacChannelRegs& regs);
    // GIF normal mode: one PATH3 submit of QWC quadwords from MADR; nothing when QWC is 0.
    void GifNormal(DmacChannelRegs& regs);

    // Guest reads shared with the tail chase: 8 bytes at a VIF-walk tag
    // address (masked RAM / scratchpad), 0 when out of range.
    uint64_t ReadTagWord(uint32_t tadr, bool from_spr) const;
    // A full guest address for `bytes`: the scratchpad prefix routes to the
    // scratchpad, everything else to RAM through the physical mask; nullptr
    // when the span is not entirely inside. Shared with the IPU/SIF channel bodies.
    uint8_t* Translate(uint32_t addr, size_t bytes) const;

private:
    const uint8_t* VifSpan(uint32_t addr, bool from_spr, size_t bytes) const;
    void FeedVif(int ch, const uint8_t* data, size_t bytes, bool tte);
    void FlushStaged(int ch);

    MainMemory& ram_;
    Scratchpad& spad_;
    Vif& vif_;
    GifArbiter& arbiter_;
    StagedFeed staged_feed_ = nullptr;
    StagedFlush staged_flush_ = nullptr;
    StagedResetLatch staged_reset_ = nullptr;
    void* staged_user_ = nullptr;
    TagObserver tag_observer_;
    std::function<void()> walk_end_observer_;
};
