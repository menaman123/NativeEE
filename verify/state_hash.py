#!/usr/bin/env python3
"""state_hash.py -- the restore.mhash oracle over a .ps2st anchor file.

Parses the frozen PS2SAVE1 envelope (32-byte FileHeader, 24-byte SectionHeader
+ payload per section, u32 trailer CRC32 over everything before it), validates
every CRC, locates RAM0 / SPAD / META and prints FNV-1a64 over their RAW
payload bytes chained in MemoryCheckpointer order (RAM 32 MB -> SPAD 16 KB ->
META 2 MB). NEW's restore.mhash (the hash of the restored guest memory right
after a state load) must equal this number for every anchor
(PHASE3_PLAN section 4.3). Any difference is a construction bug.

Usage: state_hash.py [--tsv] <anchor.ps2st> [more.ps2st ...]
Output per file: <basename>\t0x<hash>\t<ram_len>\t<spad_len>\t<meta_len>\t<file_version>\t<vsync_frame>
Exit 1 on any envelope/CRC error (the error is printed to stderr).
"""
import os
import struct
import sys
import zlib

MAGIC = b"PS2SAVE1"
FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
MASK64 = (1 << 64) - 1


def tag(s):
    return struct.unpack("<I", s.encode("ascii"))[0]


TAG_RAM, TAG_SPAD, TAG_META = tag("RAM0"), tag("SPAD"), tag("META")


def fnv1a64(data, h=FNV_OFFSET):
    # Byte-serial by definition; the chunked loop keeps Python overhead down.
    prime = FNV_PRIME
    for b in data:
        h = ((h ^ b) * prime) & MASK64
    return h


def parse(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 36 or data[:8] != MAGIC:
        raise ValueError("bad magic / truncated header")
    version, count, vsync_frame, flags, wall_ms = struct.unpack_from("<IIIIQ", data, 8)
    if not 1 <= version <= 4:
        raise ValueError("file version %d out of range" % version)
    off = 32
    sections = {}
    for _ in range(count):
        if off + 24 > len(data) - 4:
            raise ValueError("section table truncated")
        stag, sver, sflags, crc, plen = struct.unpack_from("<IIIIQ", data, off)
        off += 24
        if off + plen > len(data) - 4:
            raise ValueError("payload truncated for tag 0x%08x" % stag)
        payload = data[off:off + plen]
        if zlib.crc32(payload) & 0xFFFFFFFF != crc:
            raise ValueError("payload CRC mismatch for tag 0x%08x" % stag)
        off += plen
        sections[stag] = (sver, sflags, payload)
    trailer = struct.unpack_from("<I", data, off)[0]
    if off + 4 != len(data):
        raise ValueError("trailing bytes after the trailer CRC")
    if zlib.crc32(data[:off]) & 0xFFFFFFFF != trailer:
        raise ValueError("trailer CRC mismatch")
    return version, vsync_frame, sections


def main(argv):
    args = [a for a in argv[1:] if a != "--tsv"]
    if not args:
        sys.stderr.write(__doc__)
        return 2
    rc = 0
    for path in args:
        try:
            version, vsync_frame, sections = parse(path)
            parts = []
            for t, name in ((TAG_RAM, "RAM0"), (TAG_SPAD, "SPAD"), (TAG_META, "META")):
                if t not in sections:
                    raise ValueError("missing section " + name)
                parts.append(sections[t][2])
            h = FNV_OFFSET
            for p in parts:
                h = fnv1a64(p, h)
            print("%s\t0x%016x\t%d\t%d\t%d\t%d\t%d" % (os.path.basename(path), h, len(parts[0]),
                                                      len(parts[1]), len(parts[2]), version, vsync_frame))
            sys.stdout.flush()
        except (OSError, ValueError) as e:
            sys.stderr.write("%s: %s\n" % (path, e))
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
