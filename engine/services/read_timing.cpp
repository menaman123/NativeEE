#include "engine/services/read_timing.h"

#include <algorithm>
#include <cmath>

ReadTiming::ReadTiming(const ps2r::CdvdConfig& cfg)
    : mode_(cfg.latency_model), visibility_(cfg.delivery_visibility),
      scale_(cfg.latency_scale), poll_scale_(cfg.poll_latency_scale) {}

uint64_t ReadTiming::DueTick(uint64_t tick, uint32_t lba, uint32_t sectors) {
    return DueAtMs(tick, static_cast<double>(tick) * kTickMs, lba, sectors);
}
uint64_t ReadTiming::DueAtMs(uint64_t tick, double now_ms, uint32_t lba, uint32_t sectors) {
    const uint64_t next = tick + 1;
    position_.last_end_ms = 0;
    if (PollVisibility()) {
        position_.last_end_ms = Charge(now_ms, lba, sectors,
                                       poll_scale_ > 0 ? poll_scale_ : 1.0);
        return std::max(next, static_cast<uint64_t>(std::ceil(position_.last_end_ms / kTickMs)));
    }
    if (mode_ != ps2r::CdvdLatencyModel::Modelled || scale_ <= 0) return next;
    const double end = Charge(now_ms, lba, sectors, scale_);
    return std::max(next, static_cast<uint64_t>(std::ceil(end / kTickMs)));
}
double ReadTiming::ChargeServiceRead(double now_ms, uint32_t lba, uint32_t sectors) {
    if (PollVisibility()) return Charge(now_ms, lba, sectors, poll_scale_ > 0 ? poll_scale_ : 1.0);
    if (mode_ != ps2r::CdvdLatencyModel::Modelled || scale_ <= 0) return 0;
    return Charge(now_ms, lba, sectors, scale_);
}
double ReadTiming::ChargeOtherRead(double now_ms, uint32_t lba, uint32_t sectors) {
    return PollVisibility() ? Charge(now_ms, lba, sectors, poll_scale_ > 0 ? poll_scale_ : 1.0) : now_ms;
}
ReadTiming::Admission ReadTiming::Admit(Budget& budget, uint64_t epoch, uint64_t bytes) {
    if (budget.bytes == 0) return {true, false};
    bool refilled = false;
    if (!budget.primed || epoch > budget.epoch) {
        budget.epoch = epoch; budget.remaining = budget.bytes;
        budget.primed = true; refilled = true;
    }
    if (budget.remaining == 0) return {false, refilled};
    budget.remaining = budget.remaining > bytes ? budget.remaining - bytes : 0;
    return {true, refilled};
}
bool ReadTiming::Validate(const Position& position) {
    return std::isfinite(position.busy_ms) && std::isfinite(position.last_end_ms) &&
           position.busy_ms >= 0 && position.last_end_ms >= 0 &&
           position.last_end_ms <= position.busy_ms && position.busy_ms <= 1e15;
}
bool ReadTiming::Validate(const Budget& budget) {
    return budget.remaining <= budget.bytes &&
           (budget.primed || (budget.remaining == 0 && budget.epoch == 0));
}
bool ReadTiming::Restore(const Position& position) {
    if (!Validate(position)) return false;
    position_ = position;
    return true;
}
double ReadTiming::Charge(double now_ms, uint32_t lba, uint32_t sectors, double scale) {
    const auto delta = lba >= position_.head_lba ? lba - position_.head_lba : position_.head_lba - lba;
    const double seek = delta <= kContiguousSectors ? 0.0 : delta < kShortSeekSectors ? kShortSeekMs : kLongSeekMs;
    if (delta <= kContiguousSectors) ++contiguous_; else if (delta < kShortSeekSectors) ++short_; else ++long_;
    position_.busy_ms = std::max(position_.busy_ms, now_ms) +
                        (seek + static_cast<double>(sectors) / kSectorsPerMs) * scale;
    position_.head_lba = lba + sectors;
    return position_.busy_ms;
}
