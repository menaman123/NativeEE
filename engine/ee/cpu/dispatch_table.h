#pragma once
// DispatchTable: the engine's view of the generated dispatch surface
// (PHASE3_PLAN 3.4 row 7). The generated common TU OWNS the tables
// (recompiled_functions, g_recomp_fn_*, the sorted range tables) and the
// install primitive update_function_impl; this class only wraps them --
// it defines nothing the generated TU defines. Hooks (HLE bodies replacing
// a generated body) go THROUGH update_function_impl, never by touching the
// tables directly.
#include <cstdint>
#include <functional>
#include <cstddef>
#include <atomic>
#include <memory>

#include "engine/ee/cpu/cpu_state.h"

class DispatchTable {
public:
    using Body = std::function<void(CpuContext&, uint32_t)>;

    // Runs the generated initialize_recompiled_functions() once and binds the
    // surface's INDIRECT_JUMP diagnostics gate (crashring_enabled) to RingLog.
    static void Initialize();
    static bool IsInitialized();
    // The generated raw fast path (engine.ee_fast_dispatch): calls to ids whose
    // table entry still holds the init-time body skip the observer chain. Must
    // precede Initialize (the generated init tail latches it); a no-op after.
    static void SetFastDispatch(bool on);
    static bool FastDispatch();
    // Successful Install calls so far (the census cross-checks hooked ids
    // against installs: every install marks its id hooked, nothing else does).
    static uint64_t InstallCount();

    // Number of generated entry points (the dense id table's count).
    static uint32_t FunctionCount();
    // Address of dense id `id` (ascending); 0 when out of range.
    static uint32_t AddressOfId(uint32_t id);
    // Dense id of `addr`, or UINT32_MAX when no generated function starts there.
    static uint32_t IdOf(uint32_t addr);

    // Installs `body` at `addr` through the generated install primitive.
    // Returns false when the address is not a generated function start.
    static bool Install(uint32_t addr, Body body);
    // The body currently dispatched for `addr` (generated or installed), or an
    // empty Body when the address is not a function start -- the SdkWrap
    // rows' "original" capture.
    static Body Capture(uint32_t addr);

    // Exact entries of the current installed callable. A dispatch/range hit
    // and IsHooked are deliberately insufficient. Installation is owner-only.
    static bool CanResumeAt(uint32_t fn, uint32_t entry);
    static bool CanRestartWait(uint32_t fn, int wait_type);

    // Generator contract: immutable sidecar entries describe this exact body,
    // never an arbitrary replacement. Entries are sorted and include fn.
    struct Continuations {
        const uint32_t* entries = nullptr;
        size_t count = 0;
        int restart_wait = 0;
    };
    static Body GeneratedBody(uint32_t fn, Body body, Continuations entries);
    // Preserve proof only when wrapping an already proven exact original.
    // Used by the generated trace wrapper, which preserves entry semantics.
    static Body TraceBody(uint32_t fn, const Body& original, Body traced);
    // Structural adapter: only zero/full entry reaches entry_body. Every
    // interior request goes directly to original with the same context/entry.
    // Carries only original's exact INTERIOR proof; never full-entry or wait
    // replay proof. An opaque/wrong-address original stays unproven.
    static Body DelegateInterior(uint32_t fn, Body original, Body entry_body);
    enum class CountMode { EveryCall, FunctionAddress, InteriorCalls };
    // For the audited diagnostic-only SDK/Math counting pattern. No arbitrary
    // observer callback can acquire proof. Aliasing shared_ptrs support cells
    // in a shared counter array; enabled supports inactive-install rollback.
    // These counters must not drive game behavior. Preserves exact-entry
    // support, including any interior-only restriction, but not wait replay.
    // FunctionAddress intentionally does not count entry zero (SDK contract).
    // InteriorCalls excludes both entry zero and the function address.
    static Body CountCalls(uint32_t fn, Body original,
                           std::shared_ptr<std::atomic<uint64_t>> counter,
                           CountMode mode = CountMode::EveryCall,
                           std::shared_ptr<const std::atomic<bool>> enabled = {});
    // Every generated post-init install goes through this, including callers
    // bypassing Install. Unknown bodies are observed as omitted live frames.
    static Body ObserveInstall(uint32_t fn, Body body);
    // Service requests completion on its enclosing dispatch. The callback runs
    // after that callable returns and its omitted-frame scope has ended, while
    // the guest caller is still live. Throw/unwind cancels it; no scope refuses.
    static bool AfterCurrentReturn(CpuContext& ctx, std::function<void(CpuContext&)> callback);

    // Whether the body at `addr` is a generated one (not hooked).
    static bool IsHooked(uint32_t addr);

    // Calls the body registered for `addr` (the hook or the generated body).
    static void Invoke(uint32_t addr, CpuContext& ctx);
    // Enters the function starting at `fn` at guest address `enter` (0 or `fn`
    // = the function start; otherwise a block-start label inside it -- the
    // savestate relaunch's post-call continuation). False when no generated
    // function starts at `fn`, or the installed body lacks that exact entry.
    static bool InvokeAt(uint32_t fn, uint32_t enter, CpuContext& ctx);
private:
    struct ObservedBody;
    static const ObservedBody* Proven(uint32_t fn, const Body& body);
};
