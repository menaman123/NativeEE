#include "engine/ee/savestate/thread_section.h"

#include <cstring>
#include <memory>

#include "engine/core/savestate/payload_reader.h"
#include "engine/ee/hle/thread_manager.h"

using ps2r::savestate::Blob;
using ps2r::savestate::PayloadReader;
using ps2r::savestate::SectionSpec;

namespace {

void PutU32(std::vector<uint8_t>& v, uint32_t x) {
    const uint8_t b[4] = {static_cast<uint8_t>(x), static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x >> 16),
                          static_cast<uint8_t>(x >> 24)};
    v.insert(v.end(), b, b + 4);
}
void PutI32(std::vector<uint8_t>& v, int32_t x) { PutU32(v, static_cast<uint32_t>(x)); }

}  // namespace

ThreadSection::ThreadSection(ThreadManager& threads) : threads_(threads) {}

void ThreadSection::Register(ps2r::savestate::SectionRegistry& registry) { registry.Register(Spec()); }

SectionSpec ThreadSection::Spec() {
    SectionSpec s;
    s.tag = ps2r::savestate::TAG_THRD;
    s.version = 1;
    s.required = true;
    s.owner = "ThreadSection";
    s.serialize = [this](std::vector<uint8_t>& out) { Serialize(out); };
    s.validate = [](const Blob& b, std::string* err) {
        std::vector<Record> scratch;
        return Parse(b.payload, scratch, err);
    };
    s.restore = [this](const Blob& b, std::string* err) { return Restore(b.payload, err); };
    return s;
}

bool ThreadSection::Parse(const std::vector<uint8_t>& payload, std::vector<Record>& out, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    out.clear();
    PayloadReader r(payload);
    uint32_t count = 0;
    if (!r.Get(&count) || count > kMaxThreads) return fail("THRD: bad thread count");
    for (uint32_t i = 0; i < count; i++) {
        Record rec;
        uint8_t started = 0, has_ctx = 0, is_boundary = 0, pad = 0;
        uint32_t ctx_size = 0, chain_depth = 0;
        const bool ok = r.Get(&rec.tid) && r.Get(&rec.entry) && r.Get(&rec.stack) && r.Get(&rec.stack_size) &&
                        r.Get(&rec.gp) && r.Get(&rec.priority) && r.Get(&rec.attr) && r.Get(&rec.option) &&
                        r.Get(&rec.arg) && r.Get(&rec.status) && r.Get(&rec.wait_type) && r.Get(&rec.wait_id) &&
                        r.Get(&rec.wakeup_count) && r.Get(&rec.current_priority) && r.Get(&rec.suspend_count) &&
                        r.Get(&started) && r.Get(&has_ctx) && r.Get(&is_boundary) && r.Get(&pad) &&
                        r.Get(&rec.heap_base) && r.Get(&rec.heap_end) && r.Get(&ctx_size) && r.Get(&chain_depth);
        if (!ok) return fail("THRD: truncated record header");
        if (ctx_size != sizeof(CpuContext)) return fail("THRD: CpuContext size mismatch (file != runtime layout)");
        if (chain_depth > kMaxChain) return fail("THRD: chain depth over the cap");
        rec.started = started != 0;
        rec.has_ctx = has_ctx != 0;
        rec.is_boundary = is_boundary != 0;
        if (rec.has_ctx && !r.GetBytes(&rec.ctx, sizeof(CpuContext))) return fail("THRD: truncated CpuContext");
        rec.chain.resize(chain_depth);
        for (uint32_t c = 0; c < chain_depth; c++)
            if (!r.Get(&rec.chain[c].func_addr) || !r.Get(&rec.chain[c].ra)) return fail("THRD: truncated chain");
        out.push_back(std::move(rec));
    }
    if (r.Remaining() != 0) return fail("THRD: trailing bytes");
    return true;
}

void ThreadSection::Emit(const std::vector<Record>& records, std::vector<uint8_t>& out) {
    PutU32(out, static_cast<uint32_t>(records.size()));
    for (const Record& rec : records) {
        PutI32(out, rec.tid);
        PutU32(out, rec.entry); PutU32(out, rec.stack); PutU32(out, rec.stack_size); PutU32(out, rec.gp);
        PutU32(out, rec.priority); PutU32(out, rec.attr); PutU32(out, rec.option); PutU32(out, rec.arg);
        PutI32(out, rec.status); PutI32(out, rec.wait_type); PutI32(out, rec.wait_id);
        PutI32(out, rec.wakeup_count); PutI32(out, rec.current_priority); PutI32(out, rec.suspend_count);
        out.push_back(rec.started ? 1 : 0);
        out.push_back(rec.has_ctx ? 1 : 0);
        out.push_back(rec.is_boundary ? 1 : 0);
        out.push_back(0);
        PutU32(out, rec.heap_base); PutU32(out, rec.heap_end);
        PutU32(out, static_cast<uint32_t>(sizeof(CpuContext)));
        PutU32(out, static_cast<uint32_t>(rec.chain.size()));
        if (rec.has_ctx) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&rec.ctx);
            out.insert(out.end(), p, p + sizeof(CpuContext));
        }
        for (const ChainFrame& f : rec.chain) { PutU32(out, f.func_addr); PutU32(out, f.ra); }
    }
}

ThreadSection::Record ThreadSection::LogicalRecord(int tid, const ThreadInfo& info) {
    Record rec;
    rec.tid = tid;
    rec.entry = info.entry; rec.stack = info.stack; rec.stack_size = info.stack_size; rec.gp = info.gp;
    rec.priority = info.priority; rec.attr = info.attr; rec.option = info.option; rec.arg = info.arg;
    rec.status = info.status; rec.wait_type = info.wait_type; rec.wait_id = info.wait_id;
    rec.wakeup_count = info.wakeup_count; rec.current_priority = info.current_priority;
    rec.suspend_count = info.suspend_count;
    rec.started = info.started;
    rec.heap_base = info.heap_base; rec.heap_end = info.heap_end;
    return rec;
}

void ThreadSection::Serialize(std::vector<uint8_t>& out) const {
    std::vector<Record> records;
    const auto table = threads_.SnapshotTable();
    auto record_of = [&](int tid, ThreadInfo& info, const Record* parked) {
        std::lock_guard<std::mutex> lock(info.m);
        Record rec = LogicalRecord(tid, info);
        if (parked) {
            rec.ctx = parked->ctx; rec.chain = parked->chain;
            rec.has_ctx = parked->has_ctx; rec.is_boundary = parked->is_boundary;
        }
        return rec;
    };
    auto in_table = [&](int tid) -> ThreadInfo* {
        for (const auto& e : table) if (e.first == tid) return e.second.get();
        return nullptr;
    };
    // Restored records first, in file order (byte-identical re-serialization).
    for (const Record& parked : restored_)
        if (ThreadInfo* info = in_table(parked.tid)) records.push_back(record_of(parked.tid, *info, &parked));
    for (const auto& e : table)
        if (!RestoredFor(e.first)) records.push_back(record_of(e.first, *e.second, nullptr));
    Emit(records, out);
}

bool ThreadSection::Restore(const std::vector<uint8_t>& payload, std::string* err) {
    std::vector<Record> records;
    if (!Parse(payload, records, err)) return false;
    std::vector<ThreadManager::TableEntry> table;
    for (const Record& rec : records) {
        auto info = std::make_shared<ThreadInfo>();
        info->entry = rec.entry; info->stack = rec.stack; info->stack_size = rec.stack_size; info->gp = rec.gp;
        info->priority = rec.priority; info->attr = rec.attr; info->option = rec.option; info->arg = rec.arg;
        info->status = rec.status; info->wait_type = rec.wait_type; info->wait_id = rec.wait_id;
        info->wakeup_count = rec.wakeup_count; info->current_priority = rec.current_priority;
        info->suspend_count = rec.suspend_count;
        info->started = rec.started;
        info->heap_base = rec.heap_base; info->heap_end = rec.heap_end;
        table.emplace_back(rec.tid, std::move(info));
    }
    threads_.InstallTable(table);
    restored_ = std::move(records);
    return true;
}

const ThreadSection::Record* ThreadSection::RestoredFor(int tid) const {
    for (const Record& r : restored_) if (r.tid == tid) return &r;
    return nullptr;
}
