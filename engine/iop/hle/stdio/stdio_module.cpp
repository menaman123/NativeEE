// engine/iop/hle/stdio/stdio_module.cpp -- StdioModule bodies.
#include "engine/iop/hle/stdio/stdio_module.h"

#include <string>

#include "engine/core/log/log.h"

namespace ps2r::iop::hle {

void StdioModule::printf(IopContext& ctx) {
    std::string fmt;
    for (uint32_t a = ctx.gpr[4]; a != 0 && fmt.size() < 256; ++a) {
        const uint8_t c = mem_.Read<uint8_t>(a);
        if (c == 0) break;
        if (c != '\n') fmt.push_back(static_cast<char>(c));
    }
    calls_++;
    PS2R_LOG(IOP, Info, "iop printf: %s", fmt.c_str());
    ctx.gpr[2] = 0;
}

}  // namespace ps2r::iop::hle
