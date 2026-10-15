// engine/ipu/hle/mpeg_api.cpp -- MpegApi bodies (see the header).
#include "engine/ipu/hle/mpeg_api.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "engine/ipu/hle/frame_writer.h"

namespace ps2r::ipu {

namespace {
constexpr uint32_t kSectorBytes = 2048;
}  // namespace

MpegApi::MpegApi(GuestMemory& memory, const FmvContextLayout& layout, FmvSectorSource& sectors,
                 FmvPcmSink& pcm, DecoderFactory decoder_factory, const IpuSettings& settings)
    : memory_(memory),
      layout_(layout),
      sectors_(sectors),
      pcm_(pcm),
      decoder_factory_(std::move(decoder_factory)),
      settings_(settings) {}

// ---------------------------------------------------------------------------
// Context table.
// ---------------------------------------------------------------------------

MpegContext& MpegApi::State(uint32_t mpeg_addr) {
    auto it = contexts_.find(mpeg_addr);
    if (it == contexts_.end()) it = contexts_.emplace(mpeg_addr, NewContext()).first;
    return *it->second;
}

std::unique_ptr<MpegContext> MpegApi::NewContext() {
    auto ctx = std::make_unique<MpegContext>(decoder_factory_, settings_, pcm_);
    MpegContext* raw = ctx.get();
    raw->on_video_payload = [this, raw](const uint8_t* d, size_t n) {
        RingFillVideoPayload(raw->ctx_addr, d, n);
    };
    raw->audio_allowed = [this, raw]() { return raw->ctx_addr != 0 && OwnsRing(raw->ctx_addr); };
    return ctx;
}

MpegContext* MpegApi::Find(uint32_t mpeg_addr) {
    auto it = contexts_.find(mpeg_addr);
    return it == contexts_.end() ? nullptr : it->second.get();
}

// ---------------------------------------------------------------------------
// Guest side effects.
// ---------------------------------------------------------------------------

bool MpegApi::OwnsRing(uint32_t ctx_addr) {
    return ctx_addr != 0 && memory_.Read32(ctx_addr + layout_.ring_base) != 0;
}

// The game's video-stream callback copies each video PES payload into the
// context's bitstream ring at the write cursor (wrapping at base + size) and
// advances the cursor; the player's init then parses the sequence header at
// the ring anchor. The HLE decodes elsewhere, so it reproduces that write.
void MpegApi::RingFillVideoPayload(uint32_t ctx_addr, const uint8_t* src, size_t len) {
    if (ctx_addr == 0 || src == nullptr || len == 0) return;

    const uint32_t base = memory_.Read32(ctx_addr + layout_.ring_base);
    const uint32_t size = memory_.Read32(ctx_addr + layout_.ring_size);
    if (base == 0 || size == 0) return;  // no ring: nothing to mirror

    const uint32_t write = memory_.Read32(ctx_addr + layout_.ring_write);
    uint32_t off = (write >= base) ? (write - base) : 0u;
    if (off >= size) off %= size;

    const uint32_t tail = size - off;
    const size_t first = std::min<size_t>(len, tail);

    uint8_t* d = memory_.Translate(base + off, first);
    if (!d) return;
    std::memcpy(d, src, first);

    uint32_t new_off = off + static_cast<uint32_t>(first);
    if (new_off >= size) new_off = 0;

    const size_t rem = len - first;
    if (rem > 0) {
        if (uint8_t* d2 = memory_.Translate(base, rem)) std::memcpy(d2, src + first, rem);
        new_off = static_cast<uint32_t>(rem);
    }
    memory_.Write32(ctx_addr + layout_.ring_write, base + new_off);
}

// The IPU bitstream reader walks the read cursor as it consumes bytes; the
// HLE never reads the ring, so after each serve the ring is marked drained
// (read = write) and can never look full to the producer's free-space check.
void MpegApi::RingDrainToWrite(uint32_t ctx_addr) {
    if (ctx_addr == 0) return;
    const uint32_t base = memory_.Read32(ctx_addr + layout_.ring_base);
    const uint32_t size = memory_.Read32(ctx_addr + layout_.ring_size);
    if (base == 0 || size == 0) return;
    const uint32_t write = memory_.Read32(ctx_addr + layout_.ring_write);
    memory_.Write32(ctx_addr + layout_.ring_read, write);
}

// libmpeg's decode worker writes the inner block's first word to 0 at the
// top of every GetPicture and leaves it non-zero only once the bitstream
// parser reaches the sequence end; the driver polls it as its per-frame gate.
void MpegApi::WriteInnerStatus(uint32_t ctx_addr, bool ended) {
    if (ctx_addr == 0) return;
    const uint32_t inner = memory_.Read32(ctx_addr + layout_.inner_status_ptr);
    if (inner == 0) return;  // context not created yet
    const uint32_t value = ended ? 1u : 0u;
    if (memory_.Read32(inner) == value) return;
    memory_.Write32(inner, value);
}

// The player derives its "pictures to show" total from the header struct the
// IPU parser fills; the HLE's parser is not libmpeg's, so the word is held at
// the title's play-to-end value while the movie plays (signed compare against
// the presented count), for the ring-owning RAW8-mode context only, until the
// stream has genuinely ended.
void MpegApi::HoldFmvCompletionGate(uint32_t ctx_addr, const MpegContext& s) {
    if (ctx_addr == 0 || !s.saw_input) return;
    if (s.StreamEnded()) return;
    if (!OwnsRing(ctx_addr)) return;
    if (memory_.Read32(ctx_addr + layout_.mode) != layout_.mode_raw8) return;

    const int32_t cur = static_cast<int32_t>(memory_.Read32(ctx_addr + layout_.total));
    if (cur >= layout_.play_to_eof_total) return;  // already held
    memory_.Write32(ctx_addr + layout_.total, static_cast<uint32_t>(layout_.play_to_eof_total));
}

// ---------------------------------------------------------------------------
// Guest copies.
// ---------------------------------------------------------------------------

size_t MpegApi::CopyFromGuest(uint32_t data_addr, size_t size, std::vector<uint8_t>& out) {
    out.resize(size);
    const uint8_t* host = memory_.Translate(data_addr, size);
    if (!host) return 0;
    std::memcpy(out.data(), host, size);
    return size;
}

size_t MpegApi::CopyFromGuestRing(uint32_t data_addr, size_t size, uint32_t ring_base,
                                  uint32_t ring_size, std::vector<uint8_t>& out) {
    if (ring_base == 0 || ring_size == 0) return CopyFromGuest(data_addr, size, out);
    if (data_addr < ring_base || data_addr >= ring_base + ring_size) {
        return CopyFromGuest(data_addr, size, out);  // outside the ring: flat
    }
    const uint32_t off = data_addr - ring_base;
    const uint32_t tail = ring_size - off;
    if (size <= tail) return CopyFromGuest(data_addr, size, out);

    out.resize(size);
    const uint8_t* host_tail = memory_.Translate(data_addr, tail);
    const uint8_t* host_head = memory_.Translate(ring_base, size - tail);
    if (!host_tail || !host_head) return 0;
    std::memcpy(out.data(), host_tail, tail);
    std::memcpy(out.data() + tail, host_head, size - tail);
    return size;
}

// ---------------------------------------------------------------------------
// Lifecycle.
// ---------------------------------------------------------------------------

void MpegApi::Init() {
    std::lock_guard<std::mutex> lk(mutex_);
    contexts_.clear();
    pcm_.Flush();  // a fresh table: no queued movie PCM may survive
}

void MpegApi::Destroy(uint32_t mpeg_addr) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (MpegContext* s = Find(mpeg_addr)) {
        if (s->AudioCarried()) pcm_.Flush();
        contexts_.erase(mpeg_addr);
    }
}

void MpegApi::Reset(uint32_t mpeg_addr) {
    std::lock_guard<std::mutex> lk(mutex_);
    MpegContext& s = State(mpeg_addr);
    // Only the context that carried audio cuts the sound: a texture-streaming
    // context's Reset must not silence a live movie.
    if (s.AudioCarried()) pcm_.Flush();
    s.ResetPlayback();
}

void MpegApi::NotifyCdStreamEof() {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& [addr, ctx] : contexts_) {
        MpegContext& s = *ctx;
        if (s.saw_input && !s.stream_ended) {
            s.demux.Process(/*final_chunk=*/true);  // best-effort drain of the residue
            s.EndFromSource();
        }
    }
}

// ---------------------------------------------------------------------------
// Feed paths.
// ---------------------------------------------------------------------------

int32_t MpegApi::DemuxPss(uint32_t mpeg_addr, uint32_t data_addr, int32_t size) {
    if (size <= 0) return 0;
    std::vector<uint8_t> tmp;
    const size_t copied = CopyFromGuest(data_addr, static_cast<size_t>(size), tmp);
    if (copied == 0) return 0;

    std::lock_guard<std::mutex> lk(mutex_);
    MpegContext& s = State(mpeg_addr);
    s.ctx_addr = mpeg_addr;
    s.Demux(tmp.data(), tmp.size(), /*final_chunk=*/false);
    // The demux pump runs through the load loop: the inner gate reads 0 before
    // the first playback tick, and the completion total is held open.
    WriteInnerStatus(mpeg_addr, s.StreamEnded());
    HoldFmvCompletionGate(mpeg_addr, s);
    return static_cast<int32_t>(copied);
}

int32_t MpegApi::DemuxPssRing(uint32_t mpeg_addr, uint32_t data_addr, int32_t size,
                              uint32_t ring_base, uint32_t ring_size) {
    if (size <= 0) return 0;
    std::vector<uint8_t> tmp;
    const size_t copied =
        CopyFromGuestRing(data_addr, static_cast<size_t>(size), ring_base, ring_size, tmp);
    if (copied == 0) return 0;

    std::lock_guard<std::mutex> lk(mutex_);
    MpegContext& s = State(mpeg_addr);
    s.ctx_addr = mpeg_addr;
    s.Demux(tmp.data(), tmp.size(), /*final_chunk=*/false);
    WriteInnerStatus(mpeg_addr, s.StreamEnded());
    HoldFmvCompletionGate(mpeg_addr, s);
    return static_cast<int32_t>(copied);
}

// sceMpegGetPicture blocks on hardware: when the bitstream runs dry the
// registered refill callback pulls the next CD-stream sectors into the ring
// and decoding continues until a whole picture exists. The pump reproduces
// that from the SAME shared stream cursor the game's sceCdStRead uses, so the
// game's later reads continue where the pump stopped.
bool MpegApi::PumpUntilFrame(MpegContext& s, uint32_t mpeg_addr) {
    if (!s.frames.empty()) return true;
    s.DrainPendingEs();  // the deferred backlog serves before any disc read
    if (!s.frames.empty()) return true;
    if (s.stream_ended) return false;

    // Only the movie player's context streams from the disc.
    if (!OwnsRing(mpeg_addr)) return false;
    if (s.ctx_addr == 0) s.ctx_addr = mpeg_addr;

    pump_buf_.resize(static_cast<size_t>(settings_.sectors_per_pump) * kSectorBytes);

    uint32_t pumped = 0;
    while (s.frames.empty() && !s.stream_ended && pumped < settings_.max_sector_budget) {
        const uint32_t got = sectors_.PumpSectors(pump_buf_.data(), settings_.sectors_per_pump);
        if (got == 0) {
            // Source exhausted: a deferred backlog keeps decoding (its end
            // marker promotes when it drains); a drained pipeline ends now.
            if (s.PendingBacklog()) {
                s.EndFromSource();
                s.DrainPendingEs();
                continue;
            }
            s.EndFromSource();
            break;
        }
        pumped += got;
        s.Demux(pump_buf_.data(), static_cast<size_t>(got) * kSectorBytes, /*final_chunk=*/false);
        // The demux defers past the cap; a still-empty FIFO refills from the
        // backlog it just built.
        if (s.frames.empty()) s.DrainPendingEs();
    }
    return !s.frames.empty();
}

// ---------------------------------------------------------------------------
// Serve path.
// ---------------------------------------------------------------------------

void MpegApi::PrepareServe(MpegContext& s, uint32_t mpeg_addr) {
    // Top the FIFO back up from the deferred backlog (bounded: at most the cap).
    s.DrainPendingEs();
    // Once the stream has ended, the decoder's buffered pictures come out.
    if (s.frames.empty() && s.stream_ended && s.decoder) s.decoder->Flush(s.frames);
    if (s.frames.empty() && !s.stream_ended) PumpUntilFrame(s, mpeg_addr);
}

FmvPacer::Wait MpegApi::PaceDelivery(MpegContext& s, int64_t now_tick) {
    if (!FmvPacer::ShouldPace(s.frames_served)) return {};  // priming serves are instant
    if (s.pacer.Period() <= 0) {
        // Rate latch: the sequence header's authored rate, else NTSC.
        double fps = s.decoder ? s.decoder->AuthoredFps() : 0.0;
        if (!(fps > 0.0)) fps = FmvPacer::kFallbackFps;
        s.pace_rate_fps = fps;
        s.pacer.SetAuthoredRateTicks(fps);
        s.pacer.SetReanchorPeriods(pacer_reanchor_periods_);
    }
    return s.pacer.Next(now_tick);
}

uint64_t MpegApi::PacerReanchors() const {
    uint64_t n = 0;
    for (const auto& [addr, ctx] : contexts_) n += ctx->pacer.Reanchors();
    return n;
}

void MpegApi::FinishServe(MpegContext& s, uint32_t mpeg_addr) {
    RingDrainToWrite(s.ctx_addr);
    s.frames_served++;
    // The first served picture of a context that carries audio opens the
    // gate: PSS audio and video share their presentation origin.
    if (s.audio.Pushed() && !s.gate_opened) {
        s.gate_opened = true;
        pcm_.GateOpen();
    }
    WriteInnerStatus(mpeg_addr, s.StreamEnded());
}

MpegApi::PictureResult MpegApi::GetPicture(uint32_t mpeg_addr, uint32_t image_addr, int64_t now_tick) {
    return ServeRgb32(mpeg_addr, image_addr, nullptr, now_tick);
}

MpegApi::PictureResult MpegApi::GetPictureRgba(uint32_t mpeg_addr, DecodedPicture& picture,
                                             int64_t now_tick) {
    picture = {};
    return ServeRgb32(mpeg_addr, 0, &picture, now_tick);
}

MpegApi::PictureResult MpegApi::ServeRgb32(uint32_t mpeg_addr, uint32_t image_addr,
                                         DecodedPicture* picture, int64_t now_tick) {
    PictureResult r;
    std::lock_guard<std::mutex> lk(mutex_);
    MpegContext& s = State(mpeg_addr);
    PrepareServe(s, mpeg_addr);

    if (!s.frames.empty()) {
        const FmvPacer::Wait w = PaceDelivery(s, now_tick);
        DecodedPicture f = std::move(s.frames.front());
        s.frames.pop_front();
        s.width = static_cast<uint32_t>(f.width);
        s.height = static_cast<uint32_t>(f.height);
        const size_t bytes = FrameWriter::Rgb32Bytes(s.width, s.height);
        // A null image pointer from the driver (or an empty picture) writes
        // nothing; the serve still counts.
        if (image_addr != 0 && f.width > 0 && f.height > 0) {
            if (uint8_t* dst = memory_.Translate(image_addr, bytes)) FrameWriter::WriteRgb32Strips(dst, f);
        }
        if (picture) *picture = std::move(f);
        FinishServe(s, mpeg_addr);
        r.served = true;
        r.wait_ticks = w.wait_ticks;
        r.late = w.late;
        return r;
    }

    if (s.stream_ended && s.frames_served > 0) {
        // True end of stream, reportable only once a picture has been shown.
        WriteInnerStatus(mpeg_addr, true);
        return r;
    }
    // No picture yet but the stream is open: the driver retries next vblank.
    WriteInnerStatus(mpeg_addr, false);
    return r;
}

MpegApi::PictureResult MpegApi::GetPictureRaw8(uint32_t mpeg_addr, uint32_t plane_addr,
                                               uint32_t target_w, uint32_t target_h, int64_t now_tick) {
    PictureResult r;
    std::lock_guard<std::mutex> lk(mutex_);
    MpegContext& s = State(mpeg_addr);
    PrepareServe(s, mpeg_addr);

    if (!s.frames.empty()) {
        const FmvPacer::Wait w = PaceDelivery(s, now_tick);
        DecodedPicture f = std::move(s.frames.front());
        s.frames.pop_front();
        s.width = static_cast<uint32_t>(f.width);
        s.height = static_cast<uint32_t>(f.height);
        const size_t bytes = FrameWriter::Rgb16Bytes(target_w, target_h);
        // A null plane pointer or a zero plane geometry writes nothing; the
        // serve still counts.
        if (plane_addr != 0 && target_w != 0 && target_h != 0) {
            if (uint8_t* dst = memory_.Translate(plane_addr, bytes)) {
                FrameWriter::WriteRgb16Strips(dst, target_w, target_h, f);
            }
        }
        FinishServe(s, mpeg_addr);
        r.served = true;
        r.wait_ticks = w.wait_ticks;
        r.late = w.late;
        return r;
    }

    // No picture (stream open) or fully drained: the driver skips this vblank.
    WriteInnerStatus(mpeg_addr, s.StreamEnded());
    return r;
}

// ---------------------------------------------------------------------------
// Queries.
// ---------------------------------------------------------------------------

bool MpegApi::IsEnd(uint32_t mpeg_addr) {
    std::lock_guard<std::mutex> lk(mutex_);
    MpegContext* s = Find(mpeg_addr);
    return s ? s->StreamEnded() : false;
}

bool MpegApi::ShouldHoldAbort(uint32_t mpeg_addr) {
    std::lock_guard<std::mutex> lk(mutex_);
    MpegContext* s = Find(mpeg_addr);
    if (!s) return false;
    return s->saw_input && !s->StreamEnded();
}

uint32_t MpegApi::Width(uint32_t mpeg_addr) {
    std::lock_guard<std::mutex> lk(mutex_);
    MpegContext* s = Find(mpeg_addr);
    return s ? s->width : MpegContext::kDefaultWidth;
}

uint32_t MpegApi::Height(uint32_t mpeg_addr) {
    std::lock_guard<std::mutex> lk(mutex_);
    MpegContext* s = Find(mpeg_addr);
    return s ? s->height : MpegContext::kDefaultHeight;
}

bool MpegApi::FmvActive() {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& [addr, ctx] : contexts_) {
        const MpegContext& s = *ctx;
        if ((s.saw_input || !s.frames.empty()) && !s.StreamEnded()) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Guest-word helpers (libmpeg internals; no context state).
// ---------------------------------------------------------------------------

void MpegApi::AlalcInit(uint32_t alalc_addr, uint32_t base, uint32_t size) {
    if (alalc_addr == 0) return;
    memory_.Write32(alalc_addr + layout_.alalc_base, base);
    memory_.Write32(alalc_addr + layout_.alalc_capacity, size);
    memory_.Write32(alalc_addr + layout_.alalc_watermark, base);
    memory_.Write32(alalc_addr + layout_.alalc_base_copy, base);
}

uint32_t MpegApi::AlalcAlloc(uint32_t alalc_addr, uint32_t size, uint32_t align) {
    if (alalc_addr == 0 || align == 0) return 0;
    const uint32_t base = memory_.Read32(alalc_addr + layout_.alalc_base);
    const uint32_t capacity = memory_.Read32(alalc_addr + layout_.alalc_capacity);
    const uint32_t watermark = memory_.Read32(alalc_addr + layout_.alalc_watermark);

    // Watermark aligned up to the next `align` boundary (the library's
    // ((wm + align - 1) / align) * align).
    const uint32_t aligned = ((watermark + align - 1) / align) * align;
    const uint32_t upper = base + capacity;
    if (upper < aligned + size) return 0;  // region exhausted

    memory_.Write32(alalc_addr + layout_.alalc_watermark, aligned + size);
    return aligned;
}

void MpegApi::ClearRefBuff(uint32_t mpeg_addr) {
    if (mpeg_addr == 0) return;
    const uint32_t inner = memory_.Read32(mpeg_addr + layout_.inner_status_ptr);
    if (inner == 0) return;
    for (uint32_t off : layout_.ref_slot) {
        const uint32_t slot = memory_.Read32(inner + off);
        if (slot != 0) memory_.Write32(slot + layout_.ref_slot_status, 0);
    }
}

}  // namespace ps2r::ipu
