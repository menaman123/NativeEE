#!/usr/bin/env python3
"""launcher_lint.py -- the launcher's wording lint, ctest target LauncherLint.

The launcher is title-neutral and speaks to a player, so over app/launcher it fails on:
  1. a CODENAME anywhere in the source (code, comments, strings): crash, twinsanity, sly, cooper, slus, sles,
     sces, scus, scps, scka -- a game's name belongs in its title.json, never in the engine;
  2. DEVELOPER VOCABULARY in a string literal (what a player could read): recompile, shard, AOT, cmake, ninja,
     clang, toolchain, git clone, python, compiler, linker, ELF, sha256, json -- "Preparing your game" is the only
     name of the prepare step;
  3. PLACEHOLDER text in a string literal: lorem, TODO, TBD, placeholder, TextLabel;
  4. an EMOJI in a string literal (anything outside the BMP, or the misc-symbols / dingbats block).
With --strings FILE (the visible strings the screens test dumped, one per line) rules 2-4 run over those too.
Lines ending in a `// lint: allow` comment are exempt from rules 2-4 (a key or a path that merely looks like a word).
Exit 0 = clean, 1 = findings, 2 = usage error. --selftest runs the built-in cases.
"""
import argparse
import os
import re
import sys

CODENAMES = re.compile(r"\b(crash\w*|twinsanity|sly|cooper|slus|sles|sces|scus|scps|scka)\b", re.IGNORECASE)
DEV_VOCAB = re.compile(r"\b(recompil\w*|shards?|AOT|cmake|ninja|clang|toolchain|git\s+clone|python|compilers?|linkers?|ELF|sha-?256|json)\b")
DEV_VOCAB_CI = re.compile(r"\b(recompil\w*|shards?|cmake|ninja|clang|toolchain|git\s+clone|python|compilers?|linkers?|sha-?256|json)\b", re.IGNORECASE)
PLACEHOLDER = re.compile(r"(lorem|\bTODO\b|\bTBD\b|placeholder|TextLabel)", re.IGNORECASE)
STRING_LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
CODE_EXT = (".h", ".cpp", ".txt", ".in")


def has_emoji(text):
    for ch in text:
        o = ord(ch)
        if o > 0xFFFF or 0x2600 <= o <= 0x27BF or 0x1F000 <= o:
            return True
    return False


def dev_vocab_hits(text):
    hits = []
    for m in DEV_VOCAB_CI.finditer(text):
        hits.append(m.group(0))
    for m in re.finditer(r"\b(AOT|ELF)\b", text):   # the acronyms only in capitals: "elf" is a word, "aot" a typo
        hits.append(m.group(0))
    return hits


def lint_string(text, where, findings):
    for h in dev_vocab_hits(text):
        findings.append("%s: developer vocabulary %r in %r" % (where, h, text))
    m = PLACEHOLDER.search(text)
    if m:
        findings.append("%s: placeholder text %r in %r" % (where, m.group(0), text))
    if has_emoji(text):
        findings.append("%s: emoji in %r" % (where, text))


def lint_source(path, rel, findings):
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.read().split("\n")
    for i, line in enumerate(lines, 1):
        where = "%s:%d" % (rel, i)
        for m in CODENAMES.finditer(line):
            findings.append("%s: codename %r" % (where, m.group(0)))
        if line.rstrip().endswith("// lint: allow"):
            continue
        for m in STRING_LITERAL.finditer(line):
            text = m.group(1)
            if not text.strip():
                continue
            lint_string(text, where, findings)


def lint_strings_file(path, findings):
    with open(path, encoding="utf-8", errors="replace") as f:
        for i, line in enumerate(f, 1):
            text = line.rstrip("\n")
            if not text.strip():
                continue
            where = "%s:%d" % (os.path.basename(path), i)
            for m in CODENAMES.finditer(text):
                findings.append("%s: codename %r in %r" % (where, m.group(0), text))
            lint_string(text, where, findings)


def selftest():
    f = []
    lint_string("Preparing your game", "t", f)
    assert not f, f
    lint_string("Recompiling the ELF with cmake", "t", f)
    assert len(f) == 3, f
    f = []
    lint_string("The shelf is self-made; the wolf howled", "t", f)   # elf inside words is fine
    assert not f, f
    lint_string("TODO fix", "t", f)
    assert len(f) == 1, f
    f = []
    lint_string("Done \U0001F389", "t", f)
    assert len(f) == 1, f
    assert CODENAMES.search("previously") is None
    assert CODENAMES.search("the Sly disc") is not None
    assert CODENAMES.search("it crashed") is not None
    print("selftest ok")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument("--scan", nargs="*", default=["app/launcher"])
    ap.add_argument("--strings", default=None, help="a file of visible UI strings, one per line (optional; missing = skipped)")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    findings = []
    for scan in args.scan:
        root = os.path.join(args.root, scan)
        if not os.path.isdir(root):
            print("launcher_lint: no such directory %s" % root, file=sys.stderr)
            return 2
        for dirpath, _, files in os.walk(root):
            for name in sorted(files):
                if name.endswith(CODE_EXT):
                    p = os.path.join(dirpath, name)
                    lint_source(p, os.path.relpath(p, args.root), findings)
    if args.strings and os.path.isfile(args.strings):
        lint_strings_file(args.strings, findings)
    for x in findings:
        print(x)
    print("launcher_lint: %d finding(s)" % len(findings))
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
