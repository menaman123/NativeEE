#!/usr/bin/env python3
"""Static inventory of the title's IOP module/heap/RPC SDK users (M11 startup audit).

Reads the generated NTSC shards and the SDK binding manifest; prints every
`libkernl.a` SIF entry (module load/reboot, IOP heap, RPC bind/call/server) with
its direct callers and the call chains back to title code. Direct `recomp_call`
edges only: computed/indirect dispatch (thread entries, vtables, callbacks) is
NOT proven absent by this inventory. Never executes a game.

  python3 tools/sif_startup_callers.py [--title games/<t>] [--json out.json] [--selftest]
"""
import argparse
import json
import os
import re
import sys
from pathlib import Path

DEFAULT_TITLE = Path("games/<title>")
SIF_RE = re.compile(r"Sif(Init|Alloc|Free|Load|Reboot|Reset|Bind|Call|Register|AddCmd|Set|Get|Exit|Check|Sync|Rpc)|Iop|Module|Rpc")
# Below: game/engine/Platform code; at or above: the statically linked SDK libraries (the lowest
# manifest row is libmpeg.a `_clearOnce` 0x2b6fd8; the Platform layer runs to ~0x2b6000).
TITLE_LIMIT = 0x2B6000


def read_symbols(title):
    out = {}
    for row in (title / "hle" / "sdk_bindings.tsv").read_text().splitlines():
        cols = row.split("\t")
        if len(cols) >= 3 and cols[0] == "libkernl.a" and SIF_RE.search(cols[1]):
            out[int(cols[2], 16)] = cols[1]
    return out


def read_edges(title):
    """{callee: {callers}} and {address: generated name} from the shards' direct calls."""
    names, reverse = {}, {}
    # The generated recomp TUs: PS2R_RECOMP_DIR (the build directory), else the title's tree dir.
    recomp = Path(os.environ["PS2R_RECOMP_DIR"]) if os.environ.get("PS2R_RECOMP_DIR") else title / "recomp"
    for path in sorted(recomp.glob("recompiled_shard_*.cpp")):
        body = None
        for line in path.read_text().splitlines():
            definition = re.match(r"// Function: (\S+) at (0x[0-9a-f]+)", line)
            if definition:
                body = int(definition[2], 16)
                names[body] = definition[1]
            call = re.search(r"recomp_call\(\d+u, (0x[0-9a-f]+)u", line)
            if call and body is not None:
                reverse.setdefault(int(call[1], 16), set()).add(body)
    return names, reverse


def chains(reverse, address, limit=6):
    """Every direct-call chain from `address` up to a root (no direct caller), bounded."""
    def walk(a, seen, depth):
        prev = sorted(reverse.get(a, set()))
        if not prev or depth >= limit:
            return [[a]]
        out = []
        for p in prev:
            if p in seen:
                out.append([a, p, "cycle"])
                continue
            for chain in walk(p, seen | {a}, depth + 1):
                out.append([a] + chain)
        return out
    return walk(address, set(), 0)


def title_roots(reverse, address):
    """The title-code (below TITLE_LIMIT) functions that reach `address` through SDK code: the
    first title address on each chain, deduplicated."""
    roots = set()
    for chain in chains(reverse, address):
        for a in chain[1:]:
            if isinstance(a, int) and a < TITLE_LIMIT:
                roots.add(a)
                break
    return sorted(roots)


def render(symbols, names, reverse):
    lines = ["| SDK entry | NTSC | direct callers | first title-code callers |", "|---|---|---|---|"]
    for address, name in sorted(symbols.items(), key=lambda kv: kv[1]):
        callers = sorted(reverse.get(address, set()))
        roots = title_roots(reverse, address)
        lines.append("| `%s` | `0x%08x` | %s | %s |" % (
            name, address,
            ", ".join("`0x%06x`" % c for c in callers) or "none (static direct)",
            ", ".join("`0x%06x`" % r for r in roots) or "-"))
    return "\n".join(lines) + "\n"


def selftest():
    reverse = {0x2D4AC0: {0x17CE50}, 0x17CE50: {0x100008}, 0x2D29A8: {0x2C5080, 0x1E2958}, 0x2C5080: {0x17CE50},
               0x1E2958: {0x1E13B0}, 0x1E13B0: {0x1E1500}}
    ok = chains(reverse, 0x2D4AC0) == [[0x2D4AC0, 0x17CE50, 0x100008]]
    ok &= title_roots(reverse, 0x2D4AC0) == [0x17CE50]
    ok &= title_roots(reverse, 0x2D29A8) == [0x17CE50, 0x1E2958]
    ok &= title_roots({0x2D45E0: {0x2AE1C8}, 0x2AE1C8: {0x2AE318}}, 0x2D45E0) == [0x2AE1C8]
    ok &= chains(reverse, 0x999999) == [[0x999999]] and title_roots(reverse, 0x999999) == []
    loop = {1: {2}, 2: {1}}
    ok &= chains(loop, 1) == [[1, 2, 1, "cycle"]]
    table = render({0x2D4AC0: "sceSifLoadModule"}, {}, reverse)
    ok &= "`sceSifLoadModule` | `0x002d4ac0` | `0x17ce50` | `0x17ce50` |" in table
    print("sif_startup_callers selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--title", default=str(DEFAULT_TITLE))
    ap.add_argument("--json", help="write {entry: {address, callers, title_roots}} here")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    title = Path(a.title)
    symbols = read_symbols(title)
    names, reverse = read_edges(title)
    sys.stdout.write(render(symbols, names, reverse))
    if a.json:
        data = {name: {"address": "0x%08x" % address,
                       "callers": ["0x%06x" % c for c in sorted(reverse.get(address, set()))],
                       "title_roots": ["0x%06x" % r for r in title_roots(reverse, address)]}
                for address, name in symbols.items()}
        Path(a.json).write_text(json.dumps(data, indent=1, sort_keys=True) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
