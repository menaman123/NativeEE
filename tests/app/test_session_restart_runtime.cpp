#include "app/host/session_restart.h"
#include "app/host/session_runtime.h"
#include "app/host/session_workers.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/sched/ee_run_token.h"

#include <SDL.h>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <unistd.h>
#include "engine/platform/host/platform.h"

// This product contract fixture executes no title body or live-stack capture.
extern "C" uint32_t ee_get_call_stack_depth() { return 0; }
extern "C" bool ee_get_call_stack_top(uint32_t, uint32_t*, uint32_t*) { return false; }
extern "C" bool ee_call_stack_push(uint32_t, uint32_t) { return false; }
extern "C" void ee_call_stack_pop() {}

namespace {
using Runtime = ps2r::app::SessionRuntime;
using Restart = ps2r::app::SessionRestart;
using Controls = ps2r::app::SessionControls;
using Phase = Controls::Phase;
using Action = Controls::Action;
using namespace ps2r::savestate;
int failures = 0, checks = 0;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; \
    std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #value); } } while (false)

bool Await(const std::function<bool()>& ready) {
    const auto deadline = ps2r::Park::NowNs() + 5000000000ull;
    while (!ready()) {
        if (ps2r::Park::NowNs() >= deadline) return false;
        std::this_thread::yield();
    }
    return true;
}
bool Done(const Controls& controls, uint64_t id, Phase phase) {
    for (const auto& result : controls.Read().recent)
        if (result.request.id == id) return result.phase == phase;
    return false;
}
bool Open(Runtime& runtime) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return false;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    std::string error;
    const bool ok = runtime.Open(100, false, false, {}, error);
    if (!ok) std::fprintf(stderr, "%s\n", error.c_str());
    return ok;
}

int Deadline() {
    Runtime runtime(nullptr);
    if (!Open(runtime)) return 3;
    Runtime::Hooks hooks;
    hooks.restore = [](const auto&, uint64_t) -> Runtime::CaptureResult {
        ps2r::Park stuck;
        stuck.Wait(stuck.Generation());
        return {Phase::Failed, "unexpected wake"};
    };
    hooks.unwind_restore = [] {};
    hooks.restart = [] { return Runtime::RestoreCompletion{}; };
    hooks.alive = [] { return true; };
    runtime.SetHooks(std::move(hooks));
    runtime.Controls().Submit(Action::RestoreReproduction, 0, "fixture");
    return runtime.Run([&] {
        uint64_t frame = 400;
        while (!runtime.Closing() && runtime.Boundary(frame++)) {}
        return 0;
    }, 1);
}

void ComposedRestart() {
    auto& threads = ThreadManager::Instance();
    threads.ResetForTest();
    EeRunToken::Instance().ResetForTest();
    threads.SetStackBytes(2u << 20);
    Runtime runtime(nullptr);
    CHECK(Open(runtime));
    const Uint32 window_id = SDL_GetWindowID(SDL_GL_GetCurrentWindow());
    auto& controls = runtime.Controls();
    const auto main = std::this_thread::get_id();
    std::thread::id supervisor;
    std::atomic<uint64_t> frame{400};
    std::atomic<int> data{81}, resets{0}, joins{0}, entered{0};
    std::atomic<bool> running{true};
    std::atomic<int> clock_stops{0}, clock_wakes{0};
    ps2r::app::SessionWorkers workers(threads, running,
        [&] { ++clock_stops; }, [&] { ++clock_wakes; });
    SectionRegistry sections;
    ThreadSection thread_section(threads);
    thread_section.Register(sections);
    SectionSpec spec;
    spec.tag = Tag('D','A','T','A');
    spec.owner = "Fixture data";
    spec.validate = [](const Blob& blob, std::string*) { return blob.version == 1 && blob.payload.size() == 1; };
    spec.restore = [&](const Blob& blob, std::string*) { data.store(blob.payload[0]); return true; };
    CHECK(sections.Register(spec));
    ThreadSection::Record record;
    record.tid = 1; record.entry = 0x1000;
    record.priority = record.current_priority = 64;
    record.status = THS_RUN;
    record.started = record.has_ctx = record.is_boundary = true;
    record.ctx.cpuRegs.GPR.r[31].UL[0] = 0x1004;
    record.chain = {{0x1000, 0}};
    Blob thread_blob{TAG_THRD, 1, kSectionRequired, {}};
    ThreadSection::Emit({record}, thread_blob.payload);
    const std::string directory_s = ps2r::host::MakeTempDirectory("session_restart_runtime_", nullptr);
    CHECK(!directory_s.empty());
    const char* directory = directory_s.c_str();
    const std::string valid = std::string(directory) + "/valid.ps2st";
    const std::string missing = std::string(directory) + "/missing-owner.ps2st";
    FileMeta meta; meta.vsync_frame = 100;
    std::string error;
    CHECK(WriteStateFile(valid, meta, {thread_blob, {spec.tag, 1, kSectionRequired, {42}}}, &error));
    CHECK(WriteStateFile(missing, meta, {thread_blob}, &error));
    Restart::Plan plan;
    plan.providers = "fixture-v1"; plan.required_sections = {spec.tag};
    plan.clock = Restart::Clock::Rebase; plan.recording = Restart::Recording::Disabled;
    Restart::Sources sources;
    sources.on_game_owner = [] { return EeRunToken::Instance().CanCaptureCurrentThread(); };
    sources.on_supervisor = [&] { return std::this_thread::get_id() == supervisor; };
    sources.closing = [&] { return runtime.Closing(); };
    sources.current = [&] { return Restart::Current{plan, frame.load(), false}; };
    sources.can_resume = [](uint32_t function, uint32_t entry) { return function == 0x1000 && entry == 0x1004; };
    sources.can_restart_wait = [](uint32_t, int) { return false; };
    sources.validate_epoch = [](const auto&, std::string&) { return true; };
    sources.validate_image = [](const auto& meta, const auto&, std::string& error) {
        if (meta.vsync_frame == 100) return true;
        error = "Fixture image has an unexpected frame.";
        return false;
    };
    sources.current_quiescent = [](std::string&) { return true; };
    Restart::Hooks bindings;
    bindings.join_workers_and_writers = [&](std::string&) { ++joins; workers.Join(); return true; };
    bindings.begin_epoch = [&](const Restart::Epoch& epoch, std::string&) {
        frame.store(epoch.restored_frame); runtime.RebaseFrame(epoch.restored_frame); return true;
    };
    bindings.apply_image = [&](const std::vector<uint8_t>& image, std::string& why) {
        std::vector<Blob> blobs;
        return ParseStateFile(image.data(), image.size(), nullptr, &blobs, &why) && sections.Restore(blobs, &why);
    };
    bindings.reset_after_restore = [&](std::string&) { ++resets; running.store(true); return true; };
    bindings.cancel_workers = [&] { workers.Cancel(); };
    Restart restart(threads, sections, std::move(sources), std::move(bindings),
        [&](uint32_t, uint32_t, CpuContext&) {
            ++entered;
            while (!runtime.Closing()) {
                if (!runtime.Boundary(frame.load())) break;
                ++frame;
            }
            return true;
        });
    Runtime::Hooks hooks;
    hooks.pause = [](bool) {};
    hooks.restore = [&](const auto& request, uint64_t) {
        const auto result = restart.PrepareFile(request.id, request.path);
        return Runtime::CaptureResult{result.phase, result.detail};
    };
    hooks.unwind_restore = [&] { workers.Cancel(); };
    hooks.restart = [&] {
        const auto result = restart.RestartPrepared();
        return Runtime::RestoreCompletion{result.request, {result.phase, result.detail},
            result.frame, {}, result.requires_close};
    };
    hooks.alive = [&] { return workers.BoundaryAlive(); };
    hooks.stop = [&] { workers.Cancel(); };
    hooks.stopped = [&] { workers.Join(); };
    runtime.SetHooks(std::move(hooks));
    const auto pause = controls.Submit(Action::TogglePause);
    bool sequence = true;
    std::thread director([&] {
        sequence = Await([&] { return Done(controls, pause, Phase::Succeeded) && controls.Read().runtime.paused; });
        const uint64_t paused_frame = frame.load();
        const auto refused = controls.Submit(Action::RestoreReproduction, 0, missing);
        sequence &= Await([&] { return Done(controls, refused, Phase::Refused); });
        sequence &= frame == paused_frame && data == 81 && joins == 0 && resets == 0;
        controls.SelectRestorePath(valid);
        SDL_Event event{}; event.type = SDL_KEYDOWN;
        event.key.windowID = window_id;
        event.key.keysym.sym = SDLK_F8;
        event.key.state = SDL_PRESSED;
        sequence &= SDL_PushEvent(&event) == 1;
        sequence &= Await([&] {
            for (const auto& result : controls.Read().recent)
                if (result.request.action == Action::RestoreReproduction && result.phase == Phase::Succeeded)
                    return entered == 1 && data == 42 && controls.Read().runtime.frame == 100;
            return false;
        });
        sequence &= joins == 1 && resets == 1 && !runtime.Closing();
        const auto step = controls.Submit(Action::Step);
        sequence &= Await([&] {
            const auto view = controls.Read();
            return Done(controls, step, Phase::Succeeded) && view.runtime.frame == 101 && view.runtime.paused;
        });
        controls.Submit(Action::Close);
        if (!sequence) runtime.RequestClose();
    });
    const int result = runtime.Run([&] {
        supervisor = std::this_thread::get_id();
        CpuContext context{};
        auto info = threads.AdoptCurrentThread(1, 64); info->ctx = &context;
        EeRunToken::Instance().GuestEnter(1, 64);
        while (!runtime.Closing()) {
            if (!runtime.Boundary(frame.load())) break;
            ++frame;
        }
        std::string reason;
        if (!threads.ReleaseCurrentThread(&reason)) return 2;
        return 0;
    }, 3);
    director.join();
    CHECK(result == 0 && sequence);
    CHECK(supervisor != main && entered == 1 && resets == 1 && joins == 1);
    CHECK(controls.Read().pending.empty());
    CHECK(!threads.Running());
    CHECK(!running && clock_stops >= 2 && clock_wakes >= 3);
    std::filesystem::remove_all(directory);
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--restore-timeout") return Deadline();
    ComposedRestart();
    SDL_Quit();
    std::printf("%s SessionRestartRuntime: %d checks\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
