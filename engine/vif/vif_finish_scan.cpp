#include "engine/vif/vif_finish_scan.h"

#include <algorithm>
#include <cstring>

#include "engine/vif/vif_regs.h"
#include "engine/vif/vif_unpack.h"

namespace {

inline uint32_t ReadWord(const uint8_t* data, size_t w) {
    uint32_t x;
    std::memcpy(&x, data + w * 4, 4);
    return x;
}

// Payload bytes a non-DIRECT code word needs after itself (Vif::BytesNeeded).
uint32_t BytesNeeded(uint32_t cmd, uint32_t num, uint32_t cl, uint32_t wl) {
    if (VifUnpack::IsUnpackCmd(cmd)) return VifUnpack::GetUnpackSize(cmd, num, cl, wl);
    if (cmd == static_cast<uint32_t>(VifCommand::Mpg)) return (num == 0 ? 256 : num) * 8;
    if (cmd == static_cast<uint32_t>(VifCommand::Stmask)) return 4;
    if (cmd == static_cast<uint32_t>(VifCommand::Strow) || cmd == static_cast<uint32_t>(VifCommand::Stcol)) return 16;
    return 0;
}

}  // namespace

void VifFinishScan::ResyncRegs(uint32_t cl, uint32_t wl) {
    cl_ = cl;
    wl_ = wl;
    mode_ = Mode::Idle;
    words_left_ = 0;
    direct_finishes_ = 0;
    qw_phase_ = 0;
    path2_.Reset();
}

uint32_t VifFinishScan::Feed(const uint8_t* data, size_t size, bool tte) {
    uint32_t found = 0;
    const size_t nwords = size / 4;
    // Stream coordinates: word `wi` of this span is at quadword phase (base + wi) & 3.
    const size_t base = qw_phase_;
    size_t wi = 0;
    while (wi < nwords) {
        if (mode_ == Mode::Direct) {
            const size_t take = std::min<size_t>(words_left_, nwords - wi);
            // Counted when the DIRECT completes: the VIF hands PATH2 the packet at its
            // last quadword (Vif::CompleteDirect), so a FINISH inside a DIRECT still
            // owed bytes is not yet the parser's.
            direct_finishes_ += path2_.Feed(data + wi * 4, take * 4);
            words_left_ -= static_cast<uint32_t>(take);
            wi += take;
            if (words_left_ == 0) {
                mode_ = Mode::Idle;
                found += direct_finishes_;
                direct_finishes_ = 0;
            }
            continue;
        }
        if (mode_ == Mode::WaitData) {
            const size_t take = std::min<size_t>(words_left_, nwords - wi);
            words_left_ -= static_cast<uint32_t>(take);
            wi += take;
            if (words_left_ == 0) mode_ = Mode::Idle;
            continue;
        }
        // Idle: the next VIFcode.
        const size_t p = base + wi;   // the code's stream position
        const uint32_t code = ReadWord(data, wi);
        const uint32_t cmd = (code >> 24) & 0x7F;
        const uint32_t num = (code >> 16) & 0xFF;
        const uint32_t imm = code & 0xFFFF;
        wi++;
        if (cmd == static_cast<uint32_t>(VifCommand::Direct) || cmd == static_cast<uint32_t>(VifCommand::Directhl)) {
            const uint32_t qwc = (imm == 0) ? 0x10000u : imm;
            const size_t s = ((p + 4) & ~static_cast<size_t>(3)) - base;   // the next quadword
            mode_ = Mode::Direct;
            words_left_ = qwc * 4;
            wi = std::min(s, nwords);
            continue;
        }
        if (cmd == static_cast<uint32_t>(VifCommand::Stcycl)) {
            cl_ = imm & 0xFFu;
            wl_ = (imm >> 8) & 0xFFu;
            continue;
        }
        const uint32_t bytes = BytesNeeded(cmd, num, cl_, wl_);
        if (bytes == 0) continue;
        const uint32_t words_needed = (bytes + 3) / 4;
        size_t s = wi;   // the payload starts on the next word ...
        if (tte && (VifUnpack::IsUnpackCmd(cmd) || cmd == static_cast<uint32_t>(VifCommand::Mpg))) {
            // ... unless, inside a TTE quadword, an UNPACK / MPG payload does not fit the
            // quadword's remaining words: then on the next quadword (Vif::ProcessFifo).
            const size_t after = (p + 1) & 3;
            const size_t remaining = after == 0 ? 0 : 4 - after;
            if (remaining < words_needed) s = ((p + 4) & ~static_cast<size_t>(3)) - base;
        }
        mode_ = Mode::WaitData;
        words_left_ = words_needed;
        wi = std::min(s, nwords);
    }
    qw_phase_ = static_cast<uint32_t>((base + nwords) & 3);
    return found;
}
