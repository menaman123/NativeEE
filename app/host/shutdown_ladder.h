// app/host/shutdown_ladder.h -- how long a shutdown waits for guest worker
// threads before hard-exiting (engine.shutdown_wait_s, default 10 s, ceiling
// 600 s; 0 = never wait, exit immediately). Pure policy: the caller supplies
// elapsed time and asks for the next action.
#pragma once

namespace ps2r::app {

class ShutdownLadder {
public:
    static constexpr int kDefaultWaitSeconds = 10;
    static constexpr int kMaxWaitSeconds = 600;
    enum class Action { Wait, Warn, HardExit };

    explicit ShutdownLadder(int wait_seconds) : wait_s_(Clamp(wait_seconds)) {}

    // Clamp a configured value into [0, kMaxWaitSeconds].
    static int Clamp(int wait_seconds);
    int WaitSeconds() const { return wait_s_; }

    // A stopped emulator unwinds a guest worker only when a wait is allowed.
    bool ShouldUnwindGuestThread(bool emulator_running, bool is_guest_worker) const;
    // Next join action after `elapsed_s`: Warn once at 5 s, HardExit at the
    // configured wait (never when wait == 0 -- the caller exits at once).
    Action JoinActionAt(double elapsed_s, bool warned) const;

private:
    int wait_s_;
};

}  // namespace ps2r::app
