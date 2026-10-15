// The bulk UNPACK lanes (VIF1, mode 0, unmasked). Rules carried from the
// scalar loop: V4-16 / V4-8 widen with sign or zero extension per the USN bit;
// V3-32 takes its W lane from the next source word (the lookahead word after
// the last vector) and zeroes it when the vector's parity differs from the
// payload's quadword phase; every destination quadword is 16-aligned and never
// straddles the 16 KB wrap, so a whole-quadword store equals the per-byte
// masked write.
#include "engine/vif/vif_unpack_bulk.h"

#include <algorithm>
#include <cstring>

#if PS2R_SIMD
#include <arm_neon.h>
#endif

namespace {

constexpr uint32_t kVu1QwMask = 0x3FFu;

bool BytesNeeded(uint32_t format, uint32_t n, bool allow_v4_16, size_t* need) {
    switch (format) {
        case 0x8: *need = static_cast<size_t>(n) * 12; return true;
        case 0xC: *need = static_cast<size_t>(n) * 16; return true;
        case 0xD:
            if (!allow_v4_16) return false;
            *need = static_cast<size_t>(n) * 8;
            return true;
        case 0xE: *need = static_cast<size_t>(n) * 4; return true;
        default: return false;
    }
}

// One V3-32 vector: 12 bytes plus the lookahead W, then the parity rule.
inline void V3Vector(const uint8_t* src, size_t psz, size_t off, uint32_t k, uint8_t phase,
                     uint32_t* vec) {
    if (off + 16 <= psz) {
        std::memcpy(vec, src + off, 16);
    } else {
        std::memcpy(vec, src + off, 12);
        vec[3] = 0;
    }
    if ((k & 1u) != static_cast<uint32_t>(phase & 1u)) vec[3] = 0;
}

inline void Widen16(const uint8_t* s, bool usn, uint8_t* dst) {
#if PS2R_SIMD
    if (usn) {
        vst1q_u32(reinterpret_cast<uint32_t*>(dst),
                  vmovl_u16(vld1_u16(reinterpret_cast<const uint16_t*>(s))));
    } else {
        vst1q_s32(reinterpret_cast<int32_t*>(dst),
                  vmovl_s16(vld1_s16(reinterpret_cast<const int16_t*>(s))));
    }
#else
    uint32_t vec[4];
    for (int e = 0; e < 4; e++) {
        uint16_t hv;
        std::memcpy(&hv, s + e * 2, 2);
        vec[e] = usn ? static_cast<uint32_t>(hv)
                     : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(hv)));
    }
    std::memcpy(dst, vec, 16);
#endif
}

inline void Widen8(const uint8_t* s, bool usn, uint8_t* dst) {
#if PS2R_SIMD
    uint32_t w;
    std::memcpy(&w, s, 4);
    if (usn) {
        uint8x8_t b = vcreate_u8(static_cast<uint64_t>(w));
        vst1q_u32(reinterpret_cast<uint32_t*>(dst), vmovl_u16(vget_low_u16(vmovl_u8(b))));
    } else {
        int8x8_t b = vreinterpret_s8_u8(vcreate_u8(static_cast<uint64_t>(w)));
        vst1q_s32(reinterpret_cast<int32_t*>(dst), vmovl_s16(vget_low_s16(vmovl_s8(b))));
    }
#else
    uint32_t vec[4];
    for (int e = 0; e < 4; e++) {
        uint8_t bv = s[e];
        vec[e] = usn ? static_cast<uint32_t>(bv)
                     : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(bv)));
    }
    std::memcpy(dst, vec, 16);
#endif
}

}  // namespace

bool VifUnpackBulk::Unpack(uint32_t format, bool usn, uint32_t addr_qw, uint32_t n,
                           const uint8_t* src, size_t psz, uint8_t phase, uint8_t* dmem) {
    size_t need = 0;
    if (!BytesNeeded(format, n, true, &need)) return false;
    if (psz < need) return false;

    uint32_t aqw = addr_qw & kVu1QwMask;
    uint32_t done = 0;
    while (done < n) {
        const uint32_t run = std::min(n - done, 1024u - aqw);
        uint8_t* dst = dmem + static_cast<size_t>(aqw) * 16;
        switch (format) {
            case 0xC:
                std::memcpy(dst, src + static_cast<size_t>(done) * 16, static_cast<size_t>(run) * 16);
                break;
            case 0xD: {
                const uint8_t* s = src + static_cast<size_t>(done) * 8;
                for (uint32_t i = 0; i < run; i++)
                    Widen16(s + static_cast<size_t>(i) * 8, usn, dst + static_cast<size_t>(i) * 16);
                break;
            }
            case 0xE: {
                const uint8_t* s = src + static_cast<size_t>(done) * 4;
                for (uint32_t i = 0; i < run; i++)
                    Widen8(s + static_cast<size_t>(i) * 4, usn, dst + static_cast<size_t>(i) * 16);
                break;
            }
            case 0x8:
                for (uint32_t i = 0; i < run; i++) {
                    const uint32_t k = done + i;
                    uint32_t vec[4];
                    V3Vector(src, psz, static_cast<size_t>(k) * 12, k, phase, vec);
                    std::memcpy(dst + static_cast<size_t>(i) * 16, vec, 16);
                }
                break;
            default:
                return false;
        }
        done += run;
        aqw = (aqw + run) & kVu1QwMask;
    }
    return true;
}

bool VifUnpackBulk::UnpackSkip(uint32_t format, bool usn, uint32_t addr_qw, uint32_t n,
                               const uint8_t* src, size_t psz, uint8_t phase, uint32_t cl,
                               uint8_t* dmem) {
    size_t need = 0;
    if (!BytesNeeded(format, n, false, &need)) return false;
    if (psz < need) return false;

    const uint32_t aqw0 = addr_qw & kVu1QwMask;
    for (uint32_t k = 0; k < n; k++) {
        uint8_t* dst = dmem + static_cast<size_t>((aqw0 + k * cl) & kVu1QwMask) * 16;
        switch (format) {
            case 0xC:
                std::memcpy(dst, src + static_cast<size_t>(k) * 16, 16);
                break;
            case 0xE:
                Widen8(src + static_cast<size_t>(k) * 4, usn, dst);
                break;
            case 0x8: {
                uint32_t vec[4];
                V3Vector(src, psz, static_cast<size_t>(k) * 12, k, phase, vec);
                std::memcpy(dst, vec, 16);
                break;
            }
            default:
                return false;
        }
    }
    return true;
}
