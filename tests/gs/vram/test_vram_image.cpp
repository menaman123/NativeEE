// tests/gs/vram/test_vram_image.cpp -- VramImage / PageRecord invariants.
// Usage: test_vram_image <case>   (inv04 inv05 inv06 inv07 inv08 inv16 inv22 inv36)
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/gs/vram/page_record.h"
#include "engine/gs/vram/vram_image.h"
#include "engine/gs/vram/vram_op_log.h"

using ps2r::gs::PageRecord;
using ps2r::gs::VramImage;
using ps2r::gs::VramOpLog;

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
constexpr uint32_t kPage = VramImage::kPageBytes;
constexpr uint32_t kBase = 0x40000;  // synthetic: page 32

std::vector<uint8_t> Fill(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(seed + i * 7);
    return v;
}
}  // namespace

// Invariant 4: a reader observes exactly the bytes a later commit would produce
// (mirror overlaid with the pending shadow), never the raw mirror.
static void Inv04() {
    VramImage vram;
    auto ee = Fill(kPage, 1);
    vram.WriteStream(kBase, ee.data(), ee.size());
    std::vector<uint8_t> out(kPage);
    vram.ReadView(kBase, kPage, out.data());
    CHECK(out == ee, "view == EE bytes with no deposit");
    vram.SettleWrites();
    auto gpu = Fill(kPage, 90);
    vram.StageDeposit(kBase, gpu.data(), gpu.size(), vram.UploadEpoch());
    vram.ReadView(kBase, kPage, out.data());
    CHECK(out == gpu, "view shows the pending deposit before commit");
    CHECK(std::memcmp(vram.Bytes() + kBase, ee.data(), kPage) == 0, "raw mirror untouched by staging");
    // Partial deposit inside a page: the untouched bytes of the page still read as the mirror.
    VramImage v2;
    v2.WriteStream(kBase, ee.data(), ee.size());
    v2.SettleWrites();
    v2.StageDeposit(kBase + 100, gpu.data(), 50, v2.UploadEpoch());
    v2.ReadView(kBase, kPage, out.data());
    CHECK(std::memcmp(out.data(), ee.data(), 100) == 0, "bytes before the deposit == mirror");
    CHECK(std::memcmp(out.data() + 100, gpu.data(), 50) == 0, "deposited bytes visible");
    CHECK(std::memcmp(out.data() + 150, ee.data() + 150, kPage - 150) == 0, "bytes after == mirror");
    v2.CommitDeposits();
    std::vector<uint8_t> after(kPage);
    v2.ReadView(kBase, kPage, after.data());
    CHECK(after == out, "commit produces exactly what the view showed");
    // 4 MiB wrap
    uint8_t w[4] = {1, 2, 3, 4};
    v2.WriteStream(VramImage::kBytes - 2, w, 4);
    uint8_t rd[4];
    v2.ReadView(VramImage::kBytes - 2, 4, rd);
    CHECK(std::memcmp(rd, w, 4) == 0 && v2.Bytes()[0] == 3 && v2.Bytes()[1] == 4, "write/read wrap at 4 MiB");
}

// Invariant 5: a deposit older than an EE write to the same page is dropped.
static void Inv05() {
    VramImage vram;
    auto gpu = Fill(kPage, 7);
    vram.StageDeposit(kBase, gpu.data(), gpu.size(), vram.UploadEpoch());  // epoch 0
    auto ee = Fill(kPage, 33);
    vram.WriteStream(kBase, ee.data(), ee.size());
    vram.SettleWrites();  // stamps the page newer and commits: the deposit must drop
    CHECK(std::memcmp(vram.Bytes() + kBase, ee.data(), kPage) == 0, "newer EE writer kept");
    CHECK(vram.Stats().deposits_dropped_newer == 1, "one deposit dropped");
    CHECK(vram.Stats().deposits_committed == 0, "none committed");
    CHECK(vram.PendingPages() == 0, "ledger empty after settle");
    // Same epoch (deposit produced by a draw that saw the write) commits.
    VramImage v2;
    v2.WriteStream(kBase, ee.data(), ee.size());
    v2.SettleWrites();
    v2.StageDeposit(kBase, gpu.data(), gpu.size(), v2.UploadEpoch());
    v2.CommitDeposits();
    CHECK(std::memcmp(v2.Bytes() + kBase, gpu.data(), kPage) == 0, "deposit at the write's epoch commits");
    CHECK(v2.Stats().deposits_committed == 1, "one committed");
}

// Invariant 6: the deposit-epoch family is separate from the upload family.
static void Inv06() {
    VramImage vram;
    auto gpu = Fill(kPage, 3);
    const uint64_t up0 = vram.UploadEpoch();
    vram.StageDeposit(kBase, gpu.data(), gpu.size(), up0);
    vram.CommitDeposits();
    CHECK(vram.UploadEpoch() == up0, "commit does not bump the upload epoch");
    CHECK(vram.DepositSeq() == 1, "commit bumps the deposit seq");
    CHECK(vram.Page(kBase / kPage).deposit_epoch == 1 && vram.Page(kBase / kPage).upload_epoch == 0,
          "page carries deposit epoch 1, upload epoch 0");
    vram.NoteStreamWrite(VramImage::Span{kBase, kPage});
    CHECK(vram.UploadEpoch() == up0 + 1 && vram.DepositSeq() == 1, "stream stamp bumps only upload");
    CHECK(vram.Page(kBase / kPage).upload_epoch == up0 + 1, "page upload epoch stamped");
    CHECK(vram.Page(kBase / kPage).deposit_epoch == 1, "deposit epoch untouched by the upload stamp");
}

// Invariant 7: a page is bracketed Encoding / Pending / Applying; a second
// deposit onto a Pending page overlays the shadow without re-copying the mirror.
static void Inv07() {
    VramImage vram;
    auto ee = Fill(kPage, 5);
    vram.WriteStream(kBase, ee.data(), ee.size());
    vram.SettleWrites();
    auto a = Fill(64, 40), b = Fill(64, 80);
    vram.StageDeposit(kBase, a.data(), a.size(), vram.UploadEpoch());
    CHECK(vram.Page(kBase / kPage).state == PageRecord::State::Pending, "Pending after stage");
    CHECK(vram.PendingPages() == 1, "one pending page");
    vram.StageDeposit(kBase + 64, b.data(), b.size(), vram.UploadEpoch());
    CHECK(vram.PendingPages() == 1, "second deposit on the same page does not re-enter");
    std::vector<uint8_t> out(kPage);
    vram.ReadView(kBase, kPage, out.data());
    CHECK(std::memcmp(out.data(), a.data(), 64) == 0 && std::memcmp(out.data() + 64, b.data(), 64) == 0 &&
              std::memcmp(out.data() + 128, ee.data() + 128, kPage - 128) == 0,
          "both deposits overlay, rest is mirror");
    vram.CommitDeposits();
    CHECK(vram.Page(kBase / kPage).state == PageRecord::State::Clean, "Clean after commit");
    CHECK(vram.PendingPages() == 0, "ledger empty");
    CHECK(std::memcmp(vram.Bytes() + kBase, out.data(), kPage) == 0, "committed == view");
}

// Invariant 8: the mirror has one writer -- GPU bytes reach it only through
// CommitDeposits at an observation boundary; without one they never land.
static void Inv08() {
    VramImage vram;
    auto gpu = Fill(kPage * 2, 9);
    vram.StageDeposit(kBase, gpu.data(), gpu.size(), 0);
    std::vector<uint8_t> zero(kPage * 2, 0);
    CHECK(std::memcmp(vram.Bytes() + kBase, zero.data(), kPage * 2) == 0, "no commit: mirror stays zero");
    std::vector<uint8_t> out(kPage * 2);
    vram.ReadView(kBase, kPage * 2, out.data());
    CHECK(out == gpu, "view still composes the shadow");
    CHECK(vram.PendingPages() == 2, "two pages pending");
    vram.CommitDeposits();
    CHECK(std::memcmp(vram.Bytes() + kBase, gpu.data(), kPage * 2) == 0, "commit is the only mirror writer");
    CHECK(vram.Page(kBase / kPage + 1).deposit_epoch == 2 && vram.Page(kBase / kPage).deposit_epoch == 1,
          "deposit seq per committed page");
}

// Invariant 16: EE writes mark precise 128 B sub-page bits; a settle clears them.
static void Inv16() {
    VramImage vram;
    uint8_t one = 1;
    vram.WriteStream(kBase + 130, &one, 1);
    CHECK(vram.Page(kBase / kPage).write_bits == (1ull << 1), "byte 130 -> sub-page bit 1");
    vram.WriteStream(kBase + 8191, &one, 1);
    CHECK(vram.Page(kBase / kPage).write_bits == ((1ull << 1) | (1ull << 63)), "byte 8191 -> bit 63");
    vram.WriteStream(kBase + 127, &one, 2);  // straddles sub-pages 0 and 1
    CHECK(vram.Page(kBase / kPage).write_bits == (1ull | (1ull << 1) | (1ull << 63)), "straddle marks both");
    CHECK(vram.Page(kBase / kPage + 1).write_bits == 0, "next page untouched");
    CHECK(vram.Page(kBase / kPage).author == PageRecord::Author::Cpu, "CPU author");
    vram.SettleWrites();
    CHECK(vram.Page(kBase / kPage).write_bits == 0, "settle clears the bits");
    CHECK(vram.Page(kBase / kPage).upload_epoch == 1, "settle stamps the page");
    uint32_t first, count;
    VramImage::PagesOf(VramImage::Span{kBase + 8190, 4}, &first, &count);
    CHECK(first == kBase / kPage && count == 2, "PagesOf spans two pages");
    VramImage::PagesOf(VramImage::Span{kBase, 0}, &first, &count);
    CHECK(count == 0, "empty span has no pages");
}

// Invariant 22: with the pending ledger empty no page may be stale at save.
static void Inv22() {
    VramImage vram;
    vram.NoteGpuElided(VramImage::Span{kBase, kPage * 2}, kBase, 10, 20);
    CHECK(vram.Page(kBase / kPage).stale && vram.Page(kBase / kPage).dirty_y0 == 10 &&
              vram.Page(kBase / kPage).dirty_y1 == 20,
          "elided deposit leaves a stale band");
    vram.NoteGpuElided(VramImage::Span{kBase, kPage}, kBase, 5, 12);
    CHECK(vram.Page(kBase / kPage).dirty_y0 == 5 && vram.Page(kBase / kPage).dirty_y1 == 20, "bands union");
    vram.MaterializePages(VramImage::Span{kBase, kPage}, VramImage::Reason::Refresh);
    CHECK(!vram.Page(kBase / kPage).stale, "materialize clears the band");
    CHECK(vram.Page(kBase / kPage + 1).stale, "other page still stale");
    const uint32_t found = vram.AssertNoStaleAtSave();
    CHECK(found == 1, "one stale page reported at save");
    CHECK(!vram.Page(kBase / kPage + 1).stale && vram.Stats().save_reconciled == 1, "reconciled");
    CHECK(vram.AssertNoStaleAtSave() == 0, "zero stale after reconcile");
    // Reader set
    vram.ReaderPagesSeed(VramImage::Span{kBase, kPage});
    CHECK(vram.NeedsEagerDeposit(VramImage::Span{kBase + 100, 4}), "seeded page needs eager deposit");
    CHECK(!vram.NeedsEagerDeposit(VramImage::Span{kBase + kPage, 4}), "unseeded page does not");
}

// Invariant 36: band math -- a refresh clears only pages the band fully covers.
static void Inv36() {
    int y0 = 0, y1 = 0;
    CHECK(PageRecord::ClampToRowBand(10, 20, 256, &y0, &y1) && y0 == 10 && y1 == 20, "in-range band passes");
    CHECK(PageRecord::ClampToRowBand(-5, 300, 256, &y0, &y1) && y0 == 0 && y1 == 256, "clamped to target");
    CHECK(!PageRecord::ClampToRowBand(30, 30, 256, &y0, &y1), "empty band rejected");
    CHECK(!PageRecord::ClampToRowBand(300, 400, 256, &y0, &y1), "out-of-target rejected");

    uint32_t f = 0, l = 0;
    // CT16 pages 64x64, FBW 256 -> 4 pages per page row.
    CHECK(PageRecord::FullyCoveredPages(0x02, 0, 256, 256, 64, 192, &f, &l) && f == 4 && l == 11,
          "band [64,192) covers page rows 1..2 (pages 4..11)");
    CHECK(!PageRecord::FullyCoveredPages(0x02, 0, 256, 256, 16, 48, &f, &l), "sub-page-row band covers none");
    CHECK(!PageRecord::FullyCoveredPages(0x02, 0, 256, 128, 0, 256, &f, &l), "decode width < FBW never full");
    CHECK(PageRecord::FullyCoveredPages(0x00, 0, 256, 256, 32, 96, &f, &l) && f == 4 && l == 11,
          "CT32 band [32,96) covers pages 4..11");
    CHECK(PageRecord::FullyCoveredPages(0x00, kBase, 256, 256, 32, 96, &f, &l) && f == 36 && l == 43,
          "base offset adds fbp/8192");
    CHECK(PageRecord::FullyCoveredPages(0x02, 505 * 8192, 256, 256, 64, 192, &f, &l) && f == 509 && l == 511,
          "last page clamps to 511");
    CHECK(!PageRecord::FullyCoveredPages(0x02, 511 * 8192, 256, 256, 64, 192, &f, &l), "first page past 511: none");

    // Page-tile rect: synthetic CT16 512-wide target at 0x80000.
    const uint32_t T = 0x80000;
    int x0, ty0, x1, ty1;
    CHECK(PageRecord::FoldByteRangeToPageRect(T + 10 * 0x2000, 0x2000, T, 512, 0x02, 512, 448, &x0, &ty0, &x1, &ty1) &&
              x0 == 128 && ty0 == 64 && x1 == 192 && ty1 == 128,
          "8KB at page 10 of CT16/512 -> tile (128,64)-(192,128)");
    CHECK(PageRecord::FoldByteRangeToPageRect(T, 0x2000, T, 512, 0x02, 512, 448, &x0, &ty0, &x1, &ty1) &&
              x0 == 0 && ty0 == 0 && x1 == 64 && ty1 == 64,
          "page 0 -> first tile");
    CHECK(PageRecord::FoldByteRangeToPageRect(T + 0x2000, 0x4000, T, 512, 0x02, 512, 448, &x0, &ty0, &x1, &ty1) &&
              x0 == 64 && ty0 == 0 && x1 == 192 && ty1 == 64,
          "pages 1-2 same row -> column union");
    CHECK(PageRecord::FoldByteRangeToPageRect(T + 7 * 0x2000, 0x4000, T, 512, 0x02, 512, 448, &x0, &ty0, &x1, &ty1) &&
              x0 == 0 && ty0 == 0 && x1 == 512 && ty1 == 128,
          "row-crossing span -> full-width union");
    CHECK(PageRecord::FoldByteRangeToPageRect(T + 10 * 0x2000, 0x2000, T, 512, 0x00, 512, 448, &x0, &ty0, &x1, &ty1) &&
              x0 == 128 && ty0 == 32 && x1 == 192 && ty1 == 64,
          "CT32 page 10 -> 64x32 tile");
    // A range outside the target [T, T + 512*448*2) -- synthetic, three pages
    // into the 3 MiB half -- folds to nothing.
    CHECK(!PageRecord::FoldByteRangeToPageRect(0x00300000 + 3 * 0x2000, 0x2000, T, 512, 0x02, 512, 448, &x0, &ty0, &x1, &ty1),
          "disjoint range folds to nothing");

    // Byte range -> row band: fbp=0x1000 fbw=256 bpp=2 -> row 512 B, h 256.
    {
        int b0 = 0, b1 = 0;
        CHECK(PageRecord::FoldByteRangeToBand(0x1000 + 70 * 512, 10 * 512, 0x1000, 256, 2, 256, false, &b0, &b1) &&
                  b0 == 70 && b1 == 80,
              "row-aligned write -> its rows");
        CHECK(PageRecord::FoldByteRangeToBand(0x1000 + 100 * 512, 20 * 512, 0x1000, 256, 2, 256, false, &b0, &b1) &&
                  b0 == 70 && b1 == 120,
              "second write unions");
        CHECK(!PageRecord::FoldByteRangeToBand(0x1000 + 75 * 512, 512, 0x1000, 256, 2, 256, false, &b0, &b1) &&
                  b0 == 70 && b1 == 120,
              "covered write leaves the band");
        int z0 = 0, z1 = 0;
        CHECK(!PageRecord::FoldByteRangeToBand(0x90000, 512, 0x1000, 256, 2, 256, false, &z0, &z1), "disjoint folds nothing");
        CHECK(PageRecord::FoldByteRangeToBand(0, 0x90000, 0x1000, 256, 2, 256, false, &z0, &z1) && z0 == 0 && z1 == 256,
              "over-covering write -> full band");
        int u0 = 0, u1 = 0;
        CHECK(PageRecord::FoldByteRangeToBand(0x1000 + 10 * 512, 512, 0x1000, 256, 2, 256, true, &u0, &u1) && u0 == 0 && u1 == 256,
              "full_band -> full band");
        int p0 = 0, p1 = 0;
        CHECK(PageRecord::FoldByteRangeToBand(0x1000 + 70 * 512 + 100, 100, 0x1000, 256, 2, 256, false, &p0, &p1) &&
                  p0 == 70 && p1 == 71,
              "sub-row write covers its single row");
    }
    CHECK(PageRecord::PageHeightForPsm(0x02) == 64 && PageRecord::PageHeightForPsm(0x0A) == 64 &&
              PageRecord::PageHeightForPsm(0x00) == 32,
          "page heights");
}

// Invariant 39: a reader never sees an older GPU deposit over a newer EE transfer.
// The title screen's case: the Z->colour copy into FRAME 0x2f0000 (1024 scissor
// rows, CT16) deposits over the pages the game then streams textures through
// (0x300000..0x3effff, re-uploaded every frame); the texture decode that follows the
// upload must read the transferred texels, exactly what CommitDeposits keeps.
static void Inv39() {
    VramImage vram;
    auto old_tex = Fill(kPage, 11);
    vram.WriteStream(kBase, old_tex.data(), old_tex.size());
    vram.SettleWrites();
    auto gpu = Fill(kPage, 90);
    vram.StageDeposit(kBase, gpu.data(), gpu.size(), vram.UploadEpoch());   // the target's writeback, pending
    std::vector<uint8_t> out(kPage);
    vram.ReadView(kBase, kPage, out.data());
    CHECK(out == gpu, "before the transfer the pending deposit is the page's content");
    // The next frame's host->local transfer into the same page: stamped at arm, bytes streamed.
    auto new_tex = Fill(kPage, 33);
    vram.NoteStreamWrite(VramImage::Span{kBase, kPage});
    vram.WriteStream(kBase, new_tex.data(), new_tex.size());
    vram.ReadView(kBase, kPage, out.data());
    CHECK(out == new_tex, "a reader after the newer transfer sees the transferred bytes, not the deposit");
    vram.SettleWrites();   // commits: the deposit drops (invariant 5) -- the view already said so
    std::vector<uint8_t> after(kPage);
    vram.ReadView(kBase, kPage, after.data());
    CHECK(after == out && after == new_tex, "commit produces exactly what the view showed");
    CHECK(vram.Stats().deposits_dropped_newer == 1, "the older deposit dropped");
    // A deposit produced by a draw that saw the transfer (same epoch) is shown and commits.
    auto gpu2 = Fill(kPage, 120);
    vram.StageDeposit(kBase, gpu2.data(), gpu2.size(), vram.UploadEpoch());
    vram.ReadView(kBase, kPage, out.data());
    CHECK(out == gpu2, "a deposit at the transfer's epoch is the page's content");
    vram.CommitDeposits();
    CHECK(std::memcmp(vram.Bytes() + kBase, gpu2.data(), kPage) == 0, "... and commits");
}

// View signature: the fold of the span's page view generations moves on exactly
// the mutations that can change what ReadView returns -- an EE write into the span
// (and the settle that stamps it), a staged GPU deposit, a commit, a stream stamp
// -- and on nothing else (a write or deposit elsewhere, a settle with no write under
// the span, a reader touch). WriteStream's per-run marks equal the per-byte walk's
// (write bits, author, watermark) for a run that straddles sub-pages and pages.
static void ViewSig() {
    VramImage vram;
    const VramImage::Span span{kBase, kPage * 2};
    const uint64_t s0 = vram.ViewSignature(span);
    CHECK(s0 == vram.ViewSignature(span), "signature is a pure function of the pages");
    CHECK(s0 != vram.ViewSignature(VramImage::Span{kBase + kPage, kPage * 2}), "a different span folds differently");
    uint8_t one = 1;
    vram.WriteStream(kBase + kPage * 4, &one, 1);
    CHECK(vram.ViewSignature(span) == s0, "a write elsewhere does not move it");
    vram.SettleWrites();
    CHECK(vram.ViewSignature(span) == s0, "a settle with no write under the span does not move it");
    vram.ReaderTouch(span);
    vram.MaterializePages(span, VramImage::Reason::Refresh);
    CHECK(vram.ViewSignature(span) == s0, "reader bookkeeping does not move it");
    vram.WriteStream(kBase + 5, &one, 1);
    const uint64_t s1 = vram.ViewSignature(span);
    CHECK(s1 != s0, "a write into the span moves it");
    vram.SettleWrites();
    const uint64_t s2 = vram.ViewSignature(span);
    CHECK(s2 != s1, "the settle that stamps the page moves it");
    std::vector<uint8_t> gpu(kPage, 0xAB);
    vram.StageDeposit(kBase, gpu.data(), kPage, vram.UploadEpoch());
    const uint64_t s3 = vram.ViewSignature(span);
    CHECK(s3 != s2, "a staged deposit moves it");
    vram.CommitDeposits();
    const uint64_t s4 = vram.ViewSignature(span);
    CHECK(s4 != s3, "a commit moves it");
    vram.NoteStreamWrite(VramImage::Span{kBase + kPage, 16});
    CHECK(vram.ViewSignature(span) != s4, "a stream stamp moves it");
    // Equal marks for a straddling run: bytes 8190..8193 of page 0 land in sub-pages
    // 63 (page 0) and 0 (page 1); the watermark is the min/max address touched.
    VramImage a, b;
    const uint8_t run[4] = {1, 2, 3, 4};
    a.WriteStream(kBase + 8190, run, 4);
    for (uint32_t i = 0; i < 4; ++i) b.WriteStream(kBase + 8190 + i, run + i, 1);
    CHECK(a.Page(kBase / kPage).write_bits == b.Page(kBase / kPage).write_bits &&
              a.Page(kBase / kPage).write_bits == (1ull << 63),
          "run marks sub-page 63 of the first page");
    CHECK(a.Page(kBase / kPage + 1).write_bits == b.Page(kBase / kPage + 1).write_bits &&
              a.Page(kBase / kPage + 1).write_bits == 1ull,
          "run marks sub-page 0 of the second page");
    CHECK(std::memcmp(a.Bytes() + kBase + 8190, run, 4) == 0 && std::memcmp(a.Bytes(), b.Bytes(), VramImage::kBytes) == 0,
          "the bytes land identically");
    a.SettleWrites();
    b.SettleWrites();
    CHECK(a.Page(kBase / kPage).upload_epoch == 1 && a.Page(kBase / kPage + 1).upload_epoch == 1 &&
              a.Page(kBase / kPage + 2).upload_epoch == 0,
          "the settle stamps exactly the two pages the run touched");
    // A full-page run sets all 64 bits; a wrap at 4 MiB lands on the last and first page.
    std::vector<uint8_t> page(kPage, 7);
    VramImage c;
    c.WriteStream(kBase, page.data(), kPage);
    CHECK(c.Page(kBase / kPage).write_bits == ~0ull, "a full page sets every sub-page bit");
    c.WriteStream(VramImage::kBytes - 2, run, 4);
    CHECK(c.Bytes()[VramImage::kBytes - 1] == 2 && c.Bytes()[0] == 3 && c.Bytes()[1] == 4, "the run wraps at 4 MiB");
    CHECK((c.Page(VramImage::kPageCount - 1).write_bits >> 63) == 1 && (c.Page(0).write_bits & 1) == 1,
          "the wrap marks the last and the first page");
}

// The pipelined queue's split image (VramOpLog): an image mirrored into an op log
// and a second image that replays the log at the points the in-thread consumer
// would have run, interleaved with the consumer's deposits, end with the same
// bytes, marks and epochs as ONE shared image fed the same calls in the same
// order -- including the interleaving-dependent commit/drop decisions.
static void OpLogReplay() {
    // Three images: `shared` takes every call directly (the inline consumer);
    // `ee` takes the EE calls and mirrors them; `gpu` takes the replayed EE calls
    // plus the GPU calls, in stream order.
    VramImage shared, ee, gpu;
    VramOpLog log;
    ee.SetMirror(&log);
    const auto same_pages = [&](const char* what) {
        bool ok = std::memcmp(shared.Bytes(), gpu.Bytes(), VramImage::kBytes) == 0;
        for (uint32_t pg = 0; pg < VramImage::kPageCount && ok; ++pg) {
            const PageRecord& a = shared.Page(pg);
            const PageRecord& b = gpu.Page(pg);
            ok = a.author == b.author && a.author_fbp == b.author_fbp && a.upload_epoch == b.upload_epoch &&
                 a.deposit_epoch == b.deposit_epoch && a.shadow_epoch == b.shadow_epoch && a.state == b.state &&
                 a.write_bits == b.write_bits && a.dirty_y0 == b.dirty_y0 && a.dirty_y1 == b.dirty_y1 &&
                 a.stale == b.stale;
        }
        ok = ok && shared.UploadEpoch() == gpu.UploadEpoch() && shared.DepositSeq() == gpu.DepositSeq() &&
             shared.PendingPages() == gpu.PendingPages() && ee.UploadEpoch() == gpu.UploadEpoch();
        std::vector<uint8_t> va(VramImage::kBytes), vb(VramImage::kBytes);
        shared.ReadView(0, VramImage::kBytes, va.data());
        gpu.ReadView(0, VramImage::kBytes, vb.data());
        ok = ok && va == vb;
        CHECK(ok, what);
    };
    const auto replay = [&] {
        log.Replay(gpu);
        log.Clear();
    };
    // 1. An upload (per-pixel writes, coalesced in the log), a stream stamp, a settle.
    const std::vector<uint8_t> tex = Fill(kPage + 300, 3);
    shared.NoteStreamWrite(VramImage::Span{kBase, kPage + 300});
    ee.NoteStreamWrite(VramImage::Span{kBase, kPage + 300});
    for (size_t i = 0; i < tex.size(); i += 4) {
        const size_t n = std::min<size_t>(4, tex.size() - i);
        shared.WriteStream(kBase + static_cast<uint32_t>(i), tex.data() + i, n);
        ee.WriteStream(kBase + static_cast<uint32_t>(i), tex.data() + i, n);
    }
    CHECK(log.Ops() == 2, "the per-pixel run coalesces into one WriteStream op after the stamp");
    shared.SettleWrites();
    ee.SettleWrites();
    replay();
    same_pages("upload + settle replay identically");
    // 2. A GPU deposit under the uploaded page (batch epoch = the current upload
    //    epoch), then the EE writes the SAME page before the next settle: the
    //    settle drops the deposit on both (newer writer), the bytes are the EE's.
    const std::vector<uint8_t> dep(kPage, 0xAB);
    shared.StageDeposit(kBase, dep.data(), kPage, shared.UploadEpoch());
    gpu.StageDeposit(kBase, dep.data(), kPage, gpu.UploadEpoch());
    const uint8_t one[4] = {9, 8, 7, 6};
    shared.NoteStreamWrite(VramImage::Span{kBase + 16, 4});
    ee.NoteStreamWrite(VramImage::Span{kBase + 16, 4});
    shared.WriteStream(kBase + 16, one, 4);
    ee.WriteStream(kBase + 16, one, 4);
    shared.SettleWrites();
    ee.SettleWrites();
    replay();
    same_pages("a deposit under a newer EE write is dropped on both");
    CHECK(gpu.Stats().deposits_dropped_newer == 1 && shared.Stats().deposits_dropped_newer == 1, "dropped once each");
    // 3. A deposit with no EE write under it commits at the next settle on both;
    //    a GPU author note and a materialize land on the GPU side only (and the
    //    shared image), the EE side never sees them.
    shared.StageDeposit(kBase + kPage, dep.data(), kPage, shared.UploadEpoch());
    gpu.StageDeposit(kBase + kPage, dep.data(), kPage, gpu.UploadEpoch());
    shared.NoteGpuAuthor(VramImage::Span{kBase + kPage * 2, kPage}, kBase);
    gpu.NoteGpuAuthor(VramImage::Span{kBase + kPage * 2, kPage}, kBase);
    shared.SettleWrites();
    ee.SettleWrites();
    replay();
    same_pages("a deposit with no newer writer commits on both");
    CHECK(gpu.Page(kBase / kPage + 1).deposit_epoch == 1 && gpu.Stats().deposits_committed == 1, "committed once");
    CHECK(ee.Page(kBase / kPage + 2).author == PageRecord::Author::Cold, "the EE image never sees the GPU author");
    // 4. A NoteWriteNoBytes mark, a NoteCpuWrite, a wrap at 4 MiB, an empty settle.
    shared.NoteWriteNoBytes(kBase + kPage * 3, 8);
    ee.NoteWriteNoBytes(kBase + kPage * 3, 8);
    shared.NoteCpuWrite(VramImage::Span{kBase + kPage * 2, kPage});
    ee.NoteCpuWrite(VramImage::Span{kBase + kPage * 2, kPage});
    shared.WriteStream(VramImage::kBytes - 2, one, 4);
    ee.WriteStream(VramImage::kBytes - 2, one, 4);
    shared.WriteStream(2, one, 2);   // continues the wrapped run: coalesced
    ee.WriteStream(2, one, 2);
    CHECK(log.Ops() == 3, "the wrapped run and its continuation coalesce");
    shared.SettleWrites();
    ee.SettleWrites();
    shared.SettleWrites();
    ee.SettleWrites();
    replay();
    same_pages("marks, wrap and empty settle replay identically");
    // 5. An empty log replays nothing; the log moves.
    CHECK(log.Empty() && log.Replay(gpu) == 0, "an empty log replays nothing");
    ee.SetMirror(nullptr);
    ee.WriteStream(kBase, one, 1);
    CHECK(log.Empty(), "an unmirrored image logs nothing");
}

// NoteWriteNoBytes over a footprint (an owned texture upload: a whole slot,
// up to 256 KiB, no payload) leaves every page exactly as a byte-by-byte walk
// of 1-byte notes does: write bits, author, settle set (the settle stamps the
// same pages), the watermark -- and no view generation bump (unlike a payload
// WriteStream). Covers a run inside one page, across pages, unaligned ends
// and the wrap at 4 MiB.
static void NoBytesRuns() {
    const uint32_t kPage = VramImage::kPageBytes;
    const struct { uint32_t off; size_t len; } runs[] = {
        {kPage * 7 + 40, 8}, {kPage * 7 + 100, kPage * 3 + 17}, {kPage * 12 + 5, 256 * 1024},
        {VramImage::kBytes - 300, 700},
    };
    for (const auto& r : runs) {
        VramImage run, walk;
        run.NoteWriteNoBytes(r.off, r.len);
        for (size_t i = 0; i < r.len; ++i) walk.NoteWriteNoBytes((r.off + static_cast<uint32_t>(i)) % VramImage::kBytes, 1);
        bool same = true, untouched_view = true;
        for (uint32_t pg = 0; pg < VramImage::kPageCount; ++pg) {
            const PageRecord& a = run.Page(pg);
            const PageRecord& b = walk.Page(pg);
            if (a.write_bits != b.write_bits || a.author != b.author) same = false;
            if (a.view_gen != 0) untouched_view = false;
        }
        CHECK(same, "a footprint note marks the pages like the per-byte walk");
        CHECK(untouched_view, "a note without bytes bumps no view generation");
        run.SettleWrites();
        walk.SettleWrites();
        for (uint32_t pg = 0; pg < VramImage::kPageCount; ++pg) {
            const PageRecord& a = run.Page(pg);
            const PageRecord& b = walk.Page(pg);
            if (a.upload_epoch != b.upload_epoch || a.author != b.author || a.write_bits != b.write_bits ||
                a.view_gen != b.view_gen) same = false;
        }
        CHECK(same, "the settle stamps the same pages after either note");
        CHECK(run.Stats().settles == 1 && walk.Stats().settles == 1, "one settle each");
    }
}

int main(int argc, char** argv) {
    const std::string c = argc > 1 ? argv[1] : "all";
    if (c == "inv04" || c == "all") Inv04();
    if (c == "nobytes" || c == "all") NoBytesRuns();
    if (c == "inv05" || c == "all") Inv05();
    if (c == "inv06" || c == "all") Inv06();
    if (c == "inv07" || c == "all") Inv07();
    if (c == "inv08" || c == "all") Inv08();
    if (c == "inv16" || c == "all") Inv16();
    if (c == "inv22" || c == "all") Inv22();
    if (c == "inv36" || c == "all") Inv36();
    if (c == "inv39" || c == "all") Inv39();
    if (c == "viewsig" || c == "all") ViewSig();
    if (c == "oplog" || c == "all") OpLogReplay();
    std::printf("test_vram_image %s: %d checks, %d failures\n", c.c_str(), g_checks, g_fail);
    return g_fail ? 1 : 0;
}
