#include "engine/dmac/dmac_spr_transfer.h"

#include <algorithm>
#include <cstring>

#include "engine/core/log/log.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/scratchpad.h"

namespace {
constexpr uint32_t kMadrMask = 0x7FFFFFF0u;  // SPR is fixed to zero on ch8/ch9.
constexpr uint32_t kSadrMask = 0x3FF0u;
constexpr uint32_t kIrq = 1u << 31;
constexpr uint32_t kPceMask = 3u << 26;

bool SupportedTag(uint32_t id) {
    return id == DmacTagId::kRefe || id == DmacTagId::kCnt || id == DmacTagId::kNext ||
           id == DmacTagId::kRef || id == DmacTagId::kEnd;
}

bool EndsPacket(uint32_t tag, uint32_t chcr) {
    const uint32_t id = (tag >> 28) & 7u;
    return id == DmacTagId::kRefe || id == DmacTagId::kEnd || ((tag & kIrq) && (chcr & DmacChcr::kTie));
}

bool VuMemory(uint32_t addr) {
    // Normal-mode transfers to VU memory are legal hardware requests, but this
    // RAM-only implementation cannot service them. Do not report a bus error.
    return addr >= 0x11000000u && addr < 0x11010000u;
}
}  // namespace

DmacSprTransfer::DmacSprTransfer(MainMemory& ram, Scratchpad& spad) : ram_(ram), spad_(spad) {}

void DmacSprTransfer::ObserveCtrlWrite(uint32_t ctrl) {
    if (!(ctrl & 0xCu) && ((ctrl >> 4) & 3u) != 2u) return;
    ++stats_.unsupported_spr_ctrl_writes;
    if (reported_ctrl_ || !PS2R_LOG_ON(DMAC, Error)) return;
    reported_ctrl_ = true;
    PS2R_LOG(DMAC, Error, "SPR_FROM unsupported D_CTRL write CTRL=%08x reason=mfifo-or-stall-source "
                         "(control write ignored; subsequent starts cannot validate this configuration)", ctrl);
    ps2r::log::Logger::Instance().Flush();
}

DmacSprResult DmacSprTransfer::Run(bool to_spr, DmacChannelRegs& regs, uint32_t ctrl) {
    const DmacChannelRegs requested = regs;
    const uint32_t mode = DmacChcr::Mode(regs.chcr);
    auto& count = to_spr ? stats_.spr_to[mode] : stats_.spr_from[mode];
    ++count.starts;
    DmacSprResult result = Reject(DmacSprResult::Unsupported, RejectReason::Mode);
    // MFIFO and fromSPR stall-source operation need coordination with drain
    // channels. Never treat them as an ordinary copy.
    const bool special_from = !to_spr && ((ctrl & 0xCu) || ((ctrl >> 4) & 3u) == 2u);
    if (special_from) {
        result = Reject(DmacSprResult::Unsupported, RejectReason::MfifoOrStall);
    } else if (mode == DmacChcr::kModNormal && (regs.qwc & 0xFFFFu) == 0) {
        result = Reject(DmacSprResult::Unsupported, RejectReason::ZeroQwc);
    } else if (mode == DmacChcr::kModNormal) {
        result = Copy(to_spr, regs, count);
    } else if (to_spr && mode == DmacChcr::kModChain) {
        result = SourceChain(regs, count);
    }
    if (result == DmacSprResult::Complete) {
        ++count.completed;
    } else {
        ++count.rejected;
        if (result == DmacSprResult::Unsupported) ++count.unsupported;
        else ++count.faults;
        LogRejection(to_spr, requested, regs, ctrl);
    }
    return result;
}

DmacSprResult DmacSprTransfer::Reject(DmacSprResult result, RejectReason reason) {
    reason_ = reason;
    return result;
}

void DmacSprTransfer::LogRejection(bool to_spr, const DmacChannelRegs& requested,
                                   const DmacChannelRegs& stopped, uint32_t ctrl) {
    const uint32_t mode = DmacChcr::Mode(requested.chcr);
    auto& reported = reported_[(to_spr ? 4 : 0) + mode];
    const uint16_t bit = 1u << static_cast<unsigned>(reason_);
    if ((reported & bit) || !PS2R_LOG_ON(DMAC, Error)) return;
    reported |= bit;
    static constexpr const char* names[] = {
        "unsupported-mode", "normal-qwc-zero", "mfifo-or-stall-source", "vu-memory",
        "scratchpad-chain-source", "tag-priority-control", "chain-walk-limit", "bus-address", "invalid-tag"
    };
    PS2R_LOG(DMAC, Error,
             "SPR_%s rejected MOD=%u CTRL=%08x CHCR=%08x MADR=%08x SADR=%08x QWC=%08x TADR=%08x "
             "reason=%s stopped_MADR=%08x stopped_SADR=%08x stopped_QWC=%08x stopped_TADR=%08x",
             to_spr ? "TO" : "FROM", mode, ctrl, requested.chcr, requested.madr, requested.sadr,
             requested.qwc, requested.tadr, names[static_cast<unsigned>(reason_)],
             stopped.madr, stopped.sadr, stopped.qwc, stopped.tadr);
    ps2r::log::Logger::Instance().Flush();  // Preserve evidence before a polling loop/watchdog kill.
}

DmacSprResult DmacSprTransfer::Copy(bool to_spr, DmacChannelRegs& regs, DmacTransferCount& count) {
    regs.madr &= kMadrMask;
    regs.sadr &= kSadrMask;
    regs.qwc &= 0xFFFFu;
    while (regs.qwc != 0) {
        if (VuMemory(regs.madr)) return Reject(DmacSprResult::Unsupported, RejectReason::VuMemory);
        // Copy a whole span up to either memory boundary. The scratchpad wraps;
        // main RAM does not. Retain outstanding QWC/MADR on a bus error.
        if (regs.madr >= MainMemory::kSize) return Reject(DmacSprResult::BusError, RejectReason::BusAddress);
        const uint32_t qwords = std::min({regs.qwc, (Scratchpad::kSize - regs.sadr) / 16u,
                                         (MainMemory::kSize - regs.madr) / 16u});
        const uint32_t bytes = qwords * 16u;
        uint8_t* ram = ram_.Pointer(regs.madr, bytes);
        uint8_t* spr = spad_.Pointer(regs.sadr, bytes);
        if (to_spr) std::memcpy(spr, ram, bytes);
        else std::memcpy(ram, spr, bytes);
        regs.madr += bytes;
        regs.sadr = (regs.sadr + bytes) & kSadrMask;
        regs.qwc -= qwords;
        count.quadwords += qwords;
    }
    return DmacSprResult::Complete;
}

DmacSprResult DmacSprTransfer::SourceChain(DmacChannelRegs& regs, DmacTransferCount& count) {
    // A resumed packet drains its remaining data before fetching the next tag.
    // A fresh chain is started with QWC=0; its previous TAG is immaterial.
    if ((regs.qwc & 0xFFFFu) != 0) {
        if (!SupportedTag((regs.chcr >> 28) & 7u)) return Reject(DmacSprResult::InvalidTag, RejectReason::InvalidTag);
        if (regs.chcr & kPceMask) return Reject(DmacSprResult::Unsupported, RejectReason::PriorityControl);
        const auto result = Copy(true, regs, count);
        if (result != DmacSprResult::Complete) return result;
        if (EndsPacket(regs.chcr, regs.chcr)) return DmacSprResult::Complete;
    }
    for (uint32_t tags = 0; tags < kMaxTags; ++tags) {
        // Ch9 has no scratchpad source or address stack (manual sections 5.1/5.9).
        if (regs.tadr & 0x80000000u) return Reject(DmacSprResult::Unsupported, RejectReason::ScratchpadSource);
        regs.tadr &= ~0xFu;
        const uint8_t* tag = ram_.Pointer(regs.tadr, 16);
        if (!tag) return Reject(DmacSprResult::BusError, RejectReason::BusAddress);
        uint32_t word = 0, address = 0;
        std::memcpy(&word, tag, 4);
        std::memcpy(&address, tag + 4, 4);
        const uint32_t id = (word >> 28) & 7u;
        regs.chcr = (regs.chcr & ~DmacChcr::kTagMask) | (word & DmacChcr::kTagMask);
        if (!SupportedTag(id)) return Reject(DmacSprResult::InvalidTag, RejectReason::InvalidTag);
        if (word & kPceMask) return Reject(DmacSprResult::Unsupported, RejectReason::PriorityControl);
        const bool reference = id == DmacTagId::kRef || id == DmacTagId::kRefe;
        if ((reference || id == DmacTagId::kNext) && (address & 0x80000000u))
            return Reject(DmacSprResult::Unsupported, RejectReason::ScratchpadSource);
        const uint32_t after_tag = regs.tadr + 16u;
        const uint32_t qwc = word & 0xFFFFu;
        // TTE on a burst channel sends the entire 128-bit tag before its payload.
        if (regs.chcr & DmacChcr::kTte) {
            regs.sadr &= kSadrMask;
            std::memcpy(spad_.Pointer(regs.sadr, 16), tag, 16);
            regs.sadr = (regs.sadr + 16u) & kSadrMask;
            ++count.quadwords;
        }
        regs.madr = reference ? (address & kMadrMask) : after_tag;
        regs.qwc = qwc;
        // REFE/END have no next tag. Keep TADR at the terminal tag; the manual
        // specifies a successor only for continuing packets and tag interrupts.
        if (id == DmacTagId::kCnt) regs.tadr = after_tag + qwc * 16u;
        else if (id == DmacTagId::kNext) regs.tadr = address & kMadrMask;
        else if (id == DmacTagId::kRef) regs.tadr = after_tag;
        const auto result = Copy(true, regs, count);
        if (result != DmacSprResult::Complete) return result;
        if (EndsPacket(word, regs.chcr)) return DmacSprResult::Complete;
    }
    // A malformed/cyclic chain must not monopolize the host. This is a runtime
    // limit, not a hardware completion; STR remains set and census records it.
    return Reject(DmacSprResult::Unsupported, RejectReason::WalkLimit);
}
