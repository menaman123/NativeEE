// engine/iop/sysmem_heap.h -- the IOP kernel's sysmem arena as the loaded
// module sees it through AllocSysMemory / FreeSysMemory / QueryMaxFreeMemSize.
// Layout of the 2 MB IOP RAM this runtime uses:
//   [0, kFloor)                      module image (text/rodata/data/bss)
//   [kFloor, ...)                    heap: bump tail + first-fit free list
//   [kStackBandLo, kStackBandHi)     the guest worker-stack band (StartThread
//                                    places sp = kStackBandHi - tid*0x4000): never
//                                    handed out -- the bump pointer skips it and
//                                    parks the remainder below it on the free list
//   [..., kCeiling)                  heap continues above the band; kCeiling leaves
//                                    the callback scratch stacks at the top of RAM
//                                    (which grow down) 2 KiB of guard.
// Blocks are 16-byte rounded; Free coalesces byte-adjacent neighbours and
// reclaims a free tail; QueryMaxFree is the largest single allocatable block
// (the real QueryMaxFreeMemSize contract). Savestate contract: only heap_ptr
// is serialized; the maps are transient (a restored heap starts with an empty
// free list, and an unknown Free is a no-op).
#pragma once

#include <cstdint>
#include <map>

namespace ps2r::iop {

class SysmemHeap {
public:
    static constexpr uint32_t kArenaSize   = 2u * 1024u * 1024u;
    static constexpr uint32_t kCeiling     = kArenaSize - 0x800;
    static constexpr uint32_t kFloor       = 0x28000;
    static constexpr uint32_t kStackBandLo = 0xE0000;
    static constexpr uint32_t kStackBandHi = 1024u * 1024u;
    static_assert(kCeiling <= kArenaSize, "ceiling inside the arena");
    static_assert(kFloor < kStackBandLo && kStackBandLo < kStackBandHi && kStackBandHi < kCeiling,
                  "floor < stack band < ceiling");

    // The arena's layout as the composition root sets it (the defaults are the
    // constants above): a root hosting several modules raises the floor past
    // their images and sizes the stack band for their threads -- title data,
    // passed in, never engine constants.
    struct Layout {
        uint32_t floor = kFloor;
        uint32_t stack_band_lo = kStackBandLo;
        uint32_t stack_band_hi = kStackBandHi;
        uint32_t ceiling = kCeiling;
    };

    SysmemHeap() : SysmemHeap(Layout{}) {}
    explicit SysmemHeap(const Layout& layout);
    const Layout& GetLayout() const { return layout_; }

    // Allocated address, or 0 on arena exhaustion (the real kernel's failure shape).
    uint32_t Alloc(uint32_t size);
    // Freed rounded size, or 0 for an unknown block (no-op).
    uint32_t Free(uint32_t addr);
    uint32_t QueryMaxFree() const;
    // The rounded size of the live block at `addr` (0 = not a live block).
    uint32_t BlockSize(uint32_t addr) const;

    uint32_t HeapPtr() const { return heap_ptr_; }
    // Savestate restore: set the serialized bump tail and drop the transient maps.
    void RestoreHeapPtr(uint32_t heap_ptr);
    void ResetTransient();
    size_t LiveBlocks() const { return alloc_sizes_.size(); }

private:
    Layout layout_;
    uint32_t heap_ptr_ = kFloor;
    std::map<uint32_t, uint32_t> alloc_sizes_;   // addr -> rounded size
    std::map<uint32_t, uint32_t> free_blocks_;   // addr -> size (coalesced)
};

}  // namespace ps2r::iop
