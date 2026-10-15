#!/usr/bin/env python3
"""slab_walk.py -- the title's PAGE POOL (slab allocator) walked in two .ps2st images; every differing word of
the pool's header, page descriptors, class table and pages classed structurally.

  slab_walk.py <old.ps2st> <new.ps2st> --pool <page-pool header> --classes <n> [--arena lo-hi ...]
               [--mask verify/mask/coldboot_elf_text.ranges] [--out slab.json] [--list N]
  slab_walk.py --selftest

The page pool (read from the IMAGES; the layout is the title's allocator's, learned from its own code --
its init, free and alloc bodies and its two list primitives: singly-linked free stacks (+0xc head, +0x14
count) and doubly-linked class lists (node +0x18 prev, +0x1c next); the function names are title data and
live in `verify/attribution/classes.tsv` / the DECISIONS_LOG LEDGER section):

  header P:  +0x4 descriptor array, +0x8 u16 class-per-page table, +0xc pages base;
             +0x10..+0x2c the free-PAGE stack over the descriptor array (+0x10 base, +0x14 bytes, +0x18 32,
             +0x1c head, +0x20 initial count, +0x24 free count); +0x30 + cls*4 the class list heads.
  descriptor (32 bytes, one per 4 KB page): +0 page address (or, on the free-page stack, the next free
             descriptor), +4 page bytes (4096), +8 chunk size, +0xc free-chunk head (chunk +0 = next),
             +0x10 chunks per page, +0x14 free chunks, +0x18/+0x1c class-list prev/next.

The walk asserts the allocator's own invariants on each image: free-stack reachable == +0x24; every page is
on the free stack or on exactly one class list (free + in-use == pages); the class table names the page's
list; +0 == pages + idx*4096 and +4 == 4096 for an in-use page; the free-chunk list length == +0x14.

Word classes (a word at the same address differing between the images, outside the mask):
  slab-header        the pool header (heads/counts: the allocation sequence's consequence)
  slab-class-table   the u16 class table (a page's class, or free vs in use, differs)
  slab-desc-free     a descriptor word of a page FREE on both sides (stale fields of a returned page)
  slab-desc-state    a descriptor word of a page in use on either side (counts/links/heads)
  slab-free-link     not used on either side, and the FIRST word of a free chunk on either side: the page's
                     free-chunk list link, allocator STATE that the next alloc of the class pops (never
                     non-state; a link under the other side's USED object is slab-alloc-differs)
  slab-free-residue  the word lies in a free page or in the BODY of a free chunk on BOTH sides (stale bytes
                     no list, header or descriptor refers to)
  slab-alloc-differs free on one side, used on the other (an object exists on one side only), or the two
                     pages carry different chunk classes
  slab-reloc-equal   both sides' chunks are used and EACH side's chunk content (pointer-insensitive
                     fingerprint, same class) exists as a used chunk of the other image: the same object
                     at another place
  slab-reloc-partial both used, ONE side's chunk content exists elsewhere on the other side (the other
                     side's object has no counterpart: the slot holds different objects)
  slab-genuine       both used, neither side's chunk content has a counterpart
  other              outside the pool's ranges (left to attribute_probe.py / relocate_compare.py)

Title data (the header address, the class count) is passed in -- `verify/attribution/classes.tsv` carries
them as `@slabpool <addr> <classes>` for attribute_probe.py. The reloc-equal / reloc-partial split depends on
the pointer arenas given to the chunk fingerprints (`--arena`; attribute_probe.py passes the TSV's `@heap` /
`@pool` arenas): standalone without them a pointer into the heap or record pool is fingerprinted by value and
a moved object's chunk may miss its counterpart -- quote the attribute_probe figures. Sets are built once per
image; files only.
"""
import argparse
import hashlib
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import relocate_compare as rc  # noqa: E402

DESC_BYTES = 32
PAGE_BYTES = 4096
CLASSES = ("slab-header", "slab-class-table", "slab-desc-free", "slab-desc-state", "slab-free-link", "slab-free-residue",
           "slab-alloc-differs", "slab-reloc-equal", "slab-reloc-partial", "slab-genuine", "other")


class Pool:
    __slots__ = ("P", "ncls", "desc", "ctab", "pages", "npages", "free_pages", "inuse", "page", "header_hi")

    def __init__(self, img, P, ncls):
        w = img.w
        self.P, self.ncls = P, ncls
        self.desc, self.ctab, self.pages = w(P + 4), w(P + 8), w(P + 0xc)
        dbase, dtotal, dsz = w(P + 0x10), w(P + 0x14), w(P + 0x18)
        if dbase != self.desc or dsz != DESC_BYTES or dtotal % DESC_BYTES:
            raise ValueError("slab walk: free-page stack base/size 0x%x/0x%x/%d does not describe the descriptor array 0x%x"
                             % (dbase, dtotal, dsz, self.desc))
        self.npages = dtotal // DESC_BYTES
        self.header_hi = P + 0x30 + ncls * 4
        # the free-page stack
        free = set()
        d = w(P + 0x1c)
        while d:
            if d in free or not (self.desc <= d < self.desc + dtotal) or (d - self.desc) % DESC_BYTES:
                raise ValueError("slab walk: free-page stack link 0x%x is not a fresh descriptor" % d)
            free.add(d)
            d = w(d)
        if len(free) != w(P + 0x24):
            raise ValueError("slab walk: free-page stack reaches %d descriptors, header count %d" % (len(free), w(P + 0x24)))
        self.free_pages = free
        # the class lists
        inuse = {}
        for c in range(ncls):
            d = w(P + 0x30 + c * 4)
            guard = 0
            while d:
                if d in inuse or d in free:
                    raise ValueError("slab walk: descriptor 0x%x on two lists (class %d)" % (d, c))
                if not (self.desc <= d < self.desc + dtotal) or (d - self.desc) % DESC_BYTES:
                    raise ValueError("slab walk: class %d list link 0x%x is not a descriptor" % (c, d))
                inuse[d] = c
                d = w(d + 0x1c)
                guard += 1
                if guard > self.npages:
                    raise ValueError("slab walk: class %d list runaway" % c)
        if len(free) + len(inuse) != self.npages:
            raise ValueError("slab walk: free %d + in-use %d != pages %d" % (len(free), len(inuse), self.npages))
        self.inuse = inuse
        # per in-use page: (cls, chunk, cap, free_cnt, free_chunks)
        self.page = {}
        for d, c in inuse.items():
            idx = (d - self.desc) // DESC_BYTES
            ct = struct.unpack_from("<H", img.ram, self.ctab + idx * 2)[0]
            if ct != c:
                raise ValueError("slab walk: class table says %d, list says %d for page %d" % (ct, c, idx))
            pa, pb, chunk, fhead, cap, fcnt = (w(d + o) for o in range(0, 0x18, 4))
            if pa != self.pages + idx * PAGE_BYTES or pb != PAGE_BYTES:
                raise ValueError("slab walk: page %d descriptor names 0x%x/%d, expected 0x%x/%d" % (idx, pa, pb, self.pages + idx * PAGE_BYTES, PAGE_BYTES))
            if chunk == 0 or cap != PAGE_BYTES // chunk:
                raise ValueError("slab walk: page %d chunk %d capacity %d" % (idx, chunk, cap))
            fc = set()
            f = fhead
            while f:
                if f in fc or not (pa <= f < pa + PAGE_BYTES) or (f - pa) % chunk:
                    raise ValueError("slab walk: page %d free-chunk link 0x%x is not a chunk of the page" % (idx, f))
                fc.add(f)
                f = w(f)
            if len(fc) != fcnt:
                raise ValueError("slab walk: page %d free-chunk list %d != descriptor count %d" % (idx, len(fc), fcnt))
            self.page[idx] = (c, chunk, cap, fcnt, fc)

    # ---- queries
    def page_of(self, a):
        if self.pages <= a < self.pages + self.npages * PAGE_BYTES:
            return (a - self.pages) // PAGE_BYTES
        return None

    def status(self, a):
        """('FREE-PAGE' | 'FREE-CHUNK' | 'USED', cls, chunk_lo, chunk_size) for a word in the pages."""
        idx = self.page_of(a)
        if idx is None:
            return None
        if idx not in self.page:
            return ("FREE-PAGE", None, None, None)
        c, chunk, cap, fcnt, fc = self.page[idx]
        lo = self.pages + idx * PAGE_BYTES + ((a - self.pages - idx * PAGE_BYTES) // chunk) * chunk
        return ("FREE-CHUNK" if lo in fc else "USED", c, lo, chunk)

    def used_chunks(self):
        for idx, (c, chunk, cap, fcnt, fc) in self.page.items():
            base = self.pages + idx * PAGE_BYTES
            for k in range(cap):
                lo = base + k * chunk
                if lo not in fc:
                    yield c, lo, chunk

    def ranges(self):
        return [(self.P, self.header_hi), (self.desc, self.desc + self.npages * DESC_BYTES),
                (self.ctab, self.ctab + self.npages * 2), (self.pages, self.pages + self.npages * PAGE_BYTES)]

    def census(self):
        used = sum(cap - fcnt for c, chunk, cap, fcnt, fc in self.page.values())
        free = sum(fcnt for c, chunk, cap, fcnt, fc in self.page.values())
        return {"pages": self.npages, "free_pages": len(self.free_pages), "inuse_pages": len(self.inuse),
                "used_chunks": used, "free_chunks": free,
                "chunk_sizes": sorted({chunk for c, chunk, cap, fcnt, fc in self.page.values()})}


def _fp(ram, lo, size, arenas):
    return rc.fingerprint(ram, lo, size, arenas, limit=4096)


def compare(old_img, new_img, P, ncls, mask_rows, arenas=(), list_cap=100, collect=None):
    po, pn = Pool(old_img, P, ncls), Pool(new_img, P, ncls)
    ar_o = list(arenas) + [(po.pages, po.pages + po.npages * PAGE_BYTES)]
    ar_n = list(arenas) + [(pn.pages, pn.pages + pn.npages * PAGE_BYTES)]
    # fingerprints of used chunks per side, keyed (cls, fp) -> count (built ONCE)
    fo, fn = {}, {}
    for c, lo, size in po.used_chunks():
        fo[(c, _fp(old_img.ram, lo, size, ar_o))] = fo.get((c, _fp(old_img.ram, lo, size, ar_o)), 0) + 1
    for c, lo, size in pn.used_chunks():
        fn[(c, _fp(new_img.ram, lo, size, ar_n))] = fn.get((c, _fp(new_img.ram, lo, size, ar_n)), 0) + 1
    counts = {k: 0 for k in CLASSES}
    counts["masked"] = 0
    genuine, differs = [], []
    chunk_fp_cache_o, chunk_fp_cache_n = {}, {}
    ram_o, ram_n = old_img.ram, new_img.ram
    rngs = sorted(set(po.ranges()) | set(pn.ranges()))
    for lo, hi in rngs:
        n = min(hi, len(ram_o), len(ram_n))
        for x in range(lo, n, 4):
            vo = struct.unpack_from("<I", ram_o, x)[0]
            vn = struct.unpack_from("<I", ram_n, x)[0]
            if vo == vn:
                continue
            if any(a <= x < b for a, b, _ in mask_rows):
                counts["masked"] += 1
                continue
            cls = _classify(x, po, pn, ram_o, ram_n, fo, fn, ar_o, ar_n, chunk_fp_cache_o, chunk_fp_cache_n)
            counts[cls] += 1
            if collect is not None:
                collect[x] = cls
            if cls == "slab-genuine" and len(genuine) < list_cap:
                so, sn = po.status(x), pn.status(x)
                genuine.append({"addr": "0x%x" % x, "old": "0x%x" % vo, "new": "0x%x" % vn, "class": so[1], "chunk": "0x%x+%d" % (so[2], so[3])})
            elif cls == "slab-alloc-differs" and len(differs) < list_cap:
                so, sn = po.status(x), pn.status(x)
                differs.append({"addr": "0x%x" % x, "old": so[0] if so else "?", "new": sn[0] if sn else "?"})
    return {"old": old_img.path, "new": new_img.path, "pool": {"header": "0x%x" % P, "classes": ncls, "old": po.census(), "new": pn.census()},
            "words": counts, "genuine": genuine, "alloc_differs": differs}


def _classify(x, po, pn, ram_o, ram_n, fo, fn, ar_o, ar_n, co, cn):
    if po.P <= x < po.header_hi:
        return "slab-header"
    if po.ctab <= x < po.ctab + po.npages * 2:
        return "slab-class-table"
    if po.desc <= x < po.desc + po.npages * DESC_BYTES:
        d = po.desc + ((x - po.desc) // DESC_BYTES) * DESC_BYTES
        return "slab-desc-free" if (d in po.free_pages and d in pn.free_pages) else "slab-desc-state"
    so, sn = po.status(x), pn.status(x)
    if so is None or sn is None:
        return "other"
    if so[0] != "USED" and sn[0] != "USED":
        if (so[0] == "FREE-CHUNK" and so[2] == x) or (sn[0] == "FREE-CHUNK" and sn[2] == x):
            return "slab-free-link"
        return "slab-free-residue"
    if so[0] != "USED" or sn[0] != "USED" or so[1] != sn[1]:
        return "slab-alloc-differs"
    # both used, same class: does each side's chunk content exist on the other side?
    ko = co.get(so[2])
    if ko is None:
        ko = co[so[2]] = (so[1], _fp(ram_o, so[2], so[3], ar_o))
    kn = cn.get(sn[2])
    if kn is None:
        kn = cn[sn[2]] = (sn[1], _fp(ram_n, sn[2], sn[3], ar_n))
    if ko in fn and kn in fo:
        return "slab-reloc-equal"
    if ko in fn or kn in fo:
        return "slab-reloc-partial"
    return "slab-genuine"


def render(res):
    out = ["slab_walk: OLD %s vs NEW %s (pool %s, %d classes)" % (res["old"], res["new"], res["pool"]["header"], res["pool"]["classes"])]
    for side in ("old", "new"):
        c = res["pool"][side]
        out.append("  %s: %d pages (free %d, in use %d), used chunks %d, free chunks %d, chunk sizes %s"
                   % (side.upper(), c["pages"], c["free_pages"], c["inuse_pages"], c["used_chunks"], c["free_chunks"],
                      ",".join(str(s) for s in c["chunk_sizes"])))
    w = res["words"]
    total = sum(w[k] for k in CLASSES)
    out.append("  differing words in the pool's ranges outside the mask: %d (masked %d)" % (total, w["masked"]))
    for k in CLASSES:
        out.append("    %-20s %8d  (%5.1f%%)" % (k, w[k], 100.0 * w[k] / total if total else 0.0))
    for g in res["genuine"][:10]:
        out.append("    genuine %s old=%s new=%s class %s chunk %s" % (g["addr"], g["old"], g["new"], g["class"], g["chunk"]))
    for g in res["alloc_differs"][:6]:
        out.append("    alloc-differs %s OLD %s NEW %s" % (g["addr"], g["old"], g["new"]))
    return "\n".join(out)


# ------------------------------------------------------------------ selftest

def _build_pool(ram, P, ncls, desc, ctab, pages, npages, pages_spec):
    """pages_spec: {idx: (cls, chunk, free_chunk_indices)}; the rest are free pages (stack in idx order)."""
    struct.pack_into("<IIII", ram, P + 4, desc, ctab, pages, desc)
    struct.pack_into("<III", ram, P + 0x14, npages * DESC_BYTES, DESC_BYTES, 0)
    struct.pack_into("<II", ram, P + 0x20, npages, 0)
    free_head, nfree = 0, 0
    heads = {}
    for idx in range(npages - 1, -1, -1):
        d = desc + idx * DESC_BYTES
        if idx in pages_spec:
            c, chunk, free_idx = pages_spec[idx]
            pa = pages + idx * PAGE_BYTES
            cap = PAGE_BYTES // chunk
            fh = 0
            for k in sorted(free_idx, reverse=True):
                struct.pack_into("<I", ram, pa + k * chunk, fh)
                fh = pa + k * chunk
            struct.pack_into("<IIIIII", ram, d, pa, PAGE_BYTES, chunk, fh, cap, len(free_idx))
            struct.pack_into("<H", ram, ctab + idx * 2, c)
            prev_head = heads.get(c, 0)
            struct.pack_into("<II", ram, d + 0x18, 0, prev_head)
            if prev_head:
                struct.pack_into("<I", ram, prev_head + 0x18, d)
            heads[c] = d
        else:
            struct.pack_into("<I", ram, d, free_head)
            free_head = d
            nfree += 1
    struct.pack_into("<I", ram, P + 0x1c, free_head)
    struct.pack_into("<I", ram, P + 0x24, nfree)
    for c, d in heads.items():
        struct.pack_into("<I", ram, P + 0x30 + c * 4, d)


def selftest():
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)
    tmp = tempfile.mkdtemp(prefix="slab_selftest_")
    P, NCLS, DESC, CTAB, PAGES, NP = 0x1000, 4, 0x2000, 0x2100, 0x10000, 6
    ram_o, ram_n = bytearray(0x20000), bytearray(0x20000)

    def fill(ram, lo, size, seed):
        for k in range(0, size, 4):
            struct.pack_into("<I", ram, lo + k, (seed * 0x9e3779b1 + k) & 0xffffffff)
    # OLD: page 0 class 1 (chunk 1024): chunks 0,1 used, 2,3 free; page 1 class 2 (chunk 2048): chunk 0 used, 1 free;
    #      page 2 class 1: chunk 0 used (content S), rest free. Pages 3-5 free.
    # NEW: page 0 class 1: chunks 0,1 used (chunk 1 moved content: NEW's chunk 1 == OLD's page-2 chunk 0 (S); OLD's
    #      page-0 chunk 1 content (T) sits in NEW's page 2 chunk 0); page 1 class 2 chunk 0 used with ONE word changed
    #      (genuine); page 2 class 1 chunk 0 used (T); page 3 FREE in OLD, class 1 in NEW with chunk 0 used (alloc-differs)
    _build_pool(ram_o, P, NCLS, DESC, CTAB, PAGES, NP, {0: (1, 1024, {2, 3}), 1: (2, 2048, {1}), 2: (1, 1024, {1, 2, 3})})
    _build_pool(ram_n, P, NCLS, DESC, CTAB, PAGES, NP, {0: (1, 1024, {2, 3}), 1: (2, 2048, {1}), 2: (1, 1024, {1, 2, 3}), 3: (1, 1024, {1, 2, 3})})
    fill(ram_o, PAGES + 0 * PAGE_BYTES + 0, 1024, 11); fill(ram_n, PAGES + 0 * PAGE_BYTES + 0, 1024, 11)   # equal
    fill(ram_o, PAGES + 0 * PAGE_BYTES + 1024, 1024, 22)                                                   # T in OLD page 0
    fill(ram_n, PAGES + 2 * PAGE_BYTES + 0, 1024, 22)                                                      # T in NEW page 2
    fill(ram_o, PAGES + 2 * PAGE_BYTES + 0, 1024, 33)                                                      # S in OLD page 2
    fill(ram_n, PAGES + 0 * PAGE_BYTES + 1024, 1024, 33)                                                   # S in NEW page 0
    fill(ram_o, PAGES + 1 * PAGE_BYTES, 2048, 44); fill(ram_n, PAGES + 1 * PAGE_BYTES, 2048, 44)
    struct.pack_into("<I", ram_n, PAGES + 1 * PAGE_BYTES + 0x40, 0xdeadbeef)                              # genuine
    fill(ram_n, PAGES + 3 * PAGE_BYTES, 1024, 55)                                                          # NEW-only object
    struct.pack_into("<I", ram_o, PAGES + 0 * PAGE_BYTES + 3 * 1024 + 8, 0x11)                             # free chunk residue
    struct.pack_into("<I", ram_n, PAGES + 0 * PAGE_BYTES + 3 * 1024 + 8, 0x22)
    struct.pack_into("<I", ram_o, PAGES + 5 * PAGE_BYTES + 0x100, 0x5a)                                    # free page residue
    po, pn = os.path.join(tmp, "o.ps2st"), os.path.join(tmp, "n.ps2st")
    rc._synthetic(po, bytes(ram_o), bytes(0x100), 2)
    rc._synthetic(pn, bytes(ram_n), bytes(0x100), 2)
    io, in_ = rc.Image(po), rc.Image(pn)
    res = compare(io, in_, P, NCLS, [], list_cap=10)
    w, c = res["words"], res["pool"]
    check(c["old"]["free_pages"] == 3 and c["new"]["free_pages"] == 2 and c["old"]["used_chunks"] == 4 and c["new"]["used_chunks"] == 5,
          "walk meets the headers: OLD free pages 3 / used chunks 4, NEW 2 / 5 -> %r / %r" % (c["old"], c["new"]))
    check(w["slab-reloc-equal"] == 512, "the swapped S/T chunks (2 x 256 words) are reloc-equal -> %d" % w["slab-reloc-equal"])
    check(w["slab-genuine"] == 1 and res["genuine"][0]["addr"] == "0x%x" % (PAGES + PAGE_BYTES + 0x40), "one genuine word in the class-2 chunk -> %r" % res["genuine"])
    check(w["slab-alloc-differs"] == 256, "NEW-only object on OLD's free page 3 -> 256 alloc-differs words -> %d" % w["slab-alloc-differs"])
    check(w["slab-free-residue"] == 2, "free chunk BODY + free page stale words -> 2 free-residue -> %d" % w["slab-free-residue"])
    check(w["slab-free-link"] == 2, "NEW page 3's two free-chunk LINK words (chunk +0) over OLD's free page are slab-free-link, not residue -> %d" % w["slab-free-link"])
    struct.pack_into("<I", ram_o, PAGES + 0 * PAGE_BYTES + 2 * 1024, 0x12345678)   # OLD's free chunk 2 link overwritten: differs, chunk+0 on a free chunk
    rc._synthetic(po, bytes(ram_o), bytes(0x100), 2)
    try:
        compare(rc.Image(po), in_, P, NCLS, [], list_cap=10)
        check(False, "a broken free-chunk link must fail the walk's invariants")
    except ValueError as e:
        check("free-chunk link" in str(e), "a free-chunk link that leaves the page fails the walk -> %s" % e)
    struct.pack_into("<I", ram_o, PAGES + 0 * PAGE_BYTES + 2 * 1024, PAGES + 3 * 1024)  # restore the link
    rc._synthetic(po, bytes(ram_o), bytes(0x100), 2)
    check(w["slab-reloc-partial"] == 0, "no partial pair in this layout -> %d" % w["slab-reloc-partial"])
    check(w["slab-class-table"] == 1 and w["slab-header"] >= 2, "class table word of page 3 and the header (free head/count, class-1 head) differ -> %d / %d" % (w["slab-class-table"], w["slab-header"]))
    check(w["slab-desc-state"] >= 1 and w["slab-desc-free"] == 0, "page 3's descriptor (free -> in use) is desc-state -> %d" % w["slab-desc-state"])
    check("other" not in {k for k, v in w.items() if v and k == "other"}, "no word outside the pool's ranges")
    txt = render(res)
    check("slab-reloc-equal" in txt and "alloc-differs" in txt, "render lists the classes and the alloc-differs words")
    # invariant violations are errors
    bad = bytearray(ram_o)
    struct.pack_into("<I", bad, P + 0x24, 2)
    pb = os.path.join(tmp, "bad.ps2st")
    rc._synthetic(pb, bytes(bad), bytes(0x100), 2)
    try:
        Pool(rc.Image(pb), P, NCLS)
        check(False, "a free-stack/header count mismatch must fail the walk")
    except ValueError as e:
        check("header count" in str(e), "free-stack vs header count is checked -> %s" % e)
    bad2 = bytearray(ram_o)
    struct.pack_into("<I", bad2, DESC + 0 * DESC_BYTES + 0x14, 1)
    pb2 = os.path.join(tmp, "bad2.ps2st")
    rc._synthetic(pb2, bytes(bad2), bytes(0x100), 2)
    try:
        Pool(rc.Image(pb2), P, NCLS)
        check(False, "a free-chunk list / count mismatch must fail the walk")
    except ValueError as e:
        check("free-chunk list" in str(e), "free-chunk list vs descriptor count is checked -> %s" % e)
    same = compare(io, io, P, NCLS, [])
    check(sum(same["words"][k] for k in CLASSES) == 0, "an image against itself: 0 differing words")
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)
    print("slab_walk selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("old", nargs="?")
    ap.add_argument("new", nargs="?")
    ap.add_argument("--pool", help="page-pool header address (title data)")
    ap.add_argument("--classes", type=int, help="number of size classes (title data)")
    ap.add_argument("--arena", action="append", default=[], help="lo-hi pointer arena for the fingerprints (repeatable)")
    ap.add_argument("--mask", default=os.path.join(HERE, "mask", "coldboot_elf_text.ranges"))
    ap.add_argument("--out")
    ap.add_argument("--list", type=int, default=100)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not (a.old and a.new and a.pool and a.classes):
        ap.error("old, new, --pool and --classes are required")
    arenas = [tuple(int(x, 16) for x in s.split("-", 1)) for s in a.arena]
    res = compare(rc.Image(a.old), rc.Image(a.new), int(a.pool, 16), a.classes, rc.read_mask_ranges(a.mask), arenas, a.list)
    print(render(res))
    if a.out:
        with open(a.out, "w") as f:
            json.dump(res, f, indent=1)
        print("json: %s" % a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
