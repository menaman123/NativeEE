#!/usr/bin/env bash
# tools/ci/release_launcher.sh -- package the engine's launcher (app/launcher, Qt 6 Widgets) for a TITLE, on macOS
# (.dmg) or Linux (AppImage). Title-neutral: everything about the game comes from the manifest the title passes
# (--manifest launcher/title.json); the engine ships nothing of a title. The Windows twin is release_launcher.ps1.
# [From TwinsanityEvolved/tools/ci/release_launcher.sh, made generic (manifest-driven, in the engine), the Linux
#  AppImage lane added, and the first-run story changed to this engine's D-A model: the launcher downloads the
#  ENGINE BUNDLE and the COMPILER KIT of the same release and compiles the player's game on the player's machine.]
#
#   release_launcher.sh --manifest FILE [--engine DIR] [--qt PREFIX] [--build-dir DIR] [--dist-dir DIR]
#                       [--version TAG] [--skip-build] [--no-image] [--stub] [--pins-cache DIR]
#
# What it does, in order:
#   1. configure + build <engine>/app/launcher alone (cmake -S app/launcher; PS2R_LAUNCHER_VERSION = --version or the
#      engine's describe); its tests under QT_QPA_PLATFORM=offscreen (LauncherModels, LauncherScreens, LauncherLint /
#      LauncherOneClass -- whatever the CMakeLists registers);
#   2. stage dist/launcher-<platform>/: the application carrying the title's name (manifest `launcher_name`, else
#      "<app_data_folder> Launcher"), the manifest at <resources>/titles/title.json, BUILD-ID, LICENSE (the
#      title's, --licence, else the engine's), THIRD_PARTY_NOTICES.md (the engine's launcher + Qt, LGPL v3,
#      DYNAMIC), licenses/Qt-LICENSE.LGPL3;
#   3. macOS: macdeployqt (frameworks + plugins, dynamic), codesign (ad hoc; CODESIGN_IDENTITY names a Developer ID
#      certificate; NOTARY_PROFILE names a `notarytool store-credentials` profile -> notarize + staple), the bundle
#      answers --help;  Linux: linuxdeploy + linuxdeploy-plugin-qt (pinned in tools/toolchain/PACKAGING_PINS.sha256,
#      sha256-verified) -> AppDir -> .AppImage;
#   4. the asset scan over the staged folder (tools/ci/asset_scan.py: zero findings);
#   5. dist/<product>-launcher-<version>-<platform>.dmg | .AppImage + .sha256, the size reported.
#
# --stub: no launcher source yet (or no Qt) -- stage a one-file placeholder app with the same bundle layout so the
# rest of a release pipeline (scan, image, sums, index) can be exercised. Its BUILD-ID says STUB; a release job
# REFUSES a stub (release_index.py checks).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENGINE="$(cd "$HERE/../.." && pwd)"
MANIFEST=""; QT="${QT_PREFIX:-}"; BUILD=""; DIST_ROOT=""; VERSION=""; SKIP_BUILD=0; IMAGE=1; STUB=0; LICENCE=""; PINS_CACHE="${NATIVEEE_TOOLCHAIN_CACHE:-}"
while [ $# -gt 0 ]; do
    case "$1" in
        --manifest) MANIFEST="$2"; shift 2 ;;
        --engine) ENGINE="$(cd "$2" && pwd)"; shift 2 ;;
        --qt) QT="$2"; shift 2 ;;
        --build-dir) BUILD="$2"; shift 2 ;;
        --dist-dir) DIST_ROOT="$2"; shift 2 ;;
        --version) VERSION="$2"; shift 2 ;;
        --licence) LICENCE="$2"; shift 2 ;;
        --skip-build) SKIP_BUILD=1; shift ;;
        --no-image) IMAGE=0; shift ;;
        --stub) STUB=1; shift ;;
        --pins-cache) PINS_CACHE="$2"; shift 2 ;;
        -h|--help) sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "release_launcher: unknown argument $1" >&2; exit 2 ;;
    esac
done
[ -n "$MANIFEST" ] && [ -f "$MANIFEST" ] || { echo "release_launcher: --manifest FILE (the title's launcher/title.json) is required" >&2; exit 2; }
MANIFEST="$(cd "$(dirname "$MANIFEST")" && pwd)/$(basename "$MANIFEST")"
[ -n "$BUILD" ] || BUILD="$ENGINE/build-launcher"
[ -n "$DIST_ROOT" ] || DIST_ROOT="$ENGINE/dist"
mkdir -p "$BUILD" "$DIST_ROOT"; BUILD="$(cd "$BUILD" && pwd -P)"; DIST_ROOT="$(cd "$DIST_ROOT" && pwd -P)"
step() { echo; echo "=== $*"; }
sha256_file() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"; else shasum -a 256 "$1"; fi; }
mf() { python3 -c "import json,sys; m=json.load(open(sys.argv[1])); v=m
for k in sys.argv[2].split('.'):
    v = v.get(k) if isinstance(v, dict) else None
print('' if v is None else v)" "$MANIFEST" "$1"; }
PRODUCT="$(mf product)"; TITLE_ID="$(mf id)"; APPDATA="$(mf app_data_folder)"; LAUNCHER_NAME="$(mf launcher_name)"
[ -n "$PRODUCT" ] && [ -n "$TITLE_ID" ] || { echo "release_launcher: the manifest needs product and id" >&2; exit 1; }
[ -n "$LAUNCHER_NAME" ] || LAUNCHER_NAME="${APPDATA:-$PRODUCT} Launcher"
PRODUCT_LC="$(printf '%s' "$PRODUCT" | tr 'A-Z' 'a-z')"
[ -n "$VERSION" ] || VERSION="$(git -C "$ENGINE" describe --tags --always 2>/dev/null || git -C "$ENGINE" rev-parse --short HEAD)"
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) PLATFORM=macos-arm64; OS=macos ;;
    Darwin-x86_64) PLATFORM=macos-x86_64; OS=macos ;;
    Linux-x86_64) PLATFORM=linux-x86_64; OS=linux ;;
    *) echo "release_launcher: unsupported host $(uname -s)-$(uname -m) (Windows: release_launcher.ps1; D-B)" >&2; exit 2 ;;
esac
echo "product $PRODUCT ($TITLE_ID)  launcher name '$LAUNCHER_NAME'  version $VERSION  platform $PLATFORM"
[ -n "$LICENCE" ] || LICENCE="$(dirname "$(dirname "$MANIFEST")")/LICENSE"
[ -f "$LICENCE" ] || LICENCE="$ENGINE/LICENSE"

# --- 1. build + tests -------------------------------------------------------------------------------------------
EXE_NAME=ps2r_launcher
if [ "$STUB" = 0 ]; then
    [ -f "$ENGINE/app/launcher/CMakeLists.txt" ] || { echo "release_launcher: $ENGINE/app/launcher is not there (the launcher track); use --stub for a pipeline dry run" >&2; exit 2; }
    if [ -z "$QT" ]; then
        for c in /opt/homebrew/opt/qt6 /opt/homebrew/opt/qt /usr/lib/qt6 /usr/lib/x86_64-linux-gnu/qt6 /usr; do
            if [ -x "$c/bin/qmake" ] || [ -x "$c/bin/qmake6" ] || [ -x "$c/libexec/qmake6" ]; then QT="$c"; break; fi
        done
    fi
    [ -n "$QT" ] || { echo "release_launcher: no Qt 6 found (--qt PREFIX)" >&2; exit 2; }
    QMAKE="$QT/bin/qmake"; [ -x "$QMAKE" ] || QMAKE="$QT/bin/qmake6"; [ -x "$QMAKE" ] || QMAKE="$(command -v qmake6 || true)"
    QT_VERSION="$("$QMAKE" -query QT_VERSION 2>/dev/null || echo unknown)"
    if [ "$SKIP_BUILD" = 0 ]; then
        step "build the launcher ($ENGINE/app/launcher -> $BUILD; Qt $QT_VERSION at $QT)"
        cmake -S "$ENGINE/app/launcher" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QT" \
              "-DPS2R_LAUNCHER_VERSION=$VERSION" > "$BUILD/configure.log" 2>&1 || { tail -30 "$BUILD/configure.log"; exit 1; }
        cmake --build "$BUILD" ${JOBS:+-j "$JOBS"} > "$BUILD/build.log" 2>&1 || { grep -n "error\|FAILED" "$BUILD/build.log" | head -20; exit 1; }
        step "the launcher's tests (QT_QPA_PLATFORM=offscreen)"
        (cd "$BUILD" && QT_QPA_PLATFORM=offscreen ctest --output-on-failure 2>&1 | tail -12)
    fi
else
    QT_VERSION="none (stub)"
fi

# --- 2. stage -----------------------------------------------------------------------------------------------------
DIST="$DIST_ROOT/launcher-$PLATFORM"
rm -rf "$DIST"; mkdir -p "$DIST"
write_common() {  # $1 = resources dir
    local RES="$1"; mkdir -p "$RES/titles" "$RES/licenses"
    cp "$MANIFEST" "$RES/titles/title.json"
    {
        echo "$LAUNCHER_NAME $VERSION ($PLATFORM)$( [ "$STUB" = 1 ] && echo " STUB -- not a launcher build; a pipeline dry run" )"
        echo "title   $TITLE_ID ($PRODUCT)"
        echo "engine  $(git -C "$ENGINE" rev-parse HEAD 2>/dev/null || echo unknown)"
        echo "qt      $QT_VERSION"
        echo "built   $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    } > "$RES/BUILD-ID"
    cat "$RES/BUILD-ID"
    cp "$LICENCE" "$RES/LICENSE"
    local lic_src=""
    if [ -n "$QT" ]; then
        for c in "$QT/share/qt/licenses" "$QT/share/qt6/licenses" "$QT/Licenses"; do [ -f "$c/LICENSE.LGPL3" ] && lic_src="$c" && break; done
    fi
    if [ -n "$lic_src" ]; then cp "$lic_src/LICENSE.LGPL3" "$RES/licenses/Qt-LICENSE.LGPL3"
    elif [ -f "$ENGINE/tools/dist/licenses/Qt-LICENSE.LGPL3.txt" ]; then cp "$ENGINE/tools/dist/licenses/Qt-LICENSE.LGPL3.txt" "$RES/licenses/Qt-LICENSE.LGPL3"
    elif [ -f "$HERE/../dist/licenses/Qt-LICENSE.LGPL3.txt" ]; then cp "$HERE/../dist/licenses/Qt-LICENSE.LGPL3.txt" "$RES/licenses/Qt-LICENSE.LGPL3"   # this script's own tree
    else echo "release_launcher: no Qt LGPL text found (kit or engine/tools/dist/licenses/Qt-LICENSE.LGPL3.txt)" >&2; exit 1; fi
    {
        echo "# $LAUNCHER_NAME -- third-party notices -- $(head -1 "$RES/BUILD-ID")"
        echo
        echo "This application is the NativeEE engine's launcher (app/launcher, MIT) carrying this title's manifest. It contains"
        echo "NO game data and no code derived from the game. When you press Play the first time it downloads, from the same"
        echo "release it came from, the engine (prebuilt; no game code in it) and a compiler, reads YOUR disc image, and prepares"
        echo "the game on this computer. Nothing made from your disc is ever uploaded or shared."
        echo
        echo "| component | how it is linked | licence | text |"
        echo "|---|---|---|---|"
        echo "| $PRODUCT (the manifest, this packaging) | titles/title.json | the title repository's | LICENSE |"
        echo "| NativeEE launcher (app/launcher) | the executable | MIT | the engine repository's LICENSE |"
        echo "| Qt $QT_VERSION (Core, Gui, Widgets, Network, Concurrent, the platform plugin) | DYNAMIC: $( [ "$OS" = macos ] && echo "Contents/Frameworks, Contents/PlugIns" || echo "usr/lib inside the AppImage" ) | LGPL v3 | licenses/Qt-LICENSE.LGPL3 |"
        echo
        echo "Qt is used under the LGPL v3: it is linked dynamically and may be replaced by another build of the same major"
        echo "version (section 4(d)). Qt's source: https://download.qt.io/official_releases/qt/ ; the launcher's: the engine repository."
    } > "$RES/THIRD_PARTY_NOTICES.md"
}

if [ "$OS" = macos ]; then
    APP="$DIST/$LAUNCHER_NAME.app"
    step "stage $APP"
    if [ "$STUB" = 1 ]; then
        mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
        cat > "$APP/Contents/MacOS/$EXE_NAME" <<'EOF'
#!/bin/sh
# STUB launcher -- a pipeline dry run placeholder; prints its BUILD-ID and exits.
cat "$(dirname "$0")/../Resources/BUILD-ID"
EOF
        chmod +x "$APP/Contents/MacOS/$EXE_NAME"
        cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>$EXE_NAME</string>
<key>CFBundleName</key><string>$LAUNCHER_NAME</string>
<key>CFBundleIdentifier</key><string>dev.nativeee.launcher.stub</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>$VERSION</string>
<key>LSMinimumSystemVersion</key><string>12.0</string>
</dict></plist>
EOF
    else
        APP_SRC="$(find "$BUILD" -maxdepth 3 -name "$EXE_NAME.app" -type d | head -1)"
        [ -d "$APP_SRC" ] || { echo "release_launcher: no $EXE_NAME.app under $BUILD" >&2; exit 1; }
        cp -R "$APP_SRC" "$APP"
    fi
    PLIST="$APP/Contents/Info.plist"
    /usr/libexec/PlistBuddy -c "Set :CFBundleName $LAUNCHER_NAME" "$PLIST"
    /usr/libexec/PlistBuddy -c "Add :CFBundleDisplayName string $LAUNCHER_NAME" "$PLIST" 2>/dev/null || /usr/libexec/PlistBuddy -c "Set :CFBundleDisplayName $LAUNCHER_NAME" "$PLIST"
    /usr/libexec/PlistBuddy -c "Set :CFBundleIdentifier dev.nativeee.launcher.$(printf '%s' "$TITLE_ID" | tr 'A-Z' 'a-z' | tr -c 'a-z0-9.\n' '-')" "$PLIST"
    /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $VERSION" "$PLIST" 2>/dev/null || /usr/libexec/PlistBuddy -c "Add :CFBundleShortVersionString string $VERSION" "$PLIST"
    /usr/libexec/PlistBuddy -c "Delete :CFBundleIconFile" "$PLIST" 2>/dev/null || true
    write_common "$APP/Contents/Resources"

    if [ "$STUB" = 0 ]; then
        step "macdeployqt (Qt frameworks and plugins into the bundle, dynamic)"
        "$QT/bin/macdeployqt" "$APP" -no-strip > "$DIST/macdeployqt.log" 2>&1 || { tail -20 "$DIST/macdeployqt.log"; exit 1; }
        echo "frameworks: $(ls "$APP/Contents/Frameworks" | tr '\n' ' ')"
        otool -L "$APP/Contents/MacOS/$EXE_NAME" | grep -q "QtWidgets" || { echo "release_launcher: the executable does not link QtWidgets dynamically (the LGPL notice would lie)" >&2; exit 1; }
    fi
    step "codesign (${CODESIGN_IDENTITY:-ad hoc})"
    SIGN=(--force --sign "${CODESIGN_IDENTITY:--}" --timestamp=none)
    [ -n "${CODESIGN_IDENTITY:-}" ] && SIGN=(--force --sign "$CODESIGN_IDENTITY" --options runtime --timestamp)
    if [ -d "$APP/Contents/Frameworks" ] || [ -d "$APP/Contents/PlugIns" ]; then
        while IFS= read -r -d '' lib; do codesign "${SIGN[@]}" "$lib" 2>/dev/null; done < <(find "$APP/Contents/Frameworks" "$APP/Contents/PlugIns" -type f \( -name "*.dylib" -o -perm -u+x \) -print0 2>/dev/null)
        while IFS= read -r -d '' fw; do codesign "${SIGN[@]}" "$fw" 2>/dev/null; done < <(find "$APP/Contents/Frameworks" -name "*.framework" -maxdepth 1 -print0 2>/dev/null)
    fi
    codesign "${SIGN[@]}" "$APP"
    codesign --verify --deep --strict "$APP" && echo "codesign: verified ($(codesign -dv "$APP" 2>&1 | grep -E '^Signature=' || echo 'Signature=?'))"
    if [ "$STUB" = 0 ]; then
        "$APP/Contents/MacOS/$EXE_NAME" --help > /dev/null 2>&1 || { echo "release_launcher: the signed bundle does not start" >&2; exit 1; }
        echo "bundle starts: ok"
    fi
    STAGED="$APP"
else
    APPDIR="$DIST/AppDir"
    step "stage $APPDIR (AppImage layout)"
    mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps" "$APPDIR/usr/share/$PRODUCT_LC-launcher"
    if [ "$STUB" = 1 ]; then
        printf '#!/bin/sh\n# STUB launcher -- a pipeline dry run placeholder\ncat "$(dirname "$0")/../share/%s-launcher/BUILD-ID"\n' "$PRODUCT_LC" > "$APPDIR/usr/bin/$EXE_NAME"; chmod +x "$APPDIR/usr/bin/$EXE_NAME"
    else
        EXE_SRC="$(find "$BUILD" -maxdepth 3 -name "$EXE_NAME" -type f -perm -u+x | head -1)"
        [ -f "$EXE_SRC" ] || { echo "release_launcher: no $EXE_NAME under $BUILD" >&2; exit 1; }
        cp "$EXE_SRC" "$APPDIR/usr/bin/$EXE_NAME"
    fi
    write_common "$APPDIR/usr/share/$PRODUCT_LC-launcher"
    # the launcher looks for titles/ beside the executable or in the app's resources; the AppImage's resources
    # are usr/share/<product>-launcher/ -- a titles/ link beside the executable makes the "beside" rule hold.
    ln -sfn "../share/$PRODUCT_LC-launcher/titles" "$APPDIR/usr/bin/titles"
    cat > "$APPDIR/usr/share/applications/$PRODUCT_LC-launcher.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=$LAUNCHER_NAME
Exec=$EXE_NAME
Icon=$PRODUCT_LC-launcher
Categories=Game;
Terminal=false
EOF
    # a plain icon (no artwork of the game anywhere): a 256x256 PNG drawn here
    python3 - "$APPDIR/usr/share/icons/hicolor/256x256/apps/$PRODUCT_LC-launcher.png" <<'EOF'
import struct, sys, zlib
w = h = 256
rows = []
for y in range(h):
    row = bytearray([0])
    for x in range(w):
        d = ((x - 128) ** 2 + (y - 128) ** 2) ** 0.5
        inside = d < 112
        row += bytes([36, 52, 92, 255]) if inside else bytes([0, 0, 0, 0])
    rows.append(bytes(row))
raw = b"".join(rows)
def chunk(t, b): return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xffffffff)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
EOF
    ln -sfn "usr/share/icons/hicolor/256x256/apps/$PRODUCT_LC-launcher.png" "$APPDIR/$PRODUCT_LC-launcher.png"
    ln -sfn "usr/share/applications/$PRODUCT_LC-launcher.desktop" "$APPDIR/$PRODUCT_LC-launcher.desktop"
    STAGED="$APPDIR"
fi

# --- 4. the asset scan ---------------------------------------------------------------------------------------------
step "asset scan over the staged folder (zero findings)"
python3 "$ENGINE/tools/ci/asset_scan.py" "$DIST" --report "$DIST_ROOT/asset_scan_launcher-$PLATFORM.md"

# --- 5. the image -------------------------------------------------------------------------------------------------
OUT_BASE="$PRODUCT_LC-launcher-$VERSION-$PLATFORM"
if [ "$IMAGE" = 1 ]; then
    if [ "$OS" = macos ]; then
        step "dmg"
        OUT="$DIST_ROOT/$OUT_BASE.dmg"; rm -f "$OUT" "$OUT.sha256"
        STAGE="$(mktemp -d)"; cp -R "$APP" "$STAGE/"; ln -s /Applications "$STAGE/Applications"
        cp "$APP/Contents/Resources/THIRD_PARTY_NOTICES.md" "$STAGE/THIRD_PARTY_NOTICES.md"
        hdiutil create -quiet -volname "$LAUNCHER_NAME" -srcfolder "$STAGE" -ov -format UDZO "$OUT"
        rm -rf "$STAGE"
        if [ -n "${CODESIGN_IDENTITY:-}" ]; then
            codesign --force --sign "$CODESIGN_IDENTITY" --timestamp "$OUT"
            if [ -n "${NOTARY_PROFILE:-}" ]; then
                step "notarize (xcrun notarytool, profile $NOTARY_PROFILE) + staple"
                xcrun notarytool submit "$OUT" --keychain-profile "$NOTARY_PROFILE" --wait
                xcrun stapler staple "$OUT"
            else
                echo "notarize: skipped (NOTARY_PROFILE not set) -- Gatekeeper will ask the player to right-click > Open once"
            fi
        else
            echo "dmg signing / notarization: skipped (CODESIGN_IDENTITY not set; ad hoc bundle)"
        fi
    else
        step "AppImage (linuxdeploy + linuxdeploy-plugin-qt, pinned)"
        PINS="$ENGINE/tools/toolchain/PACKAGING_PINS.sha256"
        [ -n "$PINS_CACHE" ] || PINS_CACHE="$DIST_ROOT/.cache"; mkdir -p "$PINS_CACHE"
        fetch_pinned() {  # $1 = file name in the pins, $2 = url
            local want got; want="$(grep " $1\$" "$PINS" | cut -d' ' -f1)"
            [ -n "$want" ] || { echo "release_launcher: $1 not pinned in $PINS" >&2; exit 1; }
            if [ ! -f "$PINS_CACHE/$1" ] || [ "$(sha256_file "$PINS_CACHE/$1" | cut -d' ' -f1)" != "$want" ]; then
                curl -fSL --retry 3 -o "$PINS_CACHE/$1.part" "$2" && mv "$PINS_CACHE/$1.part" "$PINS_CACHE/$1"
            fi
            got="$(sha256_file "$PINS_CACHE/$1" | cut -d' ' -f1)"
            [ "$got" = "$want" ] || { rm -f "$PINS_CACHE/$1"; echo "release_launcher: sha256 MISMATCH for $1 ($got != $want); deleted" >&2; exit 1; }
            chmod +x "$PINS_CACHE/$1"; echo "$1: verified"
        }
        fetch_pinned linuxdeploy-1-alpha-20251107-1-x86_64.AppImage https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage
        fetch_pinned linuxdeploy-plugin-qt-1-alpha-20250213-1-x86_64.AppImage https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage
        OUT="$DIST_ROOT/$OUT_BASE.AppImage"; rm -f "$OUT" "$OUT.sha256"
        # linuxdeploy finds the plugin by name on PATH; APPIMAGE_EXTRACT_AND_RUN: no FUSE on a runner / in a container
        export APPIMAGE_EXTRACT_AND_RUN=1 QMAKE="${QMAKE:-}" OUTPUT="$OUT" PATH="$PINS_CACHE:$PATH"
        ln -sfn "$PINS_CACHE/linuxdeploy-plugin-qt-1-alpha-20250213-1-x86_64.AppImage" "$PINS_CACHE/linuxdeploy-plugin-qt-x86_64.AppImage"
        plugin_args=(); [ "$STUB" = 0 ] && plugin_args=(--plugin qt)
        (cd "$DIST_ROOT" && "$PINS_CACHE/linuxdeploy-1-alpha-20251107-1-x86_64.AppImage" --appdir "$APPDIR" \
            --desktop-file "$APPDIR/usr/share/applications/$PRODUCT_LC-launcher.desktop" \
            --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/$PRODUCT_LC-launcher.png" \
            ${plugin_args[@]+"${plugin_args[@]}"} --output appimage > "$DIST/linuxdeploy.log" 2>&1) || { tail -30 "$DIST/linuxdeploy.log"; exit 1; }
        [ -f "$OUT" ] || { ls "$DIST_ROOT"; echo "release_launcher: linuxdeploy wrote no $OUT" >&2; exit 1; }
        chmod +x "$OUT"
        python3 "$ENGINE/tools/ci/asset_scan.py" "$OUT" > /dev/null
    fi
    (cd "$DIST_ROOT" && sha256_file "$(basename "$OUT")" > "$(basename "$OUT").sha256" && cat "$(basename "$OUT").sha256")
    echo "image: $OUT ($(( $(stat -f %z "$OUT" 2>/dev/null || stat -c %s "$OUT") / 1048576 )) MB)"
fi
echo "staged: $STAGED ($(du -sh "$STAGED" | cut -f1))"
echo; echo "release_launcher: OK -> $STAGED$( [ "$STUB" = 1 ] && echo " (STUB)" )"
