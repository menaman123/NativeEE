// RecompDispatchTableLink -- links the title's generated code (ps2r_title_recomp)
// with the engine's seam providers and touches the dispatch surface through
// DispatchTable / ShadowCallStack (PHASE3_PLAN 3.4 rows 7, 8, 26). No boot: the
// only generated code that runs is initialize_recompiled_functions(). Also pins
// that a park never returns (GuestPark::Forever unwinds through a throwing sink).
// Link guard: this binary references BOTH RingLog (core) and the row-26 extern
// "C" trio (ee seam), so ring_log.o and seams.o are always pulled together --
// a second definition of the trio anywhere in the tree fails this link.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>

#include "engine/core/log/ring_log.h"
#include "engine/ee/cpu/dispatch_table.h"
#include "engine/ee/cpu/guest_park.h"
#include "engine/ee/cpu/shadow_call_stack.h"
#include "engine/ee/recomp_abi/recomp_abi.h"

extern "C" bool crashring_enabled;
extern "C" void crashring_record_indirect(uint32_t target, uint32_t ra);
extern "C" [[noreturn]] void crashring_fatal_no_jump(uint32_t pc);

using ps2r::ringlog::CH_EE;
using ps2r::ringlog::Ring;
using ps2r::ringlog::RingLog;

namespace {
struct ParkUnwind : std::exception {};
int g_failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}
}  // namespace

int main() {
    // Ring log configured FIRST (verbose EE on) with a scratch log dir: the fatal
    // report below writes ring_crash.rlog + crash_report.txt there.
    char log_dir[] = "/tmp/ps2r_dispatch_table_XXXXXX";
    check(::mkdtemp(log_dir) != nullptr, "scratch log dir");
    const uint32_t all = (1u << ps2r::ringlog::CH_COUNT) - 1;
    RingLog::Configure(all, 1u << CH_EE, 4096, log_dir);
    Ring* ring = RingLog::RegisterThread("main");
    check(ring != nullptr, "thread ring registered");
    check(!crashring_enabled, "gate is off before the dispatch surface is initialised");

    check(!DispatchTable::IsInitialized(), "not initialized before Initialize()");
    DispatchTable::Initialize();
    DispatchTable::Initialize();  // idempotent
    check(DispatchTable::IsInitialized(), "initialized after Initialize()");

    // Row 26 through the real providers: Initialize bound the gate to RingLog
    // (verbose EE on -> true), record_indirect lands on the EE channel, a later
    // Configure re-latches the gate.
    check(crashring_enabled, "Initialize latched crashring_enabled from the verbose EE bit");
    const uint64_t w0 = ring ? ring->wpos.load() : 0;
    crashring_record_indirect(0x00200000u, 0x00100008u);
    check(ring && ring->wpos.load() == w0 + 1, "crashring_record_indirect emits INDIRECT_JUMP");
    RingLog::Configure(all, 0, 4096, log_dir);
    check(!crashring_enabled, "Configure(verbose off) re-latches the gate off");
    crashring_record_indirect(0x00200000u, 0x00100008u);
    check(ring && ring->wpos.load() == w0 + 1, "record_indirect is a verbose-tier event");
    RingLog::Configure(all, 1u << CH_EE, 4096, log_dir);
    check(crashring_enabled, "Configure(verbose on) re-latches the gate on");

    const uint32_t n = DispatchTable::FunctionCount();
    std::printf("dispatch table: %u generated functions; first 0x%08x last 0x%08x\n", n,
                DispatchTable::AddressOfId(0), DispatchTable::AddressOfId(n - 1));
    // 8806 FUN_ + entry: the P8.9-A generation (BUG-072's delay-slot rule removed the 12
    // function starts that sat in a branch delay slot; was 8819 before).
    check(n == 8807, "8807 generated functions (8806 FUN_ + entry)");
    check(DispatchTable::AddressOfId(0) == 0x00100000u, "id 0 is the ELF base");
    check(DispatchTable::AddressOfId(n) == 0u, "out-of-range id -> 0");
    check(DispatchTable::IdOf(DispatchTable::AddressOfId(7)) == 7u, "IdOf(AddressOfId(7)) == 7");
    // Addresses ascend strictly.
    bool ascending = true;
    for (uint32_t i = 1; i < n; ++i)
        if (DispatchTable::AddressOfId(i) <= DispatchTable::AddressOfId(i - 1)) ascending = false;
    check(ascending, "dense id table ascends");

    // The frozen tool's raw fast path is gated by PS2_EE_RAWDISP=1 (recompiled.cpp:4302-4307,
    // DEFAULT OFF): without it hooked[] stays all-true after init = classic map dispatch
    // for every id. NEW never sets the variable, so every id reads hooked.
    uint32_t hooked_after_init = 0;
    for (uint32_t i = 0; i < n; ++i)
        if (DispatchTable::IsHooked(DispatchTable::AddressOfId(i))) ++hooked_after_init;
    std::printf("hooked after init: %u of %u (classic dispatch; PS2_EE_RAWDISP unset)\n", hooked_after_init, n);
    check(hooked_after_init == n, "classic dispatch: every id hooked after init");
    const uint32_t victim_id = 5;
    const uint32_t victim = DispatchTable::AddressOfId(victim_id);
    int hits = 0;
    uint32_t seen_addr = 0;
    check(DispatchTable::Install(victim, [&](CpuContext&, uint32_t a) { ++hits; seen_addr = a; }),
          "Install at a generated start succeeds");
    check(DispatchTable::IsHooked(victim), "victim still hooked after Install");
    check(!DispatchTable::Install(victim + 2, [](CpuContext&, uint32_t) {}), "Install off a start fails");
    CpuContext ctx{};
    DispatchTable::Invoke(victim, ctx);
    check(hits == 1 && seen_addr == victim, "Invoke runs the installed hook with its address");

    // Shadow call stack (storage owned by the generated common TU).
    check(ShadowCallStack::Depth() == 0, "shadow stack empty");
    check(ShadowCallStack::Push(0x00100008u, 0x00100010u), "push");
    ShadowCallStack::Frame f;
    check(ShadowCallStack::Depth() == 1 && ShadowCallStack::Top(0, f) && f.func_addr == 0x00100008u &&
              f.ra == 0x00100010u,
          "top frame reads back");
    ShadowCallStack::Pop();
    check(ShadowCallStack::Depth() == 0 && !ShadowCallStack::Top(0, f), "pop empties");

    // A park never returns: the sink throws, the caller's next line is unreachable.
    GuestPark::SetSink([](const ParkRecord&) { throw ParkUnwind{}; });
    bool unwound = false;
    try {
        crashring_fatal_no_jump(0x001f4940u);
        check(false, "crashring_fatal_no_jump returned");
    } catch (const ParkUnwind&) {
        unwound = true;
    }
    check(unwound && GuestPark::Count() == 1 && std::string(GuestPark::Last().reason) == "fatal_no_jump" &&
              GuestPark::Last().pc == 0x001f4940u,
          "park recorded reason + pc and unwound");
    GuestPark::SetSink(nullptr);
    // The report preceded the park: both crash artefacts exist in the log dir.
    struct stat st{};
    const std::string rlog = std::string(log_dir) + "/ring_crash.rlog";
    const std::string report = std::string(log_dir) + "/crash_report.txt";
    check(::stat(rlog.c_str(), &st) == 0 && st.st_size > 0, "ring_crash.rlog written before the park");
    check(::stat(report.c_str(), &st) == 0 && st.st_size > 0, "crash_report.txt written before the park");
    ::unlink(rlog.c_str());
    ::unlink(report.c_str());
    ::rmdir(log_dir);

    check(!RecompAbi::HasMemory(), "no memory bound in this test (no generated body runs)");

    if (g_failures) {
        std::printf("FAIL RecompDispatchTableLink: %d checks failed\n", g_failures);
        return 1;
    }
    std::printf("PASS RecompDispatchTableLink: %u functions, hook installed through update_function_impl, "
                "shadow stack round-trip, crash ring gate + record + report, park unwound\n", n);
    return 0;
}
