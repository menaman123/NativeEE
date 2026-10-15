// platform/posix/fiber_posix.h -- the POSIX fiber backend behind the Platform
// fiber entries (fiber_create / fiber_switch / fiber_current): user-space
// cooperative context switching over ucontext, shared by the macOS and Linux
// targets. One record per fiber plus one implicit record per host thread
// (created on the first fiber_current call on that thread), so a coroutine can
// switch back to whichever host thread is currently driving it. The IOP
// kernel's coroutines (engine/iop) run on this.
#pragma once

#include <cstddef>

#include "engine/platform/platform.h"

namespace ps2r::platform {

class FiberPosix {
public:
    // Allocate a stack of `stack` bytes and a context that runs fn(arg) on
    // the first switch. fn must never return (the caller's trampoline loops).
    static Fiber Create(void (*fn)(void*), void* arg, size_t stack);
    // Switch the calling context to `to`, remembering the caller as the
    // thread's current fiber so `to` can switch back with fiber_current().
    static void Switch(Fiber to);
    // The record of the context currently running on this host thread.
    static Fiber Current();
    // Release a fiber created by Create (never the running one).
    static void Destroy(Fiber f);
};

}  // namespace ps2r::platform
