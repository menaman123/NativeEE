// engine/iop/iop_coroutine.h -- one coroutine per guest IOP thread, over the
// Platform fiber entries (ucontext on macOS). The kernel switches into a
// coroutine to run its thread; the thread switches back to the dispatcher's
// context when it parks (Sleep/Delay/Sema/EventFlag/Mbx/Rpc/Exit). The
// dispatcher context is whichever host thread currently runs
// IopKernel::Dispatch (the EE thread under Frame; the IOP host thread under
// Realtime).
#pragma once

#include <cstddef>

namespace ps2r::iop {

class IopCoroutine {
public:
    IopCoroutine() = default;
    ~IopCoroutine();
    IopCoroutine(const IopCoroutine&) = delete;
    IopCoroutine& operator=(const IopCoroutine&) = delete;

    // Create the fiber; trampoline(arg) runs on the first SwitchTo and must
    // never return (the kernel's entry wrapper loops on ExitThread).
    bool Create(size_t stack_size, void (*trampoline)(void*), void* arg);
    void Destroy();
    bool Valid() const { return fiber_ != nullptr; }

    // Switch from the calling context into this coroutine.
    void SwitchTo();

    // The calling host context's handle (the dispatcher context).
    static void* CurrentHostContext();
    // Switch from a coroutine back to a host context handle.
    static void SwitchToContext(void* ctx);

private:
    void* fiber_ = nullptr;
};

}  // namespace ps2r::iop
