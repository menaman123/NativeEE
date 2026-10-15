#pragma once
// RAII guard that puts the host FPU in round-toward-zero for the scope.
//
// PS2 rule: the VU FMAC/FDIV/EFU round every result toward zero
// (truncation), so every VU execution entry and every VU1 HLE library body
// runs under FE_TOWARDZERO and restores the caller's mode on exit. Rounding
// mode is per-thread state, so the guard is safe on a worker thread.

class VuChopRoundGuard {
public:
    VuChopRoundGuard();
    ~VuChopRoundGuard();
    VuChopRoundGuard(const VuChopRoundGuard&) = delete;
    VuChopRoundGuard& operator=(const VuChopRoundGuard&) = delete;

private:
    int prev_;
};
