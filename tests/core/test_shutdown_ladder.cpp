// tests/core/test_shutdown_ladder.cpp -- the bounded-shutdown PURE policy
// (app/host/shutdown_ladder.h), the original test_shutdown_policy tables with
// the env parse replaced by the Config clamp:
//   (a) Clamp: <= 0 -> 0 (never wait), positive passes, > 600 clamps to 600;
//   (b) ShouldUnwindGuestThread: only when shutdown was requested AND the
//       caller is a guest worker AND wait != 0 -- never while running, never
//       on host threads;
//   (c) JoinActionAt ladder: Wait below 5 s; Warn once at >= 5 s; HardExit at
//       >= wait with priority over Warn; wait == 0 never hard-exits.
#include <cstdio>

#include "app/host/shutdown_ladder.h"

static int g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            g_fail++;                                                        \
        }                                                                    \
    } while (0)

using ps2r::app::ShutdownLadder;
using Action = ShutdownLadder::Action;

static void TestClamp() {
    CHECK(ShutdownLadder::kDefaultWaitSeconds == 10);
    CHECK(ShutdownLadder::kMaxWaitSeconds == 600);
    CHECK(ShutdownLadder::Clamp(0) == 0);
    CHECK(ShutdownLadder::Clamp(-5) == 0);
    CHECK(ShutdownLadder::Clamp(1) == 1);
    CHECK(ShutdownLadder::Clamp(10) == 10);
    CHECK(ShutdownLadder::Clamp(120) == 120);
    CHECK(ShutdownLadder::Clamp(600) == 600);
    CHECK(ShutdownLadder::Clamp(601) == 600);
    CHECK(ShutdownLadder::Clamp(99999) == 600);
    CHECK(ShutdownLadder(99999).WaitSeconds() == 600);
    CHECK(ShutdownLadder(-1).WaitSeconds() == 0);
    CHECK(ShutdownLadder(ShutdownLadder::kDefaultWaitSeconds).WaitSeconds() == 10);
}

static void TestShouldUnwindGuestThread() {
    const ShutdownLadder ten(10), zero(0);
    // Emulator still running: never unwind, whoever asks.
    CHECK(!ten.ShouldUnwindGuestThread(true, true));
    CHECK(!ten.ShouldUnwindGuestThread(true, false));
    CHECK(!zero.ShouldUnwindGuestThread(true, true));
    // Shutdown requested, host thread: never.
    CHECK(!ten.ShouldUnwindGuestThread(false, false));
    CHECK(!zero.ShouldUnwindGuestThread(false, false));
    // Shutdown requested, guest worker: only when a wait is allowed.
    CHECK(ten.ShouldUnwindGuestThread(false, true));
    CHECK(ShutdownLadder(1).ShouldUnwindGuestThread(false, true));
    CHECK(!zero.ShouldUnwindGuestThread(false, true));
}

static void TestJoinActionAt() {
    const ShutdownLadder ten(10);
    CHECK(ten.JoinActionAt(0.0, false) == Action::Wait);
    CHECK(ten.JoinActionAt(4.99, false) == Action::Wait);
    // Warn at the 5 s threshold, once.
    CHECK(ten.JoinActionAt(5.0, false) == Action::Warn);
    CHECK(ten.JoinActionAt(7.0, false) == Action::Warn);
    CHECK(ten.JoinActionAt(5.0, true) == Action::Wait);
    CHECK(ten.JoinActionAt(9.99, true) == Action::Wait);
    // HardExit at the wait bound (warned or not).
    CHECK(ten.JoinActionAt(10.0, true) == Action::HardExit);
    CHECK(ten.JoinActionAt(10.0, false) == Action::HardExit);
    CHECK(ten.JoinActionAt(1e9, true) == Action::HardExit);
    // Small wait (3 s): hard-exit takes priority, before the 5 s warn.
    const ShutdownLadder three(3);
    CHECK(three.JoinActionAt(2.9, false) == Action::Wait);
    CHECK(three.JoinActionAt(3.0, false) == Action::HardExit);
    CHECK(three.JoinActionAt(6.0, false) == Action::HardExit);
    // wait == 0: warn once at 5 s, never hard-exit.
    const ShutdownLadder zero(0);
    CHECK(zero.JoinActionAt(4.0, false) == Action::Wait);
    CHECK(zero.JoinActionAt(5.0, false) == Action::Warn);
    CHECK(zero.JoinActionAt(5.0, true) == Action::Wait);
    CHECK(zero.JoinActionAt(1e9, true) == Action::Wait);
    CHECK(zero.JoinActionAt(1e9, false) == Action::Warn);
    // wait == 1 (below the warn threshold): immediate hard bound.
    const ShutdownLadder one(1);
    CHECK(one.JoinActionAt(0.99, false) == Action::Wait);
    CHECK(one.JoinActionAt(1.0, false) == Action::HardExit);
}

int main() {
    TestClamp();
    TestShouldUnwindGuestThread();
    TestJoinActionAt();
    if (g_fail) {
        std::printf("test_shutdown_ladder: %d FAILURE(S)\n", g_fail);
        return 1;
    }
    std::printf("test_shutdown_ladder: all checks passed\n");
    return 0;
}
