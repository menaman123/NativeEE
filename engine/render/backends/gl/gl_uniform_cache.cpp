// render/backends/gl/gl_uniform_cache.cpp -- GlUniformCache implementation.
#include "engine/render/backends/gl/gl_uniform_cache.h"

#include "engine/platform/host/platform.h"
#include PS2R_GL_HEADER

namespace ps2r {
namespace render {

void GlUniformCache::Clear() { shadow_.clear(); }

GlUniformCache::Shadow& GlUniformCache::At(int loc) {
    if (static_cast<size_t>(loc) >= shadow_.size()) shadow_.resize(static_cast<size_t>(loc) + 16);
    return shadow_[static_cast<size_t>(loc)];
}

void GlUniformCache::Set1i(int loc, int v) {
    if (loc < 0) return;
    Shadow& s = At(loc);
    if (s.kind == 1 && s.i[0] == v) { skips_++; return; }
    s.kind = 1; s.i[0] = v;
    if (census_) census_->uniform_sets++;
    glUniform1i(loc, v);
}

void GlUniformCache::Set1f(int loc, float v) {
    if (loc < 0) return;
    Shadow& s = At(loc);
    if (s.kind == 2 && s.f[0] == v) { skips_++; return; }
    s.kind = 2; s.f[0] = v;
    if (census_) census_->uniform_sets++;
    glUniform1f(loc, v);
}

void GlUniformCache::Set2f(int loc, float a, float b) {
    if (loc < 0) return;
    Shadow& s = At(loc);
    if (s.kind == 3 && s.f[0] == a && s.f[1] == b) { skips_++; return; }
    s.kind = 3; s.f[0] = a; s.f[1] = b;
    if (census_) census_->uniform_sets++;
    glUniform2f(loc, a, b);
}

void GlUniformCache::Set2i(int loc, int a, int b) {
    if (loc < 0) return;
    Shadow& s = At(loc);
    if (s.kind == 5 && s.i[0] == a && s.i[1] == b) { skips_++; return; }
    s.kind = 5; s.i[0] = a; s.i[1] = b;
    if (census_) census_->uniform_sets++;
    glUniform2i(loc, a, b);
}

void GlUniformCache::Set3f(int loc, float a, float b, float c) {
    if (loc < 0) return;
    Shadow& s = At(loc);
    if (s.kind == 4 && s.f[0] == a && s.f[1] == b && s.f[2] == c) { skips_++; return; }
    s.kind = 4; s.f[0] = a; s.f[1] = b; s.f[2] = c;
    if (census_) census_->uniform_sets++;
    glUniform3f(loc, a, b, c);
}

void GlUniformCache::Set4f(int loc, float a, float b, float c, float d) {
    if (loc < 0) return;
    Shadow& s = At(loc);
    if (s.kind == 6 && s.f[0] == a && s.f[1] == b && s.f[2] == c && s.f[3] == d) {
        skips_++;
        return;
    }
    s.kind = 6; s.f[0] = a; s.f[1] = b; s.f[2] = c; s.f[3] = d;
    if (census_) census_->uniform_sets++;
    glUniform4f(loc, a, b, c, d);
}

}  // namespace render
}  // namespace ps2r
