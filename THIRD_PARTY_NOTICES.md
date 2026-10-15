# NativeEE — third-party notices

NativeEE's own code is MIT licensed (`LICENSE`). This file names every third-party component in the
repository and in the shipped artifact, its licence, and how it is used. The artifact folder
(`dist/<platform>/`, staged by `build.sh` / `build.ps1`) carries its own generated
`THIRD_PARTY_NOTICES.md` — the subset below that is actually linked into that binary, with the exact
versions — and the licence texts in `licenses/` (from `tools/dist/licenses/`, pinned by
`tests/lint/lint_artifact_licenses.py`).

NativeEE contains **no game data**: no textures, audio, models, movies, disc bytes, and no code
derived from the game. *the title* is the property of its rights holders; you must own the
game and supply your own disc image (`README.md`).

## Compiled into the shipped binary

| Component | Version | Licence | Where / how |
|---|---|---|---|
| SDL2 | `release-2.30.9` (`cmake/Sdl2Dependency.cmake`) | zlib — `tools/dist/licenses/SDL2-LICENSE.txt` | built from source and linked statically by the `*-dist` presets; the system package (Homebrew / distribution / vcpkg DLL) in the other presets |
| FFmpeg — libavcodec, libavutil, libswscale | `n6.1.2` (`tools/ci/ffmpeg_static.sh`) | LGPL v2.1 or later — `tools/dist/licenses/FFmpeg-COPYING.LGPLv2.1.txt`, per-component terms in `FFmpeg-LICENSE.md` | the MPEG-1/2 movie decoder. Built with `--disable-gpl --disable-nonfree --disable-version3` and only the mpeg1/mpeg2 decoders, the mpegvideo parser and colour conversion; linked statically on macOS / Linux (`linux-dist`, `macos-dist`), bundled as DLLs on Windows. LGPL §6: this repository is the complete source of the program and the recipe that rebuilds the library; `./build.sh` relinks against a modified FFmpeg |
| Dear ImGui | `v1.91.9b` (`app/gui/third_party/imgui/PROVENANCE.md`) | MIT — `app/gui/third_party/imgui/LICENSE.txt` | the first-run launcher / settings screen and the F1 developer overlay. Its `imstb_*.h` headers carry their own public-domain / MIT notices |
| gl3w-derived OpenGL loader (`imgui_impl_opengl3_loader.h`) | part of the ImGui copy | public domain (Unlicense-style) — `tools/dist/licenses/imgl3w-UNLICENSE.txt` | the only loader code in the binary; the engine itself calls the platform's GL directly |
| GCC libstdc++ / libgcc (Linux artifact only) | the toolchain's | GPL v3 with the GCC Runtime Library Exception 3.1 — `tools/dist/licenses/GCC-COPYING3.txt`, `GCC-RUNTIME-LIBRARY-EXCEPTION.txt` | linked statically by the `linux-dist` preset so the folder runs without matching distribution packages |

## Build-time tools (nothing of them is in the shipped binary)

| Component | Licence | Where / how |
|---|---|---|
| rabbitizer (MIPS disassembler) | MIT — `third_party/rabbitizer/LICENSE` | `tools/recompiler_v2` and `tools/iop_recompiler` decode the game's instructions with it at build time |
| ELFIO | MIT — `third_party/elfio/LICENSE.txt` | the recompilers read the ELF / IRX with it at build time |
| JSON for Modern C++ (nlohmann/json, `tools/recompiler_v2/include/json.hpp`) | MIT — the notice at the top of the header | the recompiler's symbol / stub inputs |

## Not bundled

- macOS system libraries (OpenGL.framework, libc++, libSystem) and the Linux run-time libraries
  (glibc, `libGL.so.1` through libglvnd / Mesa or the vendor driver, the X11 / Wayland / PulseAudio /
  ALSA back ends SDL2 loads) are the user's own.
- Reference material consulted as a specification, never copied: the `the title-reversed`
  decompilation project (PAL symbol names; `docs/design/HLE_COVERAGE.md`) and the OpenGOAL
  `jak-project` architecture (`docs/design/REFERENCE_ARCHITECTURES.md`).
