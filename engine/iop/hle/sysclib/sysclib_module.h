// engine/iop/hle/sysclib/sysclib_module.h -- SysclibModule: the IOP sysclib
// library face the recompiled module imports, bound BY ORDINAL to the real
// sysclib export table (PCSX2 IopModuleNames): 12 memcpy, 14 memset, 20 strcat,
// 23 strcpy, 27 strlen, 29 strncmp, 34 strstr. Every routine works on IOP RAM
// (pointer arguments are IOP addresses; pointer results are IOP addresses, 0 for
// null). C-string operations are bounded by the end of the 2 MB RAM.
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/iop/iop_context.h"
#include "engine/iop/iop_memory.h"

namespace ps2r::iop::hle {

class SysclibModule {
public:
    using Stub = void (SysclibModule::*)(IopContext&);
    struct OrdinalEntry {
        uint32_t ordinal;
        const char* name;
        Stub stub;
    };
    static constexpr size_t kOrdinalCount = 7;

    explicit SysclibModule(IopMemory& mem) : mem_(mem) {}

    void memcpy(IopContext& ctx);    // ordinal 12: void* memcpy(dst, src, n) -> dst
    void memset(IopContext& ctx);    // ordinal 14: void* memset(dst, c, n) -> dst
    void strcat(IopContext& ctx);    // ordinal 20: char* strcat(dst, src) -> dst
    void strcpy(IopContext& ctx);    // ordinal 23: char* strcpy(dst, src) -> dst
    void strlen(IopContext& ctx);    // ordinal 27: size_t strlen(s)
    void strncmp(IopContext& ctx);   // ordinal 29: int strncmp(a, b, n)
    void strstr(IopContext& ctx);    // ordinal 34: char* strstr(hay, needle) -> address or 0

    // The ordinal -> name/stub table (the IopSysclibMap test's subject).
    static const OrdinalEntry* OrdinalTable();
    static const OrdinalEntry* FindOrdinal(uint32_t ordinal);

private:
    // Bounded length of the C string at `addr` (stops at the end of RAM).
    size_t CStringLength(uint32_t addr) const;
    // Bytes strncmp may examine at `addr`: min(n, strlen + 1), never past the end of RAM.
    size_t CompareLength(uint32_t addr, uint32_t n) const;

    IopMemory& mem_;
};

}  // namespace ps2r::iop::hle
