#include "engine/gif/hle/gs_state_sink.h"

#include <cstring>

#include "engine/gif/gif_mtvu_marker.h"
#include "engine/gs/state/gs_state.h"

namespace {
// IEEE bits of a float, placed verbatim into the PACKED lane.
uint64_t FloatBits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}
}  // namespace

// A GIFtag was accepted: the GS resets Q to 1.0. When the tag's PRE bit is set
// the parser follows with WriteRegister(PRIM, tag.prim) itself.
void GsStateSink::BeginTag(const GifTag& /*tag*/, GifPath path) {
    ++tags_;
    path_ = static_cast<unsigned>(path) & 3;
    ++tags_by_path_[path_];
    if (path_ == static_cast<unsigned>(GifPath::Path1)) ++tags_by_bucket_[bucket_];
    gs_.GifTagRead();
}

void GsStateSink::WriteRegister(uint8_t reg, uint64_t value) {
    ++register_writes_;
    ++regs_by_path_[path_];
    gs_.WriteRegister(reg, value);
}

// PACKED RGBAQ (7.3.1): R at 0-7, G at 32-39, B at 64-71, A at 96-103; Q untouched.
void GsStateSink::PackedRgbaq(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    ++packed_writes_;
    const uint64_t lo = static_cast<uint64_t>(r) | (static_cast<uint64_t>(g) << 32);
    const uint64_t hi = static_cast<uint64_t>(b) | (static_cast<uint64_t>(a) << 32);
    gs_.WritePacked(0x01, lo, hi);
}

// PACKED ST: S at 0-31, T at 32-63, Q at 64-95 (the façade stores Q only when non-zero).
void GsStateSink::PackedSt(float s, float t, float q) {
    ++packed_writes_;
    gs_.WritePacked(0x02, FloatBits(s) | (FloatBits(t) << 32), FloatBits(q));
}

// PACKED UV: U at 0-13, V at 32-45.
void GsStateSink::PackedUv(uint16_t u, uint16_t v) {
    ++packed_writes_;
    gs_.WritePacked(0x03, static_cast<uint64_t>(u) | (static_cast<uint64_t>(v) << 32), 0);
}

// PACKED FOG: F at 100-107 (hi bits 36-43); the façade stores it as PRIM.FGE.
void GsStateSink::PackedFog(uint8_t f) {
    ++packed_writes_;
    gs_.WritePacked(0x0A, 0, static_cast<uint64_t>(f) << 36);
}

void GsStateSink::KickVertex(float x, float y, float z, uint8_t fog, bool draw) {
    ++kicks_;
    ++kicks_by_path_[path_];
    if (path_ == static_cast<unsigned>(GifPath::Path1)) ++kicks_by_bucket_[bucket_];
    gs_.KickVertex(x, y, z, fog, draw);
}

// One IMAGE quadword = two HWREG writes (lo first), delivered as 16 bytes.
void GsStateSink::ImageData(uint64_t lo, uint64_t hi) {
    ++image_qwords_;
    ++image_by_path_[path_];
    uint8_t qw[16];
    std::memcpy(qw, &lo, 8);
    std::memcpy(qw + 8, &hi, 8);
    gs_.WriteImage(qw, sizeof(qw));
}

// A span of IMAGE quadwords: the same counts, one WriteImage over the bytes (the
// transfer engine walks the words in order exactly as 16 bytes at a time would,
// and its staged store flushes at the span's end).
void GsStateSink::ImageSpan(const uint8_t* data, size_t qwords) {
    image_qwords_ += qwords;
    image_by_path_[path_] += qwords;
    gs_.WriteImage(data, qwords * 16);
}

// GIFtag EOP: the one GifEop flush (strip parity and dirty bits survive it).
void GsStateSink::EndOfPacket() {
    ++eops_;
    gs_.KickBoundary();
}

// A drain pass delivered PATH3 data with no IMAGE transfer mid-way: the GS
// flushes a non-empty draw buffer as a state-change flush (the parity restarts).
// DrawFlush::Flush is a no-op when nothing is queued.
void GsStateSink::FinishPath3() {
    ++path3_finishes_;
    gs_.Flush().Flush(ps2r::gs::DrawFlush::FlushKind::StateChange);
}

// ---- identity stamp -----------------------------------------------------------

void GsStateSink::StageKickIdentity(uint32_t hle_handler, uint64_t image_hash, uint16_t kick_pc, uint16_t kick_ord) {
    staged_ = SlotIdentity{true, hle_handler, image_hash, kick_pc, kick_ord};
    staged_valid_ = true;
}

void GsStateSink::ClearStagedIdentity() { staged_valid_ = false; }

// One FIFO entry per accepted PATH1 packet: the staged identity, else raw.
void GsStateSink::PushSlot() {
    SlotIdentity slot = staged_valid_ ? staged_ : SlotIdentity{};
    staged_valid_ = false;
    std::lock_guard<std::mutex> lock(fifo_mtx_);
    fifo_.push_back(slot);
}

void GsStateSink::OnArbiterSubmit(void* user, GifPath path, const uint8_t* data, size_t size) {
    auto* self = static_cast<GsStateSink*>(user);
    if (path == GifPath::Path1) self->PushSlot();
    if (path == GifPath::Path2) {
        // The submit may come from the MTVU worker (the VIF runs there): the
        // identity FIFO's lock covers the rows too.
        const unsigned c = static_cast<unsigned>(ClassifyPath2(data, size));
        std::lock_guard<std::mutex> lock(self->fifo_mtx_);
        ++self->path2_packets_[c];
        self->path2_bytes_[c] += size;
    }
}

const char* GsStateSink::Path2ClassName(Path2Class c) {
    switch (c) {
        case Path2Class::Image: return "image";
        case Path2Class::TexRegs: return "texregs";
        case Path2Class::TransferHead: return "transfer_head";
        case Path2Class::State: return "state";
        case Path2Class::Geometry: return "geometry";
        case Path2Class::Reglist: return "reglist";
        case Path2Class::Other: return "other";
        case Path2Class::Count: break;
    }
    return "?";
}

// GIFtag: NLOOP 0-14, EOP 15, PRE 46, PRIM 47-57, FLG 58-59, NREG 60-63, REGS 64-127
// (GS User's Manual 7.2). A+D data: the register address in bits 64-71 of the
// quadword (byte 8 of the item).
GsStateSink::Path2Class GsStateSink::ClassifyPath2(const uint8_t* data, size_t size) {
    if (!data || size < 16) return Path2Class::Other;
    uint64_t lo, hi;
    std::memcpy(&lo, data, 8);
    std::memcpy(&hi, data + 8, 8);
    const unsigned flg = static_cast<unsigned>((lo >> 58) & 3);
    if (flg == 2) return Path2Class::Image;
    if (flg == 1) return Path2Class::Reglist;
    if (flg != 0) return Path2Class::Other;
    unsigned nreg = static_cast<unsigned>((lo >> 60) & 0xF);
    if (nreg == 0) nreg = 16;
    const bool pre = ((lo >> 46) & 1) != 0;
    bool vertex = false, ad_only = true;
    for (unsigned i = 0; i < nreg; ++i) {
        const unsigned reg = static_cast<unsigned>((hi >> (i * 4)) & 0xF);
        if (reg == 0x4 || reg == 0x5 || reg == 0xC || reg == 0xD) vertex = true;
        if (reg != 0xE) ad_only = false;
    }
    if (vertex || pre) return Path2Class::Geometry;
    if (!ad_only || size < 32) return Path2Class::Other;
    const uint8_t addr = data[16 + 8];
    if (addr == 0x3F) return Path2Class::TexRegs;
    if (addr >= 0x50 && addr <= 0x53) return Path2Class::TransferHead;
    return Path2Class::State;
}

// Before the packet reaches the parser: PATH1 pops its slot; a boundary slot
// (no data) touches nothing; PATH2 / PATH3 registers are raw.
void GsStateSink::OnArbiterDeliver(void* user, const GifArbiterPacket& pkt) {
    static_cast<GsStateSink*>(user)->ApplySlot(pkt.path == GifPath::Path1, !pkt.data.empty());
}

void GsStateSink::ApplySlot(bool path1, bool has_data) {
    if (!has_data) return;
    if (!path1) {
        gs_.ClearIdentity();
        return;
    }
    SlotIdentity slot{};
    bool have = false;
    {
        std::lock_guard<std::mutex> lock(fifo_mtx_);
        if (!fifo_.empty()) {
            slot = fifo_.front();
            fifo_.pop_front();
            have = true;
        }
    }
    if (!have) {
        ++identity_underflows_;
        gs_.ClearIdentity();
        return;
    }
    if (slot.hle) {
        ++identity_stamped_;
        gs_.SetIdentity(slot.hle_handler, slot.image_hash, slot.kick_pc, slot.kick_ord, frame_inst_);
    } else {
        ++identity_raw_;
        gs_.ClearIdentity();
    }
}

size_t GsStateSink::IdentityPending() const {
    std::lock_guard<std::mutex> lock(fifo_mtx_);
    return fifo_.size();
}
