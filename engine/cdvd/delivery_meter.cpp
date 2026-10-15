// DeliveryMeter implementation; the model is described in the header.
#include "engine/cdvd/delivery_meter.h"

#include <cmath>
#include <utility>

double DeliveryMeter::ChargeReadAtMs(double now_ms, uint32_t lba, uint32_t sectors) {
    return timing_->ChargeOtherRead(now_ms, lba, sectors);
}

uint64_t DeliveryMeter::DueTick(uint64_t issue_tick, uint32_t lba, uint32_t sectors) {
    return timing_->DueTick(issue_tick, lba, sectors);
}

uint64_t DeliveryMeter::Schedule(uint64_t issue_tick, uint32_t lba, uint32_t sectors, Deliver deliver) {
    const uint64_t due = DueTick(issue_tick, lba, sectors);
    queue_.push_back(Entry{due, LastEndMs(), ++last_id_, std::move(deliver)});
    return due;
}

uint64_t DeliveryMeter::Reschedule(uint64_t due, Deliver deliver) {
    const uint64_t next = due + 1;
    queue_.push_back(Entry{next, 0.0, 0, std::move(deliver)});
    return next;
}

bool DeliveryMeter::Settle(uint64_t id) {
    if (id == 0) return false;
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if (it->id == id) {
            queue_.erase(it);
            return true;
        }
    }
    return false;
}

uint32_t DeliveryMeter::CompleteBefore(uint64_t id) {
    if (Visibility() != ps2r::CdvdDeliveryVisibility::Poll || id == 0) return 0;
    bool queued = false;
    for (const Entry& e : queue_) if (e.id == id) { queued = true; break; }
    if (!queued) return 0;
    // The waited read's predecessors in schedule order: the serialized drive
    // finished each of them at or before the waited read's end.
    uint32_t n = 0;
    while (!queue_.empty() && queue_.front().id != id) {
        Deliver d = std::move(queue_.front().deliver);
        queue_.pop_front();
        if (d) d();
        ++n;
        ++delivered_;
        ++poll_delivered_;
    }
    return n;
}

uint32_t DeliveryMeter::DeliverDue(uint64_t tick) {
    // Schedule order is due order for Instant; for Modelled the serialized
    // drive makes dues non-decreasing too, so the head is always the earliest.
    uint32_t n = 0;
    while (!queue_.empty() && queue_.front().due_tick <= tick) {
        Deliver d = std::move(queue_.front().deliver);
        queue_.pop_front();
        if (d) d();
        ++n;
        ++delivered_;
    }
    return n;
}

uint32_t DeliveryMeter::CompleteNow(uint64_t tick) {
    if (Visibility() != ps2r::CdvdDeliveryVisibility::Poll) return 0;
    // The frame of `tick` ends at the next tick edge; a read whose drive end lies
    // before it is done when the poll asks. The serialized drive keeps ends
    // non-decreasing, so the head is the earliest here too.
    const double frame_end_ms = FrameEndMs(tick);
    uint32_t n = 0;
    while (!queue_.empty() && queue_.front().end_ms < frame_end_ms) {
        Deliver d = std::move(queue_.front().deliver);
        queue_.pop_front();
        if (d) d();
        ++n;
        ++delivered_;
        ++poll_delivered_;
    }
    return n;
}
