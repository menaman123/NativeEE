// tests/gs/queue/test_draw_ir_thread_queue.cpp -- DrawIrThreadQueue over a recording
// sink: records reach the next sink in push order on the consumer thread (never
// the pushing thread); the upload payload is captured at the producer's next
// event and read back by the consumer through LastUploadPayload(); the frame's
// CRTC snapshot travels with the frame record; Sync() / WaitFrame() return only
// once the work is consumed; lockstep returns each push consumed; Stop() drains.
// No real waits: every wait is on a condition the consumer thread satisfies.
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "engine/gs/hle/ir/job_rec.h"
#include "engine/gs/hle/queue/draw_ir_thread_queue.h"
#include "engine/gs/vram/vram_image.h"

using namespace ps2r;
using namespace ps2r::gs;

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        g_checks++;                                                         \
        if (!(cond)) {                                                      \
            g_fails++;                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);       \
        }                                                                   \
    } while (0)

namespace {

// The test's recording sink (the file's primary type): records what it receives
// -- kind letter + seq, the thread that delivered, the upload payload the queue
// exposes at each event (the in-thread consumer applies it at the event after
// the UploadVRAMTexture job) and the CRTC snapshot at each frame.
struct TestDrawIrThreadQueue : DrawIrSink {
    DrawIrThreadQueue* q = nullptr;
    std::vector<std::string> log;
    std::vector<std::thread::id> threads;
    std::vector<size_t> payload_at_event;     // payload size visible at each event (0 = none)
    std::vector<uint32_t> crtc_mode_at_frame;
    // Fault case: the job with this seq throws (the GS pipeline refusing a record).
    int throw_at_seq = -1;
    // Frame-skip case: the draw with this seq parks the consumer until `gate` opens
    // (the renderer busy with an earlier frame).
    int block_at_seq = -1;
    std::atomic<bool> gate{false};
    std::atomic<bool> blocked{false};
    // Split-image case: the pipeline image read at each draw (byte 0x40000, epoch).
    VramImage* target = nullptr;
    std::vector<uint8_t> seen_at_draw;
    std::vector<uint64_t> epoch_at_draw;

    void Note(const std::string& s) {
        log.push_back(s);
        threads.push_back(std::this_thread::get_id());
        const std::vector<uint8_t>* p = q ? q->LastUploadPayload() : nullptr;
        payload_at_event.push_back(p ? p->size() : 0);
    }
    void OnDraw(const DrawIr& d) override {
        Note("D" + std::to_string(d.seq) + "v" + std::to_string(d.verts.size()));
        if (static_cast<int>(d.seq) == block_at_seq) {
            blocked = true;
            while (!gate.load()) std::this_thread::yield();
        }
        if (target) {
            seen_at_draw.push_back(target->Bytes()[0x40000]);
            epoch_at_draw.push_back(target->UploadEpoch());
        }
    }
    void OnJob(const JobRec& j) override {
        Note("J" + std::to_string(j.seq) + "t" + std::to_string(j.type));
        if (static_cast<int>(j.seq) == throw_at_seq) throw std::runtime_error("GS pipeline refused draw " + std::to_string(j.seq));
    }
    void OnFrame(const JobRec& f) override {
        Note("F" + std::to_string(f.seq));
        crtc_mode_at_frame.push_back(q ? q->CurrentFrameCrtc().fbp_bytes : 0xFFFF);
    }
};

JobRec Job(JobKind k, uint32_t seq, uint32_t payload_bytes = 0) {
    JobRec j{};
    j.type = static_cast<uint32_t>(k);
    j.seq = seq;
    j.payload_bytes = payload_bytes;
    return j;
}

DrawIr Draw(uint32_t seq, size_t nverts) {
    DrawIr d;
    d.seq = seq;
    d.verts.resize(nverts);
    for (size_t i = 0; i < nverts; ++i) d.verts[i].x = static_cast<float>(i);
    return d;
}

void TestOrderAndThread(DrawIrThreadQueue::Policy policy) {
    TestDrawIrThreadQueue sink;
    std::vector<uint8_t> published;                 // the engine's LastUploadPixels stand-in
    uint32_t fbp = 0;                               // the CRTC's DISPFB base at the push
    DrawIrThreadQueue::Hooks h;
    h.upload_pixels = [&published] { return &published; };
    h.crtc = [&fbp] {
        CrtcClassifier::Snapshot s{};
        s.fbp_bytes = fbp;
        s.valid = true;
        return s;
    };
    DrawIrThreadQueue q(sink, std::move(h), policy, 8);
    sink.q = &q;
    std::atomic<bool> started{false};
    std::thread::id consumer_tid;
    q.Start([&] { consumer_tid = std::this_thread::get_id(); started = true; });

    // Stream: draw, invalidate, upload (payload published AFTER the push), draw,
    // frame (fbp 1), draw, frame (fbp 3).
    q.OnDraw(Draw(1, 3));
    q.OnJob(Job(JobKind::InvalidateTextures, 2));
    q.OnJob(Job(JobKind::UploadVRAMTexture, 3, 16));
    published.assign(16, 0xAB);                      // the transfer engine publishes now
    q.OnDraw(Draw(4, 6));                            // the upload record is pushed here, with its 16 bytes
    fbp = 1;
    q.OnFrame(Job(JobKind::VSync, 5));
    CHECK(q.FrameSeq() == 1, "one frame pushed");
    q.WaitFrame(1);
    q.OnDraw(Draw(6, 1));
    fbp = 3;
    q.OnFrame(Job(JobKind::VSync, 7));
    q.WaitFrame(q.FrameSeq());
    q.Sync();

    const std::vector<std::string> want = {"D1v3", "J2t8", "J3t9", "D4v6", "F5", "D6v1", "F7"};
    CHECK(sink.log == want, "records delivered in push order");
    if (sink.log != want) {
        for (const std::string& s : sink.log) std::printf("  got %s\n", s.c_str());
    }
    CHECK(started.load(), "the thread-start hook ran");
    bool all_on_consumer = !sink.threads.empty();
    for (std::thread::id t : sink.threads)
        if (t != consumer_tid || t == std::this_thread::get_id()) all_on_consumer = false;
    CHECK(all_on_consumer, "every record was delivered on the consumer thread");
    // Payload visibility: none before the upload; 16 bytes at the upload job and
    // at the draw that follows it (the apply point); still 16 afterwards.
    CHECK(sink.payload_at_event.size() == 7, "seven events");
    if (sink.payload_at_event.size() == 7) {
        CHECK(sink.payload_at_event[0] == 0 && sink.payload_at_event[1] == 0, "no payload before the upload");
        CHECK(sink.payload_at_event[2] == 16, "payload exposed with the upload record");
        CHECK(sink.payload_at_event[3] == 16, "payload still exposed at the next event (apply point)");
    }
    const std::vector<uint8_t>* p = q.LastUploadPayload();
    CHECK(p && p->size() == 16 && (*p)[0] == 0xAB && (*p)[15] == 0xAB, "the copied bytes are the published ones");
    CHECK(sink.crtc_mode_at_frame.size() == 2 && sink.crtc_mode_at_frame[0] == 1 && sink.crtc_mode_at_frame[1] == 3,
          "each frame record carries the CRTC snapshot captured at its push");
    const DrawIrThreadQueue::Counters n = q.Count();
    CHECK(n.enqueued == 7 && n.consumed == 7 && n.frames == 2, "counters: 7 enqueued, 7 consumed, 2 frames");
    CHECK(n.payload_bytes == 16, "16 payload bytes copied");
    if (policy == DrawIrThreadQueue::Policy::Lockstep) {
        CHECK(n.max_depth == 1, "lockstep never holds more than one record");
        CHECK(n.syncs == 0 && n.frame_waits == 0, "lockstep: Sync/WaitFrame never found work outstanding");
    }
    q.Stop();
    CHECK(!q.Running(), "stopped");
    // After Stop the queue is transparent: a push delivers inline, in order.
    q.OnJob(Job(JobKind::SetFrontBuffer, 8));
    CHECK(sink.log.size() == 8 && sink.log.back() == "J8t4", "post-stop push delivered inline");
    CHECK(sink.threads.back() == std::this_thread::get_id(), "post-stop delivery runs on the pushing thread");
}

// A held upload with no payload published (empty vector) is still delivered as a
// job and LastUploadPayload() is cleared for it.
void TestUploadWithoutPayload() {
    TestDrawIrThreadQueue sink;
    std::vector<uint8_t> none;
    DrawIrThreadQueue::Hooks h;
    h.upload_pixels = [&none] { return &none; };
    DrawIrThreadQueue q(sink, std::move(h), DrawIrThreadQueue::Policy::Pipelined, 4);
    sink.q = &q;
    q.Start();
    q.OnJob(Job(JobKind::UploadVRAMTexture, 1, 8));
    q.OnJob(Job(JobKind::InvalidateTextures, 2));
    q.Sync();
    CHECK(sink.log.size() == 2 && sink.log[0] == "J1t9" && sink.log[1] == "J2t8", "upload then invalidate, in order");
    CHECK(q.LastUploadPayload() == nullptr, "no payload for an upload the engine never published");
    // Stop() drains what is still queued.
    for (uint32_t i = 3; i < 20; ++i) q.OnJob(Job(JobKind::SetScissor, i));
    q.Stop();
    CHECK(sink.log.size() == 19, "Stop drained every queued record");
    CHECK(q.Count().consumed == 19, "19 consumed");
}

// A throw out of the next sink on the consumer thread surfaces on the PRODUCER
// thread (once, at its next push / Sync / WaitFrame / RunOrdered) with the same
// type; the queue stays faulted, later records are dropped (counted consumed),
// a dropped RunOrdered resolves false, and the waits never hang.
void TestConsumerFault(DrawIrThreadQueue::Policy policy) {
    TestDrawIrThreadQueue sink;
    sink.throw_at_seq = 3;
    DrawIrThreadQueue q(sink, {}, policy, 8);
    q.Start();
    q.OnJob(Job(JobKind::SetScissor, 1));
    q.OnJob(Job(JobKind::SetScissor, 2));
    bool thrown = false;
    std::string what;
    try {
        q.OnJob(Job(JobKind::SetScissor, 3));   // lockstep: throws here; pipelined: at a later call
        for (uint32_t i = 4; i < 40 && !thrown; ++i) q.OnJob(Job(JobKind::SetScissor, i));
        q.Sync();
    } catch (const std::runtime_error& e) {
        thrown = true;
        what = e.what();
    }
    CHECK(thrown, "the consumer's exception surfaced on the producer thread");
    CHECK(what == "GS pipeline refused draw 3", "the same exception (type and text)");
    CHECK(q.Faulted(), "the queue is faulted");
    // Once: later producer calls neither throw nor hang.
    bool again = false;
    try {
        q.OnJob(Job(JobKind::SetScissor, 100));
        q.Sync();
        q.OnFrame(Job(JobKind::VSync, 101));
        q.WaitFrame(q.FrameSeq());
    } catch (...) { again = true; }
    CHECK(!again, "the fault is rethrown once only");
    const bool ran = q.RunOrdered([] { return true; });
    CHECK(!ran, "a RunOrdered after the fault resolves false (dropped)");
    CHECK(sink.log.size() == 3, "nothing was delivered after the faulting record");
    q.Stop();
    CHECK(q.Count().consumed == q.Count().enqueued, "every record was consumed (delivered or dropped)");
}

// The split image: the EE image's calls are logged (ProducerMirror) and replayed
// on the mirror target before the NEXT record is delivered, so a record sees the
// target exactly as the in-thread consumer saw the shared image. The EE image
// never sees GPU-side marks; both agree on the upload epoch.
void TestSplitImage() {
    VramImage ee, pipeline;
    TestDrawIrThreadQueue sink;
    sink.target = &pipeline;
    DrawIrThreadQueue q(sink, {}, DrawIrThreadQueue::Policy::Pipelined, 16, &pipeline);
    CHECK(q.ProducerMirror() != nullptr && q.MirrorTarget() == &pipeline, "a mirror target gives a producer mirror");
    ee.SetMirror(q.ProducerMirror());
    q.Start();
    const uint8_t a = 0x11, b = 0x22;
    ee.WriteStream(0x40000, &a, 1);
    ee.SettleWrites();
    q.OnDraw(Draw(1, 3));
    ee.WriteStream(0x40000, &b, 1);
    ee.SettleWrites();
    q.OnDraw(Draw(2, 3));
    q.Sync();
    CHECK(sink.seen_at_draw.size() == 2 && sink.seen_at_draw[0] == 0x11 && sink.seen_at_draw[1] == 0x22,
          "each draw sees the EE's writes up to its push, not later ones");
    CHECK(sink.epoch_at_draw[0] == 1 && sink.epoch_at_draw[1] == 2 && ee.UploadEpoch() == pipeline.UploadEpoch(),
          "the upload epoch advances identically on both images");
    CHECK(q.Count().vram_ops == 2 && q.Count().vram_op_bytes > 0, "two op-log records travelled");
    // Sync flushes a pending log by itself (no record after it).
    ee.WriteStream(0x40001, &a, 1);
    q.Sync();
    CHECK(pipeline.Bytes()[0x40001] == 0x11, "Sync() delivers the pending log");
    DrawIrThreadQueue plain(sink, {}, DrawIrThreadQueue::Policy::Pipelined, 16);
    CHECK(plain.ProducerMirror() == nullptr, "no mirror target: no producer mirror (one shared image)");
    q.Stop();
}

// The clock contract, rule 2 (frame skip): with the consumer busy with frame 1 and
// frame 2 whole in the ring, the push of frame 3's record drops frame 2 -- its
// draws are erased, its job stays in order, its frame record is delivered as a
// drop (never reaches the sink), frames_dropped=1 -- and the pushes never waited
// for ring room (capacity 2 < the records pushed). With the consumer keeping up
// (WaitFrame after each frame) nothing is dropped.
void TestFrameSkip() {
    {
        TestDrawIrThreadQueue sink;
        sink.block_at_seq = 1;
        DrawIrThreadQueue q(sink, DrawIrThreadQueue::Hooks{}, DrawIrThreadQueue::Policy::Pipelined, 2);
        sink.q = &q;
        q.SetFrameSkip(true);
        CHECK(q.FrameSkip(), "frame skip set on the pipelined queue");
        q.Start();
        q.OnDraw(Draw(1, 1));                 // frame 1: the consumer parks here
        while (!sink.blocked.load()) std::this_thread::yield();
        q.OnFrame(Job(JobKind::VSync, 11));   // frame 1's record (in the ring behind the parked draw)
        q.OnDraw(Draw(2, 1));                 // frame 2, whole in the ring, never started
        q.OnJob(Job(JobKind::InvalidateTextures, 22));
        q.OnFrame(Job(JobKind::VSync, 12));
        q.OnDraw(Draw(3, 1));                 // frame 3 arrives while the consumer is still on frame 1
        q.OnFrame(Job(JobKind::VSync, 13));   // -> frame 2 is dropped (rule 2)
        CHECK(q.Count().frames_dropped == 1, "the older pending frame was dropped at the next frame's push");
        CHECK(q.Count().max_depth > 2, "pushes never waited for ring room under frame skip (depth exceeded the capacity)");
        sink.gate = true;
        q.WaitFrame(3);
        q.Stop();
        std::string joined;
        for (const auto& e : sink.log) joined += e + " ";
        CHECK(joined == "D1v1 F11 J22t8 D3v1 F13 ", ("stream after the drop: D1 F1 J2 D3 F3 (no D2, no F2): " + joined).c_str());
        CHECK(q.Count().frames == 3 && q.Count().consumed == q.Count().enqueued, "3 frames pushed, every record consumed (delivered or dropped)");
    }
    {
        TestDrawIrThreadQueue sink;
        DrawIrThreadQueue q(sink, DrawIrThreadQueue::Hooks{}, DrawIrThreadQueue::Policy::Pipelined, 8);
        sink.q = &q;
        q.SetFrameSkip(true);
        q.Start();
        for (uint32_t f = 1; f <= 3; ++f) {
            q.OnDraw(Draw(f, 1));
            q.OnFrame(Job(JobKind::VSync, 10 + f));
            q.WaitFrame(f);   // the consumer keeps up: never a pending frame at the next push
        }
        q.Stop();
        CHECK(q.Count().frames_dropped == 0 && sink.log.size() == 6, "consumer keeping up: nothing dropped, 6 records delivered");
    }
    {
        TestDrawIrThreadQueue sink;
        DrawIrThreadQueue q(sink, DrawIrThreadQueue::Hooks{}, DrawIrThreadQueue::Policy::Lockstep, 8);
        q.SetFrameSkip(true);
        CHECK(!q.FrameSkip(), "frame skip is a pipelined-only rule (lockstep stays lockstep)");
    }
    // Depth 2 (play): the consumer parked on frame 1, frames 2 AND 3 whole in the ring --
    // frame 3's push drops nothing (two pending pictures are allowed), frame 4's push
    // drops the OLDEST whole one (frame 2); frame 3 is delivered whole after frame 1.
    {
        TestDrawIrThreadQueue sink;
        sink.block_at_seq = 1;
        DrawIrThreadQueue q(sink, DrawIrThreadQueue::Hooks{}, DrawIrThreadQueue::Policy::Pipelined, 2);
        sink.q = &q;
        q.SetFrameSkip(true, 2);
        CHECK(q.FrameSkip() && q.FrameSkipDepth() == 2, "frame skip depth 2 set on the pipelined queue");
        q.Start();
        q.OnDraw(Draw(1, 1));
        while (!sink.blocked.load()) std::this_thread::yield();
        q.OnFrame(Job(JobKind::VSync, 11));
        q.OnDraw(Draw(2, 1));
        q.OnJob(Job(JobKind::InvalidateTextures, 22));
        q.OnFrame(Job(JobKind::VSync, 12));
        q.OnDraw(Draw(3, 1));
        q.OnFrame(Job(JobKind::VSync, 13));   // two whole pending frames: allowed at depth 2
        CHECK(q.Count().frames_dropped == 0, "depth 2: the second pending frame is kept");
        q.OnDraw(Draw(4, 1));
        q.OnFrame(Job(JobKind::VSync, 14));   // a third would exceed the depth: the oldest (frame 2) is dropped
        CHECK(q.Count().frames_dropped == 1, "depth 2: the third pending frame drops the oldest whole one");
        sink.gate = true;
        q.WaitFrame(4);
        q.Stop();
        std::string joined;
        for (const auto& e : sink.log) joined += e + " ";
        CHECK(joined == "D1v1 F11 J22t8 D3v1 F13 D4v1 F14 ", ("stream after the drop: D1 F1 J2 D3 F3 D4 F4 (no D2, no F2): " + joined).c_str());
        CHECK(q.Count().frames == 4 && q.Count().consumed == q.Count().enqueued, "4 frames pushed, every record consumed (delivered or dropped)");
    }
}

// The clock contract, rule 2 (review loop 1): the EE's local->local source in
// play is served WITHOUT a wait. Page 8 (0x10000) is a GPU target's on the
// pipeline image (a deposit the consumer committed, author GpuTarget); page 9 is
// the EE's. (1) The first copy arms the snapshot and is served from the EE
// image alone (unserved=1, no GPU page known). (2) After a frame record the
// consumer published the snapshot: a copy whose source is page 8 takes the GPU
// bytes (snapshot_served=1, stale counted), a copy over page 9 reads the EE's
// own bytes (ee_served=1). (3) An EE write to page 8 after the snapshot wins
// over the snapshot's GPU bytes (the EE image is authoritative for its writes).
// (4) The producer never waited: syncs stays 0.
void TestLocalToLocalSource() {
    VramImage ee, pipeline;
    TestDrawIrThreadQueue sink;
    sink.target = &pipeline;
    DrawIrThreadQueue q(sink, {}, DrawIrThreadQueue::Policy::Pipelined, 16, &pipeline);
    sink.q = &q;
    ee.SetMirror(q.ProducerMirror());
    q.SetFrameSkip(true);
    q.Start();
    const uint32_t gpu_page = 0x10000, ee_page = 0x12000;
    const uint8_t e = 0x33;
    ee.WriteStream(ee_page, &e, 1);
    ee.WriteStream(gpu_page, &e, 1);   // the EE wrote page 8 first (epoch 1 on both images)
    ee.SettleWrites();
    q.OnDraw(Draw(1, 1));
    q.Sync();                          // test-side only: land the op log before the GPU deposit below
    const uint64_t syncs_before = q.Count().syncs;
    // The consumer's GPU deposit on page 8 (author GpuTarget), committed.
    {
        std::vector<uint8_t> px(VramImage::kPageBytes, 0xAB);
        pipeline.StageDeposit(gpu_page, px.data(), px.size(), /*batch_epoch=*/pipeline.UploadEpoch());
        pipeline.NoteGpuAuthor(VramImage::Span{gpu_page, VramImage::kPageBytes}, 0x10000);
        pipeline.CommitDeposits();
    }
    CHECK(pipeline.Bytes()[gpu_page] == 0xAB && ee.Bytes()[gpu_page] == 0x33, "the deposit is pipeline-side only");
    // (1) the first copy: no snapshot yet -> arms it, served from the EE image.
    bool over_gpu = q.ServeLocalToLocalSource(VramImage::Span{gpu_page, 64}, ee);
    CHECK(!over_gpu && q.Count().l2l_unserved == 1 && ee.Bytes()[gpu_page] == 0x33,
          "first copy: snapshot armed, served from the EE image (unserved=1)");
    // A frame completes on the consumer -> the snapshot is published.
    q.OnFrame(Job(JobKind::VSync, 11));
    q.WaitFrame(1);
    CHECK(q.Count().snapshots == 1, "the frame record published one snapshot");
    // (2) GPU page from the snapshot; EE page from the EE image.
    over_gpu = q.ServeLocalToLocalSource(VramImage::Span{gpu_page, 64}, ee);
    CHECK(over_gpu && ee.Bytes()[gpu_page] == 0xAB && ee.Bytes()[gpu_page + 100] == 0xAB,
          "a GPU-authored source page is served from the completed frame's pixels");
    CHECK(q.Count().l2l_snapshot_served == 1, "snapshot_served=1");
    over_gpu = q.ServeLocalToLocalSource(VramImage::Span{ee_page, 64}, ee);
    CHECK(!over_gpu && ee.Bytes()[ee_page] == 0x33 && q.Count().l2l_ee_served == 1,
          "an EE-authored source page is served from the EE image (ee_served=1)");
    // Staleness: two more frames pushed, none completed by the consumer... (the
    // consumer keeps up here, so the stale count is the frames since the snapshot).
    q.OnFrame(Job(JobKind::VSync, 12));
    q.OnFrame(Job(JobKind::VSync, 13));
    q.WaitFrame(3);
    CHECK(q.Count().snapshots == 3, "every completed frame republished the snapshot");
    // (3) an EE write after the snapshot wins: write page 8, no frame since.
    const uint8_t n = 0x77;
    ee.WriteStream(gpu_page, &n, 1);
    ee.SettleWrites();                 // the EE image's page epoch moves past the snapshot's
    over_gpu = q.ServeLocalToLocalSource(VramImage::Span{gpu_page, 64}, ee);
    CHECK(over_gpu && ee.Bytes()[gpu_page] == 0x77, "the EE's newer write is authoritative over the snapshot's GPU bytes");
    // (4) no wait happened on the producer in any Serve call.
    CHECK(q.Count().syncs == syncs_before, "ServeLocalToLocalSource never synced the producer");
    q.Stop();
}

// Batched consumption (perf): a pipelined ring of many records is delivered in
// push order with the counters exact; a Sync() waiter returns once everything was
// consumed (the consumer publishes its folded counts to a waiter without waiting
// for its batch to end); WaitFrame sees every frame; Stop folds the last counts.
void TestBatchedConsumption() {
    TestDrawIrThreadQueue sink;
    std::vector<uint8_t> none;
    DrawIrThreadQueue::Hooks h;
    h.upload_pixels = [&none] { return &none; };
    h.crtc = [] { return CrtcClassifier::Snapshot{}; };
    DrawIrThreadQueue q(sink, std::move(h), DrawIrThreadQueue::Policy::Pipelined, 100000);
    sink.q = &q;
    // The consumer parks on draw 1 so the ring fills with several batches' worth.
    sink.block_at_seq = 1;
    q.Start();
    const uint32_t kDraws = 1000;
    uint32_t seq = 1;
    for (uint32_t f = 0; f < 4; ++f) {
        for (uint32_t i = 0; i < kDraws / 4; ++i) q.OnDraw(Draw(seq++, 3));
        q.OnJob(Job(JobKind::InvalidateTextures, seq++));
        q.OnFrame(Job(JobKind::VSync, seq++));
    }
    while (!sink.blocked.load()) std::this_thread::yield();
    CHECK(q.Depth() >= kDraws / 2, "the ring holds a backlog while the consumer is parked");
    sink.gate = true;
    q.Sync();
    const DrawIrThreadQueue::Counters n = q.Count();
    CHECK(n.enqueued == seq - 1 && n.consumed == seq - 1, "Sync returned with every record consumed");
    CHECK(n.delivered_draws == kDraws, "every draw counted");
    CHECK(n.frames == 4, "four frames pushed");
    q.WaitFrame(4);
    CHECK(sink.log.size() == seq - 1, "every record delivered");
    bool ordered = true;
    for (size_t i = 0; i + 1 < sink.log.size() && ordered; ++i) {
        auto num = [](const std::string& s) { return std::stoul(s.substr(1)); };
        if (num(sink.log[i]) + 1 != num(sink.log[i + 1])) ordered = false;
    }
    CHECK(ordered, "records delivered in push order");
    // A second round with the consumer awake: Sync after each small push returns
    // promptly (the waiter publication path), the counters stay exact.
    for (uint32_t i = 0; i < 50; ++i) {
        q.OnDraw(Draw(seq++, 3));
        q.Sync();
        CHECK(q.Count().consumed == seq - 1, "Sync sees the record consumed");
    }
    q.OnFrame(Job(JobKind::VSync, seq++));
    q.WaitFrame(5);
    CHECK(sink.log.back() == "F" + std::to_string(seq - 1), "the fifth frame delivered");
    q.Stop();
    CHECK(q.Count().consumed == seq - 1, "Stop drained with the counts folded");
}

}  // namespace

int main(int argc, char** argv) {
    const std::string which = argc > 1 ? argv[1] : "all";
    if (which == "lockstep" || which == "all") TestOrderAndThread(DrawIrThreadQueue::Policy::Lockstep);
    if (which == "pipelined" || which == "all") TestOrderAndThread(DrawIrThreadQueue::Policy::Pipelined);
    if (which == "nopayload" || which == "all") TestUploadWithoutPayload();
    if (which == "fault" || which == "all") {
        TestConsumerFault(DrawIrThreadQueue::Policy::Lockstep);
        TestConsumerFault(DrawIrThreadQueue::Policy::Pipelined);
    }
    if (which == "splitimage" || which == "all") TestSplitImage();
    if (which == "frameskip" || which == "all") TestFrameSkip();
    if (which == "l2lsource" || which == "all") TestLocalToLocalSource();
    if (which == "batched" || which == "all") TestBatchedConsumption();
    std::printf("%s: %d checks, %d failures\n", argv[0], g_checks, g_fails);
    return g_fails ? 1 : 0;
}
