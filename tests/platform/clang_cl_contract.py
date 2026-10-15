#!/usr/bin/env python3
"""clang_cl_contract.py -- the clang-cl shim's contract, checked on every host (Charter 2, B2/B3).

  clang_cl_contract.py --tree <repo root> [--clang <clang binary>] [--scratch <dir>]

1. tools/build/clang_cl_shim.py --selftest (the mapping table).
2. Every per-target flag set the tree's CMake files spell (target_compile_options(... PRIVATE ...)
   and COMPILE_OPTIONS "...") is collected; for each distinct set the probe TU is compiled with a
   clang in cl driver mode for the x86_64-pc-windows-msvc target -- Apple clang, Linux clang and the
   Visual Studio clang-cl all have it -- (a) RAW, (b) through the shim's mapping. The contract:
   every mapped set compiles with exit 0 and no "unknown argument" / "argument unused" line, so the
   first Windows compile cannot fail on a flag spelling. The raw results are reported (how many
   sets clang-cl rejects as written: the first-run failure the shim pre-empts).
   Without a clang that understands --driver-mode=cl the probe is SKIPped (exit 77) after step 1.

The probe TU has no #include: the MSVC STL is not needed, only the driver's option handling.
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

PROBE = "static int g(int a){return a;}\nint h(void);\nint h(void){ auto x = 1; return g(x); }   // `auto`: -Weverything (cl /Wall) flags it as -Wc++98-compat\n"
OPT_RE = re.compile(r"target_compile_options\(\s*[A-Za-z0-9_${}]+\s+(?:PRIVATE|PUBLIC|INTERFACE)\s+([^)]*)\)", re.S)
COMPILE_OPTIONS_RE = re.compile(r'COMPILE_OPTIONS\s+"([^"]*)"')
GENEXPR_RE = re.compile(r"\$<[^>]*>")


def flag_sets(tree):
    sets = set()
    files = glob.glob(os.path.join(tree, "**", "CMakeLists.txt"), recursive=True) + glob.glob(os.path.join(tree, "**", "*.cmake"), recursive=True)
    for path in files:
        rel = os.path.relpath(path, tree)
        if rel.startswith(("build", "out", "dist", "third_party")) or "/build" in rel or "cmake_pch" in rel:
            continue
        try:
            text = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        for m in OPT_RE.finditer(text):
            body = GENEXPR_RE.sub(" ", m.group(1))
            toks = tuple(t for t in body.replace(";", " ").split() if t.startswith("-") and "${" not in t)
            if toks:
                sets.add(toks)
        for m in COMPILE_OPTIONS_RE.finditer(text):
            body = GENEXPR_RE.sub(" ", m.group(1))
            toks = tuple(t for t in body.replace(";", " ").split() if t.startswith("-") and "${" not in t)
            if toks:
                sets.add(toks)
    return sorted(sets)


def find_clang(explicit, scratch):
    """The first clang that compiles the mapped sanity probe in cl driver mode: the explicit
    compiler, then `clang`/`clang-cl` beside it (a `c++` symlink keeps its own driver mode), then PATH."""
    cands = []
    if explicit:
        cands.append(explicit)
        d = os.path.dirname(explicit if os.path.isabs(explicit) else (shutil.which(explicit) or explicit))
        cands += [os.path.join(d, "clang-cl"), os.path.join(d, "clang")]
    cands += ["clang-cl", "clang"]
    seen = set()
    for c in cands:
        path = c if os.path.isabs(c) else shutil.which(c)
        if not path or path in seen or not os.path.isfile(path):
            continue
        seen.add(path)
        try:
            out = subprocess.run([path, "--version"], capture_output=True, text=True, timeout=30).stdout
        except (OSError, subprocess.TimeoutExpired) as e:
            print("clang_cl_contract: %s --version: %s" % (path, e))
            continue
        if "clang" not in out.lower():
            print("clang_cl_contract: %s is not a clang (%r)" % (path, out.strip().splitlines()[:1]))
            continue
        try:
            rc, bad = compile_probe(path, ["/clang:-Wall", "-Wextra", "-Werror"], scratch, verbose=True)
        except (OSError, subprocess.TimeoutExpired) as e:
            print("clang_cl_contract: %s: %s" % (path, e))
            continue
        if rc == 0:
            return path
        print("clang_cl_contract: %s: sanity probe rc=%d %s" % (path, rc, bad[:2]))
    return None


def compile_probe(clang, flags, scratch, verbose=False):
    src = os.path.join(scratch, "probe.cpp")
    with open(src, "w") as f:
        f.write(PROBE)
    # Relative names, cwd = scratch: in cl driver mode an absolute POSIX path is an OPTION
    # (/Users/... parses as /U<macro>), the exact trap a Windows build never meets.
    is_cl_driver = os.path.basename(clang).lower().startswith("clang-cl")
    cmd = [clang] + ([] if is_cl_driver else ["--driver-mode=cl"]) + ["--target=x86_64-pc-windows-msvc", "/c"] + list(flags) + ["probe.cpp", "/Fo:probe.obj"]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=120, cwd=scratch)
    if verbose and p.returncode != 0:
        sys.stdout.write((p.stderr + p.stdout)[:2000])
    bad = [l for l in (p.stderr + p.stdout).splitlines() if "unknown argument" in l or "argument unused" in l]
    return p.returncode, bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tree", required=True)
    ap.add_argument("--clang", default=os.environ.get("PS2R_CLANG", ""))
    ap.add_argument("--scratch", default="")
    a = ap.parse_args()
    tree = os.path.abspath(a.tree)
    shim = os.path.join(tree, "tools", "build", "clang_cl_shim.py")
    r = subprocess.run([sys.executable, shim, "--selftest"], capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    if r.returncode != 0:
        print("clang_cl_contract: shim selftest FAILED")
        return 1
    sets = flag_sets(tree)
    print("clang_cl_contract: %d distinct per-target flag sets in the tree's CMake files" % len(sets))
    for s in sets:
        print("  " + " ".join(s))
    scratch = a.scratch or tempfile.mkdtemp(prefix="clang_cl_contract_")
    os.makedirs(scratch, exist_ok=True)
    clang = find_clang(a.clang, scratch)
    if not clang:
        print("clang_cl_contract: no clang that compiles the probe in cl driver mode on this host -> probe SKIP (77)")
        return 77
    from importlib.machinery import SourceFileLoader
    mod = SourceFileLoader("clang_cl_shim", shim).load_module()
    raw_fail = 0
    raw_dropped = 0
    mapped_fail = []
    for s in sets:
        rc_raw, bad_raw = compile_probe(clang, s, scratch)
        if rc_raw != 0:
            raw_fail += 1
        if bad_raw:
            raw_dropped += 1
        mapped = mod.map_clang_cl(list(s))
        rc_m, bad_m = compile_probe(clang, mapped, scratch)
        if rc_m != 0 or bad_m:
            mapped_fail.append((s, mapped, rc_m, bad_m))
    print("clang_cl_contract: clang %s; raw: %d / %d sets fail to compile, %d / %d silently drop a flag (unknown argument ignored); mapped sets failing: %d"
          % (clang, raw_fail, len(sets), raw_dropped, len(sets), len(mapped_fail)))
    for s, mapped, rc_m, bad_m in mapped_fail:
        print("  FAIL raw %s -> mapped %s rc=%d %s" % (" ".join(s), " ".join(mapped), rc_m, bad_m[:2]))
    if not a.scratch:
        shutil.rmtree(scratch, ignore_errors=True)
    if mapped_fail:
        return 1
    print("clang_cl_contract: PASS (every flag set the tree spells compiles under clang-cl through the shim)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
