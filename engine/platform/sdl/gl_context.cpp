// platform/sdl/gl_context.cpp -- GlContext implementation.
#include "engine/platform/sdl/gl_context.h"

#include <SDL.h>
#include "engine/platform/host/platform.h"
#include PS2R_GL_HEADER

#include <cstdio>

#include "engine/platform/sdl/window.h"

namespace ps2r {
namespace platform {

GlContext::~GlContext() { Destroy(); }

void GlContext::RequestAttributes() {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 32);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
}

bool GlContext::Create(Window& window) {
    Destroy();
    if (!window.Handle()) return false;
    context_ = SDL_GL_CreateContext(window.Handle());
    if (!context_) {
        std::fprintf(stderr, "[gl] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return false;
    }
    if (!MakeCurrent(window)) return false;
    std::string missing;
    if (!LoadEntryPoints(&missing)) {
        std::fprintf(stderr, "[gl] unresolved GL entry points: %s\n", missing.c_str());
        Destroy();
        return false;
    }
    SetSwapInterval(0);
    const GLubyte* v = glGetString(GL_VERSION);
    const GLubyte* r = glGetString(GL_RENDERER);
    version_ = v ? reinterpret_cast<const char*>(v) : "";
    renderer_ = r ? reinterpret_cast<const char*>(r) : "";
    return true;
}

void GlContext::Destroy() {
    if (context_) {
        SDL_GL_DeleteContext(static_cast<SDL_GLContext>(context_));
        context_ = nullptr;
    }
}

bool GlContext::MakeCurrent(Window& window) {
    if (!context_ || !window.Handle()) return false;
    if (SDL_GL_MakeCurrent(window.Handle(), static_cast<SDL_GLContext>(context_)) != 0) {
        std::fprintf(stderr, "[gl] SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

void GlContext::SetSwapInterval(int interval) { SDL_GL_SetSwapInterval(interval); }

bool GlContext::LoadEntryPoints(std::string* error) {
    std::string missing;
    if (::ps2r::host::GlLoadEntryPoints(&SDL_GL_GetProcAddress, &missing)) return true;
    if (error) *error = "unresolved GL entry points: " + missing;
    return false;
}

}  // namespace platform
}  // namespace ps2r
