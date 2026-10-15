// engine/iop/iop_coroutine.cpp -- IopCoroutine over platform::Current().fiber_*.
#include "engine/iop/iop_coroutine.h"

#include "engine/platform/platform.h"

namespace ps2r::iop {

IopCoroutine::~IopCoroutine() { Destroy(); }

bool IopCoroutine::Create(size_t stack_size, void (*trampoline)(void*), void* arg) {
    Destroy();
    const platform::Platform& p = platform::Current();
    if (!p.fiber_create) return false;
    fiber_ = p.fiber_create(trampoline, arg, stack_size).handle;
    return fiber_ != nullptr;
}

void IopCoroutine::Destroy() {
    if (!fiber_) return;
    const platform::Platform& p = platform::Current();
    if (p.fiber_destroy) p.fiber_destroy(platform::Fiber{fiber_});
    fiber_ = nullptr;
}

void IopCoroutine::SwitchTo() {
    if (fiber_) platform::Current().fiber_switch(platform::Fiber{fiber_});
}

void* IopCoroutine::CurrentHostContext() {
    const platform::Platform& p = platform::Current();
    return p.fiber_current ? p.fiber_current().handle : nullptr;
}

void IopCoroutine::SwitchToContext(void* ctx) {
    if (ctx) platform::Current().fiber_switch(platform::Fiber{ctx});
}

}  // namespace ps2r::iop
