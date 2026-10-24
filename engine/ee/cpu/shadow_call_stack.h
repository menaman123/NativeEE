#pragma once
// ShadowCallStack: a NO-STORAGE view of the EE shadow call stack the generated
// common TU owns (thread_local g_ee_call_stack / g_ee_call_stack_depth, with
// the extern "C" push/pop/depth/top accessors -- PHASE3_PLAN 3.4 row 8). The
// THRD savestate harvest and the park-site dump read it through this class.
#include <cstdint>

class ShadowCallStack {
public:
    struct Frame {
        uint32_t func_addr = 0;
        uint32_t ra = 0;
    };

    // Depth of the calling host thread's shadow stack.
    static uint32_t Depth();
    // Frame `n` from the innermost (n = 0); false when n >= Depth().
    static bool Top(uint32_t n, Frame& out);
    // Push/pop around a body the runtime invokes directly (a hook or a
    // pass-through), so the chain stays contiguous. Push returns false when
    // the fixed-capacity stack is full (the frame is then not recorded).
    static bool Push(uint32_t func_addr, uint32_t ra);
    static void Pop();
    // The generated common TU's EE_CALL_STACK_MAX (recompiler.cpp): the frame cap.
    static constexpr uint32_t kCapacity = 128;
    // Calling owner only; incomplete/truncated/opaque live chains refuse.
    static bool Complete();
    // Why Complete() is false, by name: a latched rejected push (the stack
    // overflowed since it last emptied) / the live opaque scopes (0 = none).
    static bool PushRejected();
    static uint32_t OmittedScopes();
    // Generated common TU records rejected pushes. Manual failed Push callers
    // skip Pop, so that loss remains latched until the represented stack empties.
    static void RejectedPush();
    static void StackEmptied();
    // Opaque dispatch bodies do not invent guest frames. Their live host
    // continuations forbid capture until the entire installed callable returns.
    class OmittedScope {
    public:
        explicit OmittedScope(bool omitted);
        ~OmittedScope();
        OmittedScope(const OmittedScope&) = delete;
        OmittedScope& operator=(const OmittedScope&) = delete;
    private:
        bool omitted_;
    };
};
