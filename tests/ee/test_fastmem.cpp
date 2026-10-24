// FastMemDiff: the inline fast path and the slow path agree on every access
// (port of the original tests/fastmem_diff grid: RAM low/mid/end straddle,
// extra-region boundaries, scratchpad + its high-bit pattern, HW window
// boundaries, KSEG0/KSEG1 mirrors, the 0x2xxxxxxx and 0x3xxxxxxx (UCAB) mirrors,
// unmapped ranges),
// plus the LQ/SQ quad mask and direct spot checks of where writes land.
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/ee/mem/scratchpad.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

namespace {

uint64_t g_seed = 0x1234567ull;
uint64_t Next() {
    uint64_t z = (g_seed += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

struct Pool {
    uint32_t base;
    uint32_t span;
    const char* name;
};

// The original's grid, complete: all 27 pools of tests/fastmem_diff
// (test_fastmem_diff.cpp:96-122), same bases, spans and names. The arena
// pools address the extra region a title registers at physical 0x02000000
// (here a 2 MB test buffer). IOP RAM (0x1C000000, 2 MB) has no backing in
// Phase 3 (no IOP), so both paths must agree on "unmapped" there; the KSEG1
// BIOS mirror and the 0xE0000000 segment exercise the physical mask
// (& 0x1FFFFFFF) landing in the BIOS window and in low RAM respectively; the
// 0x3 pools are the kernel's uncached-accelerated window (TLB-mapped onto RAM,
// MainMemory::Phys masks it with 0x0FFFFFFF) and its end past 32 MB.
const Pool kPools[] = {
    {0x00000000u, 0x00010000u, "ram_low"},
    {0x00100000u, 0x01E00000u, "ram_mid"},
    {0x01FFFFF0u, 0x00000020u, "ram_end_straddle"},
    {0x02000000u, 0x00000100u, "arena_lo_boundary"},
    {0x021FFFF0u, 0x00000020u, "arena_end_straddle"},
    {0x02100000u, 0x00100000u, "arena_mid"},
    {0x70000000u, 0x00004010u, "scratchpad"},
    {0xF0003FF0u, 0x00000020u, "scratchpad_hi_pattern"},
    {0x0FFFFFF0u, 0x00000020u, "hw_lo_boundary"},
    {0x10000000u, 0x00010010u, "ee_hw_regs"},
    {0x1000FFF0u, 0x00000020u, "io_hi_boundary"},
    {0x11000000u, 0x00010010u, "vu_windows"},
    {0x11FFFFF0u, 0x00000020u, "vu_gs_gap_boundary"},
    {0x12000000u, 0x00002010u, "gs_priv"},
    {0x1C000000u, 0x00000100u, "iop_ram_lo"},
    {0x1C1FFFF0u, 0x00000020u, "iop_ram_end"},
    {0x1FC00000u, 0x00000100u, "bios_lo"},
    {0x1FFFFFF0u, 0x00000010u, "bios_end"},
    {0x20000000u, 0x00000100u, "mirror_0x2_ram"},
    {0x20100000u, 0x00100000u, "mirror_0x2_ram_mid"},
    {0x30000000u, 0x01000000u, "ucab_ram"},
    {0x31FFFFF0u, 0x00000020u, "ucab_ram_end_straddle"},
    {0x32000000u, 0x00000100u, "ucab_arena_alias"},
    {0x3A000000u, 0x00000100u, "ucab_past_ram_unmapped"},
    {0x80000000u, 0x02000020u, "kseg0_ram"},
    {0xA0000000u, 0x02000020u, "kseg1_ram"},
    {0x90000000u, 0x00100000u, "kseg0_hw_alias"},
    {0x9FC00000u, 0x00000100u, "kseg0_bios_mirror"},
    {0xBFC00000u, 0x00000100u, "kseg1_bios_mirror"},
    {0xE0000000u, 0x00000100u, "high_seg_ram_wrap"},
};
static_assert(sizeof(kPools) / sizeof(kPools[0]) == 30, "port the original's 27 pools + the three UCAB pools, no fewer");

}  // namespace

int main() {
    MainMemory ram;
    Scratchpad spad;
    AddressMap map(ram, spad);
    std::vector<uint8_t> arena(2u * 1024u * 1024u, 0);
    map.RegisterExtraRegion(0x02000000u, static_cast<uint32_t>(arena.size()), arena.data(), "test_arena");
    MmioSink sink;
    MmioRouter mmio(sink);
    FastMem fm(ram, map, mmio);

    // Seed RAM, scratchpad and the arena with a pseudo-random pattern so reads
    // have something to disagree about.
    for (uint32_t i = 0; i < MainMemory::kSize; i += 8) {
        const uint64_t v = Next();
        std::memcpy(ram.Data() + i, &v, 8);
    }
    for (uint32_t i = 0; i < Scratchpad::kSize; i += 8) {
        const uint64_t v = Next();
        std::memcpy(spad.Data() + i, &v, 8);
    }
    for (size_t i = 0; i < arena.size(); i += 8) {
        const uint64_t v = Next();
        std::memcpy(arena.data() + i, &v, 8);
    }

    // 1. Read agreement per width over the grid.
    size_t reads = 0, fast_served = 0;
    for (const Pool& p : kPools) {
        for (int n = 0; n < 400; ++n) {
            const uint32_t a = p.base + static_cast<uint32_t>(Next() % p.span);
            CHECK(fm.Read<uint8_t>(a) == fm.ReadSlow<uint8_t>(a));
            CHECK(fm.Read<uint16_t>(a & ~1u) == fm.ReadSlow<uint16_t>(a & ~1u));
            CHECK(fm.Read<uint32_t>(a & ~3u) == fm.ReadSlow<uint32_t>(a & ~3u));
            CHECK(fm.Read<uint64_t>(a & ~7u) == fm.ReadSlow<uint64_t>(a & ~7u));
            QuadWord q1{}, q2{};
            fm.ReadQuad(a, q1);
            fm.ReadQuadSlow(a, q2);
            CHECK(std::memcmp(&q1, &q2, 16) == 0);
            reads += 5;
            if (FastMem::FastPath(a, 1)) ++fast_served;
        }
    }

    // 2. Write agreement: write through the fast path, read back through the
    //    slow path, and vice versa, at random grid addresses.
    for (const Pool& p : kPools) {
        for (int n = 0; n < 200; ++n) {
            const uint32_t a = (p.base + static_cast<uint32_t>(Next() % p.span)) & ~7u;
            const uint64_t v = Next();
            if (MmioRouter::IsIoRegister(a)) continue;  // sink drops writes: nothing to read back
            fm.Write<uint64_t>(a, v);
            CHECK(fm.ReadSlow<uint64_t>(a) == fm.Read<uint64_t>(a));
            const uint32_t w = static_cast<uint32_t>(Next());
            fm.WriteSlow<uint32_t>(a, w);
            CHECK(fm.Read<uint32_t>(a) == fm.ReadSlow<uint32_t>(a));
            if (map.Translate(a & ~0xFu, 16) == nullptr) continue;  // unmapped: the write drops
            QuadWord q{};
            q.u64[0] = Next();
            q.u64[1] = Next();
            fm.WriteQuad(a | 0xC, q);  // low 4 bits ignored
            QuadWord r{};
            fm.ReadQuadSlow(a & ~0xFu, r);
            CHECK(std::memcmp(&q, &r, 16) == 0);
        }
    }

    // 3. Direct spot checks: where writes land.
    fm.Write<uint32_t>(0x80001000u, 0xAABBCCDDu);  // KSEG0 -> phys 0x1000
    CHECK(fm.Read<uint32_t>(0x00001000u) == 0xAABBCCDDu);
    CHECK(fm.Read<uint32_t>(0xA0001000u) == 0xAABBCCDDu);  // KSEG1 sees it too
    fm.Write<uint32_t>(0x20002000u, 0x11223344u);  // 0x2xxxxxxx wraps into RAM
    CHECK(fm.Read<uint32_t>(0x00002000u) == 0x11223344u);
    fm.Write<uint16_t>(0x70000010u, 0xBEEF);  // scratchpad
    CHECK(std::memcmp(spad.Data() + 0x10, "\xEF\xBE", 2) == 0);
    fm.Write<uint8_t>(0xF0003FFFu, 0x5A);  // high-bit pattern hits the scratchpad's last byte
    CHECK(spad.Data()[0x3FFF] == 0x5A);
    fm.Write<uint32_t>(0x02000010u, 0xC0FFEE00u);  // registered extra region
    CHECK(std::memcmp(arena.data() + 0x10, "\x00\xEE\xFF\xC0", 4) == 0);
    // The UCAB window (0x3...): the kernel's TLB maps it onto RAM, so a display
    // list built through it (sq via 0x30322340) is what the DMAC later reads at
    // physical 0x322340 -- and what a KUSEG/KSEG0 load of the same byte sees.
    fm.Write<uint64_t>(0x30322340u, 0x0000000010000000ull);
    CHECK(fm.Read<uint64_t>(0x00322340u) == 0x0000000010000000ull);
    CHECK(fm.Read<uint64_t>(0x80322340u) == 0x0000000010000000ull);
    CHECK(fm.Read<uint64_t>(0x30322340u) == 0x0000000010000000ull);
    CHECK(FastMem::FastPath(0x30322340u, 8));
    CHECK(MainMemory::Phys(0x30322340u) == 0x00322340u);
    CHECK(MainMemory::Phys(0x31FFFFF0u) == 0x01FFFFF0u);
    CHECK(map.Translate(0x30322340u, 8) == ram.Data() + 0x322340u);
    QuadWord uq{};
    uq.u64[0] = 0x0032234050000000ull;  // a CALL tag, stored through the window
    uq.u64[1] = 0;
    fm.WriteQuad(0x30330A20u, uq);
    QuadWord ur{};
    fm.ReadQuad(0x00330A20u, ur);
    CHECK(std::memcmp(&uq, &ur, 16) == 0);
    // Past the 32 MB of RAM the window keeps mirroring the physical space (the
    // registered extra region at 0x02000000, like the 0x2 mirror does) and is
    // unmapped beyond it.
    CHECK(!FastMem::FastPath(0x32000000u, 4));
    CHECK(map.Translate(0x32000010u, 4) == arena.data() + 0x10);
    CHECK(fm.Read<uint32_t>(0x3A000000u) == 0);  // unmapped reads 0
    fm.Write<uint32_t>(0x3A000000u, 0xDEADBEEFu);  // ... and writes drop
    CHECK(fm.Read<uint32_t>(0x3A000000u) == 0);
    CHECK(map.Translate(0x3A000000u, 4) == nullptr);
    CHECK(!FastMem::FastPath(0x31FFFFFEu, 4));  // straddles the RAM end through the window
    CHECK(fm.Read<uint32_t>(0x31FFFFFEu) == 0);
    CHECK(fm.Read<uint32_t>(0x11000000u) == 0);  // no VU window registered yet -> unmapped
    // The three pools the first port dropped, pinned directly:
    fm.Write<uint32_t>(0xE0001000u, 0x0E0E0E0Eu);  // high segment: phys = addr & 0x1FFFFFFF -> low RAM
    CHECK(fm.Read<uint32_t>(0x00001000u) == 0x0E0E0E0Eu);
    CHECK(FastMem::FastPath(0xE0001000u, 4));
    fm.Write<uint32_t>(0x1FC00010u, 0xB105B105u);  // BIOS window, reached through all three mirrors
    CHECK(fm.Read<uint32_t>(0x9FC00010u) == 0xB105B105u);
    CHECK(fm.Read<uint32_t>(0xBFC00010u) == 0xB105B105u);
    CHECK(!FastMem::FastPath(0xBFC00010u, 4));
    CHECK(fm.Read<uint32_t>(0x1C1FFFF0u) == 0);  // IOP RAM: no backing in Phase 3 -> unmapped, reads 0
    fm.Write<uint32_t>(0x1C1FFFF0u, 0x10101010u);
    CHECK(fm.Read<uint32_t>(0x1C1FFFF0u) == 0);
    CHECK(map.Translate(0x1C1FFFF0u, 4) == nullptr);

    // 4. The quad mask: LQ/SQ ignore the low four address bits.
    QuadWord q{};
    q.u64[0] = 0x0102030405060708ull;
    q.u64[1] = 0x090A0B0C0D0E0F10ull;
    fm.WriteQuad(0x00003007u, q);
    QuadWord r{};
    fm.ReadQuad(0x0000300Fu, r);
    CHECK(std::memcmp(&q, &r, 16) == 0);
    CHECK(fm.Read<uint64_t>(0x00003000u) == q.u64[0]);
    // A quad whose masked address is the last 16 bytes of RAM is served fast;
    // one whose *unmasked* address straddles is still masked first.
    CHECK(FastMem::FastPath(0x01FFFFF0u, 16));
    fm.WriteQuad(0x01FFFFFFu, q);
    fm.ReadQuad(0x01FFFFF0u, r);
    CHECK(std::memcmp(&q, &r, 16) == 0);
    // A 4-byte access straddling the RAM end takes the slow path (unmapped -> 0).
    CHECK(!FastMem::FastPath(0x01FFFFFEu, 4));
    CHECK(fm.Read<uint32_t>(0x01FFFFFEu) == 0);

    // 5. MMIO goes to the router: the sink's idle values, and pages are counted.
    CHECK(fm.Read<uint32_t>(0x12001000u) == 0x8u);       // GS_CSR idle
    CHECK(fm.Read<uint32_t>(0x1000F520u) == 0x0u);       // D_ENABLER idle
    CHECK(fm.Read<uint64_t>(0x12001000u) == 0x8ull);     // 64-bit view: low word
    CHECK(fm.Read<uint8_t>(0x12001000u) == 0x8u);
    CHECK(fm.Read<uint8_t>(0x12001001u) == 0x0u);
    fm.Write<uint32_t>(0x10008000u, 0x100u);  // CHCR.STR write is accepted and dropped
    CHECK(fm.Read<uint32_t>(0x10008000u) == 0);
    CHECK(mmio.UnhandledPageCount() >= 3);

    // The write observer (Debug builds; I8.7-A's --watch-word mechanism): installed
    // over [lo, hi) it sees every Write<T>/WriteQuad touching the range -- with
    // the OLD bits read before the store -- and nothing outside it; unarmed it
    // is inert. Release builds compile the check out: the case is skipped there.
#ifndef NDEBUG
    {
        struct Seen {
            int calls = 0;
            uint32_t last_addr = 0;
            unsigned last_width = 0;
            uint64_t last_old = 0, last_new = 0;
        } seen;
        const uint32_t lo = 0x00480000u, hi = 0x00480010u;
        fm.Write<uint32_t>(lo, 0x11111111u);  // unarmed: no observer yet
        CHECK(!FastMem::HasWriteObserver());
        FastMem::SetWriteObserver(
            [](void* user, uint32_t addr, unsigned width, uint64_t old_bits, uint64_t new_bits) {
                auto* s = static_cast<Seen*>(user);
                ++s->calls;
                s->last_addr = addr;
                s->last_width = width;
                s->last_old = old_bits;
                s->last_new = new_bits;
            },
            &seen, lo, hi);
        CHECK(FastMem::HasWriteObserver());
        fm.Write<uint32_t>(lo, 0x22222222u);                 // inside
        CHECK(seen.calls == 1 && seen.last_addr == lo && seen.last_width == 4);
        CHECK(seen.last_old == 0x11111111u && seen.last_new == 0x22222222u);  // old bits read before the store
        fm.Write<uint8_t>(lo + 0xf, 0x5au);                  // last byte inside
        CHECK(seen.calls == 2 && seen.last_width == 1);
        fm.Write<uint16_t>(hi - 1, 0xbeefu);                 // straddles hi: first byte inside
        CHECK(seen.calls == 3 && seen.last_addr == hi - 1 && seen.last_width == 2);
        fm.Write<uint64_t>(lo - 4, 0x0123456789abcdefull);  // straddles lo
        CHECK(seen.calls == 4 && seen.last_addr == lo - 4 && seen.last_width == 8);
        fm.Write<uint32_t>(hi, 0x33333333u);                 // outside (hi exclusive)
        fm.Write<uint32_t>(lo - 4, 0x44444444u);             // outside (ends at lo)
        fm.Write<uint32_t>(0x00700000u, 0x55555555u);        // far away
        CHECK(seen.calls == 4);
        QuadWord q{};
        std::memset(&q, 0x77, sizeof q);
        fm.WriteQuad(hi - 16, q);                            // quad [hi-16, hi): inside, as two 8-byte halves
        CHECK(seen.calls == 6 && seen.last_width == 8 && seen.last_addr == hi - 8);
        fm.WriteQuad(hi, q);                                 // quad starting at hi: outside
        CHECK(seen.calls == 6);
        FastMem::SetWriteObserver(nullptr, nullptr, 0, 0);
        CHECK(!FastMem::HasWriteObserver());
        fm.Write<uint32_t>(lo, 0x66666666u);                 // disarmed: inert
        CHECK(seen.calls == 6);
        CHECK(fm.Read<uint32_t>(lo) == 0x66666666u);
    }
#else
    std::printf("FastMem write observer case skipped (NDEBUG)\n");
#endif

    if (g_fail) {
        std::printf("FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("PASS FastMemDiff: %zu pools, %zu grid reads agree (fast path served %zu/%zu sample addresses), %zu unhandled MMIO pages\n",
                sizeof(kPools) / sizeof(kPools[0]), reads, fast_served, reads / 5, mmio.UnhandledPageCount());
    return 0;
}
