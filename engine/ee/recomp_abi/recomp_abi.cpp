// Definitions the generated TUs reference and no engine class owns
// (PHASE3_PLAN 3.4 rows 9, 12, 24, 25, 27) plus the RecompAbi memory binding.
#include "engine/ee/recomp_abi/recomp_abi.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <map>
#include <mutex>
#include <tuple>

#include "engine/ee/recomp_abi/host_app/cdvd_stream.h"
#include "engine/ee/recomp_abi/host_app/vu.h"

namespace {
FastMem* g_bound_memory = nullptr;
}

void RecompAbi::BindMemory(FastMem* mem) { g_bound_memory = mem; }
FastMem& RecompAbi::Memory() {
    assert(g_bound_memory && "RecompAbi::BindMemory must run before generated code");
    return *g_bound_memory;
}
bool RecompAbi::HasMemory() { return g_bound_memory != nullptr; }

// Row 12: an UNOPENED ofstream. Its failbit is set, so every `g_logFile << x`
// site in the generated code (42k of them) short-circuits in the sentry and
// formats nothing. Never opened by the engine.
std::ofstream g_logFile;

// Row 9: call census is a core/diag concern; the generated inline hooks see
// `false` and never call the (empty) bodies.
bool g_call_census_enabled = false;
void call_census_enter(uint32_t) {}
void call_census_exit() {}

// Row 24: no stream-poll completion marker exists before the Phase-4 stream HLE.
bool StreamPollCompleteMarker(uint32_t) { return false; }

// Row 25: diagnostic counters the common TU's diag arms write; never read here.
std::atomic<uint64_t> g_alloc_trace_seq{0};
std::mutex g_alloc_live_mutex;
std::map<uint32_t, std::tuple<uint32_t, uint32_t, uint32_t, uint64_t>> g_alloc_live;
uint32_t g_pose_wb_count = 0;
std::atomic<uint32_t> g_ring_watch_addr{0};

// Row 27: the two vector-unit objects. Phase-3 form: reaching a VCALLMS /
// VU1 kick before the SIF park is a construction finding, so both park.
Vu0Macro g_vu0;
Vu1Window g_vu1;

// P3-SEAM-3: no PTC-stub record exists before the Phase-4 stream HLE (INERT).
#include "engine/ee/recomp_abi/host_app/hle_heap.h"
const std::vector<uint8_t>* GetPtcRecord(const std::string&) { return nullptr; }
