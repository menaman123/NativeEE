#!/usr/bin/env bash
# tools/ci/release_local.sh -- the release workflow's macOS lane, step for step, on a developer's Mac (there is no `act`
# here): what .github/workflows/release.yml does for ONE platform, so the shape, the sizes and the gates are seen before
# a tag is pushed. Nothing is uploaded. Linux runs the same steps (the launcher becomes an AppImage).
#
#   release_local.sh --title-root DIR [--engine DIR] [--out DIR] [--tag TAG] [--repo OWNER/NAME]
#                    [--build-dir DIR] [--launcher-build-dir DIR] [--launcher-engine DIR] [--qt PREFIX]
#                    [--cache DIR] [--skip-build] [--stub-launcher] [--jobs N]
#
# Steps (each prints "=== n. ..."; every artefact goes to <out>/release/ like the release job's folder):
#   1. the NO-DISC build with the host's *-dist preset (static SDL2 + the LGPL FFmpeg recipe; build.sh --no-disc
#      --preset macos-dist): nativeee (the stub), the tools, the title's HLE library -- the bundle's source;
#   2. the compiler kit: tools/toolchain/fetch_compiler.sh --smoke --pack (sha256-pinned official LLVM, cut, smoked);
#   3. the engine bundle: tools/ci/stage_engine_bundle.sh (engine_bundle target when present, else the fallback shape),
#      named per the manifest's release.bundle_folder, asset-scanned, zipped;
#   4. the launcher: tools/ci/release_launcher.sh (the real app/launcher when --launcher-engine has it and Qt is found;
#      --stub-launcher otherwise, which release_index.py then REFUSES -- the dry run shows the refusal);
#   5. the asset scan over EVERY file in <out>/release (the release job's last gate);
#   6. SHA256SUMS + release-index.json + NOTES.md (tools/ci/release_index.py --allow-missing <the other platforms>);
#   7. a size table.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENGINE="$(cd "$HERE/../.." && pwd)"
TITLE_ROOT=""; OUT=""; TAG=""; REPO=""; BUILD_DIR=""; L_BUILD=""; L_ENGINE=""; QT=""; CACHE="${NATIVEEE_TOOLCHAIN_CACHE:-}"; SKIP_BUILD=0; STUB=0; JOBS="${JOBS:-4}"
while [ $# -gt 0 ]; do
    case "$1" in
        --title-root) TITLE_ROOT="$2"; shift 2 ;;
        --engine) ENGINE="$(cd "$2" && pwd)"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --tag) TAG="$2"; shift 2 ;;
        --repo) REPO="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --launcher-build-dir) L_BUILD="$2"; shift 2 ;;
        --launcher-engine) L_ENGINE="$2"; shift 2 ;;
        --qt) QT="$2"; shift 2 ;;
        --cache) CACHE="$2"; shift 2 ;;
        --skip-build) SKIP_BUILD=1; shift ;;
        --stub-launcher) STUB=1; shift ;;
        --jobs) JOBS="$2"; shift 2 ;;
        -h|--help) sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "release_local: unknown argument $1" >&2; exit 2 ;;
    esac
done
[ -n "$TITLE_ROOT" ] && [ -f "$TITLE_ROOT/build.sh" ] || { echo "release_local: --title-root DIR (the title repository: build.sh, launcher/title.json, games/)" >&2; exit 2; }
TITLE_ROOT="$(cd "$TITLE_ROOT" && pwd)"
MANIFEST="$TITLE_ROOT/launcher/title.json"; [ -f "$MANIFEST" ] || { echo "release_local: no $MANIFEST" >&2; exit 2; }
GAMES="$TITLE_ROOT/games"; [ -d "$GAMES" ] || { echo "release_local: no $GAMES" >&2; exit 2; }
[ -n "$OUT" ] || OUT="$TITLE_ROOT/../release-local"; mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd -P)"
[ -n "$TAG" ] || TAG="$(git -C "$TITLE_ROOT" describe --tags --always 2>/dev/null || echo v0.0.0-local)"
[ -n "$REPO" ] || REPO="$(python3 -c "import json;print(json.load(open('$MANIFEST')).get('release',{}).get('repo','owner/repo'))")"
[ -n "$BUILD_DIR" ] || BUILD_DIR="$OUT/build-nodisc-dist"
[ -n "$L_ENGINE" ] || L_ENGINE="$ENGINE"
[ -n "$CACHE" ] || CACHE="$OUT/.cache"
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) PLATFORM=macos-arm64; PRESET=macos-dist ;; Darwin-x86_64) PLATFORM=macos-x86_64; PRESET=macos-dist ;;
    Linux-x86_64) PLATFORM=linux-x86_64; PRESET=linux-dist ;;
    *) echo "release_local: unsupported host (D-B; Windows: the .ps1 twins by hand)" >&2; exit 2 ;;
esac
OTHERS="$(printf '%s\n' macos-arm64 macos-x86_64 linux-x86_64 windows-x64 | grep -v "^$PLATFORM$" | paste -sd, -)"
REL="$OUT/release"; mkdir -p "$REL"
PRODUCT="$(python3 -c "import json;print(json.load(open('$MANIFEST'))['product'].lower())")"
FOLDER="$(python3 -c "import json;m=json.load(open('$MANIFEST'));print(m.get('release',{}).get('bundle_folder','\${product}-\${platform}').replace('\${product}','$PRODUCT').replace('\${platform}','$PLATFORM'))")"
BUNDLE_ZIP="$(python3 -c "import json;m=json.load(open('$MANIFEST'));print(m.get('release',{}).get('asset_pattern','\${product}-\${tag}-\${platform}.zip').replace('\${product}','$PRODUCT').replace('\${tag}','$TAG').replace('\${platform}','$PLATFORM'))")"
step() { echo; echo "=== $*"; echo "    ($(uptime | sed 's/.*load/load/'))"; }
echo "release_local: $PRODUCT $TAG on $PLATFORM (preset $PRESET) -> $REL"
T0=$(date +%s)

step "1. the no-disc build ($PRESET -> $BUILD_DIR)"
if [ "$SKIP_BUILD" = 1 ] && [ -f "$BUILD_DIR/CMakeCache.txt" ]; then echo "skipped (--skip-build)"; else
    # build.sh wants engine/ and games/ at the title root (the real repository has them; a scratch copy may symlink)
    (cd "$TITLE_ROOT" && ./build.sh --no-disc --preset "$PRESET" --build-dir "$BUILD_DIR" --jobs "$JOBS" \
        --target nativeee --target ps2r_recompiler_v2 --target ps2r_iop_recompiler --target ps2r_disc_extract \
        --target ps2r_elf_embedded --target vu1_aot_pack --target ps2r_title_hle > "$OUT/build.log" 2>&1) || { tail -20 "$OUT/build.log"; exit 1; }
    tail -2 "$OUT/build.log"
fi
T1=$(date +%s)

step "2. the compiler kit (fetch_compiler.sh --platform $PLATFORM --smoke --pack)"
"$ENGINE/tools/toolchain/fetch_compiler.sh" --platform "$PLATFORM" --out "$OUT/compiler" --cache "$CACHE" --smoke --pack > "$OUT/compiler.log" 2>&1 || { tail -20 "$OUT/compiler.log"; exit 1; }
grep -E "^(platform|cut size|smoke|zip)" "$OUT/compiler.log" || tail -5 "$OUT/compiler.log"
cp "$OUT"/compiler/nativeee-compiler-*.zip "$REL/"
T2=$(date +%s)

step "3. the engine bundle (stage_engine_bundle.sh --name $FOLDER)"
"$ENGINE/tools/ci/stage_engine_bundle.sh" --engine "$ENGINE" --games "$GAMES" --build-dir "$BUILD_DIR" --out "$OUT/bundle" \
    --platform "$PLATFORM" --version "$TAG" --name "$FOLDER" --compiler-json "$OUT/compiler/compiler/COMPILER.json" > "$OUT/bundle.log" 2>&1 || { tail -20 "$OUT/bundle.log"; exit 1; }
grep -E "route|lib/:|bin/:|include/:|games/|BUNDLE.json|NOTE|CLEAN|zip:" "$OUT/bundle.log"
cp "$OUT/bundle/$FOLDER.zip" "$REL/$BUNDLE_ZIP"
T3=$(date +%s)

step "4. the launcher (release_launcher.sh$( [ "$STUB" = 1 ] && echo ' --stub' ))"
L_ARGS=(--manifest "$MANIFEST" --engine "$L_ENGINE" --dist-dir "$OUT/launcher" --version "$TAG" --pins-cache "$CACHE")
[ -n "$QT" ] && L_ARGS+=(--qt "$QT")
if [ "$STUB" = 1 ]; then L_ARGS+=(--stub --build-dir "$OUT/launcher/build")
elif [ -n "$L_BUILD" ]; then L_ARGS+=(--build-dir "$L_BUILD" --skip-build)
else L_ARGS+=(--build-dir "$OUT/launcher/build"); fi
JOBS="$JOBS" "$ENGINE/tools/ci/release_launcher.sh" "${L_ARGS[@]}" > "$OUT/launcher.log" 2>&1 || { tail -20 "$OUT/launcher.log"; exit 1; }
grep -E "^(product|frameworks|codesign|bundle starts|image|staged|release_launcher)|CLEAN|notariz|signing" "$OUT/launcher.log"
cp "$OUT"/launcher/"$PRODUCT"-launcher-*.dmg "$OUT"/launcher/"$PRODUCT"-launcher-*.AppImage "$REL/" 2>/dev/null || true
cp "$OUT"/launcher/*.STUB "$REL/" 2>/dev/null || true
T4=$(date +%s)

step "5. the asset scan over every release file (the release job's last gate)"
for f in "$REL"/*; do
    case "$f" in *.STUB|*/SHA256SUMS|*/release-index.json|*/NOTES.md) continue ;; esac
    python3 "$ENGINE/tools/ci/asset_scan.py" "$f" | grep -E "asset_scan:" | sed "s|^|$(basename "$f"): |"
done
T5=$(date +%s)

step "6. SHA256SUMS + release-index.json + NOTES.md (release_index.py --allow-missing $OTHERS)"
set +e
python3 "$ENGINE/tools/ci/release_index.py" --dir "$REL" --tag "$TAG" --repo "$REPO" --manifest "$MANIFEST" \
    --engine-commit "$(git -C "$ENGINE" rev-parse HEAD)" --title-commit "$(git -C "$TITLE_ROOT" rev-parse HEAD 2>/dev/null || echo -)" --allow-missing "$OTHERS"
rc=$?; set -e
if [ "$rc" -ne 0 ]; then
    if [ "$STUB" = 1 ]; then echo "(expected: a STUB launcher is refused -- the index is NOT written; a real launcher build lifts this)"
    else exit "$rc"; fi
else
    cat "$REL/SHA256SUMS"
    python3 -c "import json;d=json.load(open('$REL/release-index.json'));print(json.dumps(d['x-nativeee']['platforms'],indent=1))"
fi
T6=$(date +%s)

step "7. sizes"
(cd "$REL" && ls -la | awk 'NR>1 && $NF!="." && $NF!=".." {printf "%10.1f MB  %s\n", $5/1048576, $NF}')
echo
echo "unpacked: compiler $(du -sh "$OUT/compiler/compiler" | cut -f1), engine bundle $(du -sh "$OUT/bundle/$FOLDER" | cut -f1), launcher $(du -sh "$OUT"/launcher/launcher-*/ 2>/dev/null | cut -f1 | head -1)"
printf "timings: build %ds, compiler %ds, bundle %ds, launcher %ds, scan %ds, index %ds (%s)\n" $((T1-T0)) $((T2-T1)) $((T3-T2)) $((T4-T3)) $((T5-T4)) $((T6-T5)) "$(uptime | sed 's/.*load/load/')"
echo "release_local: OK -> $REL"
