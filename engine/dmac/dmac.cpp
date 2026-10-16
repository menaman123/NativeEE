#include "engine/dmac/dmac.h"

#include <algorithm>
#include <utility>

#include "engine/core/log/log.h"
#include "engine/core/savestate/payload_reader.h"
#include "engine/core/savestate/savestate_format.h"
#include "engine/ee/sched/ee_run_token.h"
#include "engine/gif/gif_arbiter.h"

namespace {

constexpr int kVif0 = static_cast<int>(DmacChannelId::Vif0);
constexpr int kVif1 = static_cast<int>(DmacChannelId::Vif1);
constexpr int kGif = static_cast<int>(DmacChannelId::Gif);
constexpr int kIpuFrom = static_cast<int>(DmacChannelId::IpuFrom);
constexpr int kIpuTo = static_cast<int>(DmacChannelId::IpuTo);
constexpr int kSif0 = static_cast<int>(DmacChannelId::Sif0);
constexpr int kSif1 = static_cast<int>(DmacChannelId::Sif1);
constexpr int kSif2 = static_cast<int>(DmacChannelId::Sif2);
constexpr int kSprFrom = static_cast<int>(DmacChannelId::SprFrom);
constexpr int kSprTo = static_cast<int>(DmacChannelId::SprTo);

}  // namespace

Dmac::Dmac(MainMemory& ram, Scratchpad& spad, Vif& vif, GifArbiter& arbiter)
    : walker_(ram, spad, vif, arbiter), deferred_(walker_, arbiter), arbiter_(arbiter), spr_(ram, spad) {
    deferred_.SetRunner(&Dmac::RunVif1Thunk, this);
}

void Dmac::SetGuestCall(GuestCall fn) { guest_call_ = std::move(fn); }
void Dmac::SetSifCallback(SifCallback fn) { sif_ = std::move(fn); }
void Dmac::SetIpuFeedInput(IpuCallback fn) { ipu_feed_input_ = std::move(fn); }
void Dmac::SetIpuDrainOutput(IpuDrainCallback fn) { ipu_drain_output_ = std::move(fn); }
void Dmac::SetIpuInputRoom(IpuRoomCallback fn) { ipu_input_room_ = std::move(fn); }
void Dmac::SetInterruptRaise(InterruptRaise fn) { raise_ = std::move(fn); }

// ---- MMIO -------------------------------------------------------------------

// Reads map by address RANGE: every offset of a channel's range belongs to it
// (only the seven register offsets return a value, the rest read 0).
int Dmac::ChannelForRead(uint32_t addr, uint32_t* offset) {
    const uint32_t reg = addr & 0xFFFFu;
    struct Range {
        uint32_t lo, hi;
    };
    static constexpr Range kRanges[kDmacChannelCount] = {
        {0x8000u, 0x9000u}, {0x9000u, 0xA000u}, {0xA000u, 0xB000u}, {0xB000u, 0xB400u}, {0xB400u, 0xC000u},
        {0xC000u, 0xC400u}, {0xC400u, 0xC800u}, {0xC800u, 0xD000u}, {0xD000u, 0xD400u}, {0xD400u, 0xE000u},
    };
    for (int ch = 0; ch < kDmacChannelCount; ++ch) {
        if (reg >= kRanges[ch].lo && reg < kRanges[ch].hi) {
            *offset = reg - kRanges[ch].lo;
            return ch;
        }
    }
    return -1;
}

// Writes map by 256-byte page. SIF1 (0xC400) and SIF2 (0xC800)
// remain unmapped; both IPU and scratchpad channel pages are writable.
int Dmac::ChannelForWrite(uint32_t addr) {
    const uint32_t reg = addr & 0xFFFFu;
    switch (reg & 0xFF00u) {
        case 0x8000u: return kVif0;
        case 0x9000u: return kVif1;
        case 0xA000u: return kGif;
        case 0xB000u: return kIpuFrom;
        case 0xB400u: return kIpuTo;
        case 0xC000u: return kSif0;
        case 0xD000u: return kSprFrom;
        case 0xD400u: return kSprTo;
        default: return -1;
    }
}

uint32_t Dmac::Read32(uint32_t addr) {
    const uint32_t reg = addr & 0xFFFFu;
    if (reg >= 0x8000u && reg < 0xE000u) {
        uint32_t offset = 0;
        const int ch = ChannelForRead(addr, &offset);
        if (ch >= 0) {
            // Any VIF1 register read observes the parked kick's completion.
            if (ch == kVif1 && deferred_.Active())
                deferred_.FlushDeferredVif1(ch_[kVif1], DmacDeferredVif1::FlushReason::ChcrRead);
            switch (offset) {
                case DmacRegOffset::kChcr: return ch_[ch].chcr;
                case DmacRegOffset::kMadr: return ch_[ch].madr;
                case DmacRegOffset::kQwc: return ch_[ch].qwc;
                case DmacRegOffset::kTadr: return ch_[ch].tadr;
                case DmacRegOffset::kAsr0: return ch_[ch].asr0;
                case DmacRegOffset::kAsr1: return ch_[ch].asr1;
                case DmacRegOffset::kSadr: return ch_[ch].sadr;
                default: break;
            }
        }
    }
    switch (reg) {
        case 0xE000u: return g_.ctrl;
        case 0xE010u: return g_.stat;
        case 0xE020u: return g_.pcr;
        case 0xE030u: return g_.sqwc;
        case 0xE040u: return g_.rbsr;
        case 0xE050u: return g_.rbor;
        case 0xE060u: return g_.stadr;
        case 0xF520u: return g_.enabler;
        default: return 0;  // 0xF590 (D_ENABLEW) and every unmapped offset read 0
    }
}

void Dmac::Write32(uint32_t addr, uint32_t value) {
    if ((addr & 0xFFFFu) == 0xE010u) {
        // EE manual 5.9 D_STAT: status is W1C, masks toggle on writes of 1.
        // In particular BEIS must remain pending until software acknowledges it.
        const bool was_pending = CheckInterrupt();
        g_.stat &= ~(value & DmacStat::kStatusWriteMask);
        g_.stat ^= value & DmacStat::kMaskWriteMask;
        if (raise_ && !was_pending && CheckInterrupt()) raise_();
        return;
    }
    if ((addr & 0xFFFFu) == 0xE000u) {
        spr_.ObserveCtrlWrite(value);
        return;
    }
    if ((addr & 0xFFFFu) == 0xE020u) {
        // D_PCR: CPC for VIF1 is an observation point of the parked kick.
        if ((value & 0x2u) && deferred_.Active())
            deferred_.FlushDeferredVif1(ch_[kVif1], DmacDeferredVif1::FlushReason::PcrCpc);
        g_.pcr = value;
        return;
    }
    const int ch = ChannelForWrite(addr);
    if (ch < 0) return;  // D_CTRL / D_SQWC / D_RBSR / D_RBOR / D_STADR / D_ENABLEW: dropped
    // The parked kick runs before the new value lands so it is not clobbered.
    if (ch == kVif1 && deferred_.Active())
        deferred_.FlushDeferredVif1(ch_[kVif1], DmacDeferredVif1::FlushReason::RegWrite);
    switch (addr & 0xFFu) {
        case DmacRegOffset::kChcr:
            ch_[ch].chcr = value;
            if (value & DmacChcr::kStr) {
                StartChannel(ch);
                // SPR owns STR; IPU FROM may still be waiting for output and IPU TO for
                // FIFO room (STR stays set with the remaining QWC until the IPU consumes:
                // libmpeg polls D4_QWC / D4_CHCR.STR to tell "in flight" from "done").
                const bool vif1_parked = (ch == kVif1) && deferred_.Active();
                if (!vif1_parked && ch != kSprFrom && ch != kSprTo && ch != kIpuFrom && ch != kIpuTo)
                    ch_[ch].chcr &= ~DmacChcr::kStr;
            }
            break;
        case DmacRegOffset::kMadr: ch_[ch].madr = value; break;
        case DmacRegOffset::kQwc: ch_[ch].qwc = value; break;
        case DmacRegOffset::kTadr: ch_[ch].tadr = value; break;
        case DmacRegOffset::kAsr0: ch_[ch].asr0 = value; break;
        case DmacRegOffset::kAsr1: ch_[ch].asr1 = value; break;
        case DmacRegOffset::kSadr: ch_[ch].sadr = value; break;
        default: break;
    }
}

uint64_t Dmac::Read(uint32_t addr, unsigned size) {
    if (size == 8) {
        const uint64_t lo = Read32(addr);
        const uint64_t hi = Read32(addr + 4);
        return lo | (hi << 32);
    }
    const uint32_t w = Read32(addr & ~3u);
    if (size == 2) return (w >> ((addr & 2u) * 8)) & 0xFFFFu;
    if (size == 1) return (w >> ((addr & 3u) * 8)) & 0xFFu;
    return w;
}

void Dmac::Write(uint32_t addr, unsigned size, uint64_t value) {
    if (size == 8) {
        Write32(addr, static_cast<uint32_t>(value));
        Write32(addr + 4, static_cast<uint32_t>(value >> 32));
        return;
    }
    Write32(addr & ~3u, static_cast<uint32_t>(value));
}

// ---- channels ----------------------------------------------------------------

void Dmac::RunVif1Thunk(void* user) { static_cast<Dmac*>(user)->RunVifChain(kVif1); }

void Dmac::RunVifChain(int ch) {
    DmacChannelRegs& regs = ch_[ch];
    const DmacVifWalkResult res = walker_.WalkVifChain(ch, regs);
    if (!res.completed) return;  // early return: STR already clear, no interrupt status
    if (ch == kVif1) deferred_.RecordTail(res, regs.chcr);
    regs.chcr &= ~DmacChcr::kStr;
    regs.qwc = 0;
    CompleteChannel(ch);
}

void Dmac::StartChannel(int ch) {
    using namespace DmacChcr;
    if (ch < 0 || ch >= kDmacChannelCount) return;
    DmacChannelRegs& regs = ch_[ch];
    const uint32_t mode = Mode(regs.chcr);
    switch (ch) {
        case kVif0:
        case kVif1:
            if (ch == kVif1 && mode == kModChain) {
                if (deferred_.Active())
                    deferred_.FlushDeferredVif1(regs, DmacDeferredVif1::FlushReason::ReKick);
                deferred_.Park(regs);  // STR stays set until an observation point
                break;
            }
            if (mode == kModChain) {
                RunVifChain(ch);
            } else if (mode == kModNormal) {
                walker_.VifNormal(ch, regs);
                CompleteChannel(ch);
            } else {
                CompleteChannel(ch);  // interleave: no transfer modelled
            }
            if (ch == kVif1 && arbiter_.hasPending()) arbiter_.drain();
            break;
        case kGif:
            // A parked VIF1 chain's PATH2 lands before this kick's PATH3.
            if (deferred_.Active()) deferred_.FlushDeferredVif1(ch_[kVif1], DmacDeferredVif1::FlushReason::GifKick);
            if (mode == kModChain) {
                if (walker_.WalkGifChain(regs)) {
                    regs.chcr &= ~kStr;
                    regs.qwc = 0;
                    CompleteChannel(kGif);
                }
            } else if (mode == kModNormal) {
                walker_.GifNormal(regs);  // STR cleared by the CHCR write path; no CIS
            } else {
                CompleteChannel(kGif);
            }
            break;
        case kIpuFrom:
            // FROM IPU: channel 3 copies the output FIFO to memory.
            if (regs.qwc > 0 && ipu_drain_output_) {
                const size_t bytes = static_cast<size_t>(regs.qwc) * 16;
                if (uint8_t* dst = walker_.Translate(regs.madr, bytes)) {
                    const size_t transferred = std::min(bytes, ipu_drain_output_(dst, bytes));
                    const uint32_t qwords = static_cast<uint32_t>(transferred / 16);
                    regs.madr += qwords * 16;
                    regs.qwc -= qwords;
                }
                if (regs.qwc != 0) break;  // no completion until the entire output arrives
            }
            CompleteChannel(ch);
            break;
        case kIpuTo:
            // TO IPU: channel 4 copies memory to the input FIFO -- as much as the FIFO
            // takes when a room callback is bound; the rest stays queued (STR, QWC, MADR)
            // until the IPU consumes and the host restarts the channel.
            if (regs.qwc > 0 && ipu_feed_input_) {
                uint32_t qwords = regs.qwc;
                if (ipu_input_room_) {
                    const size_t room = ipu_input_room_() / 16;
                    if (room < qwords) qwords = static_cast<uint32_t>(room);
                }
                PS2R_LOG(DMAC, Narrate, "dmac-ipu: toIPU chcr=0x%08x madr=0x%08x qwc=%u feed=%u", regs.chcr, regs.madr,
                         regs.qwc, qwords);
                if (qwords > 0) {
                    const size_t bytes = static_cast<size_t>(qwords) * 16;
                    if (uint8_t* src = walker_.Translate(regs.madr, bytes)) {
                        // MADR/QWC advance before the feed: the IPU may retry a pending
                        // command on it, free room and kick this channel again from inside.
                        regs.madr += static_cast<uint32_t>(bytes);
                        regs.qwc -= qwords;
                        ipu_feed_input_(src, bytes);
                    } else {
                        regs.qwc = 0;
                    }
                }
                // Waits for FIFO room (no completion yet), or a nested kick already completed it.
                if (regs.qwc != 0 || (regs.chcr & DmacChcr::kStr) == 0) break;
            }
            CompleteChannel(ch);
            break;
        case kSif0:
        case kSif1:
        case kSif2:
            if (ch == kSif1 && sif_) sif_(ch, regs);
            CompleteChannel(ch);
            break;
        case kSprFrom:
        case kSprTo: {
            const auto result = spr_.Run(ch == kSprTo, regs, g_.ctrl);
            if (result == DmacSprResult::Complete) {
                CompleteChannel(ch);
            } else if (result != DmacSprResult::Unsupported) {
                // EE manual 5.4: invalid tags stop with CIS; an invalid memory
                // access additionally sets BEIS. Retain remaining QWC on faults.
                regs.chcr &= ~kStr;
                g_.stat |= DmacStat::Cis(ch);
                if (result == DmacSprResult::BusError) g_.stat |= DmacStat::kBeis;
                if (raise_ && CheckInterrupt()) raise_();
            }
            break;
        }
        default:
            CompleteChannel(ch);
            break;
    }
}

void Dmac::CompleteChannel(int ch) {
    if (ch < 0 || ch >= kDmacChannelCount) return;
    ch_[ch].chcr &= ~DmacChcr::kStr;
    g_.stat |= DmacStat::Cis(ch);
    ch_[ch].qwc = 0;
    if (raise_ && CheckInterrupt()) raise_();
}

bool Dmac::CheckInterrupt() const {
    const uint32_t status = g_.stat & DmacStat::kCisMask;
    const uint32_t mask = (g_.stat >> DmacStat::kCimShift) & DmacStat::kCisMask;
    return (g_.stat & DmacStat::kBeis) != 0 || (status & mask) != 0;
}

void Dmac::SetChannelInterruptMask(int ch, bool enable) {
    if (ch < 0 || ch >= kDmacChannelCount) return;
    const uint32_t bit = DmacStat::Cis(ch) << DmacStat::kCimShift;
    const bool was_pending = CheckInterrupt();
    if (enable) g_.stat |= bit;
    else g_.stat &= ~bit;
    if (raise_ && !was_pending && CheckInterrupt()) raise_();
}

void Dmac::DispatchInterrupt(CpuContext& ctx) {
    const uint32_t status = g_.stat & DmacStat::kCisMask;
    const uint32_t mask = (g_.stat >> DmacStat::kCimShift) & DmacStat::kCisMask;
    const uint32_t pending = status & mask;
    if (pending == 0) return;
    // Interrupt context (EeRunToken::IsrBegin/IsrEnd): a handler's iSignalSema
    // readies its thread, the hand-over waits for the handler's exit.
    EeRunToken::Instance().IsrBegin();
    for (int ch = 0; ch < kDmacChannelCount; ++ch) {
        if (!(pending & DmacStat::Cis(ch))) continue;
        // Acknowledged before the handlers run (even with none): a transfer the handler
        // itself re-arms may complete inside it and must raise the next interrupt.
        g_.stat &= ~DmacStat::Cis(ch);
        const std::vector<DmacHandler> chain = handlers_.ForCause(ch);
        for (const DmacHandler& h : chain) {
            if (!h.active) continue;
            // handler(channel, arg, addr) on the interrupted thread; its registers are the
            // interrupted code's and come back intact (the kernel's ISR frame).
            const uint32_t saved_pc = ctx.cpuRegs.pc;
            const auto saved_gpr = ctx.cpuRegs.GPR;
            ctx.cpuRegs.GPR.r[4].UD[0] = static_cast<uint64_t>(static_cast<int64_t>(ch));                 // $a0
            ctx.cpuRegs.GPR.r[5].UD[0] = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(h.arg)));  // $a1
            ctx.cpuRegs.GPR.r[6].UD[0] = 0;                                                               // $a2
            ctx.cpuRegs.GPR.r[28].UD[0] = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(h.gp)));  // $gp
            if (guest_call_) guest_call_(h.pc, ctx);
            const int32_t result = static_cast<int32_t>(ctx.cpuRegs.GPR.r[2].UL[0]);
            ctx.cpuRegs.pc = saved_pc;
            ctx.cpuRegs.GPR = saved_gpr;
            if (result == -1) break;  // the handler ends this channel's chain
        }
    }
    EeRunToken::Instance().IsrEnd();
}

void Dmac::FlushDeferredVif1() { deferred_.FlushDeferredVif1(ch_[kVif1], DmacDeferredVif1::FlushReason::Explicit); }
void Dmac::ChaseVif1Tail() { deferred_.ChaseVif1Tail(ch_[kVif1]); }

void Dmac::ObserveAtKernelWait() {
    // The flush drains the arbiter itself (the walk's PATH2 lands before the
    // queued PATH3); with nothing parked only the PATH3 backlog is delivered.
    if (deferred_.Active()) deferred_.FlushDeferredVif1(ch_[kVif1], DmacDeferredVif1::FlushReason::KernelWait);
    else if (arbiter_.hasPending()) arbiter_.drain();
}

// ---- savestate ----------------------------------------------------------------

void Dmac::SerializeSection(std::vector<uint8_t>& out) const {
    using ps2r::savestate::PutPod;
    for (int ch = 0; ch < kDmacChannelCount; ++ch) PutPod(out, ch_[ch]);
    PutPod(out, g_.ctrl);
    PutPod(out, g_.stat);
    PutPod(out, g_.pcr);
    PutPod(out, g_.sqwc);
    PutPod(out, g_.rbsr);
    PutPod(out, g_.rbor);
    PutPod(out, g_.stadr);
    PutPod(out, g_.enabler);
    PutPod(out, g_.enablew);
    PutPod<int32_t>(out, handlers_.NextId());
    const std::vector<DmacHandler>& table = handlers_.Table();
    PutPod<uint32_t>(out, static_cast<uint32_t>(table.size()));
    for (const DmacHandler& h : table) {
        PutPod<int32_t>(out, h.cause);
        PutPod<int32_t>(out, h.id);
        PutPod<uint32_t>(out, h.pc);
        PutPod<uint32_t>(out, h.gp);
        PutPod<uint32_t>(out, h.arg);
        PutPod<int32_t>(out, h.flag);
        PutPod<uint8_t>(out, h.active ? 1 : 0);
    }
}

bool Dmac::ValidateSection(const std::vector<uint8_t>& payload, std::string* err) {
    if (payload.size() < kSectionFixedBytes) {
        if (err) *err = "DMAC: payload shorter than the fixed part";
        return false;
    }
    const size_t rest = payload.size() - kSectionFixedBytes;
    if (rest % kHandlerRecordBytes != 0) {
        if (err) *err = "DMAC: handler records are not whole";
        return false;
    }
    ps2r::savestate::PayloadReader r(payload);
    r.Skip(kSectionFixedBytes - 4);
    uint32_t count = 0;
    r.Get(&count);
    if (count != rest / kHandlerRecordBytes || count > kMaxHandlers) {
        if (err) *err = "DMAC: handler count does not match the payload";
        return false;
    }
    return true;
}

bool Dmac::RestoreSection(const std::vector<uint8_t>& payload, std::string* err) {
    if (!ValidateSection(payload, err)) return false;
    ps2r::savestate::PayloadReader r(payload);
    for (int ch = 0; ch < kDmacChannelCount; ++ch) r.Get(&ch_[ch]);
    r.Get(&g_.ctrl);
    r.Get(&g_.stat);
    r.Get(&g_.pcr);
    r.Get(&g_.sqwc);
    r.Get(&g_.rbsr);
    r.Get(&g_.rbor);
    r.Get(&g_.stadr);
    r.Get(&g_.enabler);
    r.Get(&g_.enablew);
    int32_t next_id = 0;
    r.Get(&next_id);
    uint32_t count = 0;
    r.Get(&count);
    std::vector<DmacHandler> records;
    records.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        DmacHandler h;
        int32_t cause = 0, id = 0, flag = 0;
        uint8_t active = 0;
        if (!r.Get(&cause) || !r.Get(&id) || !r.Get(&h.pc) || !r.Get(&h.gp) || !r.Get(&h.arg) || !r.Get(&flag) ||
            !r.Get(&active)) {
            if (err) *err = "DMAC: truncated handler record";
            return false;
        }
        h.cause = cause;
        h.id = id;
        h.flag = flag;
        h.active = active != 0;
        records.push_back(h);
    }
    handlers_.Install(records, next_id);
    deferred_.Clear();  // a parked kick / tail records never survive a restore
    return true;
}

ps2r::savestate::SectionSpec Dmac::SavestateSection() {
    ps2r::savestate::SectionSpec spec;
    spec.tag = ps2r::savestate::TAG_DMAC;
    spec.version = 1;
    spec.required = true;
    spec.owner = "dmac";
    spec.serialize = [this](std::vector<uint8_t>& out) { SerializeSection(out); };
    spec.validate = [](const ps2r::savestate::Blob& b, std::string* err) { return ValidateSection(b.payload, err); };
    spec.restore = [this](const ps2r::savestate::Blob& b, std::string* err) { return RestoreSection(b.payload, err); };
    return spec;
}
