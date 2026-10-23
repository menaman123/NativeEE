#include "engine/ee/cpu/dispatch_table.h"

#include <atomic>
#include <algorithm>
#include <map>
#include <utility>

#include "engine/core/log/ring_log.h"
#include "engine/ee/cpu/shadow_call_stack.h"

// The generated surface (recompiled.h declares these; the common TU defines them).
extern std::map<uint32_t, std::function<void(CpuContext&, uint32_t)>> recompiled_functions;
extern const uint32_t g_recomp_fn_count;
extern const uint32_t g_recomp_fn_addrs[];
extern bool g_recomp_fn_hooked[];
void initialize_recompiled_functions();
bool update_function_impl(uint32_t address, const std::function<void(CpuContext&, uint32_t)>& impl);
uint32_t recomp_fn_id_for(uint32_t addr);
// The indirect-call guards' diagnostics gate (seams.cpp defines it; row 26).
extern "C" bool crashring_enabled;
// The raw fast path gate (seams.cpp defines it; the generated init tail reads it).
extern "C" bool recomp_fast_dispatch_enabled;

namespace {
std::atomic<bool> g_initialized{false};
std::atomic<uint64_t> g_installs{0};

struct Invocation {
    CpuContext* context;
    std::function<void(CpuContext&)> completion;
    Invocation* previous;
};
thread_local Invocation* current_invocation = nullptr;
}

// The std::function target carries the proof with the callable, so copying a
// saved original keeps it and assigning a new lambda loses it. Address-indexed
// stale metadata cannot accidentally bless an unrelated replacement.
struct DispatchTable::ObservedBody {
    uint32_t function;
    DispatchTable::Body body;
    DispatchTable::Continuations entries;
    bool interior_only = false;
    void operator()(CpuContext& ctx, uint32_t entry) const {
        Invocation invocation{&ctx, {}, current_invocation};
        current_invocation = &invocation;
        try {
            ShadowCallStack::OmittedScope omitted(entries.count == 0 ||
                (interior_only && (!entry || entry == function)));
            body(ctx, entry);
        } catch (...) {
            current_invocation = invocation.previous;
            throw;
        }
        current_invocation = invocation.previous;
        if (invocation.completion) invocation.completion(ctx);
    }
};

const DispatchTable::ObservedBody* DispatchTable::Proven(uint32_t fn, const DispatchTable::Body& body) {
    const auto* tracked = body.target<ObservedBody>();
    return tracked && tracked->function == fn ? tracked : nullptr;
}

void DispatchTable::Initialize() {
    if (g_initialized.exchange(true)) return;
    initialize_recompiled_functions();
    // The surface is live: its INDIRECT_JUMP gate now follows RingLog's verbose
    // EE bit (latched here and on every later RingLog::Configure).
    ps2r::ringlog::RingLog::BindGeneratedGate(&crashring_enabled);
}

bool DispatchTable::IsInitialized() { return g_initialized.load(); }

void DispatchTable::SetFastDispatch(bool on) {
    if (!g_initialized.load()) recomp_fast_dispatch_enabled = on;
}
bool DispatchTable::FastDispatch() { return recomp_fast_dispatch_enabled; }
uint64_t DispatchTable::InstallCount() { return g_installs.load(std::memory_order_relaxed); }

uint32_t DispatchTable::FunctionCount() { return g_recomp_fn_count; }

uint32_t DispatchTable::AddressOfId(uint32_t id) {
    return id < g_recomp_fn_count ? g_recomp_fn_addrs[id] : 0u;
}

uint32_t DispatchTable::IdOf(uint32_t addr) { return recomp_fn_id_for(addr); }

bool DispatchTable::Install(uint32_t addr, Body body) {
    const bool ok = update_function_impl(addr, body);
    if (ok) g_installs.fetch_add(1, std::memory_order_relaxed);
    return ok;
}

DispatchTable::Body DispatchTable::Capture(uint32_t addr) {
    auto it = recompiled_functions.find(addr);
    return it != recompiled_functions.end() ? it->second : Body();
}

bool DispatchTable::IsHooked(uint32_t addr) {
    const uint32_t id = recomp_fn_id_for(addr);
    return id < g_recomp_fn_count && g_recomp_fn_hooked[id];
}

void DispatchTable::Invoke(uint32_t addr, CpuContext& ctx) {
    auto it = recompiled_functions.find(addr);
    if (it != recompiled_functions.end()) it->second(ctx, addr);
}

bool DispatchTable::InvokeAt(uint32_t fn, uint32_t enter, CpuContext& ctx) {
    auto it = recompiled_functions.find(fn);
    if (it == recompiled_functions.end() || !CanResumeAt(fn, enter)) return false;
    it->second(ctx, enter);
    return true;
}

bool DispatchTable::CanResumeAt(uint32_t fn, uint32_t entry) {
    const auto it = recompiled_functions.find(fn);
    if (it == recompiled_functions.end()) return false;
    const auto* body = Proven(fn, it->second);
    if (!body || !body->entries.count || !body->entries.entries) return false;
    if (body->interior_only && (!entry || entry == fn)) return false;
    const auto& entries = body->entries;
    return std::binary_search(entries.entries, entries.entries + entries.count, entry ? entry : fn);
}

bool DispatchTable::CanRestartWait(uint32_t fn, int wait_type) {
    const auto it = recompiled_functions.find(fn);
    if (it == recompiled_functions.end() || !wait_type) return false;
    const auto* body = Proven(fn, it->second);
    return body && body->entries.restart_wait == wait_type && CanResumeAt(fn, 0);
}

DispatchTable::Body DispatchTable::GeneratedBody(uint32_t fn, Body body, Continuations entries) {
    return ObservedBody{fn, std::move(body), entries};
}

DispatchTable::Body DispatchTable::TraceBody(uint32_t fn, const Body& original, Body traced) {
    const auto* proof = Proven(fn, original);
    return ObservedBody{fn, std::move(traced), proof ? proof->entries : Continuations{},
        proof && proof->interior_only};
}

DispatchTable::Body DispatchTable::DelegateInterior(uint32_t fn, Body original, Body entry_body) {
    if (!original || !entry_body) return {};
    const auto* proof = Proven(fn, original);
    auto entries = proof ? proof->entries : Continuations{};
    entries.restart_wait = 0;
    Body dispatch = [fn, original = std::move(original), entry_body = std::move(entry_body)]
        (CpuContext& ctx, uint32_t entry) {
            if (!entry || entry == fn) entry_body(ctx, entry);
            else original(ctx, entry);
        };
    return ObservedBody{fn, std::move(dispatch), entries, true};
}

DispatchTable::Body DispatchTable::CountCalls(uint32_t fn, Body original,
    std::shared_ptr<std::atomic<uint64_t>> counter, CountMode mode,
    std::shared_ptr<const std::atomic<bool>> enabled) {
    if (!original || !counter ||
        (mode != CountMode::EveryCall && mode != CountMode::FunctionAddress &&
         mode != CountMode::InteriorCalls)) return {};
    const auto* proof = Proven(fn, original);
    auto entries = proof ? proof->entries : Continuations{};
    const bool interior_only = proof && proof->interior_only;
    entries.restart_wait = 0; // Counter replay needs a separate wait-owner proof.
    Body counted = [fn, original = std::move(original), counter = std::move(counter), mode,
                    enabled = std::move(enabled)](CpuContext& ctx, uint32_t entry) {
        const bool count = mode == CountMode::EveryCall ||
            (mode == CountMode::FunctionAddress && entry == fn) ||
            (mode == CountMode::InteriorCalls && entry != 0 && entry != fn);
        if (count &&
            (!enabled || enabled->load(std::memory_order_acquire)))
            counter->fetch_add(1, std::memory_order_relaxed);
        original(ctx, entry);
    };
    return ObservedBody{fn, std::move(counted), entries, interior_only};
}

DispatchTable::Body DispatchTable::ObserveInstall(uint32_t fn, Body body) {
    if (Proven(fn, body)) return body; // Restoring an exact captured original.
    return ObservedBody{fn, std::move(body), {}};
}

bool DispatchTable::AfterCurrentReturn(CpuContext& ctx, std::function<void(CpuContext&)> callback) {
    if (!current_invocation || current_invocation->context != &ctx ||
        current_invocation->completion || !callback) return false;
    current_invocation->completion = std::move(callback);
    return true;
}
