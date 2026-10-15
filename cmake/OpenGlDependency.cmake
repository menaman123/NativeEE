# cmake/OpenGlDependency.cmake -- the OpenGL link the GL backend, the SDL
# context and the GUI name as OpenGL::GL, ONE way for every host.
#
#   macOS   the OpenGL.framework (<OpenGL/gl3.h>, GL 4.1 core)
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
