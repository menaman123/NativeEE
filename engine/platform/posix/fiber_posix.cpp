// platform/posix/fiber_posix.cpp -- ucontext fibers (macOS + Linux). The
// functions are marked deprecated by the macOS SDK but remain the only
// libc-level user-space context switch; the warning is silenced for this one
// TU (clang only -- glibc's ucontext carries no deprecation).
//
// Guest stacks are mmap'd with a PROT_NONE guard page below the usable range
// (stacks grow down): a guest thread that overruns its stack faults at once
// instead of silently corrupting whatever the allocator placed next to it.
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE 1
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "engine/platform/posix/fiber_posix.h"

#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace ps2r::platform {
namespace {

struct FiberRecord {
    ucontext_t ctx{};
    uint8_t*   mapping = nullptr;   // null for a host thread's own record
    size_t     mapping_size = 0;    // guard + usable stack
    size_t     guard_size = 0;
    void     (*entry)(void*) = nullptr;
    void*      arg = nullptr;
};

thread_local FiberRecord* t_current = nullptr;

size_t PageSize() {
    static const size_t page = [] {
        const long p = sysconf(_SC_PAGESIZE);
        return p > 0 ? static_cast<size_t>(p) : static_cast<size_t>(16384);
    }();
    return page;
}

size_t RoundUpToPage(size_t n) {
    const size_t page = PageSize();
    return (n + page - 1) / page * page;
}

FiberRecord* HostRecord() {
    if (!t_current) {
        auto* r = new FiberRecord();
        if (getcontext(&r->ctx) != 0) {
            std::fprintf(stderr, "platform: getcontext failed for the host thread\n");
            std::abort();
        }
        t_current = r;
    }
    return t_current;
}

// makecontext passes int arguments only: the record pointer travels as two
// 32-bit halves.
void Trampoline(unsigned lo, unsigned hi) {
    const uintptr_t bits = (uintptr_t)lo | ((uintptr_t)hi << 32);
    auto* r = reinterpret_cast<FiberRecord*>(bits);
    r->entry(r->arg);
    std::fprintf(stderr, "platform: fiber entry returned; a fiber body must park forever\n");
    std::abort();
}

}  // namespace

Fiber FiberPosix::Create(void (*fn)(void*), void* arg, size_t stack) {
    Fiber f;
    if (!fn) return f;
    if (stack < 64 * 1024) stack = 64 * 1024;
    stack = RoundUpToPage(stack);
    const size_t guard = PageSize();
    auto* r = new FiberRecord();
    r->mapping_size = guard + stack;
    r->guard_size = guard;
    void* m = mmap(nullptr, r->mapping_size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) {
        delete r;
        return f;
    }
    r->mapping = static_cast<uint8_t*>(m);
    // The lowest page is the guard: any access below the usable stack faults.
    if (mprotect(r->mapping, guard, PROT_NONE) != 0) {
        munmap(r->mapping, r->mapping_size);
        delete r;
        return f;
    }
    r->entry = fn;
    r->arg = arg;
    if (getcontext(&r->ctx) != 0) {
        munmap(r->mapping, r->mapping_size);
        delete r;
        return f;
    }
    r->ctx.uc_stack.ss_sp = r->mapping + guard;
    r->ctx.uc_stack.ss_size = stack;
    r->ctx.uc_link = nullptr;
    const uintptr_t bits = reinterpret_cast<uintptr_t>(r);
    makecontext(&r->ctx, reinterpret_cast<void (*)()>(&Trampoline), 2,
                (unsigned)(bits & 0xFFFFFFFFu), (unsigned)(bits >> 32));
    f.handle = r;
    return f;
}

void FiberPosix::Switch(Fiber to) {
    auto* target = static_cast<FiberRecord*>(to.handle);
    if (!target) return;
    FiberRecord* from = HostRecord();
    if (from == target) return;
    t_current = target;
    if (swapcontext(&from->ctx, &target->ctx) != 0) {
        t_current = from;   // no switch happened: still this thread's slot
        std::fprintf(stderr, "platform: swapcontext failed\n");
        std::abort();
    }
    // Resumed. Nothing to restore: whoever switched back into us stored `from`
    // (= us) into ITS thread's t_current before swapping, and we are running on
    // that thread now. A store here would be wrong, not redundant: the IOP
    // kernel resumes a coroutine from whichever host thread runs Dispatch, so
    // the thread that resumed us may differ from the one we parked on, and the
    // compiler is entitled to reuse the thread_local address it resolved above
    // (TLS is per-thread-invariant across calls) -- the store would land in the
    // ORIGINAL thread's slot and leave it naming this fiber while that thread
    // runs on its own stack. Its next Switch then saves its host context into
    // this fiber's record, and the next resume of this fiber from another
    // thread jumps onto a dead region of the first thread's stack
    // (tests/iop/test_fiber_migrate.cpp pins the rule).
}

Fiber FiberPosix::Current() {
    Fiber f;
    f.handle = HostRecord();
    return f;
}

void FiberPosix::Destroy(Fiber f) {
    auto* r = static_cast<FiberRecord*>(f.handle);
    if (!r || !r->mapping) return;   // host records are never destroyed
    munmap(r->mapping, r->mapping_size);
    delete r;
}

}  // namespace ps2r::platform
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
