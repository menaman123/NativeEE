// engine/platform/windows/gl_loader.cpp -- the Windows GL entry-point table behind gl_loader.h (GENERATED header).
//
// Every pointer starts null; GlContext::Create (engine/platform/sdl/gl_context.cpp) calls Load() with
// SDL_GL_GetProcAddress right after the context is current -- SDL resolves through wglGetProcAddress and falls back
// to opengl32.dll's exports for the GL 1.1 names, which is exactly the Windows rule. A GL call before Load() is a null
// call: the backend never issues one (GlDevice is constructed after the context), and the tests go through GlContext.
#include "engine/platform/windows/gl_loader.h"

#include <cstring>

extern "C" {
#define PS2R_GLWIN_DEFINE(type, name) type ps2r_glwin_##name = nullptr;
PS2R_GLWIN_FUNCS(PS2R_GLWIN_DEFINE)
PS2R_GLWIN_OPTIONAL_FUNCS(PS2R_GLWIN_DEFINE)
#undef PS2R_GLWIN_DEFINE
}

namespace ps2r {
namespace glwin {

namespace {
struct Entry {
    const char* name;
    void* slot;   // the address of the function-pointer variable; written with memcpy (a function pointer is one void* wide here)
};
// #name stringifies the unexpanded argument (the GL name, not the macro's replacement); the slot is the pointer.
#define PS2R_GLWIN_ENTRY(type, name) {#name, static_cast<void*>(&ps2r_glwin_##name)},
const Entry kTable[] = {PS2R_GLWIN_FUNCS(PS2R_GLWIN_ENTRY)};
const Entry kOptional[] = {PS2R_GLWIN_OPTIONAL_FUNCS(PS2R_GLWIN_ENTRY) {nullptr, nullptr}};
#undef PS2R_GLWIN_ENTRY
}  // namespace

int Count() { return int(sizeof(kTable) / sizeof(kTable[0])); }

int Load(void* (*getproc)(const char*), const char** missing, int max_missing) {
    int n_missing = 0;
    for (const Entry& e : kTable) {
        void* p = getproc ? getproc(e.name) : nullptr;
        std::memcpy(e.slot, &p, sizeof(p));
        if (!p) {
            if (missing && n_missing < max_missing) missing[n_missing] = e.name;
            n_missing++;
        }
    }
    for (const Entry& e : kOptional) {   // the driver's when it has them, null when not
        if (!e.name) continue;
        void* p = getproc ? getproc(e.name) : nullptr;
        std::memcpy(e.slot, &p, sizeof(p));
    }
    return n_missing;
}

}  // namespace glwin
}  // namespace ps2r
