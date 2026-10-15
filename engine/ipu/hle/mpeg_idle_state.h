#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include "engine/ipu/hle/mpeg_context.h"

namespace ps2r::ipu {
class MpegApi;

// Unpublished closed software contexts. The application holds this allocation
// across its prepare/join/apply transaction; dropping it has no live effects.
class MpegIdleState {
private:
    friend class MpegApi;
    MpegIdleState() = default;
    MpegApi* owner_ = nullptr;
    int64_t origin_ = 0;
    std::unordered_map<uint32_t, std::unique_ptr<MpegContext>> contexts_;
};
} // namespace ps2r::ipu
