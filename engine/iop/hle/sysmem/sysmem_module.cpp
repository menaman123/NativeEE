// engine/iop/hle/sysmem/sysmem_module.cpp -- SysmemModule bodies.
#include "engine/iop/hle/sysmem/sysmem_module.h"

#include <string>

#include "engine/core/log/log.h"

namespace ps2r::iop::hle {

void SysmemModule::AllocSysMemory(IopContext& ctx) {
    const uint32_t type = ctx.gpr[4];
    const uint32_t size = ctx.gpr[5];
    const uint32_t addr = heap_.Alloc(size);
    if (addr == 0) {
        PS2R_LOG(IOP, Warn, "AllocSysMemory(type=%u, size=%u) -> arena exhausted (heap_ptr=0x%08X)",
                 type, size, heap_.HeapPtr());
    } else {
        PS2R_LOG(IOP, Narrate, "AllocSysMemory(type=%u, size=%u) -> 0x%08X", type, size, addr);
    }
    ctx.gpr[2] = addr;
}

void SysmemModule::FreeSysMemory(IopContext& ctx) {
    const uint32_t addr = ctx.gpr[4];
    const uint32_t freed = heap_.Free(addr);
    if (freed == 0) {
        PS2R_LOG(IOP, Warn, "FreeSysMemory(0x%08X) -> unknown block, no-op", addr);
    } else {
        PS2R_LOG(IOP, Narrate, "FreeSysMemory(0x%08X) -> %u bytes (heap_ptr=0x%08X)", addr, freed, heap_.HeapPtr());
    }
    ctx.gpr[2] = 0;
}

void SysmemModule::QueryMaxFreeMemSize(IopContext& ctx) {
    ctx.gpr[2] = heap_.QueryMaxFree();
    PS2R_LOG(IOP, Narrate, "QueryMaxFreeMemSize() -> %u", ctx.gpr[2]);
}

void SysmemModule::Kprintf(IopContext& ctx) {
    // Only the format string is logged (the module's Kprintf calls carry no
    // arguments that change control flow; a full formatter is not needed).
    std::string fmt;
    for (uint32_t a = ctx.gpr[4]; a != 0 && fmt.size() < 256; ++a) {
        const uint8_t c = mem_.Read<uint8_t>(a);
        if (c == 0) break;
        if (c != '\n') fmt.push_back(static_cast<char>(c));
    }
    PS2R_LOG(IOP, Info, "Kprintf: %s", fmt.c_str());
    ctx.gpr[2] = 0;
}

}  // namespace ps2r::iop::hle
