// DeliveryBudget (the original's CdvdBudgetMeter accounting, nine cases ported)
// and LoaderModel (the completion-visibility model as the DeliveryMeter's first
// producer), plus the boundary slot-4 duty order: CD delivery runs BEFORE the RPC
// end-callbacks of the same boundary.
//   Budget: refill on a strictly increasing epoch; exhaustion is stable within an
//   epoch; no carryover; an oversized read is admitted by a fresh epoch (liveness);
//   a transient epoch dip never refills; budget 0 is unmetered; KB -> bytes parse;
//   delivery-fired ticks do not advance the epoch; the FIFO pass rule.
//   Loader: enq < dlv <= rel < cns per read in the trace; the poll before the
//   boundary is HeldInflight, after it Progress; the spin rule yields only at the
//   spin site with a fresh in-flight note, declines otherwise; a budget-denied read
//   is deferred to the next boundary (dfr) and still delivered in order.
//   Poll visibility's blocking wait: delivered at the drive's end, the meter entry
//   settled; a drive end beyond the frame's edge parks whole frames (ParkFrame) --
//   no overrun, one prk line per frame; without a park (or one that cannot advance
//   time) the delivery is an overrun; boundary mode never parks.
#include <cstdio>
#include <string>
#include <vector>

#include "engine/cdvd/delivery_budget.h"
#include "engine/cdvd/delivery_meter.h"
#include "engine/cdvd/hle/loader_model.h"
#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/timing/boundary_sequencer.h"
#include "engine/pad/hle/pad_protocol.h"
#include "engine/pad/hle/pad_stamp_duty.h"
#include "engine/sif/hle/rpc_router.h"

static int g_fail = 0;
#define CHECK(cond, name) \
    do { if (cond) std::printf("PASS %s\n", name); else { std::printf("FAIL %s\n", name); g_fail++; } } while (0)

static constexpr uint64_t KB = 1024;

static void TestBudget() {
    {   // T1 refill
        DeliveryBudget m(100 * KB);
        auto r = m.Charge(0, 10 * KB);
        CHECK(r.allowed && r.refilled, "t1_first_charge_admits_and_refills");
        r = m.Charge(0, 90 * KB);
        CHECK(r.allowed && !r.refilled, "t1_same_epoch_admits_no_refill");
        CHECK(!m.Charge(0, 1).allowed, "t1_exhausted_within_epoch_denied");
        r = m.Charge(1, 1);
        CHECK(r.allowed && r.refilled, "t1_next_epoch_refills_and_admits");
    }
    {   // T2 exhaust / not-ready transitions
        DeliveryBudget m(64 * KB);
        CHECK(m.Charge(7, 64 * KB).allowed, "t2_fresh_epoch_delivers");
        CHECK(!m.Charge(7, 1).allowed && !m.Charge(7, 1).allowed && !m.Charge(7, 1).allowed, "t2_not_ready_is_stable");
        auto r = m.Charge(8, 1);
        CHECK(r.allowed && r.refilled, "t2_refill_flips_back_to_ready");
    }
    {   // T3 no carryover
        DeliveryBudget m(100 * KB);
        m.Charge(0, 10 * KB);
        m.Charge(1, 0);
        CHECK(m.Remaining() == 100 * KB, "t3_unused_allowance_does_not_accumulate");
        m.Charge(1, 150 * KB);
        CHECK(m.Remaining() == 0, "t3_overshoot_saturates_to_0");
        m.Charge(2, 0);
        CHECK(m.Remaining() == 100 * KB, "t3_no_carried_debt");
    }
    {   // T4 oversize liveness
        DeliveryBudget m(64 * KB);
        auto r = m.Charge(3, 330 * KB);
        CHECK(r.allowed && m.Remaining() == 0, "t4_fresh_epoch_admits_an_oversized_head");
        CHECK(!m.Charge(3, 1).allowed, "t4_then_denies_until_the_next_epoch");
        CHECK(m.Charge(4, 330 * KB).allowed, "t4_next_epoch_admits_again");
    }
    {   // T5 monotonic guard (a transient lower epoch read)
        DeliveryBudget m(64 * KB);
        m.Charge(10, 64 * KB);
        auto r = m.Charge(9, 1);
        CHECK(!r.allowed && !r.refilled, "t5_epoch_dip_does_not_refill");
        r = m.Charge(10, 1);
        CHECK(!r.allowed && !r.refilled, "t5_same_epoch_still_exhausted");
        CHECK(m.Charge(11, 1).refilled, "t5_higher_epoch_refills");
    }
    {   // T6 unmetered
        DeliveryBudget m(0);
        bool ok = true;
        for (int i = 0; i < 50; i++) { auto r = m.Charge(0, 1000 * KB); ok &= r.allowed && !r.refilled; }
        CHECK(ok && !m.Primed(), "t6_budget_0_is_unmetered_never_refills");
    }
    {   // T7 the KB value
        CHECK(DeliveryBudget::BytesFromKb(256) == 256 * KB && DeliveryBudget::BytesFromKb(0) == 0 &&
                  DeliveryBudget::BytesFromKb(-5) == 0 && DeliveryBudget::kDefaultBudgetBytes == 256 * KB,
              "t7_kb_to_bytes_0_or_negative_unmetered_default_256");
    }
    {   // T8 the refill epoch excludes delivery-fired ticks
        CHECK(DeliveryBudget::EpochOf(10, 0) == 10 && DeliveryBudget::EpochOf(10, 4) == 6 && DeliveryBudget::EpochOf(3, 5) == 0,
              "t8_epoch_is_ticks_minus_delivery_fired");
        DeliveryBudget m(64 * KB);
        uint64_t ticks = 5, fired = 0;
        CHECK(m.Charge(DeliveryBudget::EpochOf(ticks, fired), 64 * KB).allowed, "t8_delivering_pass_admits");
        ++ticks; ++fired;   // the delivery fired a tick: the epoch must not move
        CHECK(!m.Charge(DeliveryBudget::EpochOf(ticks, fired), 1).allowed, "t8_delivery_fired_tick_does_not_refill");
        ++ticks;            // a consumed/vsync tick: refill
        CHECK(m.Charge(DeliveryBudget::EpochOf(ticks, fired), 1).refilled, "t8_real_tick_refills");
    }
    {   // T9 the FIFO pass rule: a denied head blocks the rest of the pass, order kept
        DeliveryBudget m(64 * KB);
        std::vector<uint64_t> queue = {40 * KB, 40 * KB, 8 * KB};
        std::vector<int> delivered;
        uint64_t epoch = 0;
        auto pass = [&] {
            while (!queue.empty()) {
                if (!m.Charge(epoch, queue.front()).allowed) break;
                delivered.push_back(static_cast<int>(queue.front() / KB));
                queue.erase(queue.begin());
            }
        };
        pass();
        CHECK(delivered.size() == 2 && queue.size() == 1, "t9_first_pass_delivers_head_two_within_budget");
        pass();
        CHECK(delivered.size() == 2, "t9_same_epoch_pass_delivers_nothing_more");
        ++epoch;
        pass();
        CHECK(delivered.size() == 3 && delivered[2] == 8 && queue.empty(), "t9_next_epoch_drains_in_fifo_order");
    }
}

static void TestLoaderModel() {
    ps2r::CdvdConfig instant;
    instant.delivery_visibility = ps2r::CdvdDeliveryVisibility::Boundary;   // this case tests the boundary model (the default is poll since gate S4)
    instant.latency_model = ps2r::CdvdLatencyModel::Instant;
    DeliveryMeter meter(instant);
    DeliveryBudget budget(64 * KB);
    LoaderModel model(meter, budget);
    std::vector<std::string> trace;
    model.SetTraceSink([&](const std::string& l) { trace.push_back(l); });
    const uint32_t kSpinSite = 0x17d724;   // a title parameter here: the engine takes it as an argument

    int applied = 0;
    CHECK(model.Enqueue(0x1000, 5000, 16, /*issue*/ 10, [&] { applied = 1; }) == 11, "enq_schedules_on_the_meter_next_boundary");
    CHECK(meter.Pending() == 1 && model.InFlight(0x1000) && applied == 0, "read_in_flight_until_the_boundary");
    CHECK(model.Poll(0x1000) == LoaderModel::PollOutcome::HeldInflight, "poll_before_the_boundary_is_held_inflight");
    CHECK(model.ShouldYieldPollSlice(0x1234, kSpinSite, true) == false && model.Declines() == 0, "other_ra_is_not_a_decision");
    CHECK(model.ShouldYieldPollSlice(kSpinSite, kSpinSite, false) == false && model.Declines() == 1, "s0_zero_declines");
    CHECK(model.ShouldYieldPollSlice(kSpinSite, kSpinSite, true) == true && model.Yields() == 1, "spin_site_fresh_inflight_note_yields");
    CHECK(model.ShouldYieldPollSlice(kSpinSite, kSpinSite, true) == false && model.Declines() == 2, "note_consumed_second_query_declines");
    meter.DeliverDue(10);
    CHECK(applied == 0 && model.InFlight(0x1000), "not_visible_at_the_issue_boundary");
    meter.DeliverDue(11);
    CHECK(applied == 1 && model.Released(0x1000) && model.Delivered() == 1, "delivered_and_released_at_boundary_11");
    model.Poll(0x1000);   // note HeldInflight from before was consumed; re-poll finds it released
    CHECK(model.Consumed() == 1 && !model.InFlight(0x1000) && !model.Released(0x1000), "poll_after_the_boundary_consumes");
    CHECK(model.ShouldYieldPollSlice(kSpinSite, kSpinSite, true) == false, "progress_note_declines");
    CHECK(trace.size() == 5 && trace[0].find(" enq fs=0x1000 src=ee") != std::string::npos &&
              trace[1].find(" yld fs=0x1000 blk=inflt") != std::string::npos && trace[2].find(" dlv fs=0x1000") != std::string::npos &&
              trace[3].find(" rel fs=0x1000 arm=B") != std::string::npos && trace[4].find(" cns fs=0x1000") != std::string::npos,
          "trace_enq_yld_dlv_rel_cns_in_order");
    CHECK(trace[0].rfind("1 ", 0) == 0 && trace[4].rfind("5 ", 0) == 0, "trace_seq_numbers");

    // Budget: a 64 KB read exhausts the epoch, so the 40 KB read behind it in
    // the same boundary is deferred to the next (the FIFO head is always admitted).
    trace.clear();
    int a = 0, b = 0;
    model.Enqueue(0x2000, 100, 32, 20, [&] { a = 1; });
    model.Enqueue(0x3000, 200, 20, 20, [&] { b = 1; });
    meter.DeliverDue(21);
    CHECK(a == 1 && b == 0 && model.Deferred() == 1 && model.InFlight(0x3000) && meter.Pending() == 1, "budget_denied_read_is_deferred_still_inflight");
    CHECK(model.Poll(0x3000) == LoaderModel::PollOutcome::HeldInflight && model.ShouldYieldPollSlice(kSpinSite, kSpinSite, true),
          "deferred_read_still_yields_the_spin");
    meter.DeliverDue(22);
    CHECK(b == 1 && model.Released(0x3000), "deferred_read_delivered_at_the_next_boundary");
    bool dfr_seen = false;
    for (const std::string& l : trace) dfr_seen |= l.find(" dfr fs=0x3000 blk=budget") != std::string::npos;
    CHECK(dfr_seen, "trace_dfr_line");
    CHECK(model.Poll(0x9999) == LoaderModel::PollOutcome::Progress, "poll_of_an_unknown_object_is_progress");

    // One read in flight per fs: a second Enqueue on an in-flight object is
    // refused (0), counted, traced `rej`, and the first read's delivery stands.
    trace.clear();
    int c1 = 0, c2 = 0;
    CHECK(model.Enqueue(0x5000, 400, 2, 30, [&] { c1 = 1; }) == 31, "guard_first_enqueue_accepted");
    CHECK(model.Enqueue(0x5000, 900, 2, 30, [&] { c2 = 1; }) == 0 && model.Rejected() == 1 && meter.Pending() == 1,
          "guard_second_enqueue_on_inflight_fs_refused");
    meter.DeliverDue(31);
    CHECK(c1 == 1 && c2 == 0 && model.Released(0x5000), "guard_first_read_delivered_second_never_ran");
    CHECK(!trace.empty() && trace[1].find(" rej fs=0x5000 blk=inflt") != std::string::npos, "guard_trace_rej_line");
    CHECK(model.Poll(0x5000) == LoaderModel::PollOutcome::Progress && model.Enqueue(0x5000, 900, 2, 31, [&] { c2 = 1; }) == 32,
          "guard_lifts_after_consume");
}

// Under the Modelled latency arm a budget-denied read comes back at exactly
// due + 1: the re-schedule does not re-run the seek/transfer model (the drive's
// work is done), so the head position and busy time the model keeps are those
// of the first schedule -- the read that follows sees no phantom seek.
static void TestModelledDeferral() {
    ps2r::CdvdConfig modelled;
    modelled.delivery_visibility = ps2r::CdvdDeliveryVisibility::Boundary;   // this case tests the boundary model (the default is poll since gate S4)
    modelled.latency_model = ps2r::CdvdLatencyModel::Modelled;
    modelled.latency_scale = 1.0;
    DeliveryMeter meter(modelled);
    DeliveryBudget budget(64 * KB);
    LoaderModel model(meter, budget);
    int a = 0, b = 0;
    // 32 sectors at lba 100 from a cold head: a short seek + transfer -> due 5 (71.6 ms).
    const uint64_t due_a = model.Enqueue(0x100, 100, 32, 0, [&] { a = 1; });
    // 1 sector contiguous with A (the head is at 132 after A): no seek, +0.93 ms -> the same boundary.
    const uint64_t due_b = model.Enqueue(0x200, 132, 1, 0, [&] { b = 1; });
    CHECK(due_a == 5 && due_b == 5, "modelled_dues_follow_the_drive");
    // A probe of the model with the head where the two reads left it.
    DeliveryMeter probe(modelled);
    probe.DueTick(0, 100, 32);
    probe.DueTick(0, 132, 1);
    const uint64_t expected_next = probe.DueTick(due_b + 1, 133, 4);   // contiguous: transfer only
    for (uint64_t t = 1; t <= due_b; t++) meter.DeliverDue(t);
    CHECK(a == 1 && b == 0 && model.Delivered() == 1, "modelled_a_delivered_at_its_due_b_denied");
    // A's 64 KB exhausted the boundary's budget: B is deferred to due + 1, still in flight.
    CHECK(model.Deferred() == 1 && model.InFlight(0x200) && meter.Pending() == 1, "modelled_b_denied_by_budget_still_inflight");
    meter.DeliverDue(due_b + 1);
    CHECK(b == 1 && model.Released(0x200) && meter.Pending() == 0, "modelled_deferred_read_lands_at_due_plus_one");
    // The re-schedule left the model untouched: a following contiguous read gets
    // the same due the probe (which never re-scheduled) computes.
    CHECK(meter.DueTick(due_b + 1, 133, 4) == expected_next, "modelled_reschedule_does_not_move_the_head_or_busy_time");
}

// Slot 4 as the composition root wires it: the meter's DeliverDue, then the
// router's CompletePending, then the pad stamp, on the same boundary -- a read
// delivered at tick k is applied before the RPC end-callback queued for tick k
// runs, and the pad image the game reads after the boundary is stamped last
// (the original's duty order: CD delivery -> RPC end -> pad).
static void TestSlot4Order() {
    ps2r::CdvdConfig instant;
    instant.delivery_visibility = ps2r::CdvdDeliveryVisibility::Boundary;   // this case tests the boundary model (the default is poll since gate S4)
    instant.latency_model = ps2r::CdvdLatencyModel::Instant;
    DeliveryMeter meter(instant);
    DeliveryBudget budget;
    LoaderModel model(meter, budget);
    RpcRouter router;
    std::vector<std::string> order;
    uint64_t tick = 41;
    // The pad: one open area; its first guest write of a boundary marks the stamp.
    bool stamp_marked = false;
    PadProtocol pad([&](uint32_t, uint32_t, int) {
        if (!stamp_marked) order.push_back("pad_stamp");
        stamp_marked = true;
    });
    pad.RegisterArea(0, 0x00300000u, /*now*/ 1);
    PadStampDuty pad_duty(pad, nullptr, nullptr);
    BoundarySequencer seq;
    seq.SetStep(BoundarySequencer::Step::MaintDuties, [&](CpuContext& ctx) {
        meter.DeliverDue(tick);
        router.CompletePending(
            ctx, [&](uint32_t addr, CpuContext&) { order.push_back("rpc_end@" + std::to_string(addr)); },
            [](uint32_t) { return 0x00200000u; });   // end_func / end_param words of the client struct
        pad_duty.Run(tick);
        return true;
    });
    model.Enqueue(0x4000, 300, 4, /*issue*/ 40, [&] { order.push_back("cd_delivery"); });
    router.QueueEndCallback(0x00100000u);
    static CpuContext ctx;
    seq.RunBoundary(ctx);
    CHECK(order.size() == 3 && order[0] == "cd_delivery" && order[1].rfind("rpc_end@", 0) == 0 && order[2] == "pad_stamp",
          "slot4_cd_delivery_then_rpc_end_then_pad_stamp");
    CHECK(router.PendingCount() == 0 && meter.Pending() == 0 && pad.Stamps() == 1 && pad_duty.Runs() == 1, "slot4_all_three_duties_ran");
}

// Poll visibility (DP-17 (a)): a Poll of a read the modelled drive finished inside
// the current frame delivers it AT THE POLL (Progress, payload applied, budget not
// consulted); a read still on the drive is HeldInflight and the boundary delivers it.
static void TestPollVisibility() {
    ps2r::CdvdConfig cfg;
    cfg.latency_model = ps2r::CdvdLatencyModel::Instant;
    cfg.latency_scale = 0.0;
    cfg.delivery_visibility = ps2r::CdvdDeliveryVisibility::Poll;
    DeliveryMeter meter(cfg);
    DeliveryBudget budget(1);   // a 1-byte budget would deny every delivery if consulted
    LoaderModel model(meter, budget);
    uint64_t tick = 10;
    model.SetNowTick([&] { return tick; });
    std::vector<std::string> trace;
    model.SetTraceSink([&](const std::string& l) { trace.push_back(l); });
    int applied = 0;
    CHECK(model.Enqueue(0x1000, 0, 1, tick, [&] { applied = 1; }) == 11, "pv_enqueue_due_next_boundary_if_unpolled");
    CHECK(model.InFlight(0x1000) && applied == 0, "pv_in_flight_until_asked");
    CHECK(model.Poll(0x1000) == LoaderModel::PollOutcome::Progress && applied == 1 && !model.InFlight(0x1000),
          "pv_poll_delivers_and_consumes_the_read_done_inside_the_frame");
    CHECK(model.Delivered() == 1 && model.Consumed() == 1 && model.Deferred() == 0 && meter.PollDeliveries() == 1,
          "pv_budget_not_consulted_for_a_poll_delivery");
    bool dlv_poll = false;
    for (const auto& l : trace) if (l.find("dlv fs=0x1000 src=poll") != std::string::npos) dlv_poll = true;
    CHECK(dlv_poll, "pv_trace_names_the_poll_delivery");
    // A long seek: the drive ends in tick 16 -> HeldInflight at ticks 10..15, delivered at the poll of 16.
    applied = 0;
    const uint64_t due = model.Enqueue(0x2000, 100000, 1, tick, [&] { applied = 2; });
    CHECK(due == 17, "pv_long_seek_due_boundary_17");
    CHECK(model.Poll(0x2000) == LoaderModel::PollOutcome::HeldInflight && applied == 0, "pv_poll_before_the_drive_is_done_is_held");
    CHECK(model.ShouldYieldPollSlice(0x17d724, 0x17d724, true) && model.Yields() == 1, "pv_held_read_still_lifts_the_spin");
    tick = 15;
    CHECK(model.Poll(0x2000) == LoaderModel::PollOutcome::HeldInflight && applied == 0, "pv_tick_15_still_held");
    tick = 16;
    CHECK(model.Poll(0x2000) == LoaderModel::PollOutcome::Progress && applied == 2, "pv_tick_16_poll_delivers");
    // Unpolled: the boundary drain delivers (slot 4) with the same closure.
    applied = 0;
    tick = 20;
    model.Enqueue(0x3000, 100001, 1, tick, [&] { applied = 3; });
    CHECK(meter.DeliverDue(21) == 1 && applied == 3 && model.Released(0x3000), "pv_boundary_delivers_an_unpolled_read");
    CHECK(model.Poll(0x3000) == LoaderModel::PollOutcome::Progress && !model.InFlight(0x3000), "pv_boundary_delivered_read_consumed_by_the_poll");
    // Boundary visibility with a NowTick bound: Poll changes nothing (today's behaviour).
    ps2r::CdvdConfig bcfg;
    bcfg.delivery_visibility = ps2r::CdvdDeliveryVisibility::Boundary;   // this case tests the boundary model (the default is poll since gate S4)
    DeliveryMeter bmeter(bcfg);
    DeliveryBudget bbudget(0);
    LoaderModel bmodel(bmeter, bbudget);
    bmodel.SetNowTick([&] { return tick; });
    applied = 0;
    bmodel.Enqueue(0x4000, 0, 1, tick, [&] { applied = 4; });
    CHECK(bmodel.Poll(0x4000) == LoaderModel::PollOutcome::HeldInflight && applied == 0 && bmeter.PollDeliveries() == 0,
          "pv_boundary_visibility_poll_is_held_inflight_as_today");
    CHECK(bmeter.DeliverDue(21) == 1 && applied == 4, "pv_boundary_visibility_slot_4_delivers");
}

// The blocking wait under poll visibility (DP-17 (a) attempt 2): the wait
// delivers the read at the drive's end (WaitDelivered, not Forced), settles the
// meter entry (no phantom pop at a later poll or boundary), and an end past the
// frame's edge is an overrun. The census is split by the landing site.
static void TestPollWaitSettles() {
    ps2r::CdvdConfig cfg;
    cfg.latency_scale = 0.0;   // the deterministic profile's boundary pin: not the poll drive's scale
    cfg.delivery_visibility = ps2r::CdvdDeliveryVisibility::Poll;
    DeliveryMeter meter(cfg);
    CHECK(meter.PollScale() == 1.0, "pw_poll_scale_is_its_own_key_default_1");
    DeliveryBudget budget(0);
    LoaderModel model(meter, budget);
    uint64_t tick = 10;
    model.SetNowTick([&] { return tick; });
    int applied = 0;
    // Seven contiguous 1-sector reads issued at tick 10, each waited on (the title's
    // issue + wait pattern): ~0.926 ms each, all inside tick 10's frame.
    for (uint32_t i = 0; i < 7; i++) {
        model.Enqueue(0x100 + i, i, 1, tick, [&] { ++applied; });
        CHECK(model.CompleteNow(0x100 + i) && !model.InFlight(0x100 + i), "pw_wait_completes_the_read");
    }
    CHECK(applied == 7 && model.WaitDelivered() == 7 && model.Forced() == 0 && model.WaitOverruns() == 0,
          "pw_seven_waits_delivered_none_forced_none_overrun");
    CHECK(meter.Pending() == 0, "pw_waited_entries_settled_no_phantoms");
    std::printf("  poll wait: 7 contiguous 1-sector reads waited at tick 10 -> drive at %.3f ms (frame edge %.3f ms)\n",
                meter.BusyUntilMs(), DeliveryMeter::FrameEndMs(10));
    // The eighth, polled: it ends within tick 10 and lands at its first Poll.
    model.Enqueue(0x200, 7, 1, tick, [&] { ++applied; });
    CHECK(model.Poll(0x200) == LoaderModel::PollOutcome::Progress && applied == 8 && model.PollDelivered() == 1,
          "pw_eighth_read_polled_completes_within_the_issue_tick");
    CHECK(meter.PollDeliveries() == 1 && meter.BoundaryDeliveries() == 0, "pw_meter_pops_equal_real_deliveries");
    // A long seek waited on: the drive ends in a later frame -> delivered at the wait, counted as an overrun,
    // the drive timeline carried (the next issue starts on the busy drive, not at the frame's instant).
    model.Enqueue(0x300, 100000, 1, tick, [&] { ++applied; });
    const double end_seek = meter.BusyUntilMs();
    CHECK(end_seek >= DeliveryMeter::FrameEndMs(10), "pw_long_seek_ends_past_the_frame_edge");
    CHECK(model.CompleteNow(0x300) && applied == 9 && model.WaitDelivered() == 8 && model.WaitOverruns() == 1 && model.Forced() == 0,
          "pw_overrun_wait_delivers_and_is_counted");
    CHECK(meter.Pending() == 0 && meter.DeliverDue(100) == 0 && meter.CompleteNow(100) == 0 && applied == 9,
          "pw_overrun_entry_settled_nothing_pops_later");
    tick = 11;
    model.Enqueue(0x400, 100001, 1, tick, [&] { ++applied; });
    CHECK(meter.BusyUntilMs() > end_seek && meter.BusyUntilMs() - end_seek < 1.0, "pw_next_issue_starts_on_the_busy_drive");
    CHECK(model.BoundaryDelivered() == 0, "pw_no_boundary_delivery_in_this_scenario");
    // Boundary visibility: the wait is the pull-forward (Forced), the meter entry stays (today's phantom pop, unchanged).
    ps2r::CdvdConfig bcfg;
    bcfg.delivery_visibility = ps2r::CdvdDeliveryVisibility::Boundary;   // this case tests the boundary model (the default is poll since gate S4)
    DeliveryMeter bmeter(bcfg);
    DeliveryBudget bbudget(0);
    LoaderModel bmodel(bmeter, bbudget);
    applied = 0;
    bmodel.Enqueue(0x500, 0, 1, 10, [&] { ++applied; });
    CHECK(bmodel.CompleteNow(0x500) && applied == 1 && bmodel.Forced() == 1 && bmodel.WaitDelivered() == 0 && bmeter.Pending() == 1,
          "pw_boundary_mode_wait_is_forced_as_today");
    CHECK(bmeter.DeliverDue(11) == 1 && applied == 1 && bmodel.BoundaryDelivered() == 0, "pw_boundary_mode_phantom_pop_is_not_a_delivery");
}

// Poll visibility: a blocking wait whose drive end lies beyond the frame's edge
// parks whole frames (ParkFrame advances the tick) until the end is inside the
// frame -- no overrun, the parked frames counted; a park that cannot advance
// time leaves the delivery an overrun; boundary mode never parks.
static void TestPollWaitParks() {
    ps2r::CdvdConfig cfg;
    cfg.latency_scale = 0.0;
    cfg.delivery_visibility = ps2r::CdvdDeliveryVisibility::Poll;
    DeliveryMeter meter(cfg);
    DeliveryBudget budget(0);
    LoaderModel model(meter, budget);
    uint64_t tick = 10;
    uint64_t park_calls = 0;
    model.SetNowTick([&] { return tick; });
    model.SetParkFrame([&] { ++park_calls; ++tick; });
    std::vector<std::string> trace;
    model.SetTraceSink([&](const std::string& l) { trace.push_back(l); });
    int applied = 0;
    // A long seek (112 ms + 1 sector) waited at tick 10: the drive ends at ~279.6 ms,
    // inside tick 16's frame [266.7, 283.3) -> the wait parks 6 frames, then delivers.
    model.Enqueue(0x300, 100000, 1, tick, [&] { ++applied; });
    const double end_seek = meter.BusyUntilMs();
    CHECK(end_seek >= DeliveryMeter::FrameEndMs(10) && end_seek < DeliveryMeter::FrameEndMs(16) && end_seek >= DeliveryMeter::FrameEndMs(15),
          "pk_long_seek_ends_in_frame_16");
    CHECK(trace.size() == 1 && trace[0].find(" enq fs=0x300 src=ee lba=100000 sec=1 end=") != std::string::npos, "pk_enq_trace_names_lba_sec_end");
    CHECK(model.CompleteNow(0x300) && applied == 1 && tick == 16 && park_calls == 6 && model.WaitParks() == 6,
          "pk_wait_parks_six_frames_to_the_drive_end");
    CHECK(model.WaitOverruns() == 0 && model.WaitDelivered() == 1 && model.Forced() == 0 && meter.Pending() == 0,
          "pk_parked_wait_is_no_overrun_nothing_forced_entry_settled");
    size_t prk = 0;
    for (const auto& l : trace) if (l.find(" prk fs=0x300 end=") != std::string::npos) ++prk;
    CHECK(prk == 6, "pk_one_prk_trace_line_per_parked_frame");
    // The synchronous burst: contiguous 1-sector reads issued and waited back to back
    // (~0.926 ms each) -- 18 fit the rest of frame 16 only partly: the drive timeline
    // runs on from the seek's end, and every frame edge crossed inside a wait is a park.
    const uint64_t parks_before = model.WaitParks();
    for (uint32_t i = 0; i < 40; i++) {
        model.Enqueue(0x400 + i, 100001 + i, 1, tick, [&] { ++applied; });
        model.CompleteNow(0x400 + i);
    }
    const double end_burst = meter.BusyUntilMs();
    const uint64_t frames_spanned = static_cast<uint64_t>(end_burst / DeliveryMeter::kTickMs) - 16;
    CHECK(applied == 41 && model.WaitOverruns() == 0 && model.WaitParks() - parks_before == frames_spanned && tick == 16 + frames_spanned,
          "pk_burst_parks_exactly_the_frame_edges_the_drive_crossed");
    std::printf("  poll park: 40 contiguous waited reads from %.3f ms -> drive at %.3f ms, %llu frames parked, tick %llu\n", end_seek, end_burst,
                static_cast<unsigned long long>(model.WaitParks() - parks_before), static_cast<unsigned long long>(tick));
    // A park that cannot advance time (no guest thread / shutdown): one attempt, then the overrun.
    model.SetParkFrame([&] { ++park_calls; });
    const uint64_t calls_before = park_calls;
    model.Enqueue(0x500, 200000, 1, tick, [&] { ++applied; });
    CHECK(model.CompleteNow(0x500) && applied == 42 && park_calls == calls_before + 1 && model.WaitOverruns() == 1,
          "pk_park_that_cannot_advance_time_is_one_attempt_then_overrun");
    // No ParkFrame bound: the overrun as before.
    LoaderModel unbound(meter, budget);
    unbound.SetNowTick([&] { return tick; });
    unbound.Enqueue(0x600, 300000, 1, tick, [&] { ++applied; });
    CHECK(unbound.CompleteNow(0x600) && unbound.WaitParks() == 0 && unbound.WaitOverruns() == 1, "pk_unbound_park_keeps_the_overrun");
    // Boundary mode: the park is never consulted.
    ps2r::CdvdConfig bcfg;
    bcfg.delivery_visibility = ps2r::CdvdDeliveryVisibility::Boundary;   // this case tests the boundary model (the default is poll since gate S4)
    DeliveryMeter bmeter(bcfg);
    LoaderModel bmodel(bmeter, budget);
    uint64_t btick = 10, bparks = 0;
    bmodel.SetNowTick([&] { return btick; });
    bmodel.SetParkFrame([&] { ++bparks; ++btick; });
    bmodel.Enqueue(0x700, 100000, 1, btick, [&] { ++applied; });
    CHECK(bmodel.CompleteNow(0x700) && bparks == 0 && btick == 10 && bmodel.Forced() == 1 && bmodel.WaitParks() == 0,
          "pk_boundary_mode_never_parks");
}

// DP-22's ordering pin: completions are observed in ISSUE ORDER at every
// observation point. One drive serialises the reads, so a read issued earlier
// ends no later; the blocking wait on a later read must make the earlier one
// visible first (the console's cdvdman completes in issue order). With the
// in-order rule absent the earlier read stays "in flight" after the later one
// landed -- the inversion this pin fails on.
static void TestPollOrderAtEveryPoint() {
    ps2r::CdvdConfig cfg;
    cfg.latency_scale = 0.0;
    cfg.delivery_visibility = ps2r::CdvdDeliveryVisibility::Poll;
    DeliveryMeter meter(cfg);
    DeliveryBudget budget(0);
    LoaderModel model(meter, budget);
    uint64_t tick = 10;
    model.SetNowTick([&] { return tick; });
    model.SetParkFrame([&] { ++tick; });
    std::vector<std::string> trace;
    model.SetTraceSink([&](const std::string& l) { trace.push_back(l); });
    std::vector<int> landed;
    // A (first, a long seek: ends past the frame), then B contiguous after it (ends later still).
    model.Enqueue(0xA, 100000, 1, tick, [&] { landed.push_back(0xA); });
    model.Enqueue(0xB, 100001, 1, tick, [&] { landed.push_back(0xB); });
    const double end_a = 0, end_b = meter.BusyUntilMs();
    (void)end_a;
    CHECK(end_b > DeliveryMeter::FrameEndMs(10), "ord_both_end_past_the_issue_frame");
    // The title blocks on B: A must land first, then B; the EE never sees B without A.
    CHECK(model.CompleteNow(0xB), "ord_wait_on_the_later_read_completes");
    CHECK(landed.size() == 2 && landed[0] == 0xA && landed[1] == 0xB, "ord_earlier_read_lands_first_inside_the_wait");
    CHECK(model.Released(0xA) && !model.InFlight(0xA), "ord_earlier_read_visible_when_the_wait_returns");
    CHECK(model.WaitOrderDelivered() == 1 && model.WaitDelivered() == 2 && meter.Pending() == 0, "ord_census_counts_the_predecessor_as_a_wait_delivery");
    bool wo = false;
    for (const auto& l : trace) if (l.find(" dlv fs=0xa src=wait-order end=") != std::string::npos) wo = true;
    CHECK(wo, "ord_trace_names_the_in_order_delivery");
    CHECK(model.Poll(0xA) == LoaderModel::PollOutcome::Progress && model.Consumed() == 2, "ord_next_poll_consumes_the_earlier_read");
    // Slot 4 and the poll agree on order too: three contiguous reads issued at once
    // land head-first at the first poll of the frame that holds their ends.
    std::vector<int> order;
    const uint64_t t0 = tick;
    model.Enqueue(0x1, 100002, 1, t0, [&] { order.push_back(1); });
    model.Enqueue(0x2, 100003, 1, t0, [&] { order.push_back(2); });
    model.Enqueue(0x3, 100004, 1, t0, [&] { order.push_back(3); });
    while (tick < 400 && model.Poll(0x3) == LoaderModel::PollOutcome::HeldInflight) ++tick;
    CHECK(order.size() == 3 && order[0] == 1 && order[1] == 2 && order[2] == 3, "ord_poll_lands_the_queue_head_first");
    CHECK(model.Released(0x1) && model.Released(0x2) && !model.InFlight(0x3), "ord_no_read_skipped_by_a_later_poll");
    // Boundary mode: nothing here changes (CompleteBefore is a no-op).
    ps2r::CdvdConfig bnd;
    bnd.delivery_visibility = ps2r::CdvdDeliveryVisibility::Boundary;   // this case tests the boundary model (the default is poll since gate S4)
    DeliveryMeter bmeter(bnd);
    LoaderModel bmodel(bmeter, budget);
    int b_applied = 0;
    bmodel.Enqueue(0xA, 100000, 1, 10, [&] { ++b_applied; });
    bmodel.Enqueue(0xB, 100001, 1, 10, [&] { ++b_applied; });
    CHECK(bmeter.CompleteBefore(2) == 0 && bmodel.CompleteNow(0xB) && b_applied == 1 && bmodel.InFlight(0xA) && bmodel.Forced() == 1,
          "ord_boundary_mode_unchanged");
}

// The lift iterates while the drive completes reads inside the frame: a poll
// that finds its read landed is Progress (no yield), and ShouldYieldPollSlice
// declines; only a read whose drive end lies past the frame's edge yields.
static void TestLiftIteratesInsideTheFrame() {
    ps2r::CdvdConfig cfg;
    cfg.latency_scale = 0.0;
    cfg.delivery_visibility = ps2r::CdvdDeliveryVisibility::Poll;
    DeliveryMeter meter(cfg);
    DeliveryBudget budget(0);
    LoaderModel model(meter, budget);
    uint64_t tick = 20;
    model.SetNowTick([&] { return tick; });
    constexpr uint32_t kSite = 0x17d724;
    // Prime the head at the read's lba so the chain is contiguous (a 1-sector read lands in ~0.93 ms).
    int landed = 0;
    uint32_t lba = 200000;
    model.Enqueue(0x50, lba, 1, tick, [&] { ++landed; });   // first read: a long seek (112 ms) -> frames later
    while (model.Poll(0x50) == LoaderModel::PollOutcome::HeldInflight) ++tick;
    CHECK(landed == 1 && model.Consumed() == 1, "lift_first_read_consumed_when_its_frame_arrives");
    lba += 1;
    int polls_progress = 0, yields = 0;
    for (int i = 0; i < 3; ++i) {
        model.Enqueue(0x50, lba, 1, tick, [&] { ++landed; });
        lba += 1;
        // The title's poll inside the same frame: the drive finished -> Progress, the lift declines.
        if (model.Poll(0x50) == LoaderModel::PollOutcome::Progress) ++polls_progress;
        if (model.ShouldYieldPollSlice(kSite, kSite, true)) ++yields;
    }
    CHECK(polls_progress == 3 && yields == 0 && landed == 4, "lift_three_reads_inside_the_frame_three_polls_zero_yields");
    // The 4th: a long seek whose end lies past the frame's edge -> HeldInflight, the lift yields once.
    model.Enqueue(0x50, lba + 500000, 1, tick, [&] { ++landed; });
    CHECK(model.Poll(0x50) == LoaderModel::PollOutcome::HeldInflight && model.ShouldYieldPollSlice(kSite, kSite, true) && model.Yields() == 1,
          "lift_read_past_the_edge_yields");
    CHECK(model.PollDelivered() == 4 && model.WaitDelivered() == 0 && model.Forced() == 0, "lift_census_poll_deliveries");
}

int main() {
    TestBudget();
    TestPollOrderAtEveryPoint();
    TestLiftIteratesInsideTheFrame();
    TestPollWaitSettles();
    TestPollWaitParks();
    TestLoaderModel();
    TestSlot4Order();
    TestModelledDeferral();
    TestPollVisibility();
    std::printf(g_fail ? "FAILURES: %d\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
