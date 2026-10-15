#!/usr/bin/env python3
"""lint_session_tags.py -- ARCHITECTURE.md section 11 "Session-tag comments / namespaces: 0 in the new
tree", ctest LintSessionTags.

Scans the code of engine/ app/ games/ tools/ verify/ tests/ (.cpp .cc .h .hpp .inc .inl .py .sh .cmake
CMakeLists.txt) -- the whole hand-written tree, where LintOneClass covers only engine/games/app/tests
and only .h/.cpp -- for the two session-tag shapes of the section 11 rule:

  1. `[TAG]` prose in COMMENTS:  [R178], [S107-FOGLANE], [P0-EETOK], [SESSION-14 FIX], [W13R7] ...
     (the TAG_RE of lint_one_class.py: bracketed upper-case tokens with a digit or a dash; a quoted
     OLD line such as `OLD main.cpp:3586 [SESSION-14 FIX]` is history -- it belongs in docs/, not code);
  2. session-prefixed NAMESPACES / IDENTIFIERS in code: r120gl::, r118::, s9_composite, w13r7_x,
     `namespace zk {` (the SESSION_IDENT_RE of lint_one_class.py plus the `zk` family).

String literals are scanned separately (3): a `[TAG]` inside a literal is reported with kind
`session-tag:<literal>` so the frozen bench_report.json vocabulary (`[R127-BENCH]`,
`[R123-MTVUSYNC]`, `[R147-GFXRES]` in engine/core/diag -- tests/lint/README.md "Accepted
exception") is allowlisted BY LITERAL and anything new fails.

Exempt paths: vendored third_party/, tools/recompiler_v2/ (byte-identical OLD sources), generated
recomp/ and gen/, the generated IOP pair, verify/baseline (frozen data), tests/oracle/og_* (drivers
that include OLD headers). Markdown is not scanned: READMEs carry provenance by design.

Allowlist: tests/lint/budget_allowlist.txt entries `<relpath>:session-tag:<text>:<count>` where
<text> is the exact tag / identifier (count = occurrences in the file; growth fails again).

Exit 0 = clean, 1 = findings, 2 = usage.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lint_one_class import SESSION_IDENT_RE, strip_comments_and_strings  # noqa: E402
from lint_size_budget import load_allowlist  # noqa: E402

ROOTS_DEFAULT = ["engine", "app", "games", "tools", "verify", "tests"]
CXX_EXT = (".cpp", ".cc", ".h", ".hpp", ".inc", ".inl")
SCRIPT_EXT = (".py", ".sh", ".cmake")
EXEMPT_PATH_PATTERNS = [
    r"/third_party/",
    r"/tools/recompiler_v2/",
    r"/tools/iop_recompiler/",   # the transplanted IOP recompiler: byte-identical OLD sources (its PROVENANCE.md)
    r"/recomp/",
    r"/gen/",
    r"/iop/stream_irx/iop_recompiled\.(h|cpp)$",
    r"/verify/baseline/",
    r"/tests/oracle/og_",
    r"/__pycache__/",
    # ONLY the two lints whose docstrings/selftests quote the rejected shapes (measured without the exemption:
    # lint_session_tags.py 25 hits, lint_one_class.py 3; every other tests/lint/*.py is scanned -- review loop 1)
    r"/tests/lint/lint_session_tags\.py$",
    r"/tests/lint/lint_one_class\.py$",
]
ZK_RE = re.compile(r"\bzk::|\bnamespace\s+zk\b")
# comments: lint_one_class's TAG_RE, widened to tags with inner words (`[SESSION-14 FIX]`)
COMMENT_TAG_RE = re.compile(r"\[(?:[A-Z][A-Z0-9]*(?:-[A-Z0-9]+)+|[A-Z]{1,3}\d+[A-Z0-9-]*|GD\d+|SESSION-?\d+)(?: [A-Z0-9]+)*\]")
# literals: only SESSION-NUMBERED tags ([R127-BENCH], [W10-EEFEED], [T3], [SESSION-14 FIX]); a hyphenated
# upper-case log prefix without a session number ([GS-CLASSRDR], [VU-MODE]) is log vocabulary, not history
LITERAL_TAG_RE = re.compile(r"\[(?:[A-Z0-9-]*\b(?:R|S|W|P|T|B|GD|SESSION-?)\d+[A-Z0-9-]*)(?: [A-Z0-9]+)*\]")
STRING_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
SCRIPT_STRING_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"|\'((?:[^\'\\\n]|\\.)*)\'')


def split_script(src):
    """(code, comments) for '#'-commented scripts, same length/line structure as strip_comments_and_strings."""
    code, comments = [], []
    for line in src.split("\n"):
        i, inq = -1, None
        for k, ch in enumerate(line):
            if inq:
                if ch == inq:
                    inq = None
            elif ch in "\"'":
                inq = ch
            elif ch == "#":
                i = k
                break
        if i >= 0:
            code.append(line[:i] + " " * (len(line) - i))
            comments.append(" " * i + line[i:])
        else:
            code.append(line)
            comments.append(" " * len(line))
    return "\n".join(code), "\n".join(comments)


def scan_file(path, rel, allow, used):
    with open(path, "r", errors="replace") as f:
        src = f.read()
    ext = os.path.splitext(path)[1]
    if ext in CXX_EXT:
        code, comments = strip_comments_and_strings(src)
        # literals: scan the comment-free source
        nocomm = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), src, flags=re.S)
        nocomm = re.sub(r"//[^\n]*", "", nocomm)
        literals = [(m.start(), m.group(1)) for m in STRING_RE.finditer(nocomm)]
        lit_src = nocomm
    else:
        code, comments = split_script(src)
        literals = [(m.start(), m.group(1) or m.group(2) or "") for m in SCRIPT_STRING_RE.finditer(code)]
        lit_src = code
    raw = []
    for m in COMMENT_TAG_RE.finditer(comments):
        raw.append((comments.count("\n", 0, m.start()) + 1, "session tag in comment", m.group(0)))
    for m in SESSION_IDENT_RE.finditer(code):
        raw.append((code.count("\n", 0, m.start()) + 1, "session-prefixed identifier", m.group(0)))
    for m in ZK_RE.finditer(code):
        raw.append((code.count("\n", 0, m.start()) + 1, "session namespace", m.group(0)))
    for pos, lit in literals:
        for m in LITERAL_TAG_RE.finditer(lit):
            raw.append((lit_src.count("\n", 0, pos) + 1, "session tag in string literal", m.group(0)))
    counts = {}
    for _, _, text in raw:
        counts[text] = counts.get(text, 0) + 1
    out = []
    for line, what, text in raw:
        key = "%s:session-tag:%s" % (rel, text)
        if key in allow:
            used.add(key)
            cap, _ = allow[key]
            if counts[text] <= cap:
                continue
            what += " -- grew past its allowlisted count %d" % cap
        out.append((rel, line, what, text))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=".")
    ap.add_argument("--scan", nargs="*", default=ROOTS_DEFAULT)
    ap.add_argument("--allowlist", default="tests/lint/budget_allowlist.txt")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    root = os.path.abspath(a.root)
    allow = {k: v for k, v in load_allowlist(os.path.join(root, a.allowlist)).items() if ":session-tag:" in k}
    used = set()
    files, findings = 0, []
    for sub in a.scan:
        base = os.path.join(root, sub)
        if not os.path.isdir(base):
            continue
        for dp, dn, fn in os.walk(base):
            dn[:] = sorted(x for x in dn if x not in (".git", "__pycache__") and not x.startswith("build"))
            for name in sorted(fn):
                if not (name.endswith(CXX_EXT) or name.endswith(SCRIPT_EXT) or name == "CMakeLists.txt"):
                    continue
                path = os.path.join(dp, name)
                rel = os.path.relpath(path, root)
                if any(re.search(p, "/" + rel) for p in EXEMPT_PATH_PATTERNS):
                    continue
                files += 1
                findings.extend(scan_file(path, rel, allow, used))
    for rel, line, what, text in findings:
        print("%s:%d: %s: %s" % (rel, line, what, text))
    for key in sorted(set(allow) - used):
        print("%s: WARN stale allowlist entry -- nothing to silence; remove it" % key)
    print("LintSessionTags: %d files scanned, %d findings -> %s" % (files, len(findings), "FAIL" if findings else "PASS"))
    return 1 if findings else 0


def selftest():
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)

    d = tempfile.mkdtemp(prefix="lint_tags.")
    cases = [
        ("clean.cpp", "// the GS clamps Q to the PS2 float range (GS_SKILL)\nint q = 0; const char* s = \"[ok]\";\n", 0),
        ("tag.cpp", "// [R178] fixed the blend\nint q;\n", 1),
        ("old.cpp", "// OLD main.cpp:3586 [SESSION-14 FIX] kept\nint q;\n", 1),
        ("ns.cpp", "namespace r120gl {\nint x;\n}\nint y = r118::v;\n", 2),
        ("zk.cpp", "namespace zk {\nint x;\n}\n", 1),
        ("lit.cpp", "const char* k = \"[R127-BENCH] frames\";\n", 1),
        ("logpfx.cpp", "const char* k = \"[GS-CLASSRDR] class=%u\"; const char* j = \"[VU-MODE] cached\";\n", 0),
        ("sess.cpp", "const char* k = \"[SESSION-14 FIX] kept\";\n", 1),
        ("tag.py", "# [P0-EETOK] quick fix\nx = 1\n", 1),
        ("lit.py", "line = '[R123-MTVUSYNC] site'  # parses the frozen vocabulary\n", 1),
        ("clean.py", "x = '#[R1]'  # a '#' inside quotes is not a comment start\n", 1),
        ("CMakeLists.txt", "# [W13R7] hack\nadd_test(NAME X COMMAND y)\n", 1),
    ]
    for name, text, expect in cases:
        p = os.path.join(d, name)
        with open(p, "w") as f:
            f.write(text)
        got = scan_file(p, name, {}, set())
        check(len(got) == expect, "%-14s expect %d got %d %s" % (name, expect, len(got), [g[3] for g in got]))
    p = os.path.join(d, "lit.cpp")
    check(scan_file(p, "lit.cpp", {"lit.cpp:session-tag:[R127-BENCH]": (1, 1)}, set()) == [], "allowlisted literal is silenced")
    check(len(scan_file(p, "lit.cpp", {"lit.cpp:session-tag:[R127-BENCH]": (0, 1)}, set())) == 1, "a cap below the count fails")
    print("LintSessionTags selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
