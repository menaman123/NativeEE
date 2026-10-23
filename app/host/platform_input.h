#pragma once
// PlatformInput: the play-mode pad source -- the composition root's adapter
// between the SDL host half (InputSystem::Frame -> PadSample) and the pad
// protocol's per-boundary sample (PadProtocol::Input). Constructed ONLY in
// play mode (a window exists, time.tick_sync is off); headless and
// deterministic legs never construct it and keep the InputLog replay or the
// neutral pad, so their streams are unchanged.
//
// The main owner alone touches InputSystem or SDL. A provider change made by
// the game owner waits for main to service an owned request. Event-only
// iterations never consume physical edges. No game callback executes on main.
//
// The boundary's Sample() NEVER blocks the game thread on main (Charter 3 item
// 4, RootScreenModeSwap: main sits inside SDL_SetWindowFullscreen for the
// whole macOS Spaces transition, 150-1000 ms). It posts one owned Sample
// request and waits for it a BOUNDED time (kSampleWaitNs, 3 ms: main answers
// an idle pump in well under 1 ms, so the common case is the fresh read); past
// the bound the LAST accepted sample is served (the buttons held, counted
// `late`) and the request stays posted -- the next boundary takes its result
// when main has answered (counted `stale`: a read one boundary old), or holds
// again while it is still outstanding. The rumble serve (QueueMotors) is a
// coalesced post the owner applies when it pumps; its result is whether a
// live gamepad can take it. Census: Samples / SampleLate / SampleStale /
// SampleWaitMaxUs (run.meta pad_live=).
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "engine/core/threads/park.h"
#include "engine/pad/hle/pad_protocol.h"
#include "engine/platform/sdl/input/input_settings.h"
#include "engine/platform/sdl/input/input_system.h"

namespace ps2r {
class Config;
}

class PlatformInput {
public:
    // File path rule for input.bindings_file and the two companion files
    // (device config `input_device.ini`, `gamecontrollerdb.txt`): an absolute
    // name is used as given; a relative name is looked up next to the
    // executable first, else under paths.game_files (the one directory every
    // install has). Returns the chosen path (the game_files candidate when
    // neither exists, so a later save lands there).
    static std::string ResolveFile(const std::string& name, const std::string& exe_dir, const std::string& game_files);

    // InputSettings from Config::input (device / kbm_pad / bindings_file) with
    // the path rule above; the engine's compiled defaults for the rest.
    static ps2r::platform::sdl::InputSettings SettingsFrom(const ps2r::Config& cfg, const std::string& exe_dir);

    explicit PlatformInput(const ps2r::platform::sdl::InputSettings& settings);
    ~PlatformInput();
    PlatformInput(const PlatformInput&) = delete;
    PlatformInput& operator=(const PlatformInput&) = delete;

    // One pad sample for the boundary (active-high buttons, wire-order bits).
    // Never blocks beyond kSampleWaitNs (see the header comment).
    PadProtocol::Input Sample();
    static constexpr uint64_t kSampleWaitNs = 3000000;   // 3 ms

    // Main-only event handling; root routes reserved GUI keys first.
    bool HandleEvent(const SDL_Event& event);
    // Owner (main) only: re-read the bindings INI (the in-game Controls page's
    // press-to-bind wrote it) into the live mapper -- the next Sample maps keys
    // through it. False = not the owner, or the file was rejected (the previous
    // bindings stand; the mapper printed why). Census: BindingsReloads().
    bool ReloadBindings(const std::string& path);
    uint64_t BindingsReloads() const { return bindings_reloads_.load(); }
    uint64_t BindingsReloadsOk() const { return bindings_reloads_ok_.load(); }
    bool ServicePending();
    void BindMainWake(ps2r::Park* wake);
    // The owner's "inside a blocking call" flag (the presenter's swap / a display
    // swap): a Sample posted while it is set is not waited for at all -- the held
    // sample serves now and the answer is taken at the next boundary.
    void BindOwnerBusy(const std::atomic<bool>* busy);
    void StopService();
    // Coherent with Sample's last accepted report, not an independent poll.
    bool Connected() const;
    bool QueueMotors(unsigned small, unsigned big);
    void ResetPending();
    void SetUiCapture(bool captured);
    void SetKeyboardEnabled(bool enabled);
    // Any thread: the harness's HELD key (harness.inject_key "<KEY>@N-M"): while
    // held, every main-side Sample re-arms the kbm backend's injection overlay
    // for the scancode (SDL's keyboard-state array ignores pushed events; the
    // KEYDOWN/KEYUP pair still travels the real queue). A no-op in play.
    void HoldKey(int scancode, bool held);
    // Any thread: the harness's VIRTUAL GAMEPAD (harness.virtual_pad; pin
    // RootPadHotplug): on the owner, SDL_JoystickAttachVirtual (a game
    // controller SDL itself maps) -> the real JOYDEVICEADDED route into
    // GamepadBackend; SetVirtualButton presses its cross (SDL_CONTROLLER_BUTTON_A)
    // through the real controller events; DetachVirtual -> JOYDEVICEREMOVED.
    enum class VirtualPad { Attach, Press, Release, Detach };
    void VirtualPadAction(VirtualPad action);
    // Any thread: the virtual pad's device index (-1 = none attached).
    int VirtualPadIndex() const { return virtual_index_.load(); }
    // Any thread: InputSystem::Sources() as of the owner's last Sample/pump
    // (run.meta input_sources=: kbm=,gamepad=,active=,switches=,hotplug_*=).
    std::string Sources() const;
    bool KeyboardEnabled() const { return keyboard_enabled_.load(); }
    uint64_t Samples() const { return samples_.load(); }
    uint64_t SampleLate() const { return sample_late_.load(); }     // boundaries served the held sample past the bound
    uint64_t SampleStale() const { return sample_stale_.load(); }   // boundaries that took a previous boundary's answer
    uint64_t SampleWaitMaxUs() const { return sample_wait_max_us_.load(); }
    // run.meta pad_live= body: samples=,late=,stale=,wait_max_ms=,drains=,gamepad=
    std::string Census() const;
    uint64_t EventDrains() const { return drains_.load(); }
    bool GamepadActive() const { return gamepad_active_.load(); }

    // The input -> boundary latency census (round 5). Every input EDGE the owner's pump
    // drains (a key down / up that is not a repeat, a controller / joystick / mouse button,
    // a harness HoldKey) is stamped with its arrival instant on the host (steady_clock ns:
    // the SDL event's own timestamp -- the push / the pump that received it -- carried
    // over to the steady clock; HoldKey: the call); the EARLIEST unconsumed edge is the
    // one the next main-side Sample carries. The game owner, after Sample() served a FRESH
    // sample, takes its stamps once: the sample's instant (the owner's system_.Frame) and
    // the edge it carried (0 = none). A held / stale sample has no fresh stamps (false).
    bool TakeSampleStamps(uint64_t* sampled_ns, uint64_t* edge_ns);
    uint64_t Edges() const { return edges_.load(); }   // input edges stamped (census)

private:
    enum class Operation { Sample, Reset, Capture, Keyboard, VirtualPad };
    struct Work {
        Operation operation = Operation::Sample;
        unsigned first = 0, second = 0;
        PadProtocol::Input sample;
        uint64_t sampled_ns = 0, edge_ns = 0;   // the census stamps (steady_clock ns)
        bool connected = false, ok = false;
        std::atomic<bool> done{false};
        ps2r::Park wake;
    };
    std::shared_ptr<Work> Request(Operation operation, unsigned first = 0, unsigned second = 0);
    void Execute(Work& work);
    void Accept(const Work& work);   // the game owner's bookkeeping of a completed Sample
    bool ApplyMotors(unsigned small, unsigned big);
    ps2r::platform::sdl::InputSystem system_;
    std::thread::id owner_;
    std::chrono::steady_clock::time_point last_;
    mutable std::mutex mutex_;
    std::deque<std::shared_ptr<Work>> pending_;
    ps2r::Park* main_wake_ = nullptr;
    const std::atomic<bool>* owner_busy_ = nullptr;
    bool stopped_ = false;
    bool accepted_connected_ = false;
    std::atomic<bool> gamepad_active_{false}, keyboard_enabled_{true};
    std::atomic<uint64_t> samples_{0}, drains_{0}, bindings_reloads_{0}, bindings_reloads_ok_{0};
    // The game owner's Sample state (that thread's alone: no lock).
    std::shared_ptr<Work> outstanding_;   // a posted Sample main has not answered yet
    PadProtocol::Input last_sample_{};
    bool last_connected_ = false;
    std::atomic<uint64_t> sample_late_{0}, sample_stale_{0}, sample_wait_max_us_{0};
    // The census stamps: the earliest unconsumed edge (any thread sets, the owner's Sample
    // takes), the accepted sample's stamps (the game owner's alone) and whether they are fresh.
    std::atomic<uint64_t> edge_ns_{0}, edges_{0};
    uint64_t last_sampled_ns_ = 0, last_edge_ns_ = 0;
    bool stamps_fresh_ = false;
    void NoteEdge(uint64_t arrival_ns);
    std::vector<int> held_keys_;   // under mutex_; applied on the owner before each Sample
    std::string sources_;          // under mutex_; refreshed on the owner after each Sample
    std::atomic<int> virtual_index_{-1};
    void* virtual_joystick_ = nullptr;   // SDL_Joystick*, owner only
    // The coalesced rumble post: one pending motors value, applied when the owner pumps.
    bool motors_pending_ = false;
    unsigned motors_small_ = 0, motors_big_ = 0;
};
