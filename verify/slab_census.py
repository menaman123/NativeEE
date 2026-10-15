#!/usr/bin/env python3
"""slab_census.py -- the PAGE POOL's differing objects of one probe pair joined with NEW's allocation census: WHEN
(EE boundary) and by WHICH allocator site every differing chunk was allocated or freed on NEW's side.

  slab_census.py <old.ps2st> <new.ps2st> --watch <watch.log> --pool <hdr> --classes <n> --window <boundary>
                 --alloc-site <regex> --free-site <regex> --page-init-site <regex> [--census-from <boundary>]
                 [--mask verify/mask/coldboot_elf_text.ranges] [--arena lo-hi ...] [--out census.json] [--list N]
  slab_census.py --selftest

The census is a `--watch-word` leg (engine/core/diag/word_watch: CPU stores only) armed on every page descriptor's
free-chunk head (`desc + 0xc`; one 32-byte descriptor per 4 KB page) plus the pool header's free-page head/count and
class-list heads: a store to a descriptor's head is the allocator's list primitive -- a POP (chunk alloc: the head
moves from the popped chunk to its link), a PUSH (chunk free: the head moves to the freed chunk) or the page's free
list being BUILT (page init: a fresh page enters a class). Which primitive wrote the word is told by the recorded
site (`FUN_xxxxxxxx+off`, the regexes are title data passed in -- the allocator's own functions; the census records
no caller above them), the chunk by the old/new values. Every event is (boundary, op, page, chunk, site). The page-init
site is the pool's INIT routine building a fresh page's free-chunk list: its store reads `0x0 -> <first chunk>` on a
page that held no list (`init_from_zero`), or `<old head> -> <chunk>` when it re-threads a page. A descriptor store
whose site matches NONE of the three regexes is counted as `other` AND REPORTED (`unmatched_sites`, by site and
boundary, in the JSON and the text) -- an unmatched allocator site is a census gap, never silent.
For every page-reassigned page the TRIGGER is named too: the first POP on that page at or after its init (the
allocation that asked for a page of the new class) -- the requester is the chunk's size class (no caller recorded).
`--census-from B` says the census recorded stores from boundary B only (coldboot's `--watch-record-from 90`): the
classes `slab-old-only-unobserved` / `slab-new-only-inherited` then mean "no NEW event in [B, window]" -- an event
before B is NOT excluded; the JSON carries `census_from` and the class notes say so.

For every differing word of the pool's pages (slab_walk.py's classes, both images walked against the allocator's own
invariants) the chunk's NEW-side history inside the window (events with boundary <= --window) decides the census class:
  slab-new-progress        NEW USED / OLD free: NEW allocated the chunk in the window (the boundary + site named)
  slab-new-only-inherited  NEW USED / OLD free, no NEW alloc event: the object predates the window on NEW (inherited)
  slab-old-held-new-freed  OLD USED / NEW free: NEW FREED the chunk in the window (the boundary + site named) -- OLD
                           still holds an object NEW released (the DP-19 / DP-20 domain: OLD's dead heartbeat / zero polls).
                           The free is of the chunk ADDRESS; whether NEW freed OLD's OBJECT is told by the chunk body
                           (the bytes after the free-link word): `same-object` when NEW's freed residue equals OLD's
                           live body, `address-only` when NEW allocated+freed another object there (per chunk, and the
                           words split `...@same-object` / `...@address-only` in the histogram)
  slab-old-only-unobserved OLD USED / NEW free, no NEW event: OLD allocated it after the common ancestor; not
                           observable from NEW's census (OLD's own trace would name it)
  slab-page-reassigned     both used, the pages carry different classes: the page was (re)initialised on NEW at the
                           boundary named (page-init event; `init_from_zero` when the page had no list) and the first
                           POP after it (the trigger allocation: boundary, site, chunk, size class) -- the layout shift
  slab-reloc-equal / -partial / slab-genuine  both used, same class (slab_walk's content classes): the NEW alloc
                           event of the chunk in the window is named when there is one, else `inherited`
  free residue / links / descriptors / header / class table: unchanged from slab_walk (listed, not re-classed)
The per-boundary histogram of the events and the per-class chunk lists (capped by --list) are in the JSON.
Sets are built once per image; files only.
"""
import argparse
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import relocate_compare as rc  # noqa: E402
import slab_walk  # noqa: E402

WATCH_RE = re.compile(r"^watch=(0x[0-9a-f]+):w(\d+):b(\d+):([^:]+):(0x[0-9a-f]+)->(0x[0-9a-f]+)")
CENSUS_CLASSES = ("slab-new-progress", "slab-new-only-inherited", "slab-old-held-new-freed", "slab-old-only-unobserved",
                  "slab-page-reassigned", "slab-reloc-equal", "slab-reloc-partial", "slab-genuine", "slab-free-residue", "slab-free-link",
                  "slab-desc-state", "slab-desc-free", "slab-header", "slab-class-table", "other")


def read_events(path, pool, alloc_re, free_re, init_re, unmatched=None, non_desc=None):
    """[(boundary, op, page_idx, chunk, site)] from the census over the descriptors' free-chunk heads. A page-init
    event's tuple carries the OLD head as a 6th field (0x0 = the page had no list). `unmatched` (dict) collects the
    descriptor stores whose site matches none of the regexes: {site: {boundary: count}}; `non_desc` the stores to
    words outside the descriptors' heads (header / class table), by site."""
    out = []
    desc, n = pool.desc, pool.npages
    with open(path, errors="replace") as f:
        for line in f:
            m = WATCH_RE.match(line.strip())
            if not m:
                continue
            addr, b, site = int(m.group(1), 16), int(m.group(3)), m.group(4)
            old, new = int(m.group(5), 16), int(m.group(6), 16)
            if not (desc <= addr < desc + n * slab_walk.DESC_BYTES) or (addr - desc) % slab_walk.DESC_BYTES != 0xc:
                if non_desc is not None:
                    non_desc[site] = non_desc.get(site, 0) + 1
                continue
            idx = (addr - desc) // slab_walk.DESC_BYTES
            if alloc_re.search(site):
                out.append((b, "alloc", idx, old, site))
            elif free_re.search(site):
                out.append((b, "free", idx, new, site))
            elif init_re.search(site):
                out.append((b, "page-init", idx, new, site, old))
            else:
                out.append((b, "other", idx, new, site))
                if unmatched is not None:
                    unmatched.setdefault(site, {})
                    unmatched[site][b] = unmatched[site].get(b, 0) + 1
    return out


def chunk_body_equal(old_img, new_img, lo, size):
    """OLD's live chunk body == NEW's freed-chunk residue (both after the first word, the free-link slot)."""
    if size <= 4:
        return None
    return old_img.ram[lo + 4:lo + size] == new_img.ram[lo + 4:lo + size]


def census(old_img, new_img, P, ncls, mask_rows, events, window, arenas=(), list_cap=50, census_from=0, unmatched=None, non_desc=None):
    classes = {}
    res = slab_walk.compare(old_img, new_img, P, ncls, mask_rows, arenas, list_cap=0, collect=classes)
    po, pn = slab_walk.Pool(old_img, P, ncls), slab_walk.Pool(new_img, P, ncls)
    by_chunk = {}      # (idx, chunk) -> {alloc: [...], free: [...]}
    page_init = {}     # idx -> [(boundary, site, old_head)]
    page_pops = {}     # idx -> [(boundary, site, chunk)] in log order (the trigger search)
    hist = {}
    for ev in events:
        b, op, idx, chunk, site = ev[:5]
        if b > window:
            continue
        hist.setdefault(b, {}).setdefault(op, 0)
        hist[b][op] += 1
        if op == "page-init":
            page_init.setdefault(idx, []).append((b, site, ev[5] if len(ev) > 5 else None))
        elif op in ("alloc", "free"):
            by_chunk.setdefault((idx, chunk), {"alloc": [], "free": []})[op].append((b, site))
            if op == "alloc":
                page_pops.setdefault(idx, []).append((b, site, chunk))
    counts = {k: 0 for k in CENSUS_CLASSES}
    chunks = {}
    init_from_zero = sum(1 for v in page_init.values() for e in v if e[2] == 0)
    unobserved_note = "no NEW event in [b%d, b%d] (the census recorded stores from b%d only: an earlier event is not excluded)" % (census_from, window, census_from) \
        if census_from else "no NEW event in [b0, b%d]" % window
    for x, cls in classes.items():
        so, sn = po.status(x), pn.status(x)
        if so is None or sn is None or cls in ("slab-desc-state", "slab-desc-free", "slab-header", "slab-class-table", "slab-free-residue", "slab-free-link", "other"):
            counts[cls] += 1
            continue
        idx = po.page_of(x)
        key_o = (idx, so[2]) if so[0] == "USED" else None
        key_n = (idx, sn[2]) if sn[0] == "USED" else None
        ev_n = by_chunk.get(key_n, {"alloc": [], "free": []}) if key_n else {"alloc": [], "free": []}
        ev_o = by_chunk.get((idx, so[2]), {"alloc": [], "free": []}) if so[0] != "FREE-PAGE" and so[2] is not None else {"alloc": [], "free": []}
        if cls == "slab-alloc-differs":
            if sn[0] == "USED" and so[0] != "USED":
                ccls = "slab-new-progress" if ev_n["alloc"] else "slab-new-only-inherited"
                ck, size, c = key_n, sn[3], sn[1]
                last = ev_n["alloc"][-1] if ev_n["alloc"] else None
            elif so[0] == "USED" and sn[0] != "USED":
                ccls = "slab-old-held-new-freed" if ev_o["free"] else "slab-old-only-unobserved"
                ck, size, c = (idx, so[2]), so[3], so[1]
                last = ev_o["free"][-1] if ev_o["free"] else None
            else:
                ccls = "slab-page-reassigned"
                ck, size, c = (idx, sn[2]), sn[3], "%d<-%d" % (sn[1], so[1])
                inits = page_init.get(idx, [])
                last = inits[-1] if inits else None
        else:
            ccls = cls
            ck, size, c = key_n, sn[3], sn[1]
            last = ev_n["alloc"][-1] if ev_n["alloc"] else None
        counts[ccls] += 1
        key = (ccls,) + tuple(ck)
        if key not in chunks:
            e = {"class": ccls, "page": idx, "chunk": "0x%x" % ck[1], "size": size, "size_class": c, "words": 0,
                 "event": None if last is None else {"boundary": last[0], "site": last[1]},
                 "new_allocs": len(ev_n["alloc"]), "new_frees": len(ev_o["free"]) if so[0] == "USED" else len(ev_n["free"])}
            if ccls == "slab-page-reassigned":
                if last is not None:
                    e["event"]["init_from_zero"] = last[2] == 0
                    e["event"]["old_head"] = None if last[2] is None else "0x%x" % last[2]
                    trig = next((p for p in page_pops.get(idx, []) if p[0] >= last[0]), None)
                    e["trigger"] = None if trig is None else {"boundary": trig[0], "site": trig[1], "chunk": "0x%x" % trig[2], "size_class": sn[1], "size": sn[3]}
                else:
                    e["trigger"] = None
                    e["note"] = "no page-init event on NEW in the census window (%s)" % unobserved_note
            elif ccls in ("slab-old-only-unobserved", "slab-new-only-inherited"):
                e["note"] = unobserved_note
            elif ccls == "slab-old-held-new-freed":
                same = chunk_body_equal(old_img, new_img, ck[1], size)
                e["object"] = None if same is None else ("same-object" if same else "address-only")
            chunks[key] = e
        chunks[key]["words"] += 1
    lists = {}
    for e in sorted(chunks.values(), key=lambda e: (-e["words"], e["page"], e["chunk"])):
        lst = lists.setdefault(e["class"], [])
        if len(lst) < list_cap:
            lst.append(e)
    by_boundary = {}
    for e in chunks.values():
        if e["event"]:
            k = "%s@b%d" % (e["class"], e["event"]["boundary"])
            by_boundary[k] = by_boundary.get(k, 0) + e["words"]
        if e.get("object"):
            k = "%s@%s" % (e["class"], e["object"])
            by_boundary[k] = by_boundary.get(k, 0) + e["words"]
    triggers = {}
    for e in chunks.values():
        if e["class"] == "slab-page-reassigned":
            t = e.get("trigger")
            k = "none" if t is None else "b%d:%s:size%d" % (t["boundary"], t["site"], t["size"])
            triggers[k] = triggers.get(k, 0) + e["words"]
    sizes = {}
    for e in chunks.values():
        k = "%s/%s" % (e["class"], e["size"])
        sizes[k] = sizes.get(k, 0) + e["words"]
    return {"old": old_img.path, "new": new_img.path, "window": window, "census_from": census_from, "pool": res["pool"], "slab_walk_words": res["words"],
            "events_in_window": sum(sum(v.values()) for v in hist.values()), "events_total": len(events), "events_by_boundary": {str(k): v for k, v in sorted(hist.items())},
            "page_inits": sum(len(v) for v in page_init.values()), "page_inits_from_zero": init_from_zero, "pages_initialised": len(page_init),
            "unmatched_sites": {k: {str(b): n for b, n in sorted(v.items())} for k, v in sorted((unmatched or {}).items())},
            "non_descriptor_stores_by_site": dict(sorted((non_desc or {}).items(), key=lambda kv: -kv[1])),
            "page_reassigned_triggers": dict(sorted(triggers.items(), key=lambda kv: -kv[1])),
            "words": counts, "chunks": len(chunks), "words_by_class_and_boundary": dict(sorted(by_boundary.items(), key=lambda kv: -kv[1])),
            "words_by_class_and_size": dict(sorted(sizes.items(), key=lambda kv: -kv[1])), "lists": lists}


def render(res):
    out = ["slab_census: OLD %s vs NEW %s (window b%d; %d census events, %d inside the window)" % (res["old"], res["new"], res["window"], res["events_total"], res["events_in_window"])]
    w = res["words"]
    total = sum(w.values())
    out.append("  differing pool words %d in %d chunks/regions:" % (total, res["chunks"]))
    for k in CENSUS_CLASSES:
        if w[k]:
            out.append("    %-26s %8d  (%5.1f%%)" % (k, w[k], 100.0 * w[k] / total if total else 0))
    out.append("  words by class @ NEW event boundary (top 12): " + ", ".join("%s=%d" % kv for kv in list(res["words_by_class_and_boundary"].items())[:12]))
    out.append("  words by class / chunk size (top 12): " + ", ".join("%s=%d" % kv for kv in list(res["words_by_class_and_size"].items())[:12]))
    busiest = sorted(res["events_by_boundary"].items(), key=lambda kv: -sum(kv[1].values()))[:5]
    out.append("  busiest boundaries: " + ", ".join("b%s %s" % (b, v) for b, v in busiest))
    out.append("  page inits: %d on %d pages (%d from 0x0 = a page's first list); census recorded from b%d" % (res["page_inits"], res["pages_initialised"], res["page_inits_from_zero"], res["census_from"]))
    if res["unmatched_sites"]:
        out.append("  UNMATCHED descriptor sites (counted as `other`, NOT classified -- a census gap): " + ", ".join("%s %s" % kv for kv in res["unmatched_sites"].items()))
    else:
        out.append("  unmatched descriptor sites: none")
    if res["page_reassigned_triggers"]:
        out.append("  page-reassigned words by trigger (first pop at/after the page init: boundary:site:size): " + ", ".join("%s=%d" % kv for kv in list(res["page_reassigned_triggers"].items())[:12]))
    for cls, lst in res["lists"].items():
        for e in lst[:4]:
            out.append("    %-26s page %4d chunk %s size %4s cls %s words %4d event %s (NEW allocs %d frees %d)%s%s"
                       % (cls, e["page"], e["chunk"], e["size"], e["size_class"], e["words"], e["event"], e["new_allocs"], e["new_frees"],
                          " trigger %s" % e["trigger"] if "trigger" in e else "", " object %s" % e["object"] if e.get("object") else ""))
    return "\n".join(out)


def selftest():
    import tempfile
    import shutil
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)
    tmp = tempfile.mkdtemp(prefix="slab_census_")
    P, NCLS, DESC, CTAB, PAGES, NP = 0x1000, 4, 0x2000, 0x2100, 0x10000, 6
    ram_o, ram_n = bytearray(0x20000), bytearray(0x20000)

    def fill(ram, lo, size, seed):
        for k in range(0, size, 4):
            struct.pack_into("<I", ram, lo + k, (seed * 0x9e3779b1 + k) & 0xffffffff)
    # OLD: page 0 class 1 (1024): chunks 0,1 used; page 1 class 2 (2048): chunk 0 used. NEW: page 0: chunk 0 used, chunk 1 FREE (NEW freed it),
    # chunk 2 used (NEW allocated it); page 1 the same object with one word changed (genuine); page 3 NEW-only page class 1 chunk 0 used
    # (allocated in the window), page 4 NEW-only page class 1 chunk 0 used with NO event (inherited)
    slab_walk._build_pool(ram_o, P, NCLS, DESC, CTAB, PAGES, NP, {0: (1, 1024, {2, 3}), 1: (2, 2048, {1})})
    slab_walk._build_pool(ram_n, P, NCLS, DESC, CTAB, PAGES, NP, {0: (1, 1024, {1, 3}), 1: (2, 2048, {1}), 3: (1, 1024, {1, 2, 3}), 4: (1, 1024, {1, 2, 3})})
    fill(ram_o, PAGES, 1024, 1); fill(ram_n, PAGES, 1024, 1)
    fill(ram_o, PAGES + 1024, 1024, 2)                      # OLD's object in chunk 1
    fill(ram_n, PAGES + 2048, 1024, 3)                      # NEW's object in chunk 2
    fill(ram_o, PAGES + 4096, 2048, 4); fill(ram_n, PAGES + 4096, 2048, 4); struct.pack_into("<I", ram_n, PAGES + 4096 + 0x40, 0xdead)
    fill(ram_n, PAGES + 3 * 4096, 1024, 5); fill(ram_n, PAGES + 4 * 4096, 1024, 6)
    po, pn = os.path.join(tmp, "o.ps2st"), os.path.join(tmp, "n.ps2st")
    rc._synthetic(po, bytes(ram_o), bytes(0x100), 2); rc._synthetic(pn, bytes(ram_n), bytes(0x100), 2)
    d0, d3 = DESC + 0 * 32 + 0xc, DESC + 3 * 32 + 0xc
    watch = os.path.join(tmp, "watch.log")
    with open(watch, "w") as f:
        f.write("watch=0x%x:w4:b5:A_POP+0x10:0x%x->0x%x\n" % (d0, PAGES + 2048, PAGES + 3072))          # alloc chunk 2 of page 0 at b5
        f.write("watch=0x%x:w4:b7:A_PUSH+0x20:0x%x->0x%x\n" % (d0, PAGES + 3072, PAGES + 1024))         # free chunk 1 of page 0 at b7
        f.write("watch=0x%x:w4:b9:A_INIT+0x30:0x0->0x%x\n" % (d3, PAGES + 3 * 4096))                   # page 3 initialised at b9
        f.write("watch=0x%x:w4:b9:A_POP+0x10:0x%x->0x%x\n" % (d3, PAGES + 3 * 4096, PAGES + 3 * 4096 + 1024))   # alloc chunk 0 of page 3 at b9
        f.write("watch=0x%x:w4:b70:A_POP+0x10:0x%x->0x0\n" % (d0, PAGES + 1024))                        # after the window: ignored
    io, in_ = rc.Image(po), rc.Image(pn)
    pool = slab_walk.Pool(io, P, NCLS)
    ev = read_events(watch, pool, re.compile("A_POP"), re.compile("A_PUSH"), re.compile("A_INIT"))
    check(len(ev) == 5 and ev[0] == (5, "alloc", 0, PAGES + 2048, "A_POP+0x10") and ev[1] == (7, "free", 0, PAGES + 1024, "A_PUSH+0x20") and ev[2][1] == "page-init",
          "events decoded: pop = alloc of the old head, push = free of the new head, init = page init -> %r" % ev[:3])
    res = census(io, in_, P, NCLS, [], ev, 60)
    w = res["words"]
    check(w["slab-new-progress"] == 256 + 256 and w["slab-old-held-new-freed"] == 256 and w["slab-new-only-inherited"] == 256 and w["slab-genuine"] == 1,
          "NEW's chunk 2 (b5) + page 3 chunk 0 (b9) = new-progress 512; OLD's chunk 1 freed by NEW at b7 = old-held-new-freed 256; page 4 inherited 256; 1 genuine -> %r"
          % {k: v for k, v in w.items() if v})
    check(res["events_in_window"] == 4 and res["events_total"] == 5, "the b70 event is outside the window b60")
    check(res["page_inits"] == 1 and res["page_inits_from_zero"] == 1 and ev[2] == (9, "page-init", 3, PAGES + 3 * 4096, "A_INIT+0x30", 0),
          "the page-init store `0x0 -> chunk` is decoded as a page init FROM ZERO (the real site's shape) -> %r" % (ev[2],))
    check(res["unmatched_sites"] == {} and "unmatched descriptor sites: none" in render(res), "every descriptor site matched: no unmatched report")
    # an UNMATCHED descriptor site is reported (never silently `other`); a page REASSIGNED on NEW names its init + trigger;
    # the old-held-new-freed chunk's body tells same-object from address-only
    ram_n2 = bytearray(0x20000)
    slab_walk._build_pool(ram_n2, P, NCLS, DESC, CTAB, PAGES, NP, {0: (1, 1024, {1, 3}), 1: (3, 512, {2, 3, 4, 5, 6, 7}), 3: (1, 1024, {1, 2, 3}), 4: (1, 1024, {1, 2, 3})})
    fill(ram_n2, PAGES, 1024, 1); fill(ram_n2, PAGES + 2048, 1024, 3); fill(ram_n2, PAGES + 3 * 4096, 1024, 5); fill(ram_n2, PAGES + 4 * 4096, 1024, 6)
    fill(ram_n2, PAGES + 4096, 512, 7); fill(ram_n2, PAGES + 4096 + 512, 512, 8)          # page 1 re-initialised as class 3 (512): chunks 0,1 used
    pn2 = os.path.join(tmp, "n2.ps2st")
    rc._synthetic(pn2, bytes(ram_n2), bytes(0x100), 2)
    d1 = DESC + 1 * 32 + 0xc
    watch2 = os.path.join(tmp, "watch2.log")
    with open(watch2, "w") as f:
        f.write(open(watch).read())
        f.write("watch=0x%x:w4:b12:A_INIT+0x30:0x0->0x%x\n" % (d1, PAGES + 4096))                        # page 1 initialised at b12 (from 0x0)
        f.write("watch=0x%x:w4:b12:A_POP+0x10:0x%x->0x%x\n" % (d1, PAGES + 4096, PAGES + 4096 + 512))      # the trigger: first pop on page 1 after the init
        f.write("watch=0x%x:w4:b13:A_POP+0x10:0x%x->0x%x\n" % (d1, PAGES + 4096 + 512, PAGES + 4096 + 1024))
        f.write("watch=0x%x:w4:b14:A_MYSTERY+0x44:0x%x->0x%x\n" % (d1, PAGES + 4096 + 1024, PAGES + 4096 + 1536))   # an unmatched site
    um, nd = {}, {}
    ev2 = read_events(watch2, pool, re.compile("A_POP"), re.compile("A_PUSH"), re.compile("A_INIT"), um, nd)
    check(um == {"A_MYSTERY+0x44": {14: 1}} and any(e[1] == "other" for e in ev2), "an unmatched descriptor site is collected (site -> boundary -> count) and its event is `other` -> %r" % um)
    in2 = rc.Image(pn2)
    res2 = census(io, in2, P, NCLS, [], ev2, 60, census_from=4, unmatched=um, non_desc=nd)
    w2 = res2["words"]
    check(w2["slab-page-reassigned"] == 256 and w2["slab-old-only-unobserved"] == 256 and res2["lists"]["slab-page-reassigned"][0]["event"] == {"boundary": 12, "site": "A_INIT+0x30", "init_from_zero": True, "old_head": "0x0"},
          "page 1 (OLD class 2 / NEW class 3): OLD's 2048-byte object over NEW's two used 512 chunks = page-reassigned 256 words with the NEW init event from 0x0 at b12; over NEW's free chunks = old-only-unobserved 256 -> %r"
          % res2["lists"]["slab-page-reassigned"][0]["event"])
    trig = res2["lists"]["slab-page-reassigned"][0]["trigger"]
    check(trig == {"boundary": 12, "site": "A_POP+0x10", "chunk": "0x%x" % (PAGES + 4096), "size_class": 3, "size": 512} and res2["page_reassigned_triggers"] == {"b12:A_POP+0x10:size512": 256},
          "the trigger = the first pop on the page at/after the init (boundary, site, chunk, size class) -> %r" % trig)
    check(res2["unmatched_sites"] == {"A_MYSTERY+0x44": {"14": 1}} and "UNMATCHED descriptor sites" in render(res2) and res2["census_from"] == 4,
          "the unmatched site is REPORTED in the JSON and the text; census_from recorded")
    ohnf = res2["lists"]["slab-old-held-new-freed"][0]
    check(ohnf["object"] == "address-only" and "slab-old-held-new-freed@address-only" in res2["words_by_class_and_boundary"],
          "OLD's chunk 1 of page 0 vs NEW's freed residue (zeros): address-only (NEW's residue is not OLD's object) -> %s" % ohnf["object"])
    ram_n3 = bytearray(0x20000)
    slab_walk._build_pool(ram_n3, P, NCLS, DESC, CTAB, PAGES, NP, {0: (1, 1024, {1, 3}), 1: (2, 2048, {1}), 3: (1, 1024, {1, 2, 3}), 4: (1, 1024, {1, 2, 3})})
    fill(ram_n3, PAGES, 1024, 1); fill(ram_n3, PAGES + 2048, 1024, 3); fill(ram_n3, PAGES + 4096, 2048, 4); fill(ram_n3, PAGES + 3 * 4096, 1024, 5); fill(ram_n3, PAGES + 4 * 4096, 1024, 6)
    link = struct.unpack_from("<I", ram_n3, PAGES + 1024)[0]
    fill(ram_n3, PAGES + 1024, 1024, 2)       # NEW's freed chunk 1 still carries OLD's object bytes (the link word differs)
    struct.pack_into("<I", ram_n3, PAGES + 1024, link)
    pn3 = os.path.join(tmp, "n3.ps2st")
    rc._synthetic(pn3, bytes(ram_n3), bytes(0x100), 2)
    res3 = census(io, rc.Image(pn3), P, NCLS, [], ev, 60)
    ohnf3 = [e for e in res3["lists"].get("slab-old-held-new-freed", []) if e["chunk"] == "0x%x" % (PAGES + 1024)]
    check(ohnf3 and ohnf3[0]["object"] == "same-object", "when NEW's freed residue IS OLD's body (after the link word): same-object -> %r" % (ohnf3[0]["object"] if ohnf3 else None))
    check(res2["lists"]["slab-old-only-unobserved"][0]["note"].startswith("no NEW event in [b4, b60]") if res2["lists"].get("slab-old-only-unobserved") else True,
          "the unobserved classes say the census window they are unobserved in")
    lst = {e["chunk"]: e for e in res["lists"]["slab-new-progress"]}
    check(lst["0x%x" % (PAGES + 2048)]["event"] == {"boundary": 5, "site": "A_POP+0x10"} and lst["0x%x" % (PAGES + 3 * 4096)]["event"]["boundary"] == 9,
          "the alloc boundary + site are named per chunk")
    check(res["lists"]["slab-old-held-new-freed"][0]["event"] == {"boundary": 7, "site": "A_PUSH+0x20"}, "the free boundary + site are named for OLD's held object")
    check(res["words_by_class_and_boundary"].get("slab-new-progress@b9") == 256 and "slab-new-progress/1024" in res["words_by_class_and_size"], "the histograms")
    txt = render(res)
    check("slab-old-held-new-freed" in txt and "busiest boundaries" in txt, "render")
    shutil.rmtree(tmp, ignore_errors=True)
    print("slab_census selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("old", nargs="?")
    ap.add_argument("new", nargs="?")
    ap.add_argument("--watch")
    ap.add_argument("--pool")
    ap.add_argument("--classes", type=int)
    ap.add_argument("--window", type=int, help="the probe pair's boundary: events with boundary <= window count")
    ap.add_argument("--alloc-site", help="regex of the allocator's chunk-pop site (title data)")
    ap.add_argument("--free-site", help="regex of the chunk-push site (title data)")
    ap.add_argument("--page-init-site", help="regex of the page-init site (title data: the pool INIT routine's descriptor store, `0x0 -> first chunk` on a fresh page)")
    ap.add_argument("--census-from", type=int, default=0, help="the boundary the census recorded stores from (--watch-record-from); recorded in the JSON and the class notes")
    ap.add_argument("--arena", action="append", default=[])
    ap.add_argument("--mask", default=os.path.join(HERE, "mask", "coldboot_elf_text.ranges"))
    ap.add_argument("--out")
    ap.add_argument("--list", type=int, default=50)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not (a.old and a.new and a.watch and a.pool and a.classes and a.window is not None and a.alloc_site and a.free_site and a.page_init_site):
        ap.error("old, new, --watch, --pool, --classes, --window and the three site regexes are required")
    io, in_ = rc.Image(a.old), rc.Image(a.new)
    pool = slab_walk.Pool(io, int(a.pool, 16), a.classes)
    um, nd = {}, {}
    ev = read_events(a.watch, pool, re.compile(a.alloc_site), re.compile(a.free_site), re.compile(a.page_init_site), um, nd)
    arenas = [tuple(int(x, 16) for x in s.split("-", 1)) for s in a.arena]
    res = census(io, in_, int(a.pool, 16), a.classes, rc.read_mask_ranges(a.mask), ev, a.window, arenas, a.list, a.census_from, um, nd)
    print(render(res))
    if a.out:
        with open(a.out, "w") as f:
            json.dump(res, f, indent=1)
        print("json: %s" % a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
