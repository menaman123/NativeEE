#include "engine/ee/hle/libc.h"
#include "engine/ee/timing/tick_profile.h"

#include <cstring>

#include "engine/ee/mem/main_memory.h"
#include "engine/ee/recomp_abi/host_app/memory.h"

namespace {
uint64_t g_prot_lo = 0, g_prot_hi = 0;

MainMemory& Ram() { return RecompAbi::Memory().Ram(); }

uint64_t Rd64(const uint8_t* ram, uint32_t a) {
    uint64_t v;
    std::memcpy(&v, ram + a, 8);
    return v;
}
}  // namespace

void Libc::SetProtectedBand(uint64_t lo, uint64_t hi) {
    g_prot_lo = lo;
    g_prot_hi = hi;
}

bool Libc::RangeInRam(uint64_t base, uint64_t len) {
    const uint64_t ram = Ram().Size();
    return base < ram && len < ram && base + len <= ram;
}

bool Libc::OverlapsProtected(uint64_t base, uint64_t len) {
    return g_prot_hi > g_prot_lo && base < g_prot_hi && base + len > g_prot_lo;
}

// memcpy: shipping contract -- memory = memmove, v0 = zext32(dest), nothing else.
bool Libc::Memcpy(CpuContext& ctx) {
    ps2r::ee::TickProfile::Mark();   // the overrun caller census (hot title path)
    auto* R = ctx.cpuRegs.GPR.r;
    const uint64_t d64 = R[4].UD[0], s64 = R[5].UD[0], n64 = R[6].UD[0];
    if (!RangeInRam(d64, n64) || !RangeInRam(s64, n64)) return false;
    if (OverlapsProtected(d64, n64)) return false;
    uint8_t* ram = Ram().Data();
    if (n64 != 0) std::memmove(ram + d64, ram + s64, static_cast<size_t>(n64));
    R[2].UD[0] = d64;
    return true;
}

// memmove: exact-register mirror of the SDK body. Path selection:
//   BACKWARD (src < dest < src+n): byte loop from the ends;
//     exit r2=dest r3=dest r4=-1 r5=src r6=-1 r7=src+n r8=dest.
//   FORWARD: n<0x20 or unaligned -> byte loop; else quad loop (n>>5 iterations,
//     the last leaves source residues in v1.UD[1]/v0.UD[1]), 8-byte loop, tail.
//     exit r2=dest r3=dest+n r4=-1 r6=-1 r8=dest; r5=src+n when n>0;
//     r7 = dest+(n&~7) on the quad path, else src+n when src<dest, else untouched.
bool Libc::Memmove(CpuContext& ctx) {
    auto* R = ctx.cpuRegs.GPR.r;
    const uint64_t d64 = R[4].UD[0], s64 = R[5].UD[0], n64 = R[6].UD[0];
    if (!RangeInRam(d64, n64) || !RangeInRam(s64, n64)) return false;
    if (OverlapsProtected(d64, n64)) return false;

    const uint32_t d = static_cast<uint32_t>(d64), s = static_cast<uint32_t>(s64),
                   n = static_cast<uint32_t>(n64);
    uint8_t* ram = Ram().Data();

    if (s64 < d64 && d64 < s64 + n64) {
        std::memmove(ram + d, ram + s, n);
        R[2].UD[0] = d64;
        R[3].UD[0] = d64;
        R[4].UD[0] = ~0ull;
        R[5].UD[0] = s64;
        R[6].UD[0] = ~0ull;
        R[7].UD[0] = s64 + n64;
        R[8].UD[0] = d64;
        return true;
    }

    const bool via_l1 = (s64 < d64);
    const bool quad = (n >= 0x20) && (((s64 | d64) & 0xfull) == 0);
    if (quad) {
        const uint32_t q = n >> 5;
        std::memcpy(&R[3].UD[1], ram + s + (q - 1) * 0x20 + 0x08, 8);
        std::memcpy(&R[2].UD[1], ram + s + (q - 1) * 0x20 + 0x18, 8);
    }
    if (n != 0) std::memmove(ram + d, ram + s, n);

    R[2].UD[0] = d64;
    R[3].UD[0] = d64 + n64;
    R[4].UD[0] = ~0ull;
    R[6].UD[0] = ~0ull;
    R[8].UD[0] = d64;
    if (n != 0) R[5].UD[0] = s64 + n64;
    if (quad) {
        R[7].UD[0] = d64 + (n & ~7u);
    } else if (via_l1) {
        R[7].UD[0] = s64 + n64;
    }
    return true;
}

// memset: M1 (n<8) byte loop: r2=dest r3=dest+n r6=-1. M2 (unaligned dest):
// adds r7=dest. M3 (16-aligned): pcpyh residues in v1.UD[1] (caller's stale
// t0.US[4] broadcast), t0 = p16 broadcast (128-bit when n>=0x20), r7=dest+(n&~7),
// r9=value, r10=(n<0x20).
bool Libc::Memset(CpuContext& ctx) {
    ps2r::ee::TickProfile::Mark();   // the overrun caller census (hot title path)
    auto* R = ctx.cpuRegs.GPR.r;
    const uint64_t d64 = R[4].UD[0], n64 = R[6].UD[0];
    if (!RangeInRam(d64, n64)) return false;
    if (OverlapsProtected(d64, n64)) return false;

    const uint32_t d = static_cast<uint32_t>(d64), n = static_cast<uint32_t>(n64);
    const uint8_t v = static_cast<uint8_t>(R[5].UD[0] & 0xffull);
    if (n != 0) std::memset(Ram().Data() + d, v, n);

    if (n64 < 8) {
        R[2].UD[0] = d64;
        R[3].UD[0] = d64 + n64;
        R[6].UD[0] = ~0ull;
    } else if ((d64 & 0xfull) != 0) {
        R[2].UD[0] = d64;
        R[3].UD[0] = d64 + n64;
        R[6].UD[0] = ~0ull;
        R[7].UD[0] = d64;
    } else {
        const uint64_t bc16 = 0x0001000100010001ull;
        const uint64_t p16 = static_cast<uint64_t>(v) * 0x101ull;
        const uint64_t hi16 = R[8].UD[1] & 0xffffull;
        if (n64 < 0x20) {
            R[8].UD[0] = p16;
        } else {
            R[8].UD[0] = p16 * bc16;
            R[8].UD[1] = p16 * bc16;
        }
        R[2].UD[0] = d64;
        R[3].UD[0] = d64 + n64;
        R[3].UD[1] = hi16 * bc16;
        R[6].UD[0] = ~0ull;
        R[7].UD[0] = d64 + (n & ~7u);
        R[9].UD[0] = static_cast<uint64_t>(v);
        R[10].UD[0] = (n64 < 0x20) ? 1ull : 0ull;
    }
    return true;
}

// strlen: SWAR mirror (ONES/M constants; NUL-in-word (w-ONES)&~w&M). S1 byte
// scan (s&7), S2 8-aligned ld loop, S3 16-aligned lq loop with MMI residues.
bool Libc::Strlen(CpuContext& ctx) {
    ps2r::ee::TickProfile::Mark();   // the overrun caller census (hot title path)
    auto* R = ctx.cpuRegs.GPR.r;
    const uint64_t s64 = R[4].UD[0];
    const uint64_t ram_size = Ram().Size();
    if (s64 >= ram_size) return false;
    const uint32_t s = static_cast<uint32_t>(s64);
    const uint8_t* base = Ram().Data();
    const void* nul = std::memchr(base + s, 0, static_cast<size_t>(ram_size - s64));
    if (nul == nullptr) return false;
    const uint32_t len = static_cast<uint32_t>(static_cast<const uint8_t*>(nul) - (base + s));
    if (s64 + len + 0x20 > ram_size) return false;

    constexpr uint64_t kOnes = 0x0101010101010101ull;
    constexpr uint64_t kMsb = 0x8080808080808080ull;

    if ((s64 & 7ull) != 0) {
        R[2].UD[0] = len;
        R[4].UD[0] = static_cast<uint64_t>(s + len);
        R[7].UD[0] = s64;
        return true;
    }
    if ((s64 & 0xfull) != 0) {
        const uint64_t w0 = Rd64(base, s);
        const uint64_t t0 = (w0 - kOnes) & ~w0 & kMsb;
        R[7].UD[0] = s64;
        if (t0 != 0) {
            R[2].UD[0] = len;
            R[3].UD[0] = ~w0;
            R[4].UD[0] = static_cast<uint64_t>(s + len);
            R[5].UD[0] = s64;
        } else {
            const uint32_t wbase = (s + len) & ~7u;
            const uint64_t wl = Rd64(base, wbase);
            R[2].UD[0] = len;
            R[3].UD[0] = ~wl;
            R[4].UD[0] = static_cast<uint64_t>(s + len);
            R[5].UD[0] = wbase;
            R[6].UD[0] = kOnes;
        }
        return true;
    }

    auto swar_pair = [&](uint32_t qaddr, uint64_t out_p[2], uint64_t out_t[2]) {
        for (int h = 0; h < 2; ++h) {
            const uint64_t q = Rd64(base, qaddr + 8u * static_cast<uint32_t>(h));
            uint64_t p = 0, t = 0;
            for (int i = 0; i < 8; ++i) {
                const uint8_t b = static_cast<uint8_t>(q >> (8 * i));
                const uint8_t pb = static_cast<uint8_t>(static_cast<uint8_t>(b - 1u) & ~b);
                p |= static_cast<uint64_t>(pb) << (8 * i);
                t |= static_cast<uint64_t>(pb & 0x80u) << (8 * i);
            }
            out_p[h] = p;
            out_t[h] = t;
        }
    };
    uint64_t p[2], t[2];
    if (len < 16) {
        swar_pair(s, p, t);
        R[2].UD[0] = len;
        R[2].UD[1] = t[1];
        R[3].UD[0] = t[1];
        R[3].UD[1] = kOnes;
        R[4].UD[0] = static_cast<uint64_t>(s + len);
        R[5].UD[0] = s64;
        R[6].UD[0] = t[1] | t[0];
        R[7].UD[0] = s64;
        R[8].UD[0] = kOnes;
        R[8].UD[1] = kOnes;
        R[9].UD[0] = kMsb;
        R[9].UD[1] = kMsb;
    } else {
        const uint32_t qbase = (s + len) & ~15u;
        const uint64_t entry_a2_hi = R[6].UD[1];
        swar_pair(qbase, p, t);
        R[2].UD[0] = len;
        R[2].UD[1] = p[1];
        R[3].UD[0] = t[1] | t[0];
        R[3].UD[1] = entry_a2_hi;
        R[4].UD[0] = static_cast<uint64_t>(s + len);
        R[4].UD[1] = t[1];
        R[5].UD[0] = qbase;
        R[6].UD[0] = 0;
        R[7].UD[0] = s64;
        R[8].UD[0] = kOnes;
        R[8].UD[1] = kOnes;
        R[9].UD[0] = kMsb;
        R[9].UD[1] = kMsb;
    }
    return true;
}
