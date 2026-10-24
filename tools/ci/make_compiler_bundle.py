#!/usr/bin/env python3
"""make_compiler_bundle.py -- the pinned portable COMPILER archive a release ships (decision D-A).

  make_compiler_bundle.py --llvm-root <dir> --platform <macos-arm64|macos-x86_64|linux-x86_64|windows-x86_64>
                          --out <dir> [--sysroot <dir>] [--xz-level N]

Takes an LLVM installation (the official release tarball unpacked, Homebrew's llvm, or llvm-mingw) and
writes <out>/compiler-<platform>.tar.xz holding ONLY what the prepared-game pipeline runs:

  bin/clang, bin/clang++ (+ the versioned clang-NN it points to), bin/lld + ld64.lld / ld.lld / lld-link
  when present, bin/llvm-ar, bin/llvm-ranlib (bin/llvm-lib on Windows)
  lib/clang/<NN>/include           the compiler's own headers (stdint.h, arm_neon.h ...)
  lib/clang/<NN>/lib/<os>/         the builtins archive(s) the driver links (libclang_rt.*)
  include/c++/v1                   libc++ headers
  lib/libLLVM*.{dylib,so*}, lib/libclang-cpp*.{dylib,so*}, lib/libc++*.{dylib,so*}   when the binaries are dynamic
  <triple>/                        llvm-mingw's sysroot (headers + import libs) when present
  sysroot/                         --sysroot copied in (Linux: glibc headers + crt objects; see D-P3)

Everything lands under a single top-level directory "compiler/" so the prepare tool finds bin/clang++
at a fixed place. Prints the archive's size and SHA-256 (the release index digest) and writes
<out>/compiler-<platform>.tar.xz.sha256. Exit 0 / 1 (missing driver) / 2 usage.
"""
import argparse
import glob
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--llvm-root", required=True)
    ap.add_argument("--platform", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--sysroot", default="")
    ap.add_argument("--xz-level", type=int, default=6)
    a = ap.parse_args(argv)
    root = os.path.realpath(a.llvm_root)
    exe = ".exe" if a.platform.startswith("windows") else ""
    driver = os.path.join(root, "bin", "clang++" + exe)
    if not os.path.exists(driver):
        sys.stderr.write("make_compiler_bundle: no %s\n" % driver)
        return 1
    os.makedirs(a.out, exist_ok=True)
    stage_root = tempfile.mkdtemp(prefix="compiler_bundle_")
    stage = os.path.join(stage_root, "compiler")
    os.makedirs(stage)
    copied = []

    def put(src, rel):
        dst = os.path.join(stage, rel)
        if os.path.lexists(dst):
            return
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if os.path.islink(src):
            target = os.readlink(src)
            # a symlink inside bin/ (clang++ -> clang -> clang-23): keep it relative, copy the target too
            if not os.path.isabs(target) and "/" not in target:
                if os.path.exists(dst) or os.path.islink(dst):
                    os.remove(dst)
                os.symlink(target, dst)
                copied.append(rel)
                put(os.path.join(os.path.dirname(src), target), os.path.join(os.path.dirname(rel), target))
                return
            src = os.path.realpath(src)
        if os.path.isdir(src):
            shutil.copytree(src, dst, symlinks=True, dirs_exist_ok=True)
        else:
            shutil.copy2(src, dst)
        copied.append(rel)

    for name in ["clang", "clang++", "lld", "ld64.lld", "ld.lld", "lld-link", "llvm-ar", "llvm-ranlib", "llvm-lib"]:
        p = os.path.join(root, "bin", name + exe)
        if os.path.exists(p) or os.path.islink(p):
            put(p, os.path.join("bin", name + exe))
    for p in glob.glob(os.path.join(root, "lib", "clang", "*")):
        ver = os.path.basename(p)
        if os.path.isdir(os.path.join(p, "include")):
            put(os.path.join(p, "include"), os.path.join("lib", "clang", ver, "include"))
        for libdir in glob.glob(os.path.join(p, "lib", "*")):
            for f in glob.glob(os.path.join(libdir, "libclang_rt.*")) + glob.glob(os.path.join(libdir, "*builtins*")):
                if "asan" in f or "fuzzer" in f or "tsan" in f or "ubsan" in f or "profile" in f or "xray" in f or "rtsan" in f or "lsan" in f:
                    continue
                put(f, os.path.relpath(f, root))
    if os.path.isdir(os.path.join(root, "include", "c++", "v1")):
        put(os.path.join(root, "include", "c++", "v1"), os.path.join("include", "c++", "v1"))
    for pat in ("libLLVM*", "libclang-cpp*", "libc++*", "libunwind*"):
        for f in glob.glob(os.path.join(root, "lib", pat + ".dylib")) + glob.glob(os.path.join(root, "lib", pat + ".so*")) + \
                glob.glob(os.path.join(root, "lib", pat + ".dll")) + glob.glob(os.path.join(root, "bin", pat + ".dll")):
            put(f, os.path.relpath(f, root))
    for trip in glob.glob(os.path.join(root, "*-w64-mingw32")):
        put(trip, os.path.basename(trip))
    if a.sysroot:
        put(a.sysroot, "sysroot")
    with open(os.path.join(stage, "COMPILER.txt"), "w") as f:
        ver = subprocess.run([driver, "--version"], capture_output=True, text=True).stdout.strip().split("\n")[0]
        f.write("%s\nfrom %s\nplatform %s\nfiles:\n%s\n" % (ver, root, a.platform, "\n".join(sorted(copied))))
    archive = os.path.join(a.out, "compiler-%s.tar.xz" % a.platform)
    env = dict(os.environ, XZ_OPT="-%d -T0" % a.xz_level)
    subprocess.run(["tar", "-C", stage_root, "-cJf", archive, "compiler"], check=True, env=env)
    shutil.rmtree(stage_root)
    h = hashlib.sha256()
    with open(archive, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    with open(archive + ".sha256", "w") as f:
        f.write("%s  %s\n" % (h.hexdigest(), os.path.basename(archive)))
    print("make_compiler_bundle: %s -- %d bytes, sha256 %s, %d entries (%s)" % (archive, os.path.getsize(archive), h.hexdigest(),
                                                                                 len(copied), ver))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
