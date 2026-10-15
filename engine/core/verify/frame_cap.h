// core/verify/frame_cap.h -- exit after N frame boundaries (--frames N, the
// harness leg length). Compiled in ALL builds: it is how a leg ends.
//
// When the cap is reached the registered finalizers run in order (RunMeta,
// hash tails, savestate writer join, bench report) and the process leaves
// through std::_Exit so no static destructor or worker thread can alter the
// already-finalized streams.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace ps2r::verify {

class FrameCap {
public:
    // frames == 0 -> no cap.
    void Arm(uint64_t frames) { cap_ = frames; }
    bool Armed() const { return cap_ != 0; }
    uint64_t Cap() const { return cap_; }
    uint64_t Count() const { return count_; }

    // Finalizers run in registration order at the cap.
    void AddFinalizer(std::function<void()> fn) { finalizers_.push_back(std::move(fn)); }

    // Count one boundary; true when the cap is reached (count >= cap).
    bool OnBoundary();

    // Run the finalizers (once) and leave the process with `code`.
    [[noreturn]] void ExitNow(int code);
    // Run the finalizers once without exiting (normal teardown path).
    void RunFinalizers();

private:
    uint64_t cap_ = 0;
    uint64_t count_ = 0;
    bool     finalized_ = false;
    std::vector<std::function<void()>> finalizers_;
};

}  // namespace ps2r::verify
