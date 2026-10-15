#include "app/host/session_restart.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include <set>
#include <tuple>
#include <utility>

#include "engine/ee/hle/thread_manager.h"

namespace ps2r::app {
using Phase = SessionControls::Phase;
using savestate::Blob;

struct SessionRestart::Image {
    uint64_t request = 0;
    std::vector<uint8_t> bytes;
    savestate::FileMeta meta;
    std::vector<Blob> sections;
    SessionCheckpoint::Records records;
    Current current;
    Epoch epoch;
    uint64_t registry_revision = 0;
    using Owner = std::tuple<uint32_t, uint32_t, bool, std::string, bool, bool, bool>;
    std::vector<Owner> owners;
};

SessionRestart::SessionRestart(ThreadManager& threads, const savestate::SectionRegistry& sections,
                               Sources sources, Hooks hooks, ThreadRelauncher::EnterFn enter)
    : threads_(threads), sections_(sections), sources_(std::move(sources)), hooks_(std::move(hooks)),
      enter_bound_(bool(enter)), relauncher_(threads, std::move(enter)) {}

bool SessionRestart::SamePlan(const Plan& left, const Plan& right) {
    return left.providers == right.providers && left.required_sections == right.required_sections &&
        left.optional_missing == right.optional_missing && left.legacy_unowned == right.legacy_unowned &&
        left.clock == right.clock && left.recording == right.recording;
}

bool SessionRestart::SameOwners(const Image& image) const {
    if (sections_.Count() != image.owners.size() || sections_.Revision() != image.registry_revision) return false;
    for (size_t i = 0; i < image.owners.size(); ++i) {
        const auto& spec = sections_.Specs()[i];
        if (image.owners[i] != Image::Owner{spec.tag, spec.version, spec.required, spec.owner ? spec.owner : "",
                bool(spec.validate), bool(spec.restore), bool(spec.absent)}) return false;
    }
    return true;
}

bool SessionRestart::SameCurrent(const Image& image, bool restored) const {
    const auto current = sources_.current();
    return SamePlan(current.plan, image.current.plan) && current.recording == image.current.recording &&
        current.frame == (restored ? image.meta.vsync_frame : image.current.frame) && SameOwners(image);
}

bool SessionRestart::BeginPrepare(uint64_t request, Result& result) {
    result.request = request;
    if (!request || !sources_.on_game_owner || !sources_.on_game_owner()) {
        result.detail = "Restore preflight requires its game execution owner and request identity.";
        return false;
    }
    if (!sources_.current || !sources_.can_resume || !sources_.can_restart_wait ||
        !sources_.validate_epoch || !sources_.current_quiescent || !sources_.validate_image ||
        !sources_.on_supervisor || !sources_.closing ||
        !hooks_.join_workers_and_writers || !hooks_.begin_epoch || !hooks_.apply_image ||
        !hooks_.reset_after_restore || !hooks_.cancel_workers || !enter_bound_ ||
        bool(hooks_.prepare_image) != bool(hooks_.discard_prepared)) {
        result.detail = "Restore composition is incomplete.";
        return false;
    }
    if (sources_.closing()) {
        result.detail = "Session close takes priority over restore.";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (preparing_ || restarting_ || prepared_) {
        result.detail = "A restore is already being prepared or restarted.";
        return false;
    }
    preparing_ = true;
    return true;
}

SessionRestart::Result SessionRestart::Reject(Result result, Phase phase, std::string detail) {
    if (hooks_.discard_prepared) hooks_.discard_prepared();
    result.phase = phase;
    result.detail = std::move(detail);
    std::lock_guard<std::mutex> lock(mutex_);
    preparing_ = false;
    return result;
}

SessionRestart::Result SessionRestart::PrepareFile(uint64_t request, const std::string& path) {
    Result result;
    if (!BeginPrepare(request, result)) return result;
    try {
        if (path.empty() || path.size() > SessionControls::kTextLimit || path.find('\0') != std::string::npos)
            return Reject(result, Phase::Refused, "Invalid reproduction path.");
        const auto close_file = [](FILE* f) { if (f) std::fclose(f); };   // a lambda deleter: GCC drops fclose's attributes from `int (*)(FILE*)`
        std::unique_ptr<FILE, decltype(close_file)> file(std::fopen(path.c_str(), "rb"), close_file);
        if (!file) return Reject(result, Phase::Failed, "Cannot open reproduction: " + path);
        std::vector<uint8_t> bytes;
        std::array<uint8_t, 65536> chunk;
        for (;;) {
            const size_t read = std::fread(chunk.data(), 1, chunk.size(), file.get());
            if (read > kMaxImageBytes - bytes.size())
                return Reject(result, Phase::Refused, "Reproduction exceeds the image byte budget.");
            bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + static_cast<ptrdiff_t>(read));
            if (read == chunk.size()) continue;
            if (std::ferror(file.get())) return Reject(result, Phase::Failed, "Cannot read reproduction: " + path);
            break;
        }
        return Prepare(request, std::move(bytes));
    } catch (const std::exception& error) {
        return Reject(result, Phase::Failed, error.what());
    } catch (...) {
        return Reject(result, Phase::Failed, "Restore file provider failed.");
    }
}

SessionRestart::Result SessionRestart::PrepareImage(uint64_t request, std::vector<uint8_t> image) {
    Result result;
    if (!BeginPrepare(request, result)) return result;
    return Prepare(request, std::move(image));
}

SessionRestart::Result SessionRestart::Prepare(uint64_t request, std::vector<uint8_t> bytes) {
    Result result;
    result.request = request;
    try {
        if (bytes.empty() || bytes.size() > kMaxImageBytes)
            return Reject(result, Phase::Refused, "Invalid reproduction image size.");
        auto image = std::make_shared<Image>();
        image->request = request;
        image->bytes = std::move(bytes);
        std::string error;
        if (!savestate::ParseStateFile(image->bytes.data(), image->bytes.size(), &image->meta, &image->sections, &error))
            return Reject(result, Phase::Refused, error);
        image->current = sources_.current();
        image->epoch = {image->current.frame, image->meta.vsync_frame,
                        image->current.plan.clock, image->current.plan.recording};
        if (!Validate(*image, error)) return Reject(result, Phase::Refused, error);
        // All image/provider/entry validation precedes even read-only current
        // state checks. These callbacks may refuse; they may not reset owners.
        if (!sources_.validate_epoch(image->epoch, error) || !sources_.current_quiescent(error))
            return Reject(result, Phase::Refused, error.empty() ? "Current session cannot restart safely." : error);
        if (hooks_.prepare_image && !hooks_.prepare_image(image->bytes, error))
            return Reject(result, Phase::Refused, error.empty() ? "Selected restore resources could not be prepared." : error);
        result.phase = Phase::Working;
        result.frame = image->meta.vsync_frame;
        result.detail = "Reproduction validated; awaiting owner unwind and restart.";
        std::lock_guard<std::mutex> lock(mutex_);
        prepared_ = std::move(image);
        preparing_ = false;
        return result;
    } catch (const std::exception& error) {
        return Reject(result, Phase::Failed, error.what());
    } catch (...) {
        return Reject(result, Phase::Failed, "Restore preflight provider failed.");
    }
}

bool SessionRestart::Validate(Image& image, std::string& error) const {
    auto fail = [&](std::string why) { error = std::move(why); return false; };
    const auto& plan = image.current.plan;
    image.registry_revision = sections_.Revision();
    if (plan.providers.empty() || plan.providers.size() > SessionControls::kTextLimit ||
        plan.clock == Clock::Unspecified || plan.recording == Recording::Unspecified)
        return fail("Restore needs an explicit provider, clock and recording plan.");
    if ((image.current.recording && plan.recording != Recording::NewSegment) ||
        (!image.current.recording && plan.recording != Recording::Disabled))
        return fail("Restore recording policy does not match the live recorder.");
    if (plan.clock == Clock::RefuseRewind && image.meta.vsync_frame < image.current.frame)
        return fail("The selected clock policy refuses a backward restore.");
    std::set<uint32_t> present, ignored(plan.legacy_unowned.begin(), plan.legacy_unowned.end());
    if (ignored.size() != plan.legacy_unowned.size() || ignored.count(0))
        return fail("Invalid legacy section-owner plan.");
    for (uint32_t tag : ignored)
        if (tag != savestate::TAG_FILE && tag != savestate::TAG_CNTR && tag != savestate::TAG_MISC &&
            tag != savestate::TAG_STRM && tag != savestate::TAG_SIFS)
            return fail("New service state cannot use the frozen legacy-owner exception.");
    std::vector<Blob> owned;
    for (const auto& blob : image.sections) {
        if (!present.insert(blob.tag).second) return fail("Duplicate section: " + savestate::TagName(blob.tag));
        if (sections_.Has(blob.tag)) owned.push_back(blob);
        else if (!ignored.count(blob.tag))
            return fail("Section has no approved active owner: " + savestate::TagName(blob.tag));
    }
    std::set<uint32_t> required(plan.required_sections.begin(), plan.required_sections.end());
    if (required.size() != plan.required_sections.size() || required.count(0))
        return fail("Invalid selected-provider section plan.");
    required.insert(savestate::TAG_THRD);
    for (uint32_t tag : required)
        if (!sections_.Has(tag) || !present.count(tag))
            return fail("Selected provider state is missing: " + savestate::TagName(tag));
    std::set<uint32_t> missing_rules;
    for (const auto& [tag, policy] : plan.optional_missing) {
        (void)policy;
        if (!sections_.Has(tag) || !missing_rules.insert(tag).second || required.count(tag))
            return fail("Invalid missing-section rule: " + savestate::TagName(tag));
    }
    for (const auto& spec : sections_.Specs()) {
        image.owners.emplace_back(spec.tag, spec.version, spec.required, spec.owner ? spec.owner : "",
                                  bool(spec.validate), bool(spec.restore), bool(spec.absent));
        if (present.count(spec.tag)) {
            if (!spec.validate || !spec.restore)
                return fail("Section lacks a validating restore owner: " + savestate::TagName(spec.tag));
        } else {
            const auto rule = std::find_if(plan.optional_missing.begin(), plan.optional_missing.end(),
                [&](const auto& item) { return item.first == spec.tag; });
            if (spec.required || rule == plan.optional_missing.end())
                return fail("Unapproved missing owner state: " + savestate::TagName(spec.tag));
            if ((rule->second == Missing::Reconstruct) != bool(spec.absent))
                return fail("Missing-section policy disagrees with its owner: " + savestate::TagName(spec.tag));
        }
    }
    if (!sections_.Validate(owned, &error)) return false;
    if (!sources_.validate_image(image.meta, image.sections, error)) return false;
    for (const auto& blob : owned)
        if (blob.tag == savestate::TAG_THRD && !ThreadSection::Parse(blob.payload, image.records, &error)) return false;
    return ValidateThreads(image.records, error);
}

bool SessionRestart::ValidateThreads(const SessionCheckpoint::Records& records, std::string& error) const {
    auto fail = [&](std::string why) { error = std::move(why); return false; };
    if (!SessionCheckpoint::ValidateContinuations(records, sources_.can_resume, error)) return false;
    std::set<std::pair<int, int>> waits;
    for (const auto& record : records) {
        if (!record.started) continue;
        if (record.chain.back().ra) return fail("Continuation is missing its root frame.");
        if (record.is_boundary) continue;
        if (record.status != THS_WAIT || record.suspend_count || record.wakeup_count ||
            (record.wait_type != TSW_SLEEP && record.wait_type != TSW_SEMA && record.wait_type != TSW_EVENT) ||
            !sources_.can_restart_wait(record.chain.front().func_addr, record.wait_type))
            return fail("Thread " + std::to_string(record.tid) + ": wait restart is not proven.");
        if (record.wait_type != TSW_SLEEP && !waits.emplace(record.wait_type, record.wait_id).second)
            return fail("Multiple object waiters require a preserved replay order.");
    }
    return true;
}

bool SessionRestart::Pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bool(prepared_);
}

bool SessionRestart::DiscardPrepared(uint64_t request) {
    if (!sources_.on_game_owner || !sources_.on_game_owner()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (restarting_ || !prepared_ || prepared_->request != request) return false;
    prepared_.reset();
    if (hooks_.discard_prepared) hooks_.discard_prepared();
    return true;
}

SessionRestart::Result SessionRestart::RestartFailure(Result result, std::string detail) {
    result.phase = Phase::Failed;
    result.detail = std::move(detail);
    result.requires_close = true;
    threads_.TerminateAll();
    try { hooks_.cancel_workers(); } catch (...) { result.detail += "; worker cancellation failed"; }
    try { if (hooks_.discard_prepared) hooks_.discard_prepared(); }
    catch (...) { result.detail += "; prepared resource release failed"; }
    std::lock_guard<std::mutex> lock(mutex_);
    prepared_.reset();
    restarting_ = false;
    return result;
}

SessionRestart::Result SessionRestart::RestartPrepared() {
    Result result;
    if (threads_.CurrentIncarnation() || !sources_.on_supervisor || !sources_.on_supervisor() ||
        (sources_.on_game_owner && sources_.on_game_owner())) {
        result.detail = "Restart must run on the supervisor after game-owner unwind.";
        return result;
    }
    if (threads_.Running()) {
        result.detail = "Stop and unwind old owners before requesting supervisor restart.";
        return result;
    }
    std::shared_ptr<const Image> image;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (restarting_ || preparing_ || !prepared_) {
            result.detail = "No prepared reproduction is available for restart.";
            return result;
        }
        restarting_ = true;
        image = prepared_;
    }
    result.request = image->request;
    result.frame = image->meta.vsync_frame;
    try {
        std::string error;
        if (sources_.closing()) return RestartFailure(result, "Session closed before restart.");
        if (!SameCurrent(*image))
            return RestartFailure(result, "Restore providers or completed game key changed after preflight.");
        if (!ValidateThreads(image->records, error)) return RestartFailure(result, error);
        if (!hooks_.join_workers_and_writers(error))
            return RestartFailure(result, error.empty() ? "Old owners could not be joined." : error);
        if (sources_.closing()) return RestartFailure(result, "Session closed while owners joined.");
        if (!SameCurrent(*image))
            return RestartFailure(result, "Restore providers or completed game key changed while owners joined.");
        if (!ValidateThreads(image->records, error)) return RestartFailure(result, error);
        if (!threads_.RestartAfterJoin(&error)) return RestartFailure(result, error);
        // Old records are released only after the actual engine join contract.
        active_ = image;
        if (!hooks_.begin_epoch(image->epoch, error))
            return RestartFailure(result, error.empty() ? "Restored clock/recording epoch failed." : error);
        if (sources_.closing()) return RestartFailure(result, "Session closed during epoch change.");
        if (!hooks_.apply_image(image->bytes, error))
            return RestartFailure(result, error.empty() ? "Restored state could not be applied." : error);
        if (sources_.closing()) return RestartFailure(result, "Session closed while state was applied.");
        if (!hooks_.reset_after_restore(error))
            return RestartFailure(result, error.empty() ? "Restored owners could not be prepared." : error);
        if (sources_.closing()) return RestartFailure(result, "Session closed before relaunch.");
        if (!SameCurrent(*image, true))
            return RestartFailure(result, "Reconstructed providers or clock do not match the approved plan.");
        if (!ValidateThreads(image->records, error)) return RestartFailure(result, error);
        const int expected = static_cast<int>(std::count_if(image->records.begin(), image->records.end(),
            [](const auto& record) { return record.started && record.has_ctx; }));
        const int count = relauncher_.Relaunch(image->records, &error);
        if (count != expected) return RestartFailure(result, error.empty() ? "Incomplete continuation relaunch." : error);
        if (sources_.closing()) return RestartFailure(result, "Session closed during relaunch.");
        result.phase = Phase::Succeeded;
        result.detail = "Reproduction restored; approved continuations relaunched.";
        std::lock_guard<std::mutex> lock(mutex_);
        prepared_.reset();
        restarting_ = false;
        return result;
    } catch (const std::exception& error) {
        return RestartFailure(result, error.what());
    } catch (...) {
        return RestartFailure(result, "Restart provider failed after owner unwind.");
    }
}

}  // namespace ps2r::app
