// render/backends/gl/gl_shader_library.cpp -- GlShaderLibrary implementation.
#include "engine/render/backends/gl/gl_shader_library.h"

#include "engine/platform/host/platform.h"
#include PS2R_GL_HEADER

#include <cstdio>

#include "engine/render/backends/gl/gl_shader_sources.h"
#include "engine/render/backends/gl/gl_uniform_cache.h"

namespace ps2r {
namespace render {

const char* GlShaderLibrary::VertexSource() { return GlShaderSources::Ps2Vertex(); }
const char* GlShaderLibrary::FragmentSource() { return GlShaderSources::Ps2Fragment(); }
const char* GlShaderLibrary::ScanoutVertexSource() { return GlShaderSources::ScanoutVertex(); }
const char* GlShaderLibrary::ScanoutFragmentSource() { return GlShaderSources::ScanoutFragment(); }

uint32_t GlShaderLibrary::CompileShader(uint32_t type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[gl] shader compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

uint32_t GlShaderLibrary::LinkProgram(uint32_t vert, uint32_t frag) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[gl] program link error: %s\n", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

bool GlShaderLibrary::Build(GlUniformCache& uniforms) {
    Destroy();
    GLuint vs = CompileShader(GL_VERTEX_SHADER, VertexSource());
    if (!vs) return false;
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, FragmentSource());
    if (!fs) { glDeleteShader(vs); return false; }
    ps2_program_ = LinkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ps2_program_) return false;

    GLuint svs = CompileShader(GL_VERTEX_SHADER, ScanoutVertexSource());
    if (!svs) return false;
    GLuint sfs = CompileShader(GL_FRAGMENT_SHADER, ScanoutFragmentSource());
    if (!sfs) { glDeleteShader(svs); return false; }
    scanout_program_ = LinkProgram(svs, sfs);
    glDeleteShader(svs);
    glDeleteShader(sfs);
    if (!scanout_program_) return false;
    scanout_tex_loc_ = glGetUniformLocation(scanout_program_, "u_texture");
    if (!BuildStencilPrograms()) return false;

    LookupLocations();
    PrimeUniforms(uniforms);
    return true;
}

// The three stencil-composite programs: strip-count and invert fragments over the
// PS2 vertex program (their sampler units bound once), the constant quad pair.
bool GlShaderLibrary::BuildStencilPrograms() {
    struct Prog { const char* vs; const char* fs; uint32_t* out; };
    const Prog progs[4] = {
        {VertexSource(), GlShaderSources::StencilStripFragment(), &strip_program_},
        {VertexSource(), GlShaderSources::StencilInvertFragment(), &invert_program_},
        {GlShaderSources::ConstVertex(), GlShaderSources::ConstFragment(), &const_program_},
        {VertexSource(), GlShaderSources::HalfwordShuffleFragment(), &halfword_program_},
    };
    for (const Prog& p : progs) {
        GLuint vs = CompileShader(GL_VERTEX_SHADER, p.vs);
        if (!vs) return false;
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, p.fs);
        if (!fs) { glDeleteShader(vs); return false; }
        *p.out = LinkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (!*p.out) return false;
    }
    sloc_.strip_screen = glGetUniformLocation(strip_program_, "u_screen_size");
    sloc_.strip_zsrc_en = glGetUniformLocation(strip_program_, "u_zsrc_en");
    sloc_.strip_datm = glGetUniformLocation(strip_program_, "u_datm");
    sloc_.strip_zsrc_num = glGetUniformLocation(strip_program_, "u_zsrc_num");
    sloc_.strip_zsrc_den = glGetUniformLocation(strip_program_, "u_zsrc_den");
    glUseProgram(strip_program_);
    const GLint rt_s = glGetUniformLocation(strip_program_, "u_rt_texture");
    const GLint zs_s = glGetUniformLocation(strip_program_, "u_zsrc_tex");
    if (rt_s >= 0) glUniform1i(rt_s, 1);
    if (zs_s >= 0) glUniform1i(zs_s, 4);
    sloc_.invert_screen = glGetUniformLocation(invert_program_, "u_screen_size");
    sloc_.invert_fix = glGetUniformLocation(invert_program_, "u_fix_alpha");
    glUseProgram(invert_program_);
    const GLint rt_i = glGetUniformLocation(invert_program_, "u_rt_texture");
    if (rt_i >= 0) glUniform1i(rt_i, 1);
    sloc_.const_color = glGetUniformLocation(const_program_, "u_color");
    sloc_.hw_screen = glGetUniformLocation(halfword_program_, "u_screen_size");
    sloc_.hw_dest_hi = glGetUniformLocation(halfword_program_, "u_dest_hi");
    sloc_.hw_mask16 = glGetUniformLocation(halfword_program_, "u_mask16");
    sloc_.hw_ta0 = glGetUniformLocation(halfword_program_, "u_ta0");
    sloc_.hw_ta1 = glGetUniformLocation(halfword_program_, "u_ta1");
    sloc_.hw_aem = glGetUniformLocation(halfword_program_, "u_aem");
    glUseProgram(halfword_program_);
    const GLint src_h = glGetUniformLocation(halfword_program_, "u_texture");
    const GLint rt_h = glGetUniformLocation(halfword_program_, "u_rt_texture");
    if (src_h >= 0) glUniform1i(src_h, 0);
    if (rt_h >= 0) glUniform1i(rt_h, 1);
    glUseProgram(0);
    return true;
}

void GlShaderLibrary::Destroy() {
    if (ps2_program_) { glDeleteProgram(ps2_program_); ps2_program_ = 0; }
    if (scanout_program_) { glDeleteProgram(scanout_program_); scanout_program_ = 0; }
    if (strip_program_) { glDeleteProgram(strip_program_); strip_program_ = 0; }
    if (invert_program_) { glDeleteProgram(invert_program_); invert_program_ = 0; }
    if (const_program_) { glDeleteProgram(const_program_); const_program_ = 0; }
    if (halfword_program_) { glDeleteProgram(halfword_program_); halfword_program_ = 0; }
    sloc_ = GlStencilLocations{};
    loc_ = GlUniformLocations{};
    scanout_tex_loc_ = -1;
}

void GlShaderLibrary::LookupLocations() {
    const GLuint p = ps2_program_;
    auto L = [p](const char* name) { return glGetUniformLocation(p, name); };
    loc_.screen_size = L("u_screen_size");
    loc_.use_texture = L("u_use_texture");
    loc_.tex_sampler = L("u_texture");
    loc_.blend_a = L("u_blend_a");
    loc_.blend_b = L("u_blend_b");
    loc_.blend_c = L("u_blend_c");
    loc_.blend_d = L("u_blend_d");
    loc_.fix_alpha = L("u_fix_alpha");
    loc_.blend_enabled = L("u_blend_enabled");
    loc_.tfx = L("u_tfx");
    loc_.tcc = L("u_tcc");
    loc_.fba = L("u_fba");
    loc_.colclamp = L("u_colclamp");
    loc_.scanmsk = L("u_scanmsk");
    loc_.zte = L("u_zte");
    loc_.ztst = L("u_ztst");
    loc_.ate = L("u_ate");
    loc_.atst = L("u_atst");
    loc_.aref = L("u_aref");
    loc_.afail = L("u_afail");
    loc_.date = L("u_date");
    loc_.datm = L("u_datm");
    loc_.fog_enabled = L("u_fog_enabled");
    loc_.fogcol = L("u_fogcol");
    loc_.tex_size = L("u_tex_size");
    loc_.uv_scale = L("u_uv_scale");
    loc_.uv_mode = L("u_uv_mode");
    loc_.sw_blend = L("u_sw_blend");
    loc_.rt_sampler = L("u_rt_texture");
    loc_.flip_v = L("u_flip_v");
    loc_.fbmsk16_en = L("u_fbmsk16_en");
    loc_.fbmsk16 = L("u_fbmsk16");
    loc_.fbmsk32_en = L("u_fbmsk32_en");
    loc_.fbmsk32 = L("u_fbmsk32");
    loc_.depth_fmt = L("u_depth_fmt");
    loc_.depth_zoff = L("u_depth_zoff");
    loc_.idx_fmt = L("u_idx_fmt");
    loc_.idx_self = L("u_idx_self");
    loc_.draw_scale = L("u_draw_scale");
    loc_.clut_sampler = L("u_clut");
    loc_.idx_tex2_sampler = L("u_idx_tex2");
    loc_.fb16 = L("u_fb16");
    loc_.ct16q = L("u_ct16q");
    loc_.uv_native = L("u_uv_native");
    loc_.dthe = L("u_dthe");
    loc_.dimx_lo = L("u_dimx_lo");
    loc_.dimx_hi = L("u_dimx_hi");
    loc_.zsrc_en = L("u_zsrc_en");
    loc_.zsrc_sampler = L("u_zsrc_tex");
    loc_.zsrc_num = L("u_zsrc_num");
    loc_.zsrc_den = L("u_zsrc_den");
    loc_.clut_raw = L("u_clut_raw");
    loc_.mip_ctl = L("u_mip_ctl");
    loc_.mip_lodp = L("u_mip_lodp");
    loc_.tex_lod_hw = L("u_tex_lod_hw");
}

// A fresh program object has every uniform at its GL default; the value shadow is
// dropped BEFORE priming so it can never serve a pre-relink value. Units 2/3/4 must
// hold valid textures on every draw (macOS GL returns black for all fetches when a
// statically referenced sampler's unit is incomplete) -- the device binds its dummy
// texture there; the arms are primed OFF.
void GlShaderLibrary::PrimeUniforms(GlUniformCache& uniforms) {
    uniforms.Clear();
    glUseProgram(ps2_program_);
    uniforms.Set1i(loc_.tex_sampler, 0);
    uniforms.Set1i(loc_.rt_sampler, 1);
    uniforms.Set1i(loc_.clut_sampler, 2);
    uniforms.Set1i(loc_.idx_tex2_sampler, 3);
    uniforms.Set1i(loc_.zsrc_sampler, 4);
    uniforms.Set1i(loc_.zsrc_en, 0);
    uniforms.Set1i(loc_.zsrc_num, 1);
    uniforms.Set1i(loc_.zsrc_den, 1);
    uniforms.Set1i(loc_.fb16, 0);
    uniforms.Set1i(loc_.ct16q, 0);
    uniforms.Set1i(loc_.dthe, 0);
    uniforms.Set1i(loc_.clut_raw, 0);
    uniforms.Set1i(loc_.uv_mode, 0);
    uniforms.Set1i(loc_.tex_lod_hw, 0);
    uniforms.Set1i(loc_.fbmsk32_en, 0);
    uniforms.Set2f(loc_.uv_scale, 1.0f, 1.0f);
    glUseProgram(0);
}

}  // namespace render
}  // namespace ps2r
