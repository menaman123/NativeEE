// DeliveryMeter: completion visibility at boundary slot 4 in tick order.
//   * Instant: a read completed during tick k is not visible at boundary k and is
//     delivered at boundary k+1; several reads keep schedule (FIFO) order; one
//     DeliverDue call drains everything due at or before its tick (boundary-level);
//   * Modelled, scale 1: contiguous 16 sectors -> the next boundary (14.8 ms < a
//     tick); a long seek (112 ms + transfer) -> 8 ticks later; reads serialize on
//     the one drive (the second waits for the first); scale 0 -> instant;
//   * a delivery scheduled by a running delivery (re-entry) is queued, not run.
#include <cstdio>
#include <vector>

#include "engine/cdvd/delivery_meter.h"

static int g_fail = 0;
#define CHECK(cond, name) \
    do { if (cond) std::printf("PASS %s\n", name); else { std::printf("FAIL %s\n", name); g_fail++; } } while (0)

int main() {
    ps2r::CdvdConfig instant;
    instant.delivery_visibility = ps2r::CdvdDeliveryVisibility::Boundary;   // this case tests the boundary model (the default is poll since gate S4)
    instant.latency_model = ps2r::CdvdLatencyModel::Instant;
    DeliveryMeter m(instant);
    std::vector<int> order;
    CHECK(m.Schedule(5, 1000, 16, [&] { order.push_back(1); }) == 6, "instant_due_is_next_boundary");
    m.Schedule(5, 5000, 4, [&] { order.push_back(2); });
    CHECK(m.DeliverDue(5) == 0 && order.empty() && m.Pending() == 2, "not_visible_at_the_issue_boundary");
    CHECK(m.DeliverDue(6) == 2 && order.size() == 2 && order[0] == 1 && order[1] == 2 && m.Pending() == 0,
          "delivered_at_the_next_boundary_in_schedule_order");
    m.Schedule(7, 1, 1, [&] { order.push_back(3); });
    m.Schedule(8, 1, 1, [&] { order.push_back(4); });
    CHECK(m.DeliverDue(20) == 2 && order.size() == 4 && order[2] == 3 && order[3] == 4, "boundary_level_drain_of_every_due_entry");
    CHECK(m.Delivered() == 4, "delivered_census");
    // Re-entry: a delivery that schedules another does not run it inside the same drain.
    int nested = 0;
    m.Schedule(30, 1, 1, [&] { m.Schedule(31, 1, 1, [&] { nested = 2; }); nested = 1; });
    CHECK(m.DeliverDue(31) == 1 && nested == 1 && m.Pending() == 1, "reentrant_schedule_is_queued_not_run");
    CHECK(m.DeliverDue(32) == 1 && nested == 2, "queued_reentrant_delivery_runs_next");

    ps2r::CdvdConfig modelled;
    modelled.delivery_visibility = ps2r::CdvdDeliveryVisibility::Boundary;   // this case tests the boundary model (the default is poll since gate S4)
    modelled.latency_model = ps2r::CdvdLatencyModel::Modelled;
    modelled.latency_scale = 1.0;
    DeliveryMeter d(modelled);
    // Head at 0: 16 contiguous sectors at lba 0..16 -> seek 0, transfer 14.8 ms -> next boundary.
    CHECK(d.DueTick(10, 0, 16) == 11, "modelled_contiguous_read_next_boundary");
    // Long seek from head 16 to lba 100000: 112 ms + 16/1.08 ms = 126.8 ms from the drive's busy time
    // (11 ticks * 16.667 = 183.3 ms; busy was 181.5 ms at the last read) -> (183.3+126.8)/16.667 = 18.6 -> tick 19.
    const uint64_t long_due = d.DueTick(11, 100000, 16);
    CHECK(long_due == 19, "modelled_long_seek_eight_ticks_later");
    std::printf("  long seek due tick = %llu\n", static_cast<unsigned long long>(long_due));
    // A second read issued the same tick serializes behind the first: +14.8 ms on the busy drive -> tick 20.
    CHECK(d.DueTick(11, 100016, 16) == 20, "modelled_reads_serialize_on_the_drive");
    // Short seek (< 14764 sectors): 42 ms + transfer.
    const uint64_t short_due = d.DueTick(30, 100032 + 1000, 8);
    CHECK(short_due == 33, "modelled_short_seek_three_ticks");   // 500 ms + 49.4 ms = 549.4 ms -> 32.96 -> 33
    ps2r::CdvdConfig off;
    off.delivery_visibility = ps2r::CdvdDeliveryVisibility::Boundary;   // this case tests the boundary model (the default is poll since gate S4)
    off.latency_model = ps2r::CdvdLatencyModel::Modelled;
    off.latency_scale = 0.0;
    DeliveryMeter o(off);
    CHECK(o.DueTick(3, 500000, 64) == 4 && o.Model() == ps2r::CdvdLatencyModel::Modelled && o.Scale() == 0.0,
          "modelled_scale_0_is_instant");

    // Poll visibility (cdvd.delivery_visibility=poll, DP-17 (a)): the drive's
    // timeline decides WHEN inside the frame a read is done; CompleteNow(tick)
    // delivers the reads that end before the tick's frame edge.
    {
        ps2r::CdvdConfig poll;
        poll.latency_model = ps2r::CdvdLatencyModel::Instant;   // the deterministic profile's Boundary rule
        poll.latency_scale = 0.0;                                // "no drive time" -> the unscaled model in Poll
        poll.delivery_visibility = ps2r::CdvdDeliveryVisibility::Poll;
        DeliveryMeter p(poll);
        int got = 0;
        // Head at 0, a contiguous 1-sector (2 KB) read issued at tick 10: start 166.67 ms,
        // transfer 0.926 ms -> ends 167.6 ms < tick 11's edge (183.3 ms): complete within tick 10.
        const uint64_t due_contig = p.Schedule(10, 0, 1, [&] { got = 1; });
        CHECK(due_contig == 11, "poll_contiguous_read_due_next_boundary_if_unpolled");
        CHECK(p.CompleteNow(9) == 0 && got == 0, "poll_an_earlier_tick_does_not_see_it");
        CHECK(p.CompleteNow(10) == 1 && got == 1 && p.Pending() == 0 && p.PollDeliveries() == 1 && p.BoundaryDeliveries() == 0,
              "poll_2kb_contiguous_read_completes_within_the_issue_tick");
        std::printf("  poll: 2 KB contiguous read issued at tick 10 ends at %.3f ms (frame edge %.3f ms)\n", p.BusyUntilMs(),
                    11 * DeliveryMeter::kTickMs);
        // A long seek (head 1 -> lba 100000: 112 ms + 0.926 ms) issued at tick 10 ends at
        // 183.3 + ... no: the drive is free at 167.6 ms but the read starts at tick 10's instant
        // max(167.6, 166.67) = 167.6 -> ends 280.5 ms -> tick 16.83 -> due boundary 17 in BOTH modes.
        got = 0;
        const uint64_t due_seek = p.Schedule(10, 100000, 1, [&] { got = 2; });
        ps2r::CdvdConfig modelled;
        modelled.delivery_visibility = ps2r::CdvdDeliveryVisibility::Boundary;   // this case tests the boundary model (the default is poll since gate S4)
        modelled.latency_model = ps2r::CdvdLatencyModel::Modelled;
        modelled.latency_scale = 1.0;
        DeliveryMeter b(modelled);
        b.DueTick(10, 0, 1);   // the same first read on the Boundary drive
        const uint64_t due_seek_boundary = b.DueTick(10, 100000, 1);
        std::printf("  100 ms-class seek read issued at tick 10: due %llu (poll) / %llu (boundary), drive end %.3f ms\n",
                    static_cast<unsigned long long>(due_seek), static_cast<unsigned long long>(due_seek_boundary), p.BusyUntilMs());
        CHECK(due_seek == due_seek_boundary && due_seek == 17, "poll_long_seek_read_due_at_the_same_boundary_as_boundary_mode");
        CHECK(p.CompleteNow(10) == 0 && p.CompleteNow(15) == 0 && got == 0, "poll_a_read_ending_in_tick_16_is_not_done_before_it");
        CHECK(p.CompleteNow(16) == 1 && got == 2, "poll_the_read_lands_at_the_first_poll_of_tick_16");
        // Unpolled, the boundary delivers it (slot 4) -- the same drive end, the latest observer.
        got = 0;
        p.Schedule(20, 100001, 1, [&] { got = 3; });
        CHECK(p.DeliverDue(21) == 1 && got == 3 && p.BoundaryDeliveries() == 1, "poll_unpolled_read_delivered_by_the_boundary");
        // Boundary visibility never completes within the issue tick, whatever the model.
        DeliveryMeter bi(modelled);
        got = 0;
        bi.Schedule(10, 0, 1, [&] { got = 4; });
        CHECK(bi.CompleteNow(10) == 0 && bi.CompleteNow(10000) == 0 && got == 0 && bi.Pending() == 1,
              "boundary_mode_complete_now_is_inert");
        CHECK(bi.DeliverDue(11) == 1 && got == 4 && bi.PollDeliveries() == 0, "boundary_mode_delivers_at_slot_4_only");
    }

    if (g_fail == 0) std::printf("ALL PASS\n");
    else std::printf("FAILURES: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
