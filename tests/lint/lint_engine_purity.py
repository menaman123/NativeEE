#!/usr/bin/env python3
"""lint_engine_purity.py -- ARCHITECTURE.md section 3 (D5) / section 13 lint,
ctest target LintEnginePurity.

Greps the title-neutral trees (engine/ and verify/ by default -- EQUIVALENCE REV2 (b)
makes the harness engine-grade) for the A6 section 1.2 game-token regex set, on
COMMENT-STRIPPED code only (comments may narrate the game; code may not know it):

  1. EE load-range address literals   0x00100000 .. 0x003DAE80   (the title ELF's single
                                      PT_LOAD: p_vaddr 0x00100000, p_memsz 0x2DAE80 -> memsz end
                                      0x003DAE80; PHASE3_PLAN 7.1 P3-ELFRANGE. Covers .text,
                                      .vutext, .data, .vudata, .rodata, .sdata AND .bss, so
                                      game DATA addresses are caught, not only code)
  2. game function names              FUN_[0-9a-fA-F]{6,8}  /  func_00[0-9a-fA-F]{6}
  3. the game's VRAM base constants   (A6 section 3.3 list -- the seed denylist)
  4. game strings                     SLUS, 20909, the title, DATASYS, STREAM.IRX,
                                      lti_, <elf-crc>, BASLUS, DATA.BH/DATA.BD
  5. VU1-HLE program stamps           vs_family_*, vs_crys_*, vs_*_dual, the family
                                      dispatcher constants 0x090E0003 / 0x1720 variant base

Tolerance: 0. An allowlist file (tests/lint/purity_allowlist.txt, one
`<relpath>:<literal>` per line, each with a justification comment above it) exists
for the generic SIZE constants that collide with the widened range (1 MiB 0x100000,
2 MiB 0x200000, the 3 MiB / 0x300000 family): every entry is a reviewed size, never
an address.

Exit 0 = clean, 1 = findings, 2 = usage.
"""
import argparse
import os
import re
import sys

ROOTS_DEFAULT = ["engine", "verify"]
# Not scanned by design: games/ (title data), tools/recompiler_v2/ and third_party/ (P8.8-T, DP-23:
# byte-identical copies of OLD's recompiler_tool_v2 + elfio/rabbitizer, see their PROVENANCE.md --
# the generator is title-aware by nature and is not engine code).
CODE_EXT = (".h", ".hpp", ".cpp", ".cc", ".inl", ".py", ".cmake", ".txt", ".toml", ".ini", ".sh")
SKIP_NAMES = {"README.md"}

HEX_RE = re.compile(r"\b0[xX]([0-9a-fA-F]{5,8})\b")
# GENERIC patterns -- true of any PS2 title, so these stay in the lint: the serial shapes a
# disc carries, and the decompiled-function naming both recompilers emit.
FUN_RE = re.compile(r"\b(?:FUN_[0-9a-fA-F]{6,8}|func_00[0-9a-fA-F]{6})\b")
STRING_RE = re.compile(
    r"\b(?:SLUS|SLES|SCUS|SCES|SLPM|SLPS|SCPS|BASLUS|BASLES)[_ ]?[0-9]{3}[._ ]?[0-9]{2}\b", re.I)


# TITLE-SPECIFIC denylist. The engine must name NO game -- but WHICH addresses, strings and
# program stamps belong to a game is a property of that game, not of this lint. Hardcoding one
# title's tokens here is precisely what made this file name a game. Each title supplies its own:
#
#   games/<title>/lint_denylist.txt    "#" comments, one directive per line
#     text_lo = 0x00100000     the title ELF's PT_LOAD p_vaddr
#     text_hi = 0x003DAE80     its memsz end (exclusive) -- a literal in [lo, hi) is an address
#     0x230000                 a bare hex literal: a VRAM / data base constant
#     the title               anything else: a regex, matched case-insensitively
#
# Without a title, or without the file, only the generic patterns above apply and the
# address-range check is skipped -- there is no generic "game address" range to test against.
def load_title_denylist(root):
    """-> (vram_re, stamp_re, text_lo, text_hi); each None when not supplied."""
    title = os.environ.get("PS2R_TITLE")
    games = os.path.join(root, "games")
    if not title and os.path.isdir(games):
        found = [d for d in sorted(os.listdir(games))
                 if os.path.isdir(os.path.join(games, d, "hle"))]
        title = found[0] if len(found) == 1 else None
    if not title:
        return None, None, None, None
    path = os.path.join(games, title, "lint_denylist.txt")
    if not os.path.exists(path):
        return None, None, None, None
    return parse_denylist(open(path, encoding="utf-8").read())


def parse_denylist(text):
    addrs, pats, lo, hi = [], [], None, None
    for line in text.split("\n"):
        tok = line.split("#", 1)[0].strip()
        if not tok:
            continue
        m = re.fullmatch(r"(text_lo|text_hi)\s*=\s*(0[xX][0-9a-fA-F]+)", tok)
        if m:
            if m.group(1) == "text_lo":
                lo = int(m.group(2), 16)
            else:
                hi = int(m.group(2), 16)
        elif re.fullmatch(r"0[xX][0-9a-fA-F]+", tok):
            addrs.append(int(tok, 16))
        else:
            pats.append(tok)
    vram = (re.compile(r"\b0[xX]0*(" + "|".join("%X" % v for v in addrs) + r")\b", re.I)
            if addrs else None)
    stamp = re.compile("|".join(pats), re.I) if pats else None
    return vram, stamp, lo, hi


# Populated in main() (and in selftest()) from the configured title.
VRAM_RE, STAMP_RE, EE_TEXT_LO, EE_TEXT_HI = None, None, None, None


def strip_comments(src, ext):
    if ext in (".py", ".cmake", ".txt", ".toml", ".ini", ".sh"):
        # '#' line comments; keep strings (paths are code for scripts)
        out = []
        for line in src.split("\n"):
            i = line.find("#")
            # crude: ignore '#' inside quotes
            if i >= 0:
                q1, q2 = line.count("'", 0, i), line.count('"', 0, i)
                if q1 % 2 == 0 and q2 % 2 == 0:
                    line = line[:i]
            out.append(line)
        return "\n".join(out)
    # C/C++: remove // and /* */ comments, keep strings
    src = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    return src


def load_allowlist(path):
    allow = set()
    if not os.path.isfile(path):
        return allow
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            allow.add(line)
    return allow


def scan_file(path, rel, allow):
    ext = os.path.splitext(path)[1]
    with open(path, "r", errors="replace") as f:
        src = f.read()
    code = strip_comments(src, ext)
    findings = []

    def add(m, what):
        lit = m.group(0)
        if "%s:%s" % (rel, lit) in allow:
            return
        findings.append((rel, code.count("\n", 0, m.start()) + 1, what, lit))

    for m in HEX_RE.finditer(code):
        v = int(m.group(1), 16)
        if EE_TEXT_LO is not None and EE_TEXT_LO <= v < EE_TEXT_HI:  # memsz end exclusive
            add(m, "EE load-range address literal")
    for m in FUN_RE.finditer(code):
        add(m, "game function name")
    for m in (VRAM_RE.finditer(code) if VRAM_RE else ()):
        add(m, "game VRAM/RAM base constant")
    for m in STRING_RE.finditer(code):
        add(m, "game string")
    for m in (STAMP_RE.finditer(code) if STAMP_RE else ()):
        add(m, "VU1 program stamp / family constant")
    return findings


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=".")
    ap.add_argument("--scan", nargs="*", default=ROOTS_DEFAULT)
    ap.add_argument("--allowlist", default="tests/lint/purity_allowlist.txt")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    root = os.path.abspath(a.root)
    allow = load_allowlist(os.path.join(root, a.allowlist))
    # the configured title's own denylist, if it ships one
    global VRAM_RE, STAMP_RE, EE_TEXT_LO, EE_TEXT_HI
    VRAM_RE, STAMP_RE, EE_TEXT_LO, EE_TEXT_HI = load_title_denylist(root)
    files, findings = 0, []
    for sub in a.scan:
        base = os.path.join(root, sub)
        if not os.path.isdir(base):
            continue
        for dp, dn, fn in os.walk(base):
            dn[:] = [x for x in dn if x not in ("build", ".git", "__pycache__")]
            # the frozen hash baselines are title data under games/<title>/verify/baseline (not scanned)
            for name in fn:
                if name in SKIP_NAMES or not name.endswith(CODE_EXT):
                    continue
                path = os.path.join(dp, name)
                rel = os.path.relpath(path, root)
                files += 1
                findings.extend(scan_file(path, rel, allow))
    for rel, line, what, lit in findings:
        print("%s:%d: %s: %s" % (rel, line, what, lit))
    print("LintEnginePurity: %d files scanned, %d game tokens -> %s" % (files, len(findings), "FAIL" if findings else "PASS"))
    return 1 if findings else 0


def selftest():
    import tempfile
    d = tempfile.mkdtemp(prefix="lint_purity.")
    # a synthetic title denylist: the cases below exercise the per-title mechanism
    global VRAM_RE, STAMP_RE, EE_TEXT_LO, EE_TEXT_HI
    VRAM_RE, STAMP_RE, EE_TEXT_LO, EE_TEXT_HI = parse_denylist(r"""
text_lo = 0x00100000
text_hi = 0x003DAE80
0x230000
vs_family_\w*
""")
    cases = [
        ("clean.cpp", "uint32_t bp = tex0.tbp0; // FUN_00191bd0 in a comment is fine\nconst int kPage = 0x2000;\n", 0),
        ("addr.cpp", "if (pc == 0x0019aff0) return;\n", 1),
        ("fun.cpp", "void func_00191bd0(CpuContext&);\n", 1),
        ("vram.cpp", "if (fbp == 0x230000) latch = true;\n", 1),
        ("str.cpp", "const char* p = \"cdrom0:\\\\SLUS_123.45;1\";\n", 1),
        ("stamp.cpp", "if (strstr(name, \"vs_family_tristrip\")) cls = Strip;\n", 1),
        ("size.cpp", "static const size_t kMeg = 0x100000; /* 1 MiB */\n", 1),  # collides by design -> allowlist
        ("data.cpp", "if (addr == 0x002e804c) patched = true;\n", 1),  # .data address (inside memsz, past the old 0x0020FFFF)
        ("bss.cpp", "const uint32_t kSlot = 0x003dae7c;\n", 1),  # last .bss word (memsz end - 4)
        ("past.cpp", "const uint32_t kBase = 0x003dae80;\n", 0),  # memsz end itself is outside the range
        ("script.py", "REPO = '/home/x/NativeEE'  # SLUS in a comment only\n", 0),
    ]
    ok = True
    for name, text, expect in cases:
        p = os.path.join(d, name)
        with open(p, "w") as f:
            f.write(text)
        got = len(scan_file(p, name, set()))
        res = (got > 0) == (expect > 0)
        ok &= res
        print("  selftest %-10s expect %s got %d %s" % (name, "clean" if expect == 0 else "finding", got, "OK" if res else "MISMATCH"))
    # allowlist removes the size collision
    got = len(scan_file(os.path.join(d, "size.cpp"), "size.cpp", {"size.cpp:0x100000"}))
    ok &= (got == 0)
    print("  selftest allowlist   expect clean got %d %s" % (got, "OK" if got == 0 else "MISMATCH"))
    print("LintEnginePurity selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
