#!/usr/bin/env python3
"""recomp_shard_bodies_sha.py -- the split-independent hash of the generated EE bodies.

The recompiler packs the 8,807 function bodies into N shard TUs (`--shards N`). Every shard
starts with the same include prologue (write_cpp_includes) and `extern std::ofstream
g_logFile;`; the bodies follow in address order. Re-splitting moves only the TU boundaries,
so the concatenation of the bodies with the prologues stripped must be byte-identical across
shard counts. This tool computes that hash so a re-split can be PROVEN body-identical:

    tools/recomp_shard_bodies_sha.py DIR            print `<sha256>  bodies(<shards> shards, <bytes> B)`
    tools/recomp_shard_bodies_sha.py DIR --check H  exit 1 unless the hash equals H
    tools/recomp_shard_bodies_sha.py A --against B  exit 1 unless both directories agree (any shard counts)

Only recompiled_shard_*.cpp are read (the common TU and the header are split-independent files
with their own MANIFEST rows). The prologue is cut at the first `extern std::ofstream g_logFile;`
line; a shard without that marker is an error, never silently included.
"""
import argparse
import hashlib
import re
import sys
from pathlib import Path

MARKER = b"extern std::ofstream g_logFile;\n"
SHARD_RE = re.compile(r"^recompiled_shard_(\d{3})\.cpp$")


def shard_paths(directory):
    paths = []
    for p in Path(directory).iterdir():
        m = SHARD_RE.match(p.name)
        if m:
            paths.append((int(m.group(1)), p))
    paths.sort()
    if not paths:
        raise SystemExit("recomp_shard_bodies_sha: no recompiled_shard_NNN.cpp under %s" % directory)
    for want, (got, p) in enumerate(paths):
        if want != got:
            raise SystemExit("recomp_shard_bodies_sha: shard numbering gap at %03d (%s)" % (want, p))
    return [p for _, p in paths]


def body_of(path):
    data = path.read_bytes()
    at = data.find(MARKER)
    if at < 0:
        raise SystemExit("recomp_shard_bodies_sha: %s has no `%s` prologue marker" % (path, MARKER.decode().strip()))
    body = data[at + len(MARKER):]
    # The emitter writes exactly one blank line after the marker, then the bodies back to
    # back (each body ends with its own `}\n\n`), so the shard stream after that newline IS
    # the body stream: concatenating it across shards reproduces the single-TU body text
    # byte for byte, whatever the shard count.
    if not body.startswith(b"\n"):
        raise SystemExit("recomp_shard_bodies_sha: %s: no blank line after the prologue marker" % path)
    return body[1:]


def bodies_sha(directory):
    h = hashlib.sha256()
    total = 0
    paths = shard_paths(directory)
    for p in paths:
        b = body_of(p)
        h.update(b)
        total += len(b)
    return h.hexdigest(), len(paths), total


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("directory")
    ap.add_argument("--check", metavar="SHA256", help="fail unless the bodies hash equals this value")
    ap.add_argument("--against", metavar="DIR", help="fail unless DIR's bodies hash equals this directory's")
    a = ap.parse_args(argv)
    sha, n, total = bodies_sha(a.directory)
    print("%s  bodies(%d shards, %d B)  %s" % (sha, n, total, a.directory))
    if a.check and sha != a.check:
        print("recomp_shard_bodies_sha: MISMATCH: expected %s" % a.check, file=sys.stderr)
        return 1
    if a.against:
        sha2, n2, total2 = bodies_sha(a.against)
        print("%s  bodies(%d shards, %d B)  %s" % (sha2, n2, total2, a.against))
        if sha2 != sha:
            print("recomp_shard_bodies_sha: MISMATCH between the two directories", file=sys.stderr)
            return 1
        print("recomp_shard_bodies_sha: IDENTICAL bodies across %d vs %d shards" % (n, n2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
