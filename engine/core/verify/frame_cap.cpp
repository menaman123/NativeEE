// core/verify/frame_cap.cpp
#include "engine/core/verify/frame_cap.h"

#include <cstdlib>

namespace ps2r::verify {

bool FrameCap::OnBoundary() {
    count_++;
    return cap_ != 0 && count_ >= cap_;
}

void FrameCap::RunFinalizers() {
    if (finalized_) return;
    finalized_ = true;
    for (auto& fn : finalizers_)
        if (fn) fn();
}

void FrameCap::ExitNow(int code) {
    RunFinalizers();
    std::_Exit(code);
}

}  // namespace ps2r::verify
