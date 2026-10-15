#include "engine/ee/cpu/park_site_dump.h"

#include <cstdarg>
#include <cstdio>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/cpu/shadow_call_stack.h"

namespace {
const char* const kGprNames[32] = {
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0",   "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"};

char g_record[2048];

// snprintf-append: `pos` never passes `cap - 1`; a truncated piece stops there.
void Append(char* out, size_t cap, size_t& pos, const char* fmt, ...) {
    if (pos >= cap - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(out + pos, cap - pos, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    const size_t room = cap - 1 - pos;
    pos += static_cast<size_t>(n) < room ? static_cast<size_t>(n) : room;
}
}  // namespace

const char* ParkSiteDump::GprName(uint32_t index) { return index < 32 ? kGprNames[index] : "?"; }

size_t ParkSiteDump::Format(char* out, size_t cap, uint32_t target, const EmotionEngineState* ctx) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    size_t pos = 0;
    Append(out, cap, pos, "target=0x%x src=", target);

    // Jump-source candidates: every GPR but $zero holding the target (the R5900
    // jr/jalr read their target from one GPR; $zero is never that operand).
    bool any = false;
    if (ctx) {
        for (uint32_t i = 1; i < 32; ++i) {
            if (ctx->cpuRegs.GPR.r[i].UL[0] != target) continue;
            Append(out, cap, pos, any ? ",%s" : "%s", kGprNames[i]);
            any = true;
        }
    }
    if (!any) Append(out, cap, pos, "none");

    // The shadow chain, innermost first: frame 0 is the function containing the jump.
    Append(out, cap, pos, " chain=");
    ShadowCallStack::Frame f;
    uint32_t written = 0;
    for (uint32_t n = 0; n < kMaxFrames && ShadowCallStack::Top(n, f); ++n, ++written)
        Append(out, cap, pos, written ? "<-0x%x(ra=0x%x)" : "0x%x(ra=0x%x)", f.func_addr, f.ra);
    if (written == 0) Append(out, cap, pos, "none");
    if (written == kMaxFrames && ShadowCallStack::Top(kMaxFrames, f)) Append(out, cap, pos, "<-...");

    // The registers (low 32 bits), $zero omitted.
    Append(out, cap, pos, " gpr:");
    if (!ctx) {
        Append(out, cap, pos, "none");
    } else {
        for (uint32_t i = 1; i < 32; ++i)
            Append(out, cap, pos, i == 1 ? "%s=0x%x" : ",%s=0x%x", kGprNames[i], ctx->cpuRegs.GPR.r[i].UL[0]);
    }
    return pos;
}

const char* ParkSiteDump::FatalNoJump(uint32_t target, const EmotionEngineState* ctx) {
    Format(g_record, sizeof(g_record), target, ctx);
    return g_record;
}
