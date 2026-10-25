#!/usr/bin/env python3
"""lint_header_globals.py -- ARCHITECTURE.md section 11 "Header mutable globals: 0", ctest LintHeaderGlobals.

Scans every .h/.hpp and every textual-include sibling .inl/.inc under engine/ app/ games/ (an `inline`
mutable in an .inl included from a header is the same defect; the hand-written tree: generated recomp/gen/,
vendored third_party/, the OLD recompiler copy and the frozen register PODs are exempt as in
lint_one_class.py) and FAILS on every DEFINITION of a mutable variable at namespace scope or as a
class static data member:

  FAIL   int g_count;                      namespace-scope definition (external linkage)
  FAIL   static int s_count = 0;           namespace-scope static (one copy per TU)
  FAIL   inline std::atomic<int> g{0};     inline variable = a definition
  FAIL   struct S { static inline int n; } static inline member = a definition
  FAIL   const char* g_name;               pointer-to-const is a mutable pointer
  ok     extern int g_count;               a declaration (the definition lives in a .cpp)
  ok     struct S { static int n; };       a declaration (defined in the .cpp)
  ok     constexpr / const / static const / inline constexpr / static constexpr ...  immutable
  ok     char* const p = ...;              top-level const pointer
  ok     function-local statics, non-static data members, enumerators, using/typedef, templates'
         constexpr variable templates, function declarations.

Heuristic: comments, strings and preprocessor lines are blanked; function bodies (the brace
span after `ident(...)` + qualifiers) and enum bodies are blanked; the remaining text is cut into
`;`-terminated statements with their scope stack (namespace / extern "C" / class / struct /
union). A statement is a variable definition when it has a declarator identifier and is not a
function declaration (`(` present without `=`/`{` initialiser), a using/typedef/friend/
template-only/forward declaration, or `extern` without an initialiser. Mutability: `constexpr`,
`consteval`, `constinit`-with-const absent -> mutable; for pointer/reference declarators the
`const` that counts is the one AFTER the last `*`/`&`; otherwise any `const`. A function definition's
body ends its statement at the `}` (no `;`), so a declaration on the next line is its own statement
(review loop 1: previously `inline int F() { .. }` swallowed the following declaration). Known misses:
macro-generated definitions, declarators split across a `,` list where only the first is
inspected for the initialiser, and `decltype`-typed variables with parentheses in the type
(treated as function declarations).

Allowlist: tests/lint/budget_allowlist.txt entries `<relpath>:header-global:<name>:<count>`
(count = how many such definitions of that name the file carries; growth fails again).

Exit 0 = no unallowlisted finding, 1 = findings, 2 = usage.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lint_size_budget import strip_cxx, match_close, cxx_functions, load_allowlist  # noqa: E402

ROOTS_DEFAULT = ["engine", "app", "games"]
HEADER_EXT = (".h", ".hpp", ".inl", ".inc")   # .inl/.inc: textual includes are header-like (review loop 1)
EXEMPT_PATH_PATTERNS = [
    r"/third_party/",
    r"/tools/recompiler_v2/",
    r"/tools/iop_recompiler/",   # the transplanted IOP recompiler: byte-identical OLD sources (its PROVENANCE.md)
    r"/recomp/",
    r"/gen/",
    r"/iop/stream_irx/iop_recompiled\.(h|cpp)$",
    r"/ee/cpu/cpu_state\.h$",
    r"/ee/recomp_abi/host_app/",
    r"/iop/stream_irx/abi/host_app/",
]
SCOPE_OPEN_RE = re.compile(
    r"(?:\b(?P<ns>namespace)\s+(?:[A-Za-z_][\w:]*\s*)?(?:\[\[[^\]]*\]\]\s*)?|"
    r"\bextern\s+\"C(?:\+\+)?\"\s*|"
    r"\b(?P<rec>class|struct|union)\s+(?:\[\[[^\]]*\]\]\s*)?(?:[A-Za-z_]\w*\s*)?(?:final\s*)?(?::[^{;]*)?|"
    r"\b(?P<enum>enum)\s+(?:class\s+|struct\s+)?(?:[A-Za-z_]\w*\s*)?(?::\s*[\w:\s]+)?)\{")
SKIP_START = ("using", "typedef", "friend", "static_assert", "namespace", "template", "class", "struct",
              "union", "enum", "public", "private", "protected", "operator", "return", "concept", "requires")
IDENT_RE = re.compile(r"[A-Za-z_]\w*")


def blank_span(code, a, b):
    return code[:a] + re.sub(r"[^\n]", " ", code[a:b]) + code[b:]


def blank_preprocessor(code):
    lines = code.split("\n")
    out, cont = [], False
    for ln in lines:
        if cont or ln.lstrip().startswith("#"):
            cont = ln.rstrip().endswith("\\")
            out.append(" " * len(ln))
        else:
            out.append(ln)
    return "\n".join(out)


def blank_function_bodies(code):
    # cxx_functions reports lines; redo the span search here to blank by index
    spans = []
    for name, first, last in cxx_functions(code):
        spans.append((name, first, last))
    if not spans:
        return code
    lines = code.split("\n")
    for name, first, last in spans:
        # keep the '{' line's text before the brace and the '}' line's text after it; the body's `{` is
        # the first one AFTER `name(` (a `struct S { int f() {` line opens the record first)
        ln = lines[first - 1]
        at = re.search(r"\b%s\s*\(" % re.escape(name), ln) if name else None
        i = ln.find("{", at.end() if at else 0)
        if first == last:
            # one-line body `R f() { ... }`: blank between its braces only (both braces survive)
            j = match_close(ln, i) if i >= 0 else -1
            if 0 <= i < j:
                lines[first - 1] = ln[:i + 1] + " " * (j - i - 1) + ln[j:]
            continue
        for k in range(first - 1, last):
            if k == first - 1:
                lines[k] = lines[k][:i + 1] + " " * (len(lines[k]) - i - 1) if i >= 0 else lines[k]
            elif k == last - 1:
                i = lines[k].rfind("}")
                lines[k] = " " * i + lines[k][i:] if i >= 0 else lines[k]
            else:
                lines[k] = " " * len(lines[k])
    return "\n".join(lines)


FUNC_TAIL_RE = re.compile(r"[\s\w:<>,&*\[\]\->]*")   # what may follow the `)` of a function head before its `{`


def is_function_head(head):
    """`R f(args) [const|noexcept|override|-> T ...]` just before a `{`: a function DEFINITION whose body
    is not `;`-terminated. Not one when the head carries `=` (a lambda or brace initialiser)."""
    j = head.rfind(")")
    return j >= 0 and "=" not in head and FUNC_TAIL_RE.fullmatch(head[j + 1:]) is not None


def statements(code):
    """Yield (scope_tuple, text, line) for every `;`-terminated statement outside enum / function
    bodies. scope_tuple = the kinds of the enclosing braces ('ns'|'extern'|'rec')."""
    out = []
    stack = []        # kinds of the open braces
    i, n = 0, len(code)
    start = 0
    while i < n:
        c = code[i]
        if c == "{":
            m = None
            seg = code[start:i + 1]
            for mm in SCOPE_OPEN_RE.finditer(seg):
                if mm.end() == len(seg):
                    m = mm
            if m is not None and m.group("enum"):
                end = match_close(code, i)
                i = end + 1
                start = i
                continue
            kind = "rec" if (m is not None and m.group("rec")) else ("ns" if (m is not None and (m.group("ns") or m.group(0).lstrip().startswith("extern"))) else "other")
            if m is None:
                end = match_close(code, i)
                i = end + 1
                if is_function_head(seg[:-1]):
                    # a function definition's (blanked) body `R f(args) {...}`: the statement ends at its
                    # `}`, not at a `;` -- the next declaration must not merge into it (review loop 1 fix:
                    # `inline int F() { return 0; }\ninline int g = 0;` previously hid `g`)
                    start = i
                # else: an initialiser brace of a statement (`T x{...};`) -- its `;` ends it
                continue
            stack.append(kind)
            start = i + 1
            i += 1
            continue
        if c == "}":
            if stack:
                stack.pop()
            i += 1
            start = i
            continue
        if c == ";":
            text = code[start:i].strip()
            if text:
                out.append((tuple(stack), text, code.count("\n", 0, start + (len(code[start:i]) - len(code[start:i].lstrip()))) + 1))
            i += 1
            start = i
            continue
        i += 1
    return out


def declarator_name(text):
    """The variable name of a declaration statement: the last identifier before `=`, `{`, `[` or the end,
    ignoring a `(` that would mark a function declaration (None then)."""
    head = re.split(r"=|\{", text, 1)[0]
    if "(" in head:
        # `T x(args);` direct-init vs `R f(args);` function declaration: undecidable textually -> function
        return None
    head = re.sub(r"\[[^\]]*\]", "", head)         # arrays
    head = re.sub(r"\balignas\s*\([^)]*\)", "", head)
    ids = IDENT_RE.findall(head)
    if len(ids) < 2:
        return None
    return ids[-1]


def is_mutable(text):
    words = set(re.findall(r"[A-Za-z_]\w*", re.split(r"=|\{", text, 1)[0]))
    if "constexpr" in words or "consteval" in words:
        return False
    head = re.split(r"=|\{", text, 1)[0]
    if "*" in head or "&" in head:
        tail = head[max(head.rfind("*"), head.rfind("&")) + 1:]
        return "const" not in re.findall(r"[A-Za-z_]\w*", tail)
    return "const" not in words and "constinit" not in words


def lint_header(path, rel, allow, used):
    with open(path, "r", errors="replace") as f:
        src = f.read()
    code = blank_preprocessor(strip_cxx(src))
    code = blank_function_bodies(code)
    findings = []
    counts = {}
    for scope, text, line in statements(code):
        if any(k == "other" for k in scope):
            continue
        first = re.match(r"[A-Za-z_]\w*", text.lstrip("[ \t]"))
        if text.startswith(("template", "[[")):
            text2 = re.sub(r"^\[\[[^\]]*\]\]\s*", "", text)
            text2 = re.sub(r"^template\s*<[^;{]*?>\s*", "", text2) if text2.startswith("template") else text2
            if text2 != text:
                text = text2
                first = re.match(r"[A-Za-z_]\w*", text)
        if not first or first.group(0) in SKIP_START:
            continue
        in_record = bool(scope) and scope[-1] == "rec"
        words = re.findall(r"[A-Za-z_]\w*", re.split(r"=|\{", text, 1)[0])
        has_init = "=" in text or "{" in text
        if "extern" in words and not has_init:
            continue
        if in_record:
            if "static" not in words:
                continue                  # non-static data member
            if "inline" not in words and not has_init:
                continue                  # `static T n;` member declaration
            if "inline" not in words and has_init and not is_mutable(text):
                continue                  # `static const int k = 1;` in-class constant
        name = declarator_name(text)
        if name is None or name in ("override", "final", "default", "delete"):
            continue
        if not is_mutable(text):
            continue
        counts[name] = counts.get(name, 0) + 1
        findings.append((rel, line, name, text if len(text) < 90 else text[:87] + "..."))
    out = []
    for rel_, line, name, snip in findings:
        key = "%s:header-global:%s" % (rel_, name)
        if key in allow:
            used.add(key)
            cap, _ = allow[key]
            if counts[name] <= cap:
                continue
            out.append((rel_, line, name, snip + "  -- grew past its allowlisted count %d" % cap))
            continue
        out.append((rel_, line, name, snip))
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
    allow = {k: v for k, v in load_allowlist(os.path.join(root, a.allowlist)).items() if ":header-global:" in k}
    used = set()
    files, findings = 0, []
    for sub in a.scan:
        base = os.path.join(root, sub)
        if not os.path.isdir(base):
            continue
        for dp, dn, fn in os.walk(base):
            dn[:] = sorted(x for x in dn if x not in (".git", "__pycache__") and not x.startswith("build"))
            for name in sorted(fn):
                if not name.endswith(HEADER_EXT):
                    continue
                path = os.path.join(dp, name)
                rel = os.path.relpath(path, root).replace(os.sep, "/")   # the allowlists are written with /
                if any(re.search(p, "/" + rel) for p in EXEMPT_PATH_PATTERNS):
                    continue
                files += 1
                findings.extend(lint_header(path, rel, allow, used))
    for rel, line, name, snip in findings:
        print("%s:%d: mutable global definition in a header: %s  [%s]" % (rel, line, name, snip))
    for key in sorted(set(allow) - used):
        print("%s: WARN stale allowlist entry -- nothing to silence; remove it" % key)
    print("LintHeaderGlobals: %d headers scanned, %d mutable globals -> %s" % (files, len(findings), "FAIL" if findings else "PASS"))
    return 1 if findings else 0


def selftest():
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)

    d = tempfile.mkdtemp(prefix="lint_hdr.")
    cases = [
        ("ok1.h", "#pragma once\n#include <atomic>\nnamespace a {\nextern int g_count;\nconstexpr int kA = 1;\n"
                  "inline constexpr int kB = 2;\nstatic const int kC = 3;\nconst char* const kName = \"x\";\n"
                  "struct S { static int n; int m; static constexpr int k = 4; static const int j = 5; void f(); };\n"
                  "inline int F() { static int once = 0; return once; }\nusing T = int;\nenum class E { A, B };\n"
                  "enum Plain { X = 1, Y };\ntemplate <class T> constexpr T kV = T(1);\nvoid Decl(int a, int b);\n"
                  "struct Q { Q() : v_(0) {} int v_; };\n}\n", 0),
        ("bad1.h", "namespace a {\nint g_count;\n}\n", 1),
        ("bad2.h", "static int s_count = 0;\n", 1),
        ("bad3.h", "#include <atomic>\ninline std::atomic<int> g_live{0};\n", 1),
        ("bad4.h", "struct S {\n  static inline int n = 0;\n};\n", 1),
        ("bad5.h", "namespace a {\nconst char* g_name;\n}\n", 1),
        ("bad6.h", "namespace a {\ninline std::mutex g_mu;\n}\n", 1),
        ("ok2.h", "namespace a {\nchar* const g_p = nullptr;\nstatic constexpr double kPi = 3.14;\n}\n", 0),
        ("ok3.h", "struct S {\n  static int n;\n  std::atomic<int> live{0};\n  static const int kMax = 3;\n};\n", 0),
        ("ok4.h", "namespace a {\nstruct P { int x; };\nextern const P kTable[];\nconst P kLocal[] = { {1}, {2} };\n}\n", 0),
        ("bad7.inl", "namespace a {\ntemplate <class T> inline T Clamp(T v) { return v; }\ninline int g_calls = 0;\n}\n", 1),
        ("bad8.inc", "static int s_table_fill = 0;\n", 1),
        ("bad9.h", "namespace a {\ninline int F() { return 0; }\ninline int g_after_fn = 0;\n}\n", 1),
        ("bad10.h", "struct S { int f() const { return 1; } static inline int n = 0; };\n", 1),
        ("ok5.h", "class C {\n public:\n  C() : a_(1) {}\n  int Get() const { return a_; }\n"
                  "  static int Count() { static int c = 0; return ++c; }\n private:\n  int a_;\n"
                  "  static constexpr int kMax = 3;\n};\nnamespace b {\ninline int H(int) noexcept { }\nextern int e;\n}\n", 0),
    ]
    for name, text, expect in cases:
        p = os.path.join(d, name)
        with open(p, "w") as f:
            f.write(text)
        got = lint_header(p, name, {}, set())
        res = (len(got) > 0) == (expect > 0)
        check(res, "%-8s expect %s got %d %s" % (name, "clean" if expect == 0 else "finding", len(got), [g[2] for g in got]))
    p = os.path.join(d, "bad1.h")
    check(lint_header(p, "bad1.h", {"bad1.h:header-global:g_count": (1, 1)}, set()) == [], "allowlist silences bad1.h")
    check(len(lint_header(p, "bad1.h", {"bad1.h:header-global:g_count": (0, 1)}, set())) == 1, "a cap below the count fails")
    check(all(n.endswith(HEADER_EXT) for n in ("x.h", "x.hpp", "x.inl", "x.inc")) and not "x.cpp".endswith(HEADER_EXT),
          "HEADER_EXT covers .h .hpp .inl .inc and not .cpp")
    import shutil
    shutil.rmtree(d, ignore_errors=True)
    print("LintHeaderGlobals selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
