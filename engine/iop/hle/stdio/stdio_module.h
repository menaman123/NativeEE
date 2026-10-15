// engine/iop/hle/stdio/stdio_module.h -- StdioModule: the IOP stdio library
// face: printf (idx 4). The format string is routed to the IOP log category
// (no console); the module's printf calls carry no control-flow effects.
#pragma once

#include <cstdint>

#include "engine/iop/iop_context.h"
#include "engine/iop/iop_memory.h"

namespace ps2r::iop::hle {

class StdioModule {
public:
    explicit StdioModule(IopMemory& mem) : mem_(mem) {}

    void printf(IopContext& ctx);   // int printf(const char* fmt, ...) -> 0

    uint64_t Calls() const { return calls_; }

private:
    IopMemory& mem_;
    uint64_t calls_ = 0;
};

}  // namespace ps2r::iop::hle
