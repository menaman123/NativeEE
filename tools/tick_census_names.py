#!/usr/bin/env python3
"""tick_census_names.py -- name the functions of a long tick's census row.

    python3 tools/tick_census_names.py RUN_META [ORDINAL ...]

Reads run.meta `pace_long_trace` (TickProfile::Format rows under diag.tick_census=on:
`ord:us:pending:sites,samples=over/total:f2>f1>f0:us:n:over,...,self=f:us:n,...,incl=f:us:n,...`)
and prints, per requested ordinal (default: every long tick), the site breakdown and the
sampled chains / self rows / inclusive owners with their names from
games/<title>/symbols/pal_ntsc_map.tsv (the PAL the title-reversed name
when the NTSC address has one, else the TR C++ name, else FUN_xxxxxxxx). A read-only helper.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAP = os.path.join(ROOT, "games", "<title>", "symbols", "pal_ntsc_map.tsv")


def load_names():
    names = {}
    with open(MAP) as f:
        for line in f:
            if line.startswith("#") or line.startswith("pal_addr"):
                continue
            cols = line.rstrip("\n").split("\t")
            if len(cols) < 3:
                continue
            try:
                ntsc = int(cols[2], 16)
            except ValueError:
                continue
            name = cols[1]
            if name.startswith("FUN_") and len(cols) > 7 and cols[7]:
                name = cols[7] + "(tr)"
            names[ntsc] = name
    return names


def name_of(names, addr):
    n = names.get(addr)
    return "%x:%s" % (addr, n) if n and not n.startswith("FUN_") else "%x" % addr


def parse_meta(path):
    meta = {}
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n").lstrip()
            if "=" in line:
                k, v = line.split("=", 1)
                meta.setdefault(k, v)
    return meta


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    meta = parse_meta(sys.argv[1])
    want = set(int(a) for a in sys.argv[2:])
    names = load_names()
    trace = meta.get("pace_long_trace", "none")
    entries = [] if trace == "none" else trace.split(";")
    # The longest tick's row is kept separately (the trace keeps the first 160 long ticks).
    wm = meta.get("pace_work_max_breakdown", "none")
    if wm != "none" and wm not in entries:
        entries.append(wm)
    if not entries:
        print("no long ticks")
        return 0
    period = 16667
    for entry in entries:
        m = re.match(r"(\d+):(\d+):(\d+):(.*)", entry)
        if not m:
            continue
        ordinal, us = int(m.group(1)), int(m.group(2))
        if want and ordinal not in want:
            continue
        rest = m.group(4)
        sites = rest.split(",samples=")[0]
        print("tick %d: %.1f ms (%.2f periods) pending=%s" % (ordinal, us / 1000.0, us / period, m.group(3)))
        print("  sites: %s" % sites)
        if ",samples=" not in rest:
            print("  (no census: diag.tick_census=off)")
            continue
        census = rest.split(",samples=", 1)[1]
        chains_part = census.split(",self=")[0]
        self_part = census.split(",self=")[1].split(",incl=")[0] if ",self=" in census else ""
        incl_part = census.split(",incl=")[1] if ",incl=" in census else ""
        samples = chains_part.split(":", 1)[0]
        print("  samples over/total: %s" % samples)
        chains = chains_part.split(":", 1)[1] if ":" in chains_part else ""
        for c in re.findall(r"([0-9a-f]+)>([0-9a-f]+)>([0-9a-f]+):(\d+):(\d+):(\d+)", chains):
            print("  chain %s > %s > %s  %.1f ms (%s samples, %s over)"
                  % (name_of(names, int(c[0], 16)), name_of(names, int(c[1], 16)), name_of(names, int(c[2], 16)),
                     int(c[3]) / 1000.0, c[4], c[5]))
        for s_ in re.findall(r"([0-9a-f]+):(\d+):(\d+)", self_part):
            print("  self  %-48s %.1f ms (%s samples)" % (name_of(names, int(s_[0], 16)), int(s_[1]) / 1000.0, s_[2]))
        for i in re.findall(r"([0-9a-f]+):(\d+):(\d+)", incl_part):
            print("  incl  %-48s %.1f ms (%s samples)" % (name_of(names, int(i[0], 16)), int(i[1]) / 1000.0, i[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
