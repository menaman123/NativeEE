// engine/ipu/hle/libmpeg_bindings.h -- LibmpegBindings: the libmpeg.a SDK rows
// as manifest thunks over ONE MpegApi (the context table, demux, decode, serve)
// plus an optional retained sceMpegInit service for the model arm. The manifest handler
// is a plain function pointer, so Bind installs file-static thunks over the bound
// instance (the libdma / libcdvd rows' shape).
//
// Row mapping (engine/ipu/hle/README.md, "SDK binding rows"):
//   sceMpegInit(void)                 -> MpegApi::Init; DMAC ch3/ch4 CHCR.STR
//                                        cleared and QWC zeroed; sceIpuInit; v0 = 1
//   sceMpegCreate / _clearOnce / _clearEach / sceMpegAddCallback /
//   sceMpegAddStrCallback             -> the recompiled bodies (pure data setup
//                                        over the caller's buffer; Create is
//                                        wrapped only to count)
//   sceMpegReset(mpeg)                -> MpegApi::Reset(a0), THEN the recompiled
//                                        body (the game's reset drops the pictures
//                                        decoded from its pre-fed demux windows)
//   _sceMpegAlalcInit(alalc, base, n) -> MpegApi::AlalcInit
//   _sceMpegAlalcAlloc(mpeg, alalc, n, align) -> v0 = MpegApi::AlalcAlloc
//   sceMpegClearRefBuff(mpeg)         -> MpegApi::ClearRefBuff; v0 = 1
//   _sceMpegError1(mpeg, fmt, arg)    -> logged; void
//   sceMpegDemuxPss(mpeg, data, n)    -> v0 = MpegApi::DemuxPss
//   sceMpegDemuxPssRing(mpeg, data, n, ring, $t0 ring_size) -> v0 = DemuxPssRing
//   sceMpegGetPicture(mpeg, rgb32, mb)-> timers pump, GetPicture(a0, a1, now_tick),
//                                        park wait_ticks; v0 = served ? 0 : -1
//   sceMpegGetPictureRAW8(mpeg, raw8, mb) -> timers pump, plane select from the
//                                        layout's presented / plane_select /
//                                        plane0/1 / target_w/h words, GetPictureRaw8,
//                                        park; v0 = served ? mb : -1
//
// The timers pump runs with the live CpuContext before every GetPicture*: on
// hardware the blocking call lets the TIM0 ISR run, and the game's 9 kHz clock
// only advances when its pending timer events dispatch through a guest context.
// The pacer's wait is parked on the caller-supplied hook, one boundary tick per
// wait entry (a non-consuming tick waiter: the BoundaryProtocol wait path).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "engine/ee/cpu/cpu_state.h"

class SdkBindingManifest;

namespace ps2r::ipu {

class DmacChannelFace;
struct FmvContextLayout;
class GuestMemory;
class LibipuApi;
class MpegApi;
struct DecodedPicture;

class LibmpegBindings {
public:
    static constexpr const char* kLibrary = "libmpeg.a";
    static constexpr uint32_t kChannelIpuFrom = 3;
    static constexpr uint32_t kChannelIpuTo = 4;
    static constexpr uint32_t kChcrStr = 1u << 8;
    // sceMpegDemuxPssRing's fifth argument: $t0, the EE SCE ABI's first
    // register beyond a0..a3 (Arg index 4 counts from a0; decision DP-11).
    static constexpr int kFifthArgRegisterIndex = 4;

    struct Hooks {
        // The EeTimers pump with the live guest context (before every GetPicture*).
        std::function<void(CpuContext&)> pump_timers;
        // The vblank tick counter the pacer's grid is booked against.
        std::function<int64_t()> now_tick;
        // Park the calling guest thread until `ticks` boundaries have passed.
        std::function<void(int64_t ticks)> park;
        // A picture was served (before any pace park); `no_park` = no pace park
        // follows this serve (the deadline had passed -- a late serve -- or fell
        // exactly on `now`, or the serve is a priming one). The host's play mode
        // presents the display here when no park will (hardware shows DISPFB on
        // every vblank; the EE's frame loop does not run during a movie).
        std::function<void(bool no_park)> on_served;
    };

    LibmpegBindings(MpegApi& api, LibipuApi& ipu, DmacChannelFace& dmac, GuestMemory& memory,
                    const FmvContextLayout& layout, Hooks hooks);
    using RetainedInit = std::function<void()>;
    // Movie lifecycle selection needs no IPU/DMAC provider. An unexpected
    // generated sceMpegInit rejects before resetting software playback.
    LibmpegBindings(MpegApi& api, GuestMemory& memory, const FmvContextLayout& layout,
                    Hooks hooks, RetainedInit retained_init = {});
    // Unchanged initialization of the comparison arm, isolated from the
    // software provider. Referenced devices must outlive the binding.
    static RetainedInit RetainedInitialization(LibipuApi&, DmacChannelFace&);

    // Registers every libmpeg.a row with an engine body; returns the count.
    static int Bind(SdkBindingManifest& m, LibmpegBindings* self);

    // Software playback lifecycle for callers that own their decode operation.
    // These methods do not access IPU registers or DMA channels.
    void InitializePlayback();
    // End this movie's pictures/PCM, keeping the context's delivery clock for
    // the next movie. The title reuses that clock across Close/Open just as it
    // does across its two Create/Reset calls during Open.
    void ClosePlayback(uint32_t mpeg_addr);
    // Full context teardown, including its delivery clock. Movie Close should
    // use ClosePlayback; session initialization clears all contexts/clocks.
    void DestroyPlayback(uint32_t mpeg_addr);
    // File refill EOF may precede the last accepted picture. This query also
    // includes compressed backlog awaiting decode; it never touches hardware.
    bool HasPendingPlayback(uint32_t mpeg_addr);
    // Accept a program-stream window through the existing software demux and
    // PCM services. No generated stream callback or hardware command runs.
    int32_t DemuxPlayback(uint32_t mpeg_addr, uint32_t data_addr, int32_t bytes);
    // a0 identifies the playback. Returns the ordinary RGB32 SDK result in
    // v0, with owned row-major pixels. Timer pump, on_served and park ordering
    // are identical to the existing public picture binding.
    void ReadPlaybackPicture(CpuContext& ctx, DecodedPicture& picture);

    void SceMpegInit(CpuContext& ctx);
    bool SceMpegCreate(CpuContext& ctx);   // wrap: counts, lets the body run
    bool SceMpegReset(CpuContext& ctx);    // wrap: MpegApi::Reset, then the body
    void AlalcInit(CpuContext& ctx);
    void AlalcAlloc(CpuContext& ctx);
    void SceMpegClearRefBuff(CpuContext& ctx);
    void SceMpegError1(CpuContext& ctx);
    void SceMpegDemuxPss(CpuContext& ctx);
    void SceMpegDemuxPssRing(CpuContext& ctx);
    void SceMpegGetPicture(CpuContext& ctx);
    void SceMpegGetPictureRaw8(CpuContext& ctx);

    // Census (run.meta `fmv=`): "created=..,reset=..,demux=..,pictures=..,raw8=..,
    // served_total=..,nopic=..,late=..,parks=..,park_ticks=..".
    std::string Census() const;
    // Pictures served per playback, closed by each sceMpegReset / sceMpegInit
    // ("201,346" on the title's cold boot), the open playback last.
    std::string ServedPerPlayback() const;
    uint64_t ServedTotal() const { return served_rgb32_ + served_raw8_; }
    uint64_t RetainedInitCalls() const { return retained_init_calls_; }

private:
    void PumpAndServe(CpuContext& ctx, bool raw8, DecodedPicture* picture = nullptr);
    void RecordPlaybackEnd(uint32_t mpeg_addr);

    MpegApi& api_;
    RetainedInit retained_init_;
    GuestMemory& memory_;
    const FmvContextLayout& layout_;
    Hooks hooks_;
    uint64_t inits_ = 0;
    uint64_t creates_ = 0;
    uint64_t resets_ = 0;
    uint64_t demux_calls_ = 0;
    uint64_t served_rgb32_ = 0;
    uint64_t served_raw8_ = 0;
    uint64_t no_picture_ = 0;
    uint64_t late_ = 0;
    uint64_t parks_ = 0;
    uint64_t park_ticks_ = 0;
    uint64_t errors_ = 0;
    uint64_t retained_init_calls_ = 0;
    std::unordered_map<uint32_t, uint64_t> served_open_;   // per ctx, since its last Reset
    std::string served_closed_;
};

}  // namespace ps2r::ipu
