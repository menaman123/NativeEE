#include "app/host/session_restart.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <unistd.h>

#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/sched/ee_run_token.h"
#include "engine/platform/host/platform.h"

// No generated body or live-stack capture in this file-reader/relaunch test.
extern "C" uint32_t ee_get_call_stack_depth() { return 0; }
extern "C" bool ee_get_call_stack_top(uint32_t, uint32_t*, uint32_t*) { return false; }
extern "C" bool ee_call_stack_push(uint32_t, uint32_t) { return false; }
extern "C" void ee_call_stack_pop() {}

namespace {
using Restart = ps2r::app::SessionRestart;
using ps2r::app::SessionCheckpoint;
using Phase = ps2r::app::SessionControls::Phase;
using namespace ps2r::savestate;
constexpr uint32_t kData = Tag('D', 'A', 'T', 'A');
constexpr uint32_t kService = Tag('S', 'V', 'C', 'E');
int checks = 0, failures = 0;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; \
    std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #value); } } while (false)

SessionCheckpoint::Records Records() {
    ThreadSection::Record record;
    record.tid = 1;
    record.entry = 0x2000;
    record.priority = record.current_priority = 64;
    record.status = THS_RUN;
    record.started = record.has_ctx = record.is_boundary = true;
    record.ctx.cpuRegs.GPR.r[31].UL[0] = 0x1004;
    record.ctx.cpuRegs.GPR.r[2].UL[0] = 0xfeed;
    record.chain = {{0x1000, 0x2004}, {0x2000, 0}};
    return {record};
}

std::vector<Blob> Sections(bool service = true, uint8_t value = 42) {
    Blob threads;
    threads.tag = TAG_THRD;
    ThreadSection::Emit(Records(), threads.payload);
    std::vector<Blob> blobs{threads, {kData, 1, kSectionRequired, {value}}};
    if (service) blobs.push_back({kService, 1, 0, {7}});
    return blobs;
}

std::vector<uint8_t> Image(std::vector<Blob> blobs = Sections()) {
    FileMeta meta;
    meta.vsync_frame = 100;
    return SerializeStateFile(meta, blobs);
}

std::vector<uint8_t> ThreadImage(const SessionCheckpoint::Records& records) {
    auto sections = Sections();
    sections[0].payload.clear();
    ThreadSection::Emit(records, sections[0].payload);
    return Image(std::move(sections));
}

std::vector<uint8_t> WaitImage() {
    auto records = Records();
    auto peer = records.front();
    peer.tid = 2;
    peer.is_boundary = false;
    peer.status = THS_WAIT;
    peer.wait_type = TSW_SLEEP;
    peer.chain = {{0x3000, 0}};
    records.push_back(peer);
    return ThreadImage(records);
}

std::string Read(const std::string& path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void Adopt(CpuContext& context) {
    auto& threads = ThreadManager::Instance();
    threads.TerminateAll();
    threads.JoinAll();
    threads.ResetForTest();
    EeRunToken::Instance().ResetForTest();
    auto info = threads.AdoptCurrentThread(1, 64);
    info->ctx = &context;
    EeRunToken::Instance().GuestEnter(1, 64);
}

void UnwindOwner() {
    auto& threads = ThreadManager::Instance();
    threads.TerminateAll();
    std::string error;
    CHECK(threads.ReleaseCurrentThread(&error));
}

bool Await(const std::function<bool()>& ready) {
    const uint64_t deadline = ps2r::Park::NowNs() + 2000000000ull;
    while (!ready()) {
        if (ps2r::Park::NowNs() >= deadline) return false;
        std::this_thread::yield();
    }
    return true;
}

void RestorePins() {
    auto& threads = ThreadManager::Instance();
    auto& token = EeRunToken::Instance();
    CpuContext live{};
    Adopt(live);
    threads.SetStackBytes(2u << 20);
    const auto supervisor = std::this_thread::get_id();
    SectionRegistry registry;
    ThreadSection thread_section(threads);
    thread_section.Register(registry);
    int game_value = 81, service_value = 9, absent_calls = 0, resets = 0, joins = 0, cancels = 0;
    uint64_t live_frame = 400;
    bool recording = false, movie_closed = true, epoch_supported = true, exact_entry = true, restart_wait = false;
    bool epoch_fails = false, reset_fails = false, join_fails = false, mutate_on_join = false, revoke_on_join = false;
    bool closing = false, close_on_join = false, close_on_epoch = false, close_on_reset = false;
    bool revoke_on_reset = false;
    int epoch_checks = 0, quiescent_checks = 0;
    std::vector<std::string> order;
    SectionSpec data;
    data.tag = kData;
    data.owner = "Game fixture";
    data.validate = [](const Blob& blob, std::string* error) {
        if (blob.version == 1 && blob.payload.size() == 1 && blob.payload[0] <= 100) return true;
        if (error) *error = "Invalid fixture data.";
        return false;
    };
    data.restore = [&](const Blob& blob, std::string*) { game_value = blob.payload[0]; return true; };
    CHECK(registry.Register(data));
    SectionSpec service = data;
    service.tag = kService;
    service.owner = "Selected service fixture";
    service.required = false;
    service.restore = [&](const Blob& blob, std::string*) { service_value = blob.payload[0]; return true; };
    service.absent = [&] { ++absent_calls; service_value = 0; };
    CHECK(registry.Register(service));
    Restart::Plan plan;
    plan.providers = "game-v1+selected-service-v1";
    plan.required_sections = {kData, kService};
    plan.clock = Restart::Clock::Rebase;
    plan.recording = Restart::Recording::Disabled;
    const std::string directory_s = ps2r::host::MakeTempDirectory("session_restart_", nullptr);
    CHECK(!directory_s.empty());
    const char* directory = directory_s.c_str();
    const std::string progress = std::string(directory) + "/game-save";
    const std::string old_recording = std::string(directory) + "/input-old";
    const std::string new_recording = std::string(directory) + "/input-new";
    { std::ofstream file(progress); file << "persistent progress"; }
    { std::ofstream file(old_recording); file << "frame=400"; }
    SavestateWriter writer;
    Restart::Sources sources;
    sources.on_game_owner = [&] { return token.CanCaptureCurrentThread(); };
    sources.on_supervisor = [&] { return std::this_thread::get_id() == supervisor; };
    sources.closing = [&] { return closing; };
    sources.current = [&] { return Restart::Current{plan, live_frame, recording}; };
    sources.can_resume = [&](uint32_t function, uint32_t entry) {
        return exact_entry && ((function == 0x1000 && entry == 0x1004) ||
            (function == 0x2000 && entry == 0x2004) || (function == 0x3000 && entry == 0));
    };
    sources.can_restart_wait = [&](uint32_t function, int wait) {
        return restart_wait && function == 0x3000 && wait == TSW_SLEEP;
    };
    sources.validate_epoch = [&](const Restart::Epoch&, std::string& error) {
        ++epoch_checks;
        if (!epoch_supported) error = "Recorder segment creation is unavailable.";
        return epoch_supported;
    };
    sources.current_quiescent = [&](std::string& error) {
        ++quiescent_checks;
        if (!movie_closed) error = "Movie still owns buffers.";
        return movie_closed;
    };
    bool image_supported = true;
    bool stage_supported = true;
    unsigned stage_calls = 0;
    std::vector<uint8_t> staged_image;
    sources.validate_image = [&](const auto&, const auto&, std::string& error) {
        if (!image_supported) error = "Image metadata does not match its saved clock.";
        return image_supported;
    };
    std::atomic<bool> reset_ready{false}, first_entry{false}, allow_tail{false}, worker_ok{true};
    std::atomic<int> tails{0};
    ps2r::Park wake;
    Restart::Hooks hooks;
    hooks.join_workers_and_writers = [&](std::string&) {
        ++joins;
        order.push_back("join");
        threads.JoinAll();
        writer.JoinForExit();
        if (mutate_on_join) plan.providers += "-changed";
        if (revoke_on_join) exact_entry = false;
        if (close_on_join) closing = true;
        return !join_fails;
    };
    hooks.begin_epoch = [&](const Restart::Epoch& epoch, std::string& error) {
        order.push_back("epoch");
        if (close_on_epoch) closing = true;
        if (epoch_fails) { error = "Recorder segment failed."; return false; }
        live_frame = epoch.restored_frame;
        if (epoch.recording == Restart::Recording::NewSegment) {
            std::ofstream file(new_recording);
            file << "frame=" << epoch.restored_frame;
        }
        return true;
    };
    hooks.apply_image = [&](const std::vector<uint8_t>& bytes, std::string& error) {
        CHECK(staged_image == bytes);
        staged_image.clear();
        order.push_back("apply");
        FileMeta meta;
        std::vector<Blob> blobs, owned;
        if (!ParseStateFile(bytes.data(), bytes.size(), &meta, &blobs, &error)) return false;
        for (auto& blob : blobs) if (registry.Has(blob.tag)) owned.push_back(std::move(blob));
        return registry.Restore(owned, &error);
    };
    hooks.reset_after_restore = [&](std::string& error) {
        ++resets;
        order.push_back("reset");
        if (close_on_reset) closing = true;
        if (revoke_on_reset) exact_entry = false;
        if (reset_fails) { error = "Owner reset failed."; return false; }
        reset_ready.store(true);
        return true;
    };
    hooks.cancel_workers = [&] { ++cancels; threads.TerminateAll(); wake.Notify(); };
    hooks.prepare_image = [&](const std::vector<uint8_t>& bytes, std::string& error) {
        ++stage_calls;
        CHECK(joins == 0 || !threads.CurrentIncarnation() || token.CanCaptureCurrentThread());
        if (!stage_supported) { error = "Selected resources could not be staged."; return false; }
        staged_image = bytes;
        return true;
    };
    hooks.discard_prepared = [&] { staged_image.clear(); };
    Restart restart(threads, registry, sources, hooks, [&](uint32_t function, uint32_t entry, CpuContext& ctx) {
        if (!reset_ready.load() || live_frame != 100 || ctx.cpuRegs.GPR.r[2].UL[0] != 0xfeed)
            worker_ok.store(false);
        if (function == 0x1000) {
            first_entry.store(true);
            while (!allow_tail.load()) {
                const auto generation = wake.Generation();
                if (!allow_tail.load()) wake.Wait(generation);
            }
        }
        if (!sources.can_resume(function, entry)) worker_ok.store(false);
        ++game_value;
        ++tails;
        return true;
    });
    uint64_t request = 0;
    auto refused = [&](std::vector<uint8_t> bytes) {
        const auto result = restart.PrepareImage(++request, std::move(bytes));
        CHECK(result.phase == Phase::Refused && !result.requires_close && !restart.Pending());
        CHECK(game_value == 81 && service_value == 9 && resets == 0 && joins == 0 && absent_calls == 0);
        CHECK(token.CanCaptureCurrentThread() && live_frame == 400);
    };
    closing = true;
    refused(Image());
    closing = false;
    auto damaged = Image();
    damaged.back() ^= 1;
    refused(std::move(damaged));                 // Envelope corruption.
    refused(Image(Sections(true, 255)));        // CRC-valid invalid owned payload.
    refused(Image(Sections(false)));            // Missing selected service cannot reset it.
    CHECK(epoch_checks == 0 && quiescent_checks == 0);
    image_supported = false;
    refused(Image());                          // Pure root metadata admission precedes stop.
    image_supported = true;
    CHECK(epoch_checks == 0 && quiescent_checks == 0);
    plan.optional_missing = {{kService, Restart::Missing::Reconstruct}};
    refused(Image(Sections(false)));            // Cannot waive selected required state.
    plan.optional_missing.clear();
    exact_entry = false;
    refused(Image());                          // Address containment is insufficient.
    exact_entry = true;
    movie_closed = false;
    refused(Image());                          // EOF does not relinquish active buffers.
    movie_closed = true;
    plan.clock = Restart::Clock::RefuseRewind;
    refused(Image());
    plan.clock = Restart::Clock::Rebase;
    recording = true;
    refused(Image());                          // No silent append to old recording.
    plan.recording = Restart::Recording::NewSegment;
    epoch_supported = false;
    refused(Image());                          // Refuse before stopping or creating files.
    epoch_supported = true;
    CHECK(stage_calls == 0 && staged_image.empty());
    stage_supported = false;
    refused(Image());
    CHECK(stage_calls == 1 && staged_image.empty());
    stage_supported = true;
    recording = false;
    plan.recording = Restart::Recording::Disabled;
    auto foreign = Sections();
    foreign.push_back({Tag('N', 'E', 'W', 'S'), 1, kSectionRequired, {1}});
    refused(Image(foreign));
    plan.legacy_unowned = {foreign.back().tag};
    refused(Image(foreign));                   // No new service under legacy exceptions.
    plan.legacy_unowned.clear();
    const auto waiting = WaitImage();
    refused(waiting);
    restart_wait = true;
    CHECK(restart.PrepareImage(++request, waiting).phase == Phase::Working);
    CHECK(restart.DiscardPrepared(request));
    auto missing_root = Records();
    missing_root.front().chain.back().ra = 0xdeadbeec;
    refused(ThreadImage(missing_root));
    auto legacy = Sections();
    legacy.push_back({TAG_FILE, 1, kSectionRequired, {1}});
    refused(Image(legacy));
    plan.legacy_unowned = {TAG_FILE};
    CHECK(restart.PrepareImage(++request, Image(legacy)).phase == Phase::Working);
    CHECK(restart.DiscardPrepared(request));
    plan.legacy_unowned.clear();
    auto duplicate = Sections();
    duplicate.push_back(duplicate.back());
    refused(Image(duplicate));
    bool foreign_rejected = false;
    std::thread outsider([&] { foreign_rejected = restart.PrepareImage(++request, Image()).phase == Phase::Refused; });
    outsider.join();
    CHECK(foreign_rejected);
    CHECK(Read(progress) == "persistent progress" && Read(old_recording) == "frame=400");
    CHECK(!std::filesystem::exists(new_recording));

    // The file can disappear/change after preflight. Apply consumes retained
    // bytes, and worker records remain owned after RestartPrepared returns.
    recording = true;
    plan.recording = Restart::Recording::NewSegment;
    const std::string file_path = std::string(directory) + "/reproduction.ps2st";
    FileMeta file_meta;
    file_meta.vsync_frame = 100;
    std::string error;
    CHECK(WriteStateFile(file_path, file_meta, Sections(), &error));
    const uint64_t accepted = ++request;
    CHECK(restart.PrepareFile(accepted, file_path).phase == Phase::Working);
    CHECK(restart.Pending() && restart.PrepareImage(++request, Image()).phase == Phase::Refused);
    CHECK(restart.RestartPrepared().phase == Phase::Refused && joins == 0);
    std::filesystem::remove(file_path);
    WriteJob write;
    write.path = std::string(directory) + "/preceding-write.ps2st";
    write.meta = file_meta;
    write.sections = Sections();
    CHECK(writer.TryEnqueue(std::move(write)));
    threads.TerminateAll();
    CHECK(restart.RestartPrepared().phase == Phase::Refused && joins == 0);
    UnwindOwner();
    const auto restored = restart.RestartPrepared();
    CHECK(restored.request == accepted && restored.phase == Phase::Succeeded && !restored.requires_close);
    CHECK(Await([&] { return first_entry.load(); }));
    CHECK((order == std::vector<std::string>{"join", "epoch", "apply", "reset"}));
    CHECK(writer.CompletedCount() == 1 && writer.LastResult().ok);
    CHECK(service_value == 7 && game_value == 42 && live_frame == 100);
    CHECK(Read(progress) == "persistent progress" && Read(old_recording) == "frame=400");
    CHECK(Read(new_recording) == "frame=100");
    CHECK(!restart.Pending());
    allow_tail.store(true);
    wake.Notify();
    threads.JoinAll();
    CHECK(worker_ok.load() && tails.load() == 2 && game_value == 44);

    // A default legacy plan may explicitly reconstruct an optional owner's
    // baseline. It cannot use this rule while that service is selected.
    Adopt(live);
    plan.providers = "legacy-fixture-v1";
    plan.required_sections = {kData};
    plan.optional_missing = {{kService, Restart::Missing::Reconstruct}};
    recording = false;
    plan.recording = Restart::Recording::Disabled;
    CHECK(restart.PrepareImage(++request, Image(Sections(false))).phase == Phase::Working);
    CHECK(absent_calls == 0);
    UnwindOwner();
    CHECK(restart.RestartPrepared().phase == Phase::Succeeded);
    threads.JoinAll();
    CHECK(absent_calls == 1 && service_value == 0 && game_value == 44);
    CHECK(Read(progress) == "persistent progress");
    plan.providers = "game-v1+selected-service-v1";
    plan.required_sections = {kData, kService};
    plan.optional_missing.clear();

    // Once old owners were unwound, a changed selection or a failed stage
    // requires close; it cannot silently resume a partially reconstructed game.
    for (int failure = 0; failure < 12; ++failure) {
        Adopt(live);
        live_frame = 400;
        order.clear();
        first_entry.store(false);
        reset_ready.store(false);
        const int old_tails = tails.load(), old_cancels = cancels;
        CHECK(restart.PrepareImage(++request, Image()).phase == Phase::Working);
        UnwindOwner();
        if (failure == 0) plan.providers += "-changed";
        if (failure == 1) exact_entry = false;
        if (failure == 2) ++live_frame;
        if (failure == 3) mutate_on_join = true;
        if (failure == 4) revoke_on_join = true;
        if (failure == 5) join_fails = true;
        if (failure == 6) epoch_fails = true;
        if (failure == 7) reset_fails = true;
        if (failure == 8) close_on_join = true;
        if (failure == 9) close_on_epoch = true;
        if (failure == 10) close_on_reset = true;
        if (failure == 11) revoke_on_reset = true;
        const auto failed = restart.RestartPrepared();
        CHECK(failed.phase == Phase::Failed && failed.requires_close && !restart.Pending());
        CHECK(cancels == old_cancels + 1 && tails.load() == old_tails && !first_entry.load());
        CHECK(!threads.Running() && Read(progress) == "persistent progress");
        if (failure < 3) CHECK(order.empty());
        if (failure >= 3 && failure <= 5) CHECK((order == std::vector<std::string>{"join"}));
        if (failure == 6) CHECK((order == std::vector<std::string>{"join", "epoch"}));
        if (failure == 7) CHECK((order == std::vector<std::string>{"join", "epoch", "apply", "reset"}));
        if (failure == 8) CHECK((order == std::vector<std::string>{"join"}));
        if (failure == 9) CHECK((order == std::vector<std::string>{"join", "epoch"}));
        plan.providers = "game-v1+selected-service-v1";
        exact_entry = true;
        epoch_fails = reset_fails = join_fails = mutate_on_join = revoke_on_join = false;
        closing = close_on_join = close_on_epoch = close_on_reset = false;
        revoke_on_reset = false;
        threads.JoinAll();
    }
    threads.TerminateAll();
    threads.JoinAll();
    CHECK(!threads.RestartAfterJoin(&error)); // Failed reset left an unapplied worker table: close is mandatory.
    threads.ResetForTest();
    token.ResetForTest();
    std::filesystem::remove_all(directory);
}
} // namespace

int main() {
    RestorePins();
    std::printf("%s SessionRestart: %d checks\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
