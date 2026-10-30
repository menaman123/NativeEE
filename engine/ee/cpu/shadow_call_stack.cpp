#include "engine/ee/cpu/shadow_call_stack.h"

extern "C" uint32_t ee_get_call_stack_depth();
extern "C" bool ee_get_call_stack_top(uint32_t n, uint32_t* out_func_addr, uint32_t* out_ra);
extern "C" bool ee_call_stack_push(uint32_t func_addr, uint32_t ra);
extern "C" void ee_call_stack_pop();

// WEAK DEFAULTS: the four accessors are owned by the title's generated common TU
// (recompiler.cpp emits them with the thread_local g_ee_call_stack storage), and
// every test that models a chain defines its own strong stubs. A library that
// merely READS the chain (ps2r_gif's DrawAttribution::SubmitterFn -> Top(), the
// park-site dump, the THRD harvest) must still link on its own: these weak
// definitions describe the empty stack (depth 0, no top, push refused, pop no-op)
// and lose to any strong definition in the same link -- the generated TU's in
// the title binary (verified: `nm nativeee` shows them as strong T symbols), the
// stubs in the tests. They live in THIS object so that whichever archive order
// pulls shadow_call_stack.o in also brings the fallbacks with it.
#if defined(__GNUC__) || defined(__clang__)
extern "C" __attribute__((weak)) uint32_t ee_get_call_stack_depth() { return 0; }
extern "C" __attribute__((weak)) bool ee_get_call_stack_top(uint32_t, uint32_t*, uint32_t*) { return false; }
extern "C" __attribute__((weak)) bool ee_call_stack_push(uint32_t, uint32_t) { return false; }
extern "C" __attribute__((weak)) void ee_call_stack_pop() {}
#elif defined(_MSC_VER)
// MSVC has no weak definitions: /alternatename binds the name to the fallback
// only when nothing else in the link defines it (untested here; no MSVC host).
extern "C" uint32_t ps2r_ee_call_stack_depth_default() { return 0; }
extern "C" bool ps2r_ee_call_stack_top_default(uint32_t, uint32_t*, uint32_t*) { return false; }
extern "C" bool ps2r_ee_call_stack_push_default(uint32_t, uint32_t) { return false; }
extern "C" void ps2r_ee_call_stack_pop_default() {}
#pragma comment(linker, "/alternatename:ee_get_call_stack_depth=ps2r_ee_call_stack_depth_default")
#pragma comment(linker, "/alternatename:ee_get_call_stack_top=ps2r_ee_call_stack_top_default")
#pragma comment(linker, "/alternatename:ee_call_stack_push=ps2r_ee_call_stack_push_default")
#pragma comment(linker, "/alternatename:ee_call_stack_pop=ps2r_ee_call_stack_pop_default")
#else
#error "shadow_call_stack.cpp: no weak-default mechanism for this compiler"
#endif

namespace {
thread_local uint32_t omitted_scopes = 0;
thread_local bool rejected_push = false;
}

uint32_t ShadowCallStack::Depth() { return ee_get_call_stack_depth(); }

bool ShadowCallStack::Top(uint32_t n, Frame& out) {
    return ee_get_call_stack_top(n, &out.func_addr, &out.ra);
}

bool ShadowCallStack::Push(uint32_t func_addr, uint32_t ra) { return ee_call_stack_push(func_addr, ra); }

void ShadowCallStack::Pop() { ee_call_stack_pop(); }

// A rejected push can only be LIVE while the stack is full (a frame refused at
// the cap stays unrecorded until it returns; its own callees are refused the
// same way and the depth stays at the cap). Once the depth is below the cap,
// every live frame was recorded: the overflow latch then names a past
// saturation (a deep recursion that returned), not a hole in the live chain.
bool ShadowCallStack::Complete() {
    const uint32_t depth = Depth();
    return (!rejected_push || depth < kCapacity) && omitted_scopes == 0 && depth <= kCapacity;
}
bool ShadowCallStack::PushRejected() { return rejected_push; }
uint32_t ShadowCallStack::OmittedScopes() { return omitted_scopes; }
void ShadowCallStack::RejectedPush() { rejected_push = true; }
void ShadowCallStack::StackEmptied() { rejected_push = false; }
ShadowCallStack::OmittedScope::OmittedScope(bool omitted) : omitted_(omitted) {
    if (omitted_) ++omitted_scopes;
}
ShadowCallStack::OmittedScope::~OmittedScope() {
    if (omitted_) --omitted_scopes;
}
