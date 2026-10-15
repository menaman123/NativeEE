// gs/hle/texsrc/texture_resolver.h -- TextureResolver: the ONE ResolveRead that
// turns a draw's TexReadView plus the live target set into a TextureSource.
//
// Decision order (each tier a method):
//   MatchTarget     -- a target exists at exactly TBP. (a) a 16-bit read of a
//                      buffer that has held 32-bit content serves the alt16
//                      companion; (b) the title's scene accumulator serves its
//                      colour for any non-indexed, non-16-bit read; a target
//                      without valid content declines; (c) an indexed (T8H) read
//                      serves the target's own texels as indices (fmt 4 over a
//                      32-bit-class target when fresh or a self-read; fmt 5 over
//                      a 16-bit pair whose last draws recorded the index-lane keep
//                      mask, or -- the lane-live rescue on ANY other fall -- whose
//                      lane draws of THIS frame are still the last authors of
//                      their views, binding each half's lane surface); an indexed
//                      decline of a recently drawn 32-bit-class target deposits
//                      its rows (CT32 / CT24 encode) and arms the native-centre
//                      requantisation; (d) a colour read serves unless the target
//                      was not drawn recently AND its pages are CPU-dirty AND a
//                      newer EE upload overlaps the read footprint (then its rows
//                      are rescued into VRAM in the READ's format first and the
//                      read declines); a 16 <-> 32 class mismatch syncs the drawn
//                      rows and declines, except a CT32 read of a CT16S target,
//                      which serves. WHENEVER a target exists at TBP the decline
//                      goes straight to the decode fallback (never the depth tier).
//                      A colour-class serve of the draw's own FRAME.FBP binds
//                      nothing (textured UV mode, no sampler).
//   MatchDepth      -- a Z-format read: the target whose Z buffer CONTAINS TBP and
//                      that wrote Z most recently serves its depth attachment
//                      (fmt 1 Z32 read of a Z32 buffer, fmt 2 Z16 read of a Z16
//                      buffer at its base, fmt 3 Z16 view of a Z32 buffer) when it
//                      is not the draw's own target, the reader TBW matches and the
//                      page-row offset lies inside the buffer; every other case
//                      syncs the Z buffer to VRAM and declines.
//   RefreshZSource  -- a Z-format read that did NOT get a depth serve (a colour
//                      serve, a tier-1 decline or a depth-tier decline alike): the
//                      Z buffer that CONTAINS TBP, wrote Z within the last two
//                      frames and holds valid content (the most recent Z writer
//                      when several do) is written back to VRAM before the decode,
//                      so the decode reads the live Z bytes -- also over a colour
//                      target's rows deposited at the same address just before.
//   OwnedSlot       -- (first inside the decode fallback) a read whose TEX0 tuple
//                      names a texture slot an owner bound without transferring
//                      its bytes (OwnedTextureSlots): the owner's converted image
//                      serves, with the same mip-chain depth the decode would have
//                      built; a read over a bound slot the bind does not describe
//                      is a counted miss and falls through to the VRAM decode.
//   DecodeFallback  -- no snapshot: textured UV mode with nothing bound; else the
//                      decode caches (L1 by content key, then L2), else a fresh
//                      decode of the composed VRAM view -- UV-bounded when the
//                      vertex bbox derives a sub-rect (UvRegion) -- uploaded and
//                      recorded in both caches. Before the lookup the resolver
//                      reads ONLY the bytes the key hashes (the level-0 window,
//                      the CLUT page of an indexed read, the authored mip windows)
//                      into grow-only scratch buffers; the composed 4 MiB view the
//                      decoder addresses is read on a miss only.
// Every call starts from a default TextureSource (no per-call state survives).
#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <unordered_map>
#include <vector>

#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/targets/render_target_cache.h"
#include "engine/gs/hle/targets/target_refresher.h"
#include "engine/gs/hle/targets/vram_layout.h"
#include "engine/gs/hle/texsrc/clut_heal_cache.h"
#include "engine/gs/hle/texsrc/clut_source.h"
#include "engine/gs/hle/texsrc/content_key.h"
#include "engine/gs/hle/texsrc/decode_cache.h"
#include "engine/gs/hle/texsrc/mip_sampler.h"
#include "engine/gs/hle/texsrc/owned_texture_slots.h"
#include "engine/gs/hle/texsrc/texres_trace.h"
#include "engine/gs/hle/texsrc/texture_source.h"
#include "engine/gs/vram/vram_image.h"
#include "engine/render/backend.h"

namespace ps2r {
namespace gs {

class TextureResolver {
public:
    struct Context {
        uint32_t frame_count = 0;        // renderer frame counter (recency, lane records)
        uint32_t active_fbp = 0;         // the draw's FRAME.FBP (self-read)
        bool fast_hash = true;
        bool has_snapshot = true;        // false: the driver carries no VRAM snapshot (replay)
        uint32_t draw_seq = 0;           // the pipeline draw ordinal (trace only)
    };
    // The decode fallback's census: how the cache lookups resolved and how many
    // VRAM bytes were copied for them. `prep_bytes` counts the bytes read BEFORE
    // the lookup (the key's inputs: O(texture window) per call); `view_bytes`
    // counts the composed-view reads a decode needs (O(VRAM) per miss only).
    struct Census {
        uint64_t lookups = 0;            // DecodeFallback calls that reached the key
        uint64_t l1_hits = 0;
        uint64_t l2_hits = 0;
        uint64_t decodes = 0;
        uint64_t prep_bytes = 0;
        uint64_t view_bytes = 0;
        uint64_t sig_hits = 0;           // key reused: the hashed pages' view generations unchanged
        uint64_t owned_serves = 0;       // reads served from an owned slot bind (no VRAM read)
        uint64_t owned_misses = 0;       // reads over an owned slot the bind does not describe
    };
    static constexpr uint32_t kRecentFrames = 2;
    // A register tuple that fragments into more distinct UV sub-rects than this
    // is forced back to full decodes for good.
    static constexpr size_t kMaxRectVariants = 8;

    TextureResolver(render::Backend& be, VramImage& vram, RenderTargetCache& targets,
                    TargetRefresher& refresher, const VramLayout& layout);

    TextureSource ResolveRead(const DrawIr& ir, const Context& ctx);
    // After a draw: the rows it rendered ARE the VRAM content, so the decoded
    // textures and upload records over them (page-rounded) are stale and go.
    void EvictUnderDraw(const DrawIr& ir, const render::DrawResult& res, int render_h);
    // The InvalidateTextures job: an EE write of [addr, addr+size) landed in the
    // mirror -- its pages are CPU-authored again (no owed band), every decoded
    // texture whose source window overlaps is retired, and the pages are marked
    // CPU-dirty for the 16-bit alias demand and the indexed-serve veto.
    void OnCpuWrite(uint32_t addr, uint32_t size);

    DecodeCache& Cache() { return cache_; }
    // The slots an owner uploaded without bytes (bind / release / evict on the
    // consumer thread; the resolver serves them before any VRAM read).
    OwnedTextureSlots& Owned() { return owned_; }
    const OwnedTextureSlots& Owned() const { return owned_; }
    ClutSource& Clut() { return clut_; }
    ClutHealCache& Heal() { return heal_; }
    const Census& Stats() const { return census_; }
    // The source-composition trace (inert until a filter enables it).
    TexresTrace& Trace() { return trace_; }
    const TexresTrace& Trace() const { return trace_; }

    // Predicates (public for the unit tests).
    static bool Is16BitClass(uint32_t psm);
    static bool IsIndexedPsm(uint32_t psm);
    static bool IsZPsm(uint32_t psm);
    bool ClutServable(const TexReadView& v) const;
    static bool RtKeepServe(bool valid, bool bpp_compatible, uint64_t footprint_upload_epoch,
                            uint64_t last_draw_epoch);
    // Max upload epoch over the pages of [addr, addr+size).
    uint64_t WindowEpoch(uint32_t addr, uint32_t size) const;
    // Max upload epoch over exactly the pages a (tbp, tbw, tw, th, psm) read touches;
    // UINT64_MAX when the read shape is outside the provable class.
    uint64_t ReadRectUploadEpoch(const TexReadView& v) const;
    // The shuffle class of a plain RT serve and its u scale (render vs read PSM).
    static void ClassifyShuffle(uint32_t render_psm, uint32_t read_psm, uint8_t* type, float* scale_u);

    // Called with TEX0.TBP at every textured resolve before the target match (the
    // OLD FindTargetTexture entry hook): a serve of the armed stencil accumulator
    // materialises it first.
    void SetTouchHook(std::function<void(uint32_t)> hook) { touch_ = std::move(hook); }

private:
    std::function<void(uint32_t)> touch_;
    struct RectVariants {
        std::set<uint64_t> rects;
        bool sticky_full = false;
    };
    bool MatchTarget(const DrawIr& ir, const Context& ctx, RenderTarget& t, TextureSource* out);
    bool MatchColor(const DrawIr& ir, const Context& ctx, RenderTarget& t, TextureSource* out);
    bool MatchIndexed(const DrawIr& ir, const Context& ctx, RenderTarget& t, TextureSource* out);
    bool LaneLiveRescue(const DrawIr& ir, const Context& ctx, RenderTarget& t,
                        render::SurfaceId* lane1, render::SurfaceId* lane2) const;
    bool MatchDepth(const DrawIr& ir, const Context& ctx, TextureSource* out);
    // The Z-source refresh for a Z-format read whose result carries no depth serve.
    void RefreshZSource(const DrawIr& ir, const Context& ctx, const TextureSource& out);
    void DecodeFallback(const DrawIr& ir, const Context& ctx, TextureSource* out);
    // The trace line of a decode-fallback read: the flushed entries BEFORE the flush
    // (first call), the page composition after the window reads (second call).
    void TraceBegin(const DrawIr& ir, const Context& ctx, TexresTrace::Line* line) const;
    void TraceCompose(const TexReadView& v, uint32_t fp_start, uint32_t fp_size, uint32_t win_size,
                      TexresTrace::Line* line) const;
    bool DeriveRegion(const DrawIr& ir, int tex_w, int tex_h, ContentKey::Rect* rgn);
    render::SurfaceId DecodeAndUpload(const DrawIr& ir, const ContentKey::Rect& rgn, bool sub, int dw,
                                      int dh, const MipSampler::Chain& mips, uint8_t* levels);
    void ApplyFmvEdgeClamp(const DrawIr& ir, const ContentKey::Rect& rgn, int dw, int dh);
    void FlushPendingUnderRead(const TexReadView& v, const RenderTarget& t);
    void RescueDeposit(const DrawIr& ir, RenderTarget& t);
    bool IdxFresh(uint32_t base, const RenderTarget& t, uint8_t want_class) const;
    void FillSampler(const TexReadView& v, TextureSource* out) const;
    // The colour-class serve outputs: owner, target dims, shuffle fold, STQ prescale.
    void ServeColor(const DrawIr& ir, const Context& ctx, const RenderTarget& t,
                    render::SurfaceId surface, bool alt16_serve, TextureSource* out) const;

    render::Backend& be_;
    VramImage& vram_;
    RenderTargetCache& targets_;
    TargetRefresher& refresher_;
    const VramLayout& layout_;
    DecodeCache cache_;
    OwnedTextureSlots owned_;
    ClutSource clut_;
    ClutHealCache heal_;
    std::unordered_map<uint64_t, RectVariants> rect_variants_;   // per register tuple
    struct LastKey {
        uint64_t key;        // the last content key computed under the tuple
        uint64_t view_sig;   // VramImage::ViewSignature of the pages it hashed
    };
    std::unordered_map<uint64_t, LastKey> tuple_last_key_;       // register tuple -> last content key
    std::unordered_map<uint32_t, uint8_t> tex_levels_;           // decoded surface -> authored mip levels
    // Scratch buffers reused across calls (grow-only: a resize never shrinks them).
    std::vector<uint8_t> view_;                       // the composed 4 MiB view, read on a decode only
    std::vector<uint8_t> window_;                     // the hashed level-0 window (or its row band)
    std::vector<uint8_t> clut_page_;                  // the CLUT page of an indexed read
    std::vector<std::vector<uint8_t>> mip_win_;       // one hashed window per authored mip level
    std::vector<ContentKey::MipInput> mip_in_;
    std::vector<uint32_t> texels_;
    Census census_;
    TexresTrace trace_;
    // The scratch buffer's first `n` bytes, growing it once when it is shorter.
    static uint8_t* Scratch(std::vector<uint8_t>& buf, size_t n);
};

}  // namespace gs
}  // namespace ps2r
