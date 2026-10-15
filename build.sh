#!/usr/bin/env bash
# build.sh -- the one documented command for macOS and Linux (Track C3):
#
#   ./build.sh --disc IMAGE    configure + build the Release preset of this host, run the
#                              test suite, stage the artifact under dist/<platform>/. IMAGE is
#                              a disc image (.iso / raw .bin) of YOUR copy of the game: the build
#                              extracts the title's ELF and IOP module from it and recompiles
#                              them (the recompiled code is derived from the game and is not in
#                              this tree). Also: NATIVEEE_DISC=IMAGE in the environment.
#   ./build.sh --elf SLUS_123.45 [--irx STREAM.IRX]
#                              the same from loose files (NATIVEEE_TITLE_ELF / NATIVEEE_STREAM_IRX);
#                              without the IRX the oracle binary and the IOP tests are skipped
#   ./build.sh --no-dist       stop after the build; WITHOUT a disc/ELF this builds the engine,
#                              the recompilers and the component tests only (the title is skipped)
#   ./build.sh --no-tests      skip ctest (and build only the artifact's binary)
#   ./build.sh --no-dist       stop after the build
#   ./build.sh --dist-only     only (re)stage dist/<platform>/ from an existing build
#   ./build.sh --preset NAME   use another configure/build/test preset (default: the host's
#                              *-release preset; CMakePresets.json)
#   ./build.sh --jobs N        parallel compile jobs (default: the preset's)
#
# The artifact (docs: README.md "Users") is one self-contained folder, <platform> = macos
# or linux-<arch> (x86_64 / aarch64):
#   dist/<platform>/nativeee            the binary = recompiled code + native engine only
#   dist/<platform>/lib/                 ABSENT for the shipped presets: macos-dist and linux-dist
#                                        link SDL2 (built from the pinned release) and FFmpeg
#                                        (tools/ci/ffmpeg_static.sh: mpeg1/mpeg2 decoders +
#                                        swscale, --disable-gpl) statically; Linux also
#                                        libstdc++/libgcc. Only the system's GL (OpenGL.framework
#                                        / libGL.so.1), libc++ or glibc, and the dlopen'd
#                                        X11/Wayland/audio back ends come from the user's machine
#                                        -- no package to install. Another preset (e.g.
#                                        macos-release with Homebrew's libraries) bundles its
#                                        dynamic closure here instead; a GPL-built FFmpeg
#                                        (Homebrew's --enable-gpl) is REFUSED.
#   dist/<platform>/title/               the title's own tables the binary reads at start
#                                        (hle/sdk_bindings.tsv, data/post_load_patches.txt,
#                                        verify/restore_mhash_expected.tsv -- addresses and
#                                        hashes, no game bytes; NOT the RAM data map, which is
#                                        a subset of the ELF and is skipped in this layout)
#   dist/<platform>/BUILD-ID             the build id the binary prints with --version
#   dist/<platform>/README.md            the user half of the repo README
#   dist/<platform>/LICENSE              the project licence (MIT; the repository root LICENSE)
#   dist/<platform>/THIRD_PARTY_NOTICES.md  the bundled libraries' notices (what is linked, how, under what)
#   dist/<platform>/licenses/            the bundled libraries' licence texts, from the committed
#                                        tools/dist/licenses/ (SDL2 zlib; FFmpeg LGPL v2.1 + LICENSE.md
#                                        + the generated FFmpeg-BUILD-CONFIGURATION.txt: the configure
#                                        line libavcodec EMBEDS in the linked binary, refused on any GPL /
#                                        non-free / version3 flag or if it differs from the recipe; Dear ImGui MIT + its gl3w loader's public-domain
#                                        dedication; Linux: GPLv3 + the GCC Runtime Library Exception
#                                        for the static libstdc++/libgcc). tests/lint/
#                                        lint_artifact_licenses.py checks the staged folder.
#   dist/nativeee-<platform>.tar.gz     Linux: the one-file download of that folder
# Game data is NEVER part of it: tools/ci/asset_scan.py (names, format magic, byte windows
# of the title's ELF inside every file) runs on the staged folder and on the archive and
# the script refuses to finish on any finding. The GL loader is the platform's own
# (OpenGL.framework on macOS, libGL.so.1 via libglvnd on Linux): nothing to bundle; the only
# loader code in the binary is Dear ImGui's embedded gl3w header (public domain).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

PRESET=""
JOBS=""
RUN_TESTS=1
RUN_DIST=1
DIST_ONLY=0
DISC="${NATIVEEE_DISC:-}"
ELF="${NATIVEEE_TITLE_ELF:-}"
IRX="${NATIVEEE_STREAM_IRX:-}"
while [ $# -gt 0 ]; do
  case "$1" in
    --disc) DISC="$2"; shift 2 ;;
    --elf) ELF="$2"; shift 2 ;;
    --irx) IRX="$2"; shift 2 ;;
    --preset) PRESET="$2"; shift 2 ;;
    --jobs) JOBS="$2"; shift 2 ;;
    --no-tests) RUN_TESTS=0; shift ;;
    --no-dist) RUN_DIST=0; shift ;;
    --dist-only) DIST_ONLY=1; RUN_TESTS=0; shift ;;
    -h|--help) sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "build.sh: unknown argument $1 (try --help)" >&2; exit 2 ;;
  esac
done

case "$(uname -s)" in
  Darwin) PLATFORM=macos; DEFAULT_PRESET=macos-dist ;;
  Linux)  PLATFORM="linux-$(uname -m)"; DEFAULT_PRESET=linux-dist ;;
  *) echo "build.sh: unsupported host $(uname -s) (Windows: build.ps1)" >&2; exit 2 ;;
esac
[ -n "$PRESET" ] || PRESET="$DEFAULT_PRESET"

# The preset's binaryDir, read back from CMake so this script never hard-codes it.
preset_binary_dir() {
  python3 - "$ROOT/CMakePresets.json" "$1" "$ROOT" <<'EOF'
import json, sys
presets, name, root = json.load(open(sys.argv[1])), sys.argv[2], sys.argv[3]
by_name = {p["name"]: p for p in presets["configurePresets"]}
p = by_name.get(name)
if p is None:
    sys.exit("build.sh: no configure preset named %r in CMakePresets.json" % name)
while "binaryDir" not in p and "inherits" in p:
    p = by_name[p["inherits"] if isinstance(p["inherits"], str) else p["inherits"][0]]
print(p["binaryDir"].replace("${sourceDir}", root))
EOF
}
BUILD_DIR="$(preset_binary_dir "$PRESET")"
DIST="$ROOT/dist/$PLATFORM"

step() { printf '\n== %s\n' "$*"; }

# --- the title's inputs (cmake/TitleInputs.cmake) ------------------------------------------
# The artifact IS the title binary, and the title's recompiled code is generated from the game's
# own files: staging dist/ needs a disc image or the ELF. Without one, only --no-dist makes sense
# (engine + recompilers + component tests), and the script says so instead of failing late.
TITLE_ARGS=()
for spec in "DISC:NATIVEEE_DISC:$DISC" "ELF:NATIVEEE_TITLE_ELF:$ELF" "IRX:NATIVEEE_STREAM_IRX:$IRX"; do
  kind="${spec%%:*}"; rest="${spec#*:}"; var="${rest%%:*}"; val="${rest#*:}"
  [ -n "$val" ] || continue
  [ -f "$val" ] || { echo "build.sh: --$(printf '%s' "$kind" | tr '[:upper:]' '[:lower:]') $val: no such file" >&2; exit 2; }
  case "$val" in /*) ;; *) val="$PWD/$val" ;; esac
  TITLE_ARGS+=("-D$var=$val")
done
if [ "$DIST_ONLY" = "0" ] && [ "$RUN_DIST" = "1" ] && [ -z "$DISC" ] && [ -z "$ELF" ]; then
  cat >&2 <<'EOF_NOINPUT'
build.sh: no disc image / title ELF given -- the dist artifact is the title binary (nativeee), whose
  recompiled code is generated from YOUR copy of the game and is not part of this tree.
    ./build.sh --disc "/path/to/the title.iso"     (a .iso or raw .bin of the NTSC-U disc, SLUS-12345)
    ./build.sh --elf /path/to/SLUS_123.45 [--irx /path/to/STREAM.IRX]
  or set NATIVEEE_DISC / NATIVEEE_TITLE_ELF in the environment. To build only the engine, the
  recompilers and the component tests (no title, no artifact): ./build.sh --no-dist
EOF_NOINPUT
  exit 2
fi

if [ "$DIST_ONLY" = "0" ]; then
  if [ "$PRESET" = "linux-dist" ] || [ "$PRESET" = "macos-dist" ]; then
    # The static FFmpeg the preset's CMAKE_PREFIX_PATH names (cached after the first run).
    step "ffmpeg     (tools/ci/ffmpeg_static.sh -> $BUILD_DIR/third_party/ffmpeg)"
    "$ROOT/tools/ci/ffmpeg_static.sh" "$BUILD_DIR/third_party/ffmpeg"
  fi
  step "configure  (cmake --preset $PRESET${TITLE_ARGS[*]+ ${TITLE_ARGS[*]}})"
  cmake --preset "$PRESET" ${TITLE_ARGS[@]+"${TITLE_ARGS[@]}"}

  # Without the test run only the artifact's binary is built (the test executables are
  # two thirds of the target set; the CI artifact job and a user's build take this path).
  TARGET_ARGS=()
  [ "$RUN_TESTS" = "1" ] || TARGET_ARGS=(--target nativeee)
  # ${arr[@]+"${arr[@]}"}: an EMPTY array is "unbound" to macOS's bash 3.2 under set -u.
  step "build      (cmake --build --preset $PRESET${JOBS:+ -j$JOBS}${TARGET_ARGS[*]+ ${TARGET_ARGS[*]}})"
  # Acceptance shape: 0 FAILED, 0 compiler warnings, 0 linker warnings (the repo rule).
  BUILD_LOG="$BUILD_DIR/build.sh.log"
  if ! cmake --build --preset "$PRESET" ${JOBS:+-j"$JOBS"} ${TARGET_ARGS[@]+"${TARGET_ARGS[@]}"} 2>&1 | tee "$BUILD_LOG"; then
    echo "build.sh: BUILD FAILED (log: $BUILD_LOG)" >&2; exit 1
  fi
  N_WARN="$(grep -c 'warning:' "$BUILD_LOG" || true)"
  N_LDWARN="$(grep -c 'ld: warning' "$BUILD_LOG" || true)"
  echo "build.sh: build OK -- warning: $N_WARN, ld: warning $N_LDWARN"

  if [ "$RUN_TESTS" = "1" ]; then
    step "tests      (component + title suites; the game-file groups Root*/Og* are excluded)"
    # The GL tests open a window: on a Linux host without a display, a virtual one (Xvfb).
    XVFB=()
    if [ "$PLATFORM" != "macos" ] && [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
      XVFB=(xvfb-run -a -s "-screen 0 1280x1024x24")
    fi
    ${XVFB[@]+"${XVFB[@]}"} ctest --preset "$PRESET" -E '^Root|^Og|Oracle' --timeout 900 -j4 || {
      echo "build.sh: TESTS FAILED (ctest --preset $PRESET --rerun-failed --output-on-failure)" >&2; exit 1; }
  fi
fi

[ "$RUN_DIST" = "1" ] || exit 0

BIN="$BUILD_DIR/app/host/nativeee"
[ -x "$BIN" ] || { echo "build.sh: no binary at $BIN (build first)" >&2; exit 1; }

step "stage      ($DIST)"
rm -rf "$DIST"
mkdir -p "$DIST/lib"
cp "$BIN" "$DIST/nativeee"
"$BIN" --version > "$DIST/BUILD-ID"
cat "$DIST/BUILD-ID"

# --- the title tables the binary reads beside itself when the source tree is absent ---
# No serial is hardcoded: PS2R_TITLE if set, else the one title directory in games/ (one with
# an hle/ subdirectory). With no title there are no tables to stage.
if [ -n "${PS2R_TITLE:-}" ]; then
  TITLE_SRC="$ROOT/games/$PS2R_TITLE"
else
  TITLE_SRC=""
  for cand in "$ROOT"/games/*/; do
    [ -d "${cand}hle" ] || continue
    if [ -n "$TITLE_SRC" ]; then
      echo "build.sh: several titles in games/; set PS2R_TITLE to choose one" >&2
      exit 1
    fi
    TITLE_SRC="${cand%/}"
  done
fi
if [ -n "$TITLE_SRC" ]; then
  for rel in hle/sdk_bindings.tsv data/post_load_patches.txt verify/restore_mhash_expected.tsv; do
    [ -f "$TITLE_SRC/$rel" ] || continue
    mkdir -p "$DIST/title/$(dirname "$rel")"
    cp "$TITLE_SRC/$rel" "$DIST/title/$rel"
  done
else
  echo "build.sh: no title in games/ -- dist/ carries no title tables"
fi

# --- the user half of README.md: from the top to the "## Developers" heading ---
awk '/^## Developers/ {exit} {print}' "$ROOT/README.md" > "$DIST/README.md"
cp "$ROOT/LICENSE" "$DIST/LICENSE"   # MIT (the repository root LICENSE)

# --- bundled runtime libraries: the non-system dynamic closure of the binary ---
is_system_lib() {
  case "$1" in
    /usr/lib/*|/System/Library/*|/lib/*|/lib64/*|/usr/lib64/*) return 0 ;;
    */libc.so*|*/libm.so*|*/libpthread.so*|*/libdl.so*|*/librt.so*|*/libstdc++.so*|*/libgcc_s.so*|*/ld-linux*) return 0 ;;
    */libGL.so*|*/libGLX*|*/libEGL*|*/libX11*|*/libxcb*|*/libwayland*|*/libdrm*|*/libgbm*|*/libasound*|*/libpulse*) return 0 ;;
  esac
  return 1
}

bundle_macos() {
  local queue=("$DIST/nativeee") seen=""
  while [ ${#queue[@]} -gt 0 ]; do
    local f="${queue[0]}"; queue=("${queue[@]:1}")
    local dep
    while read -r dep; do
      [ -n "$dep" ] || continue
      is_system_lib "$dep" && continue
      case "$dep" in @*) continue ;; esac   # already relative (a bundled lib's own refs)
      local name; name="$(basename "$dep")"
      if [ ! -f "$DIST/lib/$name" ]; then
        local src="$dep"
        [ -f "$src" ] || src="$(readlink -f "$dep" 2>/dev/null || echo "$dep")"
        if [ ! -f "$src" ]; then echo "build.sh: cannot find $dep to bundle" >&2; exit 1; fi
        cp -L "$src" "$DIST/lib/$name"; chmod u+w "$DIST/lib/$name"
        install_name_tool -id "@rpath/$name" "$DIST/lib/$name" 2>/dev/null
        queue+=("$DIST/lib/$name")
      fi
      install_name_tool -change "$dep" "@rpath/$name" "$f" 2>/dev/null
    done < <(otool -L "$f" | tail -n +2 | awk '{print $1}')
  done
  # The build's own rpaths (the package prefixes CMake recorded) would be searched first and
  # win over lib/: drop them, then the one rpath the artifact needs.
  local rp
  while read -r rp; do
    [ -n "$rp" ] && install_name_tool -delete_rpath "$rp" "$DIST/nativeee" 2>/dev/null || true
  done < <(otool -l "$DIST/nativeee" | awk '/LC_RPATH/ {grab=1} grab && /path / {print $2; grab=0}')
  install_name_tool -add_rpath "@executable_path/lib" "$DIST/nativeee"
  # Re-sign everything we rewrote (arm64 refuses to load a modified, unsigned image).
  for f in "$DIST"/lib/* "$DIST/nativeee"; do codesign --force --sign - "$f" >/dev/null 2>&1 || true; done
}

bundle_linux() {
  local queue=("$DIST/nativeee")
  while [ ${#queue[@]} -gt 0 ]; do
    local f="${queue[0]}"; queue=("${queue[@]:1}")
    local dep
    while read -r dep; do
      [ -n "$dep" ] && [ -f "$dep" ] || continue
      is_system_lib "$dep" && continue
      local name; name="$(basename "$dep")"
      if [ ! -f "$DIST/lib/$name" ]; then
        cp -L "$dep" "$DIST/lib/$name"; chmod u+w "$DIST/lib/$name"
        queue+=("$DIST/lib/$name")
      fi
    done < <(ldd "$f" | awk '/=>/ {print $3}')
  done
  # The linux-dist preset links everything static: lib/ stays empty and nothing needs an
  # rpath. Only a build that did bundle a library (another preset) needs patchelf.
  [ -n "$(ls -A "$DIST/lib" 2>/dev/null)" ] || return 0
  if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '$ORIGIN/lib' "$DIST/nativeee"
    for l in "$DIST"/lib/*; do patchelf --set-rpath '$ORIGIN' "$l" || true; done
  else
    echo "build.sh: patchelf not found -- $DIST/nativeee keeps its build rpath; run with LD_LIBRARY_PATH=$DIST/lib or install patchelf" >&2
  fi
}

case "$PLATFORM" in
  macos)   bundle_macos ;;
  linux-*) bundle_linux ;;
esac

# --- the bundled FFmpeg must be the LGPL build: a GPL-configured libavcodec (Homebrew's,
#     most distributions') would put the whole artifact under the GPL. The proof is read from WHAT
#     WAS LINKED -- the configuration string libavcodec embeds (avcodec_configuration(), the
#     configure command line as run) in the staged binary (static) or in the bundled libavcodec
#     (dynamic) -- never from the recipe or a build cache: a stale prefix or a foreign FFmpeg is
#     refused here even when tools/ci/ffmpeg_static.sh's list is clean (review lic3b 1a).
FFMPEG_GPL_RE='--enable-gpl|--enable-nonfree|--enable-libx26|--enable-version3'
ffmpeg_embedded_configuration() {   # $1 = an object file; prints the ONE --prefix= string it embeds
  local confs n
  confs="$(strings "$1" | grep -- '--prefix=' | sort -u || true)"
  n="$(printf '%s' "$confs" | grep -c . || true)"
  if [ "$n" -ne 1 ]; then
    echo "build.sh: REFUSING to ship -- $(basename "$1") embeds $n FFmpeg configuration strings (--prefix=), expected exactly 1:" >&2
    printf '%s\n' "$confs" | sed 's/^/  /' >&2
    return 1
  fi
  printf '%s' "$confs"
}
FFMPEG_LINK="static"
FFMPEG_DYN=""
for l in "$DIST"/lib/libavcodec*; do [ -f "$l" ] && FFMPEG_DYN="$l"; done
if [ -n "$FFMPEG_DYN" ]; then
  FFMPEG_LINK="dynamic"
  FFMPEG_CONF="$(ffmpeg_embedded_configuration "$FFMPEG_DYN")" || { rm -rf "$DIST"; exit 1; }
  FFMPEG_CONF_FROM="$(basename "$FFMPEG_DYN") (bundled in lib/)"
else
  FFMPEG_CONF="$(ffmpeg_embedded_configuration "$DIST/nativeee")" || { rm -rf "$DIST"; exit 1; }
  FFMPEG_CONF_FROM="nativeee (linked statically)"
fi
if printf '%s' "$FFMPEG_CONF" | grep -qE -- "$FFMPEG_GPL_RE"; then
  echo "build.sh: REFUSING to ship -- the FFmpeg linked into $FFMPEG_CONF_FROM is a GPL/non-free/LGPLv3 build:" >&2
  echo "  $FFMPEG_CONF" >&2
  echo "  use the ${PLATFORM%%-*}-dist preset (static FFmpeg from tools/ci/ffmpeg_static.sh, --disable-gpl)" >&2
  rm -rf "$DIST"; exit 1
fi
if [ "$FFMPEG_LINK" = "static" ]; then
  # Static: the embedded string must be THE recipe (tools/ci/ffmpeg_static.sh --print-args), argument
  # for argument, in order -- less --prefix=<path> and the host-only --disable-x86asm. Anything else
  # means the prefix the binary linked is not what the recipe describes.
  LINKED_ARGS="$(printf '%s\n' "$FFMPEG_CONF" | tr ' ' '\n' | grep -v -e '^--prefix=' -e '^--disable-x86asm$' -e '^$')"
  RECIPE_ARGS="$("$ROOT/tools/ci/ffmpeg_static.sh" --print-args)"
  if [ "$LINKED_ARGS" != "$RECIPE_ARGS" ]; then
    echo "build.sh: REFUSING to ship -- the FFmpeg linked into nativeee was not configured with the recipe (tools/ci/ffmpeg_static.sh):" >&2
    diff <(printf '%s\n' "$RECIPE_ARGS") <(printf '%s\n' "$LINKED_ARGS") | sed 's/^/  /' >&2 || true
    echo "  (< recipe, > linked). Rebuild the prefix: rm -rf $BUILD_DIR/third_party/ffmpeg && ./build.sh" >&2
    rm -rf "$DIST"; exit 1
  fi
  echo "ffmpeg: the linked libavcodec's configuration == the recipe ($(printf '%s\n' "$RECIPE_ARGS" | grep -c .) arguments; --disable-gpl --disable-nonfree --disable-version3)"
fi
FFMPEG_TAG_SHIPPED="$(cat "$BUILD_DIR/third_party/ffmpeg/PS2R_FFMPEG_TAG" 2>/dev/null || echo "")"
SDL2_TAG="$(grep -o 'release-[0-9.]*' "$ROOT/cmake/Sdl2Dependency.cmake" | head -1)"
IMGUI_VER="$(grep -o 'Release: `[^`]*`' "$ROOT/app/gui/third_party/imgui/PROVENANCE.md" | head -1 | sed 's/Release: //; s/`//g')"

# --- the bundled libraries' licence texts: the committed set (tools/dist/licenses/, pinned by
#     tests/lint/lint_artifact_licenses.py), never a build cache or a package manager's copy ---
LIC_SRC="$ROOT/tools/dist/licenses"
mkdir -p "$DIST/licenses"
copy_licence() { cp "$LIC_SRC/$1" "$DIST/licenses/$1"; echo "licence: $1"; }
copy_licence SDL2-LICENSE.txt
copy_licence FFmpeg-LICENSE.md
copy_licence FFmpeg-COPYING.LGPLv2.1.txt
copy_licence imgui-LICENSE.txt
copy_licence imgl3w-UNLICENSE.txt
case "$PLATFORM" in
  linux-*) copy_licence GCC-COPYING3.txt; copy_licence GCC-RUNTIME-LIBRARY-EXCEPTION.txt ;;
esac
# The proof that no GPL / non-free component is compiled in -- generated from WHAT WAS LINKED: the
# configuration string libavcodec embeds in the shipped object (checked above against the recipe
# and for GPL flags), followed by the recipe it was checked against. tests/lint/
# lint_artifact_licenses.py --dist re-reads the binary and refuses a mismatch.
{
  echo "FFmpeg ${FFMPEG_TAG_SHIPPED:-$(printf '%s' "$FFMPEG_CONF" | grep -o 'ffmpeg/[0-9.]*' | head -1)} as linked into $FFMPEG_CONF_FROM."
  echo
  echo "libavcodec's configuration string embedded in $(basename "${FFMPEG_DYN:-$DIST/nativeee}") (avcodec_configuration(); the configure"
  echo "command line the linked FFmpeg was really built with; read by build.sh with: strings <file> | grep -- --prefix=):"
  echo "$FFMPEG_CONF"
  echo
  if [ "$FFMPEG_LINK" = "static" ]; then
    echo "It equals, argument for argument, the recipe (tools/ci/ffmpeg_static.sh --print-configure; --prefix and the"
    echo "host-only --disable-x86asm aside):"
    "$ROOT/tools/ci/ffmpeg_static.sh" --print-configure
  else
    echo "Dynamic FFmpeg from the package source (bundled in lib/, replaceable); no GPL / non-free flag (checked by build.sh)."
  fi
} > "$DIST/licenses/FFmpeg-BUILD-CONFIGURATION.txt"
echo "licence: FFmpeg-BUILD-CONFIGURATION.txt (generated from the configuration string embedded in $FFMPEG_CONF_FROM)"

# --- notices: what the binary links, how, under which licence, where the text is ---
{
  echo "# NativeEE -- third-party notices -- $(cat "$DIST/BUILD-ID")"
  echo
  echo "This NativeEE folder ships the recompiled game program and the native engine. It contains NO game"
  echo "data: no textures, audio, models, FMVs or disc bytes (tools/ci/asset_scan.py runs on every"
  echo "artifact). You must own the title (SLUS-12345) and supply your own disc image."
  echo
  echo "The licence texts named below are in licenses/ beside this file."
  echo
  echo "## Compiled into the binary"
  echo
  echo "- **SDL2 $SDL2_TAG** -- zlib licence (licenses/SDL2-LICENSE.txt)."
  if [ "$FFMPEG_LINK" = "static" ]; then
    echo "  Built from the pinned release by the build (cmake/Sdl2Dependency.cmake) and linked"
    echo "  STATICALLY. Its platform back ends (macOS: Cocoa/CoreAudio/GameController; Linux: X11,"
    echo "  Wayland, PulseAudio, ALSA) are the system's own, loaded at run time."
  else
    echo "  Bundled as a dynamic library in lib/ (the package source's build)."
  fi
  echo "- **FFmpeg ${FFMPEG_TAG_SHIPPED:-$(printf '%s' "$FFMPEG_CONF" | grep -o 'ffmpeg/[0-9.]*' | head -1)}** -- libavcodec, libavutil, libswscale: GNU Lesser General Public License v2.1"
  echo "  or later (licenses/FFmpeg-COPYING.LGPLv2.1.txt; the per-component terms are in"
  echo "  licenses/FFmpeg-LICENSE.md)."
  if [ "$FFMPEG_LINK" = "static" ]; then
    echo "  Built from the pinned tag by tools/ci/ffmpeg_static.sh and linked STATICALLY with ONLY the"
    echo "  MPEG-1/MPEG-2 video decoders, the mpegvideo parser and the colour conversion enabled;"
    echo "  --disable-gpl --disable-nonfree --disable-version3: NO GPL or non-free component is"
    echo "  compiled in. licenses/FFmpeg-BUILD-CONFIGURATION.txt quotes the configure line AS EMBEDDED in"
    echo "  this binary by libavcodec (what was linked, not the recipe) and the recipe it was checked against."
    echo "  LGPL section 6: the complete source of this program and the recipe that rebuilds the"
    echo "  bundled FFmpeg are published with this BUILD-ID; ./build.sh (build.ps1) relinks the"
    echo "  artifact against a modified FFmpeg."
  else
    echo "  Bundled as dynamic libraries in lib/ and replaceable (swap the lib/ files for another"
    echo "  LGPL build of the same major versions). Its configure line as embedded in the bundled"
    echo "  libavcodec (no GPL / non-free component, checked by build.sh): licenses/FFmpeg-BUILD-CONFIGURATION.txt."
  fi
  echo "- **Dear ImGui $IMGUI_VER** (app/gui/third_party/imgui: the F1 developer overlay and the first-run"
  echo "  launcher) -- MIT licence (licenses/imgui-LICENSE.txt). Its stb headers (imstb_*.h) carry"
  echo "  their own public-domain / MIT notices inside the files."
  echo "- **OpenGL loader**: the engine uses no loader library -- it links the platform's GL"
  echo "  ($( [ "$PLATFORM" = macos ] && echo "OpenGL.framework" || echo "libGL.so.1, libglvnd / Mesa or the vendor driver" )) and calls the exported entry points directly. The only"
  echo "  loader code in the binary is Dear ImGui's embedded gl3w-derived header"
  echo "  (imgui_impl_opengl3_loader.h), public domain (licenses/imgl3w-UNLICENSE.txt)."
  case "$PLATFORM" in
    linux-*)
      echo "- **libstdc++ and libgcc** (the GCC runtime libraries, linked STATICALLY: -static-libstdc++"
      echo "  -static-libgcc) -- GNU General Public License v3 (licenses/GCC-COPYING3.txt) WITH the GCC"
      echo "  Runtime Library Exception 3.1 (licenses/GCC-RUNTIME-LIBRARY-EXCEPTION.txt), which permits"
      echo "  their inclusion in this independently licensed program." ;;
  esac
  if [ -n "$(ls -A "$DIST/lib" 2>/dev/null)" ]; then
    echo
    echo "## Bundled dynamic libraries (lib/)"
    echo
    echo "This build bundles its non-system dynamic closure; each library is under its own licence as"
    echo "distributed by the package source ($( { command -v brew >/dev/null 2>&1 && echo "Homebrew ($(brew --prefix 2>/dev/null))"; } || echo "the host distribution")):"
    echo
    for l in "$DIST"/lib/*; do [ -f "$l" ] && echo "- $(basename "$l")"; done
  fi
  echo
  echo "## From your system at run time (nothing bundled)"
  echo
  case "$PLATFORM" in
    macos)   echo "OpenGL.framework, libc++, libSystem (macOS)." ;;
    linux-*) echo "glibc, libGL.so.1 (libglvnd / Mesa or the vendor driver), and whichever of libX11 /"
             echo "libwayland-client / libpulse / libasound SDL2 finds." ;;
  esac
  echo
  echo "## The port's own code"
  echo
  echo "See LICENSE (the project licence)."
} > "$DIST/THIRD_PARTY_NOTICES.md"

rmdir "$DIST/lib" 2>/dev/null || true   # the dist presets are all static; the folder is the binary + texts

# --- the licence set and the notices must agree with what was linked (ctest LintArtifactLicenses
#     runs the same check on the committed set and on dist/ when present) ---
if ! python3 "$ROOT/tests/lint/lint_artifact_licenses.py" --root "$ROOT" --dist "$DIST"; then
  echo "build.sh: REFUSING to ship -- the licence set in $DIST is incomplete or inconsistent" >&2
  rm -rf "$DIST"; exit 1
fi

# --- the asset guard: the C1 asset scan (names, magic, ELF byte windows) on the staged folder ---
step "asset scan ($DIST)"
if ! python3 "$ROOT/tools/ci/asset_scan.py" "$DIST" --report "$BUILD_DIR/asset_scan_dist.md"; then
  echo "build.sh: REFUSING to ship -- the asset scan found game data in $DIST (report: $BUILD_DIR/asset_scan_dist.md)" >&2
  rm -rf "$DIST"; exit 1
fi

# --- Linux: the one-file download, scanned as well ---
ARCHIVE=""
case "$PLATFORM" in
  linux-*)
    ARCHIVE="$ROOT/dist/nativeee-$PLATFORM.tar.gz"
    rm -f "$ARCHIVE"
    tar -C "$ROOT/dist" -czf "$ARCHIVE" "$PLATFORM"
    if ! python3 "$ROOT/tools/ci/asset_scan.py" "$ARCHIVE" --report "$BUILD_DIR/asset_scan_archive.md"; then
      echo "build.sh: REFUSING to ship -- the asset scan found game data in $ARCHIVE" >&2; rm -f "$ARCHIVE"; exit 1
    fi
    ;;
esac

step "artifact   ($DIST)"
du -sh "$DIST"; ls -la "$DIST"; [ -d "$DIST/lib" ] && ls -la "$DIST/lib"
case "$PLATFORM" in
  macos)   echo; echo "load check (every line must be a system library -- /usr/lib, /System):"
           otool -L "$DIST/nativeee" | tail -n +2
           if [ "$PRESET" = "macos-dist" ] && otool -L "$DIST/nativeee" | tail -n +2 | grep -qvE '^\s*(/usr/lib/|/System/Library/)'; then
             echo "build.sh: NOT self-contained -- a non-system library is still dynamic (see above)" >&2; exit 1
           fi ;;
  linux-*) echo; echo "load check (every line must be a system library -- glibc, libGL, or none):"
           ldd "$DIST/nativeee" | grep -v "linux-vdso" || true
           if ldd "$DIST/nativeee" | grep -qE "libSDL2|libav(codec|util)|libswscale|libstdc\+\+"; then
             echo "build.sh: NOT self-contained -- a bundled library is still dynamic (see above)" >&2; exit 1
           fi ;;
esac
[ -n "$ARCHIVE" ] && { echo; ls -la "$ARCHIVE"; shasum -a 256 "$ARCHIVE" 2>/dev/null || sha256sum "$ARCHIVE"; }
echo
echo "build.sh: done -- $DIST/nativeee ($(cat "$DIST/BUILD-ID"))"
