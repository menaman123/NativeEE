#include "engine/ee/cpu/fn_hooks.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "engine/ee/cpu/shadow_call_stack.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/recomp_abi/host_app/gs_hle.h"
#include "engine/ee/recomp_abi/recomp_abi.h"

// The generated surface (recompiled.h declares these; the common TU defines them).
extern const uint32_t g_recomp_fn_count;
extern const uint32_t g_recomp_fn_addrs[];
extern uint8_t g_recomp_fn_probe[];
uint32_t recomp_fn_id_for(uint32_t addr);

namespace {

struct ObserverSlot {
    FnHooks::Observer fn = nullptr;
    void* user = nullptr;
    uint8_t mask = 0;
};

struct Ring {
    std::deque<FnHooks::Call> calls;   // newest at the back
    uint32_t capacity = FnHooks::kRingDefault;
};

std::mutex g_mutex;                                   // arming, rings, counts, the snapshot
ObserverSlot g_observers[FnHooks::kObserverMax];
std::atomic<uint32_t> g_observer_count{0};            // slots ever used (scan bound)
std::unordered_map<uint32_t, std::unique_ptr<Ring>> g_rings;   // by id
uint32_t g_ring_capacity = FnHooks::kRingDefault;
std::vector<std::atomic<uint64_t>>* g_counts = nullptr;        // by id, sized at first use
std::vector<FnHooks::Frame> g_stack_snapshot;
std::atomic<uint64_t> g_seq{0};
std::atomic<uint64_t> g_unwinds{0};
std::atomic<uint64_t> g_overflows{0};

thread_local FnHooks::Call t_stack[FnHooks::kProbeStackMax];
thread_local uint32_t t_depth = 0;
thread_local uint32_t t_overflow = 0;   // probed enters refused at the cap (their exits are skipped)

std::vector<std::atomic<uint64_t>>& CountCells() {
    if (!g_counts) g_counts = new std::vector<std::atomic<uint64_t>>(g_recomp_fn_count);
    return *g_counts;
}

inline uint64_t Gpr(const CpuContext& ctx, int r) { return ctx.cpuRegs.GPR.r[r].UD[0]; }

void Notify(const FnHooks::Call& call, uint8_t flags, bool exit) {
    const uint32_t n = g_observer_count.load(std::memory_order_acquire);
    for (uint32_t i = 0; i < n; i++) {
        const ObserverSlot& slot = g_observers[i];
        if (slot.fn && (slot.mask & flags)) slot.fn(slot.user, call, exit);
    }
}

}  // namespace

uint32_t FnHooks::FunctionCount() { return g_recomp_fn_count; }
uint32_t FnHooks::IdOf(uint32_t fn) { return recomp_fn_id_for(fn); }
uint32_t FnHooks::AddressOfId(uint32_t id) { return id < g_recomp_fn_count ? g_recomp_fn_addrs[id] : 0u; }

bool FnHooks::Arm(uint32_t fn, uint8_t flags) {
    const uint32_t id = recomp_fn_id_for(fn);
    if (id >= g_recomp_fn_count) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (flags & kCount) CountCells();
    g_recomp_fn_probe[id] |= flags;
    return true;
}

bool FnHooks::Disarm(uint32_t fn, uint8_t flags) {
    const uint32_t id = recomp_fn_id_for(fn);
    if (id >= g_recomp_fn_count) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_recomp_fn_probe[id] &= static_cast<uint8_t>(~flags);
    return true;
}

void FnHooks::ArmAll(uint8_t flags) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (flags & kCount) CountCells();
    for (uint32_t id = 0; id < g_recomp_fn_count; id++) g_recomp_fn_probe[id] |= flags;
}

void FnHooks::DisarmAll(uint8_t flags) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (uint32_t id = 0; id < g_recomp_fn_count; id++) g_recomp_fn_probe[id] &= static_cast<uint8_t>(~flags);
}

uint8_t FnHooks::Flags(uint32_t id) { return id < g_recomp_fn_count ? g_recomp_fn_probe[id] : 0; }

uint32_t FnHooks::ArmedCount() {
    uint32_t n = 0;
    for (uint32_t id = 0; id < g_recomp_fn_count; id++) n += g_recomp_fn_probe[id] != 0;
    return n;
}

uint32_t FnHooks::AddObserver(uint8_t mask, Observer fn, void* user) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (uint32_t i = 0; i < kObserverMax; i++) {
        if (g_observers[i].fn) continue;
        g_observers[i].user = user;
        g_observers[i].mask = mask;
        g_observers[i].fn = fn;
        if (i >= g_observer_count.load()) g_observer_count.store(i + 1, std::memory_order_release);
        return i;
    }
    return UINT32_MAX;
}

void FnHooks::RemoveObserver(uint32_t slot) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (slot < kObserverMax) g_observers[slot] = ObserverSlot{};
}

void FnHooks::SetRingCapacity(uint32_t n) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_ring_capacity = n ? n : 1;
}

std::vector<FnHooks::Call> FnHooks::Recent(uint32_t fn, uint32_t n) {
    std::vector<Call> out;
    const uint32_t id = recomp_fn_id_for(fn);
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_rings.find(id);
    if (it == g_rings.end()) return out;
    const auto& calls = it->second->calls;
    for (auto r = calls.rbegin(); r != calls.rend() && out.size() < n; ++r) out.push_back(*r);
    return out;
}

uint64_t FnHooks::Count(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_counts || id >= g_counts->size()) return 0;
    return (*g_counts)[id].load(std::memory_order_relaxed);
}

void FnHooks::ResetCounts() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_counts) return;
    for (auto& c : *g_counts) c.store(0, std::memory_order_relaxed);
}

std::vector<std::pair<uint32_t, uint64_t>> FnHooks::Counts() {
    std::vector<std::pair<uint32_t, uint64_t>> out;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_counts) return out;
    for (uint32_t id = 0; id < g_counts->size(); id++) {
        const uint64_t c = (*g_counts)[id].load(std::memory_order_relaxed);
        if (c) out.emplace_back(id, c);
    }
    return out;
}

std::vector<FnHooks::Frame> FnHooks::Stack() {
    std::vector<Frame> out;
    const uint32_t depth = ShadowCallStack::Depth();
    out.reserve(depth);
    for (uint32_t n = 0; n < depth; n++) {
        ShadowCallStack::Frame f;
        if (!ShadowCallStack::Top(n, f)) break;
        out.push_back(Frame{f.func_addr, recomp_fn_id_for(f.func_addr), f.ra});
    }
    return out;
}

uint32_t FnHooks::Depth() { return t_depth; }
const FnHooks::Call* FnHooks::Top() { return t_depth ? &t_stack[t_depth - 1] : nullptr; }

void FnHooks::CaptureStackSnapshot() {
    std::vector<Frame> s = Stack();
    std::lock_guard<std::mutex> lock(g_mutex);
    g_stack_snapshot = std::move(s);
}

std::vector<FnHooks::Frame> FnHooks::StackSnapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_stack_snapshot;
}

uint64_t FnHooks::ProbedCalls() { return g_seq.load(std::memory_order_relaxed); }
uint64_t FnHooks::Unwinds() { return g_unwinds.load(std::memory_order_relaxed); }
uint64_t FnHooks::ProbeStackOverflows() { return g_overflows.load(std::memory_order_relaxed); }

void FnHooks::Enter(uint32_t id, uint32_t entry, CpuContext& ctx, uint32_t entry_ra) {
    const uint8_t flags = g_recomp_fn_probe[id];
    if (t_depth >= kProbeStackMax) {
        ++t_overflow;
        g_overflows.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    Call& c = t_stack[t_depth];
    c.id = id;
    c.fn = g_recomp_fn_addrs[id];
    c.entry = entry;
    c.frame = g_vsync_frame.load(std::memory_order_relaxed);
    c.sp = ctx.cpuRegs.GPR.r[29].UL[0];
    c.ra = entry_ra;
    ShadowCallStack::Frame top;
    c.caller = ShadowCallStack::Top(0, top) ? top.func_addr : 0u;
    c.depth = t_depth;
    c.seq = g_seq.fetch_add(1, std::memory_order_relaxed);
    for (uint32_t i = 0; i < kRegArgs; i++) c.args[i] = Gpr(ctx, 4 + static_cast<int>(i));   // a0..a3 = r4..r7, t0..t3 = r8..r11
    for (uint32_t i = 0; i < kStackArgs; i++) {
        const uint32_t at = c.sp + 0x10u + 4u * i;
        c.args[kRegArgs + i] = (RecompAbi::HasMemory() && FastMem::FastPath(at, 4)) ? RecompAbi::Memory().Read<uint32_t>(at) : 0u;
    }
    c.ret[0] = c.ret[1] = 0;
    c.unwound = false;
    ++t_depth;
    if ((flags & kCount) && g_counts) (*g_counts)[id].fetch_add(1, std::memory_order_relaxed);
    if (flags & (kObserve | kBreak | kTrace | kAttrib | kCorpus)) Notify(c, flags, false);
}

void FnHooks::Exit(uint32_t id, uint32_t entry, CpuContext& ctx) {
    if (t_overflow) { --t_overflow; return; }   // the matching enter was refused
    // Pop to the matching frame (stale frames above it belong to bodies that
    // unwound without reporting; they are dropped).
    while (t_depth && (t_stack[t_depth - 1].id != id || t_stack[t_depth - 1].entry != entry)) --t_depth;
    if (!t_depth) return;
    Call& c = t_stack[--t_depth];
    c.ret[0] = Gpr(ctx, 2);
    c.ret[1] = Gpr(ctx, 3);
    const uint8_t flags = g_recomp_fn_probe[id];
    if (flags & kRing) {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto& ring = g_rings[id];
        if (!ring) { ring = std::make_unique<Ring>(); ring->capacity = g_ring_capacity; }
        if (ring->calls.size() >= ring->capacity) ring->calls.pop_front();
        ring->calls.push_back(c);
    }
    if (flags & (kObserve | kBreak | kTrace | kAttrib | kCorpus)) Notify(c, flags, true);
}

void FnHooks::Unwind(uint32_t id, uint32_t entry) {
    g_unwinds.fetch_add(1, std::memory_order_relaxed);
    if (t_overflow) { --t_overflow; return; }
    while (t_depth && (t_stack[t_depth - 1].id != id || t_stack[t_depth - 1].entry != entry)) --t_depth;
    if (!t_depth) return;
    Call& c = t_stack[--t_depth];
    c.unwound = true;
    const uint8_t flags = g_recomp_fn_probe[id];
    if (flags & (kObserve | kBreak | kTrace | kAttrib | kCorpus)) Notify(c, flags, true);
}

// The seams the generated dispatch calls (recompiled.h declares them).
extern "C" void recomp_probe_enter(uint32_t id, uint32_t entry, CpuContext& ctx, uint32_t entry_ra) {
    FnHooks::Enter(id, entry, ctx, entry_ra);
}
extern "C" void recomp_probe_exit(uint32_t id, uint32_t entry, CpuContext& ctx) { FnHooks::Exit(id, entry, ctx); }
extern "C" void recomp_probe_unwind(uint32_t id, uint32_t entry) { FnHooks::Unwind(id, entry); }
