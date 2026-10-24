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
#ifdef _WIN32
    // 24, not 32, on Windows. The accelerated ICDs (nvoglv64 / igxelpicd64)
    // advertise 24-bit depth with 8 stencil and no 32-bit depth format at all,
    // while the Microsoft GDI Generic software renderer DOES offer 32 -- so
    // asking for 32 does not fail, it silently selects the software format and
    // the context comes back as "GDI Generic" OpenGL 1.1, where none of the 4.1
    // core entry points exist. (The 24-bit retry in platform/sdl/window.cpp only
    // fires when SDL_CreateWindow fails outright, which this does not.)
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
#else
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 32);
#endif
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
#if defined(_WIN32)
    // The GL entry points are function pointers on Windows (engine/platform/windows/gl_loader.h): fill them from this
    // context's driver before the first GL call below. A driver without one of the 4.1-core names the engine uses
    // cannot run the backend: refuse here with the names rather than crash on the first null call.
    // (The arm's host::GlLoadEntryPoints wraps ps2r::glwin::Load with SDL_GL_GetProcAddress as the resolver -- the
    // wglGetProcAddress failure values filtered, opengl32.dll's exports for the 1.1 names -- and is idempotent, so the
    // four context creators share one table.)
    std::string missing;
    if (!::ps2r::host::GlLoadEntryPoints(&SDL_GL_GetProcAddress, &missing)) {
        std::fprintf(stderr, "[gl] GL entry points are missing from this driver (first: %s) -- of %d; an OpenGL 4.1 core driver is required\n",
                     missing.c_str(), ps2r::glwin::Count());
        if (error) *error = "unresolved GL entry points: " + missing;
        return false;
    }
    return true;
#else
    std::string missing;
    if (::ps2r::host::GlLoadEntryPoints(&SDL_GL_GetProcAddress, &missing)) return true;
    if (error) *error = "unresolved GL entry points: " + missing;
    return false;
#endif
}

}  // namespace platform
}  // namespace ps2r
