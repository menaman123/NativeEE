#!/usr/bin/env python3
"""match_pal_ntsc.py -- map the PAL (SLES-52568) function symbols of the the title-reversed
decompilation onto this title's NTSC-U (SLUS-12345) function starts.

Both executables are the same codebase linked in the same order with a few insertions,
deletions and resized bodies between them, so the PAL->NTSC map is a SEQUENCE ALIGNMENT
of the two sorted function-start lists.  The only shape feature available on BOTH sides
without the PAL executable (the reference repository carries no game bytes) is the
function's extent (next start - start), so the aligner matches on extents, in order,
pinned by name anchors: the SDK functions whose names are known on both sides (this
title's sdk_bindings.tsv names <-> the PAL symbol table, aliases from ps2sdk.txt and the
reference's RETAIL(...) declarations).

    python3 tools/symbols/match_pal_ntsc.py --pal <symbol_addrs.txt> --ntsc <symbols.tsv>
        --sdk <sdk_bindings.tsv> [--ps2sdk <ps2sdk.txt>] [--tr-src <dir>...] --out <pal_ntsc_map.tsv>
    python3 tools/symbols/match_pal_ntsc.py --selftest

Output columns: pal_addr  name  ntsc_addr  confidence  method  ntsc_v2_name  ntsc_adjudicated  tr_cpp_name
  confidence: seed (name anchor, extent-verified) | high (equal extent, delta agrees with both
  neighbours) | medium (equal extent, a neighbour's delta differs or the run is short) |
  low (positional pairing inside a replaced run: the extents differ) | none (unmatched).
Nothing here is copied from the reference: names are used as names only.
"""
import argparse
import difflib
import os
import random
import re
import sys
from collections import Counter, OrderedDict


# ----------------------------------------------------------------------------- inputs
def load_pal(path):
    """symbol_addrs.txt: `Name = 0xADDR; // type:func` -> [(addr, name)] sorted."""
    rx = re.compile(r"^([A-Za-z_][A-Za-z0-9_.$]*)\s*=\s*0x([0-9A-Fa-f]+)\s*;\s*//.*type:func")
    out = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = rx.match(line)
            if m:
                out.setdefault(int(m.group(2), 16), m.group(1))
    return sorted(out.items())


def load_ntsc(path):
    """symbols.tsv: address<TAB>v2_name<TAB>adjudicated_name<TAB>source<TAB>status -> [(addr, v2, adj)]."""
    out = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if not line.strip() or line.startswith("#") or line.startswith("address"):
                continue
            p = line.rstrip("\n").split("\t")
            if len(p) < 3:
                continue
            try:
                a = int(p[0], 16)
            except ValueError:
                continue
            out.setdefault(a, (p[1], p[2] if p[2] != "-" else ""))
    return sorted((a, v, j) for a, (v, j) in out.items())


def load_sdk_names(path):
    """sdk_bindings.tsv: library<TAB>name<TAB>address -> {name: addr}."""
    out = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if not line.strip() or line.startswith("#"):
                continue
            p = line.rstrip("\n").split("\t")
            if len(p) >= 3:
                try:
                    out[p[1]] = int(p[2], 16)
                except ValueError:
                    pass
    return out


def load_aliases(ps2sdk_path):
    """ps2sdk.txt `name = target` lines: PAL symbol name -> the SDK name our manifest uses."""
    out = {}
    if not ps2sdk_path or not os.path.exists(ps2sdk_path):
        return out
    with open(ps2sdk_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            s = line.split("#")[0].strip()
            if "=" in s:
                a, b = [x.strip() for x in s.split("=", 1)]
                if b and b != "0":
                    out[a] = b
    return out


RETAIL_RX = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{()]*\)\s*(?:const\s*)?RETAIL(?:_N32)?\(([A-Za-z0-9_]+)\)")


def load_tr_cpp_names(dirs):
    """The reference's `CppName(...) RETAIL(RetailName)` declarations: retail name -> C++ name (names only)."""
    out = {}
    for d in dirs or []:
        for dp, _, fs in os.walk(d):
            for fn in fs:
                if not fn.endswith((".h", ".cpp")):
                    continue
                try:
                    with open(os.path.join(dp, fn), encoding="utf-8", errors="replace") as f:
                        text = f.read()
                except OSError:
                    continue
                for m in RETAIL_RX.finditer(text):
                    out.setdefault(m.group(2), m.group(1))
    return out


# ----------------------------------------------------------------------------- alignment
def extents(addrs):
    return [addrs[i + 1] - addrs[i] if i + 1 < len(addrs) else 0 for i in range(len(addrs))]


def name_anchors(pal, ntsc, sdk_names, aliases):
    """Every PAL symbol whose (aliased) name is a manifest name on our side: [(pi, ni, manifest_name)]."""
    ntsc_index = {a: i for i, (a, _, _) in enumerate(ntsc)}
    out = []
    for pi, (pa, pname) in enumerate(pal):
        for cand in (pname, aliases.get(pname)):
            if cand and cand in sdk_names and sdk_names[cand] in ntsc_index:
                out.append((pi, ntsc_index[sdk_names[cand]], cand))
                break
    return out


def pick_seeds(pal, ntsc, sdk_names, aliases, pal_ext, ntsc_ext):
    """Name anchors: a PAL symbol whose (aliased) name is a manifest name on our side, extents equal.
    Kept only when monotone (longest increasing subsequence on the NTSC index)."""
    cands = [(pi, ni) for pi, ni, _ in name_anchors(pal, ntsc, sdk_names, aliases) if pal_ext[pi] == ntsc_ext[ni]]
    cands.sort()
    # LIS on ni (strictly increasing) to drop the anchors that cross others.
    import bisect
    tails, links, best = [], [], []
    for k, (pi, ni) in enumerate(cands):
        pos = bisect.bisect_left([cands[t][1] for t in tails], ni)
        links.append(tails[pos - 1] if pos > 0 else -1)
        if pos == len(tails):
            tails.append(k)
        else:
            tails[pos] = k
    k = tails[-1] if tails else -1
    while k != -1:
        best.append(cands[k])
        k = links[k]
    best.reverse()
    # Drop an anchor whose delta disagrees with BOTH neighbours while they agree with each other.
    kept = []
    for i, (pi, ni) in enumerate(best):
        d = ntsc[ni][0] - pal[pi][0]
        if 0 < i < len(best) - 1:
            dl = ntsc[best[i - 1][1]][0] - pal[best[i - 1][0]][0]
            dr = ntsc[best[i + 1][1]][0] - pal[best[i + 1][0]][0]
            if dl == dr and d != dl:
                continue
        kept.append((pi, ni))
    return kept


def rejected_anchors(pal, ntsc, sdk_names, aliases, pal_ext, ntsc_ext, seeds, final):
    """The manifest names whose PAL namesake was NOT kept as a seed, with the reason and where the map
    put the PAL function instead. These are exactly the finder-miss / relink suspects: a name present on
    both sides that the shape evidence refuses to pin (the manifest's address may be the wrong function,
    or the library was linked elsewhere in the other build).
    Returns [(manifest_name, pal_addr, manifest_ntsc_addr, reason, mapped_ntsc_addr_or_None, mapped_conf)]."""
    kept = {(pi, ni) for pi, ni in seeds}
    out = []
    for pi, ni, name in name_anchors(pal, ntsc, sdk_names, aliases):
        if (pi, ni) in kept:
            continue
        reason = "extent-mismatch" if pal_ext[pi] != ntsc_ext[ni] else "non-monotone-or-delta-outlier"
        mapped = final.get(pi)
        out.append((name, pal[pi][0], ntsc[ni][0], reason,
                    ntsc[mapped[0]][0] if mapped else None, mapped[1] if mapped else "none"))
    return out


def align_segment(pal_ext, ntsc_ext, p0, p1, n0, n1):
    """Align pal[p0:p1] with ntsc[n0:n1] on extents. Returns [(pi, ni, equal_extent)]."""
    a = pal_ext[p0:p1]
    b = ntsc_ext[n0:n1]
    pairs = []
    if not a or not b:
        return pairs
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    last_a, last_b = 0, 0
    for i, j, n in sm.get_matching_blocks():
        # the replaced run between the previous block and this one: pair positionally
        ra, rb = range(last_a, i), range(last_b, j)
        for k in range(min(len(ra), len(rb))):
            pairs.append((p0 + ra[k], n0 + rb[k], False))
        for k in range(n):
            pairs.append((p0 + i + k, n0 + j + k, True))
        last_a, last_b = i + n, j + n
    return pairs


def match(pal, ntsc, seeds):
    pal_addrs = [a for a, _ in pal]
    ntsc_addrs = [a for a, _, _ in ntsc]
    pal_ext, ntsc_ext = extents(pal_addrs), extents(ntsc_addrs)
    result = {}   # pi -> (ni, confidence, method)
    bounds = [(-1, -1)] + list(seeds) + [(len(pal), len(ntsc))]
    for (pa, na), (pb, nb) in zip(bounds, bounds[1:]):
        if pa >= 0:
            result[pa] = (na, "seed", "name-anchor+extent")
        for pi, ni, eq in align_segment(pal_ext, ntsc_ext, pa + 1, pb, na + 1, nb):
            result[pi] = (ni, "equal" if eq else "low", "extent-align" if eq else "positional-in-replaced-run")
    # confidence from the delta of the neighbours
    order = sorted(result)
    deltas = {pi: ntsc_addrs[result[pi][0]] - pal_addrs[pi] for pi in order}
    final = {}
    for k, pi in enumerate(order):
        ni, conf, method = result[pi]
        if conf == "equal":
            dl = deltas[order[k - 1]] if k > 0 else None
            dr = deltas[order[k + 1]] if k + 1 < len(order) else None
            conf = "high" if (dl == deltas[pi] and dr == deltas[pi]) else "medium"
        final[pi] = (ni, conf, method)
    return final, pal_ext, ntsc_ext


# ----------------------------------------------------------------------------- selftest
def selftest(seed=7):
    rnd = random.Random(seed)
    # an NTSC-like list of 3000 functions with extents like a MIPS binary's (multiples of 8, heavy small tail)
    sizes = [8 * max(1, int(rnd.expovariate(1 / 40.0))) for _ in range(3000)]
    ntsc_addrs, a = [], 0x100000
    for s in sizes:
        ntsc_addrs.append(a)
        a += s
    # the PAL build: 2% functions inserted, 2% deleted, 3% resized, then re-laid-out in order
    pal_items = []   # (truth ntsc index or None, size)
    for i, s in enumerate(sizes):
        r = rnd.random()
        if r < 0.02:
            pal_items.append((None, 8 * rnd.randint(1, 60)))   # inserted (PAL-only)
        if r < 0.04 and r >= 0.02:
            continue   # deleted (NTSC-only)
        if 0.04 <= r < 0.07:
            pal_items.append((i, max(8, s + 8 * rnd.randint(-3, 3) or 8)))   # resized
        else:
            pal_items.append((i, s))
    pal_addrs, a = [], 0x100000 + 0x40
    for _, s in pal_items:
        pal_addrs.append(a)
        a += s
    ntsc = [(ad, f"FUN_{ad:08x}", "") for ad in ntsc_addrs]
    pal = [(ad, f"FUN_{ad:08x}") for ad in pal_addrs]
    # 12 name anchors spread over the list
    sdk_names, truth_anchor = {}, []
    for k in range(12):
        pi = (k + 1) * len(pal_items) // 13
        while pal_items[pi][0] is None:
            pi += 1
        ni = pal_items[pi][0]
        sdk_names[f"anchor{k}"] = ntsc_addrs[ni]
        pal[pi] = (pal[pi][0], f"anchor{k}")
        ntsc[ni] = (ntsc[ni][0], ntsc[ni][1], f"anchor{k}")
    pal_ext, ntsc_ext = extents(pal_addrs), extents(ntsc_addrs)
    seeds = pick_seeds(pal, ntsc, sdk_names, {}, pal_ext, ntsc_ext)
    assert len(seeds) >= 10, f"selftest: only {len(seeds)} seeds survived"
    final, _, _ = match(pal, ntsc, seeds)
    total = sum(1 for t, _ in pal_items if t is not None)
    right = wrong = 0
    by_conf = Counter()
    for pi, (ni, conf, _) in final.items():
        truth = pal_items[pi][0]
        if truth is None:
            continue
        by_conf[conf] += 1
        if ni == truth:
            right += 1
        else:
            wrong += 1
    hi_wrong = sum(1 for pi, (ni, conf, _) in final.items() if conf in ("high", "seed") and pal_items[pi][0] not in (None, ni))
    rate = right / total
    print(f"selftest: {total} true pairs, {right} right, {wrong} wrong ({rate:.1%}); high/seed wrong = {hi_wrong}; by confidence {dict(by_conf)}")
    assert rate >= 0.95, "selftest: match rate below 95%"
    assert hi_wrong == 0, "selftest: a high-confidence pair is wrong"
    # anchors are never violated
    for pi, ni in seeds:
        assert final[pi][0] == ni and final[pi][1] == "seed"
    print("selftest: PASS")
    return 0


# ----------------------------------------------------------------------------- main
def band_of(addr, sdk_lo, sdk_hi):
    if addr < sdk_lo:
        return "game"
    if addr < sdk_hi:
        return "sdk"
    return "tail"


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pal")
    ap.add_argument("--ntsc")
    ap.add_argument("--sdk")
    ap.add_argument("--ps2sdk")
    ap.add_argument("--tr-src", action="append", default=[])
    ap.add_argument("--out")
    ap.add_argument("--sample", type=int, default=0, help="print N sampled matches for a plausibility check")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)
    if args.selftest:
        return selftest()
    if not (args.pal and args.ntsc and args.sdk and args.out):
        ap.error("--pal, --ntsc, --sdk and --out are required (or --selftest)")
    pal, ntsc = load_pal(args.pal), load_ntsc(args.ntsc)
    sdk_names = load_sdk_names(args.sdk)
    aliases = load_aliases(args.ps2sdk)
    cpp_names = load_tr_cpp_names(args.tr_src)
    pal_ext, ntsc_ext = extents([a for a, _ in pal]), extents([a for a, _, _ in ntsc])
    seeds = pick_seeds(pal, ntsc, sdk_names, aliases, pal_ext, ntsc_ext)
    final, _, _ = match(pal, ntsc, seeds)
    sdk_lo, sdk_hi = min(sdk_names.values()), max(sdk_names.values()) + 1
    rows, by_conf, by_band = [], Counter(), {}
    for pi, (pa, pname) in enumerate(pal):
        if pi in final:
            ni, conf, method = final[pi]
            na, v2, adj = ntsc[ni]
        else:
            na, v2, adj, conf, method = None, "", "", "none", "unaligned"
        by_conf[conf] += 1
        b = band_of(pa, sdk_lo, sdk_hi)
        by_band.setdefault(b, Counter())[conf] += 1
        rows.append((pa, pname, na, conf, method, v2, adj, cpp_names.get(pname, "")))
    with open(args.out, "w") as f:
        f.write("# pal_ntsc_map.tsv -- PAL (SLES-52568, the title-reversed symbol_addrs.txt) function starts mapped onto\n")
        f.write("# SLUS-12345 function starts by in-order extent alignment pinned on the SDK name anchors\n")
        f.write(f"# (tools/symbols/match_pal_ntsc.py). {len(pal)} PAL functions, {len(ntsc)} NTSC; seeds {len(seeds)}; ")
        f.write("by confidence " + ", ".join(f"{k}={v}" for k, v in sorted(by_conf.items())) + ".\n")
        f.write("# A row is a PROPOSAL: adopt a name only after the plausibility check of HLE_COVERAGE.md section 6.\n")
        f.write("pal_addr\tname\tntsc_addr\tconfidence\tmethod\tntsc_v2_name\tntsc_adjudicated\ttr_cpp_name\n")
        for pa, pname, na, conf, method, v2, adj, cpp in rows:
            f.write(f"0x{pa:08x}\t{pname}\t{('0x%08x' % na) if na is not None else '-'}\t{conf}\t{method}\t{v2}\t{adj}\t{cpp}\n")
    n = len(pal)
    print(f"pal {n} functions, ntsc {len(ntsc)}, seeds {len(seeds)} (sdk band 0x{sdk_lo:x}-0x{sdk_hi:x})")
    print("by confidence: " + ", ".join(f"{k}={v} ({v / n:.1%})" for k, v in sorted(by_conf.items())))
    for b in ("game", "sdk", "tail"):
        c = by_band.get(b, Counter())
        tot = sum(c.values()) or 1
        print(f"  band {b:4s}: " + ", ".join(f"{k}={v} ({v / tot:.0%})" for k, v in sorted(c.items())))
    rejects = rejected_anchors(pal, ntsc, sdk_names, aliases, pal_ext, ntsc_ext, seeds, final)
    print(f"rejected anchors: {len(rejects)} (manifest names known on both sides that the extent evidence refused to pin;"
          " finder-miss / relink suspects)")
    for name, pa, manifest_na, reason, mapped_na, mapped_conf in rejects:
        where = f"0x{mapped_na:08x} {mapped_conf}" if mapped_na is not None else "-          none"
        agree = "agrees" if mapped_na == manifest_na else "DIFFERS"
        print(f"  {name:28s} pal 0x{pa:08x} manifest 0x{manifest_na:08x} {reason:30s} map-> {where} ({agree})")
    if args.sample:
        rnd = random.Random(1)
        named = [r for r in rows if r[7] and r[3] in ("high", "medium", "seed")]
        for pa, pname, na, conf, method, v2, adj, cpp in sorted(rnd.sample(named, min(args.sample, len(named)))):
            print(f"  0x{pa:08x} {pname:28s} -> 0x{na:08x} {v2:14s} {adj:24s} {conf:6s} TR:{cpp}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
