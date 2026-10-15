// tests/iop/test_sysclib_map.cpp -- IopSysclibMap: the sysclib ordinal table the
// recompiled STREAM.IRX imports {12 memcpy, 14 memset, 20 strcat, 23 strcpy,
// 27 strlen, 29 strncmp, 34 strstr} (the real export table; PHASE4_PLAN 2.3 (g)),
// each stub exercised over IOP RAM with IOP addresses in/out; plus the small
// kernel-facing modules landed with it (sysmem over the heap, intrman, stdio).
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/iop/hle/intrman/intrman_module.h"
#include "engine/iop/hle/stdio/stdio_module.h"
#include "engine/iop/hle/sysclib/sysclib_module.h"
#include "engine/iop/hle/sysmem/sysmem_module.h"

using namespace ps2r::iop;
using namespace ps2r::iop::hle;

static int g_fail = 0;
#define CHECK(c, msg)                                                          \
    do {                                                                       \
        if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); g_fail++; } \
    } while (0)

static void PutStr(IopMemory& m, uint32_t addr, const char* s) {
    for (size_t i = 0; i <= std::strlen(s); ++i) m.Write<uint8_t>(addr + static_cast<uint32_t>(i), static_cast<uint8_t>(s[i]));
}
static std::string GetStr(IopMemory& m, uint32_t addr) {
    std::string s;
    for (uint32_t a = addr; s.size() < 512; ++a) {
        const uint8_t c = m.Read<uint8_t>(a);
        if (!c) break;
        s.push_back(static_cast<char>(c));
    }
    return s;
}

static void TestOrdinalTable() {
    const uint32_t expect_ord[SysclibModule::kOrdinalCount] = {12, 14, 20, 23, 27, 29, 34};
    const char* expect_name[SysclibModule::kOrdinalCount] = {"memcpy", "memset", "strcat", "strcpy", "strlen", "strncmp", "strstr"};
    const auto* t = SysclibModule::OrdinalTable();
    for (size_t i = 0; i < SysclibModule::kOrdinalCount; ++i) {
        CHECK(t[i].ordinal == expect_ord[i], "ordinal in table order");
        CHECK(std::strcmp(t[i].name, expect_name[i]) == 0, "name bound to ordinal");
        CHECK(t[i].stub != nullptr, "stub bound");
        CHECK(SysclibModule::FindOrdinal(expect_ord[i]) == &t[i], "FindOrdinal hits the row");
    }
    // The pre-S44 mis-bound names are NOT ordinals of this module.
    CHECK(SysclibModule::FindOrdinal(2) == nullptr && SysclibModule::FindOrdinal(0) == nullptr, "unlisted ordinals are absent");
}

static void TestStubs() {
    IopMemory mem;
    SysclibModule sc(mem);
    IopContext ctx;
    const uint32_t A = 0x1000, B = 0x2000, C = 0x3000;
    PutStr(mem, A, "hello");
    PutStr(mem, B, " world");
    // strlen
    ctx.gpr[4] = A; sc.strlen(ctx); CHECK(ctx.gpr[2] == 5, "strlen(hello)=5");
    // strcat -> "hello world", returns dst
    ctx.gpr[4] = A; ctx.gpr[5] = B; sc.strcat(ctx);
    CHECK(ctx.gpr[2] == A && GetStr(mem, A) == "hello world", "strcat appends and returns dst");
    // strcpy
    ctx.gpr[4] = C; ctx.gpr[5] = A; sc.strcpy(ctx);
    CHECK(ctx.gpr[2] == C && GetStr(mem, C) == "hello world", "strcpy copies and returns dst");
    // strncmp
    ctx.gpr[4] = A; ctx.gpr[5] = C; ctx.gpr[6] = 32; sc.strncmp(ctx); CHECK(ctx.gpr[2] == 0, "strncmp equal -> 0");
    PutStr(mem, B, "hello there");
    ctx.gpr[4] = A; ctx.gpr[5] = B; ctx.gpr[6] = 6; sc.strncmp(ctx); CHECK(ctx.gpr[2] == 0, "strncmp equal prefix within n -> 0");
    ctx.gpr[6] = 32; sc.strncmp(ctx); CHECK(static_cast<int32_t>(ctx.gpr[2]) > 0, "strncmp 'w' > 't'");
    // strstr -> IOP address of the hit, 0 for a miss
    PutStr(mem, B, "world");
    ctx.gpr[4] = A; ctx.gpr[5] = B; sc.strstr(ctx); CHECK(ctx.gpr[2] == A + 6, "strstr returns the IOP address of the hit");
    PutStr(mem, B, "zzz");
    sc.strstr(ctx); CHECK(ctx.gpr[2] == 0, "strstr miss -> 0");
    // memset / memcpy
    ctx.gpr[4] = C; ctx.gpr[5] = 0x1AB; ctx.gpr[6] = 8; sc.memset(ctx);
    CHECK(ctx.gpr[2] == C && mem.Read<uint8_t>(C) == 0xAB && mem.Read<uint8_t>(C + 7) == 0xAB && mem.Read<uint8_t>(C + 8) == 'r',
          "memset fills n bytes with the low byte and returns dst");
    ctx.gpr[4] = B; ctx.gpr[5] = C; ctx.gpr[6] = 8; sc.memcpy(ctx);
    CHECK(ctx.gpr[2] == B && mem.Read<uint8_t>(B + 7) == 0xAB, "memcpy copies n bytes and returns dst");
    // Out-of-range pointer results are 0 / no crash: string at the very end of RAM.
    const uint32_t end = IopMemory::kSize - 4;
    mem.Write<uint32_t>(end, 0x41414141u);
    ctx.gpr[4] = end; sc.strlen(ctx); CHECK(ctx.gpr[2] == 4, "strlen is bounded by the end of RAM");
    // strncmp against the unterminated RAM-end string never reads past the last byte:
    // "AAAA" (no terminator available) vs "AAAA\0" at A — equal within n=4, and the
    // terminated operand sorts first when n reaches the missing terminator.
    mem.Write<uint32_t>(A, 0x41414141u); mem.Write<uint8_t>(A + 4, 0);
    ctx.gpr[4] = end; ctx.gpr[5] = A; ctx.gpr[6] = 4; sc.strncmp(ctx); CHECK(ctx.gpr[2] == 0, "strncmp RAM-end vs terminated, n=4 -> 0");
    ctx.gpr[6] = 32; sc.strncmp(ctx); CHECK(static_cast<int32_t>(ctx.gpr[2]) == 0, "strncmp RAM-end (4 bytes) vs 'AAAA\\0': exhausted RAM compares equal");
    ctx.gpr[4] = A; ctx.gpr[5] = end; sc.strncmp(ctx); CHECK(static_cast<int32_t>(ctx.gpr[2]) == 0, "strncmp terminated vs RAM-end: symmetric");
    mem.Write<uint8_t>(A + 3, 'B');
    sc.strncmp(ctx); CHECK(static_cast<int32_t>(ctx.gpr[2]) > 0, "strncmp 'AAAB' > RAM-end 'AAAA' (compare stays inside RAM)");
}

static void TestSmallModules() {
    IopMemory mem;
    IopContext ctx;
    SysmemModule sm(mem);
    ctx.gpr[4] = 0; ctx.gpr[5] = 0x100; sm.AllocSysMemory(ctx);
    const uint32_t a = ctx.gpr[2];
    CHECK(a == SysmemHeap::kFloor, "first AllocSysMemory returns the heap floor");
    ctx.gpr[4] = a; sm.FreeSysMemory(ctx); CHECK(ctx.gpr[2] == 0, "FreeSysMemory -> 0");
    sm.QueryMaxFreeMemSize(ctx); CHECK(ctx.gpr[2] == sm.Heap().QueryMaxFree() && ctx.gpr[2] > 0, "QueryMaxFreeMemSize = heap largest free");
    sm.RestoreHeapPtr(0x140000); CHECK(sm.Heap().HeapPtr() == 0x140000, "RestoreHeapPtr applies the IOPM heap_ptr");
    PutStr(mem, 0x4000, "kp\n"); ctx.gpr[4] = 0x4000; sm.Kprintf(ctx); CHECK(ctx.gpr[2] == 0, "Kprintf -> 0");

    IntrmanModule im(mem);
    mem.Write<uint32_t>(0x5000, 0xFFFFFFFFu);
    ctx.gpr[4] = 0x5000; im.CpuSuspendIntr(ctx);
    CHECK(ctx.gpr[2] == 0 && mem.Read<uint32_t>(0x5000) == 1 && im.SuspendCount() == 1, "CpuSuspendIntr stores the enabled state word and returns KE_OK");
    ctx.gpr[4] = mem.Read<uint32_t>(0x5000); im.CpuResumeIntr(ctx); CHECK(ctx.gpr[2] == 0 && im.Enabled(), "CpuResumeIntr restores saved state");

    StdioModule so(mem);
    ctx.gpr[4] = 0x4000; so.printf(ctx); CHECK(ctx.gpr[2] == 0 && so.Calls() == 1, "stdio printf -> 0, counted");
}

int main() {
    TestOrdinalTable();
    TestStubs();
    TestSmallModules();
    if (g_fail) { std::printf("IopSysclibMap: %d FAILED\n", g_fail); return 1; }
    std::printf("IopSysclibMap: all checks PASS (7 ordinals; sysmem/intrman/stdio faces)\n");
    return 0;
}
