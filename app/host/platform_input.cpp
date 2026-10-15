// PlatformInput implementation; the contract is in the header.
#include "app/host/platform_input.h"

#include <SDL.h>

#include <algorithm>
#include <cassert>
#include <filesystem>

#include "engine/core/config/config.h"
#include "engine/platform/sdl/input/gamepad_backend.h"
#include "engine/platform/sdl/input/keyboard_mouse_backend.h"
#include "engine/platform/sdl/input/pad_sample.h"

namespace {
constexpr float kMaxDtSeconds = 0.25f;   // a stalled frame decays the mouse stick as a quarter second, never more
constexpr const char* kDeviceConfigName = "input_device.ini";
constexpr const char* kGamepadDbName = "gamecontrollerdb.txt";
}  // namespace

std::string PlatformInput::ResolveFile(const std::string& name, const std::string& exe_dir,
                                       const std::string& game_files) {
    if (name.empty()) return std::string();
    if (name[0] == '/') return name;
    std::error_code ec;
    if (!exe_dir.empty()) {
        const std::string beside = exe_dir + "/" + name;
        if (std::filesystem::is_regular_file(beside, ec)) return beside;
    }
    return game_files + "/" + name;
}

ps2r::platform::sdl::InputSettings PlatformInput::SettingsFrom(const ps2r::Config& cfg, const std::string& exe_dir) {
    ps2r::platform::sdl::InputSettings s;
    s.bindings_path = ResolveFile(cfg.input.bindings_file, exe_dir, cfg.paths.game_files);
    s.device_config_path = ResolveFile(kDeviceConfigName, exe_dir, cfg.paths.game_files);
    s.gamecontrollerdb_path = ResolveFile(kGamepadDbName, exe_dir, cfg.paths.game_files);
    s.kbm_enabled = cfg.input.kbm_pad;
    // input.device: "none" disables the controller backend; "auto" and a
    // `guid:` selection leave it on (the selection itself is the device-config
    // INI's `pad =` line, which InputSystem::Init reads after these settings).
    s.gamepad_enabled = cfg.input.device != "none";
    return s;
}

PlatformInput::PlatformInput(const ps2r::platform::sdl::InputSettings& settings)
    : owner_(std::this_thread::get_id()), last_(std::chrono::steady_clock::now()) {
    system_.Init(settings);
    keyboard_enabled_.store(system_.KbmEnabled());
    gamepad_active_.store(system_.Gamepad() && system_.Gamepad()->IsActive());
}

PlatformInput::~PlatformInput() {
    assert(std::this_thread::get_id() == owner_);
    StopService();
    ApplyMotors(0, 0);
    system_.Shutdown();
}

void PlatformInput::BindMainWake(ps2r::Park* wake) {
    std::lock_guard<std::mutex> lock(mutex_);
    main_wake_ = wake;
}

void PlatformInput::BindOwnerBusy(const std::atomic<bool>* busy) {
    std::lock_guard<std::mutex> lock(mutex_);
    owner_busy_ = busy;
}

std::shared_ptr<PlatformInput::Work> PlatformInput::Request(Operation operation, unsigned first, unsigned second) {
    auto work = std::make_shared<Work>();
    work->operation = operation;
    work->first = first;
    work->second = second;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) return work;
        if (std::this_thread::get_id() != owner_) {
            if (pending_.size() >= 32) return work;
            pending_.push_back(work);
            if (main_wake_) main_wake_->Notify();
        }
    }
    if (std::this_thread::get_id() == owner_) Execute(*work);
    else while (!work->done.load(std::memory_order_acquire)) {
        const auto generation = work->wake.Generation();
        if (!work->done.load(std::memory_order_acquire)) work->wake.Wait(generation);
    }
    return work;
}

bool PlatformInput::HandleEvent(const SDL_Event& event) {
    if (std::this_thread::get_id() != owner_) return false;
    system_.HandleEvent(event);
    gamepad_active_.store(system_.Gamepad() && system_.Gamepad()->IsActive());
    ++drains_;
    return true;
}

bool PlatformInput::ServicePending() {
    if (std::this_thread::get_id() != owner_) return false;
    std::deque<std::shared_ptr<Work>> work;
    bool motors = false;
    unsigned small = 0, big = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        work.swap(pending_);
        motors = motors_pending_;
        motors_pending_ = false;
        small = motors_small_;
        big = motors_big_;
    }
    for (auto& request : work) Execute(*request);
    if (motors) ApplyMotors(small, big);
    return !work.empty() || motors;
}

void PlatformInput::StopService() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
    for (auto& work : pending_) {
        work->done.store(true, std::memory_order_release);
        work->wake.Notify();
    }
    pending_.clear();
}

void PlatformInput::Execute(Work& work) {
    assert(std::this_thread::get_id() == owner_);
    switch (work.operation) {
        case Operation::Sample: {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!held_keys_.empty() && system_.Kbm())
                    for (int scancode : held_keys_) system_.Kbm()->InjectKey(scancode);
            }
            const auto now = std::chrono::steady_clock::now();
            const float dt = std::clamp(std::chrono::duration<float>(now - last_).count(), 0.0f, kMaxDtSeconds);
            last_ = now;
            const auto sample = system_.Frame(dt);
            work.sample.buttons = sample.buttons;
            work.sample.lx = sample.lx; work.sample.ly = sample.ly;
            work.sample.rx = sample.rx; work.sample.ry = sample.ry;
            work.connected = system_.Connected();
            work.ok = true;
            break;
        }
        case Operation::Reset:
            system_.ResetPending();
            ApplyMotors(0, 0);
            work.ok = true;
            break;
        case Operation::Capture:
            system_.SetUiCapture(work.first != 0);
            if (work.first) SDL_SetRelativeMouseMode(SDL_FALSE);
            work.ok = true;
            break;
        case Operation::Keyboard:
            system_.SetKbmEnabled(work.first != 0);
            keyboard_enabled_.store(system_.KbmEnabled());
            work.ok = true;
            break;
        case Operation::VirtualPad: {
            auto* joystick = static_cast<SDL_Joystick*>(virtual_joystick_);
            switch (static_cast<VirtualPad>(work.first)) {
                case VirtualPad::Attach: {
                    if (joystick) break;
                    const int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 0);
                    if (index < 0) { std::fprintf(stderr, "nativeee: harness.virtual_pad attach failed: %s\n", SDL_GetError()); break; }
                    virtual_joystick_ = SDL_JoystickOpen(index);
                    virtual_index_.store(index);
                    work.ok = virtual_joystick_ != nullptr;
                    break;
                }
                case VirtualPad::Press:
                case VirtualPad::Release:
                    if (joystick) work.ok = SDL_JoystickSetVirtualButton(joystick, SDL_CONTROLLER_BUTTON_A,
                                                                         static_cast<VirtualPad>(work.first) == VirtualPad::Press ? 1 : 0) == 0;
                    break;
                case VirtualPad::Detach:
                    if (!joystick) break;
                    SDL_JoystickClose(joystick);
                    work.ok = SDL_JoystickDetachVirtual(virtual_index_.load()) == 0;
                    virtual_joystick_ = nullptr;
                    virtual_index_.store(-1);
                    break;
            }
            break;
        }
    }
    gamepad_active_.store(system_.Gamepad() && system_.Gamepad()->IsActive());
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sources_ = system_.Sources();
    }
    work.done.store(true, std::memory_order_release);
    work.wake.Notify();
}

bool PlatformInput::ApplyMotors(unsigned small, unsigned big) {
    if (small > 1 || big > 255) return false;
    auto* pads = system_.Gamepad();
    if (!pads || !pads->IsActive()) return false;
    bool applied = false, failed = false;
    for (const auto& pad : pads->Snapshot()) {
        if (pads->Selection() >= 0 && pads->Selection() != pad.instance_id) continue;
        auto* device = SDL_GameControllerFromInstanceID(pad.instance_id);
        if (!device || !SDL_GameControllerGetAttached(device) || !SDL_GameControllerHasRumble(device)) continue;
        const bool ok = SDL_GameControllerRumble(device, static_cast<Uint16>(big * 257),
            small ? 65535 : 0, (small || big) ? SDL_MAX_UINT32 : 0) == 0;
        applied |= ok;
        failed |= !ok;
    }
    return applied && !failed;
}

void PlatformInput::Accept(const Work& work) {
    if (!work.ok) return;
    last_sample_ = work.sample;
    last_connected_ = work.connected;
    ++samples_;
    std::lock_guard<std::mutex> lock(mutex_);
    accepted_connected_ = work.connected;
}

PadProtocol::Input PlatformInput::Sample() {
    {
        // Stopped (the session is closing): the neutral pad, not connected -- a
        // held button must not outlive the service.
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) { accepted_connected_ = false; return PadProtocol::Input{}; }
    }
    if (std::this_thread::get_id() == owner_) {   // a leg without a session: inline
        Work work;
        work.operation = Operation::Sample;
        Execute(work);
        Accept(work);
        return work.sample;
    }
    if (outstanding_) {
        // The previous boundary's post: answered since -> this boundary's read is
        // that answer (one boundary old, counted); still outstanding -> main is
        // busy (a display swap), the held sample serves and nothing new is posted.
        if (!outstanding_->done.load(std::memory_order_acquire)) {
            ++sample_late_;
            return last_sample_;
        }
        Accept(*outstanding_);
        outstanding_.reset();
        ++sample_stale_;
        return last_sample_;
    }
    auto work = std::make_shared<Work>();
    work->operation = Operation::Sample;
    bool owner_busy = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) { accepted_connected_ = false; return PadProtocol::Input{}; }
        if (pending_.size() >= 32) { ++sample_late_; return last_sample_; }
        pending_.push_back(work);
        if (main_wake_) main_wake_->Notify();
        owner_busy = owner_busy_ && owner_busy_->load(std::memory_order_acquire);
    }
    if (owner_busy) {   // main is inside a blocking call: no wait, the answer is the next boundary's
        outstanding_ = std::move(work);
        ++sample_late_;
        return last_sample_;
    }
    const auto started = std::chrono::steady_clock::now();
    const uint64_t deadline = ps2r::Park::NowNs() + kSampleWaitNs;
    while (!work->done.load(std::memory_order_acquire)) {
        const auto generation = work->wake.Generation();
        if (work->done.load(std::memory_order_acquire)) break;
        if (!work->wake.WaitUntil(deadline, generation) && !work->done.load(std::memory_order_acquire)) break;
    }
    const uint64_t waited_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    uint64_t seen = sample_wait_max_us_.load(std::memory_order_relaxed);
    while (waited_us > seen && !sample_wait_max_us_.compare_exchange_weak(seen, waited_us)) {}
    if (work->done.load(std::memory_order_acquire)) {
        Accept(*work);
        return last_sample_;
    }
    outstanding_ = std::move(work);   // main will answer; the next boundary takes it
    ++sample_late_;
    return last_sample_;
}

bool PlatformInput::Connected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return accepted_connected_;
}

bool PlatformInput::QueueMotors(unsigned small, unsigned big) {
    if (small > 1 || big > 255) return false;
    if (std::this_thread::get_id() == owner_) return ApplyMotors(small, big);
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) return false;
    motors_pending_ = true;   // coalesced: the latest value is the one applied
    motors_small_ = small;
    motors_big_ = big;
    if (main_wake_) main_wake_->Notify();
    return gamepad_active_.load();
}

std::string PlatformInput::Census() const {
    return "samples=" + std::to_string(samples_.load()) + ",late=" + std::to_string(sample_late_.load()) +
           ",stale=" + std::to_string(sample_stale_.load()) + ",wait_max_ms=" + std::to_string(sample_wait_max_us_.load() / 1000) +
           ",drains=" + std::to_string(drains_.load()) + ",gamepad=" + (gamepad_active_.load() ? "active" : "idle");
}

std::string PlatformInput::Sources() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sources_;
}

void PlatformInput::HoldKey(int scancode, bool held) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find(held_keys_.begin(), held_keys_.end(), scancode);
    if (held && it == held_keys_.end()) held_keys_.push_back(scancode);
    else if (!held && it != held_keys_.end()) held_keys_.erase(it);
}

void PlatformInput::VirtualPadAction(VirtualPad action) { Request(Operation::VirtualPad, static_cast<unsigned>(action)); }
void PlatformInput::ResetPending() { Request(Operation::Reset); }
void PlatformInput::SetUiCapture(bool captured) { Request(Operation::Capture, captured); }
void PlatformInput::SetKeyboardEnabled(bool enabled) { Request(Operation::Keyboard, enabled); }
