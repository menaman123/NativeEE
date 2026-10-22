// render/backends/gl/gl_uniform_cache.h -- GlUniformCache: per-location value shadow
// for the PS2 program's scalar uniforms.
//
// The draw path sets ~30 scalar uniforms per draw and consecutive draws share
// almost all of them; each setter records the last value sent per location and
// skips the glUniform* call when unchanged. Uniform values are per-program state,
// so the shadow is cleared on every relink. A negative location is a silent no-op,
// as glUniform* defines it. The shadow is a flat table indexed by location (the
// linker hands out small dense integers; a location past the table grows it), so
// the ~40 lookups per draw cost an index, not a hash.
#pragma once

#include <cstdint>
#include <vector>

#include "engine/render/backend.h"

namespace ps2r {
namespace render {

class GlUniformCache {
public:
    void Clear();
    void Set1i(int loc, int v);
    void Set1f(int loc, float v);
    void Set2f(int loc, float a, float b);
    void Set2i(int loc, int a, int b);
    void Set3f(int loc, float a, float b, float c);
    void Set4f(int loc, float a, float b, float c, float d);
    uint64_t Skips() const { return skips_; }
    // The census the issued glUniform* calls are counted into (null = none).
    void SetCensus(CallCensus* census) { census_ = census; }

private:
    struct Shadow {
        uint8_t kind = 0;   // 1=1i 2=1f 3=2f 4=3f 5=2i 6=4f
        int i[2] = {0, 0};
        float f[4] = {0.f, 0.f, 0.f, 0.f};
    };
    Shadow& At(int loc);
    std::vector<Shadow> shadow_;
    uint64_t skips_ = 0;
    CallCensus* census_ = nullptr;
};

}  // namespace render
}  // namespace ps2r
