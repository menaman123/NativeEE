// Vu0Trace: the VU0 macro-mode operand census mechanism. (1) Parse accepts a
// hex/decimal list and truncates each pc to its 8-byte bundle, rejects an
// empty item, a non-number and a pc beyond micro memory; (2) an unarmed trace
// wants nothing; (3) through VuMacroBridge a one-bundle E-bit program on an
// armed pc records the entry block (VF01..VF31, ACC, Q, I) and the exit block
// with the result lane changed (vaddx vf01 = vf02 + vf03.x), a call on an
// unarmed pc records nothing, the summary counts per pc, Flush writes the
// lines, the program's bundles are recorded once (through the bundle after
// the E bit) with the
// first call; (4) SetTrace(nullptr) turns it off.
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/vu/macro/vu_macro_bridge.h"
#include "engine/vu/vu0/vu0_trace.h"
#include "engine/vu/vu_unit.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

namespace {

void PutBundle(uint8_t* micro, uint32_t pc, uint32_t lower, uint32_t upper) {
    std::memcpy(micro + pc, &lower, 4);
    std::memcpy(micro + pc + 4, &upper, 4);
}

std::string ReadAll(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return std::string();
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

size_t Count(const std::string& hay, const std::string& needle) {
    size_t n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1)) ++n;
    return n;
}

void TestParse() {
    Vu0Trace t;
    std::string err;
    CHECK(!t.Armed());
    CHECK(!t.Wants(0x100));
    CHECK(t.Parse("0x790,0x548,1644", &err));
    CHECK(t.Armed());
    CHECK(t.Pcs().size() == 3);
    CHECK(t.Pcs()[0] == 0x790 && t.Pcs()[1] == 0x548 && t.Pcs()[2] == 0x668);
    CHECK(t.Wants(0x790) && t.Wants(0x794));  // the bundle, not the byte
    CHECK(!t.Wants(0x798));
    CHECK(t.Parse("0x7a3", &err) && t.Pcs()[0] == 0x7a0);
    CHECK(!t.Parse("0x790,,0x548", &err) && err.find("empty item") != std::string::npos);
    CHECK(!t.Parse("0x790,abc", &err) && err.find("expected") != std::string::npos);
    CHECK(!t.Parse("0x10000", &err));
    CHECK(!t.Parse("", &err));
    CHECK(t.Summary() == "0x7a0:0");  // the last successful parse stands
}

void TestBridgeTrace() {
    VuUnit vu0(0), vu1(1);
    Vu1State& st = vu0.state();
    // pc 0x100: vaddx.xyzw vf01, vf02, vf03x + E bit; lower NOP.
    // upper: func 0x00 (ADDx), fd=1, fs=2, ft=3, dest=0xF, E=bit30.
    const uint32_t upper = 0x40000000u | (0xFu << 21) | (3u << 16) | (2u << 11) | (1u << 6) | 0x00u;
    PutBundle(st.micro, 0x100, 0x8000033Cu, upper);
    PutBundle(st.micro, 0x108, 0x8000033Cu, 0x000002FFu);
    PutBundle(st.micro, 0x200, 0x8000033Cu, 0x000002FFu | 0x40000000u);  // NOP + E at the unarmed pc

    Vu0Trace trace;
    std::string err;
    CHECK(trace.Parse("0x100", &err));
    trace.SetBoundary(7);
    VuMacroBridge::Bind(&vu0, &vu1, nullptr);
    VuMacroBridge::SetTrace(&trace);

    CpuContext ctx{};
    ctx.vuRegs.Reset();
    ctx.vuRegs.VF[2].F[0] = 1.0f; ctx.vuRegs.VF[2].F[1] = 2.0f; ctx.vuRegs.VF[2].F[2] = 3.0f; ctx.vuRegs.VF[2].F[3] = 4.0f;
    ctx.vuRegs.VF[3].F[0] = 0.5f;
    ctx.vuRegs.Q = 2.0f;
    ctx.vuRegs.VF[31].F[0] = 8.0f;  // a high register: the whole file is recorded
    ctx.cpuRegs.GPR.r[4].UL[0] = 0x70003fa0u;   // a0
    ctx.cpuRegs.GPR.r[8].UL[0] = 0x70003fa0u;   // t0
    ctx.cpuRegs.GPR.r[29].UL[0] = 0x70003f90u;  // sp
    VuMacroBridge::ExecuteVu0(ctx, 0x100);
    CHECK(ctx.vuRegs.VF[1].F[0] == 1.5f && ctx.vuRegs.VF[1].F[1] == 2.5f && ctx.vuRegs.VF[1].F[3] == 4.5f);
    VuMacroBridge::ExecuteVu0(ctx, 0x200);  // unarmed: counted by the bridge, not the trace
    CHECK(trace.Calls() == 1);
    CHECK(trace.Summary() == "0x100:1");

    const char* path = "vu0_trace_test.log";
    CHECK(trace.Flush(path));
    const std::string log = ReadAll(path);
    std::remove(path);
    CHECK(log.find("vu0call=1:pc=0x100:b7\n") == 0);
    CHECK(Count(log, " in  vf") == 31 && Count(log, " out vf") == 31);
    CHECK(log.find(" in  vf01=(0,0,0,0) hex=00000000 00000000 00000000 00000000") != std::string::npos);
    CHECK(log.find(" in  vf02=(1,2,3,4) hex=3f800000 40000000 40400000 40800000") != std::string::npos);
    CHECK(log.find(" out vf01=(1.5,2.5,3.5,4.5) hex=3fc00000 40200000 40600000 40900000") != std::string::npos);
    CHECK(log.find(" in  q=2 hex=40000000") != std::string::npos);
    CHECK(log.find(" in  vf31=(8,0,0,0) hex=41000000 00000000 00000000 00000000") != std::string::npos);
    CHECK(log.find(" in  flags status=0000 mac=0000 clip=000000") != std::string::npos);
    CHECK(log.find(" in  ee a0=70003fa0 a1=00000000 a2=00000000 a3=00000000 t0=70003fa0 t1=00000000 t2=00000000 "
                   "t3=00000000 sp=70003f90") != std::string::npos);
    CHECK(log.find(" in  i=") != std::string::npos && log.find(" out i=") == std::string::npos);
    CHECK(log.find("vu0end=1:pc=0x100\n") != std::string::npos);
    CHECK(Count(log, " code 0") == 2);  // the E-bit bundle and the one the VU still runs after it
    CHECK(log.find(" code 0108 lo=8000033c up=000002ff") != std::string::npos);
    CHECK(log.find(" code 0100 lo=8000033c up=") != std::string::npos);
    CHECK(log.find("pc=0x200") == std::string::npos);

    VuMacroBridge::SetTrace(nullptr);
    VuMacroBridge::ExecuteVu0(ctx, 0x100);
    CHECK(trace.Calls() == 1);
    VuMacroBridge::Unbind();
}

}  // namespace

int main() {
    TestParse();
    TestBridgeTrace();
    if (g_fail) {
        std::printf("Vu0Trace: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("Vu0Trace: all checks passed\n");
    return 0;
}
