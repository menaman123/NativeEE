// gs/hle/texsrc/texres_trace.cpp -- TexresTrace implementation.
#include "engine/gs/hle/texsrc/texres_trace.h"

namespace ps2r {
namespace gs {

bool TexresTrace::Wants(const TexReadView& v) const {
    if (!filter_.enabled) return false;
    // Every filter field that is set must match.
    if (filter_.tbp != kAny && filter_.tbp != v.tbp) return false;
    if (filter_.cbp != kAny && filter_.cbp != v.cbp) return false;
    return true;
}

void TexresTrace::NoteOverlapping(const PendingRtSyncLedger& ledger, uint32_t start, uint32_t size,
                                  std::vector<Flushed>* out) {
    for (const PendingRtSyncLedger::Entry& e : ledger.Entries()) {
        if (!PendingRtSyncLedger::Overlaps(e, start, size)) continue;
        Flushed f;
        f.fbp = e.fbp;
        f.start = e.start;
        f.size = e.size;
        f.epoch = e.epoch;
        out->push_back(f);
    }
}

void TexresTrace::FillPages(const VramImage& vram, uint32_t start, uint32_t size, std::vector<Page>* out) {
    uint32_t first, count;
    VramImage::PagesOf(VramImage::Span{start, size}, &first, &count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t pg = (first + i) % VramImage::kPageCount;
        const PageRecord& r = vram.Page(pg);
        Page p;
        p.addr = pg * VramImage::kPageBytes;
        p.state = static_cast<uint8_t>(r.state);
        p.author = static_cast<uint8_t>(r.author);
        p.author_fbp = r.author_fbp;
        p.upload_epoch = r.upload_epoch;
        p.deposit_epoch = r.deposit_epoch;
        p.shadow_epoch = r.shadow_epoch;
        p.write_bits = r.write_bits;
        // The reader rule of VramImage::ReadView: the shadow stands in where a
        // commit would apply it.
        p.overlay = vram.HasShadow() && r.state != PageRecord::State::Clean && r.upload_epoch <= r.shadow_epoch;
        out->push_back(p);
    }
}

uint64_t TexresTrace::Hash(const uint8_t* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

void TexresTrace::OnFrameEnd() {
    if (cur_.empty()) return;   // a boundary without recorded reads keeps the last frame's
    last_.swap(cur_);
    cur_.clear();
}

const char* TexresTrace::OutcomeName(Outcome o) {
    switch (o) {
        case Outcome::None: return "none";
        case Outcome::L1Hit: return "l1";
        case Outcome::L2Hit: return "l2";
        case Outcome::Decoded: return "decode";
        case Outcome::TargetServe: return "target";
        case Outcome::DepthServe: return "depth";
        case Outcome::SelfRead: return "selfread";
        case Outcome::NoSnapshot: return "nosnapshot";
        case Outcome::OwnedServe: return "owned";
    }
    return "?";
}

void TexresTrace::Write(std::FILE* f) const {
    if (!f) return;
    static const char* const kState[] = {"clean", "pending", "encoding", "applying"};
    static const char* const kAuthor[] = {"cold", "cpu", "gpu"};
    for (const Line& l : last_) {
        const TexReadView& v = l.tex;
        std::fprintf(f,
                     "seq=%u fc=%u depoch=%llu TEX0(tbp=%06x tbw=%u psm=%02x tw=%u th=%u cbp=%06x cpsm=%02x csm=%u csa=%u)"
                     " TEX1(mxl=%u lcm=%u l=%u k=%d) mip1=%06x mip2=%06x outcome=%s key=%016llx levels=%u sub=%u"
                     " rgn=%d,%d,%d,%d fp=%06x+%x winhash=%016llx cluthash=%016llx flushed=%zu\n",
                     l.draw_seq, l.frame_count, (unsigned long long)l.draw_epoch, v.tbp, v.tbw, v.psm, v.tw, v.th,
                     v.cbp, v.cpsm, v.csm, v.csa, v.mxl, v.lcm ? 1 : 0, v.l, int(v.k), v.mip_tbp[0], v.mip_tbp[1],
                     OutcomeName(l.outcome), (unsigned long long)l.key, l.levels, l.sub ? 1 : 0, l.rgn_x0, l.rgn_y0,
                     l.rgn_w, l.rgn_h, l.fp_start, l.fp_size, (unsigned long long)l.window_hash,
                     (unsigned long long)l.clut_hash, l.flushed.size());
        for (const Flushed& x : l.flushed)
            std::fprintf(f, "  flushed fbp=%06x span=%06x+%x epoch=%llu\n", x.fbp, x.start, x.size,
                         (unsigned long long)x.epoch);
        for (const Page& p : l.pages) {
            const char* st = p.state < 4 ? kState[p.state] : "?";
            const char* au = p.author < 3 ? kAuthor[p.author] : "?";
            std::fprintf(f, "  page=%06x state=%s author=%s(%06x) up=%llu dep=%llu shadow=%llu wbits=%016llx overlay=%u\n",
                         p.addr, st, au, p.author_fbp, (unsigned long long)p.upload_epoch,
                         (unsigned long long)p.deposit_epoch, (unsigned long long)p.shadow_epoch,
                         (unsigned long long)p.write_bits, p.overlay ? 1 : 0);
        }
    }
}

uint32_t TexresTrace::WriteBytes(const std::string& dir, const std::string& prefix) const {
    uint32_t n = 0;
    for (const Line& l : last_) {
        if (l.window.empty() && l.clut.empty() && l.texels.empty()) continue;
        const std::string base = dir + "/" + prefix + std::to_string(l.draw_seq);
        if (!l.window.empty()) {
            if (std::FILE* f = std::fopen((base + "_win.bin").c_str(), "wb")) {
                std::fwrite(l.window.data(), 1, l.window.size(), f);
                std::fclose(f);
                ++n;
            }
        }
        if (!l.clut.empty()) {
            if (std::FILE* f = std::fopen((base + "_clut.bin").c_str(), "wb")) {
                std::fwrite(l.clut.data(), 1, l.clut.size(), f);
                std::fclose(f);
                ++n;
            }
        }
        if (l.tex_w > 0 && l.tex_h > 0 && l.texels.size() >= size_t(l.tex_w) * l.tex_h * 4) {
            if (std::FILE* f = std::fopen((base + "_tex.ppm").c_str(), "wb")) {
                std::fprintf(f, "P6\n%d %d\n255\n", l.tex_w, l.tex_h);
                for (int i = 0; i < l.tex_w * l.tex_h; ++i) std::fwrite(l.texels.data() + size_t(i) * 4, 1, 3, f);
                std::fclose(f);
                ++n;
            }
        }
    }
    return n;
}

}  // namespace gs
}  // namespace ps2r
