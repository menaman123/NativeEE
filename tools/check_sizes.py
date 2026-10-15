#!/usr/bin/env python3
"""Size budgets for the Phase-6 tree (ARCHITECTURE §11 / PHASE6_PLAN §2 conventions):
every file <= 1,500 lines, every function <= 150 lines (brace-depth heuristic over the
comment-stripped text; a function starts at a line ending in `) {` with an optional
const/noexcept/override and not led by a control keyword).

Scope (PHASE6_PLAN §1.5 write set): engine/gs (excluding engine/gs/format, Phase 2's),
engine/render, engine/platform/sdl, tools/gs_replay, tools/drawlist_tool, tests/lib and
games/<title>/renderer_profile. Generated recomp output is never scanned.

    python3 tools/check_sizes.py [ROOT]        # exit 0 = OK, 1 = a budget was exceeded
ctest: Phase6SizeBudgets (tests/gs/CMakeLists.txt).
"""
import pathlib
import re
import sys

FILE_BUDGET = 1500
FUNC_BUDGET = 150

SCOPE_DIRS = [
    'engine/gs', 'engine/render', 'engine/platform/sdl',
    'tools/gs_replay', 'tools/drawlist_tool', 'tests/lib',
]
TITLE_GLOB = 'games/*/renderer_profile'   # the title's renderer profile directory
EXCLUDE_PARTS = ('format',)               # engine/gs/format is Phase 2's

FUNC_OPEN = re.compile(r'\)\s*(const)?\s*(noexcept)?\s*(override)?\s*\{\s*$')
CONTROL = re.compile(r'\s*(if|for|while|switch|else|do)\b')


def files_in_scope(root):
    out = []
    for d in SCOPE_DIRS:
        p = root / d
        if not p.exists():
            continue
        for f in list(p.rglob('*.cpp')) + list(p.rglob('*.h')):
            rel = f.relative_to(root).parts
            if d == 'engine/gs' and len(rel) > 2 and rel[2] in EXCLUDE_PARTS:
                continue
            out.append(f)
    for prof in root.glob(TITLE_GLOB):
        out += list(prof.rglob('*.cpp')) + list(prof.rglob('*.h'))
    return sorted(set(out))


def check_file(f):
    fail = 0
    lines = f.read_text(errors='replace').splitlines()
    if len(lines) > FILE_BUDGET:
        print(f'FILE  {f}: {len(lines)} lines > {FILE_BUDGET}')
        fail = 1
    depth, start, name = 0, None, None
    for i, ln in enumerate(lines, 1):
        s = re.sub(r'//.*', '', ln)
        if depth == 0 and FUNC_OPEN.search(s) and not CONTROL.match(s):
            start, name = i, s.strip()[:60]
        depth += s.count('{') - s.count('}')
        if depth == 0 and start is not None:
            n = i - start + 1
            if n > FUNC_BUDGET:
                print(f'FUNC  {f}:{start}: {n} lines > {FUNC_BUDGET}  {name}')
                fail = 1
            start = None
        if depth < 0:
            depth = 0
    return fail


def main():
    root = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path('.')
    files = files_in_scope(root)
    fail = 0
    for f in files:
        fail |= check_file(f)
    print(f'size check {"FAIL" if fail else "OK"} ({len(files)} files, file <= {FILE_BUDGET}, function <= {FUNC_BUDGET})')
    return fail


if __name__ == '__main__':
    sys.exit(main())
