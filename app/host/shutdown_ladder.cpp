// app/host/shutdown_ladder.cpp
#include "app/host/shutdown_ladder.h"

namespace ps2r::app {

int ShutdownLadder::Clamp(int wait_seconds) {
    if (wait_seconds <= 0) return 0;
    return wait_seconds > kMaxWaitSeconds ? kMaxWaitSeconds : wait_seconds;
}

bool ShutdownLadder::ShouldUnwindGuestThread(bool emulator_running, bool is_guest_worker) const {
    return !emulator_running && is_guest_worker && wait_s_ != 0;
}

ShutdownLadder::Action ShutdownLadder::JoinActionAt(double elapsed_s, bool warned) const {
    if (wait_s_ != 0 && elapsed_s >= (double)wait_s_) return Action::HardExit;
    if (!warned && elapsed_s >= 5.0) return Action::Warn;
    return Action::Wait;
}

}  // namespace ps2r::app
