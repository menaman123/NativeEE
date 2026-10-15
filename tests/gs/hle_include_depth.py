#!/usr/bin/env python3
"""Gs_HleIncludeDepth: engine/gs/hle/{targets,texsrc,present} may include only engine/gs/format,
engine/gs/hle/ir, engine/gs/vram, engine/core/hash, engine/render/backend.h,
engine/render/surface_format.h and siblings under engine/gs/hle/{targets,texsrc,present}.
Nothing under engine/render/backends, no GL / SDL headers."""
import pathlib, re, sys
root = pathlib.Path(sys.argv[1])
allowed = re.compile(r'^engine/(gs/format/[^/]+\.h|gs/hle/ir/[^/]+\.h|gs/vram/[^/]+\.h|core/hash/[^/]+\.h|'
                     r'render/backend\.h|render/surface_format\.h|gs/hle/(targets|texsrc|present)/[^/]+\.h)$')
bad = 0
for sub in ('targets', 'texsrc', 'present'):
    d = root / 'engine/gs/hle' / sub
    for f in sorted(list(d.glob('*.cpp')) + list(d.glob('*.h'))):
        for i, line in enumerate(f.read_text().splitlines(), 1):
            m = re.match(r'\s*#\s*include\s+"([^"]+)"', line)
            if m and not allowed.match(m.group(1)):
                print('%s:%d: forbidden include %s' % (f, i, m.group(1))); bad += 1
            m2 = re.match(r'\s*#\s*include\s+<(OpenGL|GL|SDL)', line)
            if m2:
                print('%s:%d: forbidden system include %s' % (f, i, line.strip())); bad += 1
print('Gs_HleIncludeDepth:', 'FAIL' if bad else 'PASS')
sys.exit(1 if bad else 0)
