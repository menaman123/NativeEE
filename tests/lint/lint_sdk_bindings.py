#!/usr/bin/env python3
"""lint_sdk_bindings.py -- the two SdkBinding manifest lints of CONSTRUCTION_PLAN 4.8 over
`games/<title>/hle/sdk_bindings.tsv` (ARCHITECTURE 4.9: the engine holds handlers keyed by
(library, function); the TSV holds the addresses the sce-lib finder matched for THIS ELF).

  lint_sdk_bindings.py --check row-count [--tsv PATH]
      The data-row count (non-`#`, non-blank lines) == 298, the plan's figure
      (CONSTRUCTION_PLAN 4.8 "298 address-keyed rows"). The composition the file actually has
      is pinned too, per library, so a row added or removed under any library is named:
        libkernl 116, libmpeg 89, libcdvd 22, libpad 19, libmc 14, libscf 12, libdev 8,
        libipu 5, libdma 4, libc 4, libgraph 3, libsdr 2  (= 298)
      Every row has exactly four tab-separated columns (library, function, 0x-address, signature),
      the library ends in `.a`, the address parses, and the (library, function) pair and the
      address are each unique (two rows at one address would be two bindings for one function).

  lint_sdk_bindings.py --check no-game-address [--tsv PATH] [--tu PATH]
      "game-address bindings == 0": no row's address lies in the title's own code. What the
      check PROVES, from the data the tree has (there is no in-tree finder output yet):
        (a) every address lies inside the SDK band the manifest header declares
            (`SDK band 0x002b6fd8-0x002d6880`: the static-link block of the sce libraries,
            found by the finder with its max-gap rule) -- the title's own code lies below it
            (0x00100000..0x002b6fd7) and 34 generated functions lie above it
            (0x002d69a0..0x002d9968, unclassified by the finder: sce tail / crt / game); a
            binding on either side is out-of-band (the header's two rejections, 0x001818b0 /
            0x00287988, are the known examples and must stay absent);
        (b) every address lies in the generated output's TEXT (`games/<title>/recomp/recompiled.cpp`):
            either a FUNCTION START (a `recompiled_functions[addr]` install) or an INNER LABEL inside
            one generated function's `function_ranges` span -- the finder matched a signature at a
            label Ghidra folded into a larger function; the root reports those rows as
            `sdk_rows_not_fn_start=12` and never installs them. The inner-label set is PINNED
            (INNER_LABEL_ROWS below): a new inner-label row is named, never silently accepted;
        (c) the libc rows (not covered by the finder) are the four of OLD's guarded-libc table and
            sit inside the same band.
      It does NOT prove the signature match itself (that is the finder's job; `signature_id` is
      reserved for it) -- a row with a wrong NAME at a right-band address passes here and is caught
      by the engine's resolve pin (`SdkBindingManifestResolves`).

  lint_sdk_bindings.py --selftest
      Synthetic manifests: a 297-row file, a duplicate address, a 3-column row, an out-of-band
      address, an unpinned inner label, an address in a gap between generated functions, a pinned
      inner-label row removed, a missing header band -> each FAILS; the real manifest passes both.
Exit 0 PASS / 1 FAIL / 2 usage.
"""
import argparse
import os
import re
import sys
import tempfile
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
# No serial is hardcoded: PS2R_TITLE if set, else the one title directory under games/
# (one with an hle/ subdirectory). None, or several, leaves TITLE None -- the defaults
# below are then unusable and --tsv / --tu must be given, which is how ctest invokes this.
def _discover_title(root):
    explicit = os.environ.get("PS2R_TITLE")
    if explicit:
        return explicit
    games = os.path.join(root, "games")
    found = [d for d in sorted(os.listdir(games))
             if os.path.isdir(os.path.join(games, d, "hle"))] if os.path.isdir(games) else []
    return found[0] if len(found) == 1 else None

TITLE = _discover_title(ROOT)
TSV_DEFAULT = (os.path.join(ROOT, "games", TITLE, "hle", "sdk_bindings.tsv")
               if TITLE else None)
# The generated common TU is NOT in the tree (derived from the game; regenerated into the build
# directory from the user's disc, cmake/TitleInputs.cmake): PS2R_RECOMP_DIR names where the build
# put it; without the variable the historical in-tree path (a developer's regenerated copy).
_recomp_dir = os.environ.get("PS2R_RECOMP_DIR") or (
    os.path.join(ROOT, "games", TITLE, "recomp") if TITLE else None)
TU_DEFAULT = os.path.join(_recomp_dir, "recompiled.cpp") if _recomp_dir else None

# CONSTRUCTION_PLAN 4.8 / ARCHITECTURE 4.9 ":657": 298 address-keyed rows. The per-library
# composition is the FILE's (games/<t>/hle/README.md "Manifest reconciliation" explains each class).
EXPECTED_ROWS = 298
EXPECTED_PER_LIB = {
    "libkernl.a": 116, "libmpeg.a": 89, "libcdvd.a": 22, "libpad.a": 19, "libmc.a": 14,
    "libscf.a": 12, "libdev.a": 8, "libipu.a": 5, "libdma.a": 4, "libc.a": 4,
    "libgraph.a": 3, "libsdr.a": 2,
}
assert sum(EXPECTED_PER_LIB.values()) == EXPECTED_ROWS

# The 12 manifest rows whose address is an inner label of a generated function (not a function
# start): the root's run.meta `sdk_rows_not_fn_start_list` names exactly these; the dispatcher never
# reaches them as function entries, so they bind nothing today (ARCHITECTURE 4.9: the finder keys
# on the signature; the frozen generator keys on Ghidra's function list). Pinned so a change is seen.
INNER_LABEL_ROWS = {
    0x002be580: "_sceMpegWaitIpuIdle", 0x002be9e0: "_sceMpegFlushBuf", 0x002beb78: "_copyAddRefImage",
    0x002bec68: "_copyRefImage", 0x002c6290: "adddate", 0x002c6348: "subdate", 0x002c6448: "AdjustTime",
    0x002cffd0: "iWakeupThread", 0x002d53b0: "SetT2", 0x002d6168: "iSetTimerAlarm",
    0x002d6258: "sceSifInitRebootNotify", 0x002d65d8: "InitTLB",
}

BAND_RE = re.compile(r"SDK band (0x[0-9a-fA-F]+)\s*-\s*(0x[0-9a-fA-F]+)")
REJECT_RE = re.compile(r"\((0x[0-9a-fA-F]{8})\s+\S+,\s*(0x[0-9a-fA-F]{8})\s+\S+\)")
INSTALL_RE = re.compile(r"recompiled_functions\[0x([0-9a-fA-F]+)\]\s*=")
RANGE_RE = re.compile(r"function_ranges\.push_back\(\{0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+)")


class Manifest:
    def __init__(self, path):
        self.path = path
        self.rows = []          # (lineno, library, function, address, signature)
        self.comment_lines = 0
        self.blank_lines = 0
        self.band = None        # (lo, hi) inclusive
        self.rejections = []    # addresses the header names as out-of-band rejections
        self.errors = []
        with open(path) as f:
            for i, line in enumerate(f, 1):
                s = line.rstrip("\n")
                if s.startswith("#"):
                    self.comment_lines += 1
                    m = BAND_RE.search(s)
                    if m:
                        self.band = (int(m.group(1), 16), int(m.group(2), 16))
                    m = REJECT_RE.search(s)
                    if m:
                        self.rejections = [int(m.group(1), 16), int(m.group(2), 16)]
                    continue
                if not s.strip():
                    self.blank_lines += 1
                    continue
                cols = s.split("\t")
                if len(cols) != 4:
                    self.errors.append("%s:%d: expected 4 tab-separated columns, got %d" % (path, i, len(cols)))
                    continue
                lib, fn, addr, sig = cols
                if not lib.endswith(".a") or not fn:
                    self.errors.append("%s:%d: library must end in .a and function must be named: %r" % (path, i, cols))
                try:
                    a = int(addr, 16)
                except ValueError:
                    self.errors.append("%s:%d: address does not parse: %r" % (path, i, addr))
                    continue
                self.rows.append((i, lib, fn, a, sig))


def check_row_count(m):
    fails = list(m.errors)
    n = len(m.rows)
    if n != EXPECTED_ROWS:
        fails.append("data rows %d != %d (CONSTRUCTION_PLAN 4.8)" % (n, EXPECTED_ROWS))
    per = Counter(r[1] for r in m.rows)
    for lib in sorted(set(per) | set(EXPECTED_PER_LIB)):
        if per.get(lib, 0) != EXPECTED_PER_LIB.get(lib, 0):
            fails.append("%s: %d rows, expected %d" % (lib, per.get(lib, 0), EXPECTED_PER_LIB.get(lib, 0)))
    addrs = Counter(r[3] for r in m.rows)
    for a, c in sorted(addrs.items()):
        if c > 1:
            fails.append("address 0x%08x bound %d times" % (a, c))
    keys = Counter((r[1], r[2]) for r in m.rows)
    for k, c in sorted(keys.items()):
        if c > 1:
            fails.append("(library, function) %s bound %d times" % (k, c))
    print("LintSdkBindingRowCount: %d data rows (%d comment lines, %d blank) in %d libraries -> %s"
          % (n, m.comment_lines, m.blank_lines, len(per), "FAIL" if fails else "PASS"))
    return fails


def generated_text(tu_path):
    """(function starts, [(start, end) spans]) of the generated TU's install table."""
    starts, spans = set(), []
    with open(tu_path) as f:
        for line in f:
            mm = INSTALL_RE.search(line)
            if mm:
                starts.add(int(mm.group(1), 16))
            mr = RANGE_RE.search(line)
            if mr:
                spans.append((int(mr.group(1), 16), int(mr.group(2), 16)))
    spans.sort()
    return starts, spans


def inside_span(spans, a):
    import bisect
    i = bisect.bisect_right(spans, (a, 0xffffffff)) - 1
    return i >= 0 and spans[i][0] <= a < spans[i][1]


def check_no_game_address(m, tu_path):
    fails = list(m.errors)
    if m.band is None:
        fails.append("the manifest header names no `SDK band 0x..-0x..` line: the band is the game/SDK boundary this check needs")
        print("LintSdkBindingNoGameAddress: no band -> FAIL")
        return fails
    lo, hi = m.band
    starts, spans = generated_text(tu_path) if os.path.isfile(tu_path) else (None, None)
    if starts is None:
        fails.append("generated TU not found: %s" % tu_path)
    out_of_band, outside_text, rejected, inner = [], [], [], {}
    for lineno, lib, fn, a, _ in m.rows:
        if not (lo <= a <= hi):
            out_of_band.append("%s:%d %s %s 0x%08x outside the SDK band 0x%08x-0x%08x (game code)" % (m.path, lineno, lib, fn, a, lo, hi))
        if a in m.rejections:
            rejected.append("%s:%d %s %s 0x%08x is a header-named out-of-band rejection" % (m.path, lineno, lib, fn, a))
        if starts is not None and a not in starts:
            if inside_span(spans, a):
                inner[a] = fn
            else:
                outside_text.append("%s:%d %s %s 0x%08x is neither a generated function start nor inside one" % (m.path, lineno, lib, fn, a))
    fails += out_of_band + rejected + outside_text
    if starts is not None and inner != INNER_LABEL_ROWS:
        extra = sorted(set(inner) - set(INNER_LABEL_ROWS))
        gone = sorted(set(INNER_LABEL_ROWS) - set(inner))
        fails.append("inner-label rows changed: new %s, no longer inner %s (update INNER_LABEL_ROWS with the record)"
                     % (["0x%08x %s" % (a, inner[a]) for a in extra], ["0x%08x %s" % (a, INNER_LABEL_ROWS[a]) for a in gone]))
    print("LintSdkBindingNoGameAddress: %d rows, band 0x%08x-0x%08x, out-of-band %d, header rejections present %d, "
          "outside generated text %d, inner-label rows %d (pinned %d; function starts known: %s) -> %s"
          % (len(m.rows), lo, hi, len(out_of_band), len(rejected), len(outside_text), len(inner), len(INNER_LABEL_ROWS),
             "n/a" if starts is None else len(starts), "FAIL" if fails else "PASS"))
    return fails


def selftest():
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)

    # Every case below is derived from the real manifest's header and rows, so the selftest
    # needs one. A build with no title -- or a title that ships no sdk_bindings.tsv yet, like
    # the template -- has nothing to derive from: SKIP 77, never a silent pass.
    if not (TSV_DEFAULT and os.path.exists(TSV_DEFAULT)):
        print("lint_sdk_bindings selftest: SKIP -- this build has no title sdk_bindings.tsv")
        return 77
    have_tsv = True
    real = Manifest(TSV_DEFAULT)
    check(not check_row_count(real), "the real manifest passes the row-count lint")
    # The no-game-address cases read the generated common TU (its function starts): with no title
    # input the build did not generate it -- those cases are reported as skipped, the rest run.
    have_tu = have_tsv and bool(TU_DEFAULT) and os.path.exists(TU_DEFAULT)
    if not have_tu:
        print("  SKIP the no-game-address cases: no generated TU at %s (no title input in this build)" % TU_DEFAULT)

    def check_tu(cond_fn, what):
        if have_tu:
            check(cond_fn(), what)
        else:
            print("  SKIP %s" % what)

    check_tu(lambda: not check_no_game_address(real, TU_DEFAULT), "the real manifest passes the no-game-address lint")
    tmp = tempfile.mkdtemp(prefix="lint_sdk_bindings_")
    lines = open(TSV_DEFAULT).read().split("\n")
    header = [l for l in lines if l.startswith("#")]
    data = [l for l in lines if l and not l.startswith("#")]

    def write(name, body):
        p = os.path.join(tmp, name)
        open(p, "w").write("\n".join(body) + "\n")
        return p

    # 297 rows (one libpad row dropped)
    p = write("short.tsv", header + [l for l in data if not l.startswith("libpad.a\tscePadInit2\t")])
    f = check_row_count(Manifest(p))
    check(any("297 != 298" in x for x in f) and any("libpad.a: 18 rows" in x for x in f), "297 rows FAIL names the count and the library")
    # a duplicate address under another name
    p = write("dup.tsv", header + data + ["libpad.a\tscePadDup\t0x002c6ce0\t"])
    f = check_row_count(Manifest(p))
    check(any("0x002c6ce0 bound 2 times" in x for x in f), "a duplicate address FAILS")
    # a 3-column row
    p = write("cols.tsv", header + data[:-1] + ["libsdr.a\tsceSdRemote\t0x002c4cf0"])
    f = check_row_count(Manifest(p))
    check(any("expected 4 tab-separated columns" in x for x in f), "a 3-column row FAILS")
    # an out-of-band (game) address: the header's rejection 0x001818b0 bound
    p = write("oob.tsv", header + data[:-1] + ["libkernl.a\tsceNetcnfifCheck\t0x001818b0\t"])
    check_tu(lambda f=check_no_game_address(Manifest(p), TU_DEFAULT) if have_tu else []:
             any("outside the SDK band" in x for x in f) and any("header-named out-of-band rejection" in x for x in f),
             "a game address (the header's rejection) FAILS twice")
    # an in-band address that is an UNPINNED inner label (one word past sceGsSyncV's entry)
    p = write("mid.tsv", header + data[:-1] + ["libgraph.a\tsceGsSyncVMid\t0x002c1d7c\t"])
    check_tu(lambda f=check_no_game_address(Manifest(p), TU_DEFAULT) if have_tu else []:
             any("inner-label rows changed: new ['0x002c1d7c sceGsSyncVMid']" in x for x in f) and not any("outside" in x for x in f),
             "an unpinned in-band inner label FAILS as an inner-label change only")
    # an in-band address in a gap no generated function covers (0x2c1a30: between 0x2c1a24 and 0x2c1a40)
    p = write("gap.tsv", header + data[:-1] + ["libgraph.a\tsceGsGap\t0x002c1a30\t"])
    check_tu(lambda f=check_no_game_address(Manifest(p), TU_DEFAULT) if have_tu else []:
             any("neither a generated function start nor inside one" in x for x in f),
             "an in-band address outside every generated function FAILS")
    # a pinned inner-label row dropped
    p = write("noinner.tsv", header + [l for l in data if not l.startswith("libkernl.a\tInitTLB\t")])
    check_tu(lambda f=check_no_game_address(Manifest(p), TU_DEFAULT) if have_tu else []:
             any("no longer inner ['0x002d65d8 InitTLB']" in x for x in f), "a pinned inner-label row gone FAILS (named)")
    # the band line missing from the header
    p = write("noband.tsv", [l for l in header if "SDK band" not in l] + data)
    check_tu(lambda f=check_no_game_address(Manifest(p), TU_DEFAULT) if have_tu else []:
             any("names no `SDK band" in x for x in f), "a header without the band line FAILS")
    print("lint_sdk_bindings selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", choices=["row-count", "no-game-address"])
    ap.add_argument("--tsv", default=TSV_DEFAULT)
    ap.add_argument("--tu", default=TU_DEFAULT)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.check:
        ap.print_usage()
        return 2
    if not os.path.isfile(a.tsv):
        print("manifest not found: %s" % a.tsv)
        return 2
    m = Manifest(a.tsv)
    fails = check_row_count(m) if a.check == "row-count" else check_no_game_address(m, a.tu)
    for x in fails:
        print("  " + x)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
