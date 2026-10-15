// render/backends/gl/gl_vertex_stream.h -- GlVertexStream: the vertex array object, the
// 16 MiB streaming vertex buffer ring, and the run brackets.
//
// Per draw the converted vertices are bump-allocated (32-byte aligned) inside one
// buffer through glMapBufferRange(WRITE | UNSYNCHRONIZED | FLUSH_EXPLICIT) and the
// draw addresses them as a first-vertex index against the attribute layout
// configured once at offset 0 with stride 32. On wrap the fence of the previous wrap
// is waited on (bounding in-flight generations to one buffer), the storage is
// orphaned and a new fence issued. A run of consecutive draws keeps the VAO and
// program bound; EndDrawRun unbinds once at the end of the run (VAO 0, program
// 0) so any non-draw GL work starts from the same state.
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/render/backends/gl/gpu_vertex.h"

namespace ps2r {
namespace render {

class GlStateCache;

class GlVertexStream {
public:
    static constexpr size_t kRingSize = 16u * 1024u * 1024u;

    bool Init(GlStateCache& st);
    void Shutdown(GlStateCache& st);

    // Binds the VAO + array buffer for a draw of the PS2 program.
    void BindForDraw(GlStateCache& st);
    // Uploads n vertices into the ring; returns the first-vertex index for glDrawArrays.
    // GL_ARRAY_BUFFER must be bound to the ring (BindForDraw).
    int Upload(const GpuVertex* verts, size_t n);

    void BeginDrawRun();
    void EndDrawRun(GlStateCache& st);
    bool InDrawRun() const { return in_run_; }
    uint64_t Generation() const { return generation_; }

private:
    uint32_t vao_ = 0;
    uint32_t vbo_ = 0;
    size_t offset_ = 0;
    bool ready_ = false;
    void* fence_ = nullptr;      // GLsync of the previous wrap
    uint64_t generation_ = 1;    // bumped on every re-specification
    bool in_run_ = false;
};

}  // namespace render
}  // namespace ps2r
