// gs/hle/ir/direct_ops.cpp -- DirectOps::PacketToOps.
#include "engine/gs/hle/ir/direct_ops.h"

#include <cstring>

namespace ps2r {
namespace gs {

bool DirectOps::PacketToOps(const uint8_t* p, size_t n, std::vector<Op>& out) {
    size_t off = 0;
    while (off + 16 <= n) {
        uint64_t lo, hi;
        std::memcpy(&lo, p + off, 8);
        std::memcpy(&hi, p + off + 8, 8);
        // GIFtag (manual 7.2.1): NLOOP[14:0] EOP[15] PRE[46] PRIM[57:47] FLG[59:58] NREG[63:60]
        const uint32_t nloop = static_cast<uint32_t>(lo & 0x7FFF);
        const bool eop = (lo >> 15) & 1;
        const bool pre = (lo >> 46) & 1;
        const uint32_t prim = static_cast<uint32_t>((lo >> 47) & 0x7FF);
        const uint32_t flg = static_cast<uint32_t>((lo >> 58) & 3);
        uint32_t nreg = static_cast<uint32_t>((lo >> 60) & 0xF);
        if (nreg == 0) nreg = 16;
        uint8_t regs[16];
        for (int i = 0; i < 16; i++) regs[i] = static_cast<uint8_t>((hi >> (i * 4)) & 0xF);
        off += 16;

        // IMAGE payloads feed the transfer engine and DISABLE stops the stream;
        // neither is a draw item.
        if (flg >= 2) return false;

        // PRE: the PRIM field is written before the payload (7.2.1).
        if (pre) out.push_back({prim, 0, 0x00});

        // NLOOP = 0: every field but EOP is ignored.
        if (nloop == 0) {
            if (eop) out.push_back({0, 0, kOpFlush});
            continue;
        }

        if (flg == 0) {  // PACKED: one quadword per register item
            const size_t need = static_cast<size_t>(nloop) * nreg * 16;
            if (off + need > n) return false;
            for (uint32_t l = 0; l < nloop; l++) {
                for (uint32_t r = 0; r < nreg; r++, off += 16) {
                    uint64_t qlo, qhi;
                    std::memcpy(&qlo, p + off, 8);
                    std::memcpy(&qhi, p + off + 8, 8);
                    const uint8_t d = regs[r];
                    switch (d) {
                        case 0x0F:  // NOP
                            break;
                        case 0x0E:  // A+D: register address in bits 71:64
                            out.push_back({qlo, 0, static_cast<uint16_t>(kModeReglist | (qhi & 0xFF))});
                            break;
                        case 0x00: case 0x01: case 0x02: case 0x03:
                        case 0x04: case 0x05: case 0x0A: case 0x0C: case 0x0D:
                            out.push_back({qlo, qhi, d});
                            break;
                        default:  // TEX0_1/2, CLAMP_1/2, 0x0B: the low 64 bits are the register
                            out.push_back({qlo, 0, static_cast<uint16_t>(kModeReglist | d)});
                            break;
                    }
                }
            }
        } else {  // REGLIST: one 64-bit item each, payload padded to a quadword
            const size_t total = static_cast<size_t>(nloop) * nreg;
            const size_t qws = (total + 1) / 2;
            if (off + qws * 16 > n) return false;
            for (size_t i = 0; i < total; i++, off += 8) {
                uint64_t v;
                std::memcpy(&v, p + off, 8);
                const uint8_t d = regs[i % nreg];
                // A+D and NOP have no meaning in REGLIST mode.
                if (d == 0x0E || d == 0x0F) continue;
                out.push_back({v, 0, static_cast<uint16_t>(kModeReglist | d)});
            }
            if (total & 1) off += 8;
        }

        // EOP takes effect after the tag's payload has been consumed.
        if (eop) out.push_back({0, 0, kOpFlush});
    }
    // A complete packet consumes exactly its bytes.
    return off == n;
}

}  // namespace gs
}  // namespace ps2r
