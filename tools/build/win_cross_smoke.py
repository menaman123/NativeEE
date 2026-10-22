#!/usr/bin/env python3
"""win_cross_smoke.py -- compile the Windows-relevant TUs for x86_64-pc-windows-msvc on a
macOS/Linux dev host, BEFORE CI sees them (Charter 2, Track B2).

  tools/build/win_cross_smoke.py --build build-release --xwin /path/to/xwin-splat \
      [--clang /opt/homebrew/opt/llvm/bin/clang++] [--out /tmp/win_smoke] [-j 4] \
      [--syntax-only] [--select core,host,shard,shim,windows,recomp_common,extra] \
      [--extra path/to/tu.cpp ...] [--prefix engine/ app/ games/ tools/]

What it does: reads the dev host's compile_commands.json (CMake, the macos-release preset),
picks the selected TUs, and re-issues each compile with
  * the given clang (a clang that has the x86_64 Windows target; Apple clang does, Homebrew
    LLVM does) and `--target=x86_64-pc-windows-msvc` instead of `-arch arm64`,
  * the MSVC CRT + Windows SDK headers from an `xwin splat` (`-isystem` crt/include,
    sdk/include/{ucrt,um,shared}) -- the same headers clang-cl on the runner sees,
  * the tree's own flags otherwise unchanged (-std, -I, -D, -O, -Wall -Wextra -Werror,
    -fwrapv, -ffp-contract=off ...), so a Windows-only diagnostic here is the diagnostic the
    windows-release preset (clang-cl + tools/build/clang_cl_shim.py) prints on the runner.
  * `host/host_posix.cpp` is swapped for `host/host_windows.cpp` (+ `windows/platform_windows.cpp`,
    `host/gl_windows.cpp`: the generated GL entry-point table) with the same flags: the Windows
    arm is otherwise never compiled on this host.
  * `-iframework` and `-DPS2R_HAS_METAL=1` (the macOS Metal arm) are dropped; the Windows target
    has neither. With `--sdl-include DIR` (the SDL2 VC development headers, what vcpkg's port
    installs) Homebrew's `-isystem .../include/SDL2` is replaced by DIR, so the SDL TUs are a real
    signal; likewise `--ffmpeg-include DIR` for Homebrew's `/opt/homebrew/include` (FFmpeg).
  * `--all` = every .cpp under engine/ app/ games/ tools/ tests/ third_party/ + every recompiled
    shard + the common TU (the whole tree, compile side; the link side is the windows-cross
    toolchain: engine/platform/windows/toolchain_cross_macos.cmake).
  * PCH plumbing (`-Xclang -include-pch`, `-Winvalid-pch`, the cmake_pch include) is dropped: the
    shard TU includes recompiled.h itself.
Objects land under --out (COFF, `file` says "Intel amd64 COFF"); with --syntax-only no object
is written. The per-TU log is <out>/<tu>.log; the summary is <out>/SUMMARY.tsv.

With --lld-link it also links the PlatformHostShims contract test against the Windows arm into
a PE32+ (every Win32/UCRT import resolved against the SDK import libraries); the .exe is not
run here. What it does NOT do: run anything, or build the GL backend / SDL TUs for real (the
Windows GL loader is the open gap; SDL2 headers on the runner are vcpkg's, not Homebrew's).
Exit 0 iff every selected TU compiled (and the link test, when asked for, linked).
"""
import argparse
import concurrent.futures as cf
import json
import os
import re
import shlex
import subprocess
import sys
import time

DEFAULT_SELECT = "shim,core,host,windows,shard"


def tree_rel(path, tree):
    return os.path.relpath(path, tree) if path.startswith(tree) else path


# The generated recompiled TUs live in the BUILD directory (<build>/generated/games/<title>/recomp/,
# cmake/TitleInputs.cmake), not the tree: they are recognised by the tail of their path.
RECOMP_TAIL = "/recomp/"   # under games/<title>/, whichever title is configured


def is_recomp_tu(path, name):
    return path.replace("\\", "/").endswith(RECOMP_TAIL + name)


def select_entries(entries, tree, select, shard, extra, prefixes):
    want = set(select.split(","))
    picked = []
    for e in entries:
        f = e["file"]
        rel = tree_rel(f, tree)
        if prefixes and any(rel.startswith(p) for p in prefixes) and rel.endswith(".cpp"):
            if rel == "engine/platform/host/host_posix.cpp":
                picked.append(("host-arm", e))
            elif "recomp/recompiled" not in rel:   # the generated TUs are the shard/recomp_common selections
                picked.append(("prefix", e))
        elif "core" in want and rel.startswith("engine/core/") and rel.endswith(".cpp"):
            picked.append(("core", e))
        elif "host" in want and rel in ("engine/platform/host/platform.cpp", "engine/platform/platform.cpp"):
            picked.append(("host", e))
        elif "host" in want and rel == "engine/platform/host/host_posix.cpp":
            picked.append(("host-arm", e))   # swapped for host_windows.cpp below
        elif "shard" in want and (shard < 0 and re.search(r"recompiled_shard_\d+\.cpp$", f) and RECOMP_TAIL in f
                                  or is_recomp_tu(f, f"recompiled_shard_{shard:03d}.cpp")):
            picked.append(("shard", e))
        elif "recomp_common" in want and is_recomp_tu(f, "recompiled.cpp"):
            picked.append(("recomp_common", e))
        elif "extra" in want and any(rel == x or f == x for x in extra):
            picked.append(("extra", e))
    return picked


PCH_DROP = {"-Winvalid-pch"}

# The root CMakeLists.txt's `if(WIN32) add_compile_definitions(...)` list, verbatim: the
# UCRT/<windows.h> surface every TU sees on the Windows target.
WIN_DEFINES = ["-D_CRT_SECURE_NO_WARNINGS", "-D_CRT_NONSTDC_NO_WARNINGS", "-D_USE_MATH_DEFINES",
               "-DNOMINMAX", "-DWIN32_LEAN_AND_MEAN", "-D__PRFCHWINTRIN_H"]


def rewrite(args, tree, clang, xwin, out_dir, obj_name, syntax_only, src_override=None,
            sdl_include="", ffmpeg_include=""):
    new = [clang]
    i = 1
    skip_next = 0
    src = None
    while i < len(args):
        a = args[i]
        if skip_next:
            skip_next -= 1
            i += 1
            continue
        if a == "-arch":
            i += 2
            continue
        if a == "-isysroot":
            i += 2
            continue
        if a == "-Xclang":
            nxt = args[i + 1] if i + 1 < len(args) else ""
            if nxt in ("-include-pch", "-include", "-emit-pch"):
                # -Xclang -include-pch -Xclang <file>  (4 tokens)
                i += 4 if nxt != "-emit-pch" else 2
                continue
            new += [a, nxt]
            i += 2
            continue
        if a in PCH_DROP:
            i += 1
            continue
        if a == "-o":
            i += 2
            continue
        if a == "-c":
            i += 1
            continue
        if a.startswith("-mmacosx-version-min") or a.startswith("--sysroot"):
            i += 1
            continue
        if a == "-iframework":
            i += 2
            continue
        if a == "-DPS2R_HAS_METAL=1":
            new.append("-DPS2R_HAS_METAL=0")
            i += 1
            continue
        if a == "-DPS2R_SIMD=1":
            i += 1   # NEON only; the root CMakeLists forces PS2R_SIMD OFF on x86-64 (scalar twins)
            continue
        if a in ("-isystem", "-I") and i + 1 < len(args):
            d = args[i + 1]
            if sdl_include and ("/include/SDL2" in d or d.endswith("/SDL2")):
                new += ["-isystem", sdl_include]
                i += 2
                continue
            if ffmpeg_include and d.rstrip("/") in ("/opt/homebrew/include", "/usr/local/include"):
                new += ["-isystem", ffmpeg_include]
                i += 2
                continue
        if i == len(args) - 1 and not a.startswith("-"):
            src = a
            i += 1
            continue
        new.append(a)
        i += 1
    if src_override:
        src = src_override
    new += ["--target=x86_64-pc-windows-msvc"] + WIN_DEFINES
    for d in ("crt/include", "sdk/include/ucrt", "sdk/include/um", "sdk/include/shared"):
        new += ["-isystem", os.path.join(xwin, d)]
    # The GNU driver on the MSVC target: match what the windows presets do for the
    # exceptions/RTTI model (clang-cl's defaults are /EHsc /GR; the GNU driver defaults off).
    new += ["-fexceptions", "-fcxx-exceptions"]
    if syntax_only:
        new += ["-fsyntax-only"]
    else:
        new += ["-c", "-o", os.path.join(out_dir, obj_name)]
    new.append(src)
    return new, src


def clang_has_windows_target(clang):
    try:
        p = subprocess.run([clang, "--target=x86_64-pc-windows-msvc", "-fsyntax-only", "-x", "c++", "-"],
                           input=b"int x;", capture_output=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return False
    return p.returncode == 0


def run_one(label, cmd, log_path):
    t0 = time.time()
    with open(log_path, "w") as log:
        log.write("$ " + " ".join(shlex.quote(c) for c in cmd) + "\n\n")
        log.flush()
        p = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT)
    dt = time.time() - t0
    with open(log_path) as log:
        text = log.read()
    n_err = len(re.findall(r"(?:fatal )?error:", text))
    n_warn = len(re.findall(r": warning:", text))
    return label, p.returncode, dt, n_err, n_warn, log_path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", required=True, help="CMake build dir with compile_commands.json (macos-release)")
    ap.add_argument("--xwin", default=os.environ.get("PS2R_XWIN_ROOT", ""), help="xwin splat root (crt/, sdk/); default $PS2R_XWIN_ROOT")
    ap.add_argument("--clang", default="/opt/homebrew/opt/llvm/bin/clang++")
    ap.add_argument("--tree", default=None, help="source tree root (default: derived from the build dir's CMakeCache)")
    ap.add_argument("--out", default="/tmp/win_cross_smoke")
    ap.add_argument("-j", type=int, default=4)
    ap.add_argument("--syntax-only", action="store_true")
    ap.add_argument("--select", default=DEFAULT_SELECT)
    ap.add_argument("--shard", type=int, default=0)
    ap.add_argument("--extra", nargs="*", default=[])
    ap.add_argument("--skip-if-missing", action="store_true",
                    help="exit 77 (ctest SKIP) instead of failing when the xwin splat / clang / build dir is absent")
    ap.add_argument("--lld-link", default="", help="a COFF linker command (e.g. '/path/to/zig lld-link' or "
                    "'lld-link'); when given, also links the PlatformHostShims test against the Windows arm")
    ap.add_argument("--prefix", nargs="*", default=[],
                    help="also every .cpp whose tree-relative path starts with one of these (the sweep: "
                         "--prefix engine/ app/ games/ tools/ --syntax-only)")
    ap.add_argument("--all", action="store_true",
                    help="the whole tree: --prefix engine/ app/ games/ tools/ tests/ third_party/ "
                         "--select shim,host,shard,recomp_common --shard -1 (every shard)")
    ap.add_argument("--sdl-include", default="", help="SDL2 headers for the Windows target (SDL2-devel-*-VC/include); "
                    "replaces Homebrew's -isystem .../include/SDL2")
    ap.add_argument("--ffmpeg-include", default="", help="FFmpeg headers for the Windows target; replaces -isystem /opt/homebrew/include")
    a = ap.parse_args()
    if a.all:
        a.prefix = ["engine/", "app/", "games/", "tools/", "tests/", "third_party/"]
        a.select = "shim,host,shard,recomp_common"
        a.shard = -1

    if a.skip_if_missing:
        missing = []
        if not a.xwin or not os.path.isdir(os.path.join(a.xwin, "crt/include")):
            missing.append(f"xwin splat (PS2R_XWIN_ROOT={a.xwin or '<unset>'}; `xwin --accept-license splat --output <dir>`)")
        if not os.path.isfile(a.clang) or not clang_has_windows_target(a.clang):
            missing.append(f"a clang with the x86_64-pc-windows-msvc target ({a.clang})")
        if not os.path.isfile(os.path.join(a.build, "compile_commands.json")):
            missing.append(f"{a.build}/compile_commands.json")
        if missing:
            print("win_cross_smoke: SKIP -- missing " + "; ".join(missing))
            return 77
    tree = a.tree
    if tree is None:
        with open(os.path.join(a.build, "CMakeCache.txt")) as f:
            m = re.search(r"^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$", f.read(), re.M)
        tree = m.group(1).strip()
    tree = os.path.abspath(tree)
    os.makedirs(a.out, exist_ok=True)
    for d in ("crt/include", "sdk/include/ucrt", "sdk/include/um", "sdk/include/shared"):
        if not a.xwin or not os.path.isdir(os.path.join(a.xwin, d)):
            print(f"win_cross_smoke: missing {os.path.join(a.xwin or '<PS2R_XWIN_ROOT unset>', d)} -- run `xwin splat` first", file=sys.stderr)
            return 2

    with open(os.path.join(a.build, "compile_commands.json")) as f:
        entries = json.load(f)
    picked = select_entries(entries, tree, a.select, a.shard, [os.path.abspath(x) for x in a.extra] + a.extra, a.prefix)

    jobs = []
    seen = set()
    for kind, e in picked:
        args = shlex.split(e["command"]) if "command" in e else e["arguments"]
        rel = tree_rel(e["file"], tree)
        if rel in seen:   # the same TU compiled into several targets: once is the smoke
            continue
        seen.add(rel)
        if kind == "host-arm":
            for win_src in ("engine/platform/host/host_windows.cpp", "engine/platform/windows/platform_windows.cpp",
                            "engine/platform/host/gl_windows.cpp"):
                obj = win_src.replace("/", "_") + ".obj"
                cmd, _ = rewrite(args, tree, a.clang, a.xwin, a.out, obj, a.syntax_only,
                                 src_override=os.path.join(tree, win_src), sdl_include=a.sdl_include,
                                 ffmpeg_include=a.ffmpeg_include)
                jobs.append((win_src, cmd))
            continue
        if rel in ("engine/platform/posix/fiber_posix.cpp", "engine/platform/posix/crash_handler.cpp",
                   "engine/platform/macos/platform_macos.cpp", "engine/platform/linux/platform_linux.cpp",
                   "tests/iop/test_fiber_guard.cpp") \
                or rel.startswith("engine/render/backends/metal/"):
            continue   # the POSIX / macOS arms (+ the fork-based fiber test, if(NOT WIN32)): never compiled on the Windows target
        obj = rel.replace("/", "_") + ".obj"
        cmd, _ = rewrite(args, tree, a.clang, a.xwin, a.out, obj, a.syntax_only, sdl_include=a.sdl_include,
                         ffmpeg_include=a.ffmpeg_include)
        jobs.append((rel, cmd))

    if "shim" in a.select.split(","):
        # The shim header on its own, as a Windows TU with the tree's warning flags.
        shim_tu = os.path.join(a.out, "shim_header_tu.cpp")
        with open(shim_tu, "w") as f:
            f.write('#include "engine/platform/host/platform.h"\n'
                    "int ps2r_win_cross_smoke_shim_anchor() { return PS2R_HOST_WINDOWS; }\n")
        cmd = [a.clang, "-I" + tree, "-std=c++17", "-Wall", "-Wextra", "-Werror", "--target=x86_64-pc-windows-msvc"] + WIN_DEFINES
        for d in ("crt/include", "sdk/include/ucrt", "sdk/include/um", "sdk/include/shared"):
            cmd += ["-isystem", os.path.join(a.xwin, d)]
        cmd += ["-fsyntax-only"] if a.syntax_only else ["-c", "-o", os.path.join(a.out, "shim_header_tu.obj")]
        cmd.append(shim_tu)
        jobs.insert(0, ("engine/platform/host/platform.h (standalone TU)", cmd))

    if not jobs:
        print("win_cross_smoke: nothing selected", file=sys.stderr)
        return 2

    print(f"win_cross_smoke: {len(jobs)} TUs -> target x86_64-pc-windows-msvc, clang {a.clang}, xwin {a.xwin}, "
          f"{'syntax-only' if a.syntax_only else 'objects in ' + a.out}")
    results = []
    with cf.ThreadPoolExecutor(max_workers=a.j) as ex:
        futs = [ex.submit(run_one, label, cmd, os.path.join(a.out, label.replace("/", "_").replace(" ", "_") + ".log"))
                for label, cmd in jobs]
        for fut in cf.as_completed(futs):
            results.append(fut.result())
    results.sort(key=lambda r: r[0])
    failed = 0
    with open(os.path.join(a.out, "SUMMARY.tsv"), "w") as s:
        s.write("tu\tstatus\tseconds\terrors\twarnings\tlog\n")
        for label, rc, dt, ne, nw, log in results:
            status = "OK" if rc == 0 else "FAIL"
            failed += rc != 0
            s.write(f"{label}\t{status}\t{dt:.1f}\t{ne}\t{nw}\t{log}\n")
            print(f"  {status:4} {dt:6.1f}s  errors={ne:<3} warnings={nw:<3} {label}")
    print(f"win_cross_smoke: {len(results) - failed} / {len(results)} TUs compiled; "
          f"{'PASS' if failed == 0 else f'FAIL ({failed} TU)'}; summary {os.path.join(a.out, 'SUMMARY.tsv')}")
    if failed == 0 and a.lld_link and not a.syntax_only:
        failed += 0 if link_host_arm_test(a, tree) else 1
    return 0 if failed == 0 else 1


LINK_TEST_TUS = ("tests/platform/test_host_platform.cpp", "engine/platform/host/host_windows.cpp",
                 "engine/platform/host/platform.cpp", "engine/platform/platform.cpp",
                 "engine/platform/windows/platform_windows.cpp")


def link_host_arm_test(a, tree):
    """The PlatformHostShims contract test linked against the Windows arm into a PE32+ with
    lld-link (`--lld-link /path/to/lld-link` or `zig lld-link`: the ziglang wheel bundles it).
    Every Win32 / UCRT import the arm names must resolve against the SDK's import libraries;
    the .exe is NOT run here (that is the Windows CI job)."""
    out = os.path.join(a.out, "link_host_arm")
    os.makedirs(out, exist_ok=True)
    objs = []
    for src in LINK_TEST_TUS:
        obj = os.path.join(out, src.replace("/", "_") + ".obj")
        cmd = [a.clang, "--target=x86_64-pc-windows-msvc", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
               "-fexceptions", "-fcxx-exceptions", "-I" + tree] + WIN_DEFINES
        for d in ("crt/include", "sdk/include/ucrt", "sdk/include/um", "sdk/include/shared"):
            cmd += ["-isystem", os.path.join(a.xwin, d)]
        cmd += ["-c", os.path.join(tree, src), "-o", obj]
        label, rc, dt, ne, nw, log = run_one(src, cmd, obj + ".log")
        if rc != 0:
            print(f"win_cross_smoke: link test: {src} failed to compile ({log})")
            return False
        objs.append(obj)
    exe = os.path.join(out, "test_host_platform.exe")
    link = shlex.split(a.lld_link) + ["/nologo"] + objs + ["/out:" + exe, "/subsystem:console"]
    for d in ("crt/lib/x86_64", "sdk/lib/um/x86_64", "sdk/lib/ucrt/x86_64"):
        link.append("/libpath:" + os.path.join(a.xwin, d))
    link += ["msvcrt.lib", "vcruntime.lib", "ucrt.lib", "kernel32.lib", "dbghelp.lib", "psapi.lib"]
    label, rc, dt, ne, nw, log = run_one("link test_host_platform.exe", link, exe + ".log")
    if rc != 0 or not os.path.exists(exe):
        print(f"win_cross_smoke: link test FAILED ({log})")
        return False
    print(f"win_cross_smoke: link test PASS -- {exe} ({os.path.getsize(exe)} bytes, PE32+; "
          f"every Win32/UCRT import of the Windows arm resolved; not run here)")
    return True


if __name__ == "__main__":
    sys.exit(main())
