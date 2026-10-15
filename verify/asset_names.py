#!/usr/bin/env python3
"""asset_names.py -- a content-fingerprint name table for the title's packed asset archive.

  asset_names.py --index <archive index> --archive <archive> --out <names.tsv> [--probe 64]
  asset_names.py --selftest

The title's loader streams whole files out of one packed archive into records of its asset heap
(the record pool `relocate_compare.py` walks), so a heap record's identity is the file it holds.
This tool reads the archive's INDEX -- a u32 header word followed by entries `{u32 name_len, name
bytes (no terminator), u32 byte offset, u32 byte size}` to the end of the file (the entries are
contiguous extents of the archive; the walk is checked: the last extent must end at the archive's
size) -- and writes one line per file:

    <sha1 of the extent's first PROBE bytes>\t<name>\t<offset>\t<size>

`relocate_compare.py --names <names.tsv>` names a pool record whose first PROBE bytes hash to a
table line (a whole file placed at the record's start). Files shorter than PROBE bytes are hashed
over their whole extent and marked `short`; two files with the same fingerprint are reported
(`ambiguous`) and never named. No guest address lives in this tool; the archive paths are given on
the command line (title data).
"""
import argparse
import hashlib
import os
import struct
import sys

PROBE = 64


def read_index(path, archive_size=None):
    """-> [(name, offset, size)] in index order; raises on a malformed table or an extent walk that
    does not end at the archive's size."""
    data = open(path, "rb").read()
    if len(data) < 8:
        raise ValueError("index too short: %d bytes" % len(data))
    off = 4
    out = []
    while off + 4 <= len(data):
        ln = struct.unpack_from("<I", data, off)[0]
        if ln == 0 or ln > 1024 or off + 4 + ln + 8 > len(data):
            raise ValueError("index: bad name length %d at offset 0x%x (entry %d)" % (ln, off, len(out)))
        name = data[off + 4:off + 4 + ln].decode("latin1")
        o, s = struct.unpack_from("<II", data, off + 4 + ln)
        out.append((name, o, s))
        off += 4 + ln + 8
    if off != len(data):
        raise ValueError("index: %d trailing bytes after entry %d" % (len(data) - off, len(out)))
    for a, b in zip(out, out[1:]):
        if a[1] + a[2] != b[1]:
            raise ValueError("index: extents not contiguous at %r -> %r" % (a, b))
    if archive_size is not None and out and out[-1][1] + out[-1][2] != archive_size:
        raise ValueError("index: last extent ends at 0x%x, archive size 0x%x" % (out[-1][1] + out[-1][2], archive_size))
    return out


def fingerprints(index_entries, archive_path, probe=PROBE):
    """-> [(sha1, name, offset, size, flag)] with flag '' / 'short' / 'ambiguous'."""
    rows = []
    seen = {}
    with open(archive_path, "rb") as f:
        for name, o, s in index_entries:
            f.seek(o)
            chunk = f.read(min(s, probe))
            h = hashlib.sha1(chunk).hexdigest()
            flag = "short" if s < probe else ""
            rows.append([h, name, o, s, flag])
            seen.setdefault(h, []).append(len(rows) - 1)
    for h, idxs in seen.items():
        if len(idxs) > 1:
            for i in idxs:
                rows[i][4] = (rows[i][4] + ",ambiguous").strip(",")
    return [tuple(r) for r in rows]


def write_table(rows, path, probe=PROBE):
    with open(path, "w") as f:
        f.write("# asset_names.py: sha1 of the first %d bytes of each archive extent\tname\toffset\tsize\tflag\n" % probe)
        for h, name, o, s, flag in rows:
            f.write("%s\t%s\t%d\t%d\t%s\n" % (h, name, o, s, flag))


def load_table(path):
    """-> ({sha1: (name, offset, size)} over the unambiguous lines, probe bytes)."""
    probe = PROBE
    out = {}
    with open(path) as f:
        for line in f:
            if line.startswith("#"):
                m = line.split("first ")
                if len(m) > 1:
                    try:
                        probe = int(m[1].split()[0])
                    except ValueError:
                        pass
                continue
            cols = line.rstrip("\n").split("\t")
            if len(cols) < 4:
                continue
            flag = cols[4] if len(cols) > 4 else ""
            if "ambiguous" in flag:
                continue
            out[cols[0]] = (cols[1], int(cols[2]), int(cols[3]))
    return out, probe


def name_bytes(table, probe, buf, lo, size):
    """The table line for the record at buf[lo:lo+size], or None (a whole file at the record's start)."""
    if size <= 0:
        return None
    h = hashlib.sha1(bytes(buf[lo:lo + min(size, probe)])).hexdigest()
    hit = table.get(h)
    if hit is None:
        return None
    return hit


def selftest():
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)
    tmp = tempfile.mkdtemp(prefix="asset_names_")
    files = [("Startup\\A.bin", bytes(range(200))), ("Levels\\B.bin", bytes(range(100, 200)) * 3), ("Short.txt", b"tiny"),
             ("Dup1.bin", b"\x55" * 80), ("Dup2.bin", b"\x55" * 80 + b"\x01")]
    arch = b""
    idx = struct.pack("<I", 0x501)
    for name, body in files:
        idx += struct.pack("<I", len(name)) + name.encode("latin1") + struct.pack("<II", len(arch), len(body))
        arch += body
    ip, ap, tp = (os.path.join(tmp, x) for x in ("index.bh", "archive.bd", "names.tsv"))
    open(ip, "wb").write(idx)
    open(ap, "wb").write(arch)
    ents = read_index(ip, len(arch))
    check([e[0] for e in ents] == [f[0] for f in files] and ents[1][1] == 200, "index parsed: 5 names, B at offset 200")
    rows = fingerprints(ents, ap)
    check(rows[2][4] == "short", "a file shorter than the probe is flagged short")
    check("ambiguous" in rows[3][4] and "ambiguous" in rows[4][4], "two files with the same first 64 bytes are flagged ambiguous")
    write_table(rows, tp)
    table, probe = load_table(tp)
    check(probe == 64 and len(table) == 3, "table loads the 3 unambiguous lines, probe 64 -> %d lines" % len(table))
    rec = bytearray(0x200)
    rec[0x40:0x40 + 300] = files[1][1]
    check(name_bytes(table, probe, rec, 0x40, 0x140) == ("Levels\\B.bin", 200, 300), "a record holding B at its start is named B")
    check(name_bytes(table, probe, rec, 0x44, 0x140) is None, "an offset record is not named (whole file at the record start only)")
    check(name_bytes(table, probe, bytearray(b"\x55" * 80), 0, 80) is None, "an ambiguous fingerprint names nothing")
    bad = idx[:-4] + b"\x00\x00\x00\x00"
    open(ip, "wb").write(bad)
    try:
        read_index(ip, len(arch))
        check(False, "a table whose last extent misses the archive size must raise")
    except ValueError as e:
        check("archive size" in str(e), "extent walk checked against the archive size -> %s" % e)
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)
    print("asset_names selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--index")
    ap.add_argument("--archive")
    ap.add_argument("--out")
    ap.add_argument("--probe", type=int, default=PROBE)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not (a.index and a.archive and a.out):
        ap.error("--index, --archive and --out are required")
    ents = read_index(a.index, os.path.getsize(a.archive))
    rows = fingerprints(ents, a.archive, a.probe)
    write_table(rows, a.out, a.probe)
    amb = sum(1 for r in rows if "ambiguous" in r[4])
    short = sum(1 for r in rows if "short" in r[4])
    print("asset_names: %d files, %d ambiguous, %d short -> %s" % (len(rows), amb, short, a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
