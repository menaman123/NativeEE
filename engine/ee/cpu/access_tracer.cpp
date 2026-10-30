#include "engine/ee/cpu/access_tracer.h"

#ifdef PS2R_TRACE_ACCESS

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>

#include "engine/ee/cpu/corpus_writer.h"
#include "engine/ee/cpu/fn_hooks.h"
#include "engine/ee/mem/main_memory.h"

namespace {

constexpr uint32_t kPageBits = 12;
constexpr uint32_t kPageSize = 1u << kPageBits;

struct Page {
    uint32_t base = 0;
    uint64_t rmask[kPageSize / 64] = {};
    uint64_t wmask[kPageSize / 64] = {};
    uint8_t r[kPageSize] = {};
    uint8_t w[kPageSize] = {};
};

struct Frame {
    bool recording = false;
    FnHooks::Call call;
    std::vector<Page*> pages;         // touched, unordered; the last hit is kept first
    std::vector<uint32_t> callees;
};

std::mutex g_mutex;
std::atomic<bool> g_active{false};
std::unique_ptr<CorpusWriter> g_writer;
std::vector<uint8_t> g_scope;        // by id: 1 = record this function's calls
bool g_scope_all = false;
uint32_t g_observer = UINT32_MAX;
std::string g_path;
std::atomic<uint64_t> g_accesses{0};
std::atomic<uint64_t> g_unattributed{0};
uint32_t g_first_frame = 0, g_last_frame = 0;
bool g_have_frame = false;

thread_local std::vector<Frame> t_frames;
thread_local int t_top = -1;         // index of the innermost RECORDING frame, -1 = the innermost probed call records nothing
thread_local std::vector<Page*> t_pool;

Page* TakePage(uint32_t base) {
    Page* p;
    if (!t_pool.empty()) { p = t_pool.back(); t_pool.pop_back(); }
    else p = new Page();
    p->base = base;
    std::memset(p->rmask, 0, sizeof(p->rmask));
    std::memset(p->wmask, 0, sizeof(p->wmask));
    return p;
}

void ReturnPages(Frame& f) {
    for (Page* p : f.pages) t_pool.push_back(p);
    f.pages.clear();
}

inline uint32_t Normalize(uint32_t addr) {
    // Main RAM through any segment -> physical; the scratchpad keeps its 0x7000xxxx address.
    return (addr & 0xF0000000u) == 0x70000000u ? addr : MainMemory::Phys(addr);
}

inline Page* FindPage(Frame& f, uint32_t base) {
    for (size_t i = 0; i < f.pages.size(); i++) {
        if (f.pages[i]->base == base) {
            if (i) std::swap(f.pages[0], f.pages[i]);
            return f.pages[0];
        }
    }
    Page* p = TakePage(base);
    f.pages.insert(f.pages.begin(), p);
    return p;
}

void Touch(Frame& f, uint32_t addr, const uint8_t* bytes, uint32_t n, bool write) {
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t a = addr + i;
        Page* p = FindPage(f, a & ~(kPageSize - 1));
        const uint32_t off = a & (kPageSize - 1);
        const uint64_t bit = 1ull << (off & 63);
        if (write) {
            p->wmask[off >> 6] |= bit;
            p->w[off] = bytes[i];
        } else if (!(p->rmask[off >> 6] & bit)) {
            p->rmask[off >> 6] |= bit;
            p->r[off] = bytes[i];
        }
    }
}

void Runs(const std::vector<Page*>& pages, bool write, std::vector<CorpusWriter::Run>& out) {
    std::vector<Page*> sorted(pages);
    std::sort(sorted.begin(), sorted.end(), [](const Page* a, const Page* b) { return a->base < b->base; });
    CorpusWriter::Run* cur = nullptr;
    uint32_t next = 0;
    for (const Page* p : sorted) {
        const uint64_t* mask = write ? p->wmask : p->rmask;
        const uint8_t* val = write ? p->w : p->r;
        for (uint32_t off = 0; off < kPageSize; off++) {
            if (!(mask[off >> 6] & (1ull << (off & 63)))) { cur = nullptr; continue; }
            const uint32_t a = p->base + off;
            if (!cur || a != next) {
                out.emplace_back();
                cur = &out.back();
                cur->addr = a;
            }
            cur->bytes.push_back(val[off]);
            next = a + 1;
        }
    }
}

void Finalize(Frame& f) {
    CorpusWriter::Record r;
    r.fn = f.call.fn;
    r.frame = f.call.frame;
    r.args.assign(f.call.args, f.call.args + FnHooks::kArgs);
    r.v0 = f.call.ret[0];
    r.v1 = f.call.ret[1];
    r.sp = f.call.sp;
    r.ra = f.call.ra;
    r.caller = f.call.caller ? f.call.caller : CorpusWriter::kNoCaller;
    Runs(f.pages, false, r.reads);
    Runs(f.pages, true, r.writes);
    r.callees = std::move(f.callees);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_writer) {
        g_writer->Add(r);
        if (!g_have_frame) { g_first_frame = r.frame; g_have_frame = true; }
        g_last_frame = std::max(g_last_frame, r.frame);
    }
}

void RecomputeTop() {
    t_top = -1;
    if (!t_frames.empty() && t_frames.back().recording) t_top = static_cast<int>(t_frames.size()) - 1;
}

void OnCall(void*, const FnHooks::Call& call, bool exit) {
    if (!g_active.load(std::memory_order_relaxed)) return;
    if (!exit) {
        // Frames above `depth` belong to bodies that left without an exit report.
        while (t_frames.size() > call.depth) { ReturnPages(t_frames.back()); t_frames.pop_back(); }
        const bool real_call = call.entry == call.fn;
        if (real_call && !t_frames.empty() && t_frames.back().recording) t_frames.back().callees.push_back(call.fn);
        t_frames.emplace_back();
        Frame& f = t_frames.back();
        f.call = call;
        f.recording = real_call && (g_scope_all || (call.id < g_scope.size() && g_scope[call.id]));
        RecomputeTop();
        return;
    }
    while (t_frames.size() > call.depth + 1) { ReturnPages(t_frames.back()); t_frames.pop_back(); }
    if (t_frames.size() == call.depth + 1) {
        Frame& f = t_frames.back();
        f.call.ret[0] = call.ret[0];
        f.call.ret[1] = call.ret[1];
        if (f.recording && !call.unwound) Finalize(f);
        ReturnPages(f);
        t_frames.pop_back();
    }
    RecomputeTop();
}

}  // namespace

bool AccessTracer::Available() { return true; }
bool AccessTracer::Active() { return g_active.load(std::memory_order_relaxed); }

bool AccessTracer::Start(const std::string& path, const std::vector<uint32_t>& functions,
                         const std::vector<std::pair<std::string, std::string>>& meta_json,
                         uint64_t max_distinct, std::string* error) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_active.load()) { if (error) *error = "a recording is already running (" + g_path + ")"; return false; }
    g_scope.assign(FnHooks::FunctionCount(), 0);
    g_scope_all = functions.empty();
    std::string fn_json = "\"all\"";
    if (!g_scope_all) {
        fn_json = "[";
        for (size_t i = 0; i < functions.size(); i++) {
            const uint32_t id = FnHooks::IdOf(functions[i]);
            if (id >= g_scope.size()) { if (error) *error = "not a function start: 0x" + std::to_string(functions[i]); return false; }
            g_scope[id] = 1;
            fn_json += (i ? ", " : "") + std::to_string(functions[i]);
        }
        fn_json += "]";
    }
    g_writer = std::make_unique<CorpusWriter>();
    CorpusWriter::Limits limits;
    if (max_distinct) limits.max_distinct = max_distinct;
    g_writer->SetLimits(limits);
    g_writer->SetMeta("producer", "\"nativeee PS2R_TRACE_ACCESS (engine access_tracer)\"");
    g_writer->SetMeta("functions", fn_json);
    g_writer->SetMeta("nargs", std::to_string(FnHooks::kArgs));
    g_writer->SetMeta("args_layout", "[\"a0\", \"a1\", \"a2\", \"a3\", \"t0\", \"t1\", \"t2\", \"t3\", \"sp+0x10\", \"sp+0x14\", \"sp+0x18\", \"sp+0x1c\"]");
    g_writer->SetMeta("dedup", "\"fnv1a64(fn,args,ret,sp,caller,reads,writes,callees)\"");
    g_writer->SetMeta("attribution", "\"innermost probed call; accesses of callees outside the scope are dropped\"");
    for (const auto& kv : meta_json) g_writer->SetMeta(kv.first, kv.second);
    g_path = path;
    g_accesses.store(0);
    g_unattributed.store(0);
    g_have_frame = false;
    g_first_frame = g_last_frame = 0;
    g_observer = FnHooks::AddObserver(FnHooks::kCorpus, &OnCall, nullptr);
    if (g_observer == UINT32_MAX) { g_writer.reset(); if (error) *error = "no observer slot"; return false; }
    FnHooks::ArmAll(FnHooks::kCorpus);
    g_active.store(true, std::memory_order_release);
    return true;
}

bool AccessTracer::Stop(std::string* error) {
    std::unique_ptr<CorpusWriter> w;
    std::string path;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_active.load()) { if (error) *error = "no recording is running"; return false; }
        g_active.store(false, std::memory_order_release);
        FnHooks::DisarmAll(FnHooks::kCorpus);
        FnHooks::RemoveObserver(g_observer);
        g_observer = UINT32_MAX;
        w = std::move(g_writer);
        path = g_path;
        w->SetMeta("frames", "[" + std::to_string(g_first_frame) + ", " + std::to_string(g_last_frame) + "]");
        w->SetMeta("accesses", std::to_string(g_accesses.load()));
        w->SetMeta("unattributed_accesses", std::to_string(g_unattributed.load()));
    }
    return w->Write(path, error);
}

AccessTracer::Stats AccessTracer::CurrentStats() {
    Stats s;
    std::lock_guard<std::mutex> lock(g_mutex);
    s.accesses = g_accesses.load();
    s.unattributed = g_unattributed.load();
    s.first_frame = g_first_frame;
    s.last_frame = g_last_frame;
    if (g_writer) {
        s.calls = g_writer->Calls();
        s.distinct = g_writer->Distinct();
        s.dropped = g_writer->Dropped();
        s.bytes = g_writer->Bytes();
        s.truncated = g_writer->Truncated();
    }
    return s;
}

void AccessTracer::OnRead(uint32_t addr, const void* bytes, uint32_t n) {
    if (t_top < 0) { if (g_active.load(std::memory_order_relaxed)) g_unattributed.fetch_add(1, std::memory_order_relaxed); return; }
    g_accesses.fetch_add(1, std::memory_order_relaxed);
    Touch(t_frames[static_cast<size_t>(t_top)], Normalize(addr), static_cast<const uint8_t*>(bytes), n, false);
}

void AccessTracer::OnWrite(uint32_t addr, const void* bytes, uint32_t n) {
    if (t_top < 0) { if (g_active.load(std::memory_order_relaxed)) g_unattributed.fetch_add(1, std::memory_order_relaxed); return; }
    g_accesses.fetch_add(1, std::memory_order_relaxed);
    Touch(t_frames[static_cast<size_t>(t_top)], Normalize(addr), static_cast<const uint8_t*>(bytes), n, true);
}

#else  // not a tracing build

bool AccessTracer::Available() { return false; }
bool AccessTracer::Active() { return false; }
bool AccessTracer::Start(const std::string&, const std::vector<uint32_t>&,
                         const std::vector<std::pair<std::string, std::string>>&, uint64_t, std::string* error) {
    if (error) *error = "not a tracing build (configure with -DPS2R_TRACE_ACCESS=ON)";
    return false;
}
bool AccessTracer::Stop(std::string* error) {
    if (error) *error = "not a tracing build (configure with -DPS2R_TRACE_ACCESS=ON)";
    return false;
}
AccessTracer::Stats AccessTracer::CurrentStats() { return Stats{}; }
void AccessTracer::OnRead(uint32_t, const void*, uint32_t) {}
void AccessTracer::OnWrite(uint32_t, const void*, uint32_t) {}

#endif
