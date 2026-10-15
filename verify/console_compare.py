#!/usr/bin/env python3
"""console_compare.py -- the CONSOLE (a PCSX2 EE RAM image) as the arbiter between an OLD and a NEW .ps2st image
over the title's allocators: the page pool (slab), the record pool (compacting handle heap), the title heap, and a
list of plain words. Every structure is walked by the title's OWN layout (the same walkers as `slab_walk.py` /
`relocate_compare.py`); objects are compared by CONTENT (pointer-insensitive fingerprint), never by address.

  console_compare.py <console> <old.ps2st> <new.ps2st> --slabpool <hdr> --classes <n> --pool <hdr> [--pool-used <addr>]
                     [--heap <hdr>] [--moment <addr>[,<addr>...]] [--bootsm <addr>] [--word <addr>[:<bytes>] ...]
                     [--arena lo-hi ...] [--meta-routed <size>] [--fp-limit <bytes>] [--out json] [--list N]
  console_compare.py --selftest

Content fingerprints: every pointer-range word is masked, and the pointer arenas are ONE list applied on all three
sides (the union of the sides' page pools / record pools / heaps / `--arena`, plus the META range whenever any side
carries a META section -- its span is the images' META section length, never a constant of this tool; the trees
route descriptors there, the console never holds such an address; an asymmetric list would tokenise a raw data word
in that range `PTR!` on one side and raw on the other = a false mismatch). The fingerprint covers the WHOLE object
unless `--fp-limit` caps it; the cap, when set, is stated in the output (two objects equal up to the cap and differing
beyond would compare EQUAL).

<console> is either a raw 32 MB EE RAM dump (`*_ee.bin`, PCSX2 `eeMemory.bin`; the save's EE memory as the console
sees it) or a .ps2st. The descriptor words of the record pool are read through ONE accessor that resolves an address
in RAM0 or, for the trees' images, in the META section (`0x02000000 + off`): on the console the pool's descriptors
are slab chunks in RAM (the title keeps them on the shared page pool), in the trees they are routed to the META
arena by the META-ARENA workaround -- the SAME walk serves both, and the size class that absorbs those routed nodes
on the console (`--meta-routed`, the workaround's payload cap) is reported separately so the page-pool verdict is
not polluted by a known, named construction difference.

Output (text + `--out` JSON):
  moment      the matched-moment words on the three sides (record counter, boot-SM case word, ...)
  heap        used/free block counts per side and the three-way block-content match
  slab        per size class: in-use pages and used chunks per side; the three-way object sets by content
              (all / con+old / con+new / con-only / old-only / new-only); the PAGE verdict over the pages whose
              class differs between OLD and NEW (the "reassigned" pages): class on the console == NEW's, == OLD's,
              neither (another class / free) -- weighted by the pages' OLD-vs-NEW differing words
  records     per side: records, used/free/pending; the three-way record-content match; the one-sided records
              of each tree and whether the console holds them (by (size, fingerprint)); where the console's
              descriptors live (slab class)
  words       the listed plain words / byte ranges on the three sides

Sets are built ONCE per image. Files only (no stdin).
"""
import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import relocate_compare as rc  # noqa: E402
import slab_walk  # noqa: E402

META_BASE = rc.META_BASE
RAM_BYTES = 32 * 1024 * 1024


class Mem:
    """One accessor over RAM0 (+ META for a .ps2st). `w(a)` resolves RAM or META by address."""

    def __init__(self, path):
        self.path = path
        if path.endswith(".ps2st"):
            img = rc.Image(path)
            self.ram, self.meta, self.kind = img.ram, img.meta, "ps2st"
        else:
            with open(path, "rb") as f:
                self.ram = f.read()
            if len(self.ram) != RAM_BYTES:
                raise ValueError("%s: a raw EE image is %d bytes, got %d" % (path, RAM_BYTES, len(self.ram)))
            self.meta, self.kind = b"", "ee.bin"

    def w(self, a):
        if a >= META_BASE:
            off = a - META_BASE
            if off + 4 > len(self.meta):
                raise ValueError("%s: word 0x%x outside RAM0 and META" % (self.path, a))
            return struct.unpack_from("<I", self.meta, off)[0]
        return struct.unpack_from("<I", self.ram, a)[0]

    def q(self, a):
        return struct.unpack_from("<Q", self.ram, a)[0]

    def in_meta(self, a):
        return a >= META_BASE and a - META_BASE < len(self.meta)


# ------------------------------------------------------------------ the record pool (descriptors wherever they live)

def walk_records(m, pool, used_addr=None):
    """[(buf, size, idx, desc, state)] in chain order; state 'USED' | 'FREE' | 'PENDING'. The allocator's own state is
    asserted: free-flagged descriptors == the free heads' reach, their count == +0x4c, used == the live counter."""
    base, size = m.w(pool), m.w(pool + 4)
    d = m.w(pool + 0x50)
    recs, seen = [], set()
    while d and d not in seen:
        seen.add(d)
        idx, buf, sz, nxt, flags = m.w(d), m.w(d + 4), m.w(d + 8), m.w(d + 0x18), m.w(d + 0x1c)
        if not (base <= buf < base + size):
            raise ValueError("record walk: buf 0x%x outside pool 0x%x+0x%x (desc 0x%x)" % (buf, base, size, d))
        if flags & rc.POOL_FLAG_FREE:
            st = "FREE"
        elif flags & rc.POOL_FLAG_PENDING:
            st = "PENDING"
        else:
            st = "USED"
        recs.append((buf, sz, idx, d, st))
        d = nxt
        if len(recs) > 200000:
            raise ValueError("record walk: runaway")
    if d:
        raise ValueError("record walk: descriptor cycle at 0x%x" % d)
    for a, b in zip(recs, recs[1:]):
        if b[0] < a[0] + a[1]:
            raise ValueError("record walk: records overlap 0x%x+0x%x > 0x%x" % (a[0], a[1], b[0]))
    reach = set()
    for off in range(0xc, 0x4c, 4):
        f = m.w(pool + off)
        guard = 0
        while f and f not in reach:
            reach.add(f)
            f = m.w(f + 0x10)
            guard += 1
            if guard > 100000:
                raise ValueError("record walk: free list runaway at head +0x%x" % off)
    flagged = {r[3] for r in recs if r[4] == "FREE"}
    if flagged != reach:
        raise ValueError("record walk: free-flagged %d != free-list reachable %d" % (len(flagged), len(reach)))
    if m.w(pool + 0x4c) != len(flagged):
        raise ValueError("record walk: header free count %d != free descriptors %d" % (m.w(pool + 0x4c), len(flagged)))
    used = sum(1 for r in recs if r[4] == "USED")
    hdr = {"base": base, "size": size, "count": len(recs), "used": used, "free": len(flagged),
           "pending": sum(1 for r in recs if r[4] == "PENDING"), "desc_in_meta": sum(1 for r in recs if m.in_meta(r[3])),
           "desc_in_ram": sum(1 for r in recs if not m.in_meta(r[3])), "used_cnt_word": None}
    if used_addr is not None:
        hdr["used_cnt_word"] = m.w(used_addr)
        if hdr["used_cnt_word"] != used:
            raise ValueError("record walk: used %d != live counter 0x%x = %d" % (used, used_addr, hdr["used_cnt_word"]))
    return recs, hdr


# ------------------------------------------------------------------ three-way sets

def _three_way(sets):
    """sets: {'con': {key: n}, 'old': {...}, 'new': {...}} -> counts of keys (with multiplicity) per membership."""
    con, old, new = sets["con"], sets["old"], sets["new"]
    keys = set(con) | set(old) | set(new)
    out = {"all": 0, "con+old": 0, "con+new": 0, "con-only": 0, "old-only": 0, "new-only": 0, "old+new": 0}
    for k in keys:
        c, o, n = con.get(k, 0), old.get(k, 0), new.get(k, 0)
        t = min(c, o, n)
        out["all"] += t
        c, o, n = c - t, o - t, n - t
        t = min(c, o)
        out["con+old"] += t
        c, o = c - t, o - t
        t = min(c, n)
        out["con+new"] += t
        c, n = c - t, n - t
        t = min(o, n)
        out["old+new"] += t
        o, n = o - t, n - t
        out["con-only"] += c
        out["old-only"] += o
        out["new-only"] += n
    return out


def meta_span(mems):
    """The META arena's address span AS THE IMAGES CARRY IT: the longest META section among the sides. A .ps2st
    writes the whole arena as its META section, so that section's length is the arena's size; a raw EE image has
    none. Derived from the images, never a literal: the harness carries no address bound of its own."""
    return max(len(m.meta) for m in mems.values())


def _fp(m, lo, size, arenas, limit):
    """Pointer-insensitive content fingerprint over the WHOLE object (limit 0) or its first `limit` bytes."""
    return rc.fingerprint(m.ram, lo, size, arenas, limit=limit if limit else size)


def _arenas(mems, pools, hdrs, heap, extra):
    """ONE pointer-arena list applied on every side. The three sides' page pools, record pools and heaps are unioned
    (a word is masked as a pointer when it falls in ANY side's arena) and the META range is included on EVERY side
    whenever any side carries a META section (its span = `meta_span`): the trees route the record-pool descriptors
    into META and the console never holds an address in that range, so masking it everywhere keeps a raw data word
    that happens to fall in `[META_BASE, META_BASE + span)` tokenised the same on all three sides. An asymmetric list
    (META only where a META section exists) would tokenise such a word `PTR!` on the tree side and raw on the console
    -> a false content mismatch."""
    ar = set(tuple(x) for x in extra)
    span = meta_span(mems)
    if span:
        ar.add((META_BASE, META_BASE + span))
    for s in mems:
        p = pools[s]
        ar.add((p.pages, p.pages + p.npages * slab_walk.PAGE_BYTES))
        ar.add((hdrs[s]["base"], hdrs[s]["base"] + hdrs[s]["size"]))
        if heap is not None:
            ar.add((mems[s].w(heap + 4), mems[s].w(heap + 8)))
    return sorted(ar)


# ------------------------------------------------------------------ the compare

def compare(mems, slabpool, ncls, pool, used_addr=None, heap=None, moment=(), bootsm=None, words=(), arenas=(),
            meta_routed=None, list_cap=40, fp_limit=0):
    """`fp_limit` = bytes of each object hashed into its content fingerprint; 0 (default) = the whole object. A cap
    makes two objects equal in their first `fp_limit` bytes and differing beyond compare EQUAL -- it is reported."""
    sides = ("con", "old", "new")
    res = {"images": {s: {"path": mems[s].path, "kind": mems[s].kind} for s in sides}, "fp_limit": fp_limit}
    # the moment
    mo = {}
    for a in moment:
        mo["0x%x" % a] = {s: "0x%x" % mems[s].w(a) for s in sides}
    if bootsm is not None:
        mo["bootsm@0x%x" % bootsm] = {s: (mems[s].q(bootsm) >> 44) & 0x3f for s in sides}
    res["moment"] = mo
    # the pools
    pools = {s: slab_walk.Pool(mems[s], slabpool, ncls) for s in sides}
    recs = {}
    hdrs = {}
    for s in sides:
        recs[s], hdrs[s] = walk_records(mems[s], pool, used_addr)
    ar_all = _arenas(mems, pools, hdrs, heap, arenas)
    ar = {s: ar_all for s in sides}
    res["arenas"] = ["0x%x-0x%x" % (lo, hi) for lo, hi in ar_all]
    # ---- heap
    if heap is not None:
        hb = {}
        hres = {}
        for s in sides:
            blocks, hh = rc.walk_heap(mems[s], heap)
            hres[s] = {"used": hh["used_cnt"], "free": hh["free_cnt"], "used_bytes": hh["used_bytes"]}
            keyed = {}
            for lo, size, free in blocks:
                if not free:
                    k = (size, _fp(mems[s], lo, size, ar[s], fp_limit))
                    keyed[k] = keyed.get(k, 0) + 1
            hb[s] = keyed
        hres["match"] = _three_way(hb)
        res["heap"] = hres
    # ---- slab: per class
    chunks = {s: {} for s in sides}      # (cls, fp) -> n
    percls = {}
    for s in sides:
        p = pools[s]
        for idx, (c, chunk, cap, fcnt, fc) in p.page.items():
            e = percls.setdefault(c, {"chunk": chunk, "pages": {t: 0 for t in sides}, "used": {t: 0 for t in sides}})
            e["pages"][s] += 1
            e["used"][s] += cap - fcnt
        for c, lo, size in p.used_chunks():
            k = (c, _fp(mems[s], lo, size, ar[s], fp_limit))
            chunks[s][k] = chunks[s].get(k, 0) + 1
    # the slab class that hosts the console's record descriptors (routed to META in the trees)
    desc_cls = {}
    for r in recs["con"]:
        st = pools["con"].status(r[3])
        key = ("cls %d/%dB" % (st[1], st[3])) if st and st[0] == "USED" else "outside-slab"
        desc_cls[key] = desc_cls.get(key, 0) + 1
    slab = {"census": {s: pools[s].census() for s in sides}, "classes": [], "console_descriptor_chunks": desc_cls}
    for c in sorted(percls):
        e = percls[c]
        sub = {s: {k: n for k, n in chunks[s].items() if k[0] == c} for s in sides}
        row = {"cls": c, "chunk": e["chunk"], "pages": e["pages"], "used": e["used"], "objects": _three_way(sub)}
        if meta_routed is not None and e["chunk"] <= meta_routed + 4 and e["chunk"] >= 12:
            row["note"] = "chunk size within the META-routed payload range (console holds the routed nodes here)"
        slab["classes"].append(row)
    slab["objects_total"] = _three_way(chunks)
    # the reassigned pages: class per page per side
    def page_cls(s, idx):
        p = pools[s]
        return p.page[idx][0] if idx in p.page else None
    npages = pools["con"].npages
    pv = {"pages": {"con==new": 0, "con==old": 0, "con==both": 0, "con-neither": 0, "con-free": 0},
          "words": {"con==new": 0, "con==old": 0, "con==both": 0, "con-neither": 0, "con-free": 0}, "list": []}
    ram_o, ram_n = mems["old"].ram, mems["new"].ram
    pages_base = pools["con"].pages
    for idx in range(npages):
        co, cn, cc = page_cls("old", idx), page_cls("new", idx), page_cls("con", idx)
        if co == cn:
            continue
        lo = pages_base + idx * slab_walk.PAGE_BYTES
        diff = sum(1 for x in range(lo, lo + slab_walk.PAGE_BYTES, 4) if ram_o[x:x + 4] != ram_n[x:x + 4])
        if cc is None:
            k = "con-free"
        elif cc == cn and cc == co:
            k = "con==both"
        elif cc == cn:
            k = "con==new"
        elif cc == co:
            k = "con==old"
        else:
            k = "con-neither"
        pv["pages"][k] += 1
        pv["words"][k] += diff
        if len(pv["list"]) < list_cap:
            pv["list"].append({"page": idx, "old": co, "new": cn, "con": cc, "diff_words": diff, "verdict": k})
    slab["reassigned_pages"] = pv
    res["slab"] = slab
    # ---- records
    rsets = {s: {} for s in sides}
    for s in sides:
        for buf, sz, idx, d, st in recs[s]:
            if st == "USED":
                k = (sz, _fp(mems[s], buf, sz, ar[s], fp_limit))
                rsets[s][k] = rsets[s].get(k, 0) + 1
    rres = {"census": hdrs, "match": _three_way(rsets)}
    # the trees' one-sided records: is each on the console?
    for a, b in (("old", "new"), ("new", "old")):
        lst = []
        for k, n in rsets[a].items():
            extra = n - rsets[b].get(k, 0)
            if extra > 0:
                lst.append({"size": "0x%x" % k[0], "count": extra, "on_console": min(extra, rsets["con"].get(k, 0) - min(rsets["con"].get(k, 0), rsets[b].get(k, 0)))})
        lst.sort(key=lambda e: -int(e["size"], 16))
        rres["%s_only_records" % a] = {"records": sum(e["count"] for e in lst), "on_console": sum(e["on_console"] for e in lst), "list": lst[:list_cap]}
    byid = {}
    for s in sides:
        byid[s] = sorted((sz for buf, sz, idx, d, st in recs[s] if st == "USED"), reverse=True)
    rres["largest_used_sizes"] = {s: ["0x%x" % v for v in byid[s][:8]] for s in sides}
    res["records"] = rres
    # ---- plain words
    wo = {}
    for a, n in words:
        wo["0x%x:%d" % (a, n)] = {s: mems[s].ram[a:a + n].hex(" ") for s in sides}
    res["words"] = wo
    return res


def render(res):
    out = ["console_compare: CON %s (%s) | OLD %s | NEW %s" % (res["images"]["con"]["path"], res["images"]["con"]["kind"],
                                                             res["images"]["old"]["path"], res["images"]["new"]["path"])]
    out.append("fingerprint: %s; pointer arenas (ONE list, every side): %s" % (
        "the WHOLE object" if not res.get("fp_limit") else "the first %d bytes of each object (CAPPED)" % res["fp_limit"],
        " ".join(res.get("arenas", []))))
    out.append("moment:")
    for k, v in res["moment"].items():
        out.append("  %-22s con=%s old=%s new=%s" % (k, v["con"], v["old"], v["new"]))
    if "heap" in res:
        h = res["heap"]
        out.append("heap: used/free  con %d/%d  old %d/%d  new %d/%d;  blocks by content %s" % (
            h["con"]["used"], h["con"]["free"], h["old"]["used"], h["old"]["free"], h["new"]["used"], h["new"]["free"], h["match"]))
    s = res["slab"]
    for side in ("con", "old", "new"):
        c = s["census"][side]
        out.append("slab %s: pages %d (free %d, in use %d), used chunks %d, free chunks %d" % (
            side, c["pages"], c["free_pages"], c["inuse_pages"], c["used_chunks"], c["free_chunks"]))
    out.append("slab: console record descriptors live in %s" % s["console_descriptor_chunks"])
    out.append("  %4s %6s | %5s %5s %5s | %6s %6s %6s | %6s %7s %7s %7s %7s %7s %7s" % (
        "cls", "chunk", "pgC", "pgO", "pgN", "usedC", "usedO", "usedN", "all", "con+old", "con+new", "con-onl", "old-onl", "new-onl", "old+new"))
    for r in s["classes"]:
        o = r["objects"]
        out.append("  %4d %6d | %5d %5d %5d | %6d %6d %6d | %6d %7d %7d %7d %7d %7d %7d%s" % (
            r["cls"], r["chunk"], r["pages"]["con"], r["pages"]["old"], r["pages"]["new"], r["used"]["con"], r["used"]["old"], r["used"]["new"],
            o["all"], o["con+old"], o["con+new"], o["con-only"], o["old-only"], o["new-only"], o["old+new"], "  *" if "note" in r else ""))
    out.append("slab objects total: %s" % s["objects_total"])
    pv = s["reassigned_pages"]
    out.append("slab pages whose class differs OLD vs NEW: pages %s  words(OLD!=NEW on those pages) %s" % (pv["pages"], pv["words"]))
    for e in pv["list"][:12]:
        out.append("    page %4d old %-4s new %-4s con %-4s diff %4d -> %s" % (e["page"], e["old"], e["new"], e["con"], e["diff_words"], e["verdict"]))
    r = res["records"]
    for side in ("con", "old", "new"):
        c = r["census"][side]
        out.append("records %s: %d descriptors (used %d, free %d, pending %d; desc in RAM %d / META %d) used-counter %s" % (
            side, c["count"], c["used"], c["free"], c["pending"], c["desc_in_ram"], c["desc_in_meta"], c["used_cnt_word"]))
    out.append("records by content: %s" % r["match"])
    for a in ("old", "new"):
        e = r["%s_only_records" % a]
        out.append("records %s-only (vs the other tree): %d, of which on the console %d: %s" % (
            a.upper(), e["records"], e["on_console"], ", ".join("%s x%d (con %d)" % (x["size"], x["count"], x["on_console"]) for x in e["list"][:10])))
    out.append("largest used record sizes: %s" % r["largest_used_sizes"])
    if res["words"]:
        out.append("words:")
        for k, v in res["words"].items():
            out.append("  %s" % k)
            for side in ("con", "old", "new"):
                out.append("    %s %s" % (side, v[side]))
    return "\n".join(out)


# ------------------------------------------------------------------ selftest

def _pool_in_ram(ram, pool, base, size, recs, desc_at, used_addr=None):
    """The console's shape: descriptors are RAM chunks (0x20 apart) at `desc_at`; same layout as rc._pool."""
    struct.pack_into("<II", ram, pool, base, size)
    free_prev, nfree = 0, 0
    for i, (buf, sz, idx, free) in enumerate(recs):
        d = desc_at + i * 0x20
        nxt = desc_at + (i + 1) * 0x20 if i + 1 < len(recs) else 0
        prv = desc_at + (i - 1) * 0x20 if i > 0 else 0
        struct.pack_into("<IIII", ram, d, rc.IDX_FREE if free else idx, buf, sz, 0)
        struct.pack_into("<IIII", ram, d + 0x10, 0, prv, nxt, 0x8001 if free else 0x24002)
        if free:
            if free_prev:
                struct.pack_into("<I", ram, free_prev + 0x10, d)
            else:
                struct.pack_into("<I", ram, pool + 0xc, d)
            free_prev = d
            nfree += 1
    struct.pack_into("<I", ram, pool + 0x4c, nfree)
    struct.pack_into("<I", ram, pool + 0x50, desc_at)
    if used_addr is not None:
        struct.pack_into("<I", ram, used_addr, len(recs) - nfree)


def selftest():
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)
    tmp = tempfile.mkdtemp(prefix="console_compare_selftest_")
    SP, NCLS, DESC, CTAB, PAGES, NP = 0x8000, 4, 0x8200, 0x8400, 0x10000, 8
    POOL, PBASE, PSIZE, USED, MOM, HEAP = 0x3000, 0x40000, 0x10000, 0x3100, 0x3104, 0x3200
    rams = {s: bytearray(RAM_BYTES) for s in ("con", "old", "new")}
    metas = {s: bytearray(0x4000) for s in ("old", "new")}

    def fill(ram, lo, size, seed):
        for k in range(0, size, 4):
            struct.pack_into("<I", ram, lo + k, (seed * 0x9e3779b1 + k) & 0xffffffff)
    # slab: OLD page0 cls1(1024) chunk0 used (A), page1 cls2(2048) chunk0 used (B); NEW page0 cls1 chunk0 used (A),
    # page1 cls3(512) chunk0 used (C) = a reassigned page; CON page0 cls1 chunk0 (A), page1 cls3 chunk0 (C) -> con==new,
    # and CON page2 cls0(32) with 2 used chunks = the record descriptors (routed to META in the trees).
    slab_walk._build_pool(rams["old"], SP, NCLS, DESC, CTAB, PAGES, NP, {0: (1, 1024, {1, 2, 3}), 1: (2, 2048, {1})})
    slab_walk._build_pool(rams["new"], SP, NCLS, DESC, CTAB, PAGES, NP, {0: (1, 1024, {1, 2, 3}), 1: (3, 512, set(range(1, 8)))})
    slab_walk._build_pool(rams["con"], SP, NCLS, DESC, CTAB, PAGES, NP, {0: (1, 1024, {1, 2, 3}), 1: (3, 512, set(range(1, 8))),
                                                                       2: (0, 32, set(range(2, 128)))})
    for s in rams:
        fill(rams[s], PAGES, 1024, 11)                 # A everywhere
    fill(rams["old"], PAGES + 4096, 2048, 22)          # B (OLD only)
    fill(rams["new"], PAGES + 4096, 512, 33)           # C
    fill(rams["con"], PAGES + 4096, 512, 33)           # C
    # records: two used (R1 0x100 @+0, R2 0x80 @+0x100) on all; OLD an extra R3 0x200 @+0x200; CON also has R3 -> console holds OLD's extra;
    # R4 0x1100 @+0x400 on all three, equal in its first 4 KB and differing on the console at +0x1080 (the fingerprint-length pin);
    # R1 carries a raw DATA word in the META address range on all three sides (the symmetric-arena pin).
    R4 = (PBASE + 0x400, 0x1100, 3, False)
    recs_common = [(PBASE, 0x100, 0, False), (PBASE + 0x100, 0x80, 1, False)]
    rc._pool(rams["new"], metas["new"], POOL, PBASE, PSIZE, recs_common + [(PBASE + 0x200, 0x200, 2, True), R4], USED)
    rc._pool(rams["old"], metas["old"], POOL, PBASE, PSIZE, recs_common + [(PBASE + 0x200, 0x200, 2, False), R4], USED)
    # the console's four descriptors occupy chunks 0..3 of page 2: the free list holds chunks 4..127
    slab_walk._build_pool(rams["con"], SP, NCLS, DESC, CTAB, PAGES, NP, {0: (1, 1024, {1, 2, 3}), 1: (3, 512, set(range(1, 8))),
                                                                       2: (0, 32, set(range(4, 128)))})
    _pool_in_ram(rams["con"], POOL, PBASE, PSIZE, recs_common + [(PBASE + 0x200, 0x200, 2, False), R4], PAGES + 2 * 4096, USED)
    for s in rams:
        fill(rams[s], PBASE, 0x100, 44)
        fill(rams[s], PBASE + 0x100, 0x80, 55)
        fill(rams[s], R4[0], R4[1], 99)
        struct.pack_into("<I", rams[s], PBASE + 0x10, META_BASE + 0x101)     # a data word that LOOKS like a META address (inside the synthetic META span)
    struct.pack_into("<I", rams["con"], R4[0] + 0x1080, 0xdeadbeef)         # the console's R4 differs beyond 4 KB only
    fill(rams["old"], PBASE + 0x200, 0x200, 66)
    fill(rams["con"], PBASE + 0x200, 0x200, 66)
    # heap: OLD one extra used block
    rc._build(rams["old"], HEAP, [(0x40, False), (0x40, False), (0x20, True)])
    rc._build(rams["new"], HEAP, [(0x40, False), (0x20, True)])
    rc._build(rams["con"], HEAP, [(0x40, False), (0x20, True)])
    for s in rams:
        fill(rams[s], 0x1000 + 16, 0x40, 77)
    fill(rams["old"], 0x1000 + 16 + 0x40 + 16, 0x40, 88)
    struct.pack_into("<I", rams["con"], MOM, 0x159)
    struct.pack_into("<I", rams["old"], MOM, 0x159)
    struct.pack_into("<I", rams["new"], MOM, 0x159)
    rams["con"][0x5000] = 0xbe
    rams["old"][0x5000] = 0xbf
    rams["new"][0x5000] = 0x4f
    pc = os.path.join(tmp, "con_ee.bin")
    with open(pc, "wb") as f:
        f.write(bytes(rams["con"]))
    po, pn = os.path.join(tmp, "o.ps2st"), os.path.join(tmp, "n.ps2st")
    rc._synthetic(po, bytes(rams["old"]), bytes(metas["old"]), 120)
    rc._synthetic(pn, bytes(rams["new"]), bytes(metas["new"]), 120)
    mems = {"con": Mem(pc), "old": Mem(po), "new": Mem(pn)}
    check(mems["con"].kind == "ee.bin" and mems["old"].kind == "ps2st", "a raw EE image and a .ps2st both open through Mem")
    res = compare(mems, SP, NCLS, POOL, USED, heap=HEAP, moment=(MOM,), words=((0x5000, 1),), meta_routed=0x20)
    check(res["moment"]["0x%x" % MOM] == {"con": "0x159", "old": "0x159", "new": "0x159"}, "the moment word on three sides")
    rcen = res["records"]["census"]
    check(rcen["con"]["desc_in_ram"] == 4 and rcen["old"]["desc_in_meta"] == 4 and rcen["new"]["desc_in_meta"] == 4,
          "descriptors resolved in RAM on the console, in META in the trees -> %d / %d / %d" % (rcen["con"]["desc_in_ram"], rcen["old"]["desc_in_meta"], rcen["new"]["desc_in_meta"]))
    check(rcen["con"]["used"] == 4 and rcen["old"]["used"] == 4 and rcen["new"]["used"] == 3, "used records 4 / 4 / 3 (the live counter checked)")
    m = res["records"]["match"]
    check(m["all"] == 2 and m["con+old"] == 1 and m["new-only"] == 0 and m["con-only"] == 1 and m["old+new"] == 1,
          "record content (whole object): R1+R2 on all, OLD's extra R3 IS on the console, R4 (differs beyond 4 KB) con-only / old+new -> %s" % m)
    arl = res["arenas"]
    span = meta_span(mems)
    check(span == len(metas["old"]) and mems["con"].meta == b"",
          "the META span is derived from the images (the trees' META section length 0x%x; the raw image carries none)" % span)
    check(res["fp_limit"] == 0 and "0x%x-0x%x" % (META_BASE, META_BASE + span) in arl and len(set(arl)) == len(arl),
          "ONE arena list on every side, the META range in it, the fingerprint uncapped -> %s" % arl)
    fp_raw = rc.fingerprint(rams["con"], PBASE, 0x100, [], limit=0x100)
    fp_msk = rc.fingerprint(rams["con"], PBASE, 0x100, [(META_BASE, META_BASE + span)], limit=0x100)
    check(fp_raw != fp_msk, "R1's META-range DATA word tokenises differently masked vs raw (the asymmetry would have split it)")
    rcap = compare(mems, SP, NCLS, POOL, USED, heap=HEAP, moment=(MOM,), meta_routed=0x20, fp_limit=4096)
    mc = rcap["records"]["match"]
    check(mc["all"] == 3 and mc["con-only"] == 0 and mc["old+new"] == 0 and "CAPPED" in render(rcap),
          "a 4 KB cap makes R4 compare EQUAL on all three and the output says CAPPED -> %s" % mc)
    oo = res["records"]["old_only_records"]
    check(oo["records"] == 1 and oo["on_console"] == 1 and oo["list"][0]["size"] == "0x200", "OLD-only record 0x200 x1, on console 1 -> %s" % oo["list"])
    check(res["records"]["new_only_records"]["records"] == 0, "no NEW-only record")
    check(res["slab"]["console_descriptor_chunks"] == {"cls 0/32B": 4}, "the console's descriptors are slab class 0/32B chunks -> %s" % res["slab"]["console_descriptor_chunks"])
    pv = res["slab"]["reassigned_pages"]
    check(pv["pages"] == {"con==new": 1, "con==old": 0, "con==both": 0, "con-neither": 0, "con-free": 0} and pv["words"]["con==new"] > 0,
          "page 1 (OLD cls 2 / NEW cls 3) is NEW's class on the console, weighted by its differing words -> %s" % pv)
    cls_rows = {r["cls"]: r for r in res["slab"]["classes"]}
    check(cls_rows[1]["objects"]["all"] == 1 and cls_rows[2]["objects"]["old-only"] == 1 and cls_rows[3]["objects"]["con+new"] == 1,
          "slab objects by content: A on all, B OLD-only, C con+new")
    check(cls_rows[0]["objects"]["con-only"] == 4 and "note" in cls_rows[0], "the console's routed-node class is flagged (4 con-only chunks, META-routed note)")
    h = res["heap"]
    check(h["old"]["used"] == 2 and h["new"]["used"] == 1 and h["con"]["used"] == 1 and h["match"]["all"] == 1 and h["match"]["old-only"] == 1,
          "heap: OLD's extra block is on neither console nor NEW -> %s" % h["match"])
    check(res["words"]["0x5000:1"] == {"con": "be", "old": "bf", "new": "4f"}, "plain words on three sides")
    txt = render(res)
    check("records by content" in txt and "con==new" in txt, "render carries the record match and the page verdict")
    # a broken live counter fails the walk
    bad = bytearray(rams["con"])
    struct.pack_into("<I", bad, USED, 7)
    pb = os.path.join(tmp, "bad_ee.bin")
    with open(pb, "wb") as f:
        f.write(bytes(bad))
    try:
        walk_records(Mem(pb), POOL, USED)
        check(False, "a live counter that disagrees with the walk must fail")
    except ValueError as e:
        check("live counter" in str(e), "live counter vs used descriptors is checked -> %s" % e)
    try:
        Mem(os.path.join(tmp, "short.bin")) if open(os.path.join(tmp, "short.bin"), "wb").write(b"x" * 16) else None
        check(False, "a raw image that is not 32 MB must be refused")
    except ValueError as e:
        check("32" in str(e) or "raw EE image" in str(e), "a short raw image is refused -> %s" % e)
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)
    print("console_compare selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def _parse_words(items):
    out = []
    for s in items:
        if ":" in s:
            a, n = s.split(":", 1)
            out.append((int(a, 16), int(n, 0)))
        else:
            out.append((int(s, 16), 4))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("console", nargs="?")
    ap.add_argument("old", nargs="?")
    ap.add_argument("new", nargs="?")
    ap.add_argument("--slabpool", help="page-pool header (title data)")
    ap.add_argument("--classes", type=int, help="page-pool size classes (title data)")
    ap.add_argument("--pool", help="record-pool header (title data)")
    ap.add_argument("--pool-used", help="the pool's live-record counter (title data)")
    ap.add_argument("--heap", help="title heap header (title data)")
    ap.add_argument("--moment", default="", help="comma-separated words to print on the three sides (e.g. the record counter)")
    ap.add_argument("--bootsm", help="the boot state-machine u64 whose bits 44..49 are the case")
    ap.add_argument("--word", action="append", default=[], help="addr[:bytes] plain word / byte range to print (repeatable)")
    ap.add_argument("--arena", action="append", default=[], help="lo-hi extra pointer arena for the fingerprints (repeatable)")
    ap.add_argument("--meta-routed", help="payload cap of the META-routed nodes (flags the console's slab class that holds them)")
    ap.add_argument("--fp-limit", type=int, default=0,
                    help="bytes of each object hashed into its content fingerprint; 0 (default) = the whole object; a cap is stated in the output")
    ap.add_argument("--out")
    ap.add_argument("--list", type=int, default=40)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not (a.console and a.old and a.new and a.slabpool and a.classes and a.pool):
        ap.error("console, old, new, --slabpool, --classes and --pool are required")
    mems = {"con": Mem(a.console), "old": Mem(a.old), "new": Mem(a.new)}
    arenas = [tuple(int(x, 16) for x in s.split("-", 1)) for s in a.arena]
    moment = [int(x, 16) for x in a.moment.split(",") if x]
    res = compare(mems, int(a.slabpool, 16), a.classes, int(a.pool, 16), int(a.pool_used, 16) if a.pool_used else None,
                  int(a.heap, 16) if a.heap else None, moment, int(a.bootsm, 16) if a.bootsm else None, _parse_words(a.word), arenas,
                  int(a.meta_routed, 16) if a.meta_routed else None, a.list, a.fp_limit)
    print(render(res))
    if a.out:
        with open(a.out, "w") as f:
            json.dump(res, f, indent=1)
        print("json: %s" % a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
