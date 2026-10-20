// Whole-span VIF parser (see the header for the decline set and the contract).
#include "engine/vif/vif_fast_span.h"

#include <algorithm>
#include <cstring>

#include "engine/vif/vif.h"

namespace {

inline uint32_t ReadWord(const uint8_t* data, size_t w) {
    uint32_t x;
    std::memcpy(&x, data + w * 4, 4);
    return x;
}

inline void AppendWord(std::vector<uint8_t>& out, uint32_t w) {
    out.push_back(static_cast<uint8_t>(w & 0xFF));
    out.push_back(static_cast<uint8_t>((w >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((w >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((w >> 24) & 0xFF));
}

}  // namespace

bool VifFastSpan::Process(Vif& vif, int v, const uint8_t* data, size_t size) {
    VifUnitState& u = vif.u_[v];
    if (u.tte_feed) return false;          // the TTE pad skip needs the latch path
    if ((size & 15) != 0) return false;    // whole quadwords only
    if (!u.fifo.empty()) return false;     // buffered quadwords pending
    if (u.latch_fill != 0) return false;   // partial latch in flight
    if (u.word_index < 4) return false;    // leftover words of a held quadword

    const size_t nwords = size / 4;
    size_t wi = 0;  // parse cursor in 32-bit words

    if (u.state == VifState::DirectStream) {
        vif.UpdateStat(v, VifStat::kVpsTransfer);
        const uint32_t qwc_needed = (u.pending_bytes + 15) / 16;
        const uint32_t chunk = std::min<uint32_t>(static_cast<uint32_t>(nwords / 4), qwc_needed);
        if (chunk > 0) {
            u.direct_buffer.insert(u.direct_buffer.end(), data, data + static_cast<size_t>(chunk) * 16);
            const uint32_t transferred = chunk * 16;
            u.pending_bytes = (transferred > u.pending_bytes) ? 0 : u.pending_bytes - transferred;
        }
        if (u.pending_bytes != 0) return true;  // span exhausted, still streaming
        vif.CompleteDirect(v);
        wi = static_cast<size_t>(chunk) * 4;
    } else if (u.state == VifState::WaitData) {
        const size_t words_needed = (u.pending_bytes + 3) / 4;
        if (words_needed > nwords) {
            for (size_t w = 0; w < nwords; w++) vif.WriteToLatch2(v, ReadWord(data, w));
            return true;
        }
        vif.UpdateStat(v, VifStat::kVpsTransfer);
        u.unpack_phase = 0;  // the payload resumes at span word 0 = quadword phase 0
        u.payload.assign(data, data + words_needed * 4);
        if (VifUnpack::IsUnpackCmd(u.pending_cmd)) {
            const uint32_t la = (words_needed & 3) != 0 ? ReadWord(data, words_needed) : 0;
            AppendWord(u.payload, la);
        }
        vif.ExecuteCommand(v, u.pending_cmd, u.pending_imm, u.pending_num, u.payload.data(),
                           u.payload.size(), u.pending_irq, u.pending_use_mask);
        u.state = VifState::Idle;
        u.pending_bytes = 0;
        vif.UpdateStat(v, VifStat::kVpsIdle);
        wi = words_needed;
    }

    while (wi < nwords) {
        const size_t p = wi;
        const uint32_t code = ReadWord(data, p);
        const bool irq = ((code >> 31) & 1) != 0;
        const uint32_t cmd = (code >> 24) & 0x7F;
        const uint32_t num = (code >> 16) & 0xFF;
        const uint32_t imm = code & 0xFFFF;
        u.regs.code = code;
        wi++;

        if (cmd == static_cast<uint32_t>(VifCommand::Direct) ||
            cmd == static_cast<uint32_t>(VifCommand::Directhl)) {
            const uint32_t qwc = (imm == 0) ? 0x10000u : imm;
            const size_t s = (p + 4) & ~static_cast<size_t>(3);  // payload starts on the next quadword
            const size_t need_words = static_cast<size_t>(qwc) * 4;
            u.pending_directhl = cmd == static_cast<uint32_t>(VifCommand::Directhl);
            if (need_words <= nwords - s) {
                u.direct_buffer.clear();
                u.direct_buffer.reserve(qwc * 16);
                u.direct_buffer.insert(u.direct_buffer.end(), data + s * 4, data + s * 4 + need_words * 4);
                vif.SubmitDirect(v);
                vif.UpdateStat(v, VifStat::kVpsIdle);
                if (irq) vif.RaiseIBit(v);
                u.pending_irq = false;
                wi = s + need_words;
                continue;
            }
            u.word_index = 4;
            u.state = VifState::DirectStream;
            u.pending_bytes = qwc * 16;
            u.pending_irq = irq;
            u.direct_buffer.clear();
            u.direct_buffer.reserve(qwc * 16);
            for (size_t w = s; w < nwords; w++) vif.WriteToLatch2(v, ReadWord(data, w));
            return true;
        }

        bool use_mask = false;
        const uint32_t bytes_needed = vif.BytesNeeded(v, cmd, num, &use_mask);
        if (bytes_needed == 0) {
            vif.ExecuteCommand(v, cmd, imm, num, nullptr, 0, irq, use_mask);
            continue;
        }

        const size_t words_needed = (bytes_needed + 3) / 4;
        const size_t s = wi;  // payload start word
        if (words_needed > nwords - s) {
            // The payload crosses the span end: park the command and seed the
            // latch path with the partial quadword so it sees the same phase.
            u.state = VifState::WaitData;
            u.pending_cmd = cmd;
            u.pending_num = num;
            u.pending_imm = imm;
            u.pending_bytes = bytes_needed;
            u.pending_irq = irq;
            u.pending_use_mask = use_mask;
            vif.UpdateStat(v, VifStat::kVpsWait);
            size_t rem = s;
            if ((s & 3) != 0) {
                std::memcpy(u.current_qw.data(), data + (s / 4) * 16, 16);
                u.word_index = static_cast<int>(s & 3);
                rem = (s / 4 + 1) * 4;
            }
            for (size_t w = rem; w < nwords; w++) vif.WriteToLatch2(v, ReadWord(data, w));
            return true;
        }

        u.unpack_phase = static_cast<uint8_t>(s & 1);
        u.payload.assign(data + s * 4, data + s * 4 + words_needed * 4);
        if (VifUnpack::IsUnpackCmd(cmd)) {
            const size_t e = s + words_needed;
            const uint32_t la = (e & 3) != 0 ? ReadWord(data, e) : 0;  // e < nwords: nwords is a multiple of 4
            AppendWord(u.payload, la);
        }
        vif.ExecuteCommand(v, cmd, imm, num, u.payload.data(), u.payload.size(), irq, use_mask);
        wi = s + words_needed;
    }
    return true;
}
