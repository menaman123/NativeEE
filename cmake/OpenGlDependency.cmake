# cmake/OpenGlDependency.cmake -- the OpenGL link the GL backend, the SDL
# context and the GUI name as OpenGL::GL, ONE way for every host.
#
#   macOS   the OpenGL.framework (<OpenGL/gl3.h>, GL 4.1 core)
#   Windows opengl32.lib (FindOpenGL) exports GL 1.1 only: the engine's GL TUs include
#           the generated loader engine/platform/windows/gl_loader.h (PS2R_GL_HEADER
#           there), whose pointers engine/platform/windows/gl_loader.cpp (part of
#           ps2r_host on Windows) fills through SDL_GL_GetProcAddress. The header
#           needs Khronos' <GL/glcorearb.h> + <KHR/khrplatform.h>: the vcpkg port
#           `opengl-registry` (vcpkg.json), found below and put on OpenGL::GL's
#           include path so every GL TU sees it.
#   Linux   Mesa / libglvnd through FindOpenGL's GLVND preference (CMP0072 NEW):
#           OpenGL::GL -> libOpenGL.so + libGLX.so, which export every core GL
#           entry point, so the backend calls them directly with
#           GL_GLEXT_PROTOTYPES (engine/platform/host/platform.h) -- no loader
#           library. A tree without GLVND falls back to the legacy libGL.so,
#           which exports the same set. The shipped artifact (the linux-dist
#           preset) sets OpenGL_GL_PREFERENCE=LEGACY on purpose: libGL.so.1 is
#           on every Mesa/vendor install, while libOpenGL.so.0 is Ubuntu's
#           separate libopengl0 package (a fresh ubuntu:24.04 + libgl1 could
#           not load the GLVND-linked binary).
#
# The GL header spelling is NOT here: every TU includes PS2R_GL_HEADER from the
# ONE host header. Included from each directory that links OpenGL::GL.
if(POLICY CMP0072)
    cmake_policy(SET CMP0072 NEW)
endif()
find_package(OpenGL REQUIRED)
if(WIN32)
    find_path(PS2R_GLCOREARB_INCLUDE_DIR NAMES GL/glcorearb.h
              DOC "directory holding GL/glcorearb.h and KHR/khrplatform.h (vcpkg port opengl-registry)")
    if(NOT PS2R_GLCOREARB_INCLUDE_DIR)
        message(FATAL_ERROR "nativeee: GL/glcorearb.h not found -- the Windows GL loader needs the Khronos registry headers: "
                            "configure through the vcpkg toolchain with vcpkg.json's `opengl-registry` port installed, "
                            "or point PS2R_GLCOREARB_INCLUDE_DIR at a directory with GL/glcorearb.h + KHR/khrplatform.h")
    endif()
    set_property(TARGET OpenGL::GL APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${PS2R_GLCOREARB_INCLUDE_DIR}")
endif()
