#include "engine/gs/hle/ir/register_op_stream.h"

#include <cstring>
#include <type_traits>

#include "engine/core/hash/hash.h"

static_assert(sizeof(RegisterOp) == 24, "RegisterOp is the hashed record; its byte image is the contract");
static_assert(std::is_trivially_copyable<RegisterOp>::value, "RegisterOp is hashed as raw bytes");

// IEEE bits of a float, hashed verbatim (never re-formatted).
static uint32_t FloatBits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}

RegisterOpStream::RegisterOpStream(bool retain_records)
    : hash_(ps2r::hash::kFnv1a64Offset), retain_(retain_records) {}

void RegisterOpStream::Clear() {
    ops_.clear();
    ops_.shrink_to_fit();
    hash_ = ps2r::hash::kFnv1a64Offset;
    count_ = 0;
    path_ = 0;
}

// Fold one zero-initialised record into the running hash; a retaining stream
// appends it as well (the hash never depends on the retained copy).
void RegisterOpStream::Push(RegisterOpKind kind, uint8_t reg, uint32_t aux, uint64_t lo, uint64_t hi) {
    RegisterOp op{};
    op.kind = static_cast<uint8_t>(kind);
    op.reg = reg;
    op.path = path_;
    op.pad = 0;
    op.aux = aux;
    op.lo = lo;
    op.hi = hi;
    hash_ = ps2r::hash::Fnv1a64(&op, sizeof(op), hash_);
    count_++;
    if (retain_) ops_.push_back(op);
}

// The accepted tag, re-packed so the record is independent of GifTag's layout.
void RegisterOpStream::BeginTag(const GifTag& tag, GifPath path) {
    path_ = static_cast<uint8_t>(path);
    uint32_t aux = tag.nloop;
    aux |= static_cast<uint32_t>(tag.eop ? 1u : 0u) << 15;
    aux |= static_cast<uint32_t>(tag.pre ? 1u : 0u) << 16;
    aux |= static_cast<uint32_t>(tag.flg) << 17;
    aux |= static_cast<uint32_t>(tag.nregs) << 19;
    uint64_t regs = 0;
    for (int i = 0; i < 16; ++i) regs |= static_cast<uint64_t>(tag.regs[i] & 0xFu) << (i * 4);
    Push(RegisterOpKind::Tag, 0, aux, tag.prim, regs);
}

void RegisterOpStream::WriteRegister(uint8_t reg, uint64_t value) {
    Push(RegisterOpKind::Reg, reg, 0, value, 0);
}

void RegisterOpStream::PackedRgbaq(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    const uint64_t lo = static_cast<uint64_t>(r) | (static_cast<uint64_t>(g) << 8) |
                        (static_cast<uint64_t>(b) << 16) | (static_cast<uint64_t>(a) << 24);
    Push(RegisterOpKind::Rgbaq, static_cast<uint8_t>(GsReg::Rgbaq), 0, lo, 0);
}

void RegisterOpStream::PackedSt(float s, float t, float q) {
    const uint64_t lo = static_cast<uint64_t>(FloatBits(s)) | (static_cast<uint64_t>(FloatBits(t)) << 32);
    Push(RegisterOpKind::St, static_cast<uint8_t>(GsReg::St), 0, lo, FloatBits(q));
}

void RegisterOpStream::PackedUv(uint16_t u, uint16_t v) {
    const uint64_t lo = static_cast<uint64_t>(u) | (static_cast<uint64_t>(v) << 16);
    Push(RegisterOpKind::Uv, static_cast<uint8_t>(GsReg::Uv), 0, lo, 0);
}

void RegisterOpStream::PackedFog(uint8_t f) {
    Push(RegisterOpKind::Fog, static_cast<uint8_t>(GsReg::Fog), 0, f, 0);
}

void RegisterOpStream::KickVertex(float x, float y, float z, uint8_t fog, bool draw) {
    const uint64_t lo = static_cast<uint64_t>(FloatBits(x)) | (static_cast<uint64_t>(FloatBits(y)) << 32);
    const uint64_t hi = static_cast<uint64_t>(FloatBits(z)) | (static_cast<uint64_t>(fog) << 32) |
                        (static_cast<uint64_t>(draw ? 1u : 0u) << 40);
    Push(RegisterOpKind::Kick, 0, 0, lo, hi);
}

void RegisterOpStream::ImageData(uint64_t lo, uint64_t hi) {
    Push(RegisterOpKind::Image, static_cast<uint8_t>(GsReg::Hwreg), 0, lo, hi);
}

void RegisterOpStream::EndOfPacket() { Push(RegisterOpKind::Eop, 0, 0, 0, 0); }

void RegisterOpStream::FinishPath3() { Push(RegisterOpKind::Finish, 0, 0, 0, 0); }
