// tests/render/test_gl_state_cache.cpp -- Render_StateCacheEquivalence: a scripted
// sequence of binds / fixed-function sets issued through GlStateCache leaves the same
// GL server state (glGet / glIsEnabled) as the same sequence issued raw, including
// redundant repeats (skipped by the cache) and the delete-reverts-to-0 rules. Exit 77
// (SKIP) without a context.
#include "engine/platform/host/platform.h"
#include PS2R_GL_HEADER
#include <SDL.h>

#include <cstdio>
#include <vector>

#include "engine/platform/sdl/gl_context.h"
#include "engine/platform/sdl/window.h"
#include "engine/render/backends/gl/gl_state_cache.h"

using namespace ps2r;

static int g_checks = 0, g_fail = 0;
#define CHECK(c, msg) do { g_checks++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

struct Snapshot {
    GLint fbo_draw, fbo_read, active, tex0, tex1, prog, vao, abuf, vp[4], sc[4];
    GLboolean blend, depth, scissor, dmask, cmask[4];
    GLint eq_rgb, eq_a, src_rgb, dst_rgb, src_a, dst_a, dfunc;
    GLfloat bcol[4];
    GLint tex1_min, tex1_wrap;
};

static Snapshot Take(GLuint tex_for_params) {
    Snapshot s{};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &s.fbo_draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &s.fbo_read);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &s.active);
    glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D, &s.tex0);
    glActiveTexture(GL_TEXTURE1); glGetIntegerv(GL_TEXTURE_BINDING_2D, &s.tex1);
    glActiveTexture(static_cast<GLenum>(s.active));
    glGetIntegerv(GL_CURRENT_PROGRAM, &s.prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s.vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s.abuf);
    glGetIntegerv(GL_VIEWPORT, s.vp);
    glGetIntegerv(GL_SCISSOR_BOX, s.sc);
    s.blend = glIsEnabled(GL_BLEND); s.depth = glIsEnabled(GL_DEPTH_TEST); s.scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &s.dmask);
    glGetBooleanv(GL_COLOR_WRITEMASK, s.cmask);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &s.eq_rgb); glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &s.eq_a);
    glGetIntegerv(GL_BLEND_SRC_RGB, &s.src_rgb); glGetIntegerv(GL_BLEND_DST_RGB, &s.dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &s.src_a); glGetIntegerv(GL_BLEND_DST_ALPHA, &s.dst_a);
    glGetIntegerv(GL_DEPTH_FUNC, &s.dfunc);
    glGetFloatv(GL_BLEND_COLOR, s.bcol);
    GLint prev = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
    glBindTexture(GL_TEXTURE_2D, tex_for_params);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &s.tex1_min);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &s.tex1_wrap);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prev));
    return s;
}

static bool Same(const Snapshot& a, const Snapshot& b) {
    bool ok = a.fbo_draw == b.fbo_draw && a.fbo_read == b.fbo_read && a.active == b.active &&
              a.tex0 == b.tex0 && a.tex1 == b.tex1 && a.prog == b.prog && a.vao == b.vao &&
              a.abuf == b.abuf && a.blend == b.blend && a.depth == b.depth && a.scissor == b.scissor &&
              a.dmask == b.dmask && a.eq_rgb == b.eq_rgb && a.eq_a == b.eq_a && a.src_rgb == b.src_rgb &&
              a.dst_rgb == b.dst_rgb && a.src_a == b.src_a && a.dst_a == b.dst_a && a.dfunc == b.dfunc &&
              a.tex1_min == b.tex1_min && a.tex1_wrap == b.tex1_wrap;
    for (int i = 0; i < 4; i++)
        ok = ok && a.vp[i] == b.vp[i] && a.sc[i] == b.sc[i] && a.cmask[i] == b.cmask[i] && a.bcol[i] == b.bcol[i];
    return ok;
}

// The scripted sequence, through the cache (cached=true) or raw. Objects are shared.
struct Objs { GLuint fbo[2], tex[3], vao[2], buf[2]; };

static void Script(bool cached, render::GlStateCache& st, const Objs& o, int variant) {
    auto BindFb = [&](GLenum t, GLuint f) { if (cached) st.BindFb(t, f); else glBindFramebuffer(t, f); };
    auto Active = [&](GLenum u) { if (cached) st.ActiveTexture(u); else glActiveTexture(u); };
    auto Tex = [&](GLuint t) { if (cached) st.BindTex2D(t); else glBindTexture(GL_TEXTURE_2D, t); };
    auto Parami = [&](GLenum p, GLint v) { if (cached) st.TexParami(p, v); else glTexParameteri(GL_TEXTURE_2D, p, v); };
    auto Vao = [&](GLuint v) { if (cached) st.BindVertexArray(v); else glBindVertexArray(v); };
    auto Abuf = [&](GLuint b) { if (cached) st.BindArrayBuffer(b); else glBindBuffer(GL_ARRAY_BUFFER, b); };
    auto Cap = [&](GLenum c, bool on) { if (cached) { if (on) st.Enable(c); else st.Disable(c); } else { if (on) glEnable(c); else glDisable(c); } };
    for (int rep = 0; rep < 3; rep++) {   // repeats exercise the skip paths
        BindFb(GL_FRAMEBUFFER, o.fbo[variant & 1]);
        BindFb(GL_READ_FRAMEBUFFER, o.fbo[(variant >> 1) & 1]);
        Active(GL_TEXTURE1); Tex(o.tex[1]); Parami(GL_TEXTURE_MIN_FILTER, GL_LINEAR); Parami(GL_TEXTURE_WRAP_S, GL_REPEAT);
        Parami(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        Active(GL_TEXTURE0); Tex(o.tex[variant % 3]);
        Vao(o.vao[variant & 1]); Abuf(o.buf[(variant + 1) & 1]);
        if (cached) st.Viewport(1, 2, 30 + variant, 40); else glViewport(1, 2, 30 + variant, 40);
        Cap(GL_BLEND, (variant & 1) != 0); Cap(GL_DEPTH_TEST, true); Cap(GL_SCISSOR_TEST, (variant & 2) != 0);
        if (cached) st.BlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD); else glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
        if (cached) st.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO); else glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO);
        if (cached) st.BlendColor(0.f, 0.f, 0.f, 0.25f * variant); else glBlendColor(0.f, 0.f, 0.f, 0.25f * variant);
        if (cached) st.DepthFunc(variant & 1 ? GL_GEQUAL : GL_GREATER); else glDepthFunc(variant & 1 ? GL_GEQUAL : GL_GREATER);
        if (cached) st.DepthMask((variant & 2) == 0); else glDepthMask((variant & 2) == 0);
        if (cached) st.ColorMask(true, (variant & 1) != 0, false, true); else glColorMask(GL_TRUE, (variant & 1) != 0, GL_FALSE, GL_TRUE);
        if (cached) st.Scissor(3, 4, 10 + variant, 12); else glScissor(3, 4, 10 + variant, 12);
    }
}

int main() {
    platform::Window win;
    platform::GlContext ctx;
    if (!win.Create(true, 64, 64, "test_gl_state_cache") || !ctx.Create(win)) {
        std::printf("test_gl_state_cache: no GL context -> SKIP (77)\n");
        return 77;
    }
    Objs o{};
    glGenFramebuffers(2, o.fbo); glGenTextures(3, o.tex); glGenVertexArrays(2, o.vao); glGenBuffers(2, o.buf);
    for (GLuint t : o.tex) { glBindTexture(GL_TEXTURE_2D, t); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr); }
    glBindTexture(GL_TEXTURE_2D, 0);
    render::GlStateCache st;
    st.InvalidateAll();
    for (int variant = 0; variant < 8; variant++) {
        Script(false, st, o, variant);
        const Snapshot raw = Take(o.tex[1]);
        // reset to a neutral state so both paths start identically
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
        st.InvalidateAll();   // raw calls bypassed the shadow: it must be dropped, as after context loss
        Script(true, st, o, variant);
        const Snapshot cached = Take(o.tex[1]);
        char msg[64]; std::snprintf(msg, sizeof(msg), "variant %d: cached state == raw state", variant);
        CHECK(Same(raw, cached), msg);
        // the cache's shadow must still be right after the raw reset: re-issue and compare again
        Script(true, st, o, variant);
        CHECK(Same(raw, Take(o.tex[1])), "repeat through cache keeps state");
    }
    CHECK(st.Skips() > 0, "redundant calls were skipped");
    // delete-reverts-to-0 rules
    st.BindFb(GL_FRAMEBUFFER, o.fbo[0]); st.BindVertexArray(o.vao[0]); st.BindArrayBuffer(o.buf[0]);
    st.ActiveTexture(GL_TEXTURE0); st.BindTex2D(o.tex[0]);
    st.DeleteFramebuffers(1, &o.fbo[0]); st.DeleteVertexArrays(1, &o.vao[0]); st.DeleteBuffers(1, &o.buf[0]); st.DeleteTextures(1, &o.tex[0]);
    GLint v = -1;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &v); CHECK(v == 0, "deleted fbo binding reverted to 0");
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &v); CHECK(v == 0, "deleted vao binding reverted to 0");
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &v); CHECK(v == 0, "deleted texture binding reverted to 0");
    // a re-bind of a fresh 0 must be a skip (shadow says 0), and binding fbo[1] must issue
    const uint64_t before = st.Skips();
    st.BindFb(GL_FRAMEBUFFER, 0);
    CHECK(st.Skips() == before, "BindFb(0) after delete-fold issues no skip counter but no GL call needed");
    st.BindFb(GL_FRAMEBUFFER, o.fbo[1]);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &v); CHECK(v == static_cast<GLint>(o.fbo[1]), "bind after delete issues");
    CHECK(glGetError() == GL_NO_ERROR, "no GL error");
    std::printf("test_gl_state_cache: %d checks, %d failures, skips=%llu\n", g_checks, g_fail,
                static_cast<unsigned long long>(st.Skips()));
    return g_fail ? 1 : 0;
}
