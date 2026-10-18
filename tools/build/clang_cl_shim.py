#!/usr/bin/env python3
"""clang_cl_shim.py -- the compiler launcher that lets the tree's GNU-style per-target flags
reach clang-cl (and, degraded, cl.exe) with their intended meaning (Charter 2, Track B2/B3).

  CMAKE_CXX_COMPILER_LAUNCHER = python;<tree>/tools/build/clang_cl_shim.py     (the windows presets)
  clang_cl_shim.py <compiler> <args...>          rewrite the args, exec the compiler, exit its code
  clang_cl_shim.py --map <front-end> <args...>   print the rewritten args, one per line (the contract)
  clang_cl_shim.py --selftest

Why: ~130 CMakeLists in the tree say `-Wall -Wextra -Werror` and ~26 say `-ffp-contract=off`
(the arithmetic contract of the oracle TUs). clang-cl is a cl-compatible driver: an option may be
spelled `-X` or `/X`, so `-Wall` IS cl's `/Wall` = clang's `-Weverything` (every hand-written TU
then fails under -Werror on -Wc++98-compat alone), and clang-only flags it does not list
(`-ffp-contract=off`, `-frounding-math`, `-fvisibility=`, `-include`, `-isystem`, `-std=`, `-O0`,
`-O3`, ...) are "unknown argument ignored in clang-cl" -- a warning that -Werror turns into an
error, and the FP contract would be silently dropped without it. The fix is per-flag spelling,
not per-CMakeLists churn: `/clang:<flag>` hands a flag to the clang driver verbatim.

The table (clang-cl front end):
  -Wall                     -> /clang:-Wall          (GNU -Wall, never -Weverything)
  -Wextra -Werror -W* -Wno-* -> unchanged           (clang-cl accepts them as clang options)
  -O0 -O1 -O2 -O3 -Os       -> /Od /O1 /O2 /O2 /Os
  -std=c++NN                -> /std:c++NN
  -include X / -includeX    -> /FI X
  -isystem X                -> /clang:-isystem /clang:X
  -fexceptions / -frtti     -> /EHsc / /GR
  -fvisibility=* -fPIC -pthread -rdynamic -fno-plt -> dropped (no meaning on the Windows target)
  -ffp-contract=* -frounding-math -fno-strict-aliasing -fwrapv -f* -m* -g* (any other)
                            -> /clang:<flag>         (verbatim to the clang driver)
  everything else           -> unchanged
cl.exe front end (windows-msvc-dev, compile check only): -Wall -> /W4, -Werror -> /WX, every
other -W* and every -f*/-m* dropped (cl has no -fwrapv: games/<title>/recomp/CMakeLists.txt
already WARNs), -std= -> /std:, -O* -> /Od|/O1|/O2, -include -> /FI, -g -> /Z7.

Response files (`@file.rsp`, Ninja's long command lines) are expanded, rewritten into a sibling
file and passed as `@sibling`. Everything after the compiler path is positional; arguments the
table does not name pass through untouched, so `/showIncludes`, `/Fo`, `/c`, `-D`, `-I` ... are
never touched. Exit code: the compiler's.
"""
import os
import shlex
import subprocess
import sys
import tempfile

# Flags clang-cl's own front end accepts RAW on every LLVM release in use. -fwrapv and -fno-strict-aliasing
# are NOT here on purpose: clang-cl 18 (Ubuntu 24.04; the LLVM Visual Studio 2022 bundles) rejects them raw
# ("unknown argument ignored in clang-cl" -- fatal under -Werror) while clang 21+ accepts them; /clang:<flag> is
# valid on every release, so they take the verbatim route below (ctest PlatformClangClShimContract on both).
ACCEPTED_F_CLANG_CL = {"-fstrict-aliasing", "-fcolor-diagnostics",
                       "-fno-color-diagnostics", "-fsanitize", "-fno-sanitize", "-fdiagnostics-color",
                       "-fms-compatibility", "-fms-extensions", "-fstandalone-debug", "-flto", "-fprofile",
                       "-fsplit-lto-unit", "-fdiagnostics-absolute-paths", "-fdiagnostics-parseable-fixits",
                       "-fcoverage-mapping", "-fprofile-instr-generate", "-fprofile-instr-use"}
DROP_ON_WINDOWS = {"-pthread", "-fPIC", "-fpic", "-rdynamic", "-fno-plt", "-fPIE", "-fpie"}
DROP_PREFIXES_ON_WINDOWS = ("-fvisibility=", "-fvisibility-inlines-hidden")
OPT_MAP = {"-O0": "/Od", "-O1": "/O1", "-O2": "/O2", "-O3": "/O2", "-Os": "/Os", "-Og": "/Od", "-Ofast": "/O2"}


def _accepted_f(flag):
    for a in ACCEPTED_F_CLANG_CL:
        if flag == a or flag.startswith(a + "="):
            return True
    return False


def map_clang_cl(args):
    out = []
    i = 0
    while i < len(args):
        a = args[i]
        nxt = args[i + 1] if i + 1 < len(args) else None
        if a == "-Wall":
            out.append("/clang:-Wall")
        elif a in OPT_MAP:
            out.append(OPT_MAP[a])
        elif a.startswith("-std="):
            out.append("/std:" + a[len("-std="):].replace("gnu", "c"))
        elif a == "-include" and nxt is not None:
            out += ["/FI", nxt]
            i += 1
        elif a.startswith("-include") and len(a) > len("-include"):
            out += ["/FI", a[len("-include"):]]
        elif a == "-isystem" and nxt is not None:
            out += ["/clang:-isystem", "/clang:" + nxt]
            i += 1
        elif a == "-fexceptions":
            out.append("/EHsc")
        elif a == "-frtti":
            out.append("/GR")
        elif a in DROP_ON_WINDOWS or a.startswith(DROP_PREFIXES_ON_WINDOWS):
            pass
        elif a.startswith("-W") or a == "-w":
            out.append(a)
        elif a.startswith(("-f", "-m", "-g")) and not _accepted_f(a) and a != "-g":
            out.append("/clang:" + a)
        else:
            out.append(a)
        i += 1
    return out


def map_cl(args):
    out = []
    warned = set()
    i = 0
    while i < len(args):
        a = args[i]
        nxt = args[i + 1] if i + 1 < len(args) else None
        if a == "-Wall":
            out.append("/W4")
        elif a == "-Werror":
            out.append("/WX")
        elif a.startswith("-W") or a == "-w":
            pass
        elif a in OPT_MAP:
            out.append(OPT_MAP[a])
        elif a.startswith("-std="):
            out.append("/std:" + a[len("-std="):].replace("gnu", "c"))
        elif a == "-include" and nxt is not None:
            out += ["/FI", nxt]
            i += 1
        elif a == "-isystem" and nxt is not None:
            out += ["/external:I", nxt]
            i += 1
        elif a == "-g":
            out.append("/Z7")
        elif a.startswith(("-f", "-m", "-g")) or a in DROP_ON_WINDOWS:
            if a in ("-fwrapv", "-ffp-contract=off", "-frounding-math", "-fno-strict-aliasing") and a not in warned:
                warned.add(a)
                sys.stderr.write("clang_cl_shim: cl.exe has no equivalent of %s -- the arithmetic contract is NOT enforced (compile check only)\n" % a)
            if a == "-ffp-contract=off":
                out.append("/fp:precise")
        else:
            out.append(a)
        i += 1
    return out


def front_end(compiler):
    base = os.path.basename(compiler).lower()
    if base.endswith(".exe"):
        base = base[:-4]
    if base.startswith("clang-cl"):
        return "clang-cl"
    if base == "cl":
        return "cl"
    return "gnu"  # clang / gcc: the shim is a no-op


def expand_rsp(args):
    """Expand @file arguments (one level); returns (flat args, [rsp paths])."""
    flat, rsps = [], []
    for a in args:
        if a.startswith("@") and os.path.isfile(a[1:]):
            rsps.append(a[1:])
            with open(a[1:], encoding="utf-8", errors="replace") as f:
                text = f.read()
            flat += shlex.split(text, posix=False) if os.name == "nt" else shlex.split(text)
        else:
            flat.append(a)
    return flat, rsps


def rewrite(compiler, args):
    fe = front_end(compiler)
    if fe == "gnu":
        return args, fe
    flat, rsps = expand_rsp(args)
    mapped = map_clang_cl(flat) if fe == "clang-cl" else map_cl(flat)
    if not rsps:
        return mapped, fe
    # Keep the command line short: write the rewritten flags to a sibling response file.
    fd, path = tempfile.mkstemp(prefix=os.path.basename(rsps[0]) + ".", suffix=".shim.rsp", dir=os.path.dirname(rsps[0]) or None)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write(" ".join(_quote(m) for m in mapped))
    return ["@" + path], fe


def _quote(s):
    if not s or any(c in s for c in ' \t"'):
        return '"' + s.replace('"', '\\"') + '"'
    return s


def selftest():
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)
    m = map_clang_cl(["-Wall", "-Wextra", "-Werror", "-O2", "-ffp-contract=off", "-fno-strict-aliasing", "-fwrapv",
                      "-Wno-unused-variable", "/showIncludes", "/Fo", "x.obj", "/c", "x.cpp", "-DFOO=1", "-Ipath"])
    check(m[0] == "/clang:-Wall" and "-Wall" not in m, "-Wall -> /clang:-Wall (never cl's /Wall = -Weverything)")
    check("-Wextra" in m and "-Werror" in m and "-Wno-unused-variable" in m, "-Wextra/-Werror/-Wno-* pass through")
    check("/O2" in m and "-O2" not in m, "-O2 -> /O2")
    check("/clang:-ffp-contract=off" in m, "-ffp-contract=off -> /clang:-ffp-contract=off (the FP contract is kept, not dropped)")
    check("/clang:-fno-strict-aliasing" in m and "/clang:-fwrapv" in m and "-fwrapv" not in m,
          "-fno-strict-aliasing / -fwrapv -> /clang: (clang-cl 18 rejects them raw under -Werror; 21 accepts; /clang: is valid on both)")
    check(m[-7:] == ["/showIncludes", "/Fo", "x.obj", "/c", "x.cpp", "-DFOO=1", "-Ipath"], "cl-style and -D/-I arguments untouched, order kept")
    check(map_clang_cl(["-std=c++17"]) == ["/std:c++17"], "-std=c++17 -> /std:c++17")
    check(map_clang_cl(["-include", "pch.h"]) == ["/FI", "pch.h"] and map_clang_cl(["-includepch.h"]) == ["/FI", "pch.h"], "-include -> /FI")
    check(map_clang_cl(["-isystem", "/x"]) == ["/clang:-isystem", "/clang:/x"], "-isystem -> /clang: pair")
    check(map_clang_cl(["-O0", "-O3", "-Os"]) == ["/Od", "/O2", "/Os"], "-O0/-O3/-Os mapped")
    check(map_clang_cl(["-fvisibility=hidden", "-pthread", "-fPIC"]) == [], "no-meaning-on-Windows flags dropped")
    check(map_clang_cl(["-frounding-math", "-mfma", "-gdwarf-4"]) == ["/clang:-frounding-math", "/clang:-mfma", "/clang:-gdwarf-4"], "other -f/-m/-g flags -> /clang:")
    check(map_clang_cl(["-g", "-w", "-fsanitize=address"]) == ["-g", "-w", "-fsanitize=address"], "-g, -w, -fsanitize accepted: unchanged")
    check(map_clang_cl(["-fexceptions", "-frtti"]) == ["/EHsc", "/GR"], "-fexceptions/-frtti -> /EHsc /GR")
    c = map_cl(["-Wall", "-Wextra", "-Werror", "-Wno-x", "-ffp-contract=off", "-fwrapv", "-O2", "-g", "/c", "x.cpp"])
    check(c == ["/W4", "/WX", "/fp:precise", "/O2", "/Z7", "/c", "x.cpp"], "cl front end: /W4 /WX, -W*/-f* dropped, /fp:precise for the contract: %s" % c)
    check(front_end("C:/VS/bin/clang-cl.exe") == "clang-cl" and front_end("cl.exe") == "cl" and front_end("/usr/bin/clang++") == "gnu", "front end from the compiler name")
    check(rewrite("/usr/bin/clang++", ["-Wall", "-ffp-contract=off"])[0] == ["-Wall", "-ffp-contract=off"], "a GNU-driver compiler: the shim is a no-op")
    with tempfile.TemporaryDirectory() as d:
        rsp = os.path.join(d, "x.rsp")
        with open(rsp, "w") as f:
            f.write('-Wall -Wextra -Werror -ffp-contract=off -DX="a b" /c x.cpp\n')
        out, fe = rewrite("clang-cl.exe", ["/nologo", "@" + rsp])
        check(fe == "clang-cl" and len(out) == 1 and out[0].startswith("@") and out[0].endswith(".shim.rsp"), "a response file is expanded and rewritten into a sibling .shim.rsp")
        with open(out[0][1:]) as f:
            text = f.read()
        check("/clang:-Wall" in text and "/clang:-ffp-contract=off" in text and '"-DX=a b"' in text, "the rewritten rsp carries the mapped flags, quoted where needed: %r" % text)
    print("clang_cl_shim selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main(argv):
    if len(argv) >= 2 and argv[1] == "--selftest":
        return selftest()
    if len(argv) >= 3 and argv[1] == "--map":
        fe = argv[2]
        mapped = map_clang_cl(argv[3:]) if fe == "clang-cl" else map_cl(argv[3:]) if fe == "cl" else argv[3:]
        for m in mapped:
            print(m)
        return 0
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    compiler, args = argv[1], argv[2:]
    mapped, _ = rewrite(compiler, args)
    try:
        return subprocess.call([compiler] + mapped)
    except OSError as e:
        sys.stderr.write("clang_cl_shim: cannot run %s: %s\n" % (compiler, e))
        return 127


if __name__ == "__main__":
    sys.exit(main(sys.argv))
