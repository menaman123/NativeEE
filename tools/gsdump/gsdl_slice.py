#!/usr/bin/env python3
"""gsdl_slice.py -- stream a frame range out of a large `.gsdl` recording into a small one.

Copies the file header, every kName record (so handler names resolve) and the kDraw / kJob /
kFrame records of frames [first, last] (0-based frame index = count of kFrame records seen
before the record). Reads the input in a streaming pass, so a multi-GB recording of a long
leg can be reduced to the frames of interest without loading it into memory.

Usage: gsdl_slice.py <in.gsdl> <out.gsdl> <first> [last]
       gsdl_slice.py --selftest
"""
import os
import struct
import sys
import tempfile

HDR = struct.Struct("<IIIIQ")
REC = struct.Struct("<II")
K_JOB, K_DRAW, K_FRAME, K_NAME = 1, 2, 3, 4


def slice_file(src, dst, first, last):
    kept = dict(draws=0, jobs=0, frames=0, names=0)
    with open(src, "rb") as f, open(dst, "wb") as o:
        h = f.read(HDR.size)
        magic = HDR.unpack(h)[0]
        if magic != 0x524C4432:
            raise ValueError("not a .gsdl: magic %#x" % magic)
        o.write(h)
        frame = 0
        while True:
            rh = f.read(REC.size)
            if len(rh) < REC.size:
                break
            kind, size = REC.unpack(rh)
            payload = f.read(size)
            if len(payload) < size:
                break
            if kind == K_NAME:
                o.write(rh + payload)
                kept["names"] += 1
                continue
            if first <= frame <= last:
                o.write(rh + payload)
                if kind == K_DRAW:
                    kept["draws"] += 1
                elif kind == K_JOB:
                    kept["jobs"] += 1
                elif kind == K_FRAME:
                    kept["frames"] += 1
            if kind == K_FRAME:
                frame += 1
                if frame > last:
                    break
    return kept


def selftest():
    ok = True

    def check(c, w):
        nonlocal ok
        print(("PASS  " if c else "FAIL  ") + w)
        ok = ok and c

    d = tempfile.mkdtemp()
    src = os.path.join(d, "in.gsdl")
    dst = os.path.join(d, "out.gsdl")
    with open(src, "wb") as f:
        f.write(HDR.pack(0x524C4432, 2, 1, 0, 0))
        f.write(REC.pack(K_NAME, 12) + struct.pack("<II", 7, 4) + b"name")
        for fr in range(4):
            f.write(REC.pack(K_DRAW, 4) + struct.pack("<I", fr))
            f.write(REC.pack(K_DRAW, 4) + struct.pack("<I", fr))
            f.write(REC.pack(K_JOB, 4) + struct.pack("<I", fr))
            f.write(REC.pack(K_FRAME, 4) + struct.pack("<I", fr))
    kept = slice_file(src, dst, 1, 2)
    check(kept == dict(draws=4, jobs=2, frames=2, names=1), "kept counts %s" % kept)
    data = open(dst, "rb").read()
    check(data[:HDR.size] == HDR.pack(0x524C4432, 2, 1, 0, 0), "header copied")
    pos = HDR.size
    seen = []
    while pos < len(data):
        kind, size = REC.unpack_from(data, pos)
        seen.append((kind, struct.unpack_from("<I", data, pos + 8)[0] if kind != K_NAME else -1))
        pos += 8 + size
    check(seen == [(K_NAME, -1), (K_DRAW, 1), (K_DRAW, 1), (K_JOB, 1), (K_FRAME, 1),
                   (K_DRAW, 2), (K_DRAW, 2), (K_JOB, 2), (K_FRAME, 2)], "record order: names first, frames 1..2 only")
    for p in (src, dst):
        os.remove(p)
    os.rmdir(d)
    print("gsdl_slice selftest: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main(argv):
    if "--selftest" in argv:
        return selftest()
    if len(argv) < 4:
        print(__doc__)
        return 2
    first = int(argv[3])
    last = int(argv[4]) if len(argv) > 4 else first
    kept = slice_file(argv[1], argv[2], first, last)
    print("%s -> %s frames[%d..%d]: %s" % (argv[1], argv[2], first, last, kept))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
