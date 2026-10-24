#!/usr/bin/env bash
# tools/toolchain/fetch_compiler.sh -- the portable compiler+linker a release ships (D-A: the player's game program
# is compiled on the player's machine; the release carries the compiler, never anything compiled from a disc).
#
#   fetch_compiler.sh --platform <macos-arm64|macos-x86_64|linux-x86_64|windows-x64|auto> --out DIR
#                     [--cache DIR] [--pack] [--smoke] [--verify-only] [--offline]
#
# What it does, in order:
#   1. resolves the platform's kit in tools/toolchain/COMPILER_PINS.sha256 (an OFFICIAL LLVM release asset, its
#      sha256 pinned there with its provenance);
#   2. downloads it into --cache (default $NATIVEEE_TOOLCHAIN_CACHE, else <out>/.cache; resumable; skipped when the
#      cached file already matches) and VERIFIES the sha256 -- a mismatch deletes the file and fails;
#   3. cuts the SLIM subset into <out>/compiler/: the compiler driver (clang / clang-cl), the linker (Linux: lld;
#      Windows: lld-link; macOS: NONE -- Apple's ld from the Command Line Tools, which the SDK needs anyway, because
#      lld's ld64 flavour cannot read the macOS 26 SDK's TBD stubs), the archiver (llvm-ar / llvm-lib), the compiler's own resource headers
#      (lib/clang/<major>/include -- stdint.h, stddef.h, the intrinsics; without them the driver compiles nothing)
#      and LICENSE.TXT (Apache-2.0 WITH LLVM-exception). The official kit is 0.9-1.9 GB compressed; the subset is
#      ~100 MB packed, and it is ALL the prepare step needs from LLVM;
#   4. writes <out>/compiler/COMPILER.json (what was cut, from which kit, which hash; the launcher reads `bin`);
#   5. --smoke (native platform only): the cut clang compiles and the cut lld links a one-file C++ program;
#   6. --pack: <out>/nativeee-compiler-<platform>-llvm<version>.zip + .sha256 (zip on EVERY platform: it is what the
#      launcher unpacks with `tar -xf` (bsdtar, macOS / Windows) or `unzip` (Linux); `zip -ry` keeps modes + links).
#
# The subset does NOT include a C library or an SDK: the system headers and link stubs are the OS's (macOS: Apple's
# Command Line Tools / Xcode SDK; Linux: libc6-dev; Windows: the Windows SDK + MSVC CRT) -- they are not ours to
# redistribute. The prepare step must check for them and say so plainly (an open item it owns).
#
# Windows: the twin fetch_compiler.ps1 does the same with tar.exe (bsdtar, Windows 10+ reads .tar.xz) and
# Compress-Archive. This script also cuts the WINDOWS kit on macOS / Linux (for a cross-platform release job);
# --smoke is refused there.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PINS="$HERE/COMPILER_PINS.sha256"
platform=auto; out=""; cache="${NATIVEEE_TOOLCHAIN_CACHE:-}"; pack=0; smoke=0; verify_only=0; offline=0
while [ $# -gt 0 ]; do
    case "$1" in
        --platform) platform="$2"; shift 2 ;;
        --out) out="$2"; shift 2 ;;
        --cache) cache="$2"; shift 2 ;;
        --pack) pack=1; shift ;;
        --smoke) smoke=1; shift ;;
        --verify-only) verify_only=1; shift ;;
        --offline) offline=1; shift ;;
        -h|--help) sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "fetch_compiler: unknown argument $1" >&2; exit 2 ;;
    esac
done
step() { echo; echo "=== $*"; }
sha256_of() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

host_platform() {
    case "$(uname -s)-$(uname -m)" in
        Darwin-arm64) echo macos-arm64 ;;
        Darwin-x86_64) echo macos-x86_64 ;;
        Linux-x86_64) echo linux-x86_64 ;;
        MINGW*|MSYS*|CYGWIN*) echo windows-x64 ;;
        *) echo "fetch_compiler: unsupported host $(uname -s)-$(uname -m) (D-B: macOS arm64/x86_64, Linux x86_64, Windows x64)" >&2; exit 2 ;;
    esac
}
[ "$platform" = auto ] && platform="$(host_platform)"
case "$platform" in
    macos-arm64)  kit_re='^LLVM-[0-9.]+-macOS-ARM64\.tar\.xz$' ;;
    macos-x86_64) kit_re='^LLVM-[0-9.]+-macOS-X64\.tar\.xz$' ;;
    linux-x86_64) kit_re='^LLVM-[0-9.]+-Linux-X64\.tar\.xz$' ;;
    windows-x64)  kit_re='^clang\+llvm-[0-9.]+-x86_64-pc-windows-msvc\.tar\.xz$' ;;
    *) echo "fetch_compiler: unknown platform $platform" >&2; exit 2 ;;
esac
row="$(grep -v '^#' "$PINS" | awk 'NF==2' | grep -E " $(printf '%s' "$kit_re" | sed 's/\^//; s/\$$//')$" || true)"
[ -n "$row" ] || { echo "fetch_compiler: no row for $platform in $PINS" >&2; exit 1; }
[ "$(printf '%s\n' "$row" | wc -l | tr -d ' ')" = 1 ] || { echo "fetch_compiler: several rows match $platform in $PINS" >&2; exit 1; }
want_sha="${row%% *}"; kit="${row##* }"
version="$(printf '%s' "$kit" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
major="${version%%.*}"
url="https://github.com/llvm/llvm-project/releases/download/llvmorg-$version/$kit"
echo "platform $platform  kit $kit  llvm $version  sha256 $want_sha"
if [ "$verify_only" = 1 ]; then
    [ -n "$cache" ] || { echo "fetch_compiler: --verify-only needs --cache" >&2; exit 2; }
    f="$cache/$kit"; [ -f "$f" ] || { echo "fetch_compiler: $f not in the cache" >&2; exit 1; }
    got="$(sha256_of "$f")"; [ "$got" = "$want_sha" ] || { echo "fetch_compiler: sha256 MISMATCH for $f: $got" >&2; exit 1; }
    echo "verified: $f"; exit 0
fi
[ -n "$out" ] || { echo "fetch_compiler: --out DIR is required" >&2; exit 2; }
mkdir -p "$out"; out="$(cd "$out" && pwd -P)"
[ -n "$cache" ] || cache="$out/.cache"
mkdir -p "$cache"; cache="$(cd "$cache" && pwd -P)"
f="$cache/$kit"

step "the pinned kit ($f)"
if [ -f "$f" ] && [ "$(sha256_of "$f")" = "$want_sha" ]; then
    echo "cached and verified"
else
    [ "$offline" = 0 ] || { echo "fetch_compiler: --offline and $kit is not in the cache (or mismatches)" >&2; exit 1; }
    rm -f "$f"
    echo "downloading $url"
    curl -fSL --retry 3 -C - -o "$f.part" "$url" || { rm -f "$f.part"; echo "fetch_compiler: download failed" >&2; exit 1; }
    mv "$f.part" "$f"
    got="$(sha256_of "$f")"
    if [ "$got" != "$want_sha" ]; then
        rm -f "$f"
        echo "fetch_compiler: sha256 MISMATCH for $kit -- got $got, pinned $want_sha. Deleted; refusing." >&2; exit 1
    fi
    echo "downloaded and verified"
fi
ls -la "$f"

# --- the slim subset ------------------------------------------------------------------------------------------
# The member list (relative to the kit's single top folder). Listed, not guessed: a pattern that matches nothing
# in THIS kit is dropped (the official kits differ in which binaries are hard links, symlinks or copies), so the
# cut is reproducible from the manifest written beside it.
step "listing the kit (cached beside it)"
members="$f.members"
if [ ! -s "$members" ]; then
    tar -tf "$f" > "$members.part" && mv "$members.part" "$members"
fi
top="$(head -1 "$members" | cut -d/ -f1)"
echo "top folder: $top  ($(wc -l < "$members" | tr -d ' ') members)"
case "$platform" in
    windows-x64)
        # clang-cl.exe (the driver the windows-clang-cl preset names), lld-link.exe, llvm-lib.exe (the archiver of
        # the MSVC shape); the kit's .exe files are copies, not links, so ONLY the named ones are cut.
        want_re="^$top/(bin/(clang-cl\.exe|lld-link\.exe|llvm-lib\.exe)$|lib/clang/$major/include(/|$)|LICENSE\.TXT$)" ;;
    macos-*)
        # clang -> clang-<major>, clang++ -> clang (symlinks; `tar` restores them), llvm-ar + llvm-ranlib (-> llvm-ar).
        # NO lld: lld $version's ld64 flavour cannot read the TBD stubs of the macOS 26 SDK ("unknown architecture
        # arm64e.x1-macos"), and the SDK comes from Apple's Command Line Tools, which bring Apple's `ld` -- the linker
        # the prepare step uses on macOS (clang's default). Apple's ld warns that lib/libLTO.dylib is absent (it is:
        # LTO is not used on the player's machine); harmless. clang-<major> is statically linked: no libLLVM dylib.
        want_re="^$top/(bin/(clang|clang\+\+|clang-$major|llvm-ar|llvm-ranlib)$|lib/clang/$major/include(/|$)|LICENSE\.TXT$)" ;;
    linux-*)
        # the same + lld / ld.lld (the linker on Linux: no system linker is assumed; -fuse-ld=lld).
        want_re="^$top/(bin/(clang|clang\+\+|clang-$major|lld|ld\.lld|llvm-ar|llvm-ranlib)$|lib/clang/$major/include(/|$)|LICENSE\.TXT$)" ;;
esac
grep -E "$want_re" "$members" > "$out/.cut_members.txt" || true
n="$(wc -l < "$out/.cut_members.txt" | tr -d ' ')"
[ "$n" -gt 0 ] || { echo "fetch_compiler: nothing matched in $kit (regex: $want_re)" >&2; exit 1; }
echo "cutting $n members"

dest="$out/compiler"
rm -rf "$dest"; mkdir -p "$dest"
step "extract -> $dest"
tar -xf "$f" -C "$dest" --strip-components=1 -T "$out/.cut_members.txt"
# a symlink whose target was not cut is a dangling link: refuse (the manifest would lie)
if find "$dest" -type l ! -exec test -e {} \; -print | grep .; then
    echo "fetch_compiler: dangling symlinks above -- the cut is incomplete" >&2; exit 1
fi
du -sh "$dest" | awk '{print "cut size: " $1}'
ls "$dest/bin"

# --- COMPILER.json ----------------------------------------------------------------------------------------------
case "$platform" in
    windows-x64) cc=bin/clang-cl.exe; cxx=bin/clang-cl.exe; ld=bin/lld-link.exe; ar=bin/llvm-lib.exe ;;
    macos-*)     cc=bin/clang; cxx=bin/clang++; ld="/usr/bin/ld (Apple, from the Command Line Tools)"; ar=bin/llvm-ar ;;
    linux-*)     cc=bin/clang; cxx=bin/clang++; ld=bin/ld.lld; ar=bin/llvm-ar ;;
esac
python3 - "$dest" "$platform" "$version" "$kit" "$want_sha" "$url" "$cc" "$cxx" "$ld" "$ar" "$out/.cut_members.txt" <<'EOF'
import json, os, sys
dest, platform, version, kit, sha, url, cc, cxx, ld, ar, members = sys.argv[1:]
files = []
for root, dirs, names in os.walk(dest):
    for n in names:
        p = os.path.join(root, n)
        rel = os.path.relpath(p, dest)
        if rel == "COMPILER.json":
            continue
        st = os.lstat(p)
        files.append({"path": rel, "size": st.st_size, "symlink": os.readlink(p) if os.path.islink(p) else None})
files.sort(key=lambda e: e["path"])
doc = {
    "format": 1,
    "llvm": version,
    "platform": platform,
    "kit": {"name": kit, "sha256": sha, "url": url},
    "bin": {"cc": cc, "cxx": cxx, "ld": ld, "ar": ar},
    "resource_dir": "lib/clang/%s" % version.split(".")[0],
    "licence": "LICENSE.TXT (Apache License v2.0 with LLVM Exceptions)",
    "system_sdk_required": {
        "macos-arm64": "Apple Command Line Tools or Xcode (the macOS SDK: system headers and link stubs)",
        "macos-x86_64": "Apple Command Line Tools or Xcode (the macOS SDK: system headers and link stubs)",
        "linux-x86_64": "libc6-dev (glibc headers) and the system's libstdc++ development headers",
        "windows-x64": "the Windows SDK and the MSVC C runtime headers/libraries (Visual Studio Build Tools)",
    }[platform],
    "cut_by": "engine/tools/toolchain/fetch_compiler.sh",
    "members": [l.strip() for l in open(members) if l.strip()],
    "files": files,
}
json.dump(doc, open(os.path.join(dest, "COMPILER.json"), "w"), indent=1)
print("COMPILER.json: %d files, %.1f MB unpacked" % (len(files), sum(f["size"] for f in files) / 1048576))
EOF
rm -f "$out/.cut_members.txt"

# --- smoke (native only): compile + link a one-file program with the cut tools ------------------------------------
if [ "$smoke" = 1 ]; then
    step "smoke: the cut clang compiles and the platform linker links a one-file program"
    [ "$platform" = "$(host_platform)" ] || { echo "fetch_compiler: --smoke needs the native platform ($platform != $(host_platform))" >&2; exit 1; }
    tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
    cat > "$tmp/hello.cpp" <<'EOF'
#include <cstdio>
#include <cstdint>
#include <vector>
int main() { std::vector<std::uint32_t> v{1, 2, 3}; std::printf("nativeee compiler smoke: %zu\n", v.size()); return 0; }
EOF
    "$dest/$cxx" --version | head -1
    case "$platform" in
        macos-*)
            sdk="${SDKROOT:-$(xcrun --show-sdk-path 2>/dev/null || true)}"
            [ -n "$sdk" ] || { echo "fetch_compiler: no macOS SDK (xcode-select --install)" >&2; exit 1; }
            # the system linker (CR-13); its "-lto_library ... does not exist" warning is expected
            "$dest/$cxx" -isysroot "$sdk" -O2 -std=c++17 "$tmp/hello.cpp" -o "$tmp/hello" 2> >(grep -v "lto_library" >&2) ;;
        linux-*)
            "$dest/$cxx" -fuse-ld=lld -O2 -std=c++17 "$tmp/hello.cpp" -o "$tmp/hello" ;;
    esac
    "$tmp/hello" | grep -q "compiler smoke: 3" && echo "smoke: ok (compiled with $cxx, linked with $ld)"
fi

# --- pack -----------------------------------------------------------------------------------------------------------
if [ "$pack" = 1 ]; then
    step "pack"
    name="nativeee-compiler-$platform-llvm$version"
    rm -f "$out/$name.zip" "$out/$name.zip.sha256"
    rm -rf "$out/$name"; mv "$dest" "$out/$name"
    (cd "$out" && zip -qry "$name.zip" "$name")
    mv "$out/$name" "$dest"
    (cd "$out" && { command -v sha256sum >/dev/null 2>&1 && sha256sum "$name.zip" || shasum -a 256 "$name.zip"; } > "$name.zip.sha256" && cat "$name.zip.sha256")
    echo "zip: $out/$name.zip ($(( $(stat -f %z "$out/$name.zip" 2>/dev/null || stat -c %s "$out/$name.zip") / 1048576 )) MB)"
fi
echo; echo "fetch_compiler: OK -> $dest"
