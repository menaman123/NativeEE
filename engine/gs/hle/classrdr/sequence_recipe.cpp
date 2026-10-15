// gs/hle/classrdr/sequence_recipe.cpp -- the sequence FSM + byte arithmetic.
#include "engine/gs/hle/classrdr/sequence_recipe.h"

#include <climits>

namespace ps2r {
namespace gs {

SequenceRecipe::SequenceRecipe(const Constants& k) : k_(k) {}

uint8_t SequenceRecipe::MaterializeByte(uint8_t stencil_v) const {
    return static_cast<uint8_t>((stencil_v & k_.counter_mask) << 3);
}

uint8_t SequenceRecipe::OracleWrapByte(int net) const {
    return static_cast<uint8_t>(((k_.clear_rgba & 0xFF) + 8 * net) & 0xFF);
}

uint8_t SequenceRecipe::StencilAfter(int net) const {
    return static_cast<uint8_t>((k_.stencil_base + net) & 0xFF);
}

// CSM1 stores a 256-entry CLUT as a 16x16 grid whose middle two 8-entry blocks
// of every 32-entry group are swapped.
uint32_t SequenceRecipe::Csm1Swizzle(uint32_t index) {
    const uint32_t within_group = index % 32;
    if (within_group >= 8 && within_group < 16) return index + 8;
    if (within_group >= 16 && within_group < 24) return index - 8;
    return index;
}

void SequenceRecipe::InvertFold(uint8_t cd_r, uint8_t cd_g, uint8_t cd_b, uint8_t cd_a,
                                uint8_t out[4]) {
    const uint32_t idx = (cd_a >= 255u) ? 128u : (static_cast<uint32_t>(cd_a) >> 1);
    out[0] = static_cast<uint8_t>(255u - cd_r);
    out[1] = static_cast<uint8_t>(255u - cd_g);
    out[2] = static_cast<uint8_t>(255u - cd_b);
    out[3] = static_cast<uint8_t>(255u - Csm1Swizzle(idx));
}

void SequenceRecipe::InvertGenericRef(uint8_t cd_r, uint8_t cd_g, uint8_t cd_b, uint8_t cd_a,
                                      uint8_t out[4]) {
    const uint32_t idx = (cd_a >= 255u) ? 128u : (static_cast<uint32_t>(cd_a) >> 1);
    const int tex_rgb = 255, tex_a = static_cast<int>(255u - Csm1Swizzle(idx));
    const int fxt_rgb = (128 * tex_rgb) / 128;
    int cs_rgb = fxt_rgb + 0;
    if (cs_rgb > 255) cs_rgb = 255;
    auto bl = [&](uint8_t cd) {
        int v = static_cast<int>(static_cast<float>(cs_rgb - static_cast<int>(cd)) * 1.0f);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        return static_cast<uint8_t>(v);
    };
    out[0] = bl(cd_r);
    out[1] = bl(cd_g);
    out[2] = bl(cd_b);
    out[3] = static_cast<uint8_t>(tex_a);
}

bool SequenceRecipe::ClutIsInvertTable(const std::function<uint8_t(uint32_t)>& byte_at,
                                       const std::function<uint32_t(int, int)>& addr_of) {
    for (uint32_t p = 0; p < 256; p++) {
        const uint32_t addr = addr_of(static_cast<int>(p % 16), static_cast<int>(p / 16));
        if (byte_at(addr) != 0xFF) return false;
        if (byte_at(addr + 1) != 0xFF) return false;
        if (byte_at(addr + 2) != 0xFF) return false;
        if (byte_at(addr + 3) != static_cast<uint8_t>(255u - Csm1Swizzle(p))) return false;
    }
    return true;
}

SequenceRecipe::StripDelta SequenceRecipe::ClassifyStripColor(uint8_t r, uint8_t g,
                                                              uint8_t b) const {
    if (r == k_.incr_rgb[0] && g == k_.incr_rgb[1] && b == k_.incr_rgb[2]) return kStripIncr;
    if (r == k_.decr_rgb[0] && g == k_.decr_rgb[1] && b == k_.decr_rgb[2]) return kStripDecr;
    return kStripDecline;
}

SequenceRecipe::StripDelta SequenceRecipe::ClassifyStripVerts(const DrawIr& ir) const {
    if (ir.verts.empty()) return kStripDecline;
    const VertexRec& v0 = ir.verts[0];
    const StripDelta d = ClassifyStripColor(v0.r, v0.g, v0.b);
    if (d == kStripDecline) return kStripDecline;
    for (const VertexRec& v : ir.verts)
        if (v.r != v0.r || v.g != v0.g || v.b != v0.b) return kStripDecline;
    return d;
}

bool SequenceRecipe::ClearPrimes(const DrawIr& ir) const {
    const uint8_t r = k_.clear_rgba & 0xFF, g = (k_.clear_rgba >> 8) & 0xFF,
                  b = (k_.clear_rgba >> 16) & 0xFF, a = (k_.clear_rgba >> 24) & 0xFF;
    for (const VertexRec& v : ir.verts)
        if (v.r != r || v.g != g || v.b != b || v.a != a) return false;
    return !ir.verts.empty();
}

bool SequenceRecipe::InvertVertexColorOk(const DrawIr& ir) const {
    for (const VertexRec& v : ir.verts)
        if (v.r != k_.invert_rgba[0] || v.g != k_.invert_rgba[1] || v.b != k_.invert_rgba[2] ||
            v.a != k_.invert_rgba[3])
            return false;
    return true;
}

void SequenceRecipe::Prime(uint32_t fbp) {
    state_ = kPrimed;
    fbp_ = fbp;
    cnt_.primes++;
}

void SequenceRecipe::Arm(bool alt16) {
    state_ = kArmed;
    alt16_ = alt16;
    ext_x0_ = ext_y0_ = INT_MAX;
    ext_x1_ = ext_y1_ = INT_MIN;
    cnt_.arms++;
}

SequenceRecipe::Action SequenceRecipe::OnPreDraw(const DrawIr& ir, bool leaving_capture) {
    const uint32_t frame_fbp = ir.raster.frame.fbp;
    if (state_ == kArmed) {
        const bool reads = ir.tex_enabled && ir.tex.tbp == fbp_;
        if (frame_fbp == fbp_ || reads || leaving_capture) {
            cnt_.materializes++;
            state_ = kIdle;
            return Action::kMaterialize;
        }
        return Action::kNone;
    }
    if (state_ == kPrimed) {
        // An RGB-writing draw into the primed accumulator kills the base
        // assumption; a draw whose FBMSK protects every RGB bit passes.
        if (frame_fbp == fbp_ && (ir.raster.frame.fbmsk & k_.rgb_mask) != k_.rgb_mask) {
            state_ = kIdle;
            cnt_.demotes++;
            return Action::kDemote;
        }
    }
    return Action::kNone;
}

SequenceRecipe::Action SequenceRecipe::OnTouch(uint32_t fbp) {
    if (state_ == kArmed && fbp == fbp_) {
        cnt_.materializes++;
        state_ = kIdle;
        return Action::kMaterialize;
    }
    return Action::kNone;
}

void SequenceRecipe::ExtendExtent(int x0, int y0, int x1, int y1) {
    if (x1 <= x0 || y1 <= y0) return;
    if (x0 < ext_x0_) ext_x0_ = x0;
    if (y0 < ext_y0_) ext_y0_ = y0;
    if (x1 > ext_x1_) ext_x1_ = x1;
    if (y1 > ext_y1_) ext_y1_ = y1;
}

}  // namespace gs
}  // namespace ps2r
