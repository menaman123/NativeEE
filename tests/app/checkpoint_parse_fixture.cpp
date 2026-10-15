#include <cstdint>

// Import tests parse an immutable THRD record; they never capture/relaunch a
// caller. Supply only the generated shadow ABI needed to link its owner.
extern "C" uint32_t ee_get_call_stack_depth() { return 0; }
extern "C" bool ee_get_call_stack_top(uint32_t, uint32_t*, uint32_t*) { return false; }
extern "C" bool ee_call_stack_push(uint32_t, uint32_t) { return false; }
extern "C" void ee_call_stack_pop() {}
