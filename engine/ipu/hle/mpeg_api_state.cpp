#include "engine/ipu/hle/mpeg_api.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace ps2r::ipu {
namespace {
bool Idle(const MpegContext& state) {
    return !state.saw_input && !state.stream_ended && !state.end_after_pending &&
        !state.frames_served && state.frames.empty() && state.pending_es.empty() &&
        !state.pending_es_off && !state.AudioCarried() && !state.gate_opened &&
        !state.demux.Ended() && !state.demux.Buffered();
}
bool Fail(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}
}  // namespace

bool MpegApi::CaptureIdle(int64_t now, std::vector<IdleContext>& out, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<IdleContext> saved;
    saved.reserve(contexts_.size());
    for (const auto& [address, context] : contexts_) {
        if (!Idle(*context)) return Fail(error, "Movie software input/pictures/PCM still require Close");
        IdleContext item{address, context->ctx_addr, context->width, context->height,
                         context->pace_rate_fps, {}};
        if (!context->pacer.Capture(now, item.clock)) return Fail(error, "Movie clock cannot be captured");
        saved.push_back(item);
    }
    std::sort(saved.begin(), saved.end(), [](const auto& a, const auto& b) { return a.address < b.address; });
    if (!ValidateIdle(saved, now, error)) return false;
    out = std::move(saved);
    return true;
}

bool MpegApi::ValidateIdle(const std::vector<IdleContext>& saved, int64_t now, std::string* error) const {
    if (now < 0) return Fail(error, "Movie domain clock is not representable");
    uint32_t prior = 0;
    for (const auto& item : saved) {
        if (!item.address || item.address <= prior ||
            !memory_.Translate(item.address, sizeof(uint32_t)) ||
            (item.context_address && item.context_address != item.address))
            return Fail(error, "Movie software contexts are invalid, duplicated or unordered");
        if (!item.width || !item.height || item.width > 4096 || item.height > 4096 ||
            !std::isfinite(item.authored_fps) || item.authored_fps < 0 || item.authored_fps > 240 ||
            item.clock.period < 0 || !FmvPacer::Validate(item.clock, now))
            return Fail(error, "Movie software dimensions/rate/clock are invalid");
        if (item.clock.period == 0) {
            if (item.authored_fps != 0 || item.clock.armed)
                return Fail(error, "Movie unused clock contains a delivery deadline");
        } else {
            const double period = std::max(1.0, 60.0 / item.authored_fps + 0.5);
            if (!std::isfinite(period) || period >= double(std::numeric_limits<int64_t>::max()) ||
                int64_t(period) != item.clock.period)
                return Fail(error, "Movie delivery period disagrees with its authored rate");
        }
        prior = item.address;
    }
    return true;
}

bool MpegApi::RestoreIdle(const std::vector<IdleContext>& saved, int64_t now, std::string* error) {
    auto prepared = PrepareIdle(saved, now, error);
    return prepared && PublishIdle(*prepared, now, error);
}

std::unique_ptr<MpegIdleState> MpegApi::PrepareIdle(const std::vector<IdleContext>& saved, int64_t now,
                                                  std::string* error) {
    if (!ValidateIdle(saved, now, error)) return {};
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [address, context] : contexts_) {
        (void)address;
        if (!Idle(*context)) {
            Fail(error, "Movie software restore requires quiescent Close");
            return {};
        }
    }
    try {
        std::unique_ptr<MpegIdleState> prepared(new MpegIdleState);
        prepared->owner_ = this;
        prepared->origin_ = now;
        for (const auto& item : saved) {
            auto context = NewContext();
            context->ctx_addr = item.context_address;
            context->width = item.width;
            context->height = item.height;
            context->pace_rate_fps = item.authored_fps;
            if (!context->pacer.Restore(item.clock, now)) {
                Fail(error, "Movie clock restore failed");
                return {};
            }
            prepared->contexts_.emplace(item.address, std::move(context));
        }
        return prepared;
    } catch (const std::exception&) {
        Fail(error, "Movie software context preparation failed");
        return {};
    }
}

bool MpegApi::PublishIdle(MpegIdleState& prepared, int64_t now, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (prepared.owner_ != this || prepared.origin_ != now)
        return Fail(error, "Movie prepared software owner or clock changed");
    for (const auto& [address, context] : contexts_) {
        (void)address;
        if (!Idle(*context)) return Fail(error, "Movie software publication requires quiescent Close");
    }
    contexts_.swap(prepared.contexts_);
    prepared.owner_ = nullptr;
    // Restore reconstructs only closed software state. Source Close already
    // released PCM; the application separately verifies that sink ownership
    // is closed before admission. Reconstructing its clock is not a new
    // Close/abort intent and must not touch the sink or its execution census.
    return true;
}
}  // namespace ps2r::ipu
