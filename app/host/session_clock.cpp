#include "app/host/session_clock.h"

#include <limits>
#include <stdexcept>
#include <utility>

#include "engine/core/savestate/payload_reader.h"

namespace ps2r::app {
namespace {
template <typename T> void Put(std::vector<uint8_t>& out, T value) {
    const auto bits = static_cast<uint64_t>(value);
    for (size_t i = 0; i < sizeof(T); ++i) out.push_back(static_cast<uint8_t>(bits >> (8 * i)));
}
bool ValueValid(const SessionClock::Snapshot& value, std::string& error) {
    if (value.providers.empty() || value.providers.size() > SessionClock::kMaxIdentityBytes ||
        value.providers.find('\0') != std::string::npos) {
        error = "Clock provider identity is missing or invalid.";
        return false;
    }
    if (!EmuDomain::Validate(value.domain, value.raw_tick)) {
        error = "Saved logical clock is invalid.";
        return false;
    }
    if (value.pending_ticks > static_cast<uint64_t>(std::numeric_limits<int>::max()) ||
        value.pending_ticks > UINT64_MAX - value.raw_tick ||
        value.pending_ticks > UINT64_MAX - value.domain.tick) {
        error = "Saved pending tick requests exceed the clock range.";
        return false;
    }
    auto after_requests = value.domain;
    after_requests.tick += value.pending_ticks;
    if (!EmuDomain::Validate(after_requests, value.raw_tick + value.pending_ticks)) {
        error = "Saved pending tick requests exceed the clock range.";
        return false;
    }
    return true;
}
} // namespace

SessionClock::SessionClock(EmuDomain& domain, TickSource& source, TickSync& ticks, std::string providers)
    : domain_(domain), source_(source), ticks_(ticks), providers_(std::move(providers)) {}

SessionClock::Snapshot SessionClock::Capture() const {
    if (ticks_.BodyActive()) throw std::runtime_error("Clock capture requires a completed tick body.");
    return {providers_, source_.RawCount(), domain_.Capture(), ticks_.Pending()};
}

bool SessionClock::SetFrame(savestate::FileMeta& meta, uint64_t frame, std::string& error) {
    if (frame > std::numeric_limits<uint32_t>::max()) {
        error = "Completed game frame exceeds the reproduction format.";
        return false;
    }
    meta.vsync_frame = static_cast<uint32_t>(frame);
    return true;
}

bool SessionClock::Encode(const Snapshot& value, std::vector<uint8_t>& out, std::string& error) {
    if (!ValueValid(value, error)) return false;
    std::vector<uint8_t> bytes;
    Put<uint32_t>(bytes, kVersion);
    Put<uint32_t>(bytes, static_cast<uint32_t>(value.providers.size()));
    bytes.insert(bytes.end(), value.providers.begin(), value.providers.end());
    Put(bytes, value.raw_tick);
    const auto& state = value.domain;
    Put(bytes, state.tick); Put(bytes, state.base); Put(bytes, state.boundary_raw);
    Put(bytes, state.arrival_raw); Put(bytes, state.boundaries); Put(bytes, state.lead);
    Put(bytes, state.regime); Put(bytes, state.pin);
    Put<uint32_t>(bytes, (state.have_boundary ? 1 : 0) | (state.bypass ? 2 : 0) |
                         (state.pacer_release ? 4 : 0) | (state.detector.fast ? 8 : 0));
    Put(bytes, state.detector.n); Put(bytes, state.detector.samples);
    Put(bytes, state.detector.over); Put(bytes, state.detector.under);
    Put(bytes, value.pending_ticks);
    out = std::move(bytes);
    return true;
}

bool SessionClock::Decode(const savestate::Blob& blob, Snapshot& out, std::string& error) {
    auto fail = [&] { error = "Malformed or unsupported logical clock section."; return false; };
    if (blob.tag != kTag || blob.version != kVersion || blob.flags != savestate::kSectionRequired ||
        blob.payload.size() > kMaxIdentityBytes + 128) return fail();
    savestate::PayloadReader reader(blob.payload);
    uint32_t version = 0, length = 0, flags = 0;
    if (!reader.Get(&version) || version != kVersion || !reader.Get(&length) ||
        !length || length > kMaxIdentityBytes || length > reader.Remaining()) return fail();
    Snapshot value;
    value.providers.resize(length);
    if (!reader.GetBytes(value.providers.data(), length) || !reader.Get(&value.raw_tick)) return fail();
    auto& state = value.domain;
    if (!reader.Get(&state.tick) || !reader.Get(&state.base) || !reader.Get(&state.boundary_raw) ||
        !reader.Get(&state.arrival_raw) || !reader.Get(&state.boundaries) || !reader.Get(&state.lead) ||
        !reader.Get(&state.regime) || !reader.Get(&state.pin) || !reader.Get(&flags) || (flags & ~15u) ||
        !reader.Get(&state.detector.n) || !reader.Get(&state.detector.samples) ||
        !reader.Get(&state.detector.over) || !reader.Get(&state.detector.under) ||
        !reader.Get(&value.pending_ticks) || reader.Remaining()) return fail();
    state.have_boundary = flags & 1; state.bypass = flags & 2;
    state.pacer_release = flags & 4; state.detector.fast = flags & 8;
    if (!ValueValid(value, error)) return false;
    out = std::move(value);
    return true;
}

bool SessionClock::Validate(const Snapshot& state, std::string& error) const {
    if (!ValueValid(state, error)) return false;
    // The bypass selection is the tick SOURCE's mode (Freerun/Warp); the
    // domain only latches it at its first TickAdvance, which a --load-state
    // restore precedes -- compare the selection, not the latch.
    const bool live_bypass = accept_host_policy_ ? state.domain.bypass : source_.Bypass();
    const bool live_pacer_release = accept_host_policy_ ? state.domain.pacer_release : domain_.PacerRelease();
    if (state.providers != providers_ || state.domain.pin != domain_.RegimePin() ||
        state.domain.bypass != live_bypass || state.domain.pacer_release != live_pacer_release) {
        // Name the differing selection: the owner that must agree is explicit.
        std::string which;
        if (state.providers != providers_) which += " providers(saved=" + state.providers + ",live=" + providers_ + ")";
        if (state.domain.pin != domain_.RegimePin())
            which += " pin(saved=" + std::to_string(state.domain.pin) + ",live=" + std::to_string(domain_.RegimePin()) + ")";
        if (state.domain.bypass != live_bypass)
            which += " bypass(saved=" + std::to_string(state.domain.bypass) + ",live=" + std::to_string(live_bypass) + ")";
        if (state.domain.pacer_release != domain_.PacerRelease())
            which += " pacer_release(saved=" + std::to_string(state.domain.pacer_release) + ",live=" +
                     std::to_string(domain_.PacerRelease()) + ")";
        error = "Saved clock/provider selection differs from the running session:" + which;
        return false;
    }
    return true;
}

bool SessionClock::Restore(const Snapshot& state, std::string& error) {
    if (!Validate(state, error)) return false;
    if (!ticks_.RestorePending(state.pending_ticks)) {
        error = "Clock restore requires stopped tick execution.";
        return false;
    }
    const bool live_bypass = source_.Bypass(), live_pacer_release = domain_.PacerRelease();
    if (!domain_.Restore(state.domain, state.raw_tick)) return false;
    if (accept_host_policy_) {
        domain_.SetBypass(live_bypass);
        domain_.SetPacerRelease(live_pacer_release);
    }
    source_.ResetForRestore(state.raw_tick);
    restored_ = true;
    return true;
}

bool SessionClock::ValidateImage(const savestate::FileMeta& meta, const std::vector<savestate::Blob>& blobs,
                                 std::string& error) const {
    return ValidateBootImage(meta, blobs, false, error);
}

bool SessionClock::ValidateBootImage(const savestate::FileMeta& meta, const std::vector<savestate::Blob>& blobs,
                                     bool clockless_import, std::string& error) const {
    const savestate::Blob* found = nullptr;
    for (const auto& blob : blobs) if (blob.tag == kTag) {
        if (found) { error = "Duplicate logical clock section."; return false; }
        found = &blob;
    }
    if (!found) {
        if (clockless_import) return true;
        error = "Restore needs owned logical clock state.";
        return false;
    }
    Snapshot value;
    if (!Decode(*found, value, error) || !Validate(value, error)) return false;
    if (value.domain.boundaries != meta.vsync_frame) {
        error = "Reproduction frame does not match its saved logical clock.";
        return false;
    }
    return true;
}

savestate::SectionSpec SessionClock::Section() {
    savestate::SectionSpec spec;
    spec.tag = kTag; spec.version = kVersion; spec.owner = "SessionClock";
    spec.required = false; spec.capture_required = true;
    spec.serialize = [this](std::vector<uint8_t>& out) {
        std::string error;
        if (!Encode(Capture(), out, error)) throw std::runtime_error(error);
    };
    spec.validate = [this](const savestate::Blob& blob, std::string* error) {
        std::string why; Snapshot value;
        const bool ok = Decode(blob, value, why) && Validate(value, why);
        if (error) *error = why;
        return ok;
    };
    spec.restore = [this](const savestate::Blob& blob, std::string* error) {
        std::string why; Snapshot value;
        const bool ok = Decode(blob, value, why) && Restore(value, why);
        if (error) *error = why;
        return ok;
    };
    // Only an explicitly admitted historical boot image may omit GCLK.
    // This clears the host classification, not the logical clock itself.
    spec.absent = [this] { restored_ = false; };
    return spec;
}

} // namespace ps2r::app
