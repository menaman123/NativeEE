#!/usr/bin/env python3
"""gsstream_mult.py IN.gsstream OUT.gsstream K [--frames N] -- the STRESS capture: every kDraw record of IN written K times
(the worst frame's multiplicity over the captured one; the other records -- VramInit, VramOps, Job, Frame -- once), the first N
frames only when --frames is given. Byte-level over engine/gs/hle/capture/gs_stream_format.h (header 24 B, record header
kind:u32 size:u32), so ANY build's `gs_replay stream OUT --scale 4` replays the synthetic worst frame without the --draw-mult
option. Game-derived output: OUT must live under ~/recomp-agent-data."""
import os, struct, sys

def main():
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    if len(a) != 3:
        print(__doc__); return 2
    src, dst, k = a[0], a[1], int(a[2])
    frames = None
    if '--frames' in sys.argv: frames = int(sys.argv[sys.argv.index('--frames') + 1])
    if not os.path.abspath(dst).startswith(os.path.expanduser('~/recomp-agent-data')):
        print('OUT must live under ~/recomp-agent-data'); return 2
    n = {1: 0, 2: 0, 3: 0, 4: 0, 5: 0}
    with open(src, 'rb') as f, open(dst, 'wb') as o:
        hdr = f.read(24)
        magic, ver = struct.unpack_from('<II', hdr)
        assert magic == 0x4D525453 and ver == 1, 'not a .gsstream v1'
        o.write(hdr)
        nframes = 0
        while True:
            rh = f.read(8)
            if len(rh) < 8: break
            kind, size = struct.unpack('<II', rh)
            payload = f.read(size)
            if len(payload) < size: break
            n[kind] = n.get(kind, 0) + 1
            reps = k if kind == 3 else 1
            for _ in range(reps): o.write(rh); o.write(payload)
            if kind == 5:
                nframes += 1
                if frames and nframes >= frames: break
    print('gsstream_mult: %s -> %s K=%d frames=%d draws_in=%d (x%d) vramops=%d jobs=%d' % (os.path.basename(src), os.path.basename(dst), k, nframes, n.get(3, 0), k, n.get(2, 0), n.get(4, 0)))
    return 0

if __name__ == '__main__':
    sys.exit(main())
