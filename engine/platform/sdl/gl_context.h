// platform/sdl/gl_context.h -- GlContext: an OpenGL 4.1 core-profile context on an
// Window.
//
// 4.1 core is the macOS ceiling and the profile every shader in the tree targets;
// the depth attribute asks for 32 bits and the swap interval is 0 so a headless
// or harness run is never quantised to the display refresh (the presenter paces
// frames itself).
#pragma once

namespace ps2r {
namespace platform {

class Window;

class GlContext {
public:
    GlContext() = default;
    ~GlContext();
    GlContext(const GlContext&) = delete;
    GlContext& operator=(const GlContext&) = delete;

    // Sets the SDL_GL_* attributes (called by Window::Create after video init).
    static void RequestAttributes();
    bool Create(Window& window);
    void Destroy();
    bool MakeCurrent(Window& window);
    void SetSwapInterval(int interval);
    bool IsCreated() const { return context_ != nullptr; }
    const char* Version() const { return version_; }
    const char* Renderer() const { return renderer_; }

private:
    void* context_ = nullptr;     // SDL_GLContext
    const char* version_ = "";
    const char* renderer_ = "";
};

}  // namespace platform
}  // namespace ps2r
