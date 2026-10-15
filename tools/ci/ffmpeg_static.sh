#!/usr/bin/env bash
# tools/ci/ffmpeg_static.sh -- a minimal STATIC FFmpeg for the self-contained artifacts
# (Charter 2, Track C1: the linux-dist and macos-dist presets). The title's FMV path
# (engine/ipu, ps2r_ipu) decodes MPEG-2 video and converts YUV -> RGBA; nothing else of
# FFmpeg is used, so the artifact links only
#   libavcodec.a   (the mpeg1/mpeg2 video decoders + the mpegvideo parser)
#   libavutil.a    (frames, errors, pixel formats)
#   libswscale.a   (the colour conversion)
# built from the pinned release with every other codec, muxer, protocol, device, filter and
# external library OFF. No system FFmpeg package is needed at run time (the distribution's /
# Homebrew's libavcodec is built --enable-gpl with x264/x265 and drags dozens of shared
# libraries: neither shippable nor LGPL).
#
#   tools/ci/ffmpeg_static.sh <prefix>        build; CACHED only when <prefix>/lib/{libavcodec,libavutil,
#                                             libswscale}.a exist AND <prefix>/PS2R_FFMPEG_CONFIGURE (the
#                                             record this script writes after a build: tag + every configure
#                                             argument) equals what this script would run now. A prefix
#                                             without the record, from another tag, or from other arguments
#                                             is stale and is rebuilt -- the cache key is the recipe, not the
#                                             existence of the .a files (review lic3b 1a: a cached prefix
#                                             silently outlived a recipe change).
#   tools/ci/ffmpeg_static.sh --print-configure
#                                             print the tag and the exact configure arguments, human-readable
#                                             (no build); build.sh quotes this in dist/<platform>/licenses/
#                                             FFmpeg-BUILD-CONFIGURATION.txt as "the recipe"
#   tools/ci/ffmpeg_static.sh --print-args    the configure arguments one per line, nothing else: build.sh
#                                             and tests/lint/lint_artifact_licenses.py compare them with the
#                                             configuration string libavcodec embeds in the linked binary
#   FFMPEG_TAG=n6.1.2 (default)               the release tag (git.ffmpeg.org, shallow clone)
#   FFMPEG_JOBS=<n>                           make jobs (default: the host's CPU count)
#
# Licence: FFmpeg is LGPL v2.1 or later. This configuration enables NO GPL and NO non-free
# component (--disable-gpl --disable-nonfree; --disable-version3 keeps the LGPL v2.1 text that
# tools/dist/licenses/ ships); dist/<platform>/THIRD_PARTY_NOTICES.md names the tag and this
# recipe so the LGPL relink requirement is met from the repository (sources + this script).
set -euo pipefail
TAG="${FFMPEG_TAG:-n6.1.2}"

# The ONE list of configure arguments (read by --print-configure and by the build).
FFMPEG_CONFIGURE_ARGS=(
  --disable-everything --disable-programs --disable-doc --disable-network --disable-autodetect
  --disable-shared --enable-static --enable-pic
  --disable-gpl --disable-nonfree --disable-version3 --disable-debug
  --disable-avdevice --disable-avformat --disable-avfilter --disable-postproc --disable-swresample
  --enable-avcodec --enable-avutil --enable-swscale
  --enable-decoder=mpeg2video --enable-decoder=mpeg1video --enable-parser=mpegvideo
)

if [ "${1:-}" = "--print-configure" ]; then
  echo "FFmpeg $TAG (https://git.ffmpeg.org/ffmpeg.git, tag $TAG), built by tools/ci/ffmpeg_static.sh:"
  echo "./configure --prefix=<prefix> \\"
  for a in "${FFMPEG_CONFIGURE_ARGS[@]}"; do echo "  $a \\"; done
  echo "  [--disable-x86asm on x86-64 hosts without nasm/yasm]"
  exit 0
fi
if [ "${1:-}" = "--print-args" ]; then
  printf '%s\n' "${FFMPEG_CONFIGURE_ARGS[@]}"
  exit 0
fi

PREFIX="${1:?usage: ffmpeg_static.sh <prefix> | --print-configure | --print-args}"
if [ -n "${FFMPEG_JOBS:-}" ]; then JOBS="$FFMPEG_JOBS"
elif command -v nproc >/dev/null 2>&1; then JOBS="$(nproc)"
elif command -v sysctl >/dev/null 2>&1; then JOBS="$(sysctl -n hw.ncpu)"
else JOBS=4; fi
ARCH_FLAGS=()
case "$(uname -m)" in
  x86_64) command -v nasm >/dev/null 2>&1 || command -v yasm >/dev/null 2>&1 || ARCH_FLAGS+=(--disable-x86asm) ;;
esac
# The build's record: the tag + every configure argument, in order. A prefix is a cache hit only
# when this exact text is what the prefix was built from.
RECORD="$({ echo "tag=$TAG"; printf '%s\n' "${FFMPEG_CONFIGURE_ARGS[@]}" ${ARCH_FLAGS[@]+"${ARCH_FLAGS[@]}"}; })"
if [ -f "$PREFIX/lib/libavcodec.a" ] && [ -f "$PREFIX/lib/libswscale.a" ] && [ -f "$PREFIX/lib/libavutil.a" ]; then
  if [ -f "$PREFIX/PS2R_FFMPEG_CONFIGURE" ] && [ "$(cat "$PREFIX/PS2R_FFMPEG_CONFIGURE")" = "$RECORD" ]; then
    echo "ffmpeg_static: cached at $PREFIX ($TAG; PS2R_FFMPEG_CONFIGURE == the recipe)"
    exit 0
  fi
  if [ -f "$PREFIX/PS2R_FFMPEG_CONFIGURE" ]; then
    echo "ffmpeg_static: STALE prefix $PREFIX -- its PS2R_FFMPEG_CONFIGURE differs from the recipe; rebuilding:"
    diff <(cat "$PREFIX/PS2R_FFMPEG_CONFIGURE") <(printf '%s\n' "$RECORD") | sed 's/^/  /' || true
  else
    echo "ffmpeg_static: STALE prefix $PREFIX -- no PS2R_FFMPEG_CONFIGURE record (built before the record existed); rebuilding"
  fi
fi
SRC="$PREFIX.src"
rm -rf "$SRC" "$PREFIX"
mkdir -p "$(dirname "$PREFIX")"
echo "ffmpeg_static: cloning FFmpeg $TAG"
git clone --quiet --depth 1 --branch "$TAG" https://git.ffmpeg.org/ffmpeg.git "$SRC"
cd "$SRC"
echo "ffmpeg_static: configure (mpeg2video decoder + swscale only, static, no GPL)"
./configure --prefix="$PREFIX" "${FFMPEG_CONFIGURE_ARGS[@]}" ${ARCH_FLAGS[@]+"${ARCH_FLAGS[@]}"} \
  > "$SRC/configure.log" 2>&1 || { tail -n 30 "$SRC/configure.log"; exit 1; }
make -j"$JOBS" > "$SRC/make.log" 2>&1 || { tail -n 30 "$SRC/make.log"; exit 1; }
make install > "$SRC/install.log" 2>&1
echo "$TAG" > "$PREFIX/PS2R_FFMPEG_TAG"
# The configure line as run = the cache key (see the header). build.sh does NOT stage this: the
# artifact's licences/FFmpeg-BUILD-CONFIGURATION.txt quotes the configuration string libavcodec
# embedded in the linked binary, and checks it against --print-args.
printf '%s\n' "$RECORD" > "$PREFIX/PS2R_FFMPEG_CONFIGURE"
# The licence texts from the built tree (build.sh stages the committed copies in tools/dist/licenses/;
# these are kept so a reviewer can diff them against the committed set).
mkdir -p "$PREFIX/share/licenses"
cp "$SRC/COPYING.LGPLv2.1" "$PREFIX/share/licenses/FFmpeg-COPYING.LGPLv2.1.txt"
cp "$SRC/LICENSE.md" "$PREFIX/share/licenses/FFmpeg-LICENSE.md"
rm -rf "$SRC"
echo "ffmpeg_static: installed $TAG -> $PREFIX ($(ls "$PREFIX/lib"/*.a | xargs -n1 basename | tr '\n' ' '))"
