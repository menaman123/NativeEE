// engine/iop/hle/sysmem/sysmem_module.h -- SysmemModule: the IOP kernel's
// sysmem library as the recompiled module sees it: AllocSysMemory (idx 4),
// FreeSysMemory (5), QueryMemSize (6), QueryMaxFreeMemSize (7),
// QueryTotalFreeMemSize (8), QueryBlockTopAddress (9), QueryBlockSize (10),
// Kprintf (14), each taking the
// R3000 register file (arguments in $a0..$a3, result in $v0) over a SysmemHeap.
// Also the owner of the heap's savestate restore (heap_ptr -> RestoreHeapPtr).
#pragma once

#include <cstdint>

#include "engine/iop/iop_context.h"
#include "engine/iop/iop_memory.h"
#include "engine/iop/sysmem_heap.h"

namespace ps2r::iop::hle {

class SysmemModule {
public:
    explicit SysmemModule(IopMemory& mem) : mem_(mem) {}
    SysmemModule(IopMemory& mem, const SysmemHeap::Layout& layout) : mem_(mem), heap_(layout) {}

    // int AllocSysMemory(int type, u32 size, void* addr) -> block address or 0.
    void AllocSysMemory(IopContext& ctx);
    // int FreeSysMemory(void* addr) -> 0.
    void FreeSysMemory(IopContext& ctx);
    // u32 QueryMaxFreeMemSize() -> largest single allocatable block.
    void QueryMaxFreeMemSize(IopContext& ctx);
    // int Kprintf(const char* fmt, ...) -> 0; the format string goes to the log.
    void Kprintf(IopContext& ctx);
    // u32 QueryMemSize() -> the IOP RAM size (idx 6).
    void QueryMemSize(IopContext& ctx);
    // u32 QueryTotalFreeMemSize() -> the heap's free total (idx 8; the largest block here).
    void QueryTotalFreeMemSize(IopContext& ctx);
    // void* QueryBlockTopAddress(void* addr) -> the block's base (idx 9; the address as given).
    void QueryBlockTopAddress(IopContext& ctx);
    // int QueryBlockSize(void* addr) -> the block's size (idx 10; 0 when unknown).
    void QueryBlockSize(IopContext& ctx);

    SysmemHeap& Heap() { return heap_; }
    const SysmemHeap& Heap() const { return heap_; }
    // Savestate restore of the serialized bump tail (IOPM Statics.heap_ptr).
    void RestoreHeapPtr(uint32_t heap_ptr) { heap_.RestoreHeapPtr(heap_ptr); }

private:
    IopMemory& mem_;
    SysmemHeap heap_;
};

}  // namespace ps2r::iop::hle
