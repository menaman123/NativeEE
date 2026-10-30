#pragma once
// FnHooks: the runtime hook table over the generated dispatch (one byte per dense
// id, g_recomp_fn_probe[], owned by the generated common TU). Arming a bit on an
// id at run time -- no rebuild, no body replacement -- makes every dispatch of
// that function run recomp_probe_enter before its shadow frame is pushed and
// recomp_probe_exit after it is popped (the generated RecompProbeScope; an
// exception leaving the body reports recomp_probe_unwind instead of the exit).
// Un-armed ids pay one byte load and one predicted branch per call, out of line
// in recomp_raw_dispatch / recomp_hooked_dispatch.
//
// What a probe captures (Call): a0..a3 and t0..t3 (the SCE EE ABI's eight
// register arguments), the four stack words at sp+16..sp+28 (the o32 homes of
// arguments 5..8 / the first stack arguments), sp and ra at entry, the CALLER
// (the shadow-stack top at entry, 0 when none), the frame boundary count
// (g_vsync_frame), v0/v1 at exit, and a process-wide sequence number.
//
// Consumers: observers (a callback per armed bit mask, run synchronously on the
// guest thread that dispatches -- it may block the game: a breakpoint), the
// per-function ring of the last kRing calls (kRing bit), per-id call counts
// (kCount bit), and the tracing build's corpus recorder (access_tracer.h,
// kCorpus bit). The probe stack (one per host thread) mirrors the nesting of
// probed calls; Depth()/Top() read the calling thread's.
//
// Thread model: the generated shadow stack and the probe stack are thread_local
// to the guest's host thread. Stack()/CaptureStackSnapshot() must run on that
// thread (a frame-boundary callback does); StackSnapshot() reads the last capture
// from anywhere. Ring, counts and arming are mutex-protected.
#include <cstdint>
#include <vector>

#include "engine/ee/cpu/cpu_state.h"

class FnHooks {
public:
    static constexpr uint32_t kRegArgs = 8;     // a0..a3, t0..t3
    static constexpr uint32_t kStackArgs = 4;   // [sp+0x10], [sp+0x14], [sp+0x18], [sp+0x1c]
    static constexpr uint32_t kArgs = kRegArgs + kStackArgs;
    static constexpr uint32_t kRingDefault = 64;
    static constexpr uint32_t kProbeStackMax = 512;

    // The bits of g_recomp_fn_probe[id]. Observers register with a mask of them.
    enum Flag : uint8_t {
        kObserve = 0x01,   // observers with this bit see enter and exit
        kRing    = 0x02,   // record into the function's ring at exit
        kCount   = 0x04,   // count calls (entries)
        kCorpus  = 0x08,   // the tracing build records the call's read/write set
        kBreak   = 0x10,   // reserved for the control plane's breakpoints (observer bit)
        kTrace   = 0x20,   // reserved for the control plane's fn.trace (observer bit)
        kAttrib  = 0x40,   // attribution consumers (disc / pad / allocator loggers)
        kAll     = 0xFF
    };

    struct Call {
        uint32_t id = 0;        // dense id
        uint32_t fn = 0;        // the function's entry address
        uint32_t entry = 0;     // the address entered: == fn for a call; interior = a continuation re-entry
        uint32_t frame = 0;     // frame boundary count at entry
        uint32_t sp = 0;        // at entry
        uint32_t ra = 0;        // at entry
        uint32_t caller = 0;    // shadow-stack top at entry (a function address), 0 = none
        uint32_t depth = 0;     // probe-stack depth at entry (0 = outermost probed call)
        uint64_t seq = 0;       // process-wide probed-call ordinal
        uint64_t args[kArgs] = {};
        uint64_t ret[2] = {};   // v0, v1: valid on exit (zero on enter)
        bool unwound = false;   // the body was left by exception (ret invalid)
    };

    struct Frame {
        uint32_t fn = 0;
        uint32_t id = 0;
        uint32_t ra = 0;
    };

    // Observer: `exit` false = enter (ret zero), true = exit (ret valid, or unwound).
    using Observer = void (*)(void* user, const Call& call, bool exit);
    static constexpr uint32_t kObserverMax = 8;

    // ---- arming (any thread) ----
    static uint32_t FunctionCount();
    static uint32_t IdOf(uint32_t fn);          // UINT32_MAX when not a function start
    static uint32_t AddressOfId(uint32_t id);
    // OR / clear bits on one id; false when `fn` is not a function start.
    static bool Arm(uint32_t fn, uint8_t flags);
    static bool Disarm(uint32_t fn, uint8_t flags);
    // OR / clear bits on every id.
    static void ArmAll(uint8_t flags);
    static void DisarmAll(uint8_t flags);
    static uint8_t Flags(uint32_t id);
    static uint32_t ArmedCount();               // ids with any bit set

    // ---- observers (any thread; the callback runs on the guest thread) ----
    // Returns a slot (0..kObserverMax-1) or UINT32_MAX when full.
    static uint32_t AddObserver(uint8_t mask, Observer fn, void* user);
    static void RemoveObserver(uint32_t slot);

    // ---- the ring (kRing) ----
    static void SetRingCapacity(uint32_t n);    // applies to rings created afterwards
    // The last calls of `fn`, newest first, at most n. Empty when none / not armed.
    static std::vector<Call> Recent(uint32_t fn, uint32_t n);

    // ---- counts (kCount) ----
    static uint64_t Count(uint32_t id);
    static void ResetCounts();
    // Every id with a nonzero count: (id, count).
    static std::vector<std::pair<uint32_t, uint64_t>> Counts();

    // ---- the stacks (calling thread) ----
    // The generated shadow stack, innermost first.
    static std::vector<Frame> Stack();
    // The probe stack of the calling thread: depth, the innermost probed call.
    static uint32_t Depth();
    static const Call* Top();
    // Snapshot the calling thread's shadow stack for readers on other threads.
    static void CaptureStackSnapshot();
    static std::vector<Frame> StackSnapshot();

    // ---- statistics ----
    static uint64_t ProbedCalls();              // enters seen
    static uint64_t Unwinds();
    static uint64_t ProbeStackOverflows();

    // The seams the generated code calls (fn_hooks.cpp defines the extern "C" trio).
    static void Enter(uint32_t id, uint32_t entry, CpuContext& ctx, uint32_t entry_ra);
    static void Exit(uint32_t id, uint32_t entry, CpuContext& ctx);
    static void Unwind(uint32_t id, uint32_t entry);
};
