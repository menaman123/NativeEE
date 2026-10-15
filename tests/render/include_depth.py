#!/usr/bin/env python3
"""Render_IncludeDepth: engine/render/backends/gl may include only engine/render/*,
engine/gs/hle/ir/*, engine/gs/hle/texsrc/texture_source.h, engine/core/hash/* and the ONE
host header engine/platform/host/platform.h (the GL header spelling, PS2R_GL_HEADER)."""
import pathlib, re, sys
root = pathlib.Path(sys.argv[1])
gl = root / 'engine/render/backends/gl'
allowed = re.compile(r'^engine/(render/[^/]+\.h|render/backends/gl/[^/]+\.h|gs/hle/ir/[^/]+\.h|'
                     r'gs/hle/texsrc/texture_source\.h|core/hash/[^/]+\.h|platform/host/platform\.h)$')
bad = 0
for f in sorted(list(gl.glob('*.cpp')) + list(gl.glob('*.h'))):
    for i, line in enumerate(f.read_text().splitlines(), 1):
        m = re.match(r'\s*#\s*include\s+"([^"]+)"', line)
        if m and not allowed.match(m.group(1)):
            print('%s:%d: forbidden include %s' % (f, i, m.group(1)))
            bad += 1
print('Render_IncludeDepth:', 'FAIL' if bad else 'PASS')
sys.exit(1 if bad else 0)
