#!/usr/bin/env python3
"""gen_gl_windows.py -- generate the Windows arm's OpenGL header + entry-point table
(engine/platform/host/gl_windows.h, gl_windows.cpp) from the GL entry points and enums the
tree actually uses.

  tools/build/gen_gl_windows.py --tree <repo root> [--registry <gl3.h | glcorearb.h>]
                                [--check]   # exit 1 when the committed files are stale

Why a generated loader: Windows' opengl32.dll exports GL 1.1 only; every core 3.3 / 4.1 entry
point (glGenVertexArrays, glFenceSync, glMapBufferRange, ...) exists only as a pointer from
wglGetProcAddress on the current context. macOS (OpenGL.framework) and Linux (libOpenGL /
libGL export the core set) link the functions directly, so the backend is written against
plain names. On _WIN32 PS2R_GL_HEADER resolves to the generated header, which spells every
used name as a macro over a function pointer (`#define glClear ps2r_gl_glClear`) that
host::GlLoadEntryPoints fills once a context is current (engine/platform/sdl/gl_context.cpp
passes SDL_GL_GetProcAddress, which already falls back to opengl32.dll's exports for the 1.1
names). No third-party loader (glad / glew / opengl-registry) is vendored or fetched.

Scope -- exactly what the tree uses, nothing else, so a new GL call shows up as a build error
on the Windows target until this generator is re-run (ctest PlatformGlWindowsHeaderCurrent):
  * the scanned TUs: every .cpp / .h under engine/ app/ tests/ tools/ games/ (third_party/
    excluded) that has `#include PS2R_GL_HEADER`, plus every header under engine/render/backends/gl/;
  * names: `gl[A-Z]...(` calls and `GL_[A-Z0-9_]+` tokens in comment-stripped code;
  * types: the fixed gltypes set (GLenum ... GLsync), spelled from <cstdint> like the
    Khronos khrplatform widths on x86-64 Windows (GLintptr / GLsizeiptr = ptrdiff_t);
  * values + prototypes: read from a Khronos-derived registry header -- the macOS SDK's
    OpenGL.framework gl3.h by default (the same enum values and PFNGL*PROC typedefs as
    GL/glcorearb.h; either is accepted: --registry).

The outputs are committed: the Windows build needs no registry header and no python at
configure time.
"""
import argparse
import os
import re
import subprocess
import sys

HEADER_REL = "engine/platform/host/gl_windows.h"
SOURCE_REL = "engine/platform/host/gl_windows.cpp"
SCAN_ROOTS = ("engine", "app", "tests", "tools", "games")
GL_HEADER_DIR = "engine/render/backends/gl"

# gltypes.h widths; Win64 is LLP64 so the pointer-sized ones are ptrdiff_t (khronos_ssize_t).
GL_TYPES = [
    ("GLenum", "std::uint32_t"), ("GLboolean", "std::uint8_t"), ("GLbitfield", "std::uint32_t"),
    ("GLbyte", "std::int8_t"), ("GLshort", "std::int16_t"), ("GLint", "std::int32_t"),
    ("GLsizei", "std::int32_t"), ("GLubyte", "std::uint8_t"), ("GLushort", "std::uint16_t"),
    ("GLuint", "std::uint32_t"), ("GLhalf", "std::uint16_t"), ("GLfloat", "float"),
    ("GLclampf", "float"), ("GLdouble", "double"), ("GLclampd", "double"), ("GLvoid", "void"),
    ("GLchar", "char"), ("GLintptr", "std::ptrdiff_t"), ("GLsizeiptr", "std::ptrdiff_t"),
    ("GLint64", "std::int64_t"), ("GLuint64", "std::uint64_t"),
]

COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
CALL_RE = re.compile(r"\b(gl[A-Z][A-Za-z0-9]*)\s*\(")
ENUM_RE = re.compile(r"\bGL_[A-Z0-9_]+\b")
DEFINE_RE = re.compile(r"^\s*#\s*define\s+(GL_[A-Z0-9_]+)\s+(\S+)", re.M)
PFN_RE = re.compile(r"^\s*typedef\s+(.+?)\s*\(\s*APIENTRYP\s+PFN(GL[A-Z0-9_]+)PROC\s*\)\s*\((.*?)\)\s*;", re.M)


def default_registry():
    try:
        sdk = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True, timeout=30).stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        sdk = ""
    for cand in (os.path.join(sdk, "System/Library/Frameworks/OpenGL.framework/Headers/gl3.h") if sdk else "",
                 "/usr/include/GL/glcorearb.h", "/usr/local/include/GL/glcorearb.h"):
        if cand and os.path.isfile(cand):
            return cand
    return ""


def scanned_files(tree):
    files = []
    for root in SCAN_ROOTS:
        top = os.path.join(tree, root)
        for dirpath, dirnames, filenames in os.walk(top):
            dirnames[:] = sorted(d for d in dirnames if d != "third_party" and not d.startswith("build"))
            for fn in sorted(filenames):
                if not fn.endswith((".cpp", ".h")):
                    continue
                rel = os.path.relpath(os.path.join(dirpath, fn), tree).replace(os.sep, "/")
                if rel in (HEADER_REL, SOURCE_REL):
                    continue
                with open(os.path.join(dirpath, fn), encoding="utf-8", errors="replace") as f:
                    text = f.read()
                if re.search(r"^\s*#\s*include\s+PS2R_GL_HEADER\b", text, re.M) or rel.startswith(GL_HEADER_DIR + "/"):
                    files.append((rel, text))
    return files


def collect(files):
    calls, enums, where = set(), set(), {}
    for rel, text in files:
        code = COMMENT_RE.sub(" ", text)
        for m in CALL_RE.finditer(code):
            calls.add(m.group(1))
            where.setdefault(m.group(1), rel)
        for m in ENUM_RE.finditer(code):
            enums.add(m.group(0))
            where.setdefault(m.group(0), rel)
    return calls, enums, where


def read_registry(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    defines = {}
    for m in DEFINE_RE.finditer(text):
        defines.setdefault(m.group(1), m.group(2))
    # Aliases (`#define GL_DRAW_FRAMEBUFFER_BINDING GL_FRAMEBUFFER_BINDING`): emit the value the
    # alias stands for, since the aliased name itself need not be in the used set.
    for name in list(defines):
        value, hops = defines[name], 0
        while value in defines and hops < 8:
            value, hops = defines[value], hops + 1
        defines[name] = value
    protos = {}
    for m in PFN_RE.finditer(text):
        ret, upper, args = m.group(1).strip(), m.group(2), m.group(3).strip()
        protos.setdefault(upper, (ret, args))
    return defines, protos


def gen_header(registry_name, enums, funcs, protos, defines):
    out = []
    w = out.append
    w("// engine/platform/host/gl_windows.h -- GENERATED by tools/build/gen_gl_windows.py -- DO NOT EDIT.")
    w("// The Windows arm's GL header: the %d enums and %d entry points the tree uses, values and" % (len(enums), len(funcs)))
    w("// prototypes from %s. Every entry point is a macro over a function pointer that" % registry_name)
    w("// ps2r::host::GlLoadEntryPoints fills (engine/platform/host/platform.h section 2) once a")
    w("// context is current; opengl32.dll exports GL 1.1 only. Regenerate with")
    w("//   python3 tools/build/gen_gl_windows.py --tree .   (ctest PlatformGlWindowsHeaderCurrent)")
    w("#pragma once")
    w("")
    w("#if !defined(_WIN32)")
    w('#error "gl_windows.h is the Windows arm\'s GL header; include PS2R_GL_HEADER (engine/platform/host/platform.h)"')
    w("#endif")
    w("")
    w("#include <cstddef>")
    w("#include <cstdint>")
    w("")
    w("// The GL calling convention on Win32 (APIENTRY = __stdcall; a no-op on x86-64).")
    w("#define PS2R_GL_APIENTRY __stdcall")
    w("")
    w("// --- types (gltypes.h / khrplatform.h widths) ---")
    for name, cpp in GL_TYPES:
        w("typedef %s %s;" % (cpp, name))
    w("typedef struct __GLsync* GLsync;")
    w("")
    w("// --- enums ---")
    for e in sorted(enums):
        w("#define %s %s" % (e, defines[e]))
    w("")
    w("// --- entry points: pointer types, the pointers (defined in gl_windows.cpp) and the names ---")
    for f in sorted(funcs):
        ret, args = protos[f.upper()]
        w("typedef %s (PS2R_GL_APIENTRY* PFN_ps2r_%s)(%s);" % (ret, f, args))
    w("")
    for f in sorted(funcs):
        w("extern PFN_ps2r_%s ps2r_gl_%s;" % (f, f))
    w("")
    for f in sorted(funcs):
        w("#define %s ps2r_gl_%s" % (f, f))
    w("")
    w("namespace ps2r::host::gl_windows {")
    w("")
    w("// The loader's table: one row per entry point (the GL name and the slot to fill).")
    w("struct EntryPoint {")
    w("    const char* name;")
    w("    void** slot;")
    w("};")
    w("extern const EntryPoint kEntryPoints[%d];" % len(funcs))
    w("constexpr int kEntryPointCount = %d;" % len(funcs))
    w("")
    w("}  // namespace ps2r::host::gl_windows")
    return "\n".join(out) + "\n"


def gen_source(funcs):
    out = []
    w = out.append
    w("// engine/platform/host/gl_windows.cpp -- GENERATED by tools/build/gen_gl_windows.py -- DO NOT EDIT.")
    w("// Storage for the %d GL entry-point pointers and the loader table (gl_windows.h)." % len(funcs))
    w("#include \"engine/platform/host/gl_windows.h\"")
    w("")
    for f in sorted(funcs):
        w("PFN_ps2r_%s ps2r_gl_%s = nullptr;" % (f, f))
    w("")
    w("namespace ps2r::host::gl_windows {")
    w("")
    w("const EntryPoint kEntryPoints[%d] = {" % len(funcs))
    for f in sorted(funcs):
        w('    {"%s", reinterpret_cast<void**>(&ps2r_gl_%s)},' % (f, f))
    w("};")
    w("")
    w("}  // namespace ps2r::host::gl_windows")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tree", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    ap.add_argument("--registry", default="", help="gl3.h or glcorearb.h (default: the macOS SDK's gl3.h, else /usr/include/GL/glcorearb.h)")
    ap.add_argument("--check", action="store_true", help="compare against the committed files; exit 1 when stale")
    ap.add_argument("--skip-if-missing", action="store_true", help="exit 77 (ctest SKIP) when no registry header is found")
    a = ap.parse_args()
    tree = os.path.abspath(a.tree)
    registry = a.registry or default_registry()
    if not registry or not os.path.isfile(registry):
        print("gen_gl_windows: no registry header (gl3.h / glcorearb.h) found; pass --registry", file=sys.stderr)
        return 77 if a.skip_if_missing else 2
    registry_name = "OpenGL.framework/Headers/gl3.h" if registry.endswith("gl3.h") else os.path.basename(registry)

    files = scanned_files(tree)
    calls, enums, where = collect(files)
    defines, protos = read_registry(registry)
    bad = False
    funcs = set()
    for c in sorted(calls):
        if c.upper() not in protos:
            print("gen_gl_windows: %s (%s): no prototype in %s" % (c, where[c], registry), file=sys.stderr)
            bad = True
        else:
            funcs.add(c)
    used_enums = set()
    for e in sorted(enums):
        if e in defines:
            used_enums.add(e)
        else:
            # Tokens in strings (GLSL `#extension GL_...`) or names the registry lacks: reported, not emitted.
            print("gen_gl_windows: note: %s (%s) is not a #define in the registry; skipped" % (e, where[e]), file=sys.stderr)
    if bad:
        return 1
    header = gen_header(registry_name, used_enums, funcs, protos, defines)
    source = gen_source(funcs)
    outputs = ((os.path.join(tree, HEADER_REL), header), (os.path.join(tree, SOURCE_REL), source))
    if a.check:
        stale = []
        for path, text in outputs:
            cur = open(path, encoding="utf-8").read() if os.path.isfile(path) else ""
            if cur != text:
                stale.append(os.path.relpath(path, tree))
        if stale:
            print("gen_gl_windows: STALE -- regenerate: python3 tools/build/gen_gl_windows.py --tree . (%s)" % ", ".join(stale))
            return 1
        print("gen_gl_windows: current (%d TUs scanned, %d entry points, %d enums)" % (len(files), len(funcs), len(used_enums)))
        return 0
    for path, text in outputs:
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        print("gen_gl_windows: wrote %s" % os.path.relpath(path, tree))
    print("gen_gl_windows: %d TUs scanned, %d entry points, %d enums, registry %s" % (len(files), len(funcs), len(used_enums), registry))
    return 0


if __name__ == "__main__":
    sys.exit(main())
