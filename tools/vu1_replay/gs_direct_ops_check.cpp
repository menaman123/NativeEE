// GsDirectOpsCheck -- raw packet -> typed ops -> writes (rules in the header).
#include "tools/vu1_replay/gs_direct_ops_check.h"

#include <cstring>

bool GsDirectOpsCheck::PacketToOps(const uint8_t* p, size_t n, std::vector<Op>& out) {
    size_t off = 0;
    while (off + 16 <= n) {
        uint64_t lo, hi;
        std::memcpy(&lo, p + off, 8);
        std::memcpy(&hi, p + off + 8, 8);
        const uint32_t nloop = (uint32_t)(lo & 0x7FFF);
        const bool eop = (lo >> 15) & 1;
        const bool pre = (lo >> 46) & 1;
        const uint32_t prim = (uint32_t)((lo >> 47) & 0x7FF);
        const uint32_t flg = (uint32_t)((lo >> 58) & 3);
        uint32_t nreg = (uint32_t)((lo >> 60) & 0xF);
        if (nreg == 0) nreg = 16;
        uint8_t regs[16];
        for (int i = 0; i < 16; i++) regs[i] = (uint8_t)((hi >> (i * 4)) & 0xF);
        off += 16;

        if (flg >= 2) return false;  // IMAGE / DISABLE: not the draw path

        if (pre) out.push_back({prim, 0, 0x00});

        if (nloop == 0) {  // all fields ignored except EOP
            if (eop) out.push_back({0, 0, kOpFlush});
            continue;
        }

        if (flg == 0) {  // PACKED: one QW per register item
            const size_t need = (size_t)nloop * nreg * 16;
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
                        case 0x0E:  // A+D
                            out.push_back({qlo, 0, (uint16_t)(kModeReglist | (uint16_t)(qhi & 0xFF))});
                            break;
                        case 0x00: case 0x01: case 0x02: case 0x03:
                        case 0x04: case 0x05: case 0x0A: case 0x0C: case 0x0D:
                            out.push_back({qlo, qhi, d});
                            break;
                        default:
                            out.push_back({qlo, 0, (uint16_t)(kModeReglist | d)});
                            break;
                    }
                }
            }
        } else {  // REGLIST: one 64-bit per item, payload padded to a QW
            const size_t total = (size_t)nloop * nreg;
            const size_t qws = (total + 1) / 2;
            if (off + qws * 16 > n) return false;
            for (size_t i = 0; i < total; i++, off += 8) {
                uint64_t v;
                std::memcpy(&v, p + off, 8);
                const uint8_t d = regs[i % nreg];
                if (d == 0x0E || d == 0x0F) continue;
                out.push_back({v, 0, (uint16_t)(kModeReglist | d)});
            }
            if (total & 1) off += 8;
        }

        if (eop) out.push_back({0, 0, kOpFlush});
    }
    return off == n;
}

void GsDirectOpsCheck::OpsToWrites(const std::vector<Op>& ops, std::vector<std::pair<uint8_t, uint64_t>>& w) {
    uint32_t q_latch = 0x3F800000;  // the decoder's per-packet Q reset
    for (const auto& op : ops) {
        if (op.reg == kOpFlush) continue;
        if (op.reg & kModeReglist) {
            w.emplace_back((uint8_t)(op.reg & 0xFF), op.lo);
            continue;
        }
        switch ((uint8_t)op.reg) {
            case 0x00:  // PRIM
                w.emplace_back(0x00, op.lo & 0x7FF);
                break;
            case 0x01: {  // RGBAQ (PACKED layout, latched Q)
                const uint64_t r = op.lo & 0xFF, g = (op.lo >> 32) & 0xFF;
                const uint64_t bl = op.hi & 0xFF, al = (op.hi >> 32) & 0xFF;
                w.emplace_back(0x01, r | (g << 8) | (bl << 16) | (al << 24) | ((uint64_t)q_latch << 32));
                break;
            }
            case 0x02:  // ST (latches Q)
                q_latch = (uint32_t)(op.hi & 0xFFFFFFFF);
                w.emplace_back(0x02, op.lo);
                break;
            case 0x03:  // UV
                w.emplace_back(0x03, (op.lo & 0x3FFF) | (((op.lo >> 32) & 0x3FFF) << 16));
                break;
            case 0x04: {  // XYZF2 (ADC folds to XYZF3)
                const uint64_t x = op.lo & 0xFFFF, y = (op.lo >> 32) & 0xFFFF;
                const uint64_t z = (op.hi >> 4) & 0xFFFFFF;
                const uint64_t f = (op.hi >> 36) & 0xFF;
                const bool adc = (op.hi >> 47) & 1;
                w.emplace_back((uint8_t)(adc ? 0x0C : 0x04), x | (y << 16) | (z << 32) | (f << 56));
                break;
            }
            case 0x05: {  // XYZ2 (ADC folds to XYZ3)
                const uint64_t x = op.lo & 0xFFFF, y = (op.lo >> 32) & 0xFFFF;
                const uint64_t z = op.hi & 0xFFFFFFFF;
                const bool adc = (op.hi >> 47) & 1;
                w.emplace_back((uint8_t)(adc ? 0x0D : 0x05), x | (y << 16) | (z << 32));
                break;
            }
            case 0x0A:  // FOG
                w.emplace_back(0x0A, ((op.hi >> 36) & 0xFFull) << 56);
                break;
            default:  // PACKED 0x0C/0x0D: the decoder's default-case projection
                w.emplace_back((uint8_t)op.reg, op.lo);
                break;
        }
    }
}

bool GsDirectOpsCheck::Matches(const std::vector<std::pair<uint8_t, uint64_t>>& typed,
                               const GsPacketDiff::Stream& decoded) {
    if (!decoded.errors.empty() || typed.size() != decoded.writes.size()) return false;
    for (size_t i = 0; i < typed.size(); i++)
        if (typed[i].first != decoded.writes[i].addr || typed[i].second != decoded.writes[i].value) return false;
    return true;
}
