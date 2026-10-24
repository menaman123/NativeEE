#!/usr/bin/env bash
# tools/ci/win_cross_smoke.sh -- compile the engine's Windows-relevant TUs for x86_64-pc-windows-msvc on a macOS/Linux dev
# host, BEFORE the Windows lane sees them (the whole-tree twin over compile_commands.json is tools/build/win_cross_smoke.py).
# [TwinsanityEvolved/tools/ci/win_cross_smoke.sh (234718f), its TU list and include paths replaced by this tree's; the
#  linked PE32+ is test_host_platform.exe (the host arm's run on a Windows machine) where Twinsanity links te_fiber_test.exe.]
#
#   tools/ci/win_cross_smoke.sh --xwin <xwin splat root> [--clang <clang++>] [--gl-include DIR] [--sdl-include DIR]
#                               [--lld-link <lld-link>] [--out DIR] [--objects]
#
# What it does: the Windows arm (engine/platform/host/host_windows.cpp, engine/platform/host/platform.cpp,
# engine/platform/windows/*.cpp -- the GENERATED GL loader among them), the SDL window / context TUs
# (engine/platform/sdl/*.cpp), the GL backend (engine/render/backends/gl/*.cpp) and the platform tests
# (tests/platform/*.cpp) are re-issued with `--target=x86_64-pc-windows-msvc`, the MSVC CRT + Windows SDK headers from an
# `xwin splat` (the headers clang-cl sees on the runner), the tree's include path and the Windows lane's UCRT definitions
# (CMakeLists.txt's if(WIN32) list), -Wall -Wextra. The GL TUs need Khronos' <GL/glcorearb.h> (vcpkg port opengl-registry
# on the runner): --gl-include names a directory holding GL/ + KHR/ (default $PS2R_GLCOREARB_INCLUDE_DIR); without it they
# are listed as SKIP. The SDL TUs need the SDL2 VC development headers: --sdl-include (default $PS2R_WIN_SDL2_INCLUDE);
# without it SKIP. Syntax-only by default (`--objects` writes COFF objects under --out, and links test_host_platform.exe
# into a PE32+ when an lld-link is given or on PATH -- not run here). Exit 0 iff every TU compiled. The record is
# <out>/SUMMARY.tsv.
#
# What it does NOT do: run anything, or compile the FFmpeg-including TU (engine/ipu/hle/ffmpeg_video_decoder.cpp: the
# Windows lane's FFmpeg headers are vcpkg's, not the dev host's) -- tools/build/win_cross_smoke.py --all covers it with
# --ffmpeg-include.
set -u
XWIN="${PS2R_XWIN_ROOT:-}"; CLANG="${CLANG:-/opt/homebrew/opt/llvm/bin/clang++}"; OUT="/tmp/ps2r_win_cross_smoke"; OBJECTS=0
GL_INCLUDE="${PS2R_GLCOREARB_INCLUDE_DIR:-}"; SDL_INCLUDE="${PS2R_WIN_SDL2_INCLUDE:-}"; LLD_LINK="${PS2R_WIN_LLD_LINK:-}"
while [ $# -gt 0 ]; do
    case "$1" in
        --xwin) XWIN="$2"; shift 2 ;;
        --clang) CLANG="$2"; shift 2 ;;
        --gl-include) GL_INCLUDE="$2"; shift 2 ;;
        --sdl-include) SDL_INCLUDE="$2"; shift 2 ;;
        --lld-link) LLD_LINK="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --objects) OBJECTS=1; shift ;;
        *) echo "win_cross_smoke: unknown argument $1" >&2; exit 2 ;;
    esac
done
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
[ -n "$XWIN" ] && [ -d "$XWIN/crt/include" ] && [ -d "$XWIN/sdk/include/um" ] || { echo "win_cross_smoke: --xwin must be an xwin splat root (crt/, sdk/)" >&2; exit 2; }
[ -x "$CLANG" ] || { echo "win_cross_smoke: clang++ not found at $CLANG (Homebrew LLVM or Apple clang)" >&2; exit 2; }
mkdir -p "$OUT"
CC_C="${CLANG%++}"   # clang++ -> clang
[ -x "$CC_C" ] || CC_C="$CLANG"
[ -z "$LLD_LINK" ] && command -v lld-link >/dev/null 2>&1 && LLD_LINK="$(command -v lld-link)"

# The root CMakeLists.txt's `if(WIN32) add_compile_definitions(...)` list + the GNU driver's exceptions model on the
# MSVC target (clang-cl's defaults are /EHsc /GR; the GNU driver defaults off).
COMMON=(--target=x86_64-pc-windows-msvc -fms-compatibility-version=19.37 -fsyntax-only -Wall -Wextra -fexceptions -fcxx-exceptions
        -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_WARNINGS -D_USE_MATH_DEFINES -DNOMINMAX -DWIN32_LEAN_AND_MEAN
        -DSDL_MAIN_HANDLED -D__PRFCHWINTRIN_H -DPS2R_HAS_METAL=0
        -I"$HERE"
        -isystem "$XWIN/crt/include" -isystem "$XWIN/sdk/include/ucrt" -isystem "$XWIN/sdk/include/um" -isystem "$XWIN/sdk/include/shared")
[ -n "$GL_INCLUDE" ] && COMMON+=(-isystem "$GL_INCLUDE")
[ -n "$SDL_INCLUDE" ] && COMMON+=(-isystem "$SDL_INCLUDE")
if [ "$OBJECTS" = 1 ]; then COMMON=("${COMMON[@]/-fsyntax-only/-c}"); fi

TUS=()
for f in "$HERE"/engine/platform/host/host_windows.cpp "$HERE"/engine/platform/host/platform.cpp "$HERE"/engine/platform/windows/*.cpp \
         "$HERE"/engine/platform/sdl/*.cpp "$HERE"/engine/render/backends/gl/*.cpp "$HERE"/tests/platform/*.cpp; do
    [ -f "$f" ] && TUS+=("$f")
done
printf 'tu\tstatus\terrors\tseconds\n' > "$OUT/SUMMARY.tsv"
fail=0; ok=0; skip=0
for f in "${TUS[@]}"; do
    rel="${f#$HERE/}"
    needs_gl=0; needs_sdl=0
    case "$rel" in engine/platform/windows/gl_loader.cpp|engine/render/backends/gl/*|engine/platform/sdl/gl_context.cpp|tests/platform/test_gl_*) needs_gl=1 ;; esac
    case "$rel" in engine/platform/sdl/*|tests/platform/test_sdl_*|tests/platform/test_gl_*) needs_sdl=1 ;; esac
    grep -q "PS2R_GL_HEADER\|<GL/glcorearb.h>" "$f" 2>/dev/null && needs_gl=1
    grep -q "<SDL.h>\|<SDL2/SDL.h>\|\"SDL.h\"" "$f" 2>/dev/null && needs_sdl=1
    if [ $needs_gl = 1 ] && [ -z "$GL_INCLUDE" ]; then printf '%s\tSKIP\t0\t0\n' "$rel" >> "$OUT/SUMMARY.tsv"; echo "  SKIP          $rel (GL/glcorearb.h is the runner's vcpkg opengl-registry; --gl-include)"; skip=$((skip+1)); continue; fi
    if [ $needs_sdl = 1 ] && [ -z "$SDL_INCLUDE" ]; then printf '%s\tSKIP\t0\t0\n' "$rel" >> "$OUT/SUMMARY.tsv"; echo "  SKIP          $rel (SDL headers are the runner's vcpkg; --sdl-include)"; skip=$((skip+1)); continue; fi
    log="$OUT/$(echo "$rel" | tr '/' '_').log"
    t0=$(date +%s)
    case "$f" in
        *.c)   cmd=("$CC_C" -std=c17 "${COMMON[@]}") ;;   # clang-cl's C default on the lane
        *.cpp) cmd=("$CLANG" -std=c++17 "${COMMON[@]}") ;;
    esac
    if [ "$OBJECTS" = 1 ]; then cmd+=(-o "$OUT/$(echo "$rel" | tr '/' '_').obj"); fi
    "${cmd[@]}" "$f" > "$log" 2>&1
    rc=$?
    dt=$(( $(date +%s) - t0 ))
    errs=$(grep -c "error:" "$log" || true)
    if [ $rc -eq 0 ]; then ok=$((ok+1)); printf '%s\tOK\t%s\t%s\n' "$rel" "$errs" "$dt" >> "$OUT/SUMMARY.tsv"; echo "  OK   ${dt}s     $rel"
    else fail=$((fail+1)); printf '%s\tFAIL\t%s\t%s\n' "$rel" "$errs" "$dt" >> "$OUT/SUMMARY.tsv"; echo "  FAIL ${dt}s errors=$errs  $rel"; grep "error:" "$log" | head -3 | sed 's/^/        /'; fi
done
if [ "$OBJECTS" = 1 ] && [ -n "$LLD_LINK" ]; then
    # test_host_platform.exe as a PE32+: every Win32 import of the host arm (dbghelp, psapi, winmm, kernel32) resolves
    # against the SDK import libraries; the Windows machine runs it first (PlatformHostShims on the Windows arm).
    if $LLD_LINK /nologo /subsystem:console /out:"$OUT/test_host_platform.exe" \
        "/libpath:$XWIN/crt/lib/x86_64" "/libpath:$XWIN/sdk/lib/um/x86_64" "/libpath:$XWIN/sdk/lib/ucrt/x86_64" \
        "$OUT/tests_platform_test_host_platform.cpp.obj" "$OUT/engine_platform_host_host_windows.cpp.obj" \
        ${GL_INCLUDE:+"$OUT/engine_platform_windows_gl_loader.cpp.obj"} \
        kernel32.lib dbghelp.lib psapi.lib winmm.lib ${GL_INCLUDE:+opengl32.lib} libcmt.lib libucrt.lib libvcruntime.lib libcpmt.lib > "$OUT/link_test_host_platform.log" 2>&1
    then echo "  OK   link   test_host_platform.exe (PE32+, not run here)"; else echo "  FAIL link   test_host_platform.exe"; head -5 "$OUT/link_test_host_platform.log" | sed 's/^/        /'; fail=$((fail+1)); fi
fi
echo "win_cross_smoke: ok=$ok fail=$fail skip=$skip -> $OUT/SUMMARY.tsv"
[ $fail -eq 0 ]
