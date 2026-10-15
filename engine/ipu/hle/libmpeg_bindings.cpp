#include "engine/ipu/hle/libmpeg_bindings.h"

#include <utility>
#include <stdexcept>

#include "engine/core/log/log.h"
#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ipu/hle/fmv_context_layout.h"
#include "engine/ipu/hle/guest_memory.h"
#include "engine/ipu/hle/mpeg_api.h"

namespace ps2r::ipu {

namespace {

LibmpegBindings* g_self = nullptr;

uint32_t Arg(const CpuContext& ctx, int n) { return ctx.cpuRegs.GPR.r[4 + n].UL[0]; }
void Ret(CpuContext& ctx, int32_t v) { ctx.cpuRegs.GPR.r[2].SD[0] = v; }

void ThunkInit(CpuContext& ctx) { if (g_self) g_self->SceMpegInit(ctx); }
bool ThunkCreate(CpuContext& ctx) { return g_self ? g_self->SceMpegCreate(ctx) : false; }
bool ThunkReset(CpuContext& ctx) { return g_self ? g_self->SceMpegReset(ctx) : false; }
void ThunkAlalcInit(CpuContext& ctx) { if (g_self) g_self->AlalcInit(ctx); }
void ThunkAlalcAlloc(CpuContext& ctx) { if (g_self) g_self->AlalcAlloc(ctx); }
void ThunkClearRefBuff(CpuContext& ctx) { if (g_self) g_self->SceMpegClearRefBuff(ctx); }
void ThunkError1(CpuContext& ctx) { if (g_self) g_self->SceMpegError1(ctx); }
void ThunkDemuxPss(CpuContext& ctx) { if (g_self) g_self->SceMpegDemuxPss(ctx); }
void ThunkDemuxPssRing(CpuContext& ctx) { if (g_self) g_self->SceMpegDemuxPssRing(ctx); }
void ThunkGetPicture(CpuContext& ctx) { if (g_self) g_self->SceMpegGetPicture(ctx); }
void ThunkGetPictureRaw8(CpuContext& ctx) { if (g_self) g_self->SceMpegGetPictureRaw8(ctx); }

}  // namespace

LibmpegBindings::LibmpegBindings(MpegApi& api, GuestMemory& memory, const FmvContextLayout& layout,
                                 Hooks hooks, RetainedInit retained_init)
    : api_(api), retained_init_(std::move(retained_init)), memory_(memory), layout_(layout),
      hooks_(std::move(hooks)) {}

int LibmpegBindings::Bind(SdkBindingManifest& m, LibmpegBindings* self) {
    g_self = self;
    m.Register(kLibrary, "sceMpegInit", &ThunkInit);
    m.RegisterWrap(kLibrary, "sceMpegCreate", &ThunkCreate, false);
    m.RegisterWrap(kLibrary, "sceMpegReset", &ThunkReset, false);
    m.RegisterRecompiled(kLibrary, "_clearOnce");
    m.RegisterRecompiled(kLibrary, "_clearEach");
    m.RegisterRecompiled(kLibrary, "sceMpegAddCallback");
    m.RegisterRecompiled(kLibrary, "sceMpegAddStrCallback");
    m.Register(kLibrary, "_sceMpegAlalcInit", &ThunkAlalcInit);
    m.Register(kLibrary, "_sceMpegAlalcAlloc", &ThunkAlalcAlloc);
    m.Register(kLibrary, "sceMpegClearRefBuff", &ThunkClearRefBuff);
    m.Register(kLibrary, "_sceMpegError1", &ThunkError1);
    m.Register(kLibrary, "sceMpegDemuxPss", &ThunkDemuxPss);
    m.Register(kLibrary, "sceMpegDemuxPssRing", &ThunkDemuxPssRing);
    m.Register(kLibrary, "sceMpegGetPicture", &ThunkGetPicture);
    m.Register(kLibrary, "sceMpegGetPictureRAW8", &ThunkGetPictureRaw8);
    return 15;
}

void LibmpegBindings::RecordPlaybackEnd(uint32_t mpeg_addr) {
    auto it = served_open_.find(mpeg_addr);
    if (it == served_open_.end()) return;
    if (it->second != 0) served_closed_ += (served_closed_.empty() ? "" : ",") + std::to_string(it->second);
    served_open_.erase(it);
}

void LibmpegBindings::InitializePlayback() {
    ++inits_;
    for (auto& kv : served_open_)
        if (kv.second != 0) served_closed_ += (served_closed_.empty() ? "" : ",") + std::to_string(kv.second);
    served_open_.clear();
    api_.Init();
}

void LibmpegBindings::ClosePlayback(uint32_t mpeg_addr) {
    RecordPlaybackEnd(mpeg_addr);
    // Reset discards compressed input, codec reference pictures and captured
    // PCM, but preserves the existing tick grid. The retail Delete body did
    // not destroy this software context; the next movie's Reset reused it.
    // Keep that delivery contract without retaining any active movie.
    api_.Reset(mpeg_addr);
}

void LibmpegBindings::DestroyPlayback(uint32_t mpeg_addr) {
    RecordPlaybackEnd(mpeg_addr);
    api_.Destroy(mpeg_addr);
}

bool LibmpegBindings::HasPendingPlayback(uint32_t mpeg_addr) {
    return api_.ShouldHoldAbort(mpeg_addr);
}

int32_t LibmpegBindings::DemuxPlayback(uint32_t mpeg_addr, uint32_t data_addr, int32_t bytes) {
    ++demux_calls_;
    return api_.DemuxPss(mpeg_addr, data_addr, bytes);
}

void LibmpegBindings::SceMpegInit(CpuContext& ctx) {
    if (!retained_init_)
        throw std::runtime_error("Movie software owner reached retired sceMpegInit");
    InitializePlayback();
    ++retained_init_calls_;
    retained_init_();
    Ret(ctx, 1);
}

bool LibmpegBindings::SceMpegCreate(CpuContext& ctx) {
    ++creates_;
    PS2R_LOG(IPU, Info, "sceMpegCreate mpeg=0x%08x", Arg(ctx, 0));
    return false;   // the recompiled body builds the context
}

bool LibmpegBindings::SceMpegReset(CpuContext& ctx) {
    ++resets_;
    const uint32_t mpeg = Arg(ctx, 0);
    PS2R_LOG(IPU, Info, "sceMpegReset mpeg=0x%08x served_this_playback=%llu", mpeg,
             static_cast<unsigned long long>(served_open_.count(mpeg) ? served_open_.at(mpeg) : 0u));
    RecordPlaybackEnd(mpeg);
    api_.Reset(mpeg);
    return false;   // then the recompiled body re-initialises the library state
}

void LibmpegBindings::AlalcInit(CpuContext& ctx) { api_.AlalcInit(Arg(ctx, 0), Arg(ctx, 1), Arg(ctx, 2)); }

void LibmpegBindings::AlalcAlloc(CpuContext& ctx) {
    // a0 is the mpeg context (the library's error-log argument only).
    Ret(ctx, static_cast<int32_t>(api_.AlalcAlloc(Arg(ctx, 1), Arg(ctx, 2), Arg(ctx, 3))));
}

void LibmpegBindings::SceMpegClearRefBuff(CpuContext& ctx) {
    api_.ClearRefBuff(Arg(ctx, 0));
    Ret(ctx, 1);
}

void LibmpegBindings::SceMpegError1(CpuContext& ctx) {
    ++errors_;
    const uint32_t fmt_ptr = Arg(ctx, 1);
    std::string fmt;
    for (uint32_t i = 0; fmt_ptr != 0 && i < 256; i += 4) {
        const uint32_t w = memory_.Read32(fmt_ptr + i);
        bool end = false;
        for (int b = 0; b < 4; ++b) {
            const char c = static_cast<char>((w >> (8 * b)) & 0xFF);
            if (c == 0) { end = true; break; }
            fmt.push_back(c);
        }
        if (end) break;
    }
    PS2R_LOG(IPU, Warn, "_sceMpegError1 mpeg=0x%08x fmt=\"%s\" arg=0x%08x", Arg(ctx, 0), fmt.c_str(), Arg(ctx, 2));
}

void LibmpegBindings::SceMpegDemuxPss(CpuContext& ctx) {
    const uint32_t mpeg = Arg(ctx, 0);
    const int32_t consumed = DemuxPlayback(mpeg, Arg(ctx, 1), static_cast<int32_t>(Arg(ctx, 2)));
    PS2R_LOG(IPU, Info, "sceMpegDemuxPss mpeg=0x%08x data=0x%08x size=%u -> %d total=0x%08x presented=%u abort=%u",
             mpeg, Arg(ctx, 1), Arg(ctx, 2), consumed, memory_.Read32(mpeg + layout_.total),
             memory_.Read32(mpeg + layout_.presented), memory_.Read32(mpeg + layout_.abort_flag));
    Ret(ctx, consumed);
}

void LibmpegBindings::SceMpegDemuxPssRing(CpuContext& ctx) {
    ++demux_calls_;
    // The 5th argument travels in $t0: the EE's SCE calling convention passes
    // arguments 5..8 in $t0..$t3 (the title's own sceMpegDemuxPss wrapper
    // FUN_002bbe90 sets $t0 = -1 before the call and the body's first
    // arithmetic is a3 + t0). The original runtime read it at [sp+0x10], the
    // o32 habit; decision DP-11 (docs/phases/DECISIONS_LOG.md) binds the
    // register read. Not reached on this title today (the game calls the flat
    // sceMpegDemuxPss row).
    const uint32_t ring_size = Arg(ctx, kFifthArgRegisterIndex);
    Ret(ctx, api_.DemuxPssRing(Arg(ctx, 0), Arg(ctx, 1), static_cast<int32_t>(Arg(ctx, 2)), Arg(ctx, 3), ring_size));
}

void LibmpegBindings::PumpAndServe(CpuContext& ctx, bool raw8, DecodedPicture* picture) {
    if (hooks_.pump_timers) hooks_.pump_timers(ctx);
    const uint32_t mpeg = Arg(ctx, 0);
    const int64_t now = hooks_.now_tick ? hooks_.now_tick() : 0;
    MpegApi::PictureResult r;
    if (picture) {
        r = api_.GetPictureRgba(mpeg, *picture, now);
    } else if (!raw8) {
        r = api_.GetPicture(mpeg, Arg(ctx, 1), now);
    } else {
        // The driver's double-buffered plane selection over the context words.
        const uint32_t w = memory_.Read32(mpeg + layout_.target_w);
        const uint32_t h = memory_.Read32(mpeg + layout_.target_h);
        uint32_t idx = 0;
        if (memory_.Read32(mpeg + layout_.presented) & 1u) idx = memory_.Read32(mpeg + layout_.plane_select) != 0 ? 1u : 0u;
        const uint32_t plane = memory_.Read32(mpeg + (idx ? layout_.plane1 : layout_.plane0));
        r = api_.GetPictureRaw8(mpeg, plane, w, h, now);
    }
    // The driver's gate words at this serve (the evidence channel for the
    // serve-window attribution): completion total vs presented, the mode and
    // the scene updater's abort request, and the inner end-of-stream word.
    {
        const uint32_t inner = memory_.Read32(mpeg + layout_.inner_status_ptr);
        PS2R_LOG(IPU, Info,
                 "sceMpegGetPicture%s mpeg=0x%08x -> served=%d wait=%lld late=%d total=0x%08x presented=%u mode=%u "
                 "abort=%u inner0=%u",
                 raw8 ? "RAW8" : "", mpeg, r.served ? 1 : 0, static_cast<long long>(r.wait_ticks), r.late ? 1 : 0,
                 memory_.Read32(mpeg + layout_.total), memory_.Read32(mpeg + layout_.presented),
                 memory_.Read32(mpeg + layout_.mode), memory_.Read32(mpeg + layout_.abort_flag),
                 inner ? memory_.Read32(inner) : 0u);
    }
    if (r.late) ++late_;
    const bool parks = r.wait_ticks > 0 && hooks_.park;
    if (r.served && hooks_.on_served) hooks_.on_served(!parks);
    if (parks) {
        ++parks_;
        park_ticks_ += static_cast<uint64_t>(r.wait_ticks);
        hooks_.park(r.wait_ticks);
    }
    if (r.served) {
        (raw8 ? served_raw8_ : served_rgb32_)++;
        ++served_open_[mpeg];
        Ret(ctx, raw8 ? static_cast<int32_t>(Arg(ctx, 2)) : 0);
    } else {
        ++no_picture_;
        Ret(ctx, -1);
    }
}

void LibmpegBindings::SceMpegGetPicture(CpuContext& ctx) { PumpAndServe(ctx, false); }
void LibmpegBindings::SceMpegGetPictureRaw8(CpuContext& ctx) { PumpAndServe(ctx, true); }
void LibmpegBindings::ReadPlaybackPicture(CpuContext& ctx, DecodedPicture& picture) {
    PumpAndServe(ctx, false, &picture);
}

std::string LibmpegBindings::Census() const {
    return "created=" + std::to_string(creates_) + ",reset=" + std::to_string(resets_) +
           ",demux=" + std::to_string(demux_calls_) + ",pictures=" + std::to_string(served_rgb32_) +
           ",raw8=" + std::to_string(served_raw8_) + ",served_total=" + std::to_string(ServedTotal()) +
           ",nopic=" + std::to_string(no_picture_) + ",late=" + std::to_string(late_) +
           ",parks=" + std::to_string(parks_) + ",park_ticks=" + std::to_string(park_ticks_) +
           ",errors=" + std::to_string(errors_) + ",inits=" + std::to_string(inits_) +
           ",retained_init_calls=" + std::to_string(retained_init_calls_);
}

std::string LibmpegBindings::ServedPerPlayback() const {
    std::string out = served_closed_;
    for (const auto& kv : served_open_)
        if (kv.second != 0) out += (out.empty() ? "" : ",") + std::to_string(kv.second) + "+";
    return out.empty() ? "none" : out;
}

}  // namespace ps2r::ipu
