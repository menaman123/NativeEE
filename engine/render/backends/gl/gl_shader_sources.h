// render/backends/gl/gl_shader_sources.h -- GlShaderSources: accessors for the ONE copy
// of the GLSL text (PS2 draw program vertex + fragment, scanout quad vertex +
// fragment, the stencil-composite specialisations: strip-count / invert fragments
// over the PS2 vertex program, the constant-colour quad pair, and the folded CLEAR
// arms kept for the text gate). The text lives in gl_shader_sources.cpp as raw
// string literals.
#pragma once

namespace ps2r {
namespace render {

class GlShaderSources {
public:
    static const char* Ps2Vertex();
    static const char* Ps2Fragment();
    static const char* ScanoutVertex();
    static const char* ScanoutFragment();
    static const char* StencilClearFragment();
    static const char* StencilStripFragment();
    static const char* StencilInvertFragment();
    static const char* ConstVertex();
    static const char* ConstFragment();
    static const char* HalfwordShuffleFragment();
    static const char* Pack5551Fragment();
};

}  // namespace render
}  // namespace ps2r
