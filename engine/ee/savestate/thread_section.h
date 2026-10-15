#pragma once
// ThreadSection: the THRD savestate section (thread table + parked guest
// contexts + shadow call chains) over ThreadManager.
//
// Wire layout (frozen; the anchor states carry it):
//   u32 count; then per thread the packed 80-byte record header
//   { i32 tid; u32 entry, stack, stack_size, gp, priority, attr, option, arg;
//     i32 status, wait_type, wait_id, wakeup_count, current_priority,
//     suspend_count; u8 started, has_ctx, is_boundary, pad; u32 heap_base,
//     heap_end; u32 ctx_size; u32 chain_depth }
//   then [ctx_size bytes of CpuContext if has_ctx] then chain_depth x
//   { u32 func_addr; u32 ra }.
// ctx_size is the CpuContext layout guard: it must equal sizeof(CpuContext)
// (1600, pinned by EeCpuStateLayout) or the whole load is rejected before
// any state is touched.
//
// Restore installs the records into ThreadManager (info only -- no host
// worker starts) and keeps every parked context + chain here, in file order,
// for the relaunch step; Serialize emits restored records in that same
// order (then any newer thread by tid), so a restored image re-serializes
// byte-identical. Threads created live serialize with has_ctx = 0 until the
// park capture path (later phase) supplies their contexts.
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"
#include "engine/ee/cpu/cpu_state.h"

class ThreadManager;
struct ThreadInfo;

class ThreadSection {
public:
    static constexpr size_t kRecordHeaderBytes = 80;
    static constexpr uint32_t kMaxThreads = 256;
    static constexpr uint32_t kMaxChain = 128;

    struct ChainFrame {
        uint32_t func_addr = 0;
        uint32_t ra = 0;
    };
    struct Record {
        int32_t tid = 0;
        uint32_t entry = 0, stack = 0, stack_size = 0, gp = 0, priority = 0, attr = 0, option = 0, arg = 0;
        int32_t status = 0, wait_type = 0, wait_id = 0, wakeup_count = 0, current_priority = 0, suspend_count = 0;
        bool started = false;
        bool has_ctx = false;
        bool is_boundary = false;
        uint32_t heap_base = 0, heap_end = 0;
        CpuContext ctx{};
        std::vector<ChainFrame> chain;
    };

    explicit ThreadSection(ThreadManager& threads);

    void Register(ps2r::savestate::SectionRegistry& registry);
    ps2r::savestate::SectionSpec Spec();

    // Pure codec (no live state): parse the payload into records / emit records.
    static bool Parse(const std::vector<uint8_t>& payload, std::vector<Record>& out, std::string* err);
    static void Emit(const std::vector<Record>& records, std::vector<uint8_t>& out);
    // Logical metadata only, never a restored continuation. Caller holds
    // info.m. Fresh capture supplies ctx/chain from their execution owner.
    static Record LogicalRecord(int tid, const ThreadInfo& info);

    void Serialize(std::vector<uint8_t>& out) const;
    bool Restore(const std::vector<uint8_t>& payload, std::string* err);

    // The parked context + chain restored for `tid` (nullptr when none).
    const Record* RestoredFor(int tid) const;
    const std::vector<Record>& Restored() const { return restored_; }
    size_t RestoredCount() const { return restored_.size(); }

private:
    ThreadManager& threads_;
    std::vector<Record> restored_;  // file order
};
