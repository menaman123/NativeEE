#!/usr/bin/env bash
# tools/ci/bundle_smoke.sh -- the pair a release ships for one platform works TOGETHER: the cut compiler
# (fetch_compiler.sh) compiles a real translation unit of the title's source module out of the staged engine bundle
# (stage_engine_bundle.sh), against the bundle's own headers, with the bundle's flags -- the step the launcher's
# "Preparing your game" performs thousands of times, done once here on the lane's runner before anything is uploaded.
# No disc is involved: the TU is public repository code. Then the archiver reads a bundle library (the link side's
# first touch).
#
#   bundle_smoke.sh --bundle DIR --compiler DIR          (DIR = the staged folders, not the zips)
set -euo pipefail
bundle=""; compiler=""
while [ $# -gt 0 ]; do
    case "$1" in
        --bundle) bundle="$2"; shift 2 ;;
        --compiler) compiler="$2"; shift 2 ;;
        -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "bundle_smoke: unknown argument $1" >&2; exit 2 ;;
    esac
done
[ -f "$bundle/BUNDLE.json" ] && [ -f "$compiler/COMPILER.json" ] || { echo "bundle_smoke: --bundle DIR (with BUNDLE.json) and --compiler DIR (with COMPILER.json)" >&2; exit 2; }
PY="$(command -v python3 || command -v python)"
read -r platform title_module cxx ar < <("$PY" -c "
import json,sys
b=json.load(open(sys.argv[1])); c=json.load(open(sys.argv[2]))
print(b['platform'], b['title']['module'], c['bin']['cxx'], c['bin']['ar'])" "$bundle/BUNDLE.json" "$compiler/COMPILER.json")
flags="$("$PY" -c "import json,sys; print(' '.join(json.load(open(sys.argv[1])).get('shard_compile_flags', [])))" "$bundle/BUNDLE.json")"
tu="$(ls "$bundle/$title_module"/hle/*.cpp | head -1)"
[ -f "$tu" ] || { echo "bundle_smoke: no .cpp under $bundle/$title_module/hle" >&2; exit 1; }
lib="$(ls "$bundle"/lib/*ps2r_core* | head -1)"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
echo "platform $platform  compiler $compiler/$cxx  TU ${tu#"$bundle"/}"
"$compiler/$cxx" --version | head -1
case "$platform" in
    macos-*)
        sdk="${SDKROOT:-$(xcrun --show-sdk-path 2>/dev/null || true)}"
        [ -n "$sdk" ] || { echo "bundle_smoke: no macOS SDK (xcode-select --install)" >&2; exit 1; }
        # shellcheck disable=SC2086
        "$compiler/$cxx" -isysroot "$sdk" $flags -I"$bundle/include" -I"$bundle" -c "$tu" -o "$tmp/tu.o" ;;
    linux-*)
        # shellcheck disable=SC2086
        "$compiler/$cxx" $flags -I"$bundle/include" -I"$bundle" -c "$tu" -o "$tmp/tu.o" ;;
    windows-*)
        # clang-cl: the GNU-style flags the shards use are spelled for the cl front end as the clang-cl shim does
        "$compiler/$cxx" /std:c++17 /O2 /EHsc /w -clang:-fno-strict-aliasing -clang:-fwrapv -clang:-ffp-contract=off \
            /I"$bundle/include" /I"$bundle" /c "$tu" /Fo"$tmp/tu.obj" ;;
esac
ls -la "$tmp"/tu.o* | awk '{print "object: " $5 " bytes"}'
case "$platform" in
    windows-*) "$compiler/$ar" /LIST "$lib" | head -3 ;;
    *) "$compiler/$ar" t "$lib" | head -3 ;;
esac
echo "bundle_smoke: ok -- the cut compiler compiled a title TU against the bundle's headers; the archiver reads $(basename "$lib")"
