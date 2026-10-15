// render/backends/gl/gl_uniform_cache.cpp -- GlUniformCache implementation.
#include "engine/render/backends/gl/gl_uniform_cache.h"

#include "engine/platform/host/platform.h"
#include PS2R_GL_HEADER

namespace ps2r {
namespace render {

void GlUniformCache::Clear() { shadow_.clear(); }

void GlUniformCache::Set1i(int loc, int v) {
    if (loc < 0) return;
    Shadow& s = shadow_[loc];
    if (s.kind == 1 && s.i[0] == v) { skips_++; return; }
    s.kind = 1; s.i[0] = v;
    glUniform1i(loc, v);
}

void GlUniformCache::Set1f(int loc, float v) {
    if (loc < 0) return;
    Shadow& s = shadow_[loc];
    if (s.kind == 2 && s.f[0] == v) { skips_++; return; }
    s.kind = 2; s.f[0] = v;
    glUniform1f(loc, v);
}

void GlUniformCache::Set2f(int loc, float a, float b) {
    if (loc < 0) return;
    Shadow& s = shadow_[loc];
    if (s.kind == 3 && s.f[0] == a && s.f[1] == b) { skips_++; return; }
    s.kind = 3; s.f[0] = a; s.f[1] = b;
    glUniform2f(loc, a, b);
}

void GlUniformCache::Set2i(int loc, int a, int b) {
    if (loc < 0) return;
    Shadow& s = shadow_[loc];
    if (s.kind == 5 && s.i[0] == a && s.i[1] == b) { skips_++; return; }
    s.kind = 5; s.i[0] = a; s.i[1] = b;
    glUniform2i(loc, a, b);
}

void GlUniformCache::Set3f(int loc, float a, float b, float c) {
    if (loc < 0) return;
    Shadow& s = shadow_[loc];
    if (s.kind == 4 && s.f[0] == a && s.f[1] == b && s.f[2] == c) { skips_++; return; }
    s.kind = 4; s.f[0] = a; s.f[1] = b; s.f[2] = c;
    glUniform3f(loc, a, b, c);
}

void GlUniformCache::Set4f(int loc, float a, float b, float c, float d) {
    if (loc < 0) return;
    Shadow& s = shadow_[loc];
    if (s.kind == 6 && s.f[0] == a && s.f[1] == b && s.f[2] == c && s.f[3] == d) {
        skips_++;
        return;
    }
    s.kind = 6; s.f[0] = a; s.f[1] = b; s.f[2] = c; s.f[3] = d;
    glUniform4f(loc, a, b, c, d);
}

}  // namespace render
}  // namespace ps2r
