// tests/ipu/test_mpeg_api.cpp -- ctest MpegApi: the sceMpeg HLE entry points over a
// vector GuestMemory, a file-backed sector source and a counting PCM sink. Host-only
// half: lifecycle on an empty table, a GetPicture on an unknown context, the alalc
// allocator and reference-slot clear helpers. OLD-gated half (argv[1] = the frozen
// OLD root): the first intro movie is pumped through the blocking-refill path until
// five pictures are served; dimensions, the ring mirror, the strip bytes, the pacer
// bookings, the audio capture and the grid carried across sceMpegReset are asserted.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "counting_fmv_pcm_sink.h"
#include "engine/ipu/hle/ffmpeg_video_decoder.h"
#include "engine/ipu/hle/fmv_context_layout.h"
#include "engine/ipu/hle/frame_writer.h"
#include "engine/ipu/hle/mpeg_api.h"
#include "engine/ipu/hle/pss_demux.h"
#include "engine/ipu/hle/pss_demux_listener.h"
#include "engine/ipu/ipu_settings.h"
#include "file_fmv_sector_source.h"
#include "vector_guest_memory.h"

using namespace ps2r::ipu;

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failed;                                                      \
            std::printf("FAIL: %s (line %d): %s\n", msg, __LINE__, #cond);   \
        }                                                                    \
    } while (0)

// Synthetic context layout: offsets are arbitrary test values (the real ones are
// the title's), the guest words they address live in the vector memory.
static FmvContextLayout TestLayout() {
    FmvContextLayout l{};
    l.ring_base = 0x10;
    l.ring_size = 0x14;
    l.ring_write = 0x18;
    l.ring_read = 0x1c;
    l.ring_anchor = 0x20;
    l.inner_status_ptr = 0x24;
    l.total = 0x28;
    l.play_to_eof_total = 0x40000000;
    l.presented = 0x2c;
    l.mode = 0x30;
    l.mode_raw8 = 2;
    l.plane_select = 0x34;
    l.plane0 = 0x38;
    l.plane1 = 0x3c;
    l.target_w = 0x40;
    l.target_h = 0x44;
    l.abort_flag = 0x48;
    l.alalc_base = 0x0;
    l.alalc_capacity = 0x4;
    l.alalc_watermark = 0x8;
    l.alalc_base_copy = 0xc;
    const uint32_t slots[FmvContextLayout::kRefSlots] = {0x1c8, 0x1d8, 0x1e8, 0x1cc, 0x1dc, 0x1ec};
    for (uint32_t i = 0; i < FmvContextLayout::kRefSlots; ++i) l.ref_slot[i] = slots[i];
    l.ref_slot_status = 0x28;
    return l;
}

static std::unique_ptr<VideoDecoder> MakeFfmpegDecoder() {
    return std::make_unique<FfmpegVideoDecoder>();
}

// Independent oracle for the first served picture: demuxes the same file through
// its own PssDemux + FfmpegVideoDecoder and keeps the first decoded picture.
struct TestMpegApi : public PssDemuxListener {
    FfmpegVideoDecoder decoder;
    std::deque<DecodedPicture> pictures;

    void OnVideoEs(const uint8_t* data, size_t size, bool) override {
        if (pictures.empty()) decoder.Feed(data, size, pictures);
    }
    void OnAudioPes(const uint8_t*, size_t) override {}
    void OnProgramEnd() override {}

    bool FirstPicture(const std::string& path, DecodedPicture& out) {
        FileFmvSectorSource src;
        if (!src.Open(path)) return false;
        PssDemux demux(*this);
        std::vector<uint8_t> buf(16 * FileFmvSectorSource::kSectorBytes);
        for (int i = 0; i < 4096 && pictures.empty(); ++i) {
            const uint32_t got = src.PumpSectors(buf.data(), 16);
            if (got == 0) break;
            demux.Append(buf.data(), static_cast<size_t>(got) * FileFmvSectorSource::kSectorBytes);
            demux.Process(false);
        }
        if (pictures.empty()) return false;
        out = pictures.front();
        return true;
    }
};

// ---------------------------------------------------------------------------
// Host-only half.
// ---------------------------------------------------------------------------

static void TestLifecycleEmpty() {
    VectorGuestMemory mem;
    FileFmvSectorSource src;  // never opened: PumpSectors returns 0
    CountingFmvPcmSink sink;
    IpuSettings settings;
    const FmvContextLayout layout = TestLayout();
    MpegApi api(mem, layout, src, sink, MakeFfmpegDecoder, settings);

    api.Init();
    CHECK(sink.flushes == 1, "Init flushes the PCM sink");
    api.Reset(0x1000);
    CHECK(sink.flushes == 1, "Reset of a context without audio does not flush");
    api.Destroy(0x1000);
    CHECK(sink.flushes == 1, "Destroy of a context without audio does not flush");
    api.Destroy(0x2000);  // unknown: no-op
    CHECK(!api.FmvActive(), "no movie in flight on an empty table");
    CHECK(!api.IsEnd(0x1000) && !api.ShouldHoldAbort(0x1000), "unknown ctx: not ended, no hold");
    CHECK(api.Width(0x1000) == 512 && api.Height(0x1000) == 256, "default dimensions before a serve");
    api.NotifyCdStreamEof();  // nothing open
    CHECK(sink.gate_opens == 0 && sink.push_calls == 0, "no audio without input");
}

static void TestGetPictureUnknownCtx() {
    VectorGuestMemory mem;
    FileFmvSectorSource src;
    CountingFmvPcmSink sink;
    IpuSettings settings;
    const FmvContextLayout layout = TestLayout();
    MpegApi api(mem, layout, src, sink, MakeFfmpegDecoder, settings);
    api.Init();

    const std::vector<uint8_t> before = mem.Bytes();
    const MpegApi::PictureResult r = api.GetPicture(0x3000, 0x8000, 100);
    CHECK(!r.served && r.wait_ticks == 0 && !r.late, "unknown ctx: nothing served, no wait");
    CHECK(mem.Bytes() == before, "unknown ctx: guest memory untouched");
    const MpegApi::PictureResult r8 = api.GetPictureRaw8(0x3000, 0x8000, 32, 32, 100);
    CHECK(!r8.served, "unknown ctx (RAW8): nothing served");
    CHECK(mem.Bytes() == before, "unknown ctx (RAW8): guest memory untouched");
    CHECK(src.PumpCalls() == 0, "a context without a ring never pumps the sector source");
    CHECK(!api.FmvActive(), "still no movie in flight");
}

// The libmpeg-internal guest-word helpers: the sceMpegAlalc bump allocator and
// the reference-slot clear. No context state; every effect is a guest word.
static void TestGuestWordHelpers() {
    VectorGuestMemory mem;
    FileFmvSectorSource src;
    CountingFmvPcmSink sink;
    IpuSettings settings;
    const FmvContextLayout layout = TestLayout();
    MpegApi api(mem, layout, src, sink, MakeFfmpegDecoder, settings);

    // Alalc: init arms the four words; alloc aligns the watermark up.
    constexpr uint32_t kAlalc = 0x500;
    api.AlalcInit(0, 0x10000, 0x1000);  // null allocator: ignored
    CHECK(mem.Read32(kAlalc + layout.alalc_base) == 0, "AlalcInit(0) writes nothing");
    api.AlalcInit(kAlalc, 0x10000, 0x1000);
    CHECK(mem.Read32(kAlalc + layout.alalc_base) == 0x10000, "alalc base");
    CHECK(mem.Read32(kAlalc + layout.alalc_capacity) == 0x1000, "alalc capacity");
    CHECK(mem.Read32(kAlalc + layout.alalc_watermark) == 0x10000, "alalc watermark starts at base");
    CHECK(mem.Read32(kAlalc + layout.alalc_base_copy) == 0x10000, "alalc base copy");

    CHECK(api.AlalcAlloc(kAlalc, 0x30, 0x10) == 0x10000, "first alloc at the base");
    CHECK(mem.Read32(kAlalc + layout.alalc_watermark) == 0x10030, "watermark advanced by size");
    CHECK(api.AlalcAlloc(kAlalc, 0x8, 0x40) == 0x10040, "second alloc aligned up to 0x40");
    CHECK(mem.Read32(kAlalc + layout.alalc_watermark) == 0x10048, "watermark after the aligned alloc");
    CHECK(api.AlalcAlloc(kAlalc, 0x1000, 0x10) == 0, "exhausted region returns 0");
    CHECK(mem.Read32(kAlalc + layout.alalc_watermark) == 0x10048, "failed alloc leaves the watermark");
    CHECK(api.AlalcAlloc(kAlalc, 0x8, 0) == 0, "zero alignment returns 0");
    CHECK(api.AlalcAlloc(0, 0x8, 0x10) == 0, "null allocator returns 0");
    CHECK(api.AlalcAlloc(kAlalc, 0xfb8, 0x8) == 0x10048, "exact fit up to base + capacity succeeds");
    CHECK(mem.Read32(kAlalc + layout.alalc_watermark) == 0x11000, "watermark at the region end");

    // ClearRefBuff: only the allocated slots' status words are zeroed.
    constexpr uint32_t kCtx = 0x1000;
    constexpr uint32_t kInner = 0x2000;
    constexpr uint32_t kSlotA = 0x3000;
    constexpr uint32_t kSlotB = 0x3100;
    api.ClearRefBuff(0);  // null context: nothing
    api.ClearRefBuff(kCtx);  // inner pointer 0: nothing
    mem.Write32(kCtx + layout.inner_status_ptr, kInner);
    mem.Write32(kInner + layout.ref_slot[0], kSlotA);
    mem.Write32(kInner + layout.ref_slot[3], kSlotB);  // slots 1,2,4,5 stay null
    mem.Write32(kSlotA + layout.ref_slot_status, 0xdead);
    mem.Write32(kSlotB + layout.ref_slot_status, 0xbeef);
    mem.Write32(kSlotA + layout.ref_slot_status + 4, 0x1234);  // neighbour word
    mem.Write32(layout.ref_slot_status, 0x5555);  // a null slot must not clear address 0 + status
    api.ClearRefBuff(kCtx);
    CHECK(mem.Read32(kSlotA + layout.ref_slot_status) == 0, "slot A status cleared");
    CHECK(mem.Read32(kSlotB + layout.ref_slot_status) == 0, "slot B status cleared");
    CHECK(mem.Read32(kSlotA + layout.ref_slot_status + 4) == 0x1234, "neighbour word untouched");
    CHECK(mem.Read32(layout.ref_slot_status) == 0x5555, "null slots are skipped");
    CHECK(mem.Read32(kInner + layout.ref_slot[0]) == kSlotA, "slot pointers untouched");
}

// ---------------------------------------------------------------------------
// OLD-gated half: the first intro movie through the blocking-refill pump.
// ---------------------------------------------------------------------------

static int TestOldMovie(const std::string& og_root) {
    const std::string path = og_root + "/game_files/example_title.rep/FMV/VIVENDI.PSS";
    FileFmvSectorSource src;
    if (!src.Open(path)) {
        std::printf("SKIP: OLD movie not readable: %s\n", path.c_str());
        return 0;
    }

    constexpr uint32_t kCtx = 0x1000;
    constexpr uint32_t kInner = 0x2000;
    constexpr uint32_t kRingBase = 0x40000;
    constexpr uint32_t kRingSize = 0x40000;
    constexpr uint32_t kImage = 0x100000;
    VectorGuestMemory mem(4u << 20);
    CountingFmvPcmSink sink;
    IpuSettings settings;
    const FmvContextLayout layout = TestLayout();
    MpegApi api(mem, layout, src, sink, MakeFfmpegDecoder, settings);
    api.Init();

    // The context the player initialised: ring allocated, write = anchor =
    // base + 0x40, read = base, inner block present, RAW8 mode word.
    mem.Write32(kCtx + layout.ring_base, kRingBase);
    mem.Write32(kCtx + layout.ring_size, kRingSize);
    mem.Write32(kCtx + layout.ring_write, kRingBase + 0x40);
    mem.Write32(kCtx + layout.ring_read, kRingBase);
    mem.Write32(kCtx + layout.ring_anchor, kRingBase + 0x40);
    mem.Write32(kCtx + layout.inner_status_ptr, kInner);
    mem.Write32(kInner, 0x7777);  // stale status the worker must clear
    mem.Write32(kCtx + layout.mode, layout.mode_raw8);
    mem.Write32(kCtx + layout.total, 0);

    // The game's load loop demuxes the first sectors itself (sceMpegDemuxPss
    // from a RAM window); the same shared cursor then feeds the pump.
    constexpr uint32_t kWindow = 0x300000;
    constexpr uint32_t kWindowSectors = 16;
    {
        uint8_t* win = mem.Translate(kWindow, kWindowSectors * FileFmvSectorSource::kSectorBytes);
        const uint32_t got = src.PumpSectors(win, kWindowSectors);
        CHECK(got == kWindowSectors, "first window read");
        const int32_t consumed = api.DemuxPss(kCtx, kWindow, static_cast<int32_t>(got * 2048u));
        CHECK(consumed == static_cast<int32_t>(kWindowSectors * 2048u), "DemuxPss consumed the window");
    }
    CHECK(api.FmvActive(), "input seen: movie in flight before the first serve");
    CHECK(static_cast<int32_t>(mem.Read32(kCtx + layout.total)) == layout.play_to_eof_total,
          "completion gate held at the play-to-end total after the demux feed");
    CHECK(mem.Read32(kInner) == 0, "inner status cleared by the demux feed");

    std::vector<MpegApi::PictureResult> served;
    std::vector<uint8_t> first_strips;
    int64_t now = 1000;
    int calls = 0;
    for (; calls < 40 && served.size() < 5; ++calls, now += 2) {
        const MpegApi::PictureResult r = api.GetPicture(kCtx, kImage, now);
        if (r.served) {
            served.push_back(r);
            if (served.size() == 1) {
                const size_t bytes = FrameWriter::Rgb32Bytes(api.Width(kCtx), api.Height(kCtx));
                const uint8_t* p = mem.Translate(kImage, bytes);
                CHECK(p != nullptr, "image buffer mapped");
                if (p) first_strips.assign(p, p + bytes);
            }
        }
    }
    CHECK(served.size() >= 5, "at least 5 pictures served within 40 calls");
    CHECK(calls == 5, "every GetPicture served (the pump blocks until a picture exists)");
    CHECK(api.Width(kCtx) == 640 && api.Height(kCtx) == 480, "first movie is 640x480");
    CHECK(src.PumpCalls() > 0, "the ring-owning context pumped the sector source");
    CHECK(api.FmvActive(), "movie in flight");
    CHECK(api.ShouldHoldAbort(kCtx) && !api.IsEnd(kCtx), "started, not ended");

    // Ring mirror: the write cursor advanced past the anchor and the read
    // cursor was drained to it after the serve; the anchor holds the sequence
    // header start code and the dims the player parses from bytes 4..6.
    const uint32_t write = mem.Read32(kCtx + layout.ring_write);
    CHECK(write != kRingBase + 0x40, "ring write cursor advanced");
    CHECK(write >= kRingBase && write < kRingBase + kRingSize, "ring write cursor inside the ring");
    CHECK(mem.Read32(kCtx + layout.ring_read) == write, "ring read drained to write after a serve");
    const uint8_t* anchor = mem.Translate(kRingBase + 0x40, 7);
    CHECK(anchor && anchor[0] == 0 && anchor[1] == 0 && anchor[2] == 1 && anchor[3] == 0xB3,
          "sequence header at the ring anchor");
    if (anchor) {
        const uint32_t w = (static_cast<uint32_t>(anchor[4]) << 4) | (anchor[5] >> 4);
        const uint32_t h = ((static_cast<uint32_t>(anchor[5]) & 0xf) << 8) | anchor[6];
        CHECK(w == 640 && h == 480, "anchor header dims 640x480");
    }
    CHECK(mem.Read32(kInner) == 0, "inner status 0 while pictures remain");

    // Strip bytes of the first served picture == FrameWriter over the same
    // picture decoded independently.
    TestMpegApi oracle;
    DecodedPicture first;
    CHECK(oracle.FirstPicture(path, first), "oracle decoded the first picture");
    if (first.width > 0) {
        CHECK(first.width == 640 && first.height == 480, "oracle picture 640x480");
        std::vector<uint8_t> expect(FrameWriter::Rgb32Bytes(first.width, first.height), 0);
        FrameWriter::WriteRgb32Strips(expect.data(), first);
        CHECK(expect == first_strips, "first served strips == WriteRgb32Strips(first picture)");
    }

    // Pacer bookings: the two priming serves are instant; the third arms the
    // grid (instant); with the tick advancing by the 2-tick period every later
    // serve is on time.
    if (served.size() >= 5) {
        CHECK(served[0].wait_ticks == 0 && !served[0].late, "serve 0 instant");
        CHECK(served[1].wait_ticks == 0 && !served[1].late, "serve 1 instant");
        CHECK(served[2].wait_ticks == 0 && !served[2].late, "serve 2 arms the grid, instant");
        CHECK(served[3].wait_ticks == 0 && !served[3].late, "serve 3 on the 2-tick grid");
        CHECK(served[4].wait_ticks == 0 && !served[4].late, "serve 4 on the 2-tick grid");
    }
    // Holding the tick at the last serve's exposes the period: 2, then 4 ticks.
    const int64_t hold = now - 2;
    const MpegApi::PictureResult h1 = api.GetPicture(kCtx, kImage, hold);
    const MpegApi::PictureResult h2 = api.GetPicture(kCtx, kImage, hold);
    CHECK(h1.served && h1.wait_ticks == 2 && !h1.late, "same tick: wait one 2-tick period");
    CHECK(h2.served && h2.wait_ticks == 4 && !h2.late, "same tick again: wait two periods");
    // A late arrival (tick far ahead) serves instantly and is flagged.
    const MpegApi::PictureResult late = api.GetPicture(kCtx, kImage, hold + 100);
    CHECK(late.served && late.wait_ticks == 0 && late.late, "late arrival: instant, flagged");

    // Audio: the movie's PCM reached the sink and the gate opened once.
    CHECK(sink.push_calls > 0 && sink.frames > 0, "PCM frames pushed");
    CHECK(sink.frames % 256 == 0, "PCM arrives in 256-frame pairs");
    CHECK(sink.gate_opens == 1, "GateOpen once");
    CHECK(sink.flushes == 1, "only Init flushed so far");

    // Reset: fresh playback, audio cut, dimensions kept, no movie in flight.
    api.Reset(kCtx);
    CHECK(sink.flushes == 2, "Reset of the audio-carrying context flushes");
    CHECK(!api.FmvActive(), "Reset: no movie in flight");
    CHECK(!api.ShouldHoldAbort(kCtx), "Reset: no abort hold");

    // A second movie on the SAME context continues the first movie's tick
    // grid (Reset keeps the pacer): after the two priming serves, a paced
    // serve at a tick far past the carried deadline is a late catch-up serve,
    // not a fresh arming serve; the next one at the same tick is late again
    // (a fresh grid would have armed at now + period and booked a 2-tick wait).
    CHECK(src.Open(path), "sector source rewound for the second movie");
    {
        uint8_t* win = mem.Translate(kWindow, kWindowSectors * FileFmvSectorSource::kSectorBytes);
        const uint32_t got = src.PumpSectors(win, kWindowSectors);
        CHECK(got == kWindowSectors, "second movie: first window read");
        api.DemuxPss(kCtx, kWindow, static_cast<int32_t>(got * 2048u));
    }
    const int64_t second = hold + 100;
    const MpegApi::PictureResult m2a = api.GetPicture(kCtx, kImage, second);
    const MpegApi::PictureResult m2b = api.GetPicture(kCtx, kImage, second);
    // Null image pointer: the serve counts, nothing is written.
    mem.Write32(kImage, 0xa5a5a5a5u);
    const MpegApi::PictureResult m2c = api.GetPicture(kCtx, 0, second);
    CHECK(mem.Read32(kImage) == 0xa5a5a5a5u, "image_addr 0: guest image untouched");
    const MpegApi::PictureResult m2d = api.GetPicture(kCtx, kImage, second);
    CHECK(m2a.served && m2a.wait_ticks == 0 && !m2a.late, "second movie: priming serve 0 instant");
    CHECK(m2b.served && m2b.wait_ticks == 0 && !m2b.late, "second movie: priming serve 1 instant");
    CHECK(m2c.served && m2c.wait_ticks == 0 && m2c.late, "second movie: serve 2 late on the carried grid");
    CHECK(m2d.served && m2d.wait_ticks == 0 && m2d.late, "second movie: serve 3 still catching up");
    CHECK(mem.Read32(kImage) != 0xa5a5a5a5u, "serve 3 wrote the image again");
    CHECK(api.Width(kCtx) == 640 && api.Height(kCtx) == 480, "second movie 640x480");
    CHECK(sink.gate_opens == 2, "GateOpen issued once per playback");
    CHECK(api.FmvActive(), "second movie in flight");

    api.Reset(kCtx);
    CHECK(sink.flushes == 3, "Reset of the second playback flushes");
    api.Destroy(kCtx);
    CHECK(sink.flushes == 3, "Destroy after Reset: capture state was cleared, no flush");

    std::printf("mpeg_api: VIVENDI.PSS served=%zu (+3) sectors=%llu pcm_frames=%llu\n",
                served.size(), static_cast<unsigned long long>(src.SectorsRead()),
                static_cast<unsigned long long>(sink.frames));
    return 1;
}

// The title's own serve cadence, exactly as the coldboot leg logs it per movie
// (sceMpegCreate/Reset/DemuxPss/GetPicture rows at dev.log_level=info):
// Reset -> one 16 KB DemuxPss window -> Reset again (drops that window's
// decode; the shared stream cursor stays past it) -> two priming GetPicture
// calls (the blocking pump pulls the next sectors) -> the load loop demuxes
// the WHOLE remaining file in 16 KB windows -> the refill callback reports
// end of file and the loop hands DemuxPss its -1 -> the player keeps asking
// GetPicture until the stream genuinely ends. Pins that, asked this way, the
// engine serves the OLD golden window of the first movie: 201 pictures, all
// 640x480, no "no picture" answers before the end, ShouldHoldAbort true for
// the whole front-loaded window and false once the stream has ended.
static int TestOldMovieTitleCadence(const std::string& og_root) {
    const std::string path = og_root + "/game_files/example_title.rep/FMV/VIVENDI.PSS";
    FileFmvSectorSource src;
    if (!src.Open(path)) {
        std::printf("SKIP: OLD movie not readable (cadence): %s\n", path.c_str());
        return 0;
    }
    constexpr uint32_t kGoldenServedMovie1 = 201;  // tests/ipu/fixtures/coldboot_pictures.sha256 (640x480 rows)

    constexpr uint32_t kCtx = 0x1000;
    constexpr uint32_t kInner = 0x2000;
    constexpr uint32_t kRingBase = 0x40000;
    constexpr uint32_t kRingSize = 0x40000;
    constexpr uint32_t kImage = 0x100000;
    constexpr uint32_t kWindow = 0x300000;
    constexpr uint32_t kWindowSectors = 8;  // the title's 16 KB RAM window
    VectorGuestMemory mem(4u << 20);
    CountingFmvPcmSink sink;
    IpuSettings settings;
    const FmvContextLayout layout = TestLayout();
    MpegApi api(mem, layout, src, sink, MakeFfmpegDecoder, settings);
    api.Init();

    mem.Write32(kCtx + layout.ring_base, kRingBase);
    mem.Write32(kCtx + layout.ring_size, kRingSize);
    mem.Write32(kCtx + layout.ring_write, kRingBase + 0x40);
    mem.Write32(kCtx + layout.ring_read, kRingBase);
    mem.Write32(kCtx + layout.ring_anchor, kRingBase + 0x40);
    mem.Write32(kCtx + layout.inner_status_ptr, kInner);
    mem.Write32(kCtx + layout.mode, layout.mode_raw8);
    mem.Write32(kCtx + layout.total, 0);

    uint8_t* win = mem.Translate(kWindow, kWindowSectors * FileFmvSectorSource::kSectorBytes);
    CHECK(win != nullptr, "cadence: window mapped");
    if (!win) return 1;

    // Create + Reset, the first window, Create + Reset again.
    api.Reset(kCtx);
    CHECK(src.PumpSectors(win, kWindowSectors) == kWindowSectors, "cadence: first window read");
    CHECK(api.DemuxPss(kCtx, kWindow, static_cast<int32_t>(kWindowSectors * 2048u)) ==
              static_cast<int32_t>(kWindowSectors * 2048u),
          "cadence: first window consumed");
    api.Reset(kCtx);
    CHECK(!api.FmvActive(), "cadence: the second Reset drops the first window's decode");

    // The two priming serves (instant: the blocking pump reads ahead).
    int64_t now = 1000;
    uint32_t served = 0, nopic_before_end = 0;
    for (int i = 0; i < 2; ++i, now += 2) {
        const MpegApi::PictureResult r = api.GetPicture(kCtx, kImage, now);
        CHECK(r.served && r.wait_ticks == 0, "cadence: priming serve instant");
        if (r.served) ++served;
    }
    CHECK(api.Width(kCtx) == 640 && api.Height(kCtx) == 480, "cadence: 640x480 after the priming serves");

    // The load loop: the rest of the file in 16 KB windows, then the refill
    // callback's end-of-file (-1) handed straight to DemuxPss.
    uint32_t windows = 0;
    for (;;) {
        const uint32_t got = src.PumpSectors(win, kWindowSectors);
        if (got == 0) break;
        const int32_t n = static_cast<int32_t>(got * 2048u);
        if (api.DemuxPss(kCtx, kWindow, n) != n) { CHECK(false, "cadence: a load window not fully consumed"); break; }
        ++windows;
    }
    CHECK(windows > 400, "cadence: the whole movie went through the load loop");
    CHECK(api.DemuxPss(kCtx, kWindow + 1, -1) == 0, "cadence: the callback's -1 consumes nothing");
    CHECK(api.ShouldHoldAbort(kCtx) && !api.IsEnd(kCtx),
          "cadence: front-loaded stream, not ended: the premature abort is to be held");
    CHECK(static_cast<int32_t>(mem.Read32(kCtx + layout.total)) == layout.play_to_eof_total,
          "cadence: completion gate still held after the front load");

    // The player asks once per tick until the stream has genuinely ended.
    for (int guard = 0; guard < 2000 && !api.IsEnd(kCtx); ++guard, now += 2) {
        const MpegApi::PictureResult r = api.GetPicture(kCtx, kImage, now);
        if (r.served) ++served;
        else if (!api.IsEnd(kCtx)) ++nopic_before_end;
    }
    CHECK(api.IsEnd(kCtx), "cadence: the stream ended");
    CHECK(!api.ShouldHoldAbort(kCtx), "cadence: no abort hold once the stream has ended");
    CHECK(served == kGoldenServedMovie1, "cadence: served == the OLD golden window of movie 1 (201)");
    CHECK(nopic_before_end == 0, "cadence: every ask before the end was answered with a picture");
    CHECK(mem.Read32(kInner) == 1, "cadence: inner status 1 at the end");
    CHECK(api.Width(kCtx) == 640 && api.Height(kCtx) == 480, "cadence: still 640x480");
    std::printf("mpeg_api cadence: VIVENDI.PSS windows=%u served=%u nopic_before_end=%u\n", windows, served,
                nopic_before_end);
    return 1;
}

// MOV3's owned output must not book a second delivery or move the PCM gate.
// Compare actual independent providers over the same input, including an
// abort/reopen on the carried clock and complete EOF of the second playback.
static void TestOwnedPictureDelivery(const std::string& root) {
    constexpr uint32_t ctx = 0x1000, ring = 0x40000, window = 0x300000, image = 0x100000;
    const auto layout = TestLayout();
    IpuSettings settings;
    VectorGuestMemory retained(4u << 20), owned(4u << 20);
    FileFmvSectorSource retained_source, owned_source;
    CountingFmvPcmSink retained_pcm, owned_pcm;
    MpegApi a(retained, layout, retained_source, retained_pcm, MakeFfmpegDecoder, settings);
    MpegApi b(owned, layout, owned_source, owned_pcm, MakeFfmpegDecoder, settings);
    a.Init(); b.Init();
    for (auto* memory : {&retained, &owned}) {
        memory->Write32(ctx + layout.ring_base, ring);
        memory->Write32(ctx + layout.ring_size, 0x40000);
        memory->Write32(ctx + layout.inner_status_ptr, 0x2000);
    }
    int64_t now = 100;
    for (int playback = 0; playback < 2; ++playback) {
        const std::string path = root + "/game_files/example_title.rep/FMV/" +
                                 (playback == 0 ? "VIVENDI.PSS" : "TTIDENT.PSS");
        if (!retained_source.Open(path) || !owned_source.Open(path)) {
            std::printf("SKIP: owned picture input %s\n", path.c_str());
            return;
        }
        a.Reset(ctx); b.Reset(ctx);
        for (auto* memory : {&retained, &owned}) {
            memory->Write32(ctx + layout.ring_write, ring + 0x40);
            memory->Write32(ctx + layout.ring_read, ring);
            memory->Write32(ctx + layout.ring_anchor, ring + 0x40);
        }
        auto feed = [&](MpegApi& api, VectorGuestMemory& memory, FileFmvSectorSource& source) {
            const auto sectors = source.PumpSectors(memory.Translate(window, 16384), 8);
            api.DemuxPss(ctx, window, static_cast<int32_t>(sectors * 2048));
        };
        feed(a, retained, retained_source); feed(b, owned, owned_source);
        unsigned served = 0;
        for (unsigned call = 0; call < (playback == 0 ? 14u : 500u); ++call) {
            DecodedPicture picture;
            const auto old = a.GetPicture(ctx, image, now);
            const auto selected = b.GetPictureRgba(ctx, picture, now);
            CHECK(old.served == selected.served && old.wait_ticks == selected.wait_ticks &&
                  old.late == selected.late, "owned output preserves every delivery booking");
            CHECK(a.IsEnd(ctx) == b.IsEnd(ctx), "owned output preserves finished state");
            CHECK(retained_pcm.samples == owned_pcm.samples &&
                  retained_pcm.gate_opens == owned_pcm.gate_opens &&
                  retained_pcm.flushes == owned_pcm.flushes, "owned output preserves PCM bytes/lifetime");
            CHECK(retained_source.SectorsRead() == owned_source.SectorsRead(),
                  "owned output preserves shared-cursor read-ahead");
            if (!old.served) {
                CHECK(picture.width == 0 && picture.height == 0 && picture.rgba.empty(),
                      "EOF never reuses stale picture bytes");
                break;
            }
            ++served;
            std::vector<uint8_t> strips(FrameWriter::Rgb32Bytes(picture.width, picture.height));
            FrameWriter::WriteRgb32Strips(strips.data(), picture);
            CHECK(std::equal(strips.begin(), strips.end(), retained.Translate(image, strips.size())),
                  "owned decoder bytes convert to exactly the retained RGB32 plane");
            CHECK(owned.Read32(image) == 0, "owned output never writes a guest picture buffer");
            now += old.wait_ticks;
            if (call % 11 == 0) now += 7; // late catch-up; other serves use the booked grid
        }
        CHECK(playback == 0 ? served == 14u : (served > 300 && a.IsEnd(ctx) && b.IsEnd(ctx)),
              "same retained picture count through abort/reopen and true EOF");
        std::printf("mpeg_api owned: playback=%d pictures=%u\n", playback, served);
    }
    a.Destroy(ctx); b.Destroy(ctx);
    CHECK(retained_pcm.flushes == owned_pcm.flushes, "owned close cuts the same PCM lifetime");
}

int main(int argc, char** argv) {
    std::printf("avcodec_version=%s (0x%08x)\n", FfmpegVideoDecoder::LibraryVersion().c_str(),
                FfmpegVideoDecoder::LibavcodecVersion());
    TestLifecycleEmpty();
    TestGetPictureUnknownCtx();
    TestGuestWordHelpers();

    int old_ran = 0;
    if (argc >= 2 && argv[1][0] != '\0') {
        old_ran = TestOldMovie(argv[1]);
        old_ran |= TestOldMovieTitleCadence(argv[1]);
        TestOwnedPictureDelivery(argv[1]);
    } else {
        std::printf("SKIP: OLD-gated half (no OLD root argument)\n");
    }

    std::printf("test_mpeg_api: %d/%d checks passed\n", g_checks - g_failed, g_checks);
    if (g_failed) {
        std::printf("test_mpeg_api: FAIL\n");
        return 1;
    }
    std::printf("test_mpeg_api: PASS (lifecycle, unknown ctx, alalc/clear-ref%s)\n",
                old_ran ? ", OLD movie: dims, ring mirror, strips, pacer, audio, reset keeps grid, title cadence 201" : "");
    return 0;
}
