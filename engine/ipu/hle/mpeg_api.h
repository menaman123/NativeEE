// engine/ipu/hle/mpeg_api.h -- MpegApi: the sceMpeg entry points the SDK bindings
// call, over a GuestMemory face and a caller-supplied FmvContextLayout (the title's
// SceMpeg context offsets). Owns the context table (keyed by the EE context
// address) under one mutex, the program-stream feed paths (sceMpegDemuxPss /
// sceMpegDemuxPssRing and the blocking-refill sector pump of sceMpegGetPicture),
// the guest side effects libmpeg's IPU worker would have produced (bitstream ring
// mirror, inner status word, completion-gate hold) and the serve path into the
// game's image buffer. Delivery pacing is returned as a tick count for the caller
// to park on: no clock, no sleep, no thread here.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/ipu/hle/fmv_context_layout.h"
#include "engine/ipu/hle/fmv_pcm_sink.h"
#include "engine/ipu/hle/fmv_sector_source.h"
#include "engine/ipu/hle/guest_memory.h"
#include "engine/ipu/hle/mpeg_context.h"
#include "engine/ipu/hle/mpeg_idle_state.h"
#include "engine/ipu/hle/video_decoder.h"
#include "engine/ipu/ipu_settings.h"

namespace ps2r::ipu {

class MpegApi {
public:
    using DecoderFactory = std::function<std::unique_ptr<VideoDecoder>()>;

    struct PictureResult {
        bool served = false;      // a picture was written to the guest buffer
        int64_t wait_ticks = 0;   // ticks the caller parks for (0 = none)
        bool late = false;        // the picture's authored deadline had passed
    };

    MpegApi(GuestMemory& memory, const FmvContextLayout& layout, FmvSectorSource& sectors,
            FmvPcmSink& pcm, DecoderFactory decoder_factory, const IpuSettings& settings);

    // sceMpegInit: drops every context and flushes queued movie audio.
    void Init();
    // sceMpegDelete: drops one context; flushes audio if it carried any.
    void Destroy(uint32_t mpeg_addr);
    // sceMpegReset: fresh playback on one context (audio flushed if carried).
    void Reset(uint32_t mpeg_addr);
    // The CD stream reached its end: every open context ends (after its
    // pending backlog, if any).
    void NotifyCdStreamEof();

    // sceMpegDemuxPss / sceMpegDemuxPssRing: `size` program-stream bytes from
    // guest memory (flat, or inside the wrap-around ring [ring_base,
    // ring_base + ring_size)). Returns the bytes consumed (0 = unmapped).
    int32_t DemuxPss(uint32_t mpeg_addr, uint32_t data_addr, int32_t size);
    int32_t DemuxPssRing(uint32_t mpeg_addr, uint32_t data_addr, int32_t size, uint32_t ring_base,
                         uint32_t ring_size);

    // sceMpegGetPicture: serves the next picture as RGB32 strips at
    // `image_addr`. Blocks (pumps sectors) while the FIFO is empty and the
    // stream is open. `now_tick` is the caller's vblank tick counter; the
    // returned wait is the pacer's booking for this delivery.
    PictureResult GetPicture(uint32_t mpeg_addr, uint32_t image_addr, int64_t now_tick);
    // Same delivery, PCM gate and clock as GetPicture, with ownership of the
    // decoder's row-major RGBA8 pixels instead of a guest strip buffer.
    // The output is cleared on no-picture; alpha conversion belongs to the
    // consuming picture format (the decoder itself produces alpha 255).
    PictureResult GetPictureRgba(uint32_t mpeg_addr, DecodedPicture& picture, int64_t now_tick);
    // sceMpegGetPictureRAW8: same, as a PSMCT16 plane of `target_w` x
    // `target_h` at `plane_addr` (the plane is selected by the caller from
    // the layout's presented/plane_select words).
    PictureResult GetPictureRaw8(uint32_t mpeg_addr, uint32_t plane_addr, uint32_t target_w,
                                 uint32_t target_h, int64_t now_tick);

    // sceMpegIsEnd: the movie has genuinely finished.
    bool IsEnd(uint32_t mpeg_addr);
    // A started movie that has NOT genuinely finished: the scene updater's
    // abort (raised by a premature, front-loaded end of stream) is to be held.
    bool ShouldHoldAbort(uint32_t mpeg_addr);
    uint32_t Width(uint32_t mpeg_addr);
    uint32_t Height(uint32_t mpeg_addr);

    // A movie is in flight on some context: input has been seen or pictures
    // wait, and the stream has not genuinely finished. The present policy and
    // the savestate refusal consume this signal.
    bool FmvActive();
    struct IdleContext {
        uint32_t address = 0, context_address = 0;
        uint32_t width = 0, height = 0;
        double authored_fps = 0;
        FmvPacer::State clock;
    };
    // A closed software session retains its delivery grid across reopen.
    // Capture only reset/unused contexts: no compressed input, pictures,
    // audio header or decoder references can be silently dropped.
    // The title must also refuse an open player/file or pending render work.
    bool CaptureIdle(int64_t now_tick, std::vector<IdleContext>& out, std::string* error = nullptr);
    bool ValidateIdle(const std::vector<IdleContext>&, int64_t now_tick, std::string* error = nullptr) const;
    // Caller quiesces providers before restore. Preflight/staging precedes
    // mutation; no guest words, timer, model register or DMA state is touched.
    bool RestoreIdle(const std::vector<IdleContext>&, int64_t now_tick, std::string* error = nullptr);
    // Application restore stages all context allocations before live owners
    // are joined. Publication reuses exactly that state at its saved clock.
    std::unique_ptr<MpegIdleState> PrepareIdle(const std::vector<IdleContext>&, int64_t restored_tick,
                                              std::string* error = nullptr);
    bool PublishIdle(MpegIdleState&, int64_t restored_tick, std::string* error = nullptr);

    // Guest-word helpers of the libmpeg internals the bindings replace (no
    // context state involved; layout offsets from the FmvContextLayout).
    // _sceMpegAlalcInit: arms the bump allocator over [base, base + size);
    // a null allocator pointer is ignored.
    void AlalcInit(uint32_t alalc_addr, uint32_t base, uint32_t size);
    // _sceMpegAlalcAlloc: aligns the watermark up to `align`, reserves `size`
    // bytes and returns their start; 0 for a null allocator, a zero alignment
    // or when the region is exhausted (the library's OUT_OF_MEMORY).
    uint32_t AlalcAlloc(uint32_t alalc_addr, uint32_t size, uint32_t align);
    // sceMpegClearRefBuff: zeroes the status word of every allocated
    // reference-picture slot of the context's inner work area (a null context
    // or inner pointer does nothing). The library's return value is always 1.
    void ClearRefBuff(uint32_t mpeg_addr);

private:
    PictureResult ServeRgb32(uint32_t mpeg_addr, uint32_t image_addr,
                            DecodedPicture* picture, int64_t now_tick);
    MpegContext& State(uint32_t mpeg_addr);
    std::unique_ptr<MpegContext> NewContext();
    MpegContext* Find(uint32_t mpeg_addr);

    // Guest side effects (layout offsets relative to the context address).
    void RingFillVideoPayload(uint32_t ctx_addr, const uint8_t* src, size_t len);
    void RingDrainToWrite(uint32_t ctx_addr);
    void WriteInnerStatus(uint32_t ctx_addr, bool ended);
    void HoldFmvCompletionGate(uint32_t ctx_addr, const MpegContext& s);
    bool OwnsRing(uint32_t ctx_addr);

    // Blocking-refill pump: reads sectors until a picture exists or the
    // source is exhausted / the budget is spent. Ring-owning context only.
    bool PumpUntilFrame(MpegContext& s, uint32_t mpeg_addr);
    // Common pre-serve work of both GetPicture variants.
    void PrepareServe(MpegContext& s, uint32_t mpeg_addr);
    // Books the delivery with the pacer (instant for the two priming serves).
    FmvPacer::Wait PaceDelivery(MpegContext& s, int64_t now_tick);
public:
    // Play-mode bounded catch-up for every context's pacer (FmvPacer::
    // SetReanchorPeriods; 0 = the reference contract). Applied as each
    // context latches its period.
    void SetPacerReanchorPeriods(int64_t periods) { pacer_reanchor_periods_ = periods; }
    uint64_t PacerReanchors() const;
private:
    int64_t pacer_reanchor_periods_ = 0;
    // Post-write bookkeeping shared by both variants.
    void FinishServe(MpegContext& s, uint32_t mpeg_addr);

    size_t CopyFromGuest(uint32_t data_addr, size_t size, std::vector<uint8_t>& out);
    size_t CopyFromGuestRing(uint32_t data_addr, size_t size, uint32_t ring_base, uint32_t ring_size,
                             std::vector<uint8_t>& out);

    GuestMemory& memory_;
    const FmvContextLayout& layout_;
    FmvSectorSource& sectors_;
    FmvPcmSink& pcm_;
    DecoderFactory decoder_factory_;
    const IpuSettings& settings_;
    std::mutex mutex_;
    std::unordered_map<uint32_t, std::unique_ptr<MpegContext>> contexts_;
    std::vector<uint8_t> pump_buf_;
};

}  // namespace ps2r::ipu
