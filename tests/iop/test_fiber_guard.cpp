// tests/iop/test_fiber_guard.cpp -- the guest-stack guard page of the platform
// fiber layer (ctest FiberGuardPage). Every IOP guest thread is a coroutine on a
// fiber stack; a stack overrun must FAULT (guard page) instead of silently
// corrupting the neighbouring allocation. Each faulting probe runs in a forked
// child so the parent can assert the termination signal:
//   1. a write into the page just below the usable stack faults (the guard
//      exists and sits where the stack would grow into);
//   2. a runaway recursion inside a fiber faults instead of returning;
//   3. (positive) a fiber that uses most of its stack legitimately and switches
//      back completes without a fault.
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

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
void* g_host = nullptr;

void SwitchBackToHost() {
    ps2r::platform::Current().fiber_switch(ps2r::platform::Fiber{g_host});
}

// Probe 1: touch the guard page directly. The local sits near the top of the
// usable stack, so stack_bytes + one word below it is inside the guard.
void EntryTouchGuard(void*) {
    volatile char anchor = 0;
    // This is a raw virtual-address probe. Subtracting from &anchor as an
    // array pointer is undefined and lets Release remove the faulting access.
    const uintptr_t address = reinterpret_cast<uintptr_t>(&anchor) - (kStackBytes + 64);
    volatile char* below = reinterpret_cast<volatile char*>(address);
    *below = 1;          // must fault
    anchor = *below;
    SwitchBackToHost();  // not reached with a guard page
    for (;;) SwitchBackToHost();
}

// Probe 2: runaway recursion with page-sized frames. The frame is read AFTER
// the recursive call: every level's frame stays live across it, so no compiler
// can turn the recursion into a loop (GCC 13 -O3 rewrote the former
// `return Recurse(depth + 1) + 1` with an accumulator -- constant stack, no
// fault, the probe exited 2 on the Linux GCC lane).
volatile int g_sink = 0;
int Recurse(int depth) {
    volatile char frame[4096];
    frame[0] = static_cast<char>(depth);
    frame[4095] = static_cast<char>(depth);
    g_sink += frame[0] + frame[4095];
    if (depth > 1000000) return depth;
    const int below = Recurse(depth + 1);
    g_sink += frame[4095];
    return below + 1;
}
void EntryRecurse(void*) {
    g_sink = Recurse(0);
    SwitchBackToHost();  // not reached with a guard page
    for (;;) SwitchBackToHost();
}

// Probe 3: legitimate deep use (three quarters of the stack) then switch back.
volatile bool g_probe3_done = false;
void EntryDeepUse(void*) {
    volatile char frame[kStackBytes * 3 / 4];
    for (size_t i = 0; i < sizeof(frame); i += 512) frame[i] = static_cast<char>(i);
    g_sink = frame[0] + frame[sizeof(frame) - 512];
    g_probe3_done = true;
    for (;;) SwitchBackToHost();
}

// Run `entry` on a fresh fiber inside a forked child; return the child's wait
// status. -1 if fork failed.
int RunInChild(void (*entry)(void*)) {
    std::fflush(stdout);
    const pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        const ps2r::platform::Platform& p = ps2r::platform::Current();
        g_host = p.fiber_current().handle;
        ps2r::platform::Fiber f = p.fiber_create(entry, nullptr, kStackBytes);
        if (!f.handle) _exit(3);
        p.fiber_switch(f);
        _exit(g_probe3_done ? 0 : 2);   // came back: 0 only for the positive probe
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {}
    return status;
}

bool DiedOfMemoryFault(int status) {
    return WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS);
}

}  // namespace

int main() {
    ps2r::platform::host::RegisterSelf();
    const ps2r::platform::Platform& p = ps2r::platform::Current();
    CHECK(p.caps.has_fibers && p.fiber_create && p.fiber_switch && p.fiber_current,
          "platform exposes the fiber entries");

    const int s1 = RunInChild(&EntryTouchGuard);
    std::printf("probe1 status=0x%x signaled=%d sig=%d\n", s1, WIFSIGNALED(s1) ? 1 : 0,
                WIFSIGNALED(s1) ? WTERMSIG(s1) : 0);
    CHECK(DiedOfMemoryFault(s1), "a write just below the usable stack faults (guard page present)");

    const int s2 = RunInChild(&EntryRecurse);
    std::printf("probe2 status=0x%x signaled=%d sig=%d\n", s2, WIFSIGNALED(s2) ? 1 : 0,
                WIFSIGNALED(s2) ? WTERMSIG(s2) : 0);
    CHECK(DiedOfMemoryFault(s2), "a runaway recursion in a fiber faults instead of corrupting");

    const int s3 = RunInChild(&EntryDeepUse);
    std::printf("probe3 status=0x%x exited=%d code=%d\n", s3, WIFEXITED(s3) ? 1 : 0,
                WIFEXITED(s3) ? WEXITSTATUS(s3) : -1);
    CHECK(WIFEXITED(s3) && WEXITSTATUS(s3) == 0,
          "legitimate use of three quarters of the stack completes and switches back");

    // Create/Destroy round trip in-process (no switch): the mapping is released.
    for (int i = 0; i < 16; i++) {
        ps2r::platform::Fiber f = p.fiber_create(&EntryDeepUse, nullptr, kStackBytes);
        if (!f.handle) { g_failures++; std::printf("FAIL: fiber_create %d\n", i); break; }
        p.fiber_destroy(f);
    }
    CHECK(true, "16 create/destroy round trips");

    std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures);
    return g_failures ? 1 : 0;
}
