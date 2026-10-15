#!/usr/bin/env python3
"""check_vu1_library_purity.py -- the G8 gate: the title's VU1 family constants
and every program identity live ONLY under games/<title>/vu1_library/.

Scans engine/ and tools/ (and any extra --scan roots) for:
  * the family signature literals 0x1720, 0x090E0003, the exit stubs
    (`exit_stubs =`), the family exit TPC 0xA0 (as a word);
  * every program stamp `vs_<family>...` and the kVariantBase token;
  * every image / variant hash literal (16 hex digits) that appears anywhere
    under the library directory (--library: the registration table AND the
    programs' image guards) -- generic 16-hex constants elsewhere (the FNV
    basis) are not title data and are not flagged.
Format strings (`%016llx`) are not literals and never match. Exit 0 = 0 hits.
"""
import argparse
import os
import re
import sys

STAMP_RE = re.compile(r"\bvs_(?:family|textured|poscolor|lit|skinned|indexed|zero_kick|sprite|crys)\w*")
CONST_RE = re.compile(r"\b0[xX]1720\b|\b0[xX]090E0003\b|exit_stubs\s*=|\b0[xX]A0\b|\bkVariantBase\b")
LIB_HASH_RE = re.compile(r"\b0[xX]([0-9a-fA-F]{16})u?ll\b")
SOURCE_EXT = (".h", ".cpp", ".inl", ".c", ".hpp", ".py", ".txt", ".cmake")


def library_hashes(lib_dir):
    hs = set()
    if not os.path.isdir(lib_dir):
        return hs
    for dp, _, fns in os.walk(lib_dir):
        for fn in fns:
            if fn.endswith((".h", ".cpp")):
                for m in LIB_HASH_RE.finditer(open(os.path.join(dp, fn), errors="replace").read()):
                    h = m.group(1).lower()
                    if int(h, 16) != 0:
                        hs.add(h)
    return hs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--scan", action="append", default=None, help="roots relative to --root (default engine, tools)")
    ap.add_argument("--library", default="games/<title>/vu1_library")
    a = ap.parse_args()
    scans = a.scan or ["engine", "tools"]
    hashes = library_hashes(os.path.join(a.root, a.library))
    hash_re = re.compile(r"(?<![0-9a-fA-F])(" + "|".join(sorted(hashes)) + r")(?![0-9a-fA-F])", re.I) if hashes else None

    hits = []
    files = 0
    for scan in scans:
        for dp, dns, fns in os.walk(os.path.join(a.root, scan)):
            # tools/recompiler_v2 holds byte-identical copies of OLD's EE recompiler sources
            # (P8.8-T, DP-23; tools/recompiler_v2/PROVENANCE.md): an EE tool, no VU1 library data;
            # its generic 0xA0 byte literals (opcode masks, json.hpp UTF-8) are not the family exit TPC.
            if os.path.relpath(dp, a.root) == "tools":
                dns[:] = [d for d in dns if d != "recompiler_v2"]
            for fn in fns:
                if not fn.endswith(SOURCE_EXT):
                    continue
                path = os.path.join(dp, fn)
                files += 1
                for ln, line in enumerate(open(path, errors="replace"), 1):
                    for rx, what in ((STAMP_RE, "stamp"), (CONST_RE, "family-const")):
                        for m in rx.finditer(line):
                            hits.append((path, ln, what, m.group(0)))
                    if hash_re:
                        for m in hash_re.finditer(line):
                            hits.append((path, ln, "library-hash", m.group(0)))
    for path, ln, what, tok in hits:
        print("%s:%d: %s %s" % (os.path.relpath(path, a.root), ln, what, tok))
    print("Vu1LibraryPurity: %d files scanned under %s, %d library hashes, %d hits -> %s"
          % (files, ",".join(scans), len(hashes), len(hits), "FAIL" if hits else "PASS"))
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main())
