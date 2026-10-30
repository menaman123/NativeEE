#include "engine/gif/draw_attribution.h"

#include <atomic>
#include <mutex>
#include <unordered_map>

#include "engine/ee/cpu/shadow_call_stack.h"

namespace {
std::mutex g_mutex;
std::vector<DrawAttribution::Packet> g_frame;        // the frame being delivered
std::vector<DrawAttribution::Packet> g_last_frame;   // the last completed frame
uint32_t g_last_seq = 0;
std::unordered_map<uint32_t, DrawAttribution::CensusRow> g_census;
std::atomic<uint64_t> g_delivered{0};
std::atomic<uint64_t> g_unattributed{0};
std::atomic<bool> g_enabled{false};
thread_local uint32_t t_current_fn = 0;
thread_local GifPath t_current_path = GifPath::Path3;
}  // namespace

uint32_t DrawAttribution::SubmitterFn() {
    ShadowCallStack::Frame top;
    return ShadowCallStack::Top(0, top) ? top.func_addr : 0u;
}

void DrawAttribution::Enable(bool on) { g_enabled.store(on, std::memory_order_relaxed); }
bool DrawAttribution::Enabled() { return g_enabled.load(std::memory_order_relaxed); }

void DrawAttribution::SetCurrent(uint32_t fn, GifPath path, uint32_t bytes) {
    t_current_fn = fn;
    t_current_path = path;
    if (!g_enabled.load(std::memory_order_relaxed)) return;
    g_delivered.fetch_add(1, std::memory_order_relaxed);
    if (!fn) g_unattributed.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_frame.size() < kFrameTableMax) g_frame.push_back(Packet{fn, path, bytes});
    CensusRow& row = g_census[fn];
    row.fn = fn;
    row.packets++;
    row.bytes += bytes;
}

void DrawAttribution::EndPass() { t_current_fn = 0; }
uint32_t DrawAttribution::CurrentFn() { return t_current_fn; }
GifPath DrawAttribution::CurrentPath() { return t_current_path; }

void DrawAttribution::FrameBoundary(uint32_t seq) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_last_frame.swap(g_frame);
    g_frame.clear();
    g_last_seq = seq;
}

std::vector<DrawAttribution::Packet> DrawAttribution::LastFrame(uint32_t* seq) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (seq) *seq = g_last_seq;
    return g_last_frame;
}

std::vector<DrawAttribution::CensusRow> DrawAttribution::Census() {
    std::vector<CensusRow> out;
    std::lock_guard<std::mutex> lock(g_mutex);
    out.reserve(g_census.size());
    for (const auto& kv : g_census) out.push_back(kv.second);
    return out;
}

void DrawAttribution::ResetCensus() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_census.clear();
}

uint64_t DrawAttribution::Delivered() { return g_delivered.load(std::memory_order_relaxed); }
uint64_t DrawAttribution::Unattributed() { return g_unattributed.load(std::memory_order_relaxed); }
