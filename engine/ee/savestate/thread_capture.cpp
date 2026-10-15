#include "engine/ee/savestate/thread_capture.h"

#include <cstdio>
#include <exception>
#include <set>
#include <thread>
#include <utility>

#include "engine/ee/cpu/shadow_call_stack.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/sched/ee_run_token.h"

namespace {
thread_local ThreadCapture::ResumeScope* resume_scope = nullptr;
}

ThreadCapture::ThreadCapture(ThreadManager& threads, Proofs proofs)
    : threads_(threads), proofs_(std::move(proofs)) {
    attached_ = threads_.AttachCaptureObserver(this,
        [this](int tid, const std::shared_ptr<ThreadInfo>& info, bool parked) { Observe(tid, info, parked); });
}

ThreadCapture::~ThreadCapture() { threads_.DetachCaptureObserver(this); }

ThreadCapture::ResumeScope::ResumeScope(const Chain& saved, size_t level)
    : saved_(saved), level_(level), previous_(resume_scope) { resume_scope = this; }
ThreadCapture::ResumeScope::~ResumeScope() { resume_scope = previous_; }

bool ThreadCapture::ExpandResumeChain(const Chain& active, const Chain& saved, size_t level,
                                     Chain& complete, std::string& error) {
    if (active.empty() || level >= saved.size() || active.back().func_addr != saved[level].func_addr ||
        active.size() + saved.size() - level - 1 > ThreadSection::kMaxChain) {
        complete.clear();
        error = "Resume dispatcher tail does not match the active logical stack.";
        return false;
    }
    auto expanded = active;
    expanded.back().ra = saved[level].ra;
    expanded.insert(expanded.end(), saved.begin() + static_cast<ptrdiff_t>(level + 1), saved.end());
    complete.swap(expanded);
    error.clear();
    return true;
}

bool ThreadCapture::ReadLocalChain(Chain& out, std::string& error) const {
    out.clear();
    if (!proofs_.stack_complete || !proofs_.stack_complete()) {
        // Name the body the thread is parked in: an installed replacement
        // without continuation entries (its owner's contract gap), or a
        // saturated/rejected push -- so the refusal is attributable.
        ShadowCallStack::Frame top{};
        const bool has_top = ShadowCallStack::Depth() > 0 && ShadowCallStack::Top(0, top);
        char detail[224];
        std::snprintf(detail, sizeof(detail),
            "The owning thread's logical stack may contain omitted frames (depth=%u%s%08x; an opaque installed "
            "body without continuation entries is live, or the shadow stack saturated).",
            ShadowCallStack::Depth(), has_top ? ", innermost frame fn=0x" : ", no frame 0x", has_top ? top.func_addr : 0u);
        error = detail;
        return false;
    }
    const auto depth = ShadowCallStack::Depth();
    if (!depth || depth > ThreadSection::kMaxChain) {
        error = "The owning thread's logical stack is empty or exceeds the format cap.";
        return false;
    }
    for (uint32_t i = 0; i < depth; ++i) {
        ShadowCallStack::Frame frame;
        if (!ShadowCallStack::Top(i, frame)) {
            out.clear();
            error = "The owning thread's logical stack changed while captured.";
            return false;
        }
        out.push_back({frame.func_addr, frame.ra});
    }
    if (resume_scope) {
        if (resume_scope->previous_) {
            error = "Nested resume dispatch needs an explicit outer-scope composition.";
            out.clear();
            return false;
        }
        return ExpandResumeChain(out, resume_scope->saved_, resume_scope->level_, out, error);
    }
    error.clear();
    return true;
}

bool ThreadCapture::ValidateEntries(const ThreadSection::Record& record, std::string& error) const {
    if (!proofs_.can_resume || record.chain.empty()) {
        error = "No installed-entry proof or no logical chain.";
        return false;
    }
    if (record.chain.back().ra != 0) {
        error = "Logical chain is missing its root entry frame.";
        return false;
    }
    for (size_t i = 0; i < record.chain.size(); ++i) {
        const auto function = record.chain[i].func_addr;
        const auto entry = i ? record.chain[i - 1].ra :
            (record.is_boundary ? record.ctx.cpuRegs.GPR.r[31].UL[0] : 0);
        if (!function || (function & 3) || (entry & 3) ||
            ((i || record.is_boundary) && !entry) || !proofs_.can_resume(function, entry)) {
            char detail[128];
            std::snprintf(detail, sizeof(detail),
                "Unproven installed continuation at level %zu (fn=0x%08x, entry=0x%08x).", i, function, entry);
            error = detail;
            return false;
        }
    }
    return true;
}

void ThreadCapture::Observe(int tid, const std::shared_ptr<ThreadInfo>& info, bool parked) {
    if (!parked) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = parked_.find(tid);
        if (found != parked_.end() && found->second->identity.lock() == info) parked_.erase(found);
        return;
    }
    auto copy = std::make_shared<Parked>();
    copy->identity = info;
    {
        std::lock_guard<std::mutex> lock(info->m);
        copy->incarnation = info->capture_incarnation;
        copy->revision = info->capture_revision;
        copy->record = ThreadSection::LogicalRecord(tid, *info);
        if (tid != threads_.CurrentTid() || copy->incarnation != threads_.CurrentIncarnation() ||
            info->capture_owner != std::this_thread::get_id() || !info->ctx || !info->capture_parked) {
            copy->error = "Park notification does not own a live context.";
        } else {
            copy->record.ctx = *info->ctx; // Read only on that context's owner.
            copy->record.has_ctx = true;
        }
    }
    try {
        if (copy->error.empty() && ReadLocalChain(copy->record.chain, copy->error))
            ValidateEntries(copy->record, copy->error);
    } catch (const std::exception& failure) {
        copy->error = failure.what();
    } catch (...) {
        copy->error = "Continuation proof provider failed.";
    }
    std::lock_guard<std::mutex> lock(mutex_);
    parked_[tid] = std::move(copy);
}

bool ThreadCapture::CaptureBoundary(int tid, const CpuContext& completed_context,
                                    Records& out, std::string& error) {
    out.clear();
    try {
        return CollectBoundary(tid, completed_context, out, error);
    } catch (const std::exception& failure) {
        error = failure.what();
    } catch (...) {
        error = "Continuation proof provider failed.";
    }
    return false;
}

bool ThreadCapture::CollectBoundary(int tid, const CpuContext& completed_context,
                                    Records& out, std::string& error) {
    auto fail = [&](const std::string& reason) { error = reason; return false; };
    auto& token = EeRunToken::Instance();
    if (!attached_ || !threads_.Running() || !token.CanCaptureCurrentThread() ||
        token.CurrentTid() != tid || threads_.CurrentTid() != tid)
        return fail("Fresh capture requires its attached, active game execution owner.");
    const auto table = threads_.SnapshotTable();
    if (table.empty() || table.size() > ThreadSection::kMaxThreads)
        return fail("Invalid live thread table size.");
    std::lock_guard<std::mutex> lock(mutex_);
    Records result;
    std::vector<std::pair<uint64_t, uint64_t>> identities;
    std::set<std::pair<int, int>> object_waits;
    bool boundary_found = false;
    for (const auto& [id, info] : table) {
        if (id <= 0 || id >= 256 || !info) return fail("Invalid live thread identity.");
        std::lock_guard<std::mutex> info_lock(info->m);
        identities.emplace_back(info->capture_incarnation, info->capture_revision);
        auto record = ThreadSection::LogicalRecord(id, *info);
        const auto prefix = "Thread " + std::to_string(id) + ": ";
        if (record.started) {
            if (info->terminated || info->force_release || info->released)
                return fail(prefix + "wait completion or exit is in flight.");
            if (id == tid) {
                if (info->capture_owner != std::this_thread::get_id() ||
                    info->capture_incarnation != threads_.CurrentIncarnation() ||
                    info->ctx != &completed_context || info->capture_parked)
                    return fail(prefix + "boundary does not own the current context.");
                record.ctx = completed_context;
                record.has_ctx = record.is_boundary = boundary_found = true;
                if (!ReadLocalChain(record.chain, error)) return false;
            } else {
                const auto found = parked_.find(id);
                if (!info->capture_parked || found == parked_.end() ||
                    found->second->identity.lock() != info ||
                    found->second->incarnation != info->capture_incarnation ||
                    found->second->revision != info->capture_revision)
                    return fail(prefix + "no immutable park for this worker incarnation.");
                const auto& parked = *found->second;
                if (!parked.error.empty()) return fail(prefix + parked.error);
                if (info->capture_suspended || record.suspend_count ||
                    record.status != THS_WAIT || record.wakeup_count ||
                    (record.wait_type != TSW_SLEEP && record.wait_type != TSW_SEMA && record.wait_type != TSW_EVENT))
                    return fail(prefix + "the current suspension/wait shape cannot be replayed.");
                if (record.wait_type != parked.record.wait_type || record.wait_id != parked.record.wait_id)
                    return fail(prefix + "wait identity changed after capture.");
                record.ctx = parked.record.ctx;
                record.chain = parked.record.chain;
                record.has_ctx = true;
                if (!proofs_.can_restart_wait || record.chain.empty() ||
                    !proofs_.can_restart_wait(record.chain.front().func_addr, record.wait_type)) {
                    char detail[128];
                    std::snprintf(detail, sizeof(detail),
                        "Installed wait body is not proven safe to restart (fn=0x%08x, wait_type=%d).",
                        record.chain.empty() ? 0u : record.chain.front().func_addr, record.wait_type);
                    return fail(prefix + detail);
                }
                if (record.wait_type != TSW_SLEEP && !object_waits.emplace(record.wait_type, record.wait_id).second)
                    return fail(prefix + "multiple object waiters need a preserved replay order.");
            }
            if (!ValidateEntries(record, error)) { error = prefix + error; return false; }
        }
        result.push_back(std::move(record));
    }
    if (!boundary_found) return fail("The live table has no boundary owner.");
    // An off-token worker can begin unpark/exit while the token holder copies.
    // Recheck every incarnation/revision and table identity before admission.
    const auto after = threads_.SnapshotTable();
    if (after != table) return fail("The live thread table changed during capture.");
    for (size_t i = 0; i < table.size(); ++i) {
        const auto& info = table[i].second;
        std::lock_guard<std::mutex> info_lock(info->m);
        if (identities[i] != std::make_pair(info->capture_incarnation, info->capture_revision))
            return fail("A worker left its captured continuation during collection.");
    }
    out = std::move(result);
    error.clear();
    return true;
}

void ThreadCapture::ResetAfterRestore() {
    std::lock_guard<std::mutex> lock(mutex_);
    parked_.clear();
}
