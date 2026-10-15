// tests/ipu/test_libmpeg_bindings.cpp -- LibmpegBindings / LibipuBindings: the
// libmpeg.a and libipu.a manifest rows over a VectorGuestMemory, a recording
// DMAC face, the real IpuDevice and a MpegApi.
//
//   (a) Bind registers 15 libmpeg + 4 libipu rows; every one resolves to an
//       engine handler against the title's sdk_bindings.tsv.
//   (b) register mapping: sceMpegInit clears CHCR.STR and QWC on ch3/ch4 and
//       answers v0 = 1; _sceMpegAlalcInit/_sceMpegAlalcAlloc (a1..a3) v0;
//       sceMpegClearRefBuff v0 = 1; sceMpegDemuxPss (a0..a2) v0 = bytes
//       consumed; sceMpegDemuxPssRing reads its 5th argument from $t0 (the
//       EE SCE ABI, decision DP-11; the stack word is ignored); sceMpegGetPictureRAW8 with
//       no movie (timers pump, v0 = -1, no park, nopic census); sceIpuStopDMA /
//       sceIpuRestartDMA nine words at a0+0x00..0x20 and the a0 == 0 return.
//   (c) over FMV/VIVENDI.PSS under the OLD tree: a demux window then
//       sceMpegGetPicture serves (v0 = 0, 640x480, one timers pump per call);
//       then sceMpegGetPictureRAW8 through the four `presented/plane_select`
//       arms -- the PSMCT16 strips land in plane0 vs plane1 (the other plane
//       untouched, v0 = the mb count) -- and the park arm (a held tick after
//       the pacer is armed parks once per call for the grid's wait). The
//       picture pin against the golden manifest is the FmvPictureOracle
//       ctest's. Prints SKIP and exits 77 without the movie.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ipu/hle/ffmpeg_video_decoder.h"
#include "engine/ipu/hle/fmv_context_layout.h"
#include "engine/ipu/ipu_settings.h"
#include "engine/ipu/hle/libipu_api.h"
#include "engine/ipu/hle/libipu_bindings.h"
#include "engine/ipu/hle/libmpeg_bindings.h"
#include "engine/ipu/hle/mpeg_api.h"
#include "engine/ipu/ipu_device.h"
#include "counting_fmv_pcm_sink.h"
#include "file_fmv_sector_source.h"
#include "recording_dmac_channel_face.h"
#include "vector_guest_memory.h"

using ps2r::ipu::FfmpegVideoDecoder;
using ps2r::ipu::FmvContextLayout;
using ps2r::ipu::IpuDevice;
using ps2r::ipu::IpuSettings;
using ps2r::ipu::LibipuApi;
using ps2r::ipu::LibipuBindings;
using ps2r::ipu::LibmpegBindings;
using ps2r::ipu::MpegApi;
using ps2r::ipu::RecordingDmacChannelFace;
using ps2r::ipu::VideoDecoder;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, what)                                                                    \
    do {                                                                                     \
        ++g_checks;                                                                          \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d: %s -- %s\n", __FILE__, __LINE__, #cond, what); \
            ++g_failures;                                                                    \
        }                                                                                    \
    } while (0)

namespace {

// Synthetic context layout (the real offsets are title data; the words live in
// the vector memory at these offsets from the context base).
FmvContextLayout TestLayout() {
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

std::unique_ptr<VideoDecoder> MakeFfmpegDecoder() { return std::make_unique<FfmpegVideoDecoder>(); }

// Everything one binding set needs, in dependency order (plain data: Wire() hooks it up).
struct Rig {
    VectorGuestMemory mem{4u << 20};
    RecordingDmacChannelFace dmac;
    IpuDevice ipu;
    LibipuApi ipu_api{ipu, dmac};
    FileFmvSectorSource src;
    CountingFmvPcmSink sink;
    IpuSettings settings;
    FmvContextLayout layout = TestLayout();
    MpegApi api{mem, layout, src, sink, MakeFfmpegDecoder, settings};
    std::vector<int64_t> parks;
    int64_t now = 1000;
    LibmpegBindings::Hooks hooks;
    std::unique_ptr<LibmpegBindings> mpeg;
    LibipuBindings libipu{ipu_api, mem};
    int timer_pumps = 0;
};

// Wires the hooks to the rig's counters and builds the bindings over it.
void Wire(Rig& r) {
    r.hooks.pump_timers = [&r](CpuContext&) { ++r.timer_pumps; };
    r.hooks.now_tick = [&r] { return r.now; };
    r.hooks.park = [&r](int64_t t) { r.parks.push_back(t); };
    r.mpeg = std::make_unique<LibmpegBindings>(r.api, r.ipu_api, r.dmac, r.mem, r.layout, r.hooks);
}

CpuContext MakeCtx() {
    CpuContext ctx{};
    std::memset(&ctx, 0, sizeof(ctx));
    ctx.cpuRegs.GPR.r[29].UL[0] = 0x3f0000;  // sp inside the vector memory
    return ctx;
}
void SetArg(CpuContext& ctx, int i, uint32_t v) { ctx.cpuRegs.GPR.r[4 + i].UL[0] = v; }
int32_t V0(const CpuContext& ctx) { return ctx.cpuRegs.GPR.r[2].SL[0]; }

// ---------------------------------------------------------------------------
// (a) Bind + Resolve against the title TSV
// ---------------------------------------------------------------------------
void TestBindResolve(const std::string& tsv) {
    Rig rig;
    Wire(rig);
    SdkBindingManifest& m = SdkBindingManifest::Instance();
    m.ResetForTest();
    CHECK(LibmpegBindings::Bind(m, rig.mpeg.get()) == 15, "Bind registers 15 libmpeg.a rows");
    CHECK(LibipuBindings::Bind(m, &rig.libipu) == 4, "Bind registers 4 libipu.a rows");
    std::vector<SdkBindingManifest::TitleRow> rows;
    std::string err;
    CHECK(SdkBindingManifest::LoadTitleManifest(tsv, rows, &err), ("title TSV loads: " + err).c_str());
    int libmpeg = 0, libipu = 0, libmpeg_rows = 0, libipu_rows = 0;
    for (const auto& r : m.Resolve(rows)) {
        if (r.title.library == "libmpeg.a") ++libmpeg_rows;
        if (r.title.library == "libipu.a") ++libipu_rows;
        if (!r.engine) continue;
        if (r.engine->library == "libmpeg.a") ++libmpeg;
        if (r.engine->library == "libipu.a") ++libipu;
    }
    std::printf("  title rows: libmpeg.a %d (engine %d), libipu.a %d (engine %d)\n", libmpeg_rows, libmpeg, libipu_rows,
                libipu);
    CHECK(libmpeg == 15, "every libmpeg.a engine row resolves to a title row");
    CHECK(libipu == 4, "every libipu.a engine row resolves to a title row");
    CHECK(libipu_rows == 5, "libipu.a has 5 title rows (setD3_CHCR stays a trap row)");
}

// ---------------------------------------------------------------------------
// (b) register mapping over the fakes
// ---------------------------------------------------------------------------
void TestInitRow() {
    Rig rig;
    Wire(rig);
    for (uint32_t ch : {3u, 4u}) {
        rig.dmac.Channel(ch).chcr = LibmpegBindings::kChcrStr | 0x4D;  // STR + DIR/MOD/TTE bits
        rig.dmac.Channel(ch).qwc = 0x40;
    }
    rig.dmac.Channel(2).chcr = LibmpegBindings::kChcrStr;  // another channel: untouched
    rig.dmac.Channel(2).qwc = 7;
    CpuContext ctx = MakeCtx();
    ctx.cpuRegs.GPR.r[2].UL[0] = 0xdeadbeef;
    rig.mpeg->SceMpegInit(ctx);
    CHECK(rig.mpeg->RetainedInitCalls() == 1, "actual retained initialization is counted");
    CHECK(V0(ctx) == 1, "sceMpegInit v0 = 1");
    for (uint32_t ch : {3u, 4u}) {
        CHECK((rig.dmac.Channel(ch).chcr & LibmpegBindings::kChcrStr) == 0, "STR cleared on ch3/ch4");
        CHECK((rig.dmac.Channel(ch).chcr & 0x4D) == 0x4D, "the other CHCR bits survive");
        CHECK(rig.dmac.Channel(ch).qwc == 0, "QWC zeroed on ch3/ch4");
    }
    CHECK(rig.dmac.Channel(2).chcr == LibmpegBindings::kChcrStr && rig.dmac.Channel(2).qwc == 7, "ch2 untouched");
    CHECK(rig.libipu.InitCalls() == 0, "sceMpegInit reaches LibipuApi::Init directly (not the sceIpuInit row)");
    CHECK(rig.mpeg->Census().find("inits=1") != std::string::npos, "census counts the init");
}

void TestSoftwareInitialization() {
    Rig rig;
    Wire(rig);
    rig.mpeg = std::make_unique<LibmpegBindings>(rig.api, rig.mem, rig.layout, rig.hooks);
    for (uint32_t ch : {3u, 4u}) {
        rig.dmac.Channel(ch).chcr = LibmpegBindings::kChcrStr | 0x4D;
        rig.dmac.Channel(ch).qwc = 0x40;
    }
    const uint8_t pending[] = {0x12, 0x34, 0x56, 0x78};
    rig.ipu.FeedInputFifo(pending, sizeof(pending));
    const uint32_t control = rig.ipu.ReadCtrl();
    const int16_t samples[] = {12, -12, 34, -34};
    rig.sink.Push(samples, 2);
    rig.mpeg->InitializePlayback();
    CHECK(rig.sink.flushes == 1, "new software session requests a flush of old movie audio");
    CHECK(rig.mpeg->Census().find("inits=1") != std::string::npos, "software initialization is counted");
    CHECK(rig.dmac.Log().empty(), "software initialization makes no DMA channel access");
    CHECK(rig.ipu.InputSize() == sizeof(pending) && rig.ipu.ReadCtrl() == control,
          "software initialization leaves unrelated device state untouched");
    for (uint8_t code = 0; code < 16; ++code)
        CHECK(rig.ipu.CommandCount(code) == 0, "software initialization issues no IPU command");
    CpuContext context = MakeCtx();
    context.cpuRegs.GPR.r[2].UL[0] = 0x12345678;
    const auto memory = rig.mem.Bytes();
    const auto flushes = rig.sink.flushes;
    bool rejected = false;
    try { rig.mpeg->SceMpegInit(context); } catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected && rig.sink.flushes == flushes && rig.mem.Bytes() == memory &&
          context.cpuRegs.GPR.r[2].UL[0] == 0x12345678 && rig.mpeg->RetainedInitCalls() == 0,
          "unexpected old initialization rejects before software/PCM/guest mutation");
    CHECK(rig.dmac.Log().empty(), "software provider has no hidden hardware initialization fallback");
}

void TestAlalcRows() {
    Rig rig;
    Wire(rig);
    constexpr uint32_t kAlalc = 0x5000;
    CpuContext ctx = MakeCtx();
    SetArg(ctx, 0, kAlalc);
    SetArg(ctx, 1, 0x10000);
    SetArg(ctx, 2, 0x1000);
    rig.mpeg->AlalcInit(ctx);
    CHECK(rig.mem.Read32(kAlalc + rig.layout.alalc_base) == 0x10000, "_sceMpegAlalcInit(alalc, base, n): base");
    CHECK(rig.mem.Read32(kAlalc + rig.layout.alalc_capacity) == 0x1000, "capacity");
    // _sceMpegAlalcAlloc(mpeg, alalc, size, align): a1 is the allocator.
    ctx = MakeCtx();
    SetArg(ctx, 0, 0x7777);  // mpeg (unused by the allocator)
    SetArg(ctx, 1, kAlalc);
    SetArg(ctx, 2, 0x30);
    SetArg(ctx, 3, 16);
    rig.mpeg->AlalcAlloc(ctx);
    CHECK(V0(ctx) == 0x10000, "_sceMpegAlalcAlloc v0 = the first block at base");
    CHECK(rig.mem.Read32(kAlalc + rig.layout.alalc_watermark) == 0x10030, "watermark advanced by size");
    // sceMpegClearRefBuff(mpeg): v0 = 1 (empty inner block: nothing to clear).
    ctx = MakeCtx();
    SetArg(ctx, 0, 0x1000);
    rig.mpeg->SceMpegClearRefBuff(ctx);
    CHECK(V0(ctx) == 1, "sceMpegClearRefBuff v0 = 1");
}

void TestDemuxRows() {
    Rig rig;
    Wire(rig);
    constexpr uint32_t kCtx = 0x1000, kData = 0x20000;
    // A PSS pack header prefix: 00 00 01 BA ... -- enough for the demuxer to accept bytes.
    const uint8_t pack[16] = {0, 0, 1, 0xBA, 0x44, 0, 4, 0, 4, 1, 0x01, 0x89, 0xC3, 0xF8, 0, 0};
    uint8_t* p = rig.mem.Translate(kData, sizeof(pack));
    std::memcpy(p, pack, sizeof(pack));
    CpuContext ctx = MakeCtx();
    SetArg(ctx, 0, kCtx);
    SetArg(ctx, 1, kData);
    SetArg(ctx, 2, static_cast<uint32_t>(sizeof(pack)));
    rig.mpeg->SceMpegDemuxPss(ctx);
    CHECK(V0(ctx) == static_cast<int32_t>(sizeof(pack)), "sceMpegDemuxPss v0 = bytes consumed from (a1, a2)");
    CHECK(rig.mpeg->Census().find("demux=1") != std::string::npos, "census counts the demux call");
    // Zero size: nothing consumed.
    SetArg(ctx, 2, 0);
    rig.mpeg->SceMpegDemuxPss(ctx);
    CHECK(V0(ctx) == 0, "sceMpegDemuxPss size 0 -> v0 = 0");

    // sceMpegDemuxPssRing(mpeg, data, n, ring_base, ring_size): the 5th argument
    // is read from $t0 (decision DP-11, the EE SCE ABI's 5th-argument register);
    // the o32 stack slot [sp+0x10] is NOT consulted. A ring that wraps right
    // after `data` needs ring_size to copy the tail from ring_base.
    constexpr uint32_t kRing = 0x30000, kRingSize = 0x40;
    uint8_t* ring = rig.mem.Translate(kRing, kRingSize);
    std::memcpy(ring + kRingSize - 8, pack, 8);
    std::memcpy(ring, pack + 8, 8);
    ctx = MakeCtx();
    SetArg(ctx, 0, kCtx);
    SetArg(ctx, 1, kRing + kRingSize - 8);
    SetArg(ctx, 2, 16);
    SetArg(ctx, 3, kRing);
    rig.mem.Write32(ctx.cpuRegs.GPR.r[29].UL[0] + 0x10, 0);  // the o32 stack slot deliberately empty
    ctx.cpuRegs.GPR.r[4 + LibmpegBindings::kFifthArgRegisterIndex].UL[0] = kRingSize;  // $t0 carries ring_size
    CHECK(4 + LibmpegBindings::kFifthArgRegisterIndex == 8, "the 5th argument register is $t0 (r8)");
    rig.mpeg->SceMpegDemuxPssRing(ctx);
    CHECK(V0(ctx) == 16, "sceMpegDemuxPssRing consumed 16 bytes across the wrap using $t0 as ring_size");
}

// The driver's double-buffered plane rule (libmpeg_bindings.cpp PumpAndServe):
// presented even -> plane0; presented odd -> plane_select != 0 ? plane1 : plane0.
struct PlaneArm {
    uint32_t presented, select, expect_plane;
    const char* what;
};
const PlaneArm kPlaneArms[] = {
    {0, 1, 0, "presented even -> plane0 regardless of plane_select"},
    {1, 0, 0, "presented odd, plane_select 0 -> plane0"},
    {1, 1, 1, "presented odd, plane_select != 0 -> plane1"},
    {3, 7, 1, "presented odd (bit 0), plane_select != 0 -> plane1"},
};

void TestRaw8NoMovie() {
    // The miss path of sceMpegGetPictureRAW8: without a movie the serve fails
    // (v0 = -1, census nopic), the timers still pump once per call, and the park
    // hook is not reached at wait_ticks == 0. The plane words are read but no
    // picture exists to land in either plane, so the plane CHOICE is not
    // observable here -- TestServe pins it over the movie.
    Rig rig;
    Wire(rig);
    constexpr uint32_t kCtx = 0x1000;
    const FmvContextLayout& l = rig.layout;
    rig.mem.Write32(kCtx + l.target_w, 640);
    rig.mem.Write32(kCtx + l.target_h, 480);
    rig.mem.Write32(kCtx + l.plane0, 0x100000);
    rig.mem.Write32(kCtx + l.plane1, 0x200000);
    for (const PlaneArm& a : kPlaneArms) {
        rig.mem.Write32(kCtx + l.presented, a.presented);
        rig.mem.Write32(kCtx + l.plane_select, a.select);
        CpuContext ctx = MakeCtx();
        SetArg(ctx, 0, kCtx);
        SetArg(ctx, 1, 0x300000);
        SetArg(ctx, 2, 1200);  // mb count
        const int pumps_before = rig.timer_pumps;
        rig.mpeg->SceMpegGetPictureRaw8(ctx);
        CHECK(rig.timer_pumps == pumps_before + 1, "the timers pump runs before every GetPicture*");
        CHECK(V0(ctx) == -1, (std::string("no movie: RAW8 v0 = -1 (") + a.what + ")").c_str());
    }
    CHECK(rig.mem.Read32(0x100000) == 0 && rig.mem.Read32(0x200000) == 0, "no movie: neither plane is written");
    CHECK(rig.parks.empty(), "no park when wait_ticks == 0");
    CHECK(rig.mpeg->Census().find("nopic=4") != std::string::npos, "census counts the four misses");
}

void TestLibipuRows() {
    Rig rig;
    Wire(rig);
    constexpr uint32_t kSave = 0x6000;
    // Stage the DMAC words the snapshot reads.
    rig.dmac.Channel(4).madr = 0x11111110;
    rig.dmac.Channel(4).tadr = 0x22222220;
    rig.dmac.Channel(4).qwc = 0x33;
    rig.dmac.Channel(4).chcr = 0x100 | 0x44;  // STR set: the snapshot stores it cleared
    rig.dmac.Channel(3).madr = 0x55555550;
    rig.dmac.Channel(3).qwc = 0x66;
    rig.dmac.Channel(3).chcr = 0x100 | 0x77;
    for (uint32_t i = 0; i < 9; ++i) rig.mem.Write32(kSave + i * 4, 0xeeeeeeee);

    // a0 == 0: nothing touched, the call still counts.
    CpuContext ctx = MakeCtx();
    SetArg(ctx, 0, 0);
    rig.libipu.SceIpuStopDma(ctx);
    CHECK(rig.libipu.StopCalls() == 1, "sceIpuStopDMA(0) counted");
    CHECK(rig.mem.Read32(kSave) == 0xeeeeeeee && (rig.dmac.Channel(4).chcr & 0x100) != 0,
          "sceIpuStopDMA(0) touches neither memory nor the channels");
    rig.libipu.SceIpuRestartDma(ctx);
    CHECK(rig.libipu.RestartCalls() == 1, "sceIpuRestartDMA(0) counted and returns");

    SetArg(ctx, 0, kSave);
    rig.libipu.SceIpuStopDma(ctx);
    // Words [7] and [8] are the unit's IPU_BP and IPU_CTRL; on the idle device
    // both readbacks are stable, so the stored words must equal them (and differ
    // from the 0xeeeeeeee pre-fill).
    const uint32_t expect[9] = {0x11111110, 0x22222220, 0x33, 0x44, 0x55555550, 0x66, 0x77,
                                rig.ipu.ReadBp(), rig.ipu.ReadCtrl()};
    for (uint32_t i = 0; i < 9; ++i) {
        char what[64];
        std::snprintf(what, sizeof(what), "snapshot word [%u] at a0+0x%02x", i, i * 4);
        CHECK(rig.mem.Read32(kSave + i * 4) == expect[i], what);
    }
    CHECK(rig.mem.Read32(kSave + 7 * 4) != 0xeeeeeeee && rig.mem.Read32(kSave + 8 * 4) != 0xeeeeeeee,
          "IPU_BP / IPU_CTRL words were written");
    CHECK(rig.mem.Read32(kSave + 9 * 4) == 0, "the tenth word is not written");
    CHECK((rig.dmac.Channel(3).chcr & 0x100) == 0 && (rig.dmac.Channel(4).chcr & 0x100) == 0, "STR cleared on both channels");

    // Mutate the live channels, restart from the saved words: they come back.
    rig.dmac.Channel(4).madr = 1;
    rig.dmac.Channel(3).madr = 2;
    rig.dmac.Channel(4).qwc = 0;
    rig.dmac.Channel(3).qwc = 0;
    rig.libipu.SceIpuRestartDma(ctx);
    CHECK(rig.dmac.Channel(4).madr == 0x11111110 && rig.dmac.Channel(3).madr == 0x55555550, "MADR replayed from a0");
    CHECK(rig.dmac.Channel(4).qwc == 0x33 && rig.dmac.Channel(3).qwc == 0x66, "QWC replayed from a0");

    // sceIpuSync(mode) -> v0 = 0 on an idle unit; sceIpuInit counted.
    ctx = MakeCtx();
    SetArg(ctx, 0, 1);
    rig.libipu.SceIpuSync(ctx);
    CHECK(V0(ctx) == 0, "sceIpuSync v0 = 0 (idle)");
    rig.libipu.SceIpuInit(ctx);
    CHECK(rig.libipu.InitCalls() == 1 && rig.libipu.SyncCalls() == 1, "init/sync counters");
}

// ---------------------------------------------------------------------------
// (c) GetPicture over the first intro movie
// ---------------------------------------------------------------------------
int TestServe(const std::string& og_root) {
    const std::string path = og_root + "/game_files/example_title.rep/FMV/VIVENDI.PSS";
    Rig rig;
    Wire(rig);
    if (!rig.src.Open(path)) {
        std::printf("SKIP: OLD movie not readable: %s\n", path.c_str());
        return 77;
    }
    constexpr uint32_t kCtx = 0x1000, kInner = 0x2000, kRingBase = 0x40000, kRingSize = 0x40000, kImage = 0x100000;
    // Two PSMCT16 planes of 640x480 (0x96000 bytes each) after the RGB32 image.
    constexpr uint32_t kPlane[2] = {0x230000, 0x2d0000};
    constexpr uint32_t kWindow = 0x370000, kWindowSectors = 16;
    const FmvContextLayout& l = rig.layout;
    rig.api.Init();
    // The HLE decode boundary must not issue hardware decode/conversion
    // commands or disturb a separately staged bitstream. In particular RAW8
    // currently writes final RGB16 planes rather than feeding IPU CSC.
    const uint8_t sentinel[] = {0x89, 0xab, 0xcd, 0xef, 0x42, 0, 0, 0};
    rig.ipu.FeedInputFifo(sentinel, sizeof(sentinel));
    rig.ipu.WriteCmd(0x40000000u);
    rig.ipu.WriteCmd(0x90100008u);
    const uint32_t ipu_ctrl_before = rig.ipu.ReadCtrl();
    rig.mem.Write32(kCtx + l.ring_base, kRingBase);
    rig.mem.Write32(kCtx + l.ring_size, kRingSize);
    rig.mem.Write32(kCtx + l.ring_write, kRingBase + 0x40);
    rig.mem.Write32(kCtx + l.ring_read, kRingBase);
    rig.mem.Write32(kCtx + l.ring_anchor, kRingBase + 0x40);
    rig.mem.Write32(kCtx + l.inner_status_ptr, kInner);
    rig.mem.Write32(kCtx + l.mode, l.mode_raw8);
    rig.mem.Write32(kCtx + l.target_w, 640);
    rig.mem.Write32(kCtx + l.target_h, 480);
    rig.mem.Write32(kCtx + l.plane0, kPlane[0]);
    rig.mem.Write32(kCtx + l.plane1, kPlane[1]);

    // The load loop's demux window through the row.
    uint8_t* win = rig.mem.Translate(kWindow, kWindowSectors * FileFmvSectorSource::kSectorBytes);
    CHECK(rig.src.PumpSectors(win, kWindowSectors) == kWindowSectors, "first window read");
    CpuContext ctx = MakeCtx();
    SetArg(ctx, 0, kCtx);
    SetArg(ctx, 1, kWindow);
    SetArg(ctx, 2, kWindowSectors * 2048u);
    rig.mpeg->SceMpegDemuxPss(ctx);
    CHECK(V0(ctx) == static_cast<int32_t>(kWindowSectors * 2048u), "DemuxPss row consumed the window");

    int served = 0;
    for (int call = 0; call < 5; ++call, rig.now += 2) {
        ctx = MakeCtx();
        SetArg(ctx, 0, kCtx);
        SetArg(ctx, 1, kImage);
        rig.mpeg->SceMpegGetPicture(ctx);
        if (V0(ctx) == 0) ++served;
    }
    CHECK(served == 5, "every sceMpegGetPicture served (v0 = 0): the pump blocks until a picture exists");
    CHECK(rig.api.Width(kCtx) == 640 && rig.api.Height(kCtx) == 480, "first movie is 640x480");
    CHECK(rig.timer_pumps == 5, "one timers pump per GetPicture");

    // RAW8 plane select over real pictures. Every PSMCT16 pixel the writer
    // stores carries the 0x8000 alpha bit, so after a serve every odd byte of
    // the chosen plane has bit 7 set while the other plane (pre-zeroed) stays
    // all zero -- a wrong plane choice fails both checks.
    constexpr size_t kPlaneBytes = 640u * 480u * 2u;
    auto zero_planes = [&] {
        for (uint32_t base : kPlane) std::memset(rig.mem.Translate(base, kPlaneBytes), 0, kPlaneBytes);
    };
    auto plane_written = [&](uint32_t base) {
        const uint8_t* p = rig.mem.Translate(base, kPlaneBytes);
        for (size_t i = 1; i < kPlaneBytes; i += 2)
            if ((p[i] & 0x80) == 0) return false;
        return true;
    };
    auto plane_zero = [&](uint32_t base) {
        const uint8_t* p = rig.mem.Translate(base, kPlaneBytes);
        for (size_t i = 0; i < kPlaneBytes; ++i)
            if (p[i] != 0) return false;
        return true;
    };
    // The pacer is armed (5 serves); holding `now` across the four arms makes
    // every arm after the first wait one more period: the park hook must see
    // exactly one park per held call, the values an arithmetic progression.
    const size_t parks_before = rig.parks.size();
    int arm_index = 0;
    for (const PlaneArm& a : kPlaneArms) {
        zero_planes();
        rig.mem.Write32(kCtx + l.presented, a.presented);
        rig.mem.Write32(kCtx + l.plane_select, a.select);
        ctx = MakeCtx();
        SetArg(ctx, 0, kCtx);
        SetArg(ctx, 1, 0);     // the RAW8 row ignores a1: the plane comes from the context words
        SetArg(ctx, 2, 1200);  // mb count = 40 x 30
        const size_t parks_at_call = rig.parks.size();
        rig.mpeg->SceMpegGetPictureRaw8(ctx);
        CHECK(V0(ctx) == 1200, (std::string("RAW8 v0 = mb count (") + a.what + ")").c_str());
        CHECK(plane_written(kPlane[a.expect_plane]), (std::string("picture landed in the chosen plane (") + a.what + ")").c_str());
        CHECK(plane_zero(kPlane[1 - a.expect_plane]), (std::string("the other plane is untouched (") + a.what + ")").c_str());
        if (arm_index > 0) CHECK(rig.parks.size() == parks_at_call + 1, "a held tick parks exactly once per RAW8 call");
        ++arm_index;
    }
    CHECK(rig.parks.size() >= parks_before + 3, "at least the three held arms parked");
    for (size_t i = parks_before; i < rig.parks.size(); ++i) CHECK(rig.parks[i] > 0, "the park hook only receives wait_ticks > 0");
    if (rig.parks.size() >= parks_before + 3) {
        const size_t n = rig.parks.size();
        const int64_t step = rig.parks[n - 1] - rig.parks[n - 2];
        CHECK(step > 0 && rig.parks[n - 2] - rig.parks[n - 3] == step, "held-tick parks grow by one pacer period per call");
    }
    uint64_t park_sum = 0;
    for (int64_t t : rig.parks) park_sum += static_cast<uint64_t>(t);
    const std::string census = rig.mpeg->Census();
    std::printf("  census: %s parks=%zu served_per_playback=%s\n", census.c_str(), rig.parks.size(),
                rig.mpeg->ServedPerPlayback().c_str());
    CHECK(census.find("pictures=5") != std::string::npos, "census pictures=5");
    CHECK(census.find("raw8=4") != std::string::npos, "census raw8=4");
    CHECK(census.find("served_total=9") != std::string::npos, "census served_total=9");
    CHECK(census.find("late=0") != std::string::npos, "no late serve: the grid was never overrun");
    CHECK(census.find(",parks=" + std::to_string(rig.parks.size()) + ",") != std::string::npos, "census parks = park hook calls");
    CHECK(census.find(",park_ticks=" + std::to_string(park_sum) + ",") != std::string::npos, "census park_ticks = sum of the hook's waits");
    CHECK(rig.mpeg->ServedPerPlayback() == "9+", "open playback reads 9+");
    CHECK(rig.timer_pumps == 9, "one timers pump per GetPicture*");
    for (uint8_t code = 0; code < 16; ++code) {
        CHECK(rig.ipu.CommandCount(code) == ((code == 4 || code == 9) ? 1u : 0u),
              "HLE demux and picture delivery issue no IPU command");
        CHECK(rig.ipu.UnsupportedCommandCount(code) == 0, "HLE bypass reaches no partial hardware decode");
    }
    CHECK(rig.ipu.ReadCmd() == 0x89abcdefu && rig.ipu.ReadTop() == 0x89abcdefu,
          "HLE preserves staged IPU stream and result");
    CHECK(rig.ipu.InputSize() == sizeof(sentinel) && rig.ipu.OutputSize() == 0 &&
              rig.ipu.BitPosition() == 0 && rig.ipu.ReadCtrl() == ipu_ctrl_before,
          "HLE preserves IPU FIFOs, cursor and control");
    CHECK(rig.ipu.Threshold0() == 8 && rig.ipu.Threshold1() == 16, "HLE preserves threshold state");
    const auto access_count = rig.dmac.Log().size();
    rig.mpeg->DestroyPlayback(kCtx);
    CHECK(rig.mpeg->ServedPerPlayback() == "9", "closing the session closes its served-picture record");
    CHECK(!rig.api.ShouldHoldAbort(kCtx), "closing the session releases its decoder state");
    rig.mpeg->DestroyPlayback(kCtx);
    CHECK(rig.mpeg->ServedPerPlayback() == "9", "repeated close does not duplicate the playback record");
    CHECK(rig.dmac.Log().size() == access_count, "closing the software session makes no DMA channel access");
    CHECK(rig.ipu.InputSize() == sizeof(sentinel) && rig.ipu.ReadCtrl() == ipu_ctrl_before,
          "closing the software session leaves unrelated device state untouched");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string tsv = argc > 1 ? argv[1] : "";
    const std::string og_root = argc > 2 ? argv[2] : "";
    if (!tsv.empty()) TestBindResolve(tsv);
    TestInitRow();
    TestSoftwareInitialization();
    TestAlalcRows();
    TestDemuxRows();
    TestRaw8NoMovie();
    TestLibipuRows();
    const int serve = og_root.empty() ? 77 : TestServe(og_root);
    std::printf("LibmpegBindings: %d checks, %d failures%s\n", g_checks, g_failures,
                serve == 77 ? " (serve half SKIPPED: no OLD movie)" : "");
    if (g_failures) return 1;
    return serve == 77 ? 77 : 0;
}
