#!/usr/bin/env bash
# tools/ci/stage_engine_bundle.sh -- stage the ENGINE BUNDLE a release ships per platform (D-A): everything the
# launcher's prepare step needs to turn the player's own disc into a game program ON THE PLAYER'S MACHINE, and
# nothing derived from any disc. Source: a NO-DISC build (build.sh --no-disc / build.ps1 -NoDisc) -- by
# construction such a build never saw a game file, and the asset scan over the staged folder proves it again.
#
#   stage_engine_bundle.sh --engine DIR --games DIR --build-dir DIR --out DIR [--platform KEY] [--version TAG]
#                          [--name FOLDER] [--compiler-json FILE] [--no-archive]
#
# One script for the three OS lanes (Windows: Git Bash, `shell: bash`; python3 or python; tar + xz on all three).
# --name: the folder / archive base name. Default engine-bundle-<platform> = what tools/prepare (nativeee-prepare)
# asks a release for (`engine-bundle-<platform>.tar.xz`); the archive is .tar.xz (`tar -xf` everywhere).
#
# Two routes:
#   A. the engine's `engine_bundle` CMake target exists in the build dir (cmake/EngineBundle.cmake, tools/dist/
#      engine_bundle.py; probed with `ninja -t targets all`): `cmake --build <dir> --target engine_bundle` is run and
#      its output folder (${PS2R_BUNDLE_DIR}/engine-bundle-<platform>/, default <build-dir>/dist/) is taken AS IS --
#      the target owns the shape and BUNDLE.json (the EXACT command templates tools/prepare replays). NOTE: that
#      target defines itself only in a build that has the title's recompiled code (a DISC build: it reads the real
#      shard / link commands from build.ninja), so on a public runner it never exists and route B runs; a self-hosted
#      lane with the disc takes route A. Either way the folder is asset-scanned here (route A's own scan too).
#   B. the target does not exist yet (this script predates it): the FALLBACK shape below is staged, so CI and the
#      local dry run produce a real, scanned, sized artefact today. The two routes must converge: when A lands,
#      the fallback is deleted here and the key names below are the proposal it was built from.
#
# Fallback shape (route B):  engine-bundle-<platform>/
#   BUNDLE.json           format 0 (fallback): engine commit, platform, the libs IN LINK ORDER as the no-disc nativeee
#                         was linked (read from `ninja -t commands nativeee`), the system frameworks / libs on that
#                         line, the include roots, the recompiled-shard compile flags (games/<title>/recomp/CMakeLists.txt),
#                         the tools, the title id, what is `external` (an SDL2 the build found on the system: the
#                         release lanes configure with the *-dist presets so SDL2 is static and IN lib/)
#   BUILD-ID              `nativeee --version` of the no-disc build (the stub: it says it has no play composition)
#   lib/                  every libps2r_*.a of the build (engine + app + the title's HLE library) + libSDL2*.a when
#                         the build fetched SDL2 (dist presets); NEVER libps2r_title_recomp / _iop_* / _play (they do
#                         not exist in a no-disc build -- their presence fails the staging)
#   bin/                  ps2r_disc_extract, ps2r_elf_embedded, ps2r_recompiler_v2, ps2r_iop_recompiler, vu1_aot_pack
#   include/              the engine's headers (engine/, app/, third_party/, tools/ -- *.h *.hpp *.inl), relative to
#                         the engine root exactly as the sources include them ("engine/core/...")
#   tools/recompiler_v2/  continuations.py (the generation step's post-pass)
#   games/<title>/        the title's SOURCE module (public repository content: hle/, recomp/CMakeLists + inputs/,
#                         verify/, TITLE_INPUTS.sha256, ...; the play composition is compiled on the player's machine
#                         with the recompiled code) -- scanned like everything else
#   LICENSE-NativeEE.txt, THIRD_PARTY_NOTICES.md, licenses/
# then: tools/ci/asset_scan.py over the folder (zero findings or the folder is removed),
#       <out>/<name>.tar.xz + .sha256 (what tools/prepare unpacks with `tar -xf`).
set -euo pipefail
engine=""; games=""; build_dir=""; out=""; platform=""; version=""; compiler_json=""; do_zip=1; name=""
PY="$(command -v python3 || command -v python || true)"; [ -n "$PY" ] || { echo "stage_engine_bundle: no python3 / python" >&2; exit 2; }
while [ $# -gt 0 ]; do
    case "$1" in
        --engine) engine="$2"; shift 2 ;;
        --games) games="$2"; shift 2 ;;
        --build-dir) build_dir="$2"; shift 2 ;;
        --out) out="$2"; shift 2 ;;
        --platform) platform="$2"; shift 2 ;;
        --version) version="$2"; shift 2 ;;
        --compiler-json) compiler_json="$2"; shift 2 ;;
        --name) name="$2"; shift 2 ;;
        --no-zip|--no-archive) do_zip=0; shift ;;
        -h|--help) sed -n '2,42p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "stage_engine_bundle: unknown argument $1" >&2; exit 2 ;;
    esac
done
[ -n "$engine" ] && [ -n "$games" ] && [ -n "$build_dir" ] && [ -n "$out" ] || { echo "stage_engine_bundle: --engine, --games, --build-dir, --out are required" >&2; exit 2; }
engine="$(cd "$engine" && pwd -P)"; games="$(cd "$games" && pwd -P)"; build_dir="$(cd "$build_dir" && pwd -P)"
mkdir -p "$out"; out="$(cd "$out" && pwd -P)"
step() { echo; echo "=== $*"; }
sha256_file() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"; else shasum -a 256 "$1"; fi; }
if [ -z "$platform" ]; then
    case "$(uname -s)-$(uname -m)" in
        Darwin-arm64) platform=macos-arm64 ;; Darwin-x86_64) platform=macos-x86_64 ;;
        Linux-x86_64) platform=linux-x86_64 ;;
        MINGW*|MSYS*|CYGWIN*) platform=windows-x86_64 ;;
        *) echo "stage_engine_bundle: unsupported host (D-B)" >&2; exit 2 ;;
    esac
fi
[ -n "$version" ] || version="$(git -C "$engine" describe --tags --always 2>/dev/null || git -C "$engine" rev-parse --short HEAD)"
[ -n "$name" ] || name="engine-bundle-$platform"
dist="$out/$name"

# --- the no-disc proof, before anything is copied -------------------------------------------------------------------
step "the build dir is a NO-DISC build ($build_dir)"
if grep -qE '^NATIVEEE_DISC:[A-Z]+=.+$|^NATIVEEE_TITLE_ELF:[A-Z]+=.+$' "$build_dir/CMakeCache.txt"; then
    echo "stage_engine_bundle: REFUSING -- the build dir was configured WITH a disc / ELF (its libraries may be game-derived):" >&2
    grep -E '^NATIVEEE_(DISC|TITLE_ELF):' "$build_dir/CMakeCache.txt" >&2; exit 1
fi
if find "$build_dir" \( -name 'libps2r_title_recomp*' -o -name 'libps2r_title_iop_*' -o -name 'libps2r_title_play*' -o -name 'recompiled*.cpp' -o -name 'iop_recompiled*' \) -print -quit | grep -q .; then
    echo "stage_engine_bundle: REFUSING -- recompiled title code in the build dir (not a no-disc build)" >&2; exit 1
fi
echo "no NATIVEEE_DISC / NATIVEEE_TITLE_ELF in the cache; no recompiled title code in the tree"

# --- route A: the engine_bundle target -----------------------------------------------------------------------------
rm -rf "$dist"
if (cd "$build_dir" && ninja -t targets all 2>/dev/null | grep -qE '^engine_bundle:'); then
    step "route A: cmake --build --target engine_bundle"
    cmake --build "$build_dir" --target engine_bundle
    src="$(grep -E '^PS2R_BUNDLE_DIR:' "$build_dir/CMakeCache.txt" | cut -d= -f2-)"; [ -n "$src" ] || src="$build_dir/dist"
    src="$src/$(ls "$src" | grep -E '^engine-bundle-' | head -1)"
    [ -f "$src/BUNDLE.json" ] || { echo "stage_engine_bundle: the engine_bundle target left no engine-bundle-*/BUNDLE.json under $(dirname "$src")" >&2; exit 1; }
    mkdir -p "$out"; cp -R "$src" "$dist"
    route=A
else
    # --- route B: the fallback shape ---------------------------------------------------------------------------------
    step "route B: no engine_bundle target in the build dir -- staging the fallback shape"
    route=B
    bin="$build_dir/app/host/nativeee"; [ -f "$bin" ] || bin="$build_dir/app/host/nativeee.exe"
    [ -x "$bin" ] || { echo "stage_engine_bundle: no nativeee at $build_dir/app/host (build first)" >&2; exit 1; }
    mkdir -p "$dist/lib" "$dist/bin" "$dist/include" "$dist/tools/recompiler_v2" "$dist/licenses"
    "$bin" --version > "$dist/BUILD-ID"; cat "$dist/BUILD-ID"

    # the title: the one module in games/ (one with hle/), or PS2R_TITLE
    title_src=""
    if [ -n "${PS2R_TITLE:-}" ]; then title_src="$games/$PS2R_TITLE"; else
        for cand in "$games"/*/; do [ -d "${cand}hle" ] || continue
            [ -z "$title_src" ] || { echo "stage_engine_bundle: several titles in $games; set PS2R_TITLE" >&2; exit 1; }
            title_src="${cand%/}"; done
    fi
    [ -d "$title_src" ] || { echo "stage_engine_bundle: no title module under $games" >&2; exit 1; }
    title_id="$(basename "$title_src")"

    # lib/: every libps2r_*.a (not the tests' fakes), + the fetched static SDL2 when present
    while IFS= read -r -d '' a; do cp "$a" "$dist/lib/"; done < <(find "$build_dir" -type f \( -name 'libps2r_*.a' -o -name 'ps2r_*.lib' \) -not -path '*/tests/*' -print0)
    sdl_static="$(find "$build_dir" -type f \( -name 'libSDL2.a' -o -name 'libSDL2-static.a' -o -name 'libSDL2main.a' -o -name 'SDL2-static.lib' -o -name 'SDL2.lib' -o -name 'SDL2.dll' \) 2>/dev/null | head -5 || true)"
    sdl_in_bundle=0
    if [ -n "$sdl_static" ]; then printf '%s\n' "$sdl_static" | while read -r f; do cp "$f" "$dist/lib/"; done; sdl_in_bundle=1; fi
    # the *-dist presets' static FFmpeg (engine/tools/ci/ffmpeg_static.sh: LGPL recipe, mpeg1/mpeg2 decoders only; ~2 MB):
    # on the observed link line, so in the bundle with its texts + the configure record (the linker drops it for a title
    # whose FMV path is the IPU model -- stage_dist.sh proves that for the shipped binary; here the LINE must be complete)
    ffmpeg_in_bundle=0
    if [ -d "$build_dir/third_party/ffmpeg/lib" ]; then
        cp "$build_dir"/third_party/ffmpeg/lib/libav*.a "$build_dir"/third_party/ffmpeg/lib/libsw*.a "$dist/lib/" 2>/dev/null && ffmpeg_in_bundle=1
        cp "$engine/tools/dist/licenses/FFmpeg-LICENSE.md" "$engine/tools/dist/licenses/FFmpeg-COPYING.LGPLv2.1.txt" "$dist/licenses/"
        { echo "FFmpeg $(cat "$build_dir/third_party/ffmpeg/PS2R_FFMPEG_TAG" 2>/dev/null) built by engine/tools/ci/ffmpeg_static.sh with:"; cat "$build_dir/third_party/ffmpeg/PS2R_FFMPEG_CONFIGURE" 2>/dev/null; } > "$dist/licenses/FFmpeg-BUILD-CONFIGURATION.txt"
    fi
    echo "lib/: $(ls "$dist/lib" | wc -l | tr -d ' ') archives ($(du -sh "$dist/lib" | cut -f1))"

    # bin/: the tools the prepare step runs (extract, embedded modules, the two recompilers, the VU1 pack tool)
    for t in disc_extract/ps2r_disc_extract elf_embedded/ps2r_elf_embedded recompiler_v2/ps2r_recompiler_v2 iop_recompiler/ps2r_iop_recompiler vu1_aot_pack/vu1_aot_pack; do
        f="$build_dir/tools/$t"; [ -f "$f" ] || f="$f.exe"
        [ -f "$f" ] || { echo "stage_engine_bundle: missing tool $t (build it: it is part of the no-disc lane)" >&2; exit 1; }
        cp "$f" "$dist/bin/"
    done
    echo "bin/: $(ls "$dist/bin" | tr '\n' ' ')"

    # include/: the headers, relative to the engine root as the sources include them
    (cd "$engine" && find engine app third_party tools -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.inl' \) -not -path 'tests/*' -print0 \
        | while IFS= read -r -d '' h; do mkdir -p "$dist/include/$(dirname "$h")"; cp "$h" "$dist/include/$h"; done)
    echo "include/: $(find "$dist/include" -type f | wc -l | tr -d ' ') headers ($(du -sh "$dist/include" | cut -f1))"
    cp "$engine/tools/recompiler_v2/continuations.py" "$dist/tools/recompiler_v2/"

    # games/<title>/: the title's source module, minus nothing but VCS metadata (public repository content)
    mkdir -p "$dist/games"
    (cd "$games" && tar -cf - --exclude .git --exclude '__pycache__' "$title_id") | (cd "$dist/games" && tar -xf -)
    echo "games/$title_id: $(find "$dist/games" -type f | wc -l | tr -d ' ') files ($(du -sh "$dist/games" | cut -f1))"

    # licences
    cp "$engine/LICENSE" "$dist/LICENSE-NativeEE.txt"
    for f in SDL2-LICENSE.txt imgui-LICENSE.txt imgl3w-UNLICENSE.txt; do cp "$engine/tools/dist/licenses/$f" "$dist/licenses/$f"; done
    case "$platform" in linux-*) for f in GCC-COPYING3.txt GCC-RUNTIME-LIBRARY-EXCEPTION.txt; do cp "$engine/tools/dist/licenses/$f" "$dist/licenses/$f"; done ;; esac
    {
        echo "# NativeEE engine bundle -- third-party notices -- $(cat "$dist/BUILD-ID")"
        echo
        echo "This folder is the NativeEE engine compiled for $platform, the tools that read a disc and regenerate a"
        echo "title's program code, the engine's headers and the title's source module (games/$title_id). It contains"
        echo "NO game data and nothing compiled from any disc: it was staged from a build that never saw one, and the"
        echo "engine's asset scan ran over it. The game program is compiled on the player's machine from the player's own"
        echo "disc by the launcher's \"Preparing your game\" step, with the compiler kit shipped beside this bundle."
        echo
        echo "- NativeEE (lib/libps2r_*.a, bin/, include/) -- MIT (LICENSE-NativeEE.txt)."
        echo "- the title module (games/$title_id) -- the title repository's licence (its LICENSE)."
        echo "- SDL2 -- zlib licence (licenses/SDL2-LICENSE.txt)$( [ "$sdl_in_bundle" = 1 ] && echo ", in lib/ (static)" || echo "; NOT in this bundle: this build found the system's (see BUNDLE.json external)" )."
        [ "$ffmpeg_in_bundle" = 1 ] && echo "- FFmpeg (lib/libavcodec.a, libavutil.a, libswscale.a) -- LGPL v2.1 or later (licenses/FFmpeg-COPYING.LGPLv2.1.txt, FFmpeg-LICENSE.md); built from the pinned tag by engine/tools/ci/ffmpeg_static.sh with ONLY the MPEG-1/2 video decoders, no GPL / non-free component (licenses/FFmpeg-BUILD-CONFIGURATION.txt). Static; LGPL section 6 is met by the published recipe + this bundle: the player's link can swap it."
        echo "- Dear ImGui (lib/libps2r_imgui.a, include/app/gui/third_party/imgui) -- MIT (licenses/imgui-LICENSE.txt);"
        echo "  its gl3w-derived loader header -- public domain (licenses/imgl3w-UNLICENSE.txt)."
        case "$platform" in linux-*) echo "- libstdc++ / libgcc, linked statically by the linux-dist preset -- GPLv3 with the GCC Runtime Library Exception (licenses/)." ;; esac
    } > "$dist/THIRD_PARTY_NOTICES.md"

    # BUNDLE.json (format 0 = fallback): the link order and flags READ from the build, not typed in
    link_cmd="$(cd "$build_dir" && ninja -t commands nativeee 2>/dev/null | tail -1 || true)"
    hle_cmd="$(cd "$build_dir" && ninja -t commands ps2r_title_hle 2>/dev/null | grep -m1 -E -- ' (-c|/c) ' || true)"
    "$PY" - "$dist" "$platform" "$version" "$engine" "$title_id" "$build_dir" "$link_cmd" "$hle_cmd" "$sdl_in_bundle" "$compiler_json" <<'EOF'
import json, os, re, subprocess, sys
dist, platform, version, engine, title_id, build_dir, link_cmd, hle_cmd, sdl_in_bundle, compiler_json = sys.argv[1:]
def git(*a):
    try: return subprocess.check_output(["git", "-C", engine] + list(a), text=True).strip()
    except Exception: return ""
toks = link_cmd.split()
libs, frameworks, syslibs, dynlibs = [], [], [], []
i = 0
while i < len(toks):
    t = toks[i]
    if re.search(r"\.(dylib|so)(\.\d+)*$", t):
        if t not in dynlibs: dynlibs.append(t)
    elif t.endswith(".a") or t.endswith(".lib"):
        base = os.path.basename(t)
        if base not in libs: libs.append(base)
    elif t.startswith("/DEFAULTLIB:") or t.lower().endswith(".dll"):
        if t not in syslibs: syslibs.append(t)
    elif t == "-framework" and i + 1 < len(toks):
        if toks[i+1] not in frameworks: frameworks.append(toks[i+1]); i += 1
    elif t.startswith("-l") and t not in syslibs:
        syslibs.append(t)
    i += 1
in_bundle = set(os.listdir(os.path.join(dist, "lib")))
link_order = [l for l in libs if l in in_bundle]
external = sorted(set(libs) - in_bundle)
hle_flags = sorted({t for t in hle_cmd.split() if re.match(r"^-(std=|O\d|f|D(?!NDEBUG)|isysroot)", t)})
doc = {
  "format": 0,
  "note": "FALLBACK shape staged by engine/tools/ci/stage_engine_bundle.sh (route B) until the engine_bundle CMake target lands; the key names are the proposal.",
  "engine": {"commit": git("rev-parse", "HEAD"), "describe": version, "branch": git("rev-parse", "--abbrev-ref", "HEAD")},
  "platform": platform,
  "title": {"id": title_id, "module": "games/%s" % title_id, "inputs_sha256": "games/%s/TITLE_INPUTS.sha256" % title_id},
  "compiler": (json.load(open(compiler_json)) if compiler_json and os.path.exists(compiler_json) else {"note": "see the compiler kit shipped beside this bundle (compiler-<platform>.tar.xz, COMPILER.json)"}),
  "include_roots": ["include", "."],
  "shard_compile_flags": ["-std=c++17", "-O2", "-fno-strict-aliasing", "-fwrapv", "-ffp-contract=off", "-w"],
  "hle_compile_flags_observed": hle_flags,
  "tools": {k: "bin/%s" % k for k in ["ps2r_disc_extract", "ps2r_elf_embedded", "ps2r_recompiler_v2", "ps2r_iop_recompiler", "vu1_aot_pack"]},
  "post_pass": {"continuations": "tools/recompiler_v2/continuations.py"},
  "link": {
    "libs_in_order": ["lib/%s" % l for l in link_order],
    "frameworks": frameworks,
    "system_libs": syslibs,
    "external": external,
    "external_dynamic": dynlibs,
    "observed_from": "ninja -t commands nativeee (the no-disc stub's link line; the player's link adds the recompiled objects + the play composition in front)",
  },
  "sdl2_in_bundle": sdl_in_bundle == "1",
}
if "compiler" in doc and isinstance(doc["compiler"], dict):
    doc["compiler"].pop("files", None); doc["compiler"].pop("members", None)
json.dump(doc, open(os.path.join(dist, "BUNDLE.json"), "w"), indent=1)
print("BUNDLE.json: %d libs in link order, %d external static (%s), %d external dynamic (%s), frameworks %s" % (len(link_order), len(external), " ".join(external) or "-", len(dynlibs), " ".join(os.path.basename(d) for d in dynlibs) or "-", " ".join(frameworks) or "-"))
if dynlibs:
    print("NOTE: a release lane must configure with the *-dist preset so these are static and in lib/ (the player's machine has no Homebrew SDL2)")
EOF
fi

# --- the gate: zero game-derived bytes -----------------------------------------------------------------------------
step "asset scan ($dist)"
scan_args=()
windows_json="$(ls "$games"/*/asset_scan_windows.json 2>/dev/null | head -1 || true)"
[ -n "$windows_json" ] && scan_args+=(--windows "$windows_json")
if ! "$PY" "$engine/tools/ci/asset_scan.py" "$dist" ${scan_args[@]+"${scan_args[@]}"} --report "$out/asset_scan_$name.md"; then
    echo "stage_engine_bundle: REFUSING -- the asset scan found game data in $dist (report: $out/asset_scan_$name.md)" >&2
    rm -rf "$dist"; exit 1
fi

step "artefact ($dist, route $route)"
du -sh "$dist"; ls -la "$dist"
if [ "$do_zip" = 1 ]; then
    rm -f "$out/$name.tar.xz" "$out/$name.tar.xz.sha256"
    (cd "$out" && tar -cf - "$name" | xz -T0 -6 > "$name.tar.xz")
    "$PY" "$engine/tools/ci/asset_scan.py" "$out/$name.tar.xz" ${scan_args[@]+"${scan_args[@]}"} > /dev/null || { echo "stage_engine_bundle: REFUSING -- the asset scan found game data in the archive" >&2; rm -f "$out/$name.tar.xz"; rm -rf "$dist"; exit 1; }
    (cd "$out" && sha256_file "$name.tar.xz" > "$name.tar.xz.sha256" && cat "$name.tar.xz.sha256")
    echo "archive: $out/$name.tar.xz ($(( $(stat -f %z "$out/$name.tar.xz" 2>/dev/null || stat -c %s "$out/$name.tar.xz") / 1048576 )) MB)"
fi
echo "stage_engine_bundle: OK -> $dist"
