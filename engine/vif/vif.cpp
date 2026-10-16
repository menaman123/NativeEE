// VIF0/VIF1 command processors: feed, word-latch state machine, command execution, MMIO.
#include "engine/vif/vif.h"

#include <algorithm>
#include <cstring>

#include "engine/core/log/log.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_regs.h"
#include "engine/gif/gif_tag.h"
#include "engine/vif/vif_fast_span.h"
#include "engine/vu/vu_unit.h"

namespace {

constexpr uint32_t kCycleDefault = 0x0101u;  // CL = 1, WL = 1
constexpr int kMaxFifoLoops = 0x10000;

// Little-endian word append.
inline void AppendWord(std::vector<uint8_t>& out, uint32_t w) {
    out.push_back(static_cast<uint8_t>(w & 0xFF));
    out.push_back(static_cast<uint8_t>((w >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((w >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((w >> 24) & 0xFF));
}

inline bool IsCmd(uint32_t cmd, VifCommand c) { return cmd == static_cast<uint32_t>(c); }

}  // namespace

Vif::Vif(VuUnit& vu0, VuUnit& vu1, GifArbiter& arbiter, GifRegs& gif_regs, VifSettings settings)
    : vu0_(vu0),
      vu1_(vu1),
      vu1_target_(&vu1),
      arbiter_(arbiter),
      gif_regs_(gif_regs),
      settings_(settings),
      unpack_(settings.unpack) {
    Reset();
}

void Vif::Reset() {
    for (int v = 0; v < 2; v++) {
        VifUnitState& u = u_[v];
        std::memset(&u.regs, 0, sizeof(u.regs));
        u.regs.cycle = kCycleDefault;
        ResetTransient(v);
    }
}

void Vif::ResetTransient(int vif) {
    VifUnitState& u = u_[vif];
    u.latch.fill(0);
    u.latch_fill = 0;
    u.fifo.clear();
    u.current_qw.fill(0);
    u.word_index = 4;
    u.state = VifState::Idle;
    u.pending_cmd = 0;
    u.pending_bytes = 0;
    u.pending_num = 0;
    u.pending_imm = 0;
    u.pending_irq = false;
    u.pending_use_mask = false;
    u.pending_directhl = false;
    u.unpack_phase = 0;
    u.tte_feed = false;
    u.direct_buffer.clear();
    u.payload.clear();
}

// ---------------------------------------------------------------- feed ------

void Vif::ProcessData(int vif, const uint8_t* data, size_t size) {
    if (!data || size == 0) return;
    if (settings_.fast_span && VifFastSpan::Process(*this, vif, data, size)) return;
    for (size_t i = 0; i + 4 <= size; i += 4) {
        uint32_t word;
        std::memcpy(&word, data + i, 4);
        WriteToLatch2(vif, word);
    }
}

void Vif::WriteToLatch2(int vif, uint32_t word) {
    VifUnitState& u = u_[vif];
    if (u.latch_fill < 4) u.latch[u.latch_fill++] = word;
    if (u.latch_fill == 4) {
        u.fifo.push_back(u.latch);
        u.latch_fill = 0;
        ProcessFifo(vif);
    }
}

void Vif::WriteFifo(int vif, uint32_t address, uint32_t word) {
    VifUnitState& u = u_[vif];
    const uint32_t slot = (address & 0x0Cu) >> 2;
    u.latch[slot] = word;
    if (slot == 3) {
        u.fifo.push_back(u.latch);
        ProcessFifo(vif);
    }
}

void Vif::UpdateStat(int vif, uint32_t vps) {
    VifRegs& r = u_[vif].regs;
    r.stat = (r.stat & ~VifStat::kVpsMask) | (vps & VifStat::kVpsMask);
    const uint32_t fqc = static_cast<uint32_t>(std::min<size_t>(u_[vif].fifo.size() / 16, VifStat::kFqcMask));
    r.stat = (r.stat & ~(VifStat::kFqcMask << VifStat::kFqcShift)) | (fqc << VifStat::kFqcShift);
}

uint32_t Vif::BytesNeeded(int vif, uint32_t cmd, uint32_t num, bool* use_mask) const {
    *use_mask = false;
    if (VifUnpack::IsUnpackCmd(cmd)) {
        *use_mask = (cmd & 0x10) != 0;
        return VifUnpack::GetUnpackSize(cmd, num, VifRegsCl(u_[vif].regs), VifRegsWl(u_[vif].regs));
    }
    if (IsCmd(cmd, VifCommand::Mpg)) return (num == 0 ? 256 : num) * 8;
    if (IsCmd(cmd, VifCommand::Stmask)) return 4;
    if (IsCmd(cmd, VifCommand::Strow) || IsCmd(cmd, VifCommand::Stcol)) return 16;
    return 0;
}

void Vif::SubmitDirect(int vif) {
    VifUnitState& u = u_[vif];
    if (!u.direct_buffer.empty()) {
        // Both feeds (the word latch and the whole-span parser) hand PATH2 its packet here.
        PS2R_LOG(VIF, Narrate, "vif%d: direct%s -> path2 payload=%zu", vif, u.pending_directhl ? "hl" : "",
                 u.direct_buffer.size());
        arbiter_.submit(GifPath::Path2, u.direct_buffer.data(), u.direct_buffer.size(),
                        u.pending_directhl);
        u.direct_buffer.clear();
    }
}

// The DIRECT transfer's last quadword arrived: hand PATH2 the packet, go idle,
// raise the i-bit interrupt the code word carried.
void Vif::CompleteDirect(int vif) {
    VifUnitState& u = u_[vif];
    SubmitDirect(vif);
    u.state = VifState::Idle;
    UpdateStat(vif, VifStat::kVpsIdle);
    if (u.pending_irq && !(u.regs.err & VifErr::kMii)) u.regs.stat |= VifStat::kInt;
    u.pending_irq = false;
}

// ------------------------------------------------- word-latch state machine --

void Vif::ProcessFifo(int vif) {
    VifUnitState& u = u_[vif];
    int loops = 0;
    while (loops++ < kMaxFifoLoops) {
        if (u.state == VifState::DirectStream) {
            UpdateStat(vif, VifStat::kVpsTransfer);
            if (u.word_index < 4) u.word_index = 4;  // the rest of the code word's quadword is discarded
            const uint32_t available = static_cast<uint32_t>(u.fifo.size());
            const uint32_t qwc_needed = (u.pending_bytes + 15) / 16;
            const uint32_t chunk = std::min(available, qwc_needed);
            for (uint32_t i = 0; i < chunk; i++) {
                const auto& qw = u.fifo.front();
                const uint8_t* bytes = reinterpret_cast<const uint8_t*>(qw.data());
                u.direct_buffer.insert(u.direct_buffer.end(), bytes, bytes + 16);
                u.fifo.pop_front();
            }
            const uint32_t transferred = chunk * 16;
            u.pending_bytes = (transferred > u.pending_bytes) ? 0 : u.pending_bytes - transferred;
            if (u.pending_bytes != 0) return;
            CompleteDirect(vif);
            continue;
        }

        if (u.state == VifState::WaitData) {
            uint32_t words_needed = (u.pending_bytes + 3) / 4;
            const uint32_t words_in_curr = static_cast<uint32_t>(4 - u.word_index);
            const uint32_t fifo_words = static_cast<uint32_t>(u.fifo.size()) * 4;
            if (words_in_curr + fifo_words < words_needed) return;
            UpdateStat(vif, VifStat::kVpsTransfer);
            u.payload.clear();
            u.unpack_phase = static_cast<uint8_t>((u.word_index >= 4 ? 0 : u.word_index) & 1);
            while (words_needed > 0) {
                if (u.word_index >= 4) {
                    u.current_qw = u.fifo.front();
                    u.fifo.pop_front();
                    u.word_index = 0;
                }
                AppendWord(u.payload, u.current_qw[static_cast<size_t>(u.word_index++)]);
                words_needed--;
            }
            if (VifUnpack::IsUnpackCmd(u.pending_cmd)) {
                uint32_t la = 0;
                if (u.word_index < 4) la = u.current_qw[static_cast<size_t>(u.word_index)];
                else if (!u.fifo.empty()) la = u.fifo.front()[0];
                AppendWord(u.payload, la);
            }
            ExecuteCommand(vif, u.pending_cmd, u.pending_imm, u.pending_num, u.payload.data(),
                           u.payload.size(), u.pending_irq, u.pending_use_mask);
            u.state = VifState::Idle;
            u.pending_bytes = 0;
            UpdateStat(vif, VifStat::kVpsIdle);
            continue;
        }

        // Idle: decode the next VIFcode.
        if (u.word_index >= 4) {
            if (u.fifo.empty()) return;
            u.current_qw = u.fifo.front();
            u.fifo.pop_front();
            u.word_index = 0;
        }
        const uint32_t code = u.current_qw[static_cast<size_t>(u.word_index++)];
        const bool irq = ((code >> 31) & 1) != 0;
        const uint32_t cmd = (code >> 24) & 0x7F;
        const uint32_t num = (code >> 16) & 0xFF;
        const uint32_t imm = code & 0xFFFF;
        u.regs.code = code;

        if (IsCmd(cmd, VifCommand::Direct) || IsCmd(cmd, VifCommand::Directhl)) {
            u.word_index = 4;  // the payload starts on the next quadword
            const uint32_t qwc = (imm == 0) ? 0x10000u : imm;
            u.state = VifState::DirectStream;
            u.pending_bytes = qwc * 16;
            u.pending_irq = irq;
            u.pending_directhl = IsCmd(cmd, VifCommand::Directhl);
            u.direct_buffer.clear();
            u.direct_buffer.reserve(qwc * 16);
            continue;
        }

        bool use_mask = false;
        const uint32_t bytes_needed = BytesNeeded(vif, cmd, num, &use_mask);
        const bool force_align = VifUnpack::IsUnpackCmd(cmd) || IsCmd(cmd, VifCommand::Mpg);
        if (bytes_needed == 0) {
            ExecuteCommand(vif, cmd, imm, num, nullptr, 0, irq, use_mask);
            continue;
        }

        uint32_t words_needed = (bytes_needed + 3) / 4;
        // A tag's TTE quadword ends in two pad words: a payload that does not
        // fit in the remaining words of that quadword starts on the next one.
        if (force_align && static_cast<uint32_t>(4 - u.word_index) < words_needed && u.tte_feed)
            u.word_index = 4;
        const uint32_t words_in_curr = static_cast<uint32_t>(4 - u.word_index);
        const uint32_t fifo_words = static_cast<uint32_t>(u.fifo.size()) * 4;
        if (words_in_curr + fifo_words < words_needed) {
            u.state = VifState::WaitData;
            u.pending_cmd = cmd;
            u.pending_num = num;
            u.pending_imm = imm;
            u.pending_bytes = bytes_needed;
            u.pending_irq = irq;
            u.pending_use_mask = use_mask;
            UpdateStat(vif, VifStat::kVpsWait);
            return;
        }
        u.payload.clear();
        u.unpack_phase = static_cast<uint8_t>((u.word_index >= 4 ? 0 : u.word_index) & 1);
        while (words_needed > 0) {
            if (u.word_index >= 4) {
                u.current_qw = u.fifo.front();
                u.fifo.pop_front();
                u.word_index = 0;
            }
            AppendWord(u.payload, u.current_qw[static_cast<size_t>(u.word_index++)]);
            words_needed--;
        }
        if (VifUnpack::IsUnpackCmd(cmd)) {
            uint32_t la = 0;
            if (u.word_index < 4) la = u.current_qw[static_cast<size_t>(u.word_index)];
            else if (!u.fifo.empty()) la = u.fifo.front()[0];
            AppendWord(u.payload, la);
        }
        ExecuteCommand(vif, cmd, imm, num, u.payload.data(), u.payload.size(), irq, use_mask);
    }
    if (u.state == VifState::Idle) UpdateStat(vif, VifStat::kVpsIdle);
}

// --------------------------------------------------------- command execution --

void Vif::SwapDoubleBuffer() {
    VifRegs& r = u_[1].regs;
    r.top = r.tops;
    r.itops = r.itop;
    if (r.ofst != 0) r.tops = (r.stat & VifStat::kDbf) ? r.base : (r.base + r.ofst);
    r.stat ^= VifStat::kDbf;
}

void Vif::KickVu1(uint32_t start_pc, bool resume) {
    SwapDoubleBuffer();
    const VifRegs& r = u_[1].regs;
    VuUnit& vu = CtxFor(1);
    vu.SetVifTop(static_cast<uint16_t>(r.top), static_cast<uint16_t>(r.itops));
    if (tops_obs_) tops_obs_(tops_user_, static_cast<uint16_t>(r.tops), static_cast<uint16_t>(r.itops));
    vu.Execute(resume ? vu.regs().TPC : start_pc);
}

void Vif::ExecuteCommand(int vif, uint32_t cmd, uint32_t imm, uint32_t num, const uint8_t* payload,
                         size_t payload_size, bool irq, bool use_mask) {
    VifUnitState& u = u_[vif];
    VifRegs& r = u.regs;
    PS2R_LOG(VIF, Narrate, "vif%d: cmd=0x%02x imm=0x%04x num=0x%02x irq=%d payload=%zu", vif, cmd, imm, num,
             irq ? 1 : 0, payload_size);
    if (irq && !(r.err & VifErr::kMii)) r.stat |= VifStat::kInt;

    if (VifUnpack::IsUnpackCmd(cmd)) {
        unpack_.Execute(vif, CtxFor(vif), r, cmd, imm, num, payload, payload_size, use_mask,
                        u.unpack_phase);
        return;
    }

    switch (static_cast<VifCommand>(cmd)) {
        case VifCommand::Nop:
            break;
        case VifCommand::Stcycl:
            r.cycle = imm;
            break;
        case VifCommand::Offset:
            if (vif == 1) {
                r.ofst = imm & 0x3FF;
                r.stat &= ~VifStat::kDbf;
                r.tops = r.base;
            }
            break;
        case VifCommand::Base:
            if (vif == 1) r.base = imm & 0x3FF;
            break;
        case VifCommand::Itop:
            r.itop = imm & 0x3FF;
            break;
        case VifCommand::Stmod:
            r.mode = imm & 3;
            break;
        case VifCommand::Mskpath3:
            if (vif == 1) gif_regs_.SetPath3MaskedByVif((imm & 0x8000) != 0);
            break;
        case VifCommand::Mark:
            r.mark = imm;
            r.stat |= VifStat::kMrk;
            break;
        case VifCommand::Flushe:
        case VifCommand::Flush:
        case VifCommand::Flusha:
            // VU programs and PATH2 transfers complete synchronously here: the wait is already satisfied.
            break;
        case VifCommand::Mscal:
            if (vif == 1) KickVu1(imm * 8, false);
            else vu0_.Execute(imm * 8);
            break;
        case VifCommand::Mscalf:
            if (vif == 1) KickVu1(imm * 8, false);
            break;
        case VifCommand::Mscnt:
            if (vif == 1) KickVu1(0, true);
            break;
        case VifCommand::Stmask:
            if (payload_size >= 4) std::memcpy(&r.mask, payload, 4);
            break;
        case VifCommand::Strow:
            if (payload_size >= 16) std::memcpy(r.row, payload, 16);
            break;
        case VifCommand::Stcol:
            if (payload_size >= 16) std::memcpy(r.col, payload, 16);
            break;
        case VifCommand::Mpg:
            CtxFor(vif).WriteMicroMem(imm * 8, payload, payload_size);
            break;
        case VifCommand::Direct:
        case VifCommand::Directhl:
            break;  // handled by the feed paths
        default:
            break;  // undefined VIFcodes are ignored (no ER1, no stall)
    }
}

// ------------------------------------------------------------- MTVU seam ----

VuUnit& Vif::CtxFor(int vif) { return vif == 1 ? *vu1_target_ : vu0_; }

void Vif::BindVu1(VuUnit* unit) { vu1_target_ = unit ? unit : &vu1_; }

void Vif::SetTteFeed(int vif, bool on) { u_[vif].tte_feed = on; }

void Vif::ProcessVif1(const uint8_t* data, size_t size) { ProcessData(1, data, size); }
void Vif::SetVif1TteFeed(bool on) { SetTteFeed(1, on); }
void Vif::ResetVif1Latch() { ResetLatch(1); }

void Vif::ResetLatch(int vif) {
    u_[vif].latch_fill = 0;
    u_[vif].latch.fill(0);
}

bool Vif::IsIdle(int vif) const {
    const VifUnitState& u = u_[vif];
    return u.state == VifState::Idle && u.latch_fill == 0 && u.word_index >= 4 && u.fifo.empty();
}

void Vif::SetTopsObserver(TopsObserver fn, void* user) {
    tops_obs_ = fn;
    tops_user_ = user;
}

// ------------------------------------------------------------------ MMIO ----

uint32_t Vif::ReadRegister(int vif, uint32_t offset) const {
    const VifRegs& r = u_[vif].regs;
    using namespace VifRegOffset;
    switch (offset) {
        case kStat: return r.stat;
        case kFbrst: return r.fbrst;
        case kErr: return r.err;
        case kMark: return r.mark;
        case kCycle: return r.cycle;
        case kMode: return r.mode;
        case kNum: return r.num;
        case kMask: return r.mask;
        case kCode: return r.code;
        case kItops: return r.itops;
        case kBase: return vif == 1 ? r.base : 0;
        case kOfst: return vif == 1 ? r.ofst : 0;
        case kTops: return vif == 1 ? r.tops : 0;
        case kItop: return r.itop;
        case kTop: return vif == 1 ? r.top : 0;
        default:
            if (offset >= kRow0 && offset < kRow0 + 0x10) return r.row[(offset - kRow0) / 4];
            if (offset >= kCol0 && offset < kCol0 + 0x10) return r.col[(offset - kCol0) / 4];
            return 0;
    }
}

void Vif::WriteRegister(int vif, uint32_t offset, uint32_t value) {
    VifUnitState& u = u_[vif];
    VifRegs& r = u.regs;
    using namespace VifRegOffset;
    switch (offset) {
        case kStat:
            r.stat &= ~(value & 0x3F40u);  // write-1-to-clear MRK / VSS / VFS / VIS / INT / ER0 / ER1
            break;
        case kFbrst:
            r.fbrst = value;
            if (value & VifFbrst::kRst) {
                u.fifo.clear();
                u.state = VifState::Idle;
                u.pending_bytes = 0;
            }
            if (value & VifFbrst::kFbk) r.stat |= VifStat::kVfs;
            if (value & VifFbrst::kStp) r.stat |= VifStat::kVss;
            if (value & VifFbrst::kStc)
                r.stat &= ~(VifStat::kVss | VifStat::kVfs | VifStat::kVis | VifStat::kInt |
                            VifStat::kEr0 | VifStat::kEr1);
            break;
        case kErr:
            r.err = value & 7;
            break;
        case kMark:
            r.mark = value;
            r.stat &= ~VifStat::kMrk;
            break;
        case kCycle:
            r.cycle = value;
            break;
        case kMode:
            r.mode = value & 3;
            break;
        case kMask:
            r.mask = value;
            break;
        default:
            if (offset >= kRow0 && offset < kRow0 + 0x10) r.row[(offset - kRow0) / 4] = value;
            else if (offset >= kCol0 && offset < kCol0 + 0x10) r.col[(offset - kCol0) / 4] = value;
            break;  // NUM / CODE / ITOPS / BASE / OFST / TOPS / ITOP / TOP are read-only
    }
}

uint64_t Vif::Read(uint32_t addr, unsigned size) {
    (void)size;
    if (addr >= kVif0Base && addr < kVif0Base + kRegsSize) return ReadRegister(0, addr - kVif0Base);
    if (addr >= kVif1Base && addr < kVif1Base + kRegsSize) return ReadRegister(1, addr - kVif1Base);
    return 0;
}

void Vif::Write(uint32_t addr, unsigned size, uint64_t value) {
    (void)size;
    const uint32_t v32 = static_cast<uint32_t>(value);
    if (addr >= kVif0Base && addr < kVif0Base + kRegsSize) WriteRegister(0, addr - kVif0Base, v32);
    else if (addr >= kVif1Base && addr < kVif1Base + kRegsSize) WriteRegister(1, addr - kVif1Base, v32);
    else if (addr >= kVif1FifoBase && addr < kVif1FifoBase + kFifoSize) WriteFifo(1, addr, v32);
}
