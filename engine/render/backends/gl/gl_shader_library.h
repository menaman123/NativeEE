// render/backends/gl/gl_shader_library.h -- GlShaderLibrary: compiles and links the
// programs (PS2 draw, scanout quad, the stencil-composite strip-count / invert /
// constant-quad specialisations) and holds the PS2 program's uniform-location table
// plus the specialisations' few locations.
//
// The sources are the baseline text of GlShaderSources; compile / link failures are
// reported on stderr with the driver log and leave the program id 0. After a
// successful link the sampler uniforms are bound to fixed units (u_texture 0,
// u_rt_texture 1, u_clut 2, u_idx_tex2 3, u_zsrc_tex 4) and the arms that a draw
// may leave untouched are primed to their off values so the first draws are
// deterministic.
#pragma once

#include <cstdint>

namespace ps2r {
namespace render {

class GlUniformCache;

// Uniform locations of the PS2 draw program (-1 when the driver optimised the
// uniform away; glUniform* on -1 is a no-op).
struct GlUniformLocations {
    int screen_size = -1, use_texture = -1, tex_sampler = -1;
    int blend_a = -1, blend_b = -1, blend_c = -1, blend_d = -1, fix_alpha = -1, blend_enabled = -1;
    int tfx = -1, tcc = -1, fba = -1, colclamp = -1, scanmsk = -1;
    int zte = -1, ztst = -1;
    int ate = -1, atst = -1, aref = -1, afail = -1, date = -1, datm = -1;
    int fog_enabled = -1, fogcol = -1;
    int tex_size = -1, uv_scale = -1, uv_mode = -1;
    int sw_blend = -1, rt_sampler = -1, flip_v = -1;
    int fbmsk16_en = -1, fbmsk16 = -1;
    int fbmsk32_en = -1, fbmsk32 = -1;
    int depth_fmt = -1, depth_zoff = -1;
    int idx_fmt = -1, idx_self = -1, draw_scale = -1, uv_native = -1;
    int clut_sampler = -1, idx_tex2_sampler = -1;
    int fb16 = -1, ct16q = -1, dthe = -1, dimx_lo = -1, dimx_hi = -1;
    int zsrc_en = -1, zsrc_sampler = -1, zsrc_num = -1, zsrc_den = -1, clut_raw = -1;
    int mip_ctl = -1, mip_lodp = -1, tex_lod_hw = -1;
};

// The stencil-composite programs' uniform locations (samplers are bound once at
// link: u_rt_texture 1, u_zsrc_tex 4 -- the PS2 program's units).
struct GlStencilLocations {
    int strip_screen = -1, strip_zsrc_en = -1, strip_datm = -1, strip_zsrc_num = -1, strip_zsrc_den = -1;
    int invert_screen = -1, invert_fix = -1;
    int const_color = -1;
    int hw_screen = -1, hw_dest_hi = -1, hw_mask16 = -1, hw_ta0 = -1, hw_ta1 = -1, hw_aem = -1;
};

class GlShaderLibrary {
public:
    // Compiles + links both programs; primes the PS2 program through `uniforms`
    // (which is cleared first). Returns false on any failure.
    bool Build(GlUniformCache& uniforms);
    void Destroy();

    uint32_t Ps2Program() const { return ps2_program_; }
    uint32_t ScanoutProgram() const { return scanout_program_; }
    int ScanoutTextureLocation() const { return scanout_tex_loc_; }
    const GlUniformLocations& Locations() const { return loc_; }
    uint32_t StencilStripProgram() const { return strip_program_; }
    uint32_t StencilInvertProgram() const { return invert_program_; }
    uint32_t ConstProgram() const { return const_program_; }
    uint32_t HalfwordShuffleProgram() const { return halfword_program_; }
    uint32_t Pack5551Program() const { return pack5551_program_; }
    const GlStencilLocations& StencilLocations() const { return sloc_; }

    static const char* VertexSource();
    static const char* FragmentSource();
    static const char* ScanoutVertexSource();
    static const char* ScanoutFragmentSource();

private:
    static uint32_t CompileShader(uint32_t type, const char* source);
    static uint32_t LinkProgram(uint32_t vert, uint32_t frag);
    void LookupLocations();
    void PrimeUniforms(GlUniformCache& uniforms);

    bool BuildStencilPrograms();

    uint32_t ps2_program_ = 0;
    uint32_t scanout_program_ = 0;
    uint32_t strip_program_ = 0;
    uint32_t invert_program_ = 0;
    uint32_t const_program_ = 0;
    uint32_t halfword_program_ = 0;
    uint32_t pack5551_program_ = 0;
    GlStencilLocations sloc_;
    int scanout_tex_loc_ = -1;
    GlUniformLocations loc_;
};

}  // namespace render
}  // namespace ps2r
