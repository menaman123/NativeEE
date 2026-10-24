#!/usr/bin/env python3
"""lint_ui_strings.py -- no codenames, no developer vocabulary in anything a player reads.

  lint_ui_strings.py <file.h|file.cpp|file.json> [...]

Scans every string literal ("..." in C/C++ sources; every string value in JSON) and fails on a word
from DENYLIST (whole-word, case-insensitive) or a codename from CODENAMES. The prepare pipeline keeps
its player-facing text in tools/prepare/ui_strings.h; the launcher runs this over its own sources
too. "Preparing your game" is the only name for the prepare step: a literal that names the step
otherwise ("installing", "building", "compiling", "setting up") is a failure as well.

Exit 0 clean, 1 findings, 2 usage.
"""
import json
import re
import sys

DENYLIST = {
    # the pipeline's own vocabulary
    "recompile", "recompiled", "recompiling", "recompilation", "compile", "compiled", "compiling", "compiler",
    "link", "linked", "linking", "linker", "build", "built", "building", "shard", "shards", "aot", "jit",
    "cmake", "ninja", "clang", "llvm", "gcc", "binary", "binaries", "executable", "elf", "iso", "irx", "iop",
    "ee", "vu1", "vu0", "gs", "hash", "sha256", "sha-256", "checksum", "manifest", "bundle", "toolchain", "sdk",
    "sysroot", "pch", "object", "objects", "archive", "artifact", "artefact", "pipeline", "stage", "lane",
    # the alternative names of the step
    "install", "installing", "installation", "setup", "setting up", "configuring", "configure",
    # developer process words
    "cache", "cached", "stamp", "json", "ini", "cli", "api", "url", "http", "curl", "tar", "xz", "zip",
    "repository", "repo", "commit", "tag", "release", "ci", "nightly", "debug", "verbose", "log",
}
# Project codenames / internal names that must never reach a player.
CODENAMES = {"nativeee", "ps2r", "twinsanityevolved", "sly2recomp", "d-a", "d-b", "hle", "cairo museum", "paris"}
# Words that look like the denylist but are plain English in these phrases.
ALLOW_PHRASES = {"the details below", "developer tools", "xcode-select --install", "open terminal and run"}

STRING_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')
WORD_RE = re.compile(r"[a-z][a-z0-9+#-]*")


def literals(path):
    text = open(path, encoding="utf-8").read()
    if path.endswith(".json"):
        def walk(v):
            if isinstance(v, str):
                yield v
            elif isinstance(v, dict):
                for x in v.values():
                    yield from walk(x)
            elif isinstance(v, list):
                for x in v:
                    yield from walk(x)
        yield from walk(json.loads(text))
        return
    for m in STRING_RE.finditer(text):
        lit = m.group(1)
        # skip include paths, format specs and identifiers (no space, or a path)
        if "/" in lit or "." in lit.split(" ")[0] and " " not in lit:
            continue
        if not re.search(r"[A-Za-z]{3,}", lit):
            continue
        yield lit


def findings_for(lit):
    low = lit.lower()
    scrubbed = low
    for ph in ALLOW_PHRASES:
        scrubbed = scrubbed.replace(ph, " ")
    out = []
    for cn in CODENAMES:
        if cn in scrubbed:
            out.append(("codename", cn))
    for w in WORD_RE.findall(scrubbed):
        if w in DENYLIST:
            out.append(("developer vocabulary", w))
    for ph in ("setting up",):
        if ph in scrubbed:
            out.append(("developer vocabulary", ph))
    return out


def main(argv):
    if not argv:
        sys.stderr.write(__doc__)
        return 2
    bad = 0
    total = 0
    for path in argv:
        for lit in literals(path):
            total += 1
            for kind, word in findings_for(lit):
                bad += 1
                print("%s: %s %r in %r" % (path, kind, word, lit))
    print("lint_ui_strings: %d literal(s) in %d file(s), %d finding(s)" % (total, len(argv), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
