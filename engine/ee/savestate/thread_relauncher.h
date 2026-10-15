#pragma once
// ThreadRelauncher: the savestate relaunch step. After the THRD section has
// installed the thread table (info only), every record that was started and
// carries a parked context gets a fresh host worker whose run loop re-
// establishes the guest call chain logically -- the original host stack of
// the parked thread cannot be rebuilt:
//   * chain[0] (innermost) is re-entered at FUNCTION START: for an admitted
//     save that frame is the blocking kernel/HLE stub, so re-running it
//     re-executes the wait with the restored arguments and re-parks the
//     thread exactly as saved;
//   * the boundary thread (is_boundary) enters chain[0] at ctx.$ra instead --
//     the post-HLE continuation of the in-flight frame-boundary call, which
//     replays "the HLE just returned" including chain[0]'s post-sync tail;
//   * when a dispatch level returns, the next outer frame is entered at the
//     returning frame's ra (a block-start label of the generated function --
//     the post-call continuation); when the outermost frame returns the
//     thread ends;
//   * a chain frame naming no generated function is a state divergence: the
//     thread parks the leg (`resume_no_function`) instead of silently
//     re-running from the entry.
// Election protocol (EeRunToken, BUG-036): BeginRelaunch holds elections,
// every resumed thread is readied at its restored current priority in file
// order, EndRelaunch runs ONE election, so the first post-load runner is the
// thbase winner, never the first host thread to arrive (RelaunchOrder).
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/savestate/thread_section.h"

class ThreadManager;

class ThreadRelauncher {
public:
    // Enters generated function `fn` at guest address `enter` (0 = function
    // start); false when no generated function starts at `fn`.
    using EnterFn = std::function<bool(uint32_t fn, uint32_t enter, CpuContext& ctx)>;

    ThreadRelauncher(ThreadManager& threads, EnterFn enter);

    // Relaunches every started record with a context. Returns the number of
    // workers started; -1 (with `err`) when a worker could not be created
    // (the table is then partially relaunched: the caller treats it as fatal).
    int Relaunch(const std::vector<ThreadSection::Record>& records, std::string* err);

    // The chain walk one relaunched worker runs (public for the unit test).
    void WalkChain(const ThreadSection::Record& record, CpuContext& ctx) const;

private:
    ThreadManager& threads_;
    EnterFn enter_;
};
