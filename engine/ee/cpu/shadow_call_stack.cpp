#include "engine/ee/cpu/shadow_call_stack.h"

extern "C" uint32_t ee_get_call_stack_depth();
extern "C" bool ee_get_call_stack_top(uint32_t n, uint32_t* out_func_addr, uint32_t* out_ra);
extern "C" bool ee_call_stack_push(uint32_t func_addr, uint32_t ra);
extern "C" void ee_call_stack_pop();

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

bool ShadowCallStack::Complete() { return !rejected_push && omitted_scopes == 0 && Depth() <= 128; }
void ShadowCallStack::RejectedPush() { rejected_push = true; }
void ShadowCallStack::StackEmptied() { rejected_push = false; }
ShadowCallStack::OmittedScope::OmittedScope(bool omitted) : omitted_(omitted) {
    if (omitted_) ++omitted_scopes;
}
ShadowCallStack::OmittedScope::~OmittedScope() {
    if (omitted_) --omitted_scopes;
}
