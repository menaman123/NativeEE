#include "engine/vu/hle/kick_lowering.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <utility>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_register_dispatch.h"
#include "engine/gif/gif_tag.h"
#include "engine/gif/gs_sink.h"

struct KickLowering::Counters {
    std::atomic<uint64_t> submitted{0}, queued{0}, lowered{0}, raw_fallback{0};
    std::atomic<uint64_t> queued_state{0}, lowered_state{0}, queued_packed{0}, lowered_packed{0};
    std::atomic<uint64_t> unsupported{0}, unconfigured{0}, ordering_fallback{0};
    // The unsupported census by producing program: written by the submitting
    // thread, read by the census at finalisation.
    mutable std::mutex by_program_mutex;
    std::vector<UnsupportedByProgram> by_program;
};

namespace {

// The recognized HLE emitter's output is already transformed and quantized.
// Keep integer depth until delivery so conversion uses the drain owner's
// rounding mode, just as the existing raw consumer does.
struct GeometryVertex {
    float s, t, q;
    uint8_t r, g, b, a;
    float x, y;
    uint32_t z;
    uint8_t fog;
    bool draw;
};

struct GeometryBatch {
    GifTag tag{};
    std::vector<GeometryVertex> vertices;
};

// One A+D pair as the PACKED quadword carries it: the register address in the
// low byte of the high half, the 64-bit value in the low half.
struct RegisterWrite {
    uint8_t reg;
    uint64_t value;
};

// The material's register block: a single PACKED tag with one A+D descriptor
// and EOP (TR AddressDataTag), NLOOP writes.
struct StateBatch {
    GifTag tag{};
    std::vector<RegisterWrite> writes;
};

uint32_t Word(const uint8_t* data) {
    uint32_t word;
    std::memcpy(&word, data, sizeof(word));
    return word;
}

float Scalar(const uint8_t* data) {
    float value;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

bool ReadGeometry(const std::vector<uint8_t>& kick, GeometryBatch& batch) {
    if (kick.size() < 16) return false;
    uint64_t lo, hi;
    std::memcpy(&lo, kick.data(), sizeof(lo));
    std::memcpy(&hi, kick.data() + 8, sizeof(hi));
    batch.tag.Parse(lo, hi);
    const auto& tag = batch.tag;
    // A fixed producer layout, not a descriptor-stream interpreter. State
    // packets, transfers, other layouts, multiple tags and incomplete packets
    // remain explicit raw routes without any GS mutation during recognition.
    if (!tag.eop || tag.flg != GifFormat::Packed || tag.nregs != 3 ||
        tag.regs[0] != 2 || tag.regs[1] != 1 ||
        (tag.regs[2] != 5 && tag.regs[2] != 4) ||
        kick.size() != 16 + size_t(tag.nloop) * 48)
        return false;

    const bool with_fog = tag.regs[2] == 4;
    batch.vertices.reserve(tag.nloop);
    for (size_t index = 0; index < tag.nloop; ++index) {
        const uint8_t* st = kick.data() + 16 + index * 48;
        const uint8_t* color = st + 16;
        const uint8_t* position = st + 32;
        GeometryVertex vertex{};
        vertex.s = Scalar(st);
        vertex.t = Scalar(st + 4);
        vertex.q = Scalar(st + 8);
        vertex.r = static_cast<uint8_t>(Word(color));
        vertex.g = static_cast<uint8_t>(Word(color + 4));
        vertex.b = static_cast<uint8_t>(Word(color + 8));
        vertex.a = static_cast<uint8_t>(Word(color + 12));
        vertex.x = static_cast<float>(Word(position) & 0xffffu) / 16.0f;
        vertex.y = static_cast<float>(Word(position + 4) & 0xffffu) / 16.0f;
        const uint32_t z = Word(position + 8), flags = Word(position + 12);
        vertex.z = with_fog ? (z >> 4) & 0xffffffu : z;
        vertex.fog = with_fog ? static_cast<uint8_t>(flags >> 4) : 0;
        vertex.draw = (flags & 0x8000u) == 0;
        batch.vertices.push_back(vertex);
    }
    return true;
}

// The register-block shape: PACKED, exactly one descriptor and it is A+D, EOP,
// NLOOP quadwords and nothing after them. Recognition reads bytes only; the
// register effects happen on the drain owner, through the parser's dispatcher.
bool ReadStateWrites(const std::vector<uint8_t>& kick, StateBatch& batch) {
    if (kick.size() < 16) return false;
    uint64_t lo, hi;
    std::memcpy(&lo, kick.data(), sizeof(lo));
    std::memcpy(&hi, kick.data() + 8, sizeof(hi));
    batch.tag.Parse(lo, hi);
    const auto& tag = batch.tag;
    if (!tag.eop || tag.flg != GifFormat::Packed || tag.nregs != 1 ||
        tag.regs[0] != static_cast<uint8_t>(GsReg::Ad) || kick.size() != 16 + size_t(tag.nloop) * 16)
        return false;
    batch.writes.reserve(tag.nloop);
    for (size_t index = 0; index < tag.nloop; ++index) {
        const uint8_t* qw = kick.data() + 16 + index * 16;
        uint64_t value, address;
        std::memcpy(&value, qw, sizeof(value));
        std::memcpy(&address, qw + 8, sizeof(address));
        batch.writes.push_back({static_cast<uint8_t>(address & 0xFFu), value});
    }
    return true;
}

// One PACKED quadword under its descriptor, as the tag lists them.
struct PackedQuad {
    uint8_t reg;
    uint64_t lo, hi;
};

// Any other single-tag PACKED packet with EOP (the sprite typesetter's
// {UV,RGBAQ,XYZ2,UV,XYZ2}, the particle quad's nine descriptors, the decal's
// 4 x {ST,RGBAQ,XYZ2}): every quadword kept with its descriptor for the
// parser's PACKED dispatcher on the drain owner. Reached only after the strip
// and register-block shapes declined, so it never carries those.
struct PackedBatch {
    GifTag tag{};
    std::vector<PackedQuad> quads;
};

bool ReadPackedList(const std::vector<uint8_t>& kick, PackedBatch& batch) {
    if (kick.size() < 16) return false;
    uint64_t lo, hi;
    std::memcpy(&lo, kick.data(), sizeof(lo));
    std::memcpy(&hi, kick.data() + 8, sizeof(hi));
    batch.tag.Parse(lo, hi);
    const auto& tag = batch.tag;
    const size_t count = size_t(tag.nloop) * tag.nregs;
    if (!tag.eop || tag.flg != GifFormat::Packed || kick.size() != 16 + count * 16) return false;
    batch.quads.reserve(count);
    const uint8_t* qw = kick.data() + 16;
    for (size_t loop = 0; loop < tag.nloop; ++loop) {
        for (unsigned reg = 0; reg < tag.nregs; ++reg, qw += 16) {
            PackedQuad quad;
            quad.reg = tag.regs[reg];
            std::memcpy(&quad.lo, qw, sizeof(quad.lo));
            std::memcpy(&quad.hi, qw + 8, sizeof(quad.hi));
            batch.quads.push_back(quad);
        }
    }
    return true;
}

// Names the declined packet's shape for the census: the format, the register
// descriptors as hex nibbles, and why the single-tag rule failed.
void DescribeShape(const std::vector<uint8_t>& kick, char* shape, size_t capacity) {
    if (kick.size() < 16) {
        std::snprintf(shape, capacity, "short");
        return;
    }
    uint64_t lo, hi;
    std::memcpy(&lo, kick.data(), sizeof(lo));
    std::memcpy(&hi, kick.data() + 8, sizeof(hi));
    GifTag tag;
    tag.Parse(lo, hi);
    static const char* const kFormats[] = {"packed", "reglist", "image", "disabled"};
    char regs[17] = {};
    for (unsigned i = 0; i < tag.nregs && i < 16; ++i) regs[i] = "0123456789abcdef"[tag.regs[i] & 15];
    size_t expected = 16;
    if (tag.flg == GifFormat::Packed) expected += size_t(tag.nloop) * tag.nregs * 16;
    else if (tag.flg == GifFormat::Reglist) expected += (size_t(tag.nloop) * tag.nregs + 1) / 2 * 16;
    else expected += size_t(tag.nloop) * 16;
    const char* size_note = kick.size() == expected ? "" : (kick.size() > expected ? ":tail" : ":trunc");
    std::snprintf(shape, capacity, "%s:%s%s%s", kFormats[static_cast<unsigned>(tag.flg) & 3], regs,
                  tag.eop ? "" : ":noeop", size_note);
}

void DeliverGeometry(GsSink& sink, const GeometryBatch& batch) {
    // Reuse the existing attribute/primitive assembly and DrawIR flush. This
    // preserves inherited render state, q==0 retention, ADC, strip parity,
    // pending primitives and the identity applied by the ordered drain observer.
    sink.BeginTag(batch.tag, GifPath::Path1);
    if (batch.tag.pre) sink.WriteRegister(static_cast<uint8_t>(GsReg::Prim), batch.tag.prim);
    for (const auto& vertex : batch.vertices) {
        sink.PackedSt(vertex.s, vertex.t, vertex.q);
        sink.PackedRgbaq(vertex.r, vertex.g, vertex.b, vertex.a);
        sink.KickVertex(vertex.x, vertex.y, static_cast<float>(vertex.z), vertex.fog, vertex.draw);
    }
    sink.EndOfPacket();
}

void DeliverState(GsSink& sink, const StateBatch& batch) {
    // The parser's A+D sequence for the same bytes: the tag (Q reset), PRE's
    // PRIM, one dispatched register per loop, the EOP flush. Inherited state,
    // the identity stamp and ordering are the slot's, as for geometry.
    sink.BeginTag(batch.tag, GifPath::Path1);
    if (batch.tag.pre) sink.WriteRegister(static_cast<uint8_t>(GsReg::Prim), batch.tag.prim);
    for (const RegisterWrite& write : batch.writes) DispatchGifRegister(sink, write.reg, write.value);
    sink.EndOfPacket();
}

void DeliverPackedList(GsSink& sink, const PackedBatch& batch) {
    // The parser's PACKED walk for the same bytes: the tag, PRE's PRIM, each
    // quadword under its descriptor in loop order, the EOP flush.
    sink.BeginTag(batch.tag, GifPath::Path1);
    if (batch.tag.pre) sink.WriteRegister(static_cast<uint8_t>(GsReg::Prim), batch.tag.prim);
    for (const PackedQuad& quad : batch.quads) DispatchPackedQuadword(sink, quad.reg, quad.lo, quad.hi);
    sink.EndOfPacket();
}

}  // namespace

KickLowering::KickLowering(GifArbiter& arbiter)
    : arbiter_(arbiter), counters_(std::make_shared<Counters>()) {}

KickLowering::KickLowering(GifArbiter& arbiter, GsSink& sink)
    : arbiter_(arbiter), sink_(&sink), counters_(std::make_shared<Counters>()) {}

KickLowering::~KickLowering() = default;

bool KickLowering::TryLower(const std::vector<uint8_t>& kick) {
    if (!sink_) {
        counters_->unconfigured.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    // The semantic payload and fallback bytes are owned by the same slot.
    // Recognition happens on the producer; effects happen only on the drain.
    // Shared counters outlive this lowering if its queued slots are discarded.
    GeometryBatch geometry;
    if (ReadGeometry(kick, geometry)) {
        counters_->queued.fetch_add(1, std::memory_order_relaxed);
        arbiter_.submitDirectOwned(std::vector<uint8_t>(kick),
            [batch = std::move(geometry), sink = sink_, counters = counters_](bool direct) {
                if (!direct) {
                    counters->ordering_fallback.fetch_add(1, std::memory_order_relaxed);
                    counters->raw_fallback.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
                DeliverGeometry(*sink, batch);
                counters->lowered.fetch_add(1, std::memory_order_relaxed);
            });
        return true;
    }
    StateBatch state;
    if (ReadStateWrites(kick, state)) {
        counters_->queued.fetch_add(1, std::memory_order_relaxed);
        counters_->queued_state.fetch_add(1, std::memory_order_relaxed);
        arbiter_.submitDirectOwned(std::vector<uint8_t>(kick),
            [batch = std::move(state), sink = sink_, counters = counters_](bool direct) {
                if (!direct) {
                    counters->ordering_fallback.fetch_add(1, std::memory_order_relaxed);
                    counters->raw_fallback.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
                DeliverState(*sink, batch);
                counters->lowered.fetch_add(1, std::memory_order_relaxed);
                counters->lowered_state.fetch_add(1, std::memory_order_relaxed);
            });
        return true;
    }
    PackedBatch packed;
    if (ReadPackedList(kick, packed)) {
        counters_->queued.fetch_add(1, std::memory_order_relaxed);
        counters_->queued_packed.fetch_add(1, std::memory_order_relaxed);
        arbiter_.submitDirectOwned(std::vector<uint8_t>(kick),
            [batch = std::move(packed), sink = sink_, counters = counters_](bool direct) {
                if (!direct) {
                    counters->ordering_fallback.fetch_add(1, std::memory_order_relaxed);
                    counters->raw_fallback.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
                DeliverPackedList(*sink, batch);
                counters->lowered.fetch_add(1, std::memory_order_relaxed);
                counters->lowered_packed.fetch_add(1, std::memory_order_relaxed);
            });
        return true;
    }
    counters_->unsupported.fetch_add(1, std::memory_order_relaxed);
    NoteUnsupported(kick);
    return false;
}

void KickLowering::NoteUnsupported(const std::vector<uint8_t>& kick) {
    // Same program (hash, name pointer, entry pc) and the same declined shape
    // -> same row. Names are the library's static strings, so pointer identity
    // is the name's identity.
    UnsupportedByProgram fresh;
    fresh.origin = origin_;
    DescribeShape(kick, fresh.shape, sizeof(fresh.shape));
    fresh.count = 1;
    std::lock_guard<std::mutex> lock(counters_->by_program_mutex);
    for (UnsupportedByProgram& row : counters_->by_program) {
        if (row.origin.program_hash == origin_.program_hash && row.origin.name == origin_.name &&
            row.origin.entry_pc == origin_.entry_pc && std::strcmp(row.shape, fresh.shape) == 0) {
            ++row.count;
            return;
        }
    }
    counters_->by_program.push_back(fresh);
}

void KickLowering::Submit(std::vector<uint8_t>&& kick) {
    Submit(std::move(kick), Origin{});
}

void KickLowering::Submit(std::vector<uint8_t>&& kick, const Origin& origin) {
    counters_->submitted.fetch_add(1, std::memory_order_relaxed);
    origin_ = origin;
    if (TryLower(kick)) return;
    counters_->raw_fallback.fetch_add(1, std::memory_order_relaxed);
    arbiter_.submitOwned(GifPath::Path1, std::move(kick));
}

uint64_t KickLowering::submitted() const {
    return counters_->submitted.load(std::memory_order_relaxed);
}

std::vector<KickLowering::UnsupportedByProgram> KickLowering::unsupported_by_program() const {
    std::vector<UnsupportedByProgram> rows;
    {
        std::lock_guard<std::mutex> lock(counters_->by_program_mutex);
        rows = counters_->by_program;
    }
    std::stable_sort(rows.begin(), rows.end(),
                     [](const UnsupportedByProgram& a, const UnsupportedByProgram& b) { return a.count > b.count; });
    return rows;
}

KickLowering::Census KickLowering::census() const {
    const auto& c = *counters_;
    Census census;
    census.submitted = c.submitted.load(std::memory_order_relaxed);
    census.queued = c.queued.load(std::memory_order_relaxed);
    census.lowered = c.lowered.load(std::memory_order_relaxed);
    census.queued_state = c.queued_state.load(std::memory_order_relaxed);
    census.lowered_state = c.lowered_state.load(std::memory_order_relaxed);
    census.queued_packed = c.queued_packed.load(std::memory_order_relaxed);
    census.lowered_packed = c.lowered_packed.load(std::memory_order_relaxed);
    census.raw_fallback = c.raw_fallback.load(std::memory_order_relaxed);
    census.unsupported = c.unsupported.load(std::memory_order_relaxed);
    census.unconfigured = c.unconfigured.load(std::memory_order_relaxed);
    census.ordering_fallback = c.ordering_fallback.load(std::memory_order_relaxed);
    return census;
}
