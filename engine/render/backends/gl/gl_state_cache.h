// render/backends/gl/gl_state_cache.h -- GlStateCache: the GL binding / fixed-function
// state shadow.
//
// Every GL bind and fixed-function state call of the backend goes through this
// object; a call is skipped only when the shadow -- fed by every site -- already
// records the requested value, so the GL server state always equals what a raw
// call would have produced. Deletion follows the GL 4.1 rule that deleting a bound
// framebuffer / texture / buffer / vertex array reverts that binding to 0; a deleted
// program folds to "unknown" (a program in use is only flagged for deletion and its
// name may be reused). Texture parameters are shadowed per texture object so a name
// re-used after glGenTextures starts with a clean record. Single GL thread.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/render/backend.h"

namespace ps2r {
namespace render {

class GlStateCache {
public:
    static constexpr uint32_t kUnknown = 0xFFFFFFFFu;
    static constexpr int kMaxUnits = 8;

    void InvalidateAll();

    // framebuffer
    void BindFb(uint32_t target, uint32_t fbo);
    void DeleteFramebuffers(int n, const uint32_t* ids);
    // texture units / objects
    void ActiveTexture(uint32_t unit_enum);
    void BindTex2D(uint32_t tex);
    void TexParami(uint32_t pname, int param);
    void TexParamf(uint32_t pname, float param);
    void DeleteTextures(int n, const uint32_t* ids);
    // program / array buffer / viewport
    void UseProgram(uint32_t prog);
    void DeleteProgram(uint32_t prog);
    void BindArrayBuffer(uint32_t buf);
    void DeleteBuffers(int n, const uint32_t* ids);
    void Viewport(int x, int y, int w, int h);
    // fixed-function draw state
    void Enable(uint32_t cap);
    void Disable(uint32_t cap);
    void BlendEquationSeparate(uint32_t rgb, uint32_t a);
    void BlendFuncSeparate(uint32_t sr, uint32_t dr, uint32_t sa, uint32_t da);
    void BlendFunc(uint32_t s, uint32_t d);
    void BlendColor(float r, float g, float b, float a);
    void DepthFunc(uint32_t f);
    void DepthMask(bool m);
    void ColorMask(bool r, bool g, bool b, bool a);
    void Scissor(int x, int y, int w, int h);
    void BindVertexArray(uint32_t vao);
    void DeleteVertexArrays(int n, const uint32_t* ids);

    uint64_t Skips() const { return skips_; }
    // The census the issued binds are counted into (null = none).
    void SetCensus(CallCensus* census) { census_ = census; }

private:
    struct TexParamBucket {
        std::vector<std::pair<uint32_t, int32_t>> params;
    };
    bool TexParamCheckSet(uint32_t tex, uint32_t pname, int32_t bits);
    void SetCap(uint32_t cap, bool on);

    uint32_t fbo_draw_ = kUnknown;
    uint32_t fbo_read_ = kUnknown;
    int active_unit_ = -1;
    uint32_t tex2d_[kMaxUnits] = {kUnknown, kUnknown, kUnknown, kUnknown,
                                  kUnknown, kUnknown, kUnknown, kUnknown};
    std::unordered_map<uint32_t, TexParamBucket> tex_params_;

    uint32_t program_ = kUnknown;
    uint32_t array_buffer_ = kUnknown;
    bool viewport_known_ = false;
    int viewport_[4] = {0, 0, 0, 0};

    int8_t cap_blend_ = -1, cap_depth_ = -1, cap_scissor_ = -1;
    uint32_t blend_eq_rgb_ = kUnknown, blend_eq_a_ = kUnknown;
    uint32_t blend_src_rgb_ = kUnknown, blend_dst_rgb_ = kUnknown;
    uint32_t blend_src_a_ = kUnknown, blend_dst_a_ = kUnknown;
    bool blend_color_known_ = false;
    float blend_color_[4] = {0, 0, 0, 0};
    uint32_t depth_func_ = kUnknown;
    int8_t depth_mask_ = -1;
    int8_t color_mask_[4] = {-1, -1, -1, -1};
    bool scissor_known_ = false;
    int scissor_box_[4] = {0, 0, 0, 0};
    uint32_t vao_ = kUnknown;
    uint64_t skips_ = 0;
    CallCensus* census_ = nullptr;
};

}  // namespace render
}  // namespace ps2r
