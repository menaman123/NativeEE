// ParkSiteDump: the fatal_no_jump park record names the jump-source register
// candidates, the shadow chain (innermost first) and the GPRs. The shadow stack
// accessors the generated common TU owns are stubbed here (no title link).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/cpu/park_site_dump.h"

namespace {
struct Frame {
    uint32_t func, ra;
};
Frame g_frames[16];
uint32_t g_depth = 0;
}  // namespace

extern "C" uint32_t ee_get_call_stack_depth() { return g_depth; }
extern "C" bool ee_get_call_stack_top(uint32_t n, uint32_t* out_func_addr, uint32_t* out_ra) {
    if (n >= g_depth) return false;
    const Frame& f = g_frames[g_depth - 1 - n];
    if (out_func_addr) *out_func_addr = f.func;
    if (out_ra) *out_ra = f.ra;
    return true;
}
extern "C" bool ee_call_stack_push(uint32_t func_addr, uint32_t ra) {
    if (g_depth >= 16) return false;
    g_frames[g_depth++] = {func_addr, ra};
    return true;
}
extern "C" void ee_call_stack_pop() {
    if (g_depth) --g_depth;
}

namespace {
int g_fails = 0;
void Check(bool ok, const char* name, const std::string& got = std::string()) {
    std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, got.empty() ? "" : " got=", got.c_str());
    if (!ok) ++g_fails;
}
bool Has(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }
}  // namespace

int main() {
    CpuContext ctx{};
    char buf[2048];

    // No chain, no ctx: every half says none.
    ParkSiteDump::Format(buf, sizeof(buf), 0, nullptr);
    std::string s = buf;
    Check(s == "target=0x0 src=none chain=none gpr:none", "psd_empty_record", s);

    // A jr through t9 = 0 from FUN_0027edf0 called by FUN_00282098: t9 and
    // every other zero register are the candidates, the chain is innermost first.
    ee_call_stack_push(0x17ce50, 0x100f08);
    ee_call_stack_push(0x282098, 0x2927f0 + 0x40);
    ee_call_stack_push(0x27edf0, 0x2820b8);
    for (uint32_t i = 1; i < 32; ++i) ctx.cpuRegs.GPR.r[i].UL[0] = 0x1000 + i;  // nothing zero
    ctx.cpuRegs.GPR.r[25].UL[0] = 0;        // t9: the jump register
    ctx.cpuRegs.GPR.r[2].UL[0] = 0xbb3560;  // v0: the object base
    ParkSiteDump::Format(buf, sizeof(buf), 0, &ctx);
    s = buf;
    Check(Has(s, "target=0x0 src=t9 "), "psd_single_zero_register_is_the_source", s);
    Check(Has(s, "chain=0x27edf0(ra=0x2820b8)<-0x282098(ra=0x292830)<-0x17ce50(ra=0x100f08) "), "psd_chain_innermost_first", s);
    Check(Has(s, "gpr:at=0x1001,v0=0xbb3560,v1=0x1003,"), "psd_gpr_dump_starts_at_at", s);
    Check(Has(s, ",t9=0x0,"), "psd_gpr_dump_names_t9", s);
    Check(Has(s, ",ra=0x101f") && s.back() == 'f', "psd_gpr_dump_ends_with_ra", s);

    // Several registers hold the target: all are listed, in index order.
    ctx.cpuRegs.GPR.r[8].UL[0] = 0;   // t0
    ctx.cpuRegs.GPR.r[31].UL[0] = 0;  // ra
    ParkSiteDump::Format(buf, sizeof(buf), 0, &ctx);
    s = buf;
    Check(Has(s, " src=t0,t9,ra "), "psd_every_matching_register_listed", s);

    // A non-zero target is matched the same way.
    ctx.cpuRegs.GPR.r[3].UL[0] = 0xdeadbee0;
    ParkSiteDump::Format(buf, sizeof(buf), 0xdeadbee0, &ctx);
    s = buf;
    Check(Has(s, "target=0xdeadbee0 src=v1 "), "psd_nonzero_target_matched", s);

    // Deep chains are cut at kMaxFrames with an ellipsis.
    for (uint32_t i = 0; i < 13; ++i) ee_call_stack_push(0x200000 + i * 8, 0x300000 + i * 8);
    ParkSiteDump::Format(buf, sizeof(buf), 0, &ctx);
    s = buf;
    Check(Has(s, "<-...") , "psd_chain_cut_with_ellipsis", s.substr(0, 120));

    // Truncation never overruns the buffer.
    char small[40];
    std::memset(small, 'X', sizeof(small));
    const size_t n = ParkSiteDump::Format(small, sizeof(small), 0, &ctx);
    Check(n == sizeof(small) - 1 && small[sizeof(small) - 1] == '\0', "psd_truncated_at_cap", std::string(small));

    // The process-lived record equals Format's text.
    const char* rec = ParkSiteDump::FatalNoJump(0, &ctx);
    Check(rec && std::string(rec) == s, "psd_fatal_record_equals_format");
    Check(std::string(ParkSiteDump::GprName(25)) == "t9" && std::string(ParkSiteDump::GprName(32)) == "?", "psd_gpr_names");

    std::printf(g_fails ? "FAILED %d\n" : "ALL PASS\n", g_fails);
    return g_fails ? 1 : 0;
}
