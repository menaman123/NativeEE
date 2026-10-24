// gs/hle/texsrc/texture_dump_sink.h -- TextureDumpSink: the texture-oracle observer of
// the decode fallback.
//
// The resolver decodes every DISTINCT texture content exactly once (the content key is
// the hash of the bytes); this sink sits on that miss path and, while armed, receives
// each fresh decode: the register tuple, the composed VRAM view the decoder addressed,
// the decoded RGBA8 level 0 the FAST tier (BlockDecode::Decode) produced and the draw
// / frame that first used it. Per distinct texture (keyed by the hash of the tuple, the
// level-0 window bytes and the CLUT page) it
//   1. runs ORACLE B -- the per-pixel reference decode (BlockDecode::DecodeGeneric over
//      PsmLayout::PixelAddress, the manual's six-step address) of the same view and
//      compares it with the fast tier's texels byte for byte; a difference is an engine
//      bug of the fast tier (count + first differing texel are recorded);
//   2. writes it ONCE to <dir>/<hash>.ppm (P6, RGB), <hash>.alpha.pgm (P5, the PS2 alpha
//      byte), <hash>.json (the tuple, sizes, the sub-rect, the window / CLUT hashes, the
//      content hash, the first draw / frame, the palette of an indexed read, the
//      oracle_b verdict) and, bounded by kMaxRawBytes, <hash>.win (the raw level-0
//      footprint bytes) + <hash>.clut (the raw 8 KiB CLUT page) so an offline decoder
//      can reproduce the read.
// Reads served without a decode (a render target, a depth buffer, an owned slot) are
// not textures this sink can image; NoteServe records their tuple + tier once per tuple
// into <dir>/serves.tsv so a judge can tell "we served a target where the oracle read
// memory" from a decode fault. Disarmed, every entry point is one load of an atomic.
// Title-neutral: the composition arms it for a frame window and names the directory.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "engine/gs/hle/ir/tex_read_view.h"

namespace ps2r {
namespace gs {

class TextureDumpSink {
public:
    static constexpr uint32_t kMaxRawBytes = 1024u * 1024u;   // .win files above this are skipped

    struct Decode {
        const TexReadView* tex = nullptr;
        const uint8_t* view = nullptr;       // the composed 4 MiB view the decoder addressed
        const uint32_t* texels = nullptr;    // the fast tier's level 0, dw * dh RGBA8 (PS2 alpha)
        int dw = 0, dh = 0;
        bool sub = false;                    // texels are the sub-rect [x0, x0+dw) x [y0, y0+dh)
        int x0 = 0, y0 = 0;
        uint32_t fp_start = 0, fp_size = 0;  // the level-0 footprint (bytes, wrapped)
        uint32_t frame_count = 0;
        uint32_t draw_seq = 0;
    };
    struct OracleB {
        bool equal = true;
        uint32_t diffs = 0;
        int first_x = -1, first_y = -1;
    };
    struct Stats {
        uint64_t decodes = 0;        // decodes seen while armed
        uint64_t distinct = 0;       // distinct textures (first sight -> written)
        uint64_t written = 0;        // files written
        uint64_t failed = 0;         // file writes that failed
        uint64_t oracle_b_bad = 0;   // distinct textures whose fast tier != reference
        uint64_t serves = 0;         // distinct served (non-decoded) tuples recorded
    };

    // Arm: writes go to `dir` (created when missing). Disarm flushes serves.tsv.
    bool Arm(const std::string& dir);
    void Disarm();
    bool Armed() const { return armed_.load(std::memory_order_acquire); }
    const Stats& GetStats() const { return stats_; }

    // The miss path's report (resolver, consumer thread).
    void OnDecode(const Decode& d);
    // A read served without a decode: `tier` names the serve (target / depth / self / owned).
    void NoteServe(const TexReadView& v, const char* tier, uint32_t frame_count, uint32_t draw_seq);

    // Oracle B over a view (public for the tests): the reference decode of `d` compared
    // with d.texels.
    static OracleB CompareWithReference(const Decode& d, std::vector<uint32_t>* reference);
    // The hash this sink keys a texture by: the tuple fields the decode depends on, the
    // footprint bytes and (indexed) the CLUT page.
    static uint64_t ContentHash(const Decode& d);
    static uint64_t HashBytes(uint64_t h, const uint8_t* p, size_t n);

private:
    bool WriteTexture(const Decode& d, uint64_t hash, const OracleB& ob, const std::vector<uint32_t>& reference);
    bool WriteJson(const Decode& d, uint64_t hash, const OracleB& ob, uint64_t win_hash, uint64_t clut_hash);
    std::string Path(uint64_t hash, const char* ext) const;

    std::atomic<bool> armed_{false};
    std::string dir_;
    std::unordered_set<uint64_t> seen_;
    std::unordered_set<uint64_t> served_;
    std::vector<std::string> serve_lines_;
    std::vector<uint32_t> reference_;
    Stats stats_;
};

}  // namespace gs
}  // namespace ps2r
