# tools/ci/linux.Dockerfile -- the Linux lane of Charter 2 Track B (B1/B3):
# build the tree with the linux-release preset and run the component suite
# (ctest; the Root legs SKIP 77 because the game files are NEVER in CI).
#
#   docker build -f tools/ci/linux.Dockerfile -t nativeee-linux .                 # GCC lane
#   docker build -f tools/ci/linux.Dockerfile --build-arg PRESET=linux-release-clang -t nativeee-linux-clang .
#   docker build -f tools/ci/linux.Dockerfile --platform linux/amd64 -t nativeee-linux-amd64 .
#   docker build -f tools/ci/linux.Dockerfile --target toolchain -t nativeee-linux-toolchain .  # deps only
#
# Stage `toolchain`: Ubuntu 24.04 + GCC 13 / Clang 18, CMake, Ninja, Python 3,
# SDL2 (libsdl2-dev -- the found path of cmake/Sdl2Dependency.cmake), Mesa GL +
# libglvnd (OpenGL::GL exports the core entry points the backend calls through
# GL_GLEXT_PROTOTYPES), FFmpeg (ps2r_ipu's libavcodec/avutil/swscale), Xvfb
# (a GLX display for the GL tests: llvmpipe, GL 4.5 core). Also the bind-mount
# driver's image: `docker run -v <tree>:/src nativeee-linux-toolchain tools/ci/linux_build.sh`.
# Stage `build`: COPY the tree, configure + build the preset, run ctest.
#
# Memory: a 4 MB recompiled shard at -O2 takes ~1.5-2.5 GB in GCC; JOBS=3 fits an
# 8 GB runner (the GitHub ubuntu runners and Docker Desktop's default).
ARG UBUNTU=24.04
FROM ubuntu:${UBUNTU} AS toolchain
ENV DEBIAN_FRONTEND=noninteractive
# The package list is tools/ci/linux_packages.txt (shared with .github/workflows/ci.yml).
COPY tools/ci/linux_packages.txt /tmp/linux_packages.txt
RUN apt-get update && apt-get install -y --no-install-recommends \
        $(grep -v '^#' /tmp/linux_packages.txt | xargs) \
    && rm -rf /var/lib/apt/lists/* \
    && gcc --version | head -1 && clang --version | head -1 && cmake --version | head -1
# The GL tests open an SDL window: a virtual X display with Mesa's llvmpipe.
ENV SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 PS2R_CI=1
WORKDIR /src
COPY tools/ci/linux_build.sh /usr/local/bin/ps2r-linux-build
RUN chmod +x /usr/local/bin/ps2r-linux-build

FROM toolchain AS build
ARG PRESET=linux-release-gcc
ARG JOBS=3
COPY . /src
RUN ps2r-linux-build "${PRESET}" "${JOBS}"
