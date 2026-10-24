// tests/iop/test_sysmem_heap.cpp -- SysmemHeap vectors (PHASE4_PLAN section 4.1, ctest
// IopSysmemHeap). The IOP sysmem arena as the loaded module sees it: the heap floor
// sits right above the module image (its .bss ends at 0x20C80), the bump pointer skips
// the worker-stack band, and a block never enters the callback scratch stacks.
//
// Vectors:
//   1. Boot replay -- the measured boot allocation sequence ends with a largest free
//      block >= retail's 559,104 (the answer key the module reads at DAT_12ef0) and
//      never places a byte inside the stack band or the module image.
//   2. Dialogue-class start -- post-boot, a fresh 2x0x40000 ring pair passes the
//      262,144 gate and allocates.
//   3. Lifecycle -- 5 start/stop cycles return the free pool to baseline.
//   4. Band skip -- a straddling allocation jumps the band and parks the remainder.
//   5. Arena exhaustion -- Alloc fails with 0, the tail never enters the scratch stacks.
//   6. Free / coalesce / tail reclaim.
//   7. Savestate contract -- a restored bump tail above the band keeps working.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/iop/sysmem_heap.h"

using ps2r::iop::SysmemHeap;

static int g_failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

static constexpr uint32_t kModuleEnd    = 0x20C80;   // STREAM.IRX .bss end
static constexpr uint32_t kRetailFree   = 559104;    // PCSX2 island answer key
static constexpr uint32_t kCmd45Need    = 262144;    // music/dialogue env need
static constexpr uint32_t kRing         = 0x40000;

// One block must never overlap the module image, the stack band, or the
// callback-scratch-stack region above the ceiling.
static bool block_ok(uint32_t addr, uint32_t size) {
    if (addr == 0) return false;
    if (addr < kModuleEnd) return false;
    const uint32_t end = addr + size;
    if (addr < SysmemHeap::kStackBandHi &&
        end > SysmemHeap::kStackBandLo)
        return false;
    if (end > SysmemHeap::kCeiling) return false;
    return true;
}

// The exact boot allocation sizes measured on the OLD boot (order
// preserved; the 2048-byte header rings alloc+free around the big rings).
struct BootAlloc { uint32_t size; bool freed_immediately; };

int main() {
    // ---- Vector 1: boot replay --------------------------------------------
    {
        SysmemHeap h;
        std::vector<uint32_t> live;
        // Boot sequence (measured order): misc + data-stream rings, one
        // transient header ring, then the three 0x40000 music rings.
        const uint32_t boot_sizes[] = {1440, 4096, 131072, 65536, 2048};
        for (uint32_t sz : boot_sizes) {
            uint32_t a = h.Alloc(sz);
            CHECK(block_ok(a, (sz + 0xFF) & ~0xFFu));
            live.push_back(a);
        }
        // Transient header ring: alloc + free (cmd13/cmd15 class).
        {
            uint32_t a = h.Alloc(2048);
            CHECK(block_ok(a, 2048));
            CHECK(h.Free(a) == 2048);
        }
        // Three music rings (the boot cmd45-gated starts).
        uint32_t rings[3];
        for (int i = 0; i < 3; i++) {
            CHECK(h.QueryMaxFree() >= kCmd45Need);  // the cmd45 gate passes
            rings[i] = h.Alloc(kRing);
            CHECK(block_ok(rings[i], kRing));
        }
        const uint32_t post_boot_free = h.QueryMaxFree();
        std::printf("boot replay: heap_ptr=0x%X largest_free=%u (retail %u)\n",
                    h.HeapPtr(), post_boot_free, kRetailFree);
        CHECK(post_boot_free >= kRetailFree);

        // ---- Vector 2: dialogue-class start on the booted heap ------------
        CHECK(h.QueryMaxFree() >= kCmd45Need);
        uint32_t d1 = h.Alloc(kRing);
        uint32_t d2 = h.Alloc(kRing);
        CHECK(block_ok(d1, kRing));
        CHECK(block_ok(d2, kRing));

        // ---- Vector 3: 4+ start/stop cycles return to baseline ------------
        CHECK(h.Free(d1) == kRing);
        CHECK(h.Free(d2) == kRing);
        const uint32_t baseline = h.QueryMaxFree();
        for (int cycle = 0; cycle < 5; cycle++) {
            uint32_t a = h.Alloc(kRing);
            uint32_t b = h.Alloc(kRing);
            CHECK(block_ok(a, kRing));
            CHECK(block_ok(b, kRing));
            // header ring alloc/free ride-along (level-load class traffic)
            uint32_t hd = h.Alloc(2048);
            CHECK(h.Free(hd) == 2048);
            CHECK(h.Free(a) == kRing);
            CHECK(h.Free(b) == kRing);
            const uint32_t now = h.QueryMaxFree();
            std::printf("cycle %d: largest_free=%u (baseline %u)\n",
                        cycle + 1, now, baseline);
            CHECK(now == baseline);  // the descent pattern must be gone
        }
    }

    // ---- Vector 4: band skip parks the remainder --------------------------
    {
        SysmemHeap h;
        // Push the bump close to the band bottom.
        const uint32_t below = SysmemHeap::kStackBandLo - h.HeapPtr() - 0x100;
        uint32_t big = h.Alloc(below);
        CHECK(block_ok(big, below));
        // Next alloc would straddle the band: must land above it.
        uint32_t a = h.Alloc(0x1000);
        CHECK(a == SysmemHeap::kStackBandHi);
        CHECK(block_ok(a, 0x1000));
        // The 0x100 remainder must be reusable from the free list.
        uint32_t small = h.Alloc(0x100);
        CHECK(small == SysmemHeap::kStackBandLo - 0x100);
        CHECK(block_ok(small, 0x100));
    }

    // ---- Vector 5: ceiling exhaustion fails, no growth into scratch stacks
    {
        SysmemHeap h;
        std::vector<uint32_t> got;
        for (;;) {
            uint32_t a = h.Alloc(kRing);
            if (a == 0) break;
            CHECK(block_ok(a, kRing));
            got.push_back(a);
            CHECK(got.size() < 64);  // 2 MB / 256 KB = 8 max; runaway guard
        }
        CHECK(h.HeapPtr() <= SysmemHeap::kCeiling);
        // The tail may never enter the callback scratch stacks at
        // 0x1FFA00/0x1FFC00/0x1FFE00.
        CHECK(h.HeapPtr() <= 0x1FF800u);
        // Full-arena accounting: everything allocated stays in bounds and
        // freeing it all restores the boot-empty baseline.
        for (uint32_t a : got) CHECK(h.Free(a) == kRing);
        CHECK(h.HeapPtr() == SysmemHeap::kFloor ||
              h.QueryMaxFree() >= SysmemHeap::kCeiling -
                                      SysmemHeap::kStackBandHi);
    }

    // ---- Vector 6: free/coalesce/tail-reclaim (the heap contract) -------------
    {
        SysmemHeap h;
        uint32_t a = h.Alloc(0x1000);
        uint32_t b = h.Alloc(0x1000);
        uint32_t c = h.Alloc(0x1000);
        CHECK(h.Free(b) == 0x1000);           // hole
        CHECK(h.Free(a) == 0x1000);           // coalesce left
        uint32_t re = h.Alloc(0x2000);        // fits the coalesced hole
        CHECK(re == a);
        CHECK(h.Free(re) == 0x2000);
        CHECK(h.Free(c) == 0x1000);           // tail reclaim all the way
        CHECK(h.HeapPtr() == SysmemHeap::kFloor);
        CHECK(h.Free(0xDEAD0) == 0);          // unknown block -> no-op
    }

    // ---- Vector 8: the 256-byte allocation unit (SYSMEM's granularity) -----
    // A 0x38-byte request owns a 0x100-byte block: the next block starts 0x100
    // later, so a module writing 0x48 bytes into it (spiop's 989snd handler node)
    // never touches a neighbour; QueryBlockSize answers in units; Free returns them.
    {
        SysmemHeap h;
        uint32_t node = h.Alloc(0x38);
        uint32_t next = h.Alloc(0x10);
        CHECK(next == node + SysmemHeap::kBlockUnit);
        CHECK(h.BlockSize(node) == SysmemHeap::kBlockUnit);
        CHECK(h.Alloc(0x101) == next + SysmemHeap::kBlockUnit);
        CHECK(h.Free(node) == SysmemHeap::kBlockUnit);
        CHECK(h.Alloc(0) == node);  // a zero request still takes one unit (the hole fits)
        CHECK(h.QueryMaxFree() % SysmemHeap::kBlockUnit == 0);
    }

    // ---- Vector 7: restored bump tail from the pre-band layout ------------
    {
        SysmemHeap h;
        h.RestoreHeapPtr(0x1F15A0);  // measured post-boot bump tail of the pre-band layout
        uint32_t a = h.Alloc(0x2000);
        CHECK(a == 0x1F15A0);
        CHECK(block_ok(a, 0x2000));
        CHECK(h.Free(a) == 0x2000);
        CHECK(h.HeapPtr() == 0x1F15A0);
    }

    if (g_failures) {
        std::printf("%d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("all sysmem_heap vectors passed\n");
    return 0;
}
