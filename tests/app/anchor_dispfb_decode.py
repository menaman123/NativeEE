#!/usr/bin/env python3
"""anchor_dispfb_decode.py -- the frames-lane hash of a restored anchor's DISPFB
buffer, decoded out of the anchor's own VRAM section: what the CRTC scans out at
the first vblank after the restore, and therefore what NEW's key-1 present (the
restore point, boundary 0) must hash to (DP-26 (a), BUG-076).

The decode mirrors engine/gs: the GSPR section's DISPFB1 (FBP in 8 KiB units,
FBW in 64-px units, PSM; GS User's Manual 4.4), the DISPLAY1 pixel height
((DH + 1) / (MAGV + 1); 4.5), PSMCT16's block / column swizzle (8.3.2 / 8.4, as
engine/gs/format/psm_layout.cpp), the tree's one 5 -> 8 widening -- bit
replication (c << 3) | (c >> 2), engine/gs/format/texa_expand.h -- and
FrameHasher::HashNativeScale (engine/core/verify/frame_hasher.cpp: FNV-1a64 over
u32 w, u32 h little-endian, then R, G, B per pixel, rows TOP-DOWN -- the grab
buffer is bottom-up and the hasher reads it top-down, so a correctly oriented
grab hashes like the PS2 image itself). Only PSMCT16 displays are decoded (the
frozen anchors' tuple); any other PSM returns None.

  anchor_dispfb_decode.py <anchor.ps2st> [--repo ROOT]   prints the tuple + hash
"""
import os
import struct
import sys

FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
MASK64 = (1 << 64) - 1

# PSMCT16 layout (GS User's Manual 8.3.2 / 8.4): 64x64 pages of 32 blocks (16x8),
# a block of 4 columns (16x2), 16-bit elements.
BLOCK16 = [
    [0, 2, 8, 10], [1, 3, 9, 11], [4, 6, 12, 14], [5, 7, 13, 15],
    [16, 18, 24, 26], [17, 19, 25, 27], [20, 22, 28, 30], [21, 23, 29, 31],
]
COLUMN_WORD = [[0, 1, 4, 5, 8, 9, 12, 13], [2, 3, 6, 7, 10, 11, 14, 15]]
VRAM_BYTES = 4 * 1024 * 1024


def _tag(s):
    return struct.unpack("<I", s.encode("ascii"))[0]


TAG_GSPR = _tag("GSPR")
TAG_VRAM = _tag("VRAM")


def ct16_byte_addr(x, y, bp_blocks, bw_pages):
    page = (y // 64) * bw_pages + (x // 64)
    lx, ly = x % 64, y % 64
    block = BLOCK16[(ly // 8) & 7][(lx // 16) & 3]
    bx, by = lx % 16, ly % 8
    column, cy = by // 2, by % 2
    elem_in_block = column * 32 + COLUMN_WORD[cy][bx & 7] * 2 + (bx >> 3)
    element = bp_blocks * 128 + page * 4096 + block * 128 + elem_in_block
    return (element * 2) % VRAM_BYTES


def replicate5(c):
    return ((c << 3) | (c >> 2)) & 0xFF


def hash_rgb_topdown(w, h, rgb):
    hsh = FNV_OFFSET
    for b in struct.pack("<II", w, h):
        hsh = ((hsh ^ b) * FNV_PRIME) & MASK64
    for b in rgb:
        hsh = ((hsh ^ b) * FNV_PRIME) & MASK64
    return hsh


def sections(path, repo_root):
    """{tag: payload} of the anchor through verify/state_hash.py's parser."""
    sys.path.insert(0, os.path.join(repo_root, "verify"))
    import state_hash  # noqa: E402
    _version, _vf, secs = state_hash.parse(path)
    return {tag: payload for tag, (_sver, _sflags, payload) in secs.items()}


def dispfb_decode(path, repo_root):
    """dict(fbp, fbw_px, psm, w, h, fhash) for a PSMCT16 DISPFB1; None otherwise."""
    secs = sections(path, repo_root)
    if TAG_GSPR not in secs or TAG_VRAM not in secs:
        return None
    regs = struct.unpack_from("<19Q", secs[TAG_GSPR], 0)
    dispfb1, display1 = regs[7], regs[8]
    fbp, fbw, psm = dispfb1 & 0x1FF, (dispfb1 >> 9) & 0x3F, (dispfb1 >> 15) & 0x1F
    magv, dh = (display1 >> 27) & 3, (display1 >> 44) & 0x7FF
    w, h = fbw * 64, (dh + 1) // (magv + 1)
    if psm != 0x02 or w <= 0 or h <= 0:
        return None
    vram = secs[TAG_VRAM]
    out = bytearray(w * h * 3)
    i = 0
    bp_blocks = fbp * 32   # 8 KiB units = 32 blocks of 256 B
    for y in range(h):
        for x in range(w):
            a = ct16_byte_addr(x, y, bp_blocks, fbw)
            px = vram[a] | (vram[a + 1] << 8)
            out[i] = replicate5(px & 0x1F)
            out[i + 1] = replicate5((px >> 5) & 0x1F)
            out[i + 2] = replicate5((px >> 10) & 0x1F)
            i += 3
    return dict(fbp=fbp, fbw_px=w, psm=psm, w=w, h=h, fhash=hash_rgb_topdown(w, h, bytes(out)))


def main():
    argv = sys.argv[1:]
    repo = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    if "--repo" in argv:
        i = argv.index("--repo")
        repo = argv[i + 1]
        del argv[i:i + 2]
    if len(argv) != 1:
        print(__doc__)
        return 2
    d = dispfb_decode(argv[0], repo)
    if d is None:
        print("%s: DISPFB1 not a PSMCT16 display (or sections missing)" % argv[0])
        return 1
    print("%s: DISPFB1 FBP=%d FBW=%d px PSM=%d -> %dx%d fhash=0x%016x"
          % (argv[0], d["fbp"], d["fbw_px"], d["psm"], d["w"], d["h"], d["fhash"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
