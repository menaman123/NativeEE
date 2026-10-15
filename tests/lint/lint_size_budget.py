#!/usr/bin/env python3
"""lint_size_budget.py -- ARCHITECTURE.md section 11 size budgets, ctest target LintSizeBudget.

Measures, on the hand-written tree, the three budgets of the section 11 table:

  File       soft 1,000 lines  -> WARN       hard 1,500 -> WARN(hard)     fail 2,000 -> FAIL
  Function   soft   150 lines  -> WARN       hard   300 -> WARN(hard)     fail   400 -> FAIL
  Shard      soft  3 MB        -> WARN       hard  4 MB -> FAIL          (generated recomp shards)
             MB = decimal megabytes, 1 MB = 1,000,000 bytes (3,000,000 / 4,000,000), the unit the
             section 11 table and its status note use ("shard 028 at 4.30 MB" = 4,296,958 B); NOT MiB.

Scanned: every .cpp/.cc/.h/.hpp/.inc/.py under engine/ app/ games/ tools/ verify/ tests/.
Exempt (never measured against the file/function budgets): vendored third_party/ (app/gui/
third_party/ per CONSTRUCTION_PLAN 4.8 and the elfio/rabbitizer copies), tools/recompiler_v2/
(byte-identical OLD sources, PROVENANCE.md), generated gen/, the generated IOP recompiled pair,
verify/baseline and games/<t>/verify/baseline (frozen data, not code), __pycache__/build dirs.
The generated EE shards games/<t>/recomp/*.cpp|.h are measured ONLY against the shard byte
budget.

Function-length heuristic (stated so its misses are known):
  C++  -- comments and string literals are blanked first; a function body is the brace span
          that follows `ident(...)` + optional qualifiers (const, noexcept[(..)], override,
          final, &, &&, volatile, -> ret, requires ..., : init-list) where `ident` is not a
          control keyword (if/for/while/switch/catch/return/sizeof/decltype/alignof/
          constexpr/new/delete/throw/static_assert). Length = lines from the `{` line to the
          matching `}` line inclusive. Lambdas count inside their enclosing function (their
          `(` follows `]`). Known misses: bodies wrapped in macros, function-try-blocks
          (`) try {`), K&R definitions, `operator` overloads whose `(` follows `)` (`operator()`),
          and `struct X { ... } x;` which is not a function. Over-count risk: a parenthesised
          initialiser followed by a brace (`T x(1) {`) is not valid C++, so none known.
  Python -- a `def`/`async def` line at indent I extends to the line before the next non-blank,
          non-comment line at indent <= I (decorators of the next def end it). Nested defs are
          measured on their own AND inside their parent.

Allowlist (tests/lint/budget_allowlist.txt; shared by the three section 11 lints):
  <relpath>:<kind>:<measured>      with a `# YYYY-MM-DD <reason>` comment line directly above
  kind = file-lines | func-lines:<Name> | shard-bytes | header-global:<name> | session-tag:<text>
A FAIL is silenced only while the measured value is <= the allowlisted one: an allowlisted
offender that GROWS fails again. Entries that no longer silence anything are reported as
stale (WARN) so the table is never looser than the tree. The thresholds themselves are never
read from the allowlist.

Exit 0 = no unallowlisted FAIL (WARNs are the orchestrator's refactor queue), 1 = FAIL, 2 = usage.
"""
import argparse
import os
import re
import sys

ROOTS_DEFAULT = ["engine", "app", "games", "tools", "verify", "tests"]
CODE_EXT = (".cpp", ".cc", ".h", ".hpp", ".inc", ".py")
CXX_EXT = (".cpp", ".cc", ".h", ".hpp", ".inc")

FILE_SOFT, FILE_HARD, FILE_FAIL = 1000, 1500, 2000
FUNC_SOFT, FUNC_HARD, FUNC_FAIL = 150, 300, 400
MB = 1_000_000                       # decimal megabytes: the section 11 table's unit (see the docstring)
SHARD_SOFT, SHARD_HARD = 3 * MB, 4 * MB

EXEMPT_PATH_PATTERNS = [
    r"/third_party/",            # vendored (elfio, rabbitizer, app/gui/third_party ImGui): not ours to lint
    r"/tools/recompiler_v2/",    # byte-identical OLD recompiler sources (PROVENANCE.md)
    r"/tools/iop_recompiler/",   # the transplanted IOP recompiler: byte-identical OLD sources (its PROVENANCE.md)
    r"/gen/",                    # generated bindings
    r"/iop/stream_irx/iop_recompiled\.(h|cpp)$",
    r"/verify/baseline/",        # frozen data
    r"/__pycache__/",
]
SHARD_RE = re.compile(r"/games/[^/]+/recomp/[^/]+\.(cpp|h)$")

CONTROL_WORDS = {"if", "for", "while", "switch", "catch", "return", "sizeof", "decltype",
                 "alignof", "alignas", "constexpr", "new", "delete", "throw", "static_assert",
                 "defined", "typeid", "noexcept", "co_return", "co_await", "co_yield",
                 "and", "or", "not"}
QUALIFIER_RE = re.compile(
    r"\s*(?:const|volatile|override|final|&&|&|noexcept(?:\s*\([^()]*\))?|mutable|"
    r"->\s*[\w:<>,\s\*&\[\]]+?(?=\s*(?:\{|requires|noexcept|const|override|final))|"
    r"requires\s+[^{]*?(?=\{))\s*")
PY_DEF_RE = re.compile(r"^([ \t]*)(?:async\s+)?def\s+([A-Za-z_]\w*)\s*\(")


def strip_cxx(src):
    """Blank comments and string/char literals, preserving length and newlines."""
    out = []
    i, n = 0, len(src)
    while i < n:
        two = src[i:i + 2]
        if two == "//":
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i)); i = j
        elif two == "/*":
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", src[i:j])); i = j
        elif src[i] == '"' and i > 0 and src[i - 1] == "R" and (i < 2 or not (src[i - 2].isalnum() or src[i - 2] == "_")):
            # raw string literal R"delim( ... )delim": blank it whole (GLSL sources live in these)
            m = re.match(r'"([^(\s"\\]{0,16})\(', src[i:])
            if not m:
                out.append(src[i]); i += 1; continue
            end_tok = ")" + m.group(1) + '"'
            j = src.find(end_tok, i + m.end())
            j = n if j < 0 else j + len(end_tok)
            out.append(re.sub(r"[^\n]", " ", src[i:j])); i = j
        elif src[i] in "\"'":
            q = src[i]; j = i + 1
            while j < n and src[j] != q and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            j = min(n, j + 1)
            out.append(q + " " * max(0, j - i - 2) + (q if j - i >= 2 else "")); i = j
        else:
            out.append(src[i]); i += 1
    return "".join(out)


def match_close(code, open_idx, open_ch="{", close_ch="}"):
    depth = 0
    for k in range(open_idx, len(code)):
        c = code[k]
        if c == open_ch:
            depth += 1
        elif c == close_ch:
            depth -= 1
            if depth == 0:
                return k
    return len(code) - 1


def skip_init_list(code, k):
    """From ':' after ')' of a constructor, skip `ident(...)|ident{...}, ...` to the body '{'."""
    n = len(code)
    k += 1
    while k < n:
        while k < n and code[k].isspace():
            k += 1
        m = re.match(r"[A-Za-z_][\w:<>]*", code[k:])
        if not m:
            return None
        k += m.end()
        while k < n and code[k].isspace():
            k += 1
        if k >= n or code[k] not in "({":
            return None
        close = ")" if code[k] == "(" else "}"
        k = match_close(code, k, code[k], close) + 1
        while k < n and code[k].isspace():
            k += 1
        if k < n and code[k] == ",":
            k += 1
            continue
        return k if k < n and code[k] == "{" else None
    return None


def cxx_functions(code):
    """Yield (name, first_line, last_line) for every function body found by the heuristic."""
    out = []
    n = len(code)
    for m in re.finditer(r"([A-Za-z_][\w:<>~]*)\s*\(", code):
        name = m.group(1)
        base = name.rsplit("::", 1)[-1].lstrip("~")
        if base in CONTROL_WORDS or base in ("", "operator"):
            continue
        # the token before the identifier must not be `.`/`->` (a call) -- definitions are not member calls
        before = code[:m.start()].rstrip()
        if before.endswith((".", "->", "::")):
            continue
        open_p = m.end() - 1
        close_p = match_close(code, open_p, "(", ")")
        k = close_p + 1
        while True:
            q = QUALIFIER_RE.match(code, k)
            if not q or q.end() == k:
                break
            k = q.end()
        while k < n and code[k].isspace():
            k += 1
        if k < n and code[k] == ":":
            k2 = skip_init_list(code, k)
            if k2 is None:
                continue
            k = k2
        if k >= n or code[k] != "{":
            continue
        # a call statement `f(x) {` cannot occur; but `return f(x);` is filtered by `{` absent
        end = match_close(code, k)
        first = code.count("\n", 0, k) + 1
        last = code.count("\n", 0, end) + 1
        out.append((name, first, last))
    return out


def py_functions(lines):
    out = []
    for i, line in enumerate(lines):
        m = PY_DEF_RE.match(line)
        if not m:
            continue
        indent = len(m.group(1).expandtabs(8))
        end = len(lines)
        for j in range(i + 1, len(lines)):
            s = lines[j]
            if not s.strip() or s.lstrip().startswith("#"):
                continue
            if len(s) - len(s.lstrip()) <= indent and len(s.expandtabs(8)) - len(s.expandtabs(8).lstrip()) <= indent:
                end = j
                break
        # trailing blank/comment lines belong to the gap, not the function
        while end - 1 > i and not lines[end - 1].strip():
            end -= 1
        out.append((m.group(2), i + 1, end))
    return out


def load_allowlist(path):
    allow = {}
    if not os.path.isfile(path):
        return allow
    with open(path) as f:
        for ln, line in enumerate(f, 1):
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            parts = s.rsplit(":", 1)
            if len(parts) != 2 or not parts[1].isdigit():
                raise SystemExit("%s:%d: allowlist entry must be <relpath>:<kind>:<measured>, got %r" % (path, ln, s))
            allow[parts[0]] = (int(parts[1]), ln)
    return allow


class Report:
    def __init__(self, allow):
        self.allow = allow
        self.used = set()
        self.fails, self.warns = [], []
        self.census = {"file_soft": 0, "file_hard": 0, "file_fail": 0,
                       "func_soft": 0, "func_hard": 0, "func_fail": 0,
                       "shard_soft": 0, "shard_hard": 0, "files": 0, "functions": 0, "shards": 0}

    def fail(self, rel, kind, measured, msg):
        key = "%s:%s" % (rel, kind)
        if key in self.allow:
            self.used.add(key)
            cap, _ = self.allow[key]
            if measured <= cap:
                self.warns.append("%s: ALLOWLISTED %s (measured %d <= allowlisted %d)" % (rel, msg, measured, cap))
                return
            self.fails.append("%s: FAIL %s -- grew past its allowlisted %d" % (rel, msg, cap))
            return
        self.fails.append("%s: FAIL %s" % (rel, msg))

    def warn(self, rel, msg):
        self.warns.append("%s: WARN %s" % (rel, msg))


def lint_file(path, rel, rep):
    ext = os.path.splitext(path)[1]
    if SHARD_RE.search("/" + rel):
        size = os.path.getsize(path)
        rep.census["shards"] += 1
        if size > SHARD_HARD:
            rep.census["shard_hard"] += 1
            rep.fail(rel, "shard-bytes", size, "generated shard %d bytes > hard %d" % (size, SHARD_HARD))
        elif size > SHARD_SOFT:
            rep.census["shard_soft"] += 1
            rep.warn(rel, "generated shard %d bytes > soft %d" % (size, SHARD_SOFT))
        return
    with open(path, "r", errors="replace") as f:
        src = f.read()
    lines = src.split("\n")
    nlines = len(lines) - (1 if src.endswith("\n") else 0)
    rep.census["files"] += 1
    if nlines > FILE_FAIL:
        rep.census["file_fail"] += 1
        rep.fail(rel, "file-lines", nlines, "file %d lines > fail %d" % (nlines, FILE_FAIL))
    elif nlines > FILE_HARD:
        rep.census["file_hard"] += 1
        rep.warn(rel, "file %d lines > hard %d (hard)" % (nlines, FILE_HARD))
    elif nlines > FILE_SOFT:
        rep.census["file_soft"] += 1
        rep.warn(rel, "file %d lines > soft %d" % (nlines, FILE_SOFT))
    funcs = cxx_functions(strip_cxx(src)) if ext in CXX_EXT else py_functions(lines)
    for name, first, last in funcs:
        length = last - first + 1
        rep.census["functions"] += 1
        where = "%s:%d" % (rel, first)
        if length > FUNC_FAIL:
            rep.census["func_fail"] += 1
            rep.fail(rel, "func-lines:" + name, length, "function %s (%s) %d lines > fail %d" % (name, where, length, FUNC_FAIL))
        elif length > FUNC_HARD:
            rep.census["func_hard"] += 1
            rep.warn(rel, "function %s (%s) %d lines > hard %d (hard)" % (name, where, length, FUNC_HARD))
        elif length > FUNC_SOFT:
            rep.census["func_soft"] += 1
            rep.warn(rel, "function %s (%s) %d lines > soft %d" % (name, where, length, FUNC_SOFT))


def walk(root, scan):
    for sub in scan:
        base = os.path.join(root, sub)
        if not os.path.isdir(base):
            continue
        for dp, dn, fn in os.walk(base):
            dn[:] = sorted(x for x in dn if x not in (".git", "__pycache__") and not x.startswith("build"))
            for name in sorted(fn):
                if not name.endswith(CODE_EXT):
                    continue
                path = os.path.join(dp, name)
                rel = os.path.relpath(path, root)
                if any(re.search(p, "/" + rel) for p in EXEMPT_PATH_PATTERNS):
                    continue
                yield path, rel


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=".")
    ap.add_argument("--scan", nargs="*", default=ROOTS_DEFAULT)
    ap.add_argument("--allowlist", default="tests/lint/budget_allowlist.txt")
    ap.add_argument("--quiet-warns", action="store_true", help="print only FAIL lines and the census")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    root = os.path.abspath(a.root)
    rep = Report(load_allowlist(os.path.join(root, a.allowlist)))
    for path, rel in walk(root, a.scan):
        lint_file(path, rel, rep)
    for key, (cap, ln) in sorted(rep.allow.items()):
        if key not in rep.used and ":" in key and key.split(":", 1)[1].split(":")[0] in ("file-lines", "func-lines", "shard-bytes"):
            rep.warns.append("%s: WARN stale allowlist entry (line %d, cap %d) -- nothing to silence; remove it" % (key, ln, cap))
    if not a.quiet_warns:
        for w in rep.warns:
            print(w)
    for f in rep.fails:
        print(f)
    c = rep.census
    print("LintSizeBudget census: files %d (>%d: %d, >%d: %d, >%d: %d); functions %d (>%d: %d, >%d: %d, >%d: %d); "
          "shards %d (>%dMB: %d, >%dMB: %d)" % (
              c["files"], FILE_SOFT, c["file_soft"] + c["file_hard"] + c["file_fail"], FILE_HARD, c["file_hard"] + c["file_fail"],
              FILE_FAIL, c["file_fail"],
              c["functions"], FUNC_SOFT, c["func_soft"] + c["func_hard"] + c["func_fail"], FUNC_HARD, c["func_hard"] + c["func_fail"],
              FUNC_FAIL, c["func_fail"],
              c["shards"], SHARD_SOFT // MB, c["shard_soft"] + c["shard_hard"], SHARD_HARD // MB, c["shard_hard"]))
    print("LintSizeBudget: %d warnings, %d failures -> %s" % (len(rep.warns), len(rep.fails), "FAIL" if rep.fails else "PASS"))
    return 1 if rep.fails else 0


def selftest():
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)

    code = strip_cxx(
        'namespace a {\n'
        'struct T { int v() const { return 1; }\n  T() : x_(1), y_{2} {\n    int q = 0;\n  }\n int x_, y_; };\n'
        'static int Helper(int a,\n                  int b) noexcept {\n  if (a) {\n    return b;\n  }\n  for (;;) {}\n  return 0;\n}\n'
        'auto L = [](int z) { return z; };\n'
        'void Caller() { Helper(1, 2); obj.Method(3); }\n'
        'int T::big(int x) -> int { return x; }\n'
        '}\n')
    fns = cxx_functions(code)
    names = {n: (f, l) for n, f, l in fns}
    check("v" in names and names["v"] == (2, 2), "inline accessor found on one line -> %r" % (names.get("v"),))
    check("T" in names and names["T"] == (3, 5), "constructor with init list -> %r" % (names.get("T"),))
    check("Helper" in names and names["Helper"] == (8, 14), "multi-line signature + noexcept -> %r" % (names.get("Helper"),))
    check("Caller" in names and "Method" not in names and "L" not in names,
          "calls and lambdas are not definitions -> %r" % sorted(names))
    check("T::big" in names, "trailing return type -> %r" % sorted(names))
    raw = strip_cxx('const char* k = R"GLSL(\nvoid main() {\n  x;\n  y;\n}\n)GLSL";\nint f() { return 1; }\n')
    rn = {n for n, _, _ in cxx_functions(raw)}
    check(rn == {"f"}, "raw string literal contents are not code -> %r" % sorted(rn))
    py = ("import os\n\ndef a():\n    x = 1\n\n    return x\n\n\n@deco\ndef b(y):\n    def inner():\n        pass\n    return inner\n"
          "class C:\n    def m(self):\n        return 1\n").split("\n")
    pf = {n: (f, l) for n, f, l in py_functions(py)}
    check(pf.get("a") == (3, 6) and pf.get("b") == (10, 13) and pf.get("inner") == (11, 12) and pf.get("m") == (15, 16),
          "python def spans -> %r" % pf)
    import tempfile
    d = tempfile.mkdtemp(prefix="lint_size.")
    os.makedirs(os.path.join(d, "engine", "x"))
    big = "void f() {\n" + "  int q = 0;\n" * 500 + "}\n"
    with open(os.path.join(d, "engine", "x", "big.cpp"), "w") as f:
        f.write(big)
    with open(os.path.join(d, "allow.txt"), "w") as f:
        f.write("# 2026-10-06 test\nengine/x/big.cpp:func-lines:f:502\n")
    rep = Report({})
    lint_file(os.path.join(d, "engine", "x", "big.cpp"), "engine/x/big.cpp", rep)
    check(len(rep.fails) == 1 and "func" in rep.fails[0], "a 502-line function FAILs -> %r" % rep.fails)
    rep2 = Report(load_allowlist(os.path.join(d, "allow.txt")))
    lint_file(os.path.join(d, "engine", "x", "big.cpp"), "engine/x/big.cpp", rep2)
    check(not rep2.fails and any("ALLOWLISTED" in w for w in rep2.warns), "the allowlist entry silences it -> %r" % rep2.warns)
    rep3 = Report({"engine/x/big.cpp:func-lines:f": (400, 1)})
    lint_file(os.path.join(d, "engine", "x", "big.cpp"), "engine/x/big.cpp", rep3)
    check(len(rep3.fails) == 1 and "grew past" in rep3.fails[0], "growth past the allowlisted size fails again")
    print("LintSizeBudget selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
