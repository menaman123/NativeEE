#!/usr/bin/env python3
"""lift_og_tables.py -- lift the frozen ORIGINAL's GS address tables into a fixture.

Reads OLD host_app/texture_decoder.cpp READ-ONLY (never edits, never builds it) and
extracts, by name, the constexpr initializers of the seven block tables and the
three column tables the OLD GetAddressPSM* family uses, plus the OLD PSMT4 column
formula (linear: nibble = (x % 32) + (y % 4) * 32; texture_decoder.cpp
GetAddressPSMT4 / PageLUTs) evaluated into a [16][32] table so the oracle can treat
every OLD variant uniformly.

Output: og_tables.bin (little-endian: magic 'OGT1', u32 table count, then per table
32-byte NUL-padded name, u16 rows, u16 cols, rows*cols u16 values) and its sha256 in
og_tables.sha256. Both are checked in; `--check` verifies the checked-in pair without
needing the OLD tree, `--lift` regenerates from OLD and fails if the result differs
from the checked-in fixture (use `--write` to accept a new lift).
"""
import argparse
import hashlib
import os
import re
import struct
import sys

TABLES = [
    # name in OLD source,        rows, cols
    ("block_table_PSMCT32", 4, 8),
    ("block_table_PSMZ32", 4, 8),
    ("block_table_PSMCT16", 8, 4),
    ("block_table_PSMZ16", 8, 4),
    ("block_table_PSMZ16S", 8, 4),
    ("block_table_PSMT8", 4, 8),
    ("block_table_PSMT4", 8, 4),
    ("columnTable32", 8, 8),
    ("columnTable16", 8, 16),
    ("columnTable8", 16, 16),
]
MAGIC = b"OGT1"


def parse_table(src, name, rows, cols):
    m = re.search(r"\b" + re.escape(name) + r"\s*\[\s*\d+\s*\]\s*\[\s*\d+\s*\]\s*=\s*\{", src)
    if not m:
        raise SystemExit("table %s not found in OLD source" % name)
    depth, i, start = 1, m.end(), m.end()
    while depth > 0:
        c = src[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        i += 1
    body = src[start:i - 1]
    body = re.sub(r"//[^\n]*", "", body)
    values = [int(v) for v in re.findall(r"-?\d+", body)]
    if len(values) != rows * cols:
        raise SystemExit("table %s: expected %d values, found %d" % (name, rows * cols, len(values)))
    return values


def old_t4_column_table():
    # OLD GetAddressPSMT4: column = (y%16)/4; nibble = (x%32) + (y%4)*32;
    # byte_in_block = column*64 + nibble/2. Stored here in NIBBLE units within
    # the 256-byte block (0..511) so the oracle compares like with like.
    out = []
    for y in range(16):
        for x in range(32):
            column = (y % 16) // 4
            nibble = (x % 32) + (y % 4) * 32
            out.append(column * 128 + nibble)
    return out


def lift(og_root):
    path = os.path.join(og_root, "host_app", "texture_decoder.cpp")
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        src = f.read()
    tables = []
    for name, rows, cols in TABLES:
        tables.append((name, rows, cols, parse_table(src, name, rows, cols)))
    tables.append(("og_t4_column_formula", 16, 32, old_t4_column_table()))
    return tables


def encode(tables):
    out = bytearray(MAGIC)
    out += struct.pack("<I", len(tables))
    for name, rows, cols, values in tables:
        nb = name.encode("ascii")
        if len(nb) > 32:
            raise SystemExit("name too long: " + name)
        out += nb + b"\0" * (32 - len(nb))
        out += struct.pack("<HH", rows, cols)
        out += struct.pack("<%dH" % len(values), *values)
    return bytes(out)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--og-root", default="")
    ap.add_argument("--out", required=True, help="og_tables.bin path")
    ap.add_argument("--check", action="store_true", help="verify out matches out.sha256 (no OLD needed)")
    ap.add_argument("--lift", action="store_true", help="lift from OLD and compare to the checked-in fixture")
    ap.add_argument("--write", action="store_true", help="with --lift: write the fixture + sha")
    a = ap.parse_args()
    sha_path = a.out[: -len(".bin")] + ".sha256" if a.out.endswith(".bin") else a.out + ".sha256"

    if a.check:
        if not os.path.exists(a.out) or not os.path.exists(sha_path):
            print("lift_og_tables: FAIL fixture or sha missing (%s)" % a.out)
            return 1
        with open(a.out, "rb") as f:
            data = f.read()
        with open(sha_path, "r") as f:
            want = f.read().split()[0]
        got = sha256(data)
        if data[:4] != MAGIC or got != want:
            print("lift_og_tables: FAIL sha mismatch want %s got %s" % (want, got))
            return 1
        n = struct.unpack_from("<I", data, 4)[0]
        print("lift_og_tables: OK %d tables, sha256 %s" % (n, got))
        return 0

    if a.lift:
        if not a.og_root or not os.path.exists(os.path.join(a.og_root, "host_app", "texture_decoder.cpp")):
            print("lift_og_tables: SKIP (OLD tree not found at '%s')" % a.og_root)
            return 0
        data = encode(lift(a.og_root))
        got = sha256(data)
        if a.write:
            with open(a.out, "wb") as f:
                f.write(data)
            with open(sha_path, "w") as f:
                f.write(got + "  og_tables.bin\n")
            print("lift_og_tables: WROTE %s (%d bytes) sha256 %s" % (a.out, len(data), got))
            return 0
        if not os.path.exists(a.out):
            print("lift_og_tables: FAIL no checked-in fixture; run with --write")
            return 1
        with open(a.out, "rb") as f:
            have = f.read()
        if have != data:
            print("lift_og_tables: FAIL OLD tables differ from the checked-in fixture (sha %s vs %s)"
                  % (sha256(have), got))
            return 1
        print("lift_og_tables: OK re-lift identical to fixture, sha256 %s" % got)
        return 0

    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
