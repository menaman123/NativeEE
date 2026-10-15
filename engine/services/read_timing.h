#pragma once

#include <cstdint>

#include "engine/core/config/config.h"

// The existing deterministic read-deadline/budget arithmetic, separated from
// device callbacks. The file service owns its requests; a remaining comparison
// or audio client can share this position without owning file completions.
class ReadTiming {
public:
    static constexpr double kTickMs = 16667.0 / 1000.0;
    static constexpr uint32_t kContiguousSectors = 16, kShortSeekSectors = 14764;
    static constexpr double kShortSeekMs = 42.0, kLongSeekMs = 112.0, kSectorsPerMs = 1.08;
    struct Position {
        double busy_ms = 0, last_end_ms = 0;
        uint32_t head_lba = 0;
    };
    struct Budget {
        uint64_t bytes = 0, epoch = 0, remaining = 0;
        bool primed = false;
    };
    struct Admission { bool allowed = false, refilled = false; };

    explicit ReadTiming(const ps2r::CdvdConfig& cfg);
    uint64_t DueTick(uint64_t tick, uint32_t lba, uint32_t sectors);
    // Same algorithm with the service owner's accepted logical instant.
    // DueTick retains the existing comparison arm's frame-start input.
    uint64_t DueAtMs(uint64_t tick, double now_ms, uint32_t lba, uint32_t sectors);
    double ChargeOtherRead(double now_ms, uint32_t lba, uint32_t sectors);
    // A read a service consumes without a delivery (a music buffer refill):
    // DueAtMs's charge under the same visibility/model rules, no deadline.
    // Returns the drive's end (ms), 0 when this configuration charges nothing.
    double ChargeServiceRead(double now_ms, uint32_t lba, uint32_t sectors);
    static Admission Admit(Budget& budget, uint64_t epoch, uint64_t bytes);
    static double FrameEndMs(uint64_t tick) { return static_cast<double>(tick + 1) * kTickMs; }
    static bool Validate(const Position& position);
    static bool Validate(const Budget& budget);
    const Position& Capture() const { return position_; }
    bool Restore(const Position& position);
    bool PollVisibility() const { return visibility_ == ps2r::CdvdDeliveryVisibility::Poll; }
    ps2r::CdvdLatencyModel Mode() const { return mode_; }
    ps2r::CdvdDeliveryVisibility Visibility() const { return visibility_; }
    double Scale() const { return scale_; }
    double PollScale() const { return poll_scale_; }
    uint64_t SeeksContiguous() const { return contiguous_; }
    uint64_t SeeksShort() const { return short_; }
    uint64_t SeeksLong() const { return long_; }

private:
    double Charge(double now_ms, uint32_t lba, uint32_t sectors, double scale);
    ps2r::CdvdLatencyModel mode_;
    ps2r::CdvdDeliveryVisibility visibility_;
    double scale_, poll_scale_;
    Position position_;
    uint64_t contiguous_ = 0, short_ = 0, long_ = 0;
};
