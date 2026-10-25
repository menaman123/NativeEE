#!/usr/bin/env python3
"""check_getenv_sites.py -- the tree has exactly ONE getenv site: the dev-build
PS2R_SET override in engine/core/config/config_loader.cpp (STAGE_RULES,
ARCHITECTURE section 8). Scans every code root (engine/ app/ games/ tests/ tools/; comments stripped).
Pinned exemptions: the generated TUs' arms, the transplanted generator sources, and the test-only
private-fixture library (tests/lib/private_fixtures.cpp, NATIVEEE_PRIVATE_FIXTURES)."""
import argparse
import os
import re
import sys

ALLOWED = "engine/core/config/config_loader.cpp"
# PHASE3_PLAN 3.3 dated exemption (2026-09-26, stage 3B): the FROZEN recompiler emits 6 getenv
# arms into the title's common TU (PS2_EE_RAWDISP x1, PS2_RECOMP_CORPUS x2, PS2_SHADOW_DIAG x3),
# each OFF unless the variable is set; generated code is never edited, and the in-tree
# recompiler (Phase 9.1) removes them (golden diff = these arms). Exactly this file, no shards.
# PHASE4_PLAN 2.10 / CONSTRUCTION A.1 dated exemption (2026-09-27, stage 4B): the FROZEN
# iop_recompiler_tool emits 5 `getenv("PS2_STRMQ")` probe arms (FLAG_LEDGER DELETE) into the
# title's STREAM.IRX output, each OFF unless the variable is set (NEW sets no env); generated
# code is never edited, and the in-tree iop_recompiler (Phase 9.1) removes them.
# The two generated TUs are NOT in the tree any more (they are derived from the game and regenerated
# into the build directory from the user's disc, cmake/TitleInputs.cmake): when the build generated
# them, PS2R_RECOMP_DIR / PS2R_IOP_RECOMP_DIR name their directories and the counts are checked there;
# when it did not (no title input), the row is reported as not generated and does not fail.
GENERATED_EXEMPT = {
    "games/<title>/recomp/recompiled.cpp": 6,
    "games/<title>/iop/stream_irx/iop_recompiled.cpp": 5,
}
GENERATED_DIRS = {
    "games/<title>/recomp": "PS2R_RECOMP_DIR",
    "games/<title>/iop/stream_irx": "PS2R_IOP_RECOMP_DIR",
}
# P8.8-T (2026-10-03, DP-23): tools/recompiler_v2 holds BYTE-IDENTICAL copies of OLD's
# recompiler_tool_v2 sources (tools/recompiler_v2/PROVENANCE.md). The matches there are the
# GENERATOR'S TEXT of the arms counted above (string literals the tool writes into the common
# TU: recompiler.cpp x5, corpus/recorder_gen.cpp x2) plus OLD's host_app/memory.h (x2), a header
# the tool compiles against, never run by NEW. Pinned per file: a changed count means the
# transplanted tool changed (P8.9-A on: the row moves with the fix, the emitted-arm count
# above moves with the regenerated output).
# tools/iop_recompiler/iop_recompiler.cpp (byte-identical copy of OLD's IOP recompiler, its PROVENANCE.md):
# the generator's text of the 5 PS2_STRMQ arms it writes into iop_recompiled.cpp (one literal, x1).
TRANSPLANT_EXEMPT = {
    "tools/iop_recompiler/iop_recompiler.cpp": 1,
    "tools/recompiler_v2/host_app/memory.h": 2,
    "tools/recompiler_v2/recompiler_tool/corpus/recorder_gen.cpp": 2,
    "tools/recompiler_v2/recompiler_tool/recompiler.cpp": 4,
}
# Public-tree private-fixture contract (2026-10-14, cmake/Ps2rPrivateFixtures.cmake): the game-derived
# test fixtures are outside the repository and TEST code finds them through NATIVEEE_PRIVATE_FIXTURES.
# tests/lib/private_fixtures.cpp (library ps2r_private_fixtures, linked by TESTS only -- never by
# engine/, app/ or games/ product code) reads that variable in Root() and names it in Skip(): the two
# sites below. Pinned per file like the transplants: a changed count means the contract changed.
TEST_FIXTURE_EXEMPT = {
    "tests/lib/private_fixtures.cpp": 2,
}
CODE_EXT = (".h", ".hpp", ".cpp", ".cc", ".inl")
GETENV_RE = re.compile(r"\bgetenv\s*\(")


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    a = ap.parse_args()
    root = os.path.abspath(a.root)
    sites = []
    for sub in ("engine", "app", "games", "tests", "tools"):
        base = os.path.join(root, sub)
        for dp, dn, fn in os.walk(base):
            dn[:] = [x for x in dn if x not in ("build", ".git")]
            for name in fn:
                if not name.endswith(CODE_EXT):
                    continue
                path = os.path.join(dp, name)
                with open(path, errors="replace") as f:
                    code = strip_comments(f.read())
                for m in GETENV_RE.finditer(code):
                    sites.append((os.path.relpath(path, root).replace(os.sep, "/"), code.count("\n", 0, m.start()) + 1))
    # The generated TUs, where the build put them (the tree-relative key is kept for the report).
    generated_present = set()
    for rel_dir, env in GENERATED_DIRS.items():
        gen_dir = os.environ.get(env)
        if not gen_dir:
            continue
        for name in os.listdir(gen_dir) if os.path.isdir(gen_dir) else []:
            if not name.endswith(CODE_EXT):
                continue
            rel = rel_dir + "/" + name
            with open(os.path.join(gen_dir, name), errors="replace") as f:
                code = strip_comments(f.read())
            generated_present.add(rel)
            for m in GETENV_RE.finditer(code):
                sites.append((rel, code.count("\n", 0, m.start()) + 1))
    for rel in GENERATED_EXEMPT:
        if os.path.exists(os.path.join(root, rel)):
            generated_present.add(rel)
    generated = [(rel, line) for rel, line in sites if rel in GENERATED_EXEMPT]
    sites = [(rel, line) for rel, line in sites if rel not in GENERATED_EXEMPT]
    transplanted = [(rel, line) for rel, line in sites if rel in TRANSPLANT_EXEMPT]
    sites = [(rel, line) for rel, line in sites if rel not in TRANSPLANT_EXEMPT]
    fixture_sites = [(rel, line) for rel, line in sites if rel in TEST_FIXTURE_EXEMPT]
    sites = [(rel, line) for rel, line in sites if rel not in TEST_FIXTURE_EXEMPT]
    for rel, line in sites:
        print("%s:%d: getenv" % (rel, line))
    for rel, expected in GENERATED_EXEMPT.items():
        if rel not in generated_present:
            print("generated exemption %s: not generated in this build (no title input) -- not checked" % rel)
            continue
        got = sum(1 for r, _ in generated if r == rel)
        print("generated exemption %s: %d getenv arms (frozen tool output; expected %d)" % (rel, got, expected))
        if got != expected:
            sites.append((rel, -1))  # a changed count means the frozen output changed -> fail
    for rel, expected in TRANSPLANT_EXEMPT.items():
        got = sum(1 for r, _ in transplanted if r == rel)
        print("transplant exemption %s: %d getenv texts (generator source copied from OLD; expected %d)" % (rel, got, expected))
        if got != expected:
            sites.append((rel, -1))  # the transplanted tool changed -> fail until the pin is moved with the change
    for rel, expected in TEST_FIXTURE_EXEMPT.items():
        got = sum(1 for r, _ in fixture_sites if r == rel)
        print("test-fixture exemption %s: %d getenv sites (NATIVEEE_PRIVATE_FIXTURES, tests only; expected %d)" % (rel, got, expected))
        if got != expected:
            sites.append((rel, -1))  # the private-fixture contract changed -> fail until the pin is moved with the change
    ok = len(sites) == 1 and sites[0][0] == ALLOWED
    print("getenv sites: %d -> %s" % (len(sites), "PASS" if ok else "FAIL (expected exactly one, in %s)" % ALLOWED))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
