#include "engine/ee/cpu/fn_swap.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <utility>

#include "engine/ee/cpu/dispatch_table.h"
#include "engine/ee/cpu/shadow_call_stack.h"
#include "engine/platform/host/platform.h"

// The generated surface (recompiled.h declares these; the common TU defines them).
extern const uint32_t g_recomp_fn_count;
extern bool g_recomp_fn_hooked[];
extern void (* const g_recomp_fn_raw[])(CpuContext&, uint32_t);
uint32_t recomp_fn_id_for(uint32_t addr);

namespace {

struct Swap {
    FnSwap::Info info;
    DispatchTable::Body original;        // the body dispatched before the FIRST swap of this address
    bool original_hooked = true;         // g_recomp_fn_hooked[id] before the first swap
    std::shared_ptr<std::atomic<uint64_t>> calls;
};

std::mutex g_mu;
std::map<uint32_t, Swap> g_swaps;                 // by address
std::map<std::string, ps2r::host::DynamicLibrary> g_libraries;   // opened libraries, never closed

}  // namespace

FnSwap::Body FnSwap::GeneratedBody(uint32_t addr) {
    const uint32_t id = recomp_fn_id_for(addr);
    if (id >= g_recomp_fn_count) return nullptr;
    return g_recomp_fn_raw[id];
}

bool FnSwap::OnCallingStack(uint32_t addr) {
    const uint32_t depth = ShadowCallStack::Depth();
    ShadowCallStack::Frame f;
    for (uint32_t n = 0; n < depth; ++n)
        if (ShadowCallStack::Top(n, f) && f.func_addr == addr) return true;
    return false;
}

bool FnSwap::IsSwapped(uint32_t addr) {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_swaps.count(addr) != 0;
}

std::vector<FnSwap::Info> FnSwap::List() {
    std::lock_guard<std::mutex> lock(g_mu);
    std::vector<Info> out;
    for (auto& [addr, s] : g_swaps) {
        Info i = s.info;
        i.calls = s.calls->load(std::memory_order_relaxed);
        out.push_back(i);
    }
    return out;
}

bool FnSwap::Install(uint32_t addr, const std::string& library, const std::string& symbol, bool inherit_proof,
                     std::string* previous, std::string* err) {
    if (library.empty()) { if (err) *err = "library_path is empty"; return false; }
    // dlopen / LoadLibrary through the platform layer (ps2r::host::DynamicLibrary)
    ps2r::host::DynamicLibrary handle;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        auto it = g_libraries.find(library);
        if (it != g_libraries.end()) handle = it->second;
    }
    if (!handle.handle) {
        std::string why;
        if (!ps2r::host::DynamicLibraryOpen(&handle, library.c_str(), &why)) {
            if (err) *err = "opening " + library + " failed: " + why;
            return false;
        }
        std::lock_guard<std::mutex> lock(g_mu);
        g_libraries.emplace(library, handle);
    }
    const std::string sym = symbol.empty() ? kDefaultSymbol : symbol;
    void* p = ps2r::host::DynamicLibrarySymbol(handle, sym.c_str());
    if (!p) {
        if (err) *err = "symbol " + sym + " not found in " + library;
        return false;
    }
    Body body = reinterpret_cast<Body>(p);
    // The one candidate format (recomp-agent docs/VERIFY.md): a library built for ONE function also
    // exports `const uint32_t recomp_candidate_addr`, the entry it was verified for. A swap of it under
    // another address is refused; a library without the symbol (a hand-made module) is taken as is.
    if (const void* ap = ps2r::host::DynamicLibrarySymbol(handle, kAddrSymbol)) {
        uint32_t built_for = 0;
        std::memcpy(&built_for, ap, sizeof(built_for));
        if (built_for != addr) {
            if (err) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "the candidate library is built for 0x%08x (%s), not 0x%08x",
                              built_for, kAddrSymbol, addr);
                *err = std::string(buf) + ": " + library;
            }
            return false;
        }
    }
    return InstallBody(addr, body, inherit_proof, previous, err, library, sym);
}

bool FnSwap::InstallBody(uint32_t addr, Body body, bool inherit_proof, std::string* previous, std::string* err,
                         const std::string& library, const std::string& symbol) {
    if (!body) { if (err) *err = "the candidate body is null"; return false; }
    const uint32_t id = recomp_fn_id_for(addr);
    if (id >= g_recomp_fn_count) { if (err) *err = "not a recompiled function's entry"; return false; }
    if (!DispatchTable::IsInitialized()) { if (err) *err = "the dispatch table is not initialised"; return false; }
    if (OnCallingStack(addr)) {
        if (err) *err = "a call of the function is in flight on this thread (it is on the shadow stack); swap at a boundary where it is not";
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_swaps.find(addr);
    const bool was_swapped = it != g_swaps.end();
    DispatchTable::Body original = was_swapped ? it->second.original : DispatchTable::Capture(addr);
    if (!original) { if (err) *err = "no body is registered at this address"; return false; }
    auto calls = std::make_shared<std::atomic<uint64_t>>(0);
    DispatchTable::Body wrapper = [body, addr, calls](CpuContext& ctx, uint32_t call_addr) {
        const uint32_t entry_ra = ctx.cpuRegs.GPR.r[31].UL[0];
        calls->fetch_add(1, std::memory_order_relaxed);
        // The init wrapper's duties around the candidate: ra read before the body, a shadow frame for the
        // call, popped however the body leaves.
        const bool pushed = ShadowCallStack::Push(addr, entry_ra);
        if (!pushed) ShadowCallStack::RejectedPush();
        try {
            body(ctx, call_addr);
        } catch (...) {
            if (pushed) ShadowCallStack::Pop();
            throw;
        }
        if (pushed) ShadowCallStack::Pop();
    };
    // The proof travels with the callable: TraceBody keeps the previous body's continuation entries and
    // restartable-wait declaration (a candidate in the generated shape); without it the body is opaque.
    DispatchTable::Body installed = inherit_proof ? DispatchTable::TraceBody(addr, original, std::move(wrapper))
                                                  : DispatchTable::ObserveInstall(addr, std::move(wrapper));
    if (!DispatchTable::Install(addr, std::move(installed))) { if (err) *err = "update_function_impl refused the address"; return false; }
    if (previous) *previous = was_swapped ? "swapped" : "generated";
    if (!was_swapped) {
        Swap s;
        s.original = std::move(original);
        s.original_hooked = g_recomp_fn_hooked[id];
        it = g_swaps.emplace(addr, std::move(s)).first;
    }
    it->second.info.addr = addr;
    it->second.info.id = id;
    it->second.info.library = library;
    it->second.info.symbol = symbol;
    it->second.info.inherit_proof = inherit_proof;
    it->second.calls = calls;
    return true;
}

bool FnSwap::Remove(uint32_t addr, std::string* err) {
    if (OnCallingStack(addr)) {
        if (err) *err = "a call of the function is in flight on this thread (it is on the shadow stack)";
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_swaps.find(addr);
    if (it == g_swaps.end()) { if (err) *err = "the function is not swapped"; return false; }
    if (!DispatchTable::Install(addr, it->second.original)) { if (err) *err = "update_function_impl refused the address"; return false; }
    // Install marks the id hooked; the default wrapper's id was on the raw fast path (when the seam is on).
    g_recomp_fn_hooked[it->second.info.id] = it->second.original_hooked;
    g_swaps.erase(it);
    return true;
}
