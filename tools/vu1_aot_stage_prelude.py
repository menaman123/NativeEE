#!/usr/bin/env python3
"""vu1_aot_stage_prelude.py -- stage the headers a GENERATED VU1 image TU needs.

The VU1 AOT generator (engine/vu/vu1_aot/Vu1AotEmitter) emits C++ that includes ONE
header, engine/vu/vu1_aot/vu1_aot_prelude.h, and is compiled at RUN time by
Vu1AotBuilder outside the source tree. This script asks the build's own compiler
for the prelude's include closure (-MM) and copies exactly those files, under their
tree-relative paths, into --out (plus MANIFEST.txt listing them), so the running
engine can hand the generated TU an include root that matches the engine build it
came from. Nothing is hand-maintained: a header the prelude starts to pull is staged
on the next build.

    vu1_aot_stage_prelude.py --compiler <c++> --root <source root> --out <dir> [--define NAME=VAL]...

Exit 0 on success, 1 when the compiler could not produce the dependency list.
"""
import argparse
import os
import shutil
import subprocess
import sys

PRELUDE = "engine/vu/vu1_aot/vu1_aot_prelude.h"


def closure(compiler, root, defines):
    cmd = [compiler, "-std=c++17", "-I" + root, "-MM", "-x", "c++", os.path.join(root, PRELUDE)]
    for d in defines:
        cmd.append("-D" + d)
    try:
        out = subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        sys.stderr.write("vu1_aot_stage_prelude: %s\n" % e)
        return None
    deps = out.replace("\\\n", " ").split(":", 1)[1].split()
    rel = []
    root_abs = os.path.abspath(root)
    for d in deps:
        a = os.path.abspath(d if os.path.isabs(d) else os.path.join(root, d))
        if a.startswith(root_abs + os.sep):
            rel.append(os.path.relpath(a, root_abs))
    return sorted(set(rel))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--compiler", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--define", action="append", default=[])
    a = ap.parse_args()
    files = closure(a.compiler, a.root, a.define)
    if not files:
        return 1
    manifest = os.path.join(a.out, "MANIFEST.txt")
    have = []
    if os.path.exists(manifest):
        with open(manifest) as f:
            have = [l.strip() for l in f if l.strip()]
    changed = have != files
    for rel in files:
        src = os.path.join(a.root, rel)
        dst = os.path.join(a.out, rel)
        if (not os.path.exists(dst) or os.path.getmtime(dst) < os.path.getmtime(src)
                or os.path.getsize(dst) != os.path.getsize(src)):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
            changed = True
    # Files no longer in the closure are removed so the staged tree IS the closure.
    for rel in have:
        if rel not in files:
            try:
                os.remove(os.path.join(a.out, rel))
            except OSError:
                pass
            changed = True
    if changed or not os.path.exists(manifest):
        os.makedirs(a.out, exist_ok=True)
        with open(manifest, "w") as f:
            f.write("\n".join(files) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
