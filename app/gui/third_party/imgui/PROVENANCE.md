# Dear ImGui

Unmodified selected files from the official upstream `ocornut/imgui`.

- Release: `v1.91.9b`
- Commit: `f5befd2d29e66809cd1110a152e375a7f1981f06`
- Upstream: https://github.com/ocornut/imgui
- Source archive: https://codeload.github.com/ocornut/imgui/tar.gz/f5befd2d29e66809cd1110a152e375a7f1981f06
- Archive SHA-256: `85f4ce357df05bcc331b587f01976f47fb55f19fadf477a7907289686bc3f4c8`
- Obtained: 2026-10-07
- License: MIT, retained verbatim in `LICENSE.txt`; bundled stb notices
  retained in each `imstb_*.h`.
- Selected: the four core compilation units, public/internal/config and stb
  headers, SDL2 and OpenGL3 backend pairs and the OpenGL3 loader header.
  No examples, demos, other backends, or code from OLD/OpenGOAL are included.
- Changes to upstream sources: none. `SOURCE_SHA256.txt` fingerprints every
  copied upstream file.

Primary integration references, read at the pinned commit:

- https://github.com/ocornut/imgui/blob/f5befd2d29e66809cd1110a152e375a7f1981f06/docs/BACKENDS.md
- https://github.com/ocornut/imgui/blob/f5befd2d29e66809cd1110a152e375a7f1981f06/docs/FAQ.md
- https://github.com/ocornut/imgui/blob/f5befd2d29e66809cd1110a152e375a7f1981f06/backends/imgui_impl_sdl2.cpp
- https://github.com/ocornut/imgui/blob/f5befd2d29e66809cd1110a152e375a7f1981f06/backends/imgui_impl_opengl3.cpp

The application serializes all calls into its sole ImGui context. SDL
initialization/events/NewFrame/shutdown stay on the process main thread.
Rendering uses the current GL owner and never invokes the SDL backend.
The overlay has no platform viewports, settings-file IO or game callbacks.
