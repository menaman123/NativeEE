#!/usr/bin/env python3
"""lint_one_class.py -- ARCHITECTURE.md section 13 (D4) lint, ctest target LintOneClass.

Pure python, regex-AST hybrid (no libclang dependency). Walks the hand-written C++
under the scanned roots and fails on:

  1. more than ONE primary type per .h/.cpp pair  (a primary type = a top-level
     class/struct/union that declares at least one member function, or a top-level
     namespace holding >= 2 unrelated free-function groups is approximated by the
     file-name rule below);
  2. any `#include "...inc"` of hand-written code (textual includes are banned;
     `.inl` template siblings are the ONLY sanctioned textual include);
  3. a non-trivial function DEFINITION in a header (bodies longer than the trivial
     accessor allowance) outside `.inl`;
  4. (--tags) session tags in comments, e.g. [R178], [S107-FOGLANE], [P0-EETOK],
     and session-prefixed namespaces/identifiers (r120gl::, s8_, w13r7_);
  5. file name must be the snake_case of its single primary type (psm_layout.h holds
     PsmLayout). Files with zero primary types (free-function groups, PODs) are fine.

Exemptions (ARCH section 13, explicit list -- never implicit):
  generated trees (games/<title>/{recomp,gen,iop/stream_irx}/*recompiled*, build_id),
  ee/cpu/cpu_state.h register PODs, vendored third_party/, and .inl siblings.

Exit 0 = clean, 1 = findings, 2 = usage error. `--strict-headers` promotes the
header-definition check from allowance-based to zero-tolerance (off by default).
"""
import argparse
import os
import re
import sys

ROOTS_DEFAULT = ["engine", "games", "app", "tests"]
CODE_EXT = (".h", ".hpp", ".cpp", ".cc", ".inl")

EXEMPT_PATH_PATTERNS = [
    r"/third_party/",            # vendored libraries (elfio, rabbitizer: byte-identical copies of OLD's,
                                 # third_party/*/PROVENANCE.md); not ours to lint
    r"/tools/recompiler_v2/",    # the transplanted EE recompiler (P8.8-T, DP-23): byte-identical copies of
                                 # OLD's recompiler_tool_v2 sources (tools/recompiler_v2/PROVENANCE.md);
                                 # a tool, not NEW engine code -- fixes land there with a provenance row
    r"/tools/iop_recompiler/",   # the transplanted IOP recompiler (tools/iop_recompiler/PROVENANCE.md): same rule
    r"/recomp/",                 # generated recompiled EE output (lint-exempt)
    r"/gen/",                    # generated bindings
    r"/iop/stream_irx/iop_recompiled\.(h|cpp)$",
    r"/build_id\.(h|cpp)$",
    r"/ee/cpu/cpu_state\.h$",    # frozen register PODs
    r"/ee/recomp_abi/host_app/",  # generated-code ABI: declaration-only forwarders under the OLD
                                 # header names the frozen recompiler emits (PHASE3_PLAN 2.9); the
                                 # ONE exemption class for adapters, retired with the in-tree recompiler
    r"/iop/stream_irx/abi/host_app/",   # the IOP generated-code ABI: the same forwarder class under the
                                        # OLD header names the frozen IOP recompiler emits (PHASE4_PLAN 2.10)
    r"/ee/cpu/seams\.cpp$",      # the seam providers: free-function bodies of the generated code's
                                 # roster (PHASE3_PLAN 3.4), not a class
    r"/tests/oracle/og_",        # og_*: drivers that #include OLD headers read-only (the OLD code is not
                                 # ours to lint); every other tests/oracle file is linted normally
]

# top-level record declaration: class|struct|union Name [final] [: bases] {
RECORD_RE = re.compile(
    r"^(?P<indent>[ \t]*)(template\s*<[^{;]*>\s*)?(?P<kind>class|struct|union)\s+"
    r"(?:\[\[[^\]]*\]\]\s*)?(?P<name>[A-Za-z_]\w*)\s*(final\s*)?(?::[^{;]*)?\{",
    re.M)
# member function declaration inside a record body: `ret name(args)` followed by ; or {
MEMBER_FN_RE = re.compile(
    r"^[ \t]*(?!return\b|else\b|if\b|for\b|while\b|switch\b|case\b|typedef\b|using\b|"
    r"friend\b|template\b|#)[\w:<>,\s\*&~\[\]]*?\b(?P<name>~?[A-Za-z_]\w*)\s*\([^;{}]*\)\s*"
    r"(const\s*)?(noexcept\s*)?(override\s*)?(final\s*)?(=\s*(0|default|delete)\s*)?[;{]",
    re.M)
# [ \t]* not \s*: with re.M a leading \s* would swallow preceding blank lines and shift
# the reported line number to the first of them.
INC_INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"[^"]*\.inc"', re.M)
TAG_RE = re.compile(r"\[(?:[A-Z][A-Z0-9]*(?:-[A-Z0-9]+)+|[A-Z]{1,3}\d+[A-Z0-9-]*|GD\d+|R\d+|S\d+|W\d+|P\d+)\]")
SESSION_PREFIX = r"(?:r\d{2,3}[a-z]*|s\d{1,3}|w\d{1,2}r?\d*)"
SESSION_IDENT_RE = re.compile(r"\b" + SESSION_PREFIX + r"_[a-z]\w*"          # r120gl_foo, s8_ctx
                              r"|\b" + SESSION_PREFIX + r"::"                 # r120gl::
                              r"|\bnamespace\s+" + SESSION_PREFIX + r"\b")    # namespace r120gl {


def strip_comments_and_strings(src):
    """Return (code_only, comments_only) with the same length/line structure
    preserved for line numbering."""
    code = []
    comments = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        two = src[i:i + 2]
        if two == "//":
            j = src.find("\n", i)
            j = n if j < 0 else j
            comments.append(src[i:j]); code.append(" " * (j - i)); i = j
        elif two == "/*":
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            seg = src[i:j]
            comments.append(seg)
            code.append(re.sub(r"[^\n]", " ", seg)); i = j
        elif c == '"' or c == "'":
            q = c; j = i + 1
            while j < n and src[j] != q:
                j += 2 if src[j] == "\\" else 1
            j = min(n, j + 1)
            code.append(q + " " * max(0, j - i - 2) + q); comments.append(""); i = j
        else:
            code.append(c); comments.append(" " if c != "\n" else "\n"); i += 1
    return "".join(code), "".join(comments)


def brace_span(code, open_idx):
    """Index just past the matching '}' for the '{' at open_idx."""
    depth = 0
    for k in range(open_idx, len(code)):
        ch = code[k]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return k + 1
    return len(code)


def top_level_records(code):
    """Yield (name, kind, body, line) for records not nested inside another record
    (namespaces are transparent)."""
    out = []
    spans = []  # (start, end) of record bodies already seen
    for m in RECORD_RE.finditer(code):
        start = m.start()
        if any(s <= start < e for s, e in spans):
            continue  # nested type
        # skip forward declarations / enum class
        open_idx = m.end() - 1
        end = brace_span(code, open_idx)
        # a `struct X {...} var;` typedef-style anonymous use is still a record
        # only count if this looks like a definition (has a body)
        spans.append((open_idx, end))
        body = code[open_idx + 1:end - 1]
        line = code.count("\n", 0, start) + 1
        out.append((m.group("name"), m.group("kind"), body, line))
    return out


def has_member_function(body):
    # remove nested record bodies so their methods do not count for the parent
    flat = []
    depth = 0
    for ch in body:
        if ch == "{":
            depth += 1
            if depth == 1:
                flat.append("{")
            continue
        if ch == "}":
            depth -= 1
            if depth == 0:
                flat.append("}")
            continue
        if depth == 0:
            flat.append(ch)
    text = "".join(flat)
    for m in MEMBER_FN_RE.finditer(text):
        name = m.group("name")
        if name in ("operator",):
            return True
        if name and not name.startswith(("if", "for", "while", "switch", "return")):
            return True
    return False


def snake_case(name):
    s1 = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    s2 = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s1)
    return s2.lower()


def header_definitions(code, max_body_lines):
    """Non-trivial function definitions in a header: a body with more than
    `max_body_lines` lines or more than one statement. Returns list of (line, snippet)."""
    findings = []
    # function-like: name(args) [const] {   -- at any depth (methods + free functions)
    for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\([^;{}()]*\)\s*(const\s*)?(noexcept\s*)?(override\s*)?"
                         r"(->\s*[\w:<>]+\s*)?\{", code):
        name = m.group(1)
        if name in ("if", "for", "while", "switch", "catch", "return", "sizeof",
                    "alignof", "decltype", "static_assert", "namespace", "operator"):
            continue
        open_idx = m.end() - 1
        end = brace_span(code, open_idx)
        body = code[open_idx + 1:end - 1]
        stmts = body.count(";")
        lines = body.strip().count("\n") + (1 if body.strip() else 0)
        if lines > max_body_lines or stmts > 1:
            findings.append((code.count("\n", 0, m.start()) + 1,
                             name + "(...) { ... }  [%d lines, %d statements]" % (lines, stmts)))
    return findings


def lint_file(path, rel, args):
    findings = []
    with open(path, "r", errors="replace") as f:
        src = f.read()
    code, comments = strip_comments_and_strings(src)
    # includes must be matched with string literals intact (comments removed only)
    code_with_strings = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), src, flags=re.S)
    code_with_strings = re.sub(r"//[^\n]*", "", code_with_strings)

    for m in INC_INCLUDE_RE.finditer(code_with_strings):
        # line number must be counted in the SAME buffer the match came from
        findings.append((rel, code_with_strings.count("\n", 0, m.start()) + 1,
                         "textual .inc include of hand-written code is banned (ARCH 13)"))

    primaries = [(n, k, l) for (n, k, body, l) in top_level_records(code) if has_member_function(body)]
    if len(primaries) > 1:
        findings.append((rel, primaries[1][2],
                         "more than one primary type in this file: " +
                         ", ".join("%s %s (line %d)" % (k, n, l) for n, k, l in primaries)))
    elif len(primaries) == 1 and args.filename_rule:
        base = os.path.splitext(os.path.basename(rel))[0]
        want = snake_case(primaries[0][0])
        if base != want and not base.startswith(want):
            findings.append((rel, primaries[0][2],
                             "file name '%s' should be '%s' for primary type %s (snake_case = PascalCase)"
                             % (base, want, primaries[0][0])))

    if rel.endswith((".h", ".hpp")):
        for line, snip in header_definitions(code, 0 if args.strict_headers else args.max_inline_lines):
            findings.append((rel, line, "non-trivial function definition in a header: " + snip +
                             " (move to .cpp or a template .inl)"))

    if args.tags:
        for m in TAG_RE.finditer(comments):
            findings.append((rel, comments.count("\n", 0, m.start()) + 1,
                             "session tag in comment: " + m.group(0)))
        for m in SESSION_IDENT_RE.finditer(code):
            findings.append((rel, code.count("\n", 0, m.start()) + 1,
                             "session-prefixed identifier: " + m.group(0)))
    return findings


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=".", help="repository root")
    ap.add_argument("--scan", nargs="*", default=ROOTS_DEFAULT, help="sub-trees to scan")
    ap.add_argument("--tags", action="store_true", default=True, help="also reject session tags (default on)")
    ap.add_argument("--no-tags", dest="tags", action="store_false")
    ap.add_argument("--filename-rule", action="store_true", default=True)
    ap.add_argument("--no-filename-rule", dest="filename_rule", action="store_false")
    ap.add_argument("--strict-headers", action="store_true")
    ap.add_argument("--max-inline-lines", type=int, default=3,
                    help="trivial-accessor allowance for definitions in headers")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()

    root = os.path.abspath(a.root)
    files = 0
    findings = []
    for sub in a.scan:
        base = os.path.join(root, sub)
        if not os.path.isdir(base):
            continue
        for dp, dn, fn in os.walk(base):
            dn[:] = [x for x in dn if x not in ("build", ".git", "__pycache__")]
            for name in fn:
                if not name.endswith(CODE_EXT):
                    continue
                path = os.path.join(dp, name)
                rel = os.path.relpath(path, root).replace(os.sep, "/")   # the allowlists are written with /
                if any(re.search(p, "/" + rel) for p in EXEMPT_PATH_PATTERNS):
                    continue
                if name.endswith(".inl"):
                    continue  # sanctioned template siblings
                files += 1
                findings.extend(lint_file(path, rel, a))
    for rel, line, msg in findings:
        print("%s:%d: %s" % (rel, line, msg))
    print("LintOneClass: %d files scanned, %d findings -> %s" % (files, len(findings), "FAIL" if findings else "PASS"))
    return 1 if findings else 0


def selftest():
    import tempfile
    ok = True
    d = tempfile.mkdtemp(prefix="lint_one_class.")
    os.makedirs(os.path.join(d, "engine", "x"))
    good = "// ok\nclass PsmLayout {\n public:\n  int bpp() const { return bpp_; }\n private:\n  int bpp_;\n};\n"
    bad2 = "class A { void f(); };\nclass B { void g(); };\n"
    badinc = '#include "foo.inc"\n'
    badtag = "// [R178] fixed blend\nstruct Q { int x; };\n"
    baddef = "struct Big { void f(); };\ninline void Big::f() {\n int a = 1;\n int b = 2;\n a += b;\n}\n"
    badns = "namespace r120gl {\nint x;\n}\n"
    badinc2 = "// comment line\n// another\n#include \"foo.inc\"\n"   # finding must be on line 3
    cases = [("psm_layout.h", good, 0), ("two.h", bad2, 1), ("inc.cpp", badinc, 1),
             ("tag.h", badtag, 1), ("big.h", baddef, 1), ("wrong_name.h", good, 1),
             ("ns.cpp", badns, 1)]
    class A: pass
    a = A(); a.tags = True; a.filename_rule = True; a.strict_headers = False; a.max_inline_lines = 3
    for name, text, expect in cases:
        p = os.path.join(d, "engine", "x", name)
        with open(p, "w") as f:
            f.write(text)
        got = len(lint_file(p, "engine/x/" + name, a))
        res = (got > 0) == (expect > 0)
        ok &= res
        print("  selftest %-14s expect %s got %d %s" % (name, "clean" if expect == 0 else "finding", got, "OK" if res else "MISMATCH"))
    # .inc finding line number: reported in the buffer the match came from
    p = os.path.join(d, "engine", "x", "inc_line.cpp")
    with open(p, "w") as f:
        f.write(badinc2)
    f2 = lint_file(p, "engine/x/inc_line.cpp", a)
    line_ok = len(f2) == 1 and f2[0][1] == 3
    ok &= line_ok
    print("  selftest %-14s expect line 3 got %s %s" % ("inc_line.cpp", [x[1] for x in f2], "OK" if line_ok else "MISMATCH"))
    print("LintOneClass selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
