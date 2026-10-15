// engine/iop/hle/sysmem/sysmem_module.h -- SysmemModule: the IOP kernel's
// sysmem library as the recompiled module sees it: AllocSysMemory (idx 4),
// FreeSysMemory (5), QueryMaxFreeMemSize (7), Kprintf (14), each taking the
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

    // int AllocSysMemory(int type, u32 size, void* addr) -> block address or 0.
    void AllocSysMemory(IopContext& ctx);
    // int FreeSysMemory(void* addr) -> 0.
    void FreeSysMemory(IopContext& ctx);
    // u32 QueryMaxFreeMemSize() -> largest single allocatable block.
    void QueryMaxFreeMemSize(IopContext& ctx);
    // int Kprintf(const char* fmt, ...) -> 0; the format string goes to the log.
    void Kprintf(IopContext& ctx);

    SysmemHeap& Heap() { return heap_; }
    const SysmemHeap& Heap() const { return heap_; }
    // Savestate restore of the serialized bump tail (IOPM Statics.heap_ptr).
    void RestoreHeapPtr(uint32_t heap_ptr) { heap_.RestoreHeapPtr(heap_ptr); }

private:
    IopMemory& mem_;
    SysmemHeap heap_;
};

}  // namespace ps2r::iop::hle
