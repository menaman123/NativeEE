#!/usr/bin/env python3
"""win_cross_emulator.py -- CMAKE_CROSSCOMPILING_EMULATOR for the Windows cross build
(engine/platform/windows/toolchain_cross_macos.cmake).

The build runs its own tools at build time (tools/disc_extract, tools/elf_embedded, the EE and
IOP recompilers: cmake/TitleInputs.cmake, games/<title>/{recomp,iop}/CMakeLists.txt). In a
cross build those are PE32+ images this host cannot execute. CMake prepends the emulator to
every add_custom_command COMMAND whose first word is an executable target; this script maps
the .exe it is handed to the SAME tool in a native host build of the same tree and execs that:

    <cross build>/tools/recompiler_v2/ps2r_recompiler_v2.exe
 -> <host build>/tools/recompiler_v2/ps2r_recompiler_v2

The host build is the environment variable PS2R_HOST_TOOLS_BUILD (the toolchain passes it
through `cmake -E env` from its cache variable of the same name); the cross build dir is the
nearest ancestor of the .exe with a CMakeCache.txt: a configured + built macOS/Linux build directory of the
same source tree (the tools are deterministic: the recompiled output is MANIFEST-checked, so
the host tool and the Windows tool produce the same bytes by contract). The Windows .exe of
each tool is still built (it is the artefact the Windows user runs); only the invocation is
redirected.

A path that is not under the cross build dir, or has no host twin, is reported and the call
fails (exit 127) -- never silently run as the wrong thing. Nothing of the game runs here: the
tools consume the user's own disc image (NATIVEEE_DISC) exactly as a native build does.
"""
import os
import sys


def main():
    if len(sys.argv) < 2:
        print("win_cross_emulator: usage: win_cross_emulator.py <tool.exe> [args...]", file=sys.stderr)
        return 2
    exe = os.path.abspath(sys.argv[1])
    host = os.environ.get("PS2R_HOST_TOOLS_BUILD", "")
    host = os.path.abspath(host) if host else ""
    if not host or not os.path.isdir(host):
        print(f"win_cross_emulator: PS2R_HOST_TOOLS_BUILD is not a directory ({host!r}); "
              "point it at a native build of this tree (the one whose tools run here)", file=sys.stderr)
        return 127
    # The cross build dir: the nearest ancestor of the .exe that holds a CMakeCache.txt.
    cross = os.path.dirname(exe)
    while cross and not os.path.isfile(os.path.join(cross, "CMakeCache.txt")):
        parent = os.path.dirname(cross)
        if parent == cross:
            cross = ""
            break
        cross = parent
    if not cross:
        print(f"win_cross_emulator: {exe} is not inside a CMake build directory", file=sys.stderr)
        return 127
    rel = os.path.relpath(exe, cross)
    if rel.lower().endswith(".exe"):
        rel = rel[:-4]
    twin = os.path.join(host, rel)
    if not os.path.isfile(twin) or not os.access(twin, os.X_OK):
        print(f"win_cross_emulator: no host twin for {rel} at {twin} -- build it in the host tree first "
              f"(cmake --build {host} --target {os.path.basename(rel)})", file=sys.stderr)
        return 127
    args = [twin] + sys.argv[2:]
    os.execv(twin, args)


if __name__ == "__main__":
    sys.exit(main())
