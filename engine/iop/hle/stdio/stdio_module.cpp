// engine/iop/hle/stdio/stdio_module.cpp -- StdioModule bodies.
#include "engine/iop/hle/stdio/stdio_module.h"

#include <cstdio>
#include <string>

#include "engine/core/log/log.h"

namespace ps2r::iop::hle {

namespace {

// The module's variadic arguments under the MIPS o32 convention: a1..a3, then the
// caller's stack from sp+0x10 (one word each).
uint32_t VarArg(const IopMemory& mem, const IopContext& ctx, unsigned index) {
    if (index < 3) return ctx.gpr[5 + index];
    return mem.Read<uint32_t>(ctx.gpr[29] + 0x10 + (index - 3) * 4);
}

std::string GuestString(const IopMemory& mem, uint32_t addr, size_t limit) {
    std::string s;
    for (uint32_t a = addr; a != 0 && s.size() < limit; ++a) {
        const uint8_t c = mem.Read<uint8_t>(a);
        if (c == 0) break;
        s.push_back(static_cast<char>(c));
    }
    return s;
}

}  // namespace

void StdioModule::printf(IopContext& ctx) {
    const std::string fmt = GuestString(mem_, ctx.gpr[4], 512);
    // Expand %d %i %u %x %X %c %s %p %% (flags / width / precision kept) so a hosted
    // module's diagnostics read as they would on the console's TTY.
    std::string out;
    unsigned arg = 0;
    for (size_t i = 0; i < fmt.size(); ++i) {
        const char c = fmt[i];
        if (c != '%') {
            if (c != '\n' && c != '\r') out.push_back(c);
            else if (c == '\n') out.push_back('|');
            continue;
        }
        std::string spec = "%";
        size_t j = i + 1;
        while (j < fmt.size() && (fmt[j] == '-' || fmt[j] == '+' || fmt[j] == ' ' || fmt[j] == '#' || fmt[j] == '0' ||
                                  fmt[j] == '.' || (fmt[j] >= '1' && fmt[j] <= '9') || fmt[j] == 'l' || fmt[j] == 'h'))
            spec.push_back(fmt[j++]);
        if (j >= fmt.size()) break;
        const char conv = fmt[j];
        i = j;
        char buf[64];
        switch (conv) {
            case '%': out.push_back('%'); break;
            case 'd': case 'i':
                spec.push_back('d');
                std::snprintf(buf, sizeof(buf), spec.c_str(), static_cast<int32_t>(VarArg(mem_, ctx, arg++)));
                out += buf;
                break;
            case 'u': case 'x': case 'X': case 'o':
                spec.push_back(conv);
                std::snprintf(buf, sizeof(buf), spec.c_str(), VarArg(mem_, ctx, arg++));
                out += buf;
                break;
            case 'p':
                std::snprintf(buf, sizeof(buf), "0x%08x", VarArg(mem_, ctx, arg++));
                out += buf;
                break;
            case 'c':
                out.push_back(static_cast<char>(VarArg(mem_, ctx, arg++) & 0xFF));
                break;
            case 's':
                out += GuestString(mem_, VarArg(mem_, ctx, arg++), 256);
                break;
            default:
                out += spec;
                out.push_back(conv);
                break;
        }
    }
    calls_++;
    PS2R_LOG(IOP, Info, "iop printf: %s", out.c_str());
    ctx.gpr[2] = 0;
}

}  // namespace ps2r::iop::hle
