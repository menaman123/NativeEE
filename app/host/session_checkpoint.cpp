#include "app/host/session_checkpoint.h"

#include <cstdio>
#include <exception>
#include <set>
#include <utility>

#include "engine/ee/savestate/thread_capture.h"

namespace ps2r::app {
using Phase = SessionControls::Phase;

SessionCheckpoint::SessionCheckpoint(const savestate::SectionRegistry& sections, Sources sources)
    : sections_(sections), sources_(std::move(sources)) {}

bool SessionCheckpoint::ValidateContinuations(const Records& records, const EntryCheck& can_resume,
                                             std::string& error) {
    auto fail = [&](const std::string& why) { error = why; return false; };
    if (!can_resume) return fail("No installed-entry validation provider.");
    if (records.empty() || records.size() > ThreadSection::kMaxThreads)
        return fail("Invalid live thread count.");
    std::set<int32_t> ids;
    unsigned boundaries = 0;
    for (const auto& record : records) {
        const auto prefix = "Thread " + std::to_string(record.tid) + ": ";
        if (record.tid <= 0 || record.tid >= 256 || !ids.insert(record.tid).second)
            return fail("Invalid or duplicate live thread identity.");
        if (!record.started) {
            if (record.has_ctx || !record.chain.empty() || record.is_boundary)
                return fail(prefix + "dormant thread carries a stale continuation.");
            continue;
        }
        if (!record.has_ctx || record.chain.empty() || record.chain.size() > ThreadSection::kMaxChain)
            return fail(prefix + "missing or oversized fresh continuation.");
        if (record.is_boundary) ++boundaries;
        for (size_t i = 0; i < record.chain.size(); ++i) {
            const uint32_t entry = i ? record.chain[i - 1].ra :
                (record.is_boundary ? record.ctx.cpuRegs.GPR.r[31].UL[0] : 0);
            const uint32_t function = record.chain[i].func_addr;
            if (!function || (function & 3) || (entry & 3) ||
                ((i != 0 || record.is_boundary) && entry == 0) || !can_resume(function, entry)) {
                char detail[128];
                std::snprintf(detail, sizeof(detail),
                    "Unproven installed continuation at level %zu (fn=0x%08x, entry=0x%08x).", i, function, entry);
                return fail(prefix + detail);
            }
        }
    }
    if (boundaries != 1) return fail("A live capture needs exactly one completed boundary owner.");
    error.clear();
    return true;
}

bool SessionCheckpoint::ExpandResumeChain(const std::vector<ThreadSection::ChainFrame>& active,
                                         const std::vector<ThreadSection::ChainFrame>& saved,
                                         size_t level, std::vector<ThreadSection::ChainFrame>& complete,
                                         std::string& error) {
    return ThreadCapture::ExpandResumeChain(active, saved, level, complete, error);
}

SessionCheckpoint::Result SessionCheckpoint::Refuse(Result result, Phase phase, std::string detail) {
    result.phase = phase;
    result.detail = std::move(detail);
    std::lock_guard<std::mutex> lock(mutex_);
    active_.reset();
    expected_completion_ = 0;
    return result;
}

SessionCheckpoint::Result SessionCheckpoint::Capture(uint64_t request, std::string path, savestate::FileMeta meta) {
    Result result{request, Phase::Refused, std::move(path), {}, meta.vsync_frame};
    if (!request || result.path.empty() || result.path.size() > SessionControls::kTextLimit ||
        result.path.find('\0') != std::string::npos) {
        result.detail = "Invalid reproduction request or destination.";
        return result;
    }
    if (!sources_.on_game_owner || !sources_.on_game_owner()) {
        result.detail = "Capture must run on the game execution owner.";
        return result;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_) {
            result.detail = "A reproduction capture or unreported write is already pending.";
            return result;
        }
        active_ = result;
    }
    try {
        std::string error;
        if (!sources_.quiescent)
            return Refuse(result, Phase::Refused, "No quiescence provider.");
        if (!sources_.quiescent(error))
            return Refuse(result, Phase::Waiting, error.empty() ? "Service state is still busy." : error);
        Records threads;
        if (!sources_.fresh_threads || !sources_.fresh_threads(threads, error))
            return Refuse(result, Phase::Refused, error.empty() ? "Fresh continuations are unavailable." : error);
        if (!ValidateContinuations(threads, sources_.can_resume, error))
            return Refuse(result, Phase::Refused, error);
        if (!sections_.Has(savestate::TAG_THRD))
            return Refuse(result, Phase::Refused, "The active thread section has no registered owner.");

        savestate::WriteJob job;
        job.path = result.path;
        job.meta = meta;
        size_t total = 0;
        for (const auto& spec : sections_.Specs()) {
            savestate::Blob blob;
            blob.tag = spec.tag;
            blob.version = spec.version;
            blob.flags = (spec.required || spec.capture_required) ? savestate::kSectionRequired : 0;
            if (spec.tag == savestate::TAG_THRD) {
                if (spec.version != 1)
                    return Refuse(result, Phase::Refused, "Unsupported live thread section version.");
                ThreadSection::Emit(threads, blob.payload);
            } else if (spec.serialize) {
                spec.serialize(blob.payload);
            } else {
                return Refuse(result, Phase::Refused, std::string("Section has no capture provider: ") +
                    (spec.owner ? spec.owner : "(unnamed)"));
            }
            total += blob.payload.size();
            if (blob.payload.size() > savestate::kMaxSectionPayload || total > (256u << 20))
                return Refuse(result, Phase::Failed, "Reproduction capture exceeds its byte budget.");
            job.sections.push_back(std::move(blob));
        }
        // Every service keeps its owner, version and validator. No missing
        // state is substituted and no legacy unknown-section exception is added.
        if (!sections_.Validate(job.sections, &error))
            return Refuse(result, Phase::Failed, error);
        job.meta.file_version = savestate::SectionRegistry::FileVersionFor(job.sections);
        const auto completion = writer_.CompletedCount() + 1;
        result.phase = Phase::Working;
        result.detail = "Captured owned state; writing reproduction.";
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_ = result;
        }
        if (!writer_.TryEnqueue(std::move(job)))
            return Refuse(result, Phase::Refused, "The reproduction writer is occupied.");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            expected_completion_ = completion;
        }
        return result;
    } catch (const std::exception& failure) {
        return Refuse(result, Phase::Failed, failure.what());
    } catch (...) {
        return Refuse(result, Phase::Failed, "A capture provider threw an unknown exception.");
    }
}

std::optional<SessionCheckpoint::Result> SessionCheckpoint::Poll() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || !expected_completion_ || writer_.CompletedCount() < expected_completion_) return {};
    const auto written = writer_.LastResult();
    Result result = *active_;
    const bool succeeded = written.ok && written.path == result.path;
    result.phase = succeeded ? Phase::Succeeded : Phase::Failed;
    result.detail = succeeded ? "Reproduction written: " + written.path :
        (written.error.empty() ? "The reproduction writer returned an unexpected result." : written.error);
    active_.reset();
    expected_completion_ = 0;
    return result;
}

void SessionCheckpoint::Join() { writer_.JoinForExit(); }

bool SessionCheckpoint::Busy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_.has_value();
}

}  // namespace ps2r::app
