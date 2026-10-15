// tests/gs/xfer/test_transfer_engine.cpp -- TransferEngine invariants, the
// HWREG cursor and the staged column store's equivalence to the per-pixel store.
// Usage: test_transfer_engine <case> (inv03 inv21 inv31 cursor staged)
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "engine/gs/format/psm_layout.h"
#include "engine/gs/hle/ir/draw_ir_sink.h"
#include "engine/gs/vram/vram_image.h"
#include "engine/gs/vram/vram_op_log.h"
#include "engine/gs/xfer/transfer_engine.h"
#include "tests/lib/capture_sink.h"

using namespace ps2r::gs;

static int g_checks = 0, g_fail = 0;
#define CHECK(c, msg)                                                       \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(c)) {                                                         \
            ++g_fail;                                                       \
            std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); \
        }                                                                   \
    } while (0)

namespace {

// Register encodings (GS User's Manual 4.2).
uint64_t Bitblt(uint32_t sbp_blk, uint32_t sbw64, uint32_t spsm, uint32_t dbp_blk, uint32_t dbw64, uint32_t dpsm) {
    return (uint64_t)(sbp_blk & 0x3FFF) | ((uint64_t)(sbw64 & 0x3F) << 16) | ((uint64_t)(spsm & 0x3F) << 24) |
           ((uint64_t)(dbp_blk & 0x3FFF) << 32) | ((uint64_t)(dbw64 & 0x3F) << 48) | ((uint64_t)(dpsm & 0x3F) << 56);
}
uint64_t Trxpos(uint32_t ssax, uint32_t ssay, uint32_t dsax, uint32_t dsay, uint32_t dir) {
    return (uint64_t)(ssax & 0x7FF) | ((uint64_t)(ssay & 0x7FF) << 16) | ((uint64_t)(dsax & 0x7FF) << 32) |
           ((uint64_t)(dsay & 0x7FF) << 48) | ((uint64_t)(dir & 3) << 59);
}
uint64_t Trxreg(uint32_t w, uint32_t h) { return (uint64_t)(w & 0xFFF) | ((uint64_t)(h & 0xFFF) << 32); }

constexpr uint32_t kDstBlk = 0x400;  // synthetic destination: block 0x400 = byte 0x40000 (page 32)

// A VRAM + transfer engine + capturing sink, wired.
struct Rig {
    VramImage vram;
    TransferEngine xfer{&vram};
    CaptureSink sink;
};
Rig* NewRig() {
    Rig* r = new Rig;
    r->xfer.SetSink(&r->sink);
    return r;
}
void Setup(Rig& r, uint32_t dpsm, uint32_t w, uint32_t h, uint32_t dbw64 = 1, uint32_t dsax = 0, uint32_t dsay = 0) {
    r.xfer.WriteRegister(0x50, Bitblt(0, 1, 0, kDstBlk, dbw64, dpsm));
    r.xfer.WriteRegister(0x51, Trxpos(0, 0, dsax, dsay, 0));
    r.xfer.WriteRegister(0x52, Trxreg(w, h));
    r.xfer.WriteRegister(0x53, 0);
}
size_t Jobs(const Rig& r, JobKind k) {
    size_t n = 0;
    for (const JobRec& j : r.sink.jobs) n += (j.type == (uint32_t)k);
    return n;
}

// Pack `n` pixels of `bits` each into a 64-bit IMAGE word (low pixel first).
uint64_t Pack(const std::vector<uint32_t>& px, uint32_t bits) {
    uint64_t w = 0;
    for (size_t i = 0; i < px.size(); ++i) w |= (uint64_t)(px[i] & ((bits == 32) ? 0xFFFFFFFFu : ((1u << bits) - 1))) << (i * bits);
    return w;
}

}  // namespace

// Invariant 3: TRXDIR latches the staged BITBLTBUF/TRXPOS/TRXREG; later writes to
// the three registers do not alter the transfer in flight.
static void Inv03() {
    std::unique_ptr<Rig> rig(NewRig());
    Rig& r = *rig;
    Setup(r, 0x00, 4, 2);  // CT32 4x2 at block 0x400
    CHECK(r.xfer.Active().dbp == kDstBlk * 256 && r.xfer.Active().rrw == 4 && r.xfer.Active().rrh == 2, "latched");
    CHECK(r.xfer.State().active && r.xfer.State().xdir == 0, "armed host->local");
    // Mid-transfer: restage a different destination and size -- must be ignored.
    r.xfer.WriteRegister(0x50, Bitblt(0, 1, 0, 0x800, 4, 0x02));
    r.xfer.WriteRegister(0x52, Trxreg(1, 1));
    r.xfer.WriteRegister(0x51, Trxpos(0, 0, 7, 7, 0));
    CHECK(r.xfer.Active().dbp == kDstBlk * 256 && r.xfer.Active().rrw == 4 && r.xfer.Active().dpsm == 0, "active unchanged");
    CHECK(r.xfer.Staged().dbp == 0x800 * 256 && r.xfer.Staged().rrw == 1, "staged updated");
    uint64_t w = Pack({0x11111111u, 0x22222222u}, 32);
    r.xfer.WriteHwreg(w);
    r.xfer.WriteHwreg(Pack({0x33333333u, 0x44444444u}, 32));
    r.xfer.WriteHwreg(Pack({0x55555555u, 0x66666666u}, 32));
    CHECK(r.xfer.State().active, "3 words = 6 of 8 pixels: still active");
    r.xfer.WriteHwreg(Pack({0x77777777u, 0x88888888u}, 32));
    CHECK(!r.xfer.State().active, "8 pixels: complete");
    const uint32_t expect[8] = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u, 0x55555555u, 0x66666666u, 0x77777777u, 0x88888888u};
    bool ok = true;
    for (uint32_t y = 0; y < 2; ++y)
        for (uint32_t x = 0; x < 4; ++x) {
            uint32_t v;
            std::memcpy(&v, r.vram.Bytes() + PsmLayout::PixelByteAddress(Psm::CT32, x, y, kDstBlk, 1), 4);
            ok &= v == expect[y * 4 + x];
        }
    CHECK(ok, "pixels landed at the LATCHED destination through PsmLayout");
    CHECK(Jobs(r, JobKind::InvalidateTextures) == 1 && Jobs(r, JobKind::UploadVRAMTexture) == 1, "completion jobs");
    const JobRec& up = r.sink.jobs.back();
    CHECK(up.arg1 == kDstBlk * 256 && up.arg2 == 4 && up.arg3 == 2 && up.arg4 == 32 && up.payload_bytes == 32,
          "upload job args (dbp, w, h, bytes, payload)");
    CHECK(r.xfer.LastUploadPixels().size() == 32 && r.xfer.LastUploadPixels()[3] == 0x22, "RGBA8 linear copy: a' = min(2a,255)");
    // A stale TRXREG write after completion + a new TRXDIR latches the NEW staged set.
    r.xfer.WriteRegister(0x53, 0);
    CHECK(r.xfer.Active().dbp == 0x800 * 256 && r.xfer.Active().rrw == 1 && r.xfer.Active().dpsm == 0x02, "new latch");
    CHECK(r.xfer.State().cur_x == 7 && r.xfer.State().cur_y == 7, "cursor armed at the new DSAX/DSAY");
    // Only the host->local arm touches the active flag: an unfinished upload
    // stays armed across TRXDIR 1/3, refuses data meanwhile, and is flushed
    // partially (to its OWN destination) by the next host->local start.
    {
        std::unique_ptr<Rig> rig2(NewRig());
        Rig& q = *rig2;
        Setup(q, 0x00, 4, 2);  // CT32 4x2 at block 0x400
        q.xfer.WriteHwreg(Pack({1, 2}, 32));
        q.xfer.WriteHwreg(Pack({3, 4}, 32));  // 4 of 8 pixels = one full row
        q.xfer.WriteRegister(0x53, 3);
        CHECK(q.xfer.State().active && q.xfer.State().xdir == 3 && q.xfer.State().pixels_written == 4,
              "TRXDIR 3 leaves the unfinished upload armed");
        q.xfer.WriteHwreg(Pack({9, 9}, 32));
        CHECK(q.xfer.State().pixels_written == 4 && q.xfer.Stats().hwreg == 2, "data refused while XDIR != 0");
        q.xfer.WriteRegister(0x53, 1);
        CHECK(q.xfer.State().active && q.xfer.Stats().local_host == 1, "TRXDIR 1 leaves it armed too");
        const size_t jobs_before = q.sink.jobs.size();
        q.xfer.WriteRegister(0x50, Bitblt(0, 1, 0, 0x800, 1, 0x00));
        q.xfer.WriteRegister(0x53, 0);
        CHECK(q.sink.jobs.size() == jobs_before + 2, "the next host->local start flushes the partial upload");
        const JobRec& pu = q.sink.jobs.back();
        CHECK(pu.type == (uint32_t)JobKind::UploadVRAMTexture && pu.arg1 == kDstBlk * 256 && pu.arg2 == 4 && pu.arg3 == 1 &&
                  pu.arg4 == 16,
              "partial upload attributed to its own destination, 4x1");
        CHECK(q.xfer.State().active && q.xfer.State().pixels_written == 0 && q.xfer.Active().dbp == 0x800 * 256,
              "the new transfer is armed");
    }
    // The partial flush's payload is sized from the LATCHED geometry, which an
    // intermediate TRXDIR may have widened beyond the buffer in flight: the copy
    // is bounded (real bytes, then zeros), never a read past the buffer.
    // Variant A: intermediate TRXDIR 3 (idle); variant B: intermediate TRXDIR 2
    // (local->local copy) -- both latch the 64x64 TRXREG into the flush geometry.
    for (uint32_t mid = 3; mid >= 2; --mid) {
        std::unique_ptr<Rig> rig3(NewRig());
        Rig& q = *rig3;
        Setup(q, 0x00, 4, 2);  // CT32 4x2 at block 0x400: linear_ = 32 bytes
        q.xfer.WriteHwreg(Pack({0x11111111u, 0x22222222u}, 32));
        q.xfer.WriteHwreg(Pack({0x33333333u, 0x44444444u}, 32));  // 4 of 8 pixels
        q.xfer.WriteRegister(0x52, Trxreg(64, 64));               // restage LARGER
        q.xfer.WriteRegister(0x53, mid);
        CHECK(q.xfer.State().active && q.xfer.State().xdir == mid && q.xfer.Active().rrw == 64 && q.xfer.Active().rrh == 64,
              "intermediate TRXDIR latched the wider geometry, upload still armed");
        const size_t jobs_before = q.sink.jobs.size();
        q.xfer.WriteRegister(0x50, Bitblt(0, 1, 0, 0x800, 1, 0x00));
        q.xfer.WriteRegister(0x53, 0);
        CHECK(q.sink.jobs.size() == jobs_before + 2, "TRXDIR 0 flushes the partial upload");
        const JobRec& pu = q.sink.jobs.back();
        const uint32_t pw = 64, ph = 1;  // 4 pixels / 64 wide -> 0 rows, clamped to 1
        CHECK(pu.type == (uint32_t)JobKind::UploadVRAMTexture && pu.arg2 == pw && pu.arg3 == ph && pu.arg4 == pw * ph * 4 &&
                  pu.payload_bytes == pw * ph * 4,
              "partial upload sized from the latched geometry: payload_bytes == pw*ph*4");
        const std::vector<uint8_t>& p = q.xfer.LastUploadPixels();
        CHECK(p.size() == pw * ph * 4, "payload vector is pw*ph*4 bytes");
        const uint8_t expect_prefix[16] = {0x11, 0x11, 0x11, 0x22, 0x22, 0x22, 0x22, 0x44, 0x33, 0x33, 0x33, 0x66, 0x44, 0x44, 0x44, 0x88};
        CHECK(p.size() >= 32 && std::memcmp(p.data(), expect_prefix, 16) == 0, "prefix == the written pixels (RGBA8, a'=min(2a,255))");
        bool tail_zero = true;
        for (size_t i = 32; i < p.size(); ++i) tail_zero &= p[i] == 0;
        CHECK(tail_zero, "tail past the in-flight buffer is all zero");
        // Bytes 16..31 are the unwritten half of the in-flight buffer: zero too.
        bool mid_zero = true;
        for (size_t i = 16; i < 32; ++i) mid_zero &= p[i] == 0;
        CHECK(mid_zero, "unwritten pixels of the in-flight buffer are zero");
    }
}

// Invariant 31: a completed transfer re-armed by further IMAGE data re-stamps
// the destination pages with a fresh upload epoch.
static void Inv31() {
    std::unique_ptr<Rig> rig(NewRig());
    Rig& r = *rig;
    Setup(r, 0x02, 4, 1);  // CT16 4x1 = one 64-bit word
    const uint64_t e0 = r.vram.Page(kDstBlk * 256 / 8192).upload_epoch;
    CHECK(e0 == 1, "TRXDIR stamps the destination (epoch 1)");
    uint8_t img[8];
    uint64_t w = Pack({0x1111u, 0x2222u, 0x3333u, 0x4444u}, 16);
    std::memcpy(img, &w, 8);
    r.xfer.WriteImage(img, 8);
    CHECK(!r.xfer.State().active, "complete after one word");
    const uint64_t e1 = r.vram.Page(kDstBlk * 256 / 8192).upload_epoch;
    CHECK(e1 > e0, "completion settles (epoch advanced)");
    r.xfer.WriteImage(img, 8);  // re-arm at the same geometry
    const uint64_t e2 = r.vram.Page(kDstBlk * 256 / 8192).upload_epoch;
    CHECK(e2 > e1, "re-arm re-stamps the destination");
    CHECK(r.xfer.Stats().completed == 2, "second transfer completed");
    // The linear payload moved out with the first upload job; a re-armed transfer
    // writes VRAM and invalidates but emits no second upload payload (finding
    // P6-REARM-NO-UPLOAD: the original runtime never re-allocates the buffer).
    CHECK(Jobs(r, JobKind::UploadVRAMTexture) == 1 && Jobs(r, JobKind::InvalidateTextures) == 2, "re-arm: invalidate only");
    uint16_t v0;
    std::memcpy(&v0, r.vram.Bytes() + PsmLayout::PixelByteAddress(Psm::CT16, 0, 0, kDstBlk, 1), 2);
    CHECK(v0 == 0x1111, "re-armed data still lands in VRAM");
    // Register write after completion but without new data: no stamp.
    const uint64_t e3 = r.vram.Page(kDstBlk * 256 / 8192).upload_epoch;
    r.xfer.WriteRegister(0x52, Trxreg(4, 1));
    CHECK(r.vram.Page(kDstBlk * 256 / 8192).upload_epoch == e3, "staging alone never stamps");
    // TRXDIR=3 then IMAGE data: the re-arm sets the active flag whatever the
    // direction, but XDIR 3 neither stamps nor stores (the data is refused).
    r.xfer.WriteRegister(0x53, 3);
    CHECK(!r.xfer.State().active && r.xfer.State().xdir == 3, "TRXDIR 3 after completion: idle, inactive");
    const uint64_t e4 = r.vram.Page(kDstBlk * 256 / 8192).upload_epoch;
    const uint64_t up4 = r.vram.UploadEpoch();
    uint8_t img2[8];
    const uint64_t w2 = Pack({0xAAAAu, 0xBBBBu, 0xCCCCu, 0xDDDDu}, 16);
    std::memcpy(img2, &w2, 8);
    r.xfer.WriteImage(img2, 8);
    CHECK(r.xfer.State().active && r.xfer.State().pixels_written == 0, "xdir 3: IMAGE data re-arms (active flag set)");
    CHECK(r.vram.Page(kDstBlk * 256 / 8192).upload_epoch == e4 && r.vram.UploadEpoch() == up4, "xdir 3: no stamp");
    std::memcpy(&v0, r.vram.Bytes() + PsmLayout::PixelByteAddress(Psm::CT16, 0, 0, kDstBlk, 1), 2);
    CHECK(v0 == 0x1111 && r.xfer.Stats().hwreg == 2, "xdir 3: no store, data ignored");
    CHECK(r.xfer.Stats().completed == 2, "xdir 3: nothing completes");
}

// Invariant 21: a local->local copy whose source pages a GPU target authored is
// detected (the mirror may lag the target) and the copy itself is exact.
static void Inv21() {
    std::unique_ptr<Rig> rig(NewRig());
    Rig& r = *rig;
    // Source: CT32 2x2 at block 0x100 (byte 0x10000), written by the EE side.
    const uint32_t src_blk = 0x100;
    const uint32_t px[4] = {0xA0A0A0A0u, 0xB1B1B1B1u, 0xC2C2C2C2u, 0xD3D3D3D3u};
    for (uint32_t i = 0; i < 4; ++i) {
        uint8_t b[4];
        std::memcpy(b, &px[i], 4);
        r.vram.WriteStream(PsmLayout::PixelByteAddress(Psm::CT32, i % 2, i / 2, src_blk, 1), b, 4);
    }
    r.vram.SettleWrites();
    r.xfer.WriteRegister(0x50, Bitblt(src_blk, 1, 0x00, kDstBlk, 1, 0x00));
    r.xfer.WriteRegister(0x51, Trxpos(0, 0, 0, 0, 0));
    r.xfer.WriteRegister(0x52, Trxreg(2, 2));
    r.xfer.WriteRegister(0x53, 2);
    CHECK(r.xfer.Stats().local_local == 1 && r.xfer.Stats().local_local_over_gpu == 0, "CPU-authored source: not flagged");
    bool ok = true;
    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t v;
        std::memcpy(&v, r.vram.Bytes() + PsmLayout::PixelByteAddress(Psm::CT32, i % 2, i / 2, kDstBlk, 1), 4);
        ok &= v == px[i];
    }
    CHECK(ok, "local->local copies exactly");
    CHECK(Jobs(r, JobKind::InvalidateTextures) == 1, "invalidate job for the destination");
    // Direction 3 (-x, -y) copies the same rectangle (order reversed, result equal for disjoint buffers).
    r.xfer.WriteRegister(0x50, Bitblt(src_blk, 1, 0x00, 0x600, 1, 0x00));
    r.xfer.WriteRegister(0x51, Trxpos(0, 0, 0, 0, 3));
    r.xfer.WriteRegister(0x53, 2);
    ok = true;
    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t v;
        std::memcpy(&v, r.vram.Bytes() + PsmLayout::PixelByteAddress(Psm::CT32, i % 2, i / 2, 0x600, 1), 4);
        ok &= v == px[i];
    }
    CHECK(ok, "dir 3 copy equals dir 0 copy");
    // CT24 destination keeps the destination's upper byte.
    uint8_t up[4] = {0, 0, 0, 0xEE};
    r.vram.WriteStream(PsmLayout::PixelByteAddress(Psm::CT32, 0, 0, 0x700, 1), up, 4);
    r.xfer.WriteRegister(0x50, Bitblt(src_blk, 1, 0x00, 0x700, 1, 0x01));
    r.xfer.WriteRegister(0x51, Trxpos(0, 0, 0, 0, 0));
    r.xfer.WriteRegister(0x52, Trxreg(1, 1));
    r.xfer.WriteRegister(0x53, 2);
    CHECK(r.vram.Bytes()[PsmLayout::PixelByteAddress(Psm::CT32, 0, 0, 0x700, 1) + 3] == 0xEE &&
              r.vram.Bytes()[PsmLayout::PixelByteAddress(Psm::CT32, 0, 0, 0x700, 1)] == 0xA0,
          "CT24 local->local preserves byte 3");
    // GPU-authored source pages are detected.
    r.vram.NoteGpuAuthor(VramImage::Span{src_blk * 256, 8192}, src_blk * 256);
    r.xfer.WriteRegister(0x50, Bitblt(src_blk, 1, 0x00, kDstBlk, 1, 0x00));
    r.xfer.WriteRegister(0x52, Trxreg(2, 2));
    r.xfer.WriteRegister(0x53, 2);
    CHECK(r.xfer.Stats().local_local_over_gpu == 1, "GPU-authored source detected");
    CHECK(r.vram.Page(kDstBlk * 256 / 8192).author == PageRecord::Author::Cpu, "destination becomes CPU-authored");
}

// The HWREG cursor per PSM: exact VRAM bytes via PsmLayout, completion at
// RRW*RRH, row wrap inside a word, CT24 byte-3 rule, T4 drop, DSAX/DSAY offset.
static void Cursor() {
    // CT32 3x2 at dsax=1, dsay=2 (row wrap inside the second word).
    {
        std::unique_ptr<Rig> rig(NewRig());
    Rig& r = *rig;
        Setup(r, 0x00, 3, 2, 1, 1, 2);
        const uint32_t px[6] = {1, 2, 3, 4, 5, 6};
        r.xfer.WriteHwreg(Pack({px[0], px[1]}, 32));
        r.xfer.WriteHwreg(Pack({px[2], px[3]}, 32));
        CHECK(r.xfer.State().active && r.xfer.State().pixels_written == 4, "4 of 6 written");
        r.xfer.WriteHwreg(Pack({px[4], px[5]}, 32));
        CHECK(!r.xfer.State().active, "complete at rrw*rrh");
        bool ok = true;
        for (uint32_t i = 0; i < 6; ++i) {
            uint32_t v;
            std::memcpy(&v, r.vram.Bytes() + PsmLayout::PixelByteAddress(Psm::CT32, 1 + i % 3, 2 + i / 3, kDstBlk, 1), 4);
            ok &= v == px[i];
        }
        CHECK(ok, "CT32 pixels at (dsax+x, dsay+y)");
        // Extra data after completion re-arms and writes the same rect again.
        r.xfer.WriteHwreg(Pack({9, 9}, 32));
        CHECK(r.xfer.Stats().hwreg == 3, "HWREG while inactive is dropped (re-arm is the IMAGE path)");
    }
    // CT24: three bytes stored, byte 3 forced to 0x80.
    {
        std::unique_ptr<Rig> rig(NewRig());
    Rig& r = *rig;
        Setup(r, 0x01, 2, 1);
        r.xfer.WriteHwreg(Pack({0x00112233u, 0xFF445566u}, 32));
        const uint8_t* p0 = r.vram.Bytes() + PsmLayout::PixelByteAddress(Psm::CT24, 0, 0, kDstBlk, 1);
        const uint8_t* p1 = r.vram.Bytes() + PsmLayout::PixelByteAddress(Psm::CT24, 1, 0, kDstBlk, 1);
        CHECK(p0[0] == 0x33 && p0[1] == 0x22 && p0[2] == 0x11 && p0[3] == 0x80, "CT24 pixel 0: rgb + 0x80");
        CHECK(p1[0] == 0x66 && p1[1] == 0x55 && p1[2] == 0x44 && p1[3] == 0x80, "CT24 pixel 1: upper byte forced 0x80");
        CHECK(!r.xfer.State().active, "CT24 completes at 2 pixels");
        CHECK(r.xfer.LastUploadPixels()[3] == 255, "CT24 linear alpha 255");
    }
    // CT16: 4 pixels per word; linear copy expands 5-bit channels.
    {
        std::unique_ptr<Rig> rig(NewRig());
    Rig& r = *rig;
        Setup(r, 0x02, 4, 1);
        r.xfer.WriteHwreg(Pack({0x001Fu, 0x03E0u, 0x7C00u, 0x8000u}, 16));
        uint16_t v[4];
        for (uint32_t x = 0; x < 4; ++x) std::memcpy(&v[x], r.vram.Bytes() + PsmLayout::PixelByteAddress(Psm::CT16, x, 0, kDstBlk, 1), 2);
        CHECK(v[0] == 0x001F && v[1] == 0x03E0 && v[2] == 0x7C00 && v[3] == 0x8000, "CT16 halfwords");
        const std::vector<uint8_t>& l = r.xfer.LastUploadPixels();
        CHECK(l.size() == 16 && l[0] == 0xF8 && l[1] == 0 && l[3] == 0 && l[5] == 0xF8 && l[10] == 0xF8 && l[15] == 255,
              "CT16 linear expansion (<<3, alpha from bit 15)");
    }
    // T8: 8 pixels per word.
    {
        std::unique_ptr<Rig> rig(NewRig());
    Rig& r = *rig;
        Setup(r, 0x13, 8, 1, 2);  // dbw 128 px = one T8 page row
        r.xfer.WriteHwreg(0x0807060504030201ull);
        bool ok = true;
        for (uint32_t x = 0; x < 8; ++x) ok &= r.vram.Bytes()[PsmLayout::PixelByteAddress(Psm::T8, x, 0, kDstBlk, 2)] == x + 1;
        CHECK(ok, "T8 bytes");
        CHECK(!r.xfer.State().active, "T8 completes at 8");
    }
    // T4: cursor advances 16 per word and the transfer completes, but no nibble
    // is stored (upload-drop compatibility with the original runtime); every
    // pixel still counts as a written byte, so the completion settle bumps the
    // upload epoch exactly as a storing PSM does.
    {
        std::unique_ptr<Rig> rig(NewRig());
    Rig& r = *rig;
        Setup(r, 0x14, 16, 1, 2);
        const uint64_t up0 = r.vram.UploadEpoch();  // 1: the TRXDIR stamp
        const uint64_t settles0 = r.vram.Stats().settles;
        r.xfer.WriteHwreg(0xFEDCBA9876543210ull);
        CHECK(!r.xfer.State().active && r.xfer.State().pixels_written == 16, "T4 completes at 16 pixels");
        bool zero = true;
        for (uint32_t i = 0; i < 8192; ++i) zero &= r.vram.Bytes()[kDstBlk * 256 + i] == 0;
        CHECK(zero, "T4 host->local stores nothing (bug-compatible drop)");
        CHECK(r.vram.UploadEpoch() == up0 + 1 && r.vram.Stats().settles == settles0 + 1,
              "T4 completion settles the noted pixels: upload epoch +1");
        CHECK(r.vram.Page(kDstBlk * 256 / 8192).upload_epoch == up0 + 1, "the destination page carries the settle epoch");
        CHECK(r.vram.Page(kDstBlk * 256 / 8192).write_bits == 0 && r.vram.Stats().stream_writes == 0,
              "settle consumed the marks; no byte stream was written");
        CHECK(Jobs(r, JobKind::UploadVRAMTexture) == 1 && r.sink.jobs.back().arg4 == 16 && r.sink.jobs.back().vram_epoch == up0 + 1,
              "T4 upload job charged 1 byte/pixel and stamped with the settled epoch");
    }
    // Pixel-size table.
    CHECK(TransferEngine::PixelSize(0x00) == 4 && TransferEngine::PixelSize(0x02) == 2 && TransferEngine::PixelSize(0x13) == 1 &&
              TransferEngine::PixelSize(0x14) == 1 && TransferEngine::PixelSize(0x1B) == 4,
          "PixelSize table");
    CHECK(TransferEngine::PixelsPerWord(0x01) == 2 && TransferEngine::PixelsPerWord(0x0A) == 4 &&
              TransferEngine::PixelsPerWord(0x13) == 8 && TransferEngine::PixelsPerWord(0x14) == 16 &&
              TransferEngine::PixelsPerWord(0x1B) == 2,
          "PixelsPerWord table");
}

// The staged column store (WriteImage spans) against the per-pixel reference --
// every pixel of the transfer stored at PsmLayout::PixelByteAddress with the
// store rules (CT32 4 bytes, CT24 3 + 0x80, CT16 2, T8 1, T4 nothing) as its own
// WriteStream -- over random geometry per PSM (base block, DBW, DSAX/DSAY off the
// column grid, RRW/RRH spanning page columns), spans cut at random quadword
// counts, and the word-at-a-time HWREG register path. Compared: every VRAM byte,
// every page's write bits / author / settle membership, the RGBA8 linear copy, the
// completion state, and the VramOpLog replay of the staged image onto a fresh
// image (the pipelined consumer's view). A partial transfer re-armed by a new
// TRXDIR keeps its bytes.
namespace {
// A 64-bit LCG (the test's only randomness; the seed is fixed).
uint32_t NextRand(uint64_t& s) { s = s * 6364136223846793005ull + 1442695040888963407ull; return static_cast<uint32_t>(s >> 33); }
uint32_t RandBelow(uint64_t& s, uint32_t n) { return n ? NextRand(s) % n : 0; }
struct Geom { uint32_t psm, dbp_blk, dbw64, dsax, dsay, rrw, rrh; };

uint32_t RefPixelSize(uint32_t psm) { return TransferEngine::PixelSize(psm); }

// The reference store of one transfer's pixel stream into `img` (and `lin`).
void ReferenceStore(VramImage& img, const Geom& g, const std::vector<uint32_t>& px, std::vector<uint8_t>* lin) {
    Psm mode;
    const bool known = PsmLayout::FromField(g.psm, &mode);
    lin->assign(static_cast<size_t>(g.rrw) * g.rrh * 4, 0);
    size_t i = 0;
    for (uint32_t y = g.dsay; y < g.dsay + g.rrh; ++y) {
        for (uint32_t x = g.dsax; x < g.dsax + g.rrw; ++x, ++i) {
            if (i >= px.size()) return;
            const uint32_t p = g.psm == 0x01 ? (px[i] & 0xFFFFFF) : px[i];
            const uint32_t addr = known ? PsmLayout::PixelByteAddress(mode, x, y, g.dbp_blk, g.dbw64)
                                        : (g.dbp_blk * 256 + (y * g.dbw64 * 64 + x) * RefPixelSize(g.psm)) % VramImage::kBytes;
            uint8_t b[4];
            switch (g.psm) {
                case 0x00: b[0] = p; b[1] = p >> 8; b[2] = p >> 16; b[3] = p >> 24; img.WriteStream(addr, b, 4); break;
                case 0x01: b[0] = p; b[1] = p >> 8; b[2] = p >> 16; b[3] = 0x80; img.WriteStream(addr, b, 4); break;
                case 0x02: case 0x0A: b[0] = p; b[1] = p >> 8; img.WriteStream(addr, b, 2); break;
                case 0x13: b[0] = p; img.WriteStream(addr, b, 1); break;
                default: img.NoteWriteNoBytes(addr, RefPixelSize(g.psm)); break;
            }
            uint8_t* l = lin->data() + i * 4;
            switch (g.psm) {
                case 0x00: { const uint32_t a = p >> 24; l[0] = p; l[1] = p >> 8; l[2] = p >> 16; l[3] = a * 2 > 255 ? 255 : a * 2; break; }
                case 0x01: l[0] = p; l[1] = p >> 8; l[2] = p >> 16; l[3] = 255; break;
                case 0x02: case 0x0A: l[0] = (p & 0x1F) << 3; l[1] = ((p >> 5) & 0x1F) << 3; l[2] = ((p >> 10) & 0x1F) << 3; l[3] = (p & 0x8000) ? 255 : 0; break;
                default: l[0] = p; l[1] = 0; l[2] = 0; l[3] = 255; break;
            }
        }
    }
    img.SettleWrites();
}

// The pixel stream packed into IMAGE words (PixelsPerWord per 64 bits).
std::vector<uint8_t> PackStream(const Geom& g, const std::vector<uint32_t>& px) {
    const uint32_t ppw = TransferEngine::PixelsPerWord(g.psm);
    const uint32_t bits = 64 / ppw;
    std::vector<uint8_t> out;
    for (size_t i = 0; i < px.size(); i += ppw) {
        uint64_t w = 0;
        for (uint32_t k = 0; k < ppw && i + k < px.size(); ++k)
            w |= (static_cast<uint64_t>(px[i + k]) & (bits == 64 ? ~0ull : ((1ull << bits) - 1))) << (k * bits);
        uint8_t b[8];
        std::memcpy(b, &w, 8);
        out.insert(out.end(), b, b + 8);
    }
    while (out.size() % 16) out.push_back(0);   // the stream is quadwords
    return out;
}

bool SameImage(const VramImage& a, const VramImage& b, const char* what) {
    bool same_bytes = std::memcmp(a.Bytes(), b.Bytes(), VramImage::kBytes) == 0;
    bool same_marks = true;
    for (uint32_t pg = 0; pg < VramImage::kPageCount && same_marks; ++pg) {
        const PageRecord& pa = a.Page(pg);
        const PageRecord& pb = b.Page(pg);
        same_marks = pa.write_bits == pb.write_bits && pa.author == pb.author && pa.upload_epoch == pb.upload_epoch;
    }
    if (!same_bytes || !same_marks) std::fprintf(stderr, "  %s: bytes %s marks %s\n", what, same_bytes ? "same" : "DIFFER", same_marks ? "same" : "DIFFER");
    return same_bytes && same_marks;
}
}  // namespace

static void Staged() {
    const uint32_t psms[] = {0x00, 0x01, 0x02, 0x0A, 0x13, 0x14};
    uint64_t rng = 0x5EEDC01Dull;
    int cases = 0;
    for (uint32_t psm : psms) {
        Psm mode;
        const bool known = PsmLayout::FromField(psm, &mode);
        const PsmDescriptor* d = known ? &PsmLayout::Describe(mode) : nullptr;
        for (int rep = 0; rep < 24; ++rep, ++cases) {
            Geom g;
            g.psm = psm;
            g.dbp_blk = rep % 3 == 0 ? 0 : RandBelow(rng, 0x3C00);
            g.dbw64 = 1 + RandBelow(rng, 8);                   // 64..512 px wide buffers
            const uint32_t pw = d ? d->page_w : 64;
            g.dsax = RandBelow(rng, pw + 5);                   // off the column grid, may cross a page
            g.dsay = RandBelow(rng, 40);
            // Widths in whole quadwords of pixels (a stream is quadwords; a trailing
            // partial quadword would re-arm the transfer on both arms alike, which
            // is Inv31's case, not this one): narrow and page-spanning.
            const uint32_t ppqw = 2 * TransferEngine::PixelsPerWord(psm);
            g.rrw = ppqw * (1 + RandBelow(rng, rep % 2 ? (2 * pw) / ppqw : 3));
            g.rrh = 1 + RandBelow(rng, 12);
            const size_t n = static_cast<size_t>(g.rrw) * g.rrh;
            std::vector<uint32_t> px(n);
            for (uint32_t& v : px) v = NextRand(rng) | (NextRand(rng) << 16);
            // Reference.
            VramImage ref;
            std::vector<uint8_t> ref_lin;
            ref.NoteStreamWrite(VramImage::Span{g.dbp_blk * 256, g.rrw * g.rrh * RefPixelSize(psm)});
            ReferenceStore(ref, g, px, &ref_lin);
            // Staged: WriteImage spans of random quadword counts.
            const std::vector<uint8_t> stream = PackStream(g, px);
            std::unique_ptr<Rig> rig(NewRig());
            Rig& r = *rig;
            VramOpLog log;
            r.vram.SetMirror(&log);
            r.xfer.WriteRegister(0x50, Bitblt(0, 1, 0, g.dbp_blk, g.dbw64, psm));
            r.xfer.WriteRegister(0x51, Trxpos(0, 0, g.dsax, g.dsay, 0));
            r.xfer.WriteRegister(0x52, Trxreg(g.rrw, g.rrh));
            r.xfer.WriteRegister(0x53, 0);
            size_t off = 0;
            while (off < stream.size()) {
                size_t qw = 1 + RandBelow(rng, rep % 4 == 0 ? 1 : 300);
                if (rep % 4 == 1) qw = stream.size() / 16;    // one span
                size_t bytes = std::min(qw * 16, stream.size() - off);
                r.xfer.WriteImage(stream.data() + off, bytes);
                off += bytes;
            }
            char what[96];
            std::snprintf(what, sizeof(what), "psm %02x rep %d (%ux%u at %u,%u bw %u)", psm, rep, g.rrw, g.rrh, g.dsax, g.dsay, g.dbw64);
            CHECK(!r.xfer.State().active, "the transfer completed");
            CHECK(SameImage(r.vram, ref, what), "staged span store == per-pixel reference (bytes + marks)");
            if (psm != 0x14) CHECK(r.xfer.LastUploadPixels() == ref_lin, "RGBA8 linear copy identical");
            // The op log replayed on a fresh image reproduces the staged image.
            VramImage replayed;
            log.Replay(replayed);
            CHECK(SameImage(replayed, r.vram, "op-log replay"), "VramOpLog replay == the staged image");
            // The register path (one HWREG word per call) lands the same bytes.
            std::unique_ptr<Rig> rig2(NewRig());
            Rig& q = *rig2;
            q.xfer.WriteRegister(0x50, Bitblt(0, 1, 0, g.dbp_blk, g.dbw64, psm));
            q.xfer.WriteRegister(0x51, Trxpos(0, 0, g.dsax, g.dsay, 0));
            q.xfer.WriteRegister(0x52, Trxreg(g.rrw, g.rrh));
            q.xfer.WriteRegister(0x53, 0);
            for (size_t o = 0; o + 8 <= stream.size(); o += 8) {
                uint64_t w;
                std::memcpy(&w, stream.data() + o, 8);
                q.xfer.WriteHwreg(w);
            }
            CHECK(SameImage(q.vram, ref, "HWREG word path"), "HWREG word-at-a-time == reference");
        }
    }
    // A partial transfer (half the words) then a new TRXDIR: the half's bytes are
    // in VRAM (the stage flushed before the latch) and the partial-flush jobs fire.
    {
        std::unique_ptr<Rig> rig(NewRig());
        Rig& r = *rig;
        Setup(r, 0x00, 16, 4);   // CT32 16x4 at block 0x400: 64 pixels, 32 words
        std::vector<uint32_t> px(64);
        for (uint32_t i = 0; i < 64; ++i) px[i] = 0x01010101u * (i + 1);
        Geom g{0x00, kDstBlk, 1, 0, 0, 16, 4};
        const std::vector<uint8_t> stream = PackStream(g, px);
        r.xfer.WriteImage(stream.data(), stream.size() / 2);   // rows 0-1 only
        CHECK(r.xfer.State().active && r.xfer.State().pixels_written == 32, "half the pixels in flight");
        VramImage ref;
        std::vector<uint8_t> lin;
        ref.NoteStreamWrite(VramImage::Span{kDstBlk * 256, 16 * 4 * 4});
        ReferenceStore(ref, g, std::vector<uint32_t>(px.begin(), px.begin() + 32), &lin);
        CHECK(std::memcmp(r.vram.Bytes(), ref.Bytes(), VramImage::kBytes) == 0, "the span's bytes are in VRAM at the span's end");
        r.xfer.WriteRegister(0x50, Bitblt(0, 1, 0, 0x800, 1, 0x02));
        r.xfer.WriteRegister(0x52, Trxreg(2, 2));
        r.xfer.WriteRegister(0x53, 0);
        CHECK(Jobs(r, JobKind::UploadVRAMTexture) == 1, "partial flush job for the half transfer");
        CHECK(std::memcmp(r.vram.Bytes(), ref.Bytes(), VramImage::kBytes) == 0, "the half's bytes survive the re-latch");
    }
    std::printf("staged store: %d random transfers x 6 PSMs compared\n", cases);
}

int main(int argc, char** argv) {
    const std::string c = argc > 1 ? argv[1] : "all";
    if (c == "inv03" || c == "all") Inv03();
    if (c == "inv21" || c == "all") Inv21();
    if (c == "inv31" || c == "all") Inv31();
    if (c == "cursor" || c == "all") Cursor();
    if (c == "staged" || c == "all") Staged();
    std::printf("test_transfer_engine %s: %d checks, %d failures\n", c.c_str(), g_checks, g_fail);
    return g_fail ? 1 : 0;
}
