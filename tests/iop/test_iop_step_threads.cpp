// tests/iop/test_iop_step_threads.cpp -- IopKernel step bodies (StartStepThread) vs coroutine bodies
// (StartThread): the SAME scheduler decisions for the same workload, and the host cost of each form.
//
// Workload: a producer thread signals a semaphore N times per quantum (a timer beat does the same), two
// consumer threads of different priorities wait on it and record (tid, count) effects; a sleeper is
// woken by the producer; an event-flag waiter and a message-box waiter; a Delay. The workload is run once
// with coroutine bodies and once with step bodies; the recorded effect sequences must be identical (the
// scheduler, not the body form, decides the order). Then the cost: K dispatch passes of a sema ping-pong
// in each form, steady_clock, printed (never asserted: the host is shared).
#include <chrono>
#include <memory>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/iop/iop_error.h"
#include "engine/iop/iop_kernel.h"
#include "engine/platform/host/platform.h"

using ps2r::iop::IopClock;
using ps2r::iop::IopError;
using ps2r::iop::IopKernel;

static int g_failures = 0;

#define CHECK(cond, name)                                        \
    do {                                                         \
        if (cond) {                                              \
            std::printf("PASS: %s\n", name);                     \
        } else {                                                 \
            std::printf("FAIL: %s (line %d)\n", name, __LINE__); \
            g_failures++;                                        \
        }                                                        \
    } while (0)

namespace {

struct Effects {
    std::vector<std::string> rows;
    void Add(const char* what, int a, int b) { rows.push_back(std::string(what) + ":" + std::to_string(a) + ":" + std::to_string(b)); }
};

constexpr int kSema = 3;
constexpr int kRounds = 6;

// The consumer: for(;;) { r = WaitSema(s); record }   -- coroutine form.
void ConsumerCoro(IopKernel& k, Effects& e, int tid) {
    for (int i = 0;; ++i) {
        const int r = k.WaitSema(kSema);
        e.Add("consume", tid, r * 100 + i);
    }
}

// The same consumer as a step body: the park is returned, the result arrives in Resume.
IopKernel::StepBody ConsumerStep(Effects& e, int tid) {
    auto i = std::make_shared<int>(0);
    return [&e, tid, i](const IopKernel::Resume& r) -> IopKernel::Park {
        if (!r.first) e.Add("consume", tid, r.result * 100 + (*i)++);
        return IopKernel::Park::Sema(kSema);
    };
}

// The sleeper: for(;;) { SleepThread(); record }.
void SleeperCoro(IopKernel& k, Effects& e, int tid) {
    for (int i = 0;; ++i) {
        k.SleepThread();
        e.Add("woke", tid, i);
    }
}
IopKernel::StepBody SleeperStep(Effects& e, int tid) {
    auto i = std::make_shared<int>(0);
    return [&e, tid, i](const IopKernel::Resume& r) -> IopKernel::Park {
        if (!r.first) e.Add("woke", tid, (*i)++);
        return IopKernel::Park::Sleep();
    };
}

// The producer: per round -- SignalSema x2, WakeupThread(sleeper), SetEventFlag, SendMbx, DelayUs(100).
void ProducerBody(IopKernel& k, Effects& e, int ef, int mbx, int sleeper, int round) {
    k.SignalSema(kSema);
    k.SignalSema(kSema);
    k.WakeupThread(sleeper);
    k.SetEventFlag(ef, 1u << (round & 7));
    k.SendMbx(mbx, reinterpret_cast<void*>(static_cast<uintptr_t>(round + 1)));
    e.Add("produced", round, k.SemaCount(kSema));
}
void ProducerCoro(IopKernel& k, Effects& e, int ef, int mbx, int sleeper) {
    for (int round = 0; round < kRounds; ++round) {
        ProducerBody(k, e, ef, mbx, sleeper, round);
        k.DelayUs(100);
    }
    e.Add("producer_exit", 0, 0);
}
IopKernel::StepBody ProducerStep(IopKernel& k, Effects& e, int ef, int mbx, int sleeper) {
    auto round = std::make_shared<int>(0);
    return [&k, &e, ef, mbx, sleeper, round](const IopKernel::Resume&) -> IopKernel::Park {
        if (*round >= kRounds) {
            e.Add("producer_exit", 0, 0);
            return IopKernel::Park::Exit();
        }
        ProducerBody(k, e, ef, mbx, sleeper, (*round)++);
        return IopKernel::Park::Delay(100);
    };
}

// The event-flag waiter: for(;;) { WaitEventFlag(ef, 0xff, OR|CLEAR); record } and the mbx receiver.
void FlagCoro(IopKernel& k, Effects& e, int ef, int tid) {
    for (int i = 0;; ++i) {
        const int r = k.WaitEventFlag(ef, 0xff, 0x11);
        e.Add("flag", tid, r * 100 + i);
    }
}
IopKernel::StepBody FlagStep(Effects& e, int ef, int tid) {
    auto i = std::make_shared<int>(0);
    return [&e, ef, tid, i](const IopKernel::Resume& r) -> IopKernel::Park {
        if (!r.first) e.Add("flag", tid, r.result * 100 + (*i)++);
        return IopKernel::Park::EventFlag(ef, 0xff, 0x11);
    };
}
void MbxCoro(IopKernel& k, Effects& e, int mbx, int tid) {
    for (;;) {
        void* msg = nullptr;
        k.ReceiveMbx(&msg, mbx);
        e.Add("mbx", tid, static_cast<int>(reinterpret_cast<uintptr_t>(msg)));
    }
}
IopKernel::StepBody MbxStep(Effects& e, int mbx, int tid) {
    return [&e, mbx, tid](const IopKernel::Resume& r) -> IopKernel::Park {
        if (!r.first) e.Add("mbx", tid, static_cast<int>(reinterpret_cast<uintptr_t>(r.message)));
        return IopKernel::Park::Mbx(mbx);
    };
}

void DriveIdle(IopKernel& k, int max_iters = 1000) {
    for (int i = 0; i < max_iters; i++) {
        auto r = k.Dispatch();
        if (!r.next_due_us) {
            if (!r.ran_something) return;
            continue;
        }
        k.Clock().AdvanceToUs(*r.next_due_us);
    }
}

Effects RunWorkload(bool step) {
    IopKernel k;
    k.Clock().SetPolicy(IopClock::Policy::Frame);
    Effects e;
    k.RegisterSema(kSema, 0, 100);
    const int ef = k.CreateEventFlag(0, false);
    const int mbx = k.CreateMbx();
    // tids: 1 producer (prio 30), 2 consumer A (prio 20), 3 consumer B (prio 40), 4 sleeper (25), 5 flag (35), 6 mbx (45)
    k.RegisterThread(1, 30);
    k.RegisterThread(2, 20);
    k.RegisterThread(3, 40);
    k.RegisterThread(4, 25);
    k.RegisterThread(5, 35);
    k.RegisterThread(6, 45);
    if (step) {
        k.StartStepThread(2, ConsumerStep(e, 2));
        k.StartStepThread(3, ConsumerStep(e, 3));
        k.StartStepThread(4, SleeperStep(e, 4));
        k.StartStepThread(5, FlagStep(e, ef, 5));
        k.StartStepThread(6, MbxStep(e, mbx, 6));
        k.StartStepThread(1, ProducerStep(k, e, ef, mbx, 4));
    } else {
        k.StartThread(2, [&] { ConsumerCoro(k, e, 2); });
        k.StartThread(3, [&] { ConsumerCoro(k, e, 3); });
        k.StartThread(4, [&] { SleeperCoro(k, e, 4); });
        k.StartThread(5, [&] { FlagCoro(k, e, ef, 5); });
        k.StartThread(6, [&] { MbxCoro(k, e, mbx, 6); });
        k.StartThread(1, [&] { ProducerCoro(k, e, ef, mbx, 4); });
    }
    DriveIdle(k);
    e.Add("final_sema", k.SemaCount(kSema), 0);
    e.Add("states", static_cast<int>(k.ThreadState(1)), static_cast<int>(k.ThreadState(2)));
    e.Add("counts", static_cast<int>(k.CoroutineSwitches()), static_cast<int>(k.StepRuns()));
    return e;
}

// The cost: a sema ping-pong between two threads, K rounds, each form; ns per ROUND (two handoffs: the
// whole ping-pong runs inside one Dispatch pass, each handoff readying the other thread).
double PingPongNsPerRound(bool step, int rounds) {
    IopKernel k;
    k.Clock().SetPolicy(IopClock::Policy::Frame);
    k.RegisterSema(1, 0, 10);
    k.RegisterSema(2, 0, 10);
    k.RegisterThread(1, 20);
    k.RegisterThread(2, 20);
    int done = 0;
    if (step) {
        k.StartStepThread(1, [&k, &done, rounds](const IopKernel::Resume& r) -> IopKernel::Park {
            if (!r.first) {
                if (++done >= rounds) return IopKernel::Park::Exit();
            }
            k.SignalSema(2);
            return IopKernel::Park::Sema(1);
        });
        k.StartStepThread(2, [&k](const IopKernel::Resume& r) -> IopKernel::Park {
            if (!r.first) k.SignalSema(1);
            return IopKernel::Park::Sema(2);
        });
    } else {
        k.StartThread(1, [&] {
            for (;;) {
                k.SignalSema(2);
                k.WaitSema(1);
                if (++done >= rounds) return;
            }
        });
        k.StartThread(2, [&] {
            for (;;) {
                k.WaitSema(2);
                k.SignalSema(1);
            }
        });
    }
    const auto t0 = std::chrono::steady_clock::now();
    uint64_t passes = 0;
    // Each pass runs both threads once (a handoff each way); the producer exits after `rounds`.
    for (int i = 0; i < rounds * 4 && k.ThreadState(1) != IopKernel::State::Dormant; ++i) {
        k.Dispatch();
        ++passes;
    }
    const double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
    std::printf("  (%s: %llu dispatch pass(es) for %d rounds)\n", step ? "step" : "coroutine", static_cast<unsigned long long>(passes), rounds);
    return ns / static_cast<double>(rounds);
}

}  // namespace

int main() {
    ps2r::platform::host::RegisterSelf();   // the coroutines' fiber backend
    const Effects coro = RunWorkload(false);
    const Effects step = RunWorkload(true);
    // The two forms differ in exactly one recorded row: the switch/step census.
    std::vector<std::string> a = coro.rows, b = step.rows;
    CHECK(!a.empty() && a.back().rfind("counts:", 0) == 0, "census row present");
    a.pop_back();
    b.pop_back();
    CHECK(a == b, "step bodies: the same effect sequence as coroutine bodies (sema handoff, wakeup, flag, mbx, delay, exit)");
    if (a != b) {
        for (size_t i = 0; i < a.size() || i < b.size(); ++i)
            std::printf("  %3zu coro=%-28s step=%s\n", i, i < a.size() ? a[i].c_str() : "-", i < b.size() ? b[i].c_str() : "-");
    }
    CHECK(coro.rows.back().rfind("counts:", 0) == 0 && coro.rows.back().find(":0") == coro.rows.back().size() - 2,
          "coroutine run: zero step runs");
    CHECK(step.rows.back().rfind("counts:0:", 0) == 0, "step run: zero coroutine switches");
    bool consumed = false;
    for (const std::string& r : a) consumed = consumed || r.rfind("consume:2:", 0) == 0;
    CHECK(consumed, "the workload exercised the sema consumers");
    // The cost, printed (the host is shared; a ratio is the information).
    const int rounds = 20000;
    const double coro_ns = PingPongNsPerRound(false, rounds);
    const double step_ns = PingPongNsPerRound(true, rounds);
    std::printf("COST: sema ping-pong, %d rounds (2 thread handoffs each): coroutine %.0f ns/round, step %.0f ns/round (step/coro %.2f)\n",
                rounds, coro_ns, step_ns, coro_ns > 0 ? step_ns / coro_ns : 0.0);
    CHECK(step_ns > 0 && coro_ns > 0, "both forms ran the ping-pong");
    std::printf("%s\n", g_failures ? "FAILED" : "ALL PASS");
    return g_failures ? 1 : 0;
}
