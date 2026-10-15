#pragma once
// Warp: the harness's run-to-frame release policy (Config
// harness.warp_to_frame). While frames [0, target) are in flight the
// TickSource releases at once and presents are skipped; reaching the target
// frame re-enables pacing. A target of 0 means never configured (no-op).
// The frame number is the emu-domain boundary count -- the warp cannot change
// any clock, only how fast the host reaches a boundary.
#include <cstdint>

#include "engine/ee/timing/tick_source.h"

class Warp {
public:
    void SetTarget(uint32_t target_frame);
    uint32_t TargetFrame() const { return target_; }
    bool Active() const { return active_; }

    // Called once per frame boundary with the frame just completed.
    // Returns true when this call released the warp.
    bool OnFrame(uint64_t frame);
    // Pushes the current state into the source: Warp mode while active,
    // `fallback` otherwise.
    void Apply(TickSource& source, TickSource::Mode fallback, uint32_t fallback_k) const;

private:
    uint32_t target_ = 0;
    bool active_ = false;
};
