// tests/iop/test_fiber_migrate.cpp -- a fiber resumed from a SECOND host thread
// (ctest FiberHostMigration). The IOP kernel's coroutines are driven by
// whichever host thread runs IopKernel::Dispatch: the EE main thread at the
// frame boundary, any EE guest thread at a back-edge yield or an RPC drive. A
// coroutine therefore parks on host thread A and is resumed on host thread B.
// The platform fiber layer must keep every host thread's "current context"
// record its own across such a migration:
//   1. after B resumes the fiber and the fiber parks back to B, A's
//      fiber_current() is still A's host record (a thread_local written through
//      an address cached across swapcontext would land in A's slot: BUG the
//      a second title's FMV exposed -- the next Dispatch on A then saved A's context INTO
//      the fiber's record and B later jumped onto A's stack);
//   2. the fiber keeps running correctly when A and B alternate resuming it
//      (its own saved context is never overwritten by a host context).
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "engine/platform/host/platform.h"
#include "engine/platform/platform.h"

static int g_failures = 0;
#define CHECK(cond, name)                                        \
    do {                                                         \
        if (cond) std::printf("PASS: %s\n", name);               \
        else { std::printf("FAIL: %s (line %d)\n", name, __LINE__); g_failures++; } \
    } while (0)

namespace {

constexpr size_t kStackBytes = 64 * 1024;

// The dispatcher context, as IopKernel::Dispatch records it: the host record
// of the thread that is switching into the fiber right now.
std::atomic<void*> g_dispatch{nullptr};
std::atomic<int> g_runs{0};

// The coroutine body: count a run, park back to whoever resumed us. Like an IOP
// guest thread it never returns.
void FiberBody(void*) {
    const ps2r::platform::Platform& p = ps2r::platform::Current();
    for (;;) {
        g_runs.fetch_add(1, std::memory_order_acq_rel);
        p.fiber_switch(ps2r::platform::Fiber{g_dispatch.load(std::memory_order_acquire)});
    }
}

// One dispatcher step on the calling host thread: record ourselves as the
// dispatcher, run the fiber until it parks. Returns true if the fiber ran once.
bool RunFiberOnce(ps2r::platform::Fiber f) {
    const ps2r::platform::Platform& p = ps2r::platform::Current();
    const int before = g_runs.load(std::memory_order_acquire);
    g_dispatch.store(p.fiber_current().handle, std::memory_order_release);
    p.fiber_switch(f);
    return g_runs.load(std::memory_order_acquire) == before + 1;
}

}  // namespace

int main() {
    ps2r::platform::host::RegisterSelf();
    const ps2r::platform::Platform& p = ps2r::platform::Current();
    CHECK(p.caps.has_fibers && p.fiber_create && p.fiber_switch && p.fiber_current,
          "platform exposes the fiber entries");

    ps2r::platform::Fiber f = p.fiber_create(&FiberBody, nullptr, kStackBytes);
    CHECK(f.handle != nullptr, "fiber created");
    void* const host_a = p.fiber_current().handle;
    CHECK(host_a != nullptr, "host thread A has its own record");

    // A runs the fiber first: the fiber parks on A.
    CHECK(RunFiberOnce(f), "fiber ran once from host thread A");
    CHECK(p.fiber_current().handle == host_a, "A's current record is A's after the first run");

    // B resumes the fiber (migration A -> B); the fiber parks back to B.
    bool ran_on_b = false;
    void* host_b = nullptr;
    bool b_record_intact = false;
    std::thread b([&] {
        host_b = p.fiber_current().handle;
        ran_on_b = RunFiberOnce(f);
        b_record_intact = (p.fiber_current().handle == host_b);
    });
    b.join();
    CHECK(ran_on_b, "fiber ran once from host thread B");
    CHECK(host_b != nullptr && host_b != host_a, "B has its own host record");
    CHECK(b_record_intact, "B's current record is B's after the fiber parked back to B");
    // (1) the migration must not have touched A's slot.
    CHECK(p.fiber_current().handle == host_a,
          "A's current record is still A's after the fiber migrated to B and parked there");

    // (2) alternate A / B / A / B: every step runs the fiber exactly once.
    bool alternation_ok = true;
    for (int i = 0; i < 8 && alternation_ok; i++) {
        if (!RunFiberOnce(f)) { alternation_ok = false; break; }
        bool ran = false;
        std::thread t([&] { ran = RunFiberOnce(f); });
        t.join();
        if (!ran) alternation_ok = false;
    }
    CHECK(alternation_ok, "fiber runs exactly once per resume while A and B alternate (8 rounds)");
    CHECK(g_runs.load() == 2 + 16, "run count matches the number of resumes");
    CHECK(p.fiber_current().handle == host_a, "A's current record is A's at the end");

    std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures);
    return g_failures ? 1 : 0;
}
