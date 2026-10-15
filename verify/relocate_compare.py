#!/usr/bin/env python3
"""relocate_compare.py -- relocation-aware equality of two .ps2st images over the title's allocators.

  relocate_compare.py <old.ps2st> <new.ps2st> --heap <title heap header> --pool <title record-pool header>
                      [--pool-used <addr of the pool's live-record counter>] [--pool-head auto]
                      [--old-trace <OLD [ALLOC-TRACE] log>]
                      [--mask verify/mask/coldboot_elf_text.ranges] [--out reloc.json] [--list N]
  relocate_compare.py --selftest

Two title allocators are walked in BOTH images (the layouts are the title's, read from the images,
never assumed):

* the TITLE HEAP at `--heap` (+0 free head, +4 arena lo, +8 arena hi, +0xc/+0x10 free/used counts,
  +0x14/+0x18 free/used bytes): blocks are contiguous 16-byte headers (+0 next-free, +4 prev-free,
  +8 previous block, +0xc size | bit31 = FREE), user data at +16; the walk is checked against the
  header's counts. Free blocks are FREE units, never matched.
* the RECORD POOL at `--pool`: descriptors (0x20 bytes of title state each: +0 the record's handle
  index, +4 buf, +8 size, +0x10 free-list link, +0x14 prev, +0x18 next, +0x1c flags with bit 0 =
  FREE) form an address-ordered doubly-linked list whose head is the pool header's +0x50 word. The
  walk follows the chain WHEREVER the header points: on the hardware (and with `hle.meta_arena=off`)
  the descriptors are page-pool chunks in RAM0; an image written under the META-ARENA workaround
  holds them in the META section (`0x02000000 + off`); a restored image may hold both. Each side is
  walked from its OWN header word (the two sides' descriptors need not share a region). The
  per-class free-list heads are the header's +0xc..+0x48 words and the
  header's +0x4c word is the free-descriptor count. The walk honours the allocator's own state:
  the free-flagged descriptors must be exactly the ones reachable from the free heads and their
  number must equal +0x4c; with `--pool-used` the used count must equal that word on each side.
  Free descriptors are FREE units (their bytes are `free-residue`), never matched records.

Matching. Heap blocks: (size, pointer-insensitive fingerprint), then unmoved (same address, same
size), then same size + >= `--min-sim` equal words. Pool records: FIRST by the allocator's OWN key,
the handle index -- the same index with the same size on both sides whose bytes are equal under
relocation is one record (moved when the buffers differ); the same index with different sizes is
UNMATCHED on both sides (a both-sided record whose allocation differs -- never called "old-only");
an index present on one side only is an OLD-only / NEW-only record. SECOND, the records an index
pair does not explain (index pairs whose content differs, the one-sided and unmatched records) are
re-paired by content ((size, pointer-insensitive fingerprint), then same size + >= `--min-sim`):
such a pair is a CONTENT-PASS match (`reloc-rehandled`), split by the handle index of its two records:
  rehandled-same-idx  the SAME handle on both sides -- pass 1's exact equality failed on in-place
                      differing words (>= min-sim of the record is equal), the record only MOVED
                      (the allocator compacted it);
  rehandled-other-idx ANOTHER handle on the other side = the allocation ORDER differs.
What neither pass pairs is UNPAIRED (content with no counterpart).
A matched pair defines `delta = new_lo - old_lo`.

Every RAM0 word differing at the SAME address (outside the mask) is classified from BOTH sides --
OLD[x] must be explained (found at its relocated place) AND NEW[x] must be explained (a relocated
copy of a matched OLD record's word, or the content of a NEW-only record, ...):

  reloc-content     both explained: OLD[x] == NEW[x + delta_old(x)] and NEW[x] == OLD[x + delta_new(x)]
  reloc-rehandled   as above, but a side's record was paired by the CONTENT pass (the JSON's
                    `words.reloc-rehandled-same-idx` / `-other-idx` split the class by the handle test above;
                    the sub-counts are not classes of their own and sum to `reloc-rehandled`)
  reloc-pointer     as above with a relocated pointer into a matched record on one or both sides;
                    outside the arenas: NEW[x] == OLD[x] + delta(target)
  old-only-record   x lies in an OLD-only POOL record (NEW's side explained, free or a NEW-only record)
  new-only-record   x lies in a NEW-only POOL record (OLD's side explained or free)
  only-records-both x lies in a one-sided record on BOTH sides (OLD-only under NEW-only)
  heap-old-only-block / heap-new-only-block   the same for the TITLE HEAP's one-sided blocks (its own class:
                    the §7.1 heap row records OLD's hook-induced tail blocks, not pool records)
  unmatched-record  x lies in an UNMATCHED (both-sided, size differs) record on either side
  unpaired-record   x lies in a record neither pass pairs (its content has no counterpart) on either side
  free-residue      x lies in free space on both sides, or in free space on one side and is
                    explained on the other (stale bytes of unallocated space)
  genuine           inside a matched record on either side, no relocation explains that side
  other             outside both arenas, not a relocated pointer (left to attribute_probe.py's rules)

`--old-trace` (an OLD `[ALLOC-TRACE] seq=N F:<frame> op=ALLOC ... idx=0x.. buf=0x..` log) names the
OLD allocation (seq, frame) of the first record, in address order, at which the two pools' layouts
diverge, and of every OLD-only / unmatched record.

The two header addresses are TITLE DATA: `verify/attribution/classes.tsv` carries them as `@heap` /
`@pool` / `@pool-used` directives for attribute_probe.py; this tool takes them on the command line.

The JSON (`--out`) carries the counts, the record census, the first divergence, the relocation map and
the `genuine` / `other` word lists (capped by `--list`), which `attribute_probe.py --reloc` consumes.
"""
import argparse
import bisect
import hashlib
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import state_hash  # noqa: E402

META_BASE = 0x02000000
FREE_BIT = 0x80000000
POOL_FLAG_FREE = 0x1      # descriptor on a free list
POOL_FLAG_PENDING = 0x4   # released by the title but not yet on a free list (its free path dispatches on flags & 0xf)
IDX_FREE = 0xfffffffe
CLASSES = ("reloc-content", "reloc-rehandled", "reloc-pointer", "old-only-record", "new-only-record", "only-records-both",
           "heap-old-only-block", "heap-new-only-block", "unmatched-record", "unpaired-record", "free-residue", "genuine", "other")


def read_mask_ranges(path):
    rows = []
    if not path or not os.path.isfile(path):
        return rows
    rx = re.compile(r"^\s*(0x[0-9a-fA-F]+)\s*-\s*(0x[0-9a-fA-F]+)\s+(\S+)")
    with open(path) as f:
        for line in f:
            m = rx.match(line.split("#", 1)[0])
            if m:
                rows.append((int(m.group(1), 16), int(m.group(2), 16), m.group(3)))
    return rows


class Image:
    def __init__(self, path):
        _, self.vf, sec = state_hash.parse(path)
        self.path = path
        self.ram = sec[state_hash.TAG_RAM][2]
        self.spad = sec[state_hash.TAG_SPAD][2]
        self.meta = sec[state_hash.TAG_META][2]

    def w(self, a):
        return struct.unpack_from("<I", self.ram, a)[0]

    def mw(self, a):
        return struct.unpack_from("<I", self.meta, a - META_BASE)[0]

    def has_desc(self, a):
        """True when a 0x20-byte descriptor at `a` lies inside RAM0 or inside the image's META section."""
        if a >= META_BASE:
            return a - META_BASE + 0x20 <= len(self.meta)
        return 0 < a and a + 0x20 <= len(self.ram)

    def dw(self, a):
        """A descriptor word resolved by address: RAM0 below META_BASE, the META section at/above it."""
        return self.mw(a) if a >= META_BASE else self.w(a)


# ------------------------------------------------------------------ walkers

def walk_heap(img, heap):
    """[(user_addr, size, free)] + header dict; raises on a walk that does not meet the header."""
    lo, hi = img.w(heap + 4), img.w(heap + 8)
    blocks = []
    p = lo
    prev = 0
    while p + 16 <= hi:
        hdr = img.w(p + 0xc)
        size = hdr & 0x7fffffff
        if img.w(p + 8) != prev:
            raise ValueError("heap walk: block 0x%x prev link 0x%x != 0x%x" % (p, img.w(p + 8), prev))
        blocks.append((p + 16, size, bool(hdr & FREE_BIT)))
        prev = p
        p += 16 + size
        if len(blocks) > 1000000:
            raise ValueError("heap walk: runaway")
    if p != hi:
        raise ValueError("heap walk: end 0x%x != arena hi 0x%x" % (p, hi))
    header = {"arena_lo": lo, "arena_hi": hi, "free_cnt": img.w(heap + 0xc), "used_cnt": img.w(heap + 0x10),
              "free_bytes": img.w(heap + 0x14), "used_bytes": img.w(heap + 0x18), "alloc_calls": img.w(heap + 0x28)}
    nfree = sum(1 for b in blocks if b[2])
    if nfree != header["free_cnt"] or len(blocks) - nfree != header["used_cnt"]:
        raise ValueError("heap walk: counts free=%d used=%d vs header %r" % (nfree, len(blocks) - nfree, header))
    return blocks, header


def walk_pool(img, pool, head=None, used_addr=None):
    """[(buf, size, free, idx, desc, pending)] in chain order from the descriptors -- wherever the header's chain
    points (RAM0 page-pool chunks on the hardware / `hle.meta_arena=off`, the META section under the workaround,
    both in a restored image) -- honouring the allocator's own free state (flags bit 0, the free-list heads, the
    header's free count). `head` None = the image's own header word (+0x50)."""
    base, size = img.w(pool), img.w(pool + 4)
    recs = []
    d = img.w(pool + 0x50) if head is None else head
    seen = set()
    while d and d not in seen and img.has_desc(d):
        seen.add(d)
        idx, buf, sz, nxt, flags = img.dw(d), img.dw(d + 4), img.dw(d + 8), img.dw(d + 0x18), img.dw(d + 0x1c)
        if not (base <= buf < base + size):
            raise ValueError("pool walk: record buf 0x%x outside pool 0x%x+0x%x (desc 0x%x)" % (buf, base, size, d))
        free = bool(flags & POOL_FLAG_FREE)
        pending = (not free) and bool(flags & POOL_FLAG_PENDING)
        recs.append((buf, sz, free or pending, idx, d, pending))
        d = nxt
    if d and d in seen:
        raise ValueError("pool walk: descriptor cycle at 0x%x" % d)
    if d:
        raise ValueError("pool walk: descriptor 0x%x outside RAM0 and META (after %d descriptors)" % (d, len(recs)))
    for a, b in zip(recs, recs[1:]):
        if b[0] < a[0] + a[1]:
            raise ValueError("pool walk: records overlap 0x%x+0x%x > 0x%x" % (a[0], a[1], b[0]))
    # the allocator's free state: free heads +0xc..+0x48 linked by +0x10; count at +0x4c
    reach = set()
    for off in range(0xc, 0x4c, 4):
        f = img.w(pool + off)
        guard = 0
        while f and f not in reach and img.has_desc(f):
            reach.add(f)
            f = img.dw(f + 0x10)
            guard += 1
            if guard > 100000:
                raise ValueError("pool walk: free list runaway at head +0x%x" % off)
    flagged = {r[4] for r in recs if r[2] and not r[5]}
    pending = [r for r in recs if r[5]]
    if flagged != reach:
        raise ValueError("pool walk: free-flagged descriptors (%d) != free-list reachable (%d): only-flagged %s only-reachable %s"
                         % (len(flagged), len(reach), ["0x%x" % x for x in sorted(flagged - reach)][:8],
                            ["0x%x" % x for x in sorted(reach - flagged)][:8]))
    free_cnt = img.w(pool + 0x4c)
    if free_cnt != len(flagged):
        raise ValueError("pool walk: header free count %d != free descriptors %d" % (free_cnt, len(flagged)))
    used = len(recs) - len(flagged) - len(pending)
    hdr = {"base": base, "size": size, "count": len(recs), "free": len(flagged), "pending": len(pending), "used": used,
           "desc_in_meta": sum(1 for r in recs if r[4] >= META_BASE), "desc_in_ram": sum(1 for r in recs if r[4] < META_BASE),
           "pending_records": [{"lo": "0x%x" % r[0], "size": r[1], "idx": r[3]} for r in pending],
           "free_cnt_word": free_cnt, "used_cnt_word": None}
    if used_addr is not None:
        hdr["used_cnt_word"] = img.w(used_addr)
        if hdr["used_cnt_word"] != used:
            raise ValueError("pool walk: used descriptors %d != live-record counter 0x%x = %d" % (used, used_addr, hdr["used_cnt_word"]))
    return recs, hdr


# ------------------------------------------------------------------ matching

def fingerprint(ram, lo, size, arenas, limit=4096):
    n = min(size, limit) // 4
    words = struct.unpack_from("<%dI" % n, ram, lo)
    out = bytearray()
    for v in words:
        if any(a <= v < b for a, b in arenas):
            out += b"PTR!"
        else:
            out += struct.pack("<I", v)
    return hashlib.sha1(bytes(out)).hexdigest()


def similarity(ram_a, lo_a, ram_b, lo_b, size, arenas_a, arenas_b, limit=4096):
    n = min(size, limit) // 4
    if n == 0:
        return 1.0
    wa = struct.unpack_from("<%dI" % n, ram_a, lo_a)
    wb = struct.unpack_from("<%dI" % n, ram_b, lo_b)
    eq = 0
    for i in range(n):
        if wa[i] == wb[i]:
            eq += 1
        elif any(a <= wa[i] < b for a, b in arenas_a) and any(a <= wb[i] < b for a, b in arenas_b):
            eq += 1
    return eq / n


def match_heap_blocks(old_recs, new_recs, old_img, new_img, arenas_old, arenas_new, min_sim=0.9, unmoved=True):
    """Heap blocks [(lo, size)]: (1) equal (size, pointer-insensitive fingerprint) in address order;
    (2) same address + same size (only with `unmoved`); (3) same size + similarity >= min_sim.
    -> (pairs [(old_lo, new_lo, size)], old_only [(lo, size)], new_only [(lo, size)])."""
    def keyed(recs, img, arenas):
        out = {}
        for lo, size in recs:
            out.setdefault((size, fingerprint(img.ram, lo, size, arenas)), []).append(lo)
        return out
    ko = keyed(old_recs, old_img, arenas_old)
    kn = keyed(new_recs, new_img, arenas_new)
    pairs = []
    rest_o, rest_n = [], []
    for k in sorted(ko, key=lambda k: ko[k][0]):
        o, n = ko[k], kn.get(k, [])
        for i in range(min(len(o), len(n))):
            pairs.append((o[i], n[i], k[0]))
        rest_o += [(lo, k[0]) for lo in o[len(n):]]
    for k in kn:
        rest_n += [(lo, k[0]) for lo in kn[k][len(ko.get(k, [])):]]
    by_addr_n = {(lo, sz): True for lo, sz in rest_n}
    still_o = []
    for lo, sz in sorted(rest_o):
        if unmoved and by_addr_n.pop((lo, sz), None):
            pairs.append((lo, lo, sz))
        else:
            still_o.append((lo, sz))
    rest_n = sorted(by_addr_n)
    old_only, taken = [], set()
    for lo, sz in still_o:
        best, best_s = None, 0.0
        for j, (ln, szn) in enumerate(rest_n):
            if szn != sz or j in taken:
                continue
            sim = similarity(old_img.ram, lo, new_img.ram, ln, sz, arenas_old, arenas_new)
            if sim > best_s:
                best, best_s = j, sim
        if best is not None and best_s >= min_sim:
            taken.add(best)
            pairs.append((lo, rest_n[best][0], sz))
        else:
            old_only.append((lo, sz))
    new_only = [rest_n[j] for j in range(len(rest_n)) if j not in taken]
    pairs.sort()
    return pairs, sorted(old_only), sorted(new_only)


def match_pool_records(old_used, new_used):
    """Pool records [(lo, size, idx)] matched by the allocator's handle index.
    -> (pairs [(old_lo, new_lo, size, idx)], old_only [(lo, size, idx)], new_only [...],
        unmatched_old [(lo, size, idx)], unmatched_new [...]) -- unmatched = same idx, different size
    (or the free marker index on a used descriptor)."""
    bo = {}
    bn = {}
    unmatched_old, unmatched_new = [], []
    for lo, sz, idx in old_used:
        if idx == IDX_FREE or idx in bo:
            unmatched_old.append((lo, sz, idx))
        else:
            bo[idx] = (lo, sz)
    for lo, sz, idx in new_used:
        if idx == IDX_FREE or idx in bn:
            unmatched_new.append((lo, sz, idx))
        else:
            bn[idx] = (lo, sz)
    pairs, old_only, new_only = [], [], []
    for idx in sorted(bo):
        lo, sz = bo[idx]
        if idx not in bn:
            old_only.append((lo, sz, idx))
        elif bn[idx][1] == sz:
            pairs.append((lo, bn[idx][0], sz, idx))
        else:
            unmatched_old.append((lo, sz, idx))
            unmatched_new.append((bn[idx][0], bn[idx][1], idx))
    for idx in sorted(bn):
        if idx not in bo:
            new_only.append((bn[idx][0], bn[idx][1], idx))
    pairs.sort()
    return pairs, sorted(old_only), sorted(new_only), sorted(unmatched_old), sorted(unmatched_new)


class RelocMap:
    """Interval map lo -> (lo, size, delta); `find(addr)` -> (lo, size, delta) or None."""

    def __init__(self, triples):
        self.items = sorted(triples)
        self.starts = [x[0] for x in self.items]

    def find(self, addr):
        i = bisect.bisect_right(self.starts, addr) - 1
        if i >= 0:
            lo, size, delta = self.items[i]
            if lo <= addr < lo + size:
                return lo, size, delta
        return None


class Intervals:
    def __init__(self, items):
        self.items = sorted((lo, size) for lo, size, *_ in items)
        self.starts = [x[0] for x in self.items]

    def find(self, addr):
        i = bisect.bisect_right(self.starts, addr) - 1
        if i >= 0 and self.items[i][0] <= addr < self.items[i][0] + self.items[i][1]:
            return self.items[i]
        return None


def interval_find(items, addr):
    starts = [x[0] for x in items]
    i = bisect.bisect_right(starts, addr) - 1
    if i >= 0 and items[i][0] <= addr < items[i][0] + items[i][1]:
        return items[i]
    return None


# ------------------------------------------------------------------ the compare

class Side:
    """One image's unit layout: matched map (own addr -> delta to the other side), the only/unmatched/free
    intervals and the arenas."""

    def __init__(self, rmap, only, unmatched, free, arenas, unpaired=None, rehandled=None, heap_only=None):
        self.rmap, self.only, self.unmatched, self.free, self.arenas = rmap, only, unmatched, free, arenas
        self.unpaired = unpaired if unpaired is not None else Intervals([])
        self.rehandled = rehandled if rehandled is not None else Intervals([])
        self.heap_only = heap_only if heap_only is not None else Intervals([])

    def in_arena(self, a):
        return any(lo <= a < hi for lo, hi in self.arenas)


def _side_status(x, v_here, ram_other, n, here, other):
    """Status of word x on one side: EXPLAINED / ONLY / UNMATCHED / FREE / GENUINE / OUTSIDE / PTR.
    `here` = this side's layout (deltas map this side's addresses to the other side's), `other` = the
    other side's layout (to relocate a pointer VALUE held on this side)."""
    hit = here.rmap.find(x)
    if hit is not None:
        y = x + hit[2]
        vy = struct.unpack_from("<I", ram_other, y)[0] if 0 <= y < n else None
        if vy == v_here:
            return "REHANDLED" if here.rehandled.find(x) is not None else "EXPLAINED"
        t = here.rmap.find(v_here)  # a pointer into one of this side's matched records
        if t is not None and vy == (v_here + t[2]) & 0xffffffff:
            return "PTR"
        return "GENUINE"
    if here.unmatched.find(x) is not None:
        return "UNMATCHED"
    if here.unpaired.find(x) is not None:
        return "UNPAIRED"
    if here.heap_only.find(x) is not None:
        return "HONLY"
    if here.only.find(x) is not None:
        return "ONLY"
    if here.free.find(x) is not None:
        return "FREE"
    if here.in_arena(x):
        return "GAP"  # inside the arena, in no unit (allocator metadata / padding)
    return "OUTSIDE"


def _classify_word(x, vo, vn, old_side, new_side, ram_o, ram_n, n):
    so = _side_status(x, vo, ram_n, n, old_side, new_side)
    sn = _side_status(x, vn, ram_o, n, new_side, old_side)
    if so == "OUTSIDE" and sn == "OUTSIDE":
        t = old_side.rmap.find(vo)
        if t is not None and vn == (vo + t[2]) & 0xffffffff:
            return "reloc-pointer", so, sn
        return "other", so, sn
    if "GENUINE" in (so, sn):
        return "genuine", so, sn
    if "UNMATCHED" in (so, sn):
        return "unmatched-record", so, sn
    if "UNPAIRED" in (so, sn):
        return "unpaired-record", so, sn
    if so in ("ONLY", "HONLY") and sn in ("ONLY", "HONLY"):
        return "only-records-both", so, sn
    if so == "HONLY":
        return "heap-old-only-block", so, sn
    if sn == "HONLY":
        return "heap-new-only-block", so, sn
    if so == "ONLY":
        return "old-only-record", so, sn
    if sn == "ONLY":
        return "new-only-record", so, sn
    if "FREE" in (so, sn) or "GAP" in (so, sn) or "OUTSIDE" in (so, sn):
        return "free-residue", so, sn
    if "PTR" in (so, sn):
        return "reloc-pointer", so, sn
    if "REHANDLED" in (so, sn):
        return "reloc-rehandled", so, sn
    return "reloc-content", so, sn


def _equal_under_relocation(ram_o, o, ram_n, nw, size, rmap):
    a = ram_o[o:o + size]
    b = ram_n[nw:nw + size]
    if a == b:
        return True
    mm = size // 4
    wa = struct.unpack_from("<%dI" % mm, a)
    wb = struct.unpack_from("<%dI" % mm, b)
    for i in range(mm):
        if wa[i] == wb[i]:
            continue
        t = rmap.find(wa[i])
        if t is None or wb[i] != (wa[i] + t[2]) & 0xffffffff:
            return False
    return True


def load_old_trace(path):
    """{(idx, buf): (seq, frame)} of the LAST ALLOC line per (idx, buf) in an OLD [ALLOC-TRACE] log."""
    out = {}
    if not path:
        return out
    rx = re.compile(r"seq=(\d+) F:(\d+) op=ALLOC .*?idx=(0x[0-9a-f]+) .*?buf=(0x[0-9a-f]+)")
    with open(path, errors="replace") as f:
        for line in f:
            m = rx.search(line)
            if m:
                out[(int(m.group(3), 16), int(m.group(4), 16))] = (int(m.group(1)), int(m.group(2)))
    return out


def _lis_length(seq):
    """Length of the longest strictly increasing subsequence (patience sorting)."""
    tails = []
    for v in seq:
        i = bisect.bisect_left(tails, v)
        if i == len(tails):
            tails.append(v)
        else:
            tails[i] = v
    return len(tails)


def pool_layout(po, pn, idx_equal, rehandled, unm_o, unm_n, unp_o, unp_n, old_img, new_img, names=None, rehandled_same=None):
    """The record pool's layout on both sides as the title's allocator laid it out: a per-record table in
    address order (side, lo, size, idx, match class, counterpart, delta, name), the ORDER analysis over the
    matched records (the fraction of matched records that keep their relative address order on the other
    side = the longest common order; the first record out of order), the free-chunk census (count, bytes,
    largest) per side, and the names the `--names` table gives the records (a whole archive file at the
    record's start). `rehandled_same` = the set of (old_lo, new_lo) content-pass pairs whose handle idx is
    the same on both sides (their class reads `rehandled-same-idx`, the others `rehandled-other-idx`)."""
    table, probe = (names if names else ({}, 64))
    same = rehandled_same if rehandled_same is not None else set()
    other_of = {}
    cls_of = {}
    for o, nw, sz in idx_equal:
        other_of[("old", o)] = nw
        other_of[("new", nw)] = o
        cls_of[("old", o)] = cls_of[("new", nw)] = "idx-equal"
    for o, nw, sz in rehandled:
        other_of[("old", o)] = nw
        other_of[("new", nw)] = o
        cls_of[("old", o)] = cls_of[("new", nw)] = "rehandled-same-idx" if (o, nw) in same else "rehandled-other-idx"
    for lo, sz in unm_o:
        cls_of[("old", lo)] = "unmatched"
    for lo, sz in unm_n:
        cls_of[("new", lo)] = "unmatched"
    for lo, sz in unp_o:
        cls_of[("old", lo)] = "unpaired"
    for lo, sz in unp_n:
        cls_of[("new", lo)] = "unpaired"
    rows = []
    named = {"old": 0, "new": 0}
    free_census = {}
    for side, recs, img in (("old", po, old_img), ("new", pn, new_img)):
        free_chunks = [(lo, sz) for lo, sz, fr, _, _, _ in recs if fr]
        free_census[side] = {"count": len(free_chunks), "bytes": sum(sz for _, sz in free_chunks),
                             "largest": max([sz for _, sz in free_chunks] or [0]),
                             "used_bytes": sum(sz for lo, sz, fr, _, _, _ in recs if not fr),
                             "used_span_lo": "0x%x" % min([lo for lo, sz, fr, _, _, _ in recs if not fr] or [0]),
                             "used_span_hi": "0x%x" % max([lo + sz for lo, sz, fr, _, _, _ in recs if not fr] or [0])}
        rank = 0
        for lo, sz, fr, idx, _, pending in recs:
            if fr:
                rows.append({"side": side, "rank": None, "lo": "0x%x" % lo, "size": sz, "idx": None, "class": "pending" if pending else "free",
                             "other": None, "delta": None, "name": None})
                continue
            cls = cls_of.get((side, lo), "only")
            oth = other_of.get((side, lo))
            hit = None
            if table:
                import asset_names
                hit = asset_names.name_bytes(table, probe, img.ram, lo, sz)
                if hit is not None:
                    named[side] += 1
            rows.append({"side": side, "rank": rank, "lo": "0x%x" % lo, "size": sz, "idx": idx, "class": cls,
                         "other": None if oth is None else "0x%x" % oth, "delta": None if oth is None else (oth - lo if side == "old" else lo - oth),
                         "name": None if hit is None else hit[0]})
            rank += 1
    # ORDER: matched records sorted by OLD address -> their NEW addresses; the longest increasing run of those
    # is the set of records that keep their relative order on both sides
    matched = sorted([(o, nw, sz, "idx-equal") for o, nw, sz in idx_equal] + [(o, nw, sz, "rehandled") for o, nw, sz in rehandled])
    new_seq = [nw for _, nw, _, _ in matched]
    lcs = _lis_length(new_seq)
    first_out = None
    if matched:
        # the first matched record (by OLD address) whose NEW address is below the NEW address of an earlier matched record
        max_new = -1
        name_of = {}
        for r in rows:
            if r["name"] and r["side"] == "old":
                name_of[int(r["lo"], 16)] = r["name"]
        for k, (o, nw, sz, how) in enumerate(matched):
            if nw < max_new:
                first_out = {"ordinal": k, "old": "0x%x" % o, "new": "0x%x" % nw, "size": sz, "how": how, "name": name_of.get(o),
                             "note": "its NEW address is below that of an earlier (by OLD address) matched record"}
                break
            max_new = max(max_new, nw)
    order = {"matched": len(matched), "longest_common_order": lcs,
             "in_order_fraction": (lcs / len(matched)) if matched else 1.0,
             "out_of_order": len(matched) - lcs, "first_out_of_order": first_out}
    return {"records": rows, "order": order, "free": free_census, "named": named}


def compare(old_img, new_img, heap, pool, pool_head, mask_rows, list_cap=200, min_sim=0.9, collect=None,
            pool_used=None, old_trace=None, names=None, layout=False):
    """`collect`: an optional dict filled with {addr: class} for every differing outside-mask RAM0 word.
    `names`: (table, probe) from asset_names.load_table -- names the pool records; `layout`: add the per-record
    table + order analysis (`pool_layout`) to the result."""
    res = {"old": old_img.path, "new": new_img.path, "old_vsync_frame": old_img.vf, "new_vsync_frame": new_img.vf}
    arenas_old, arenas_new = [], []
    census = {}
    pairs = []          # (old_lo, new_lo, size)
    only_o, only_n = [], []
    unm_o, unm_n = [], []
    free_o, free_n = [], []
    heap_only_o, heap_only_n = [], []
    if heap is not None:
        ho, hdr_o = walk_heap(old_img, heap)
        hn, hdr_n = walk_heap(new_img, heap)
        census["heap"] = {"old": hdr_o, "new": hdr_n, "old_blocks": len(ho), "new_blocks": len(hn)}
        arenas_old.append((hdr_o["arena_lo"], hdr_o["arena_hi"]))
        arenas_new.append((hdr_n["arena_lo"], hdr_n["arena_hi"]))
        free_o += [(lo, sz) for lo, sz, fr in ho if fr]
        free_n += [(lo, sz) for lo, sz, fr in hn if fr]
        hp, hoo, hno = match_heap_blocks([(lo, sz) for lo, sz, fr in ho if not fr], [(lo, sz) for lo, sz, fr in hn if not fr],
                                         old_img, new_img, arenas_old + [(0, 0)], arenas_new + [(0, 0)], min_sim)
        pairs += hp
        heap_only_o, heap_only_n = hoo, hno
        census["heap"]["identical_layout_blocks"] = sum(1 for a, b in zip(ho, hn) if a[0] == b[0] and a[1] == b[1])
    first_div = None
    unp_o, unp_n, rehandled_items, rehandled_same_items = [], [], [], []
    if pool is not None:
        # each side follows its OWN header word unless the caller overrides the head with an address that is not
        # simply OLD's header word (the descriptors need not share a region: META under the workaround, RAM0 without)
        override = None if pool_head is None or pool_head == old_img.w(pool + 0x50) else pool_head
        po, phdr_o = walk_pool(old_img, pool, override, pool_used)
        pn, phdr_n = walk_pool(new_img, pool, override, pool_used)
        census["pool"] = {"old": phdr_o, "new": phdr_n}
        arenas_old.append((phdr_o["base"], phdr_o["base"] + phdr_o["size"]))
        arenas_new.append((phdr_n["base"], phdr_n["base"] + phdr_n["size"]))
        free_o += [(lo, sz) for lo, sz, fr, _, _, _ in po if fr]
        free_n += [(lo, sz) for lo, sz, fr, _, _, _ in pn if fr]
        pp, poo, pno, puo, pun = match_pool_records([(lo, sz, idx) for lo, sz, fr, idx, _, _ in po if not fr],
                                                    [(lo, sz, idx) for lo, sz, fr, idx, _, _ in pn if not fr])
        # pass 1 result: keep the idx pairs whose bytes are equal under relocation (pointers into the
        # pool/heap relocated by the idx map itself); the rest go to the content pass
        idx_map = RelocMap([(o, s_, nw - o) for o, nw, s_, _ in pp])
        idx_equal, idx_unequal = [], []
        for o, nw, sz, idx in pp:
            if _equal_under_relocation(old_img.ram, o, new_img.ram, nw, sz, idx_map):
                idx_equal.append((o, nw, sz))
            else:
                idx_unequal.append((o, nw, sz, idx))
        left_o = [(o, sz) for o, _, sz, _ in idx_unequal] + [(lo, sz) for lo, sz, _ in poo] + [(lo, sz) for lo, sz, _ in puo]
        left_n = [(nw, sz) for _, nw, sz, _ in idx_unequal] + [(lo, sz) for lo, sz, _ in pno] + [(lo, sz) for lo, sz, _ in pun]
        re_pairs, re_o, re_n = match_heap_blocks(sorted(left_o), sorted(left_n), old_img, new_img,
                                                 arenas_old + [(0, 0)], arenas_new + [(0, 0)], min_sim, unmoved=False)
        rehandled = [(o, nw, sz) for o, nw, sz in re_pairs]
        pairs += idx_equal + rehandled
        rehandled_items = [(o, sz) for o, _, sz in rehandled] + [(nw, sz) for _, nw, sz in rehandled]
        # the handle test over the content-pass pairs: the same idx on both sides = the record only MOVED
        # (pass 1 failed on in-place differing words); another idx = the allocation order differs
        idx_of_o = {r[0]: r[3] for r in po if not r[2]}
        idx_of_n = {r[0]: r[3] for r in pn if not r[2]}
        rehandled_same = {(o, nw) for o, nw, _ in rehandled if idx_of_o.get(o) == idx_of_n.get(nw)}
        rehandled_same_items = [(o, sz) for o, nw, sz in rehandled if (o, nw) in rehandled_same] + \
                               [(nw, sz) for o, nw, sz in rehandled if (o, nw) in rehandled_same]
        set_only_o = {(lo, sz) for lo, sz, _ in poo}
        set_only_n = {(lo, sz) for lo, sz, _ in pno}
        set_unm_o = {(lo, sz) for lo, sz, _ in puo}
        set_unm_n = {(lo, sz) for lo, sz, _ in pun}
        unp_o = [(lo, sz) for lo, sz in re_o if (lo, sz) not in set_only_o and (lo, sz) not in set_unm_o]
        unp_n = [(lo, sz) for lo, sz in re_n if (lo, sz) not in set_only_n and (lo, sz) not in set_unm_n]
        only_o += [(lo, sz) for lo, sz in re_o if (lo, sz) in set_only_o]
        only_n += [(lo, sz) for lo, sz in re_n if (lo, sz) in set_only_n]
        unm_o += [(lo, sz) for lo, sz in re_o if (lo, sz) in set_unm_o]
        unm_n += [(lo, sz) for lo, sz in re_n if (lo, sz) in set_unm_n]
        trace = load_old_trace(old_trace)

        def named(lo, sz, idx):
            e = {"lo": "0x%x" % lo, "size": sz, "idx": idx}
            t = trace.get((idx, lo))
            if t:
                e["old_alloc"] = {"seq": t[0], "frame": t[1]}
            return e
        census["pool"]["by_idx"] = {"old_only": [named(*r) for r in poo],
                                    "new_only": [{"lo": "0x%x" % lo, "size": sz, "idx": idx} for lo, sz, idx in pno],
                                    "unmatched_old": [named(*r) for r in puo],
                                    "unmatched_new": [{"lo": "0x%x" % lo, "size": sz, "idx": idx} for lo, sz, idx in pun]}
        set_re_o = set(re_o)
        set_re_n = set(re_n)
        census["pool"]["old_only"] = [named(*r) for r in poo if (r[0], r[1]) in set_re_o]
        census["pool"]["new_only"] = [{"lo": "0x%x" % lo, "size": sz, "idx": idx} for lo, sz, idx in pno if (lo, sz) in set_re_n]
        census["pool"]["unmatched_old"] = [named(*r) for r in puo if (r[0], r[1]) in set_re_o]
        census["pool"]["unmatched_new"] = [{"lo": "0x%x" % lo, "size": sz, "idx": idx} for lo, sz, idx in pun if (lo, sz) in set_re_n]
        census["pool"]["idx_pairs"] = len(pp)
        census["pool"]["idx_pairs_equal"] = len(idx_equal)
        census["pool"]["rehandled"] = len(rehandled)
        census["pool"]["rehandled_same_idx"] = len(rehandled_same)
        census["pool"]["rehandled_other_idx"] = len(rehandled) - len(rehandled_same)
        census["pool"]["rehandled_list"] = [{"old": "0x%x" % o, "new": "0x%x" % nw, "size": sz, "old_idx": idx_of_o.get(o), "new_idx": idx_of_n.get(nw),
                                            "same_idx": (o, nw) in rehandled_same} for o, nw, sz in rehandled]
        census["pool"]["unpaired_old"] = [named(lo, sz, next((i for b, s_, i in [(r[0], r[1], r[3]) for r in po] if b == lo), -1)) for lo, sz in sorted(unp_o)]
        idx_of_n = {r[0]: r[3] for r in pn}
        census["pool"]["unpaired_new"] = [{"lo": "0x%x" % lo, "size": sz, "idx": idx_of_n.get(lo, -1)} for lo, sz in sorted(unp_n)]
        census["pool"]["matched"] = len(idx_equal) + len(rehandled)
        census["pool"]["moved"] = sum(1 for o, nw, _ in idx_equal + rehandled if o != nw)
        # first divergence in address order over USED records
        uo = [r for r in po if not r[2]]
        un = [r for r in pn if not r[2]]
        same = 0
        for a, b in zip(uo, un):
            if (a[0], a[1], a[3]) != (b[0], b[1], b[3]):
                first_div = {"ordinal": same, "old": named(a[0], a[1], a[3]), "new": {"lo": "0x%x" % b[0], "size": b[1], "idx": b[3]}}
                break
            same += 1
        census["pool"]["identical_layout_records"] = same
        if layout or names:
            census["pool"]["layout"] = pool_layout(po, pn, idx_equal, rehandled, unm_o, unm_n, unp_o, unp_n, old_img, new_img, names, rehandled_same)
    rmap_o = RelocMap([(o, s, nw - o) for o, nw, s in pairs])
    rmap_n = RelocMap([(nw, s, o - nw) for o, nw, s in pairs])
    old_side = Side(rmap_o, Intervals(only_o), Intervals(unm_o), Intervals(free_o), arenas_old,
                    Intervals(unp_o), Intervals(rehandled_items), Intervals(heap_only_o))
    new_side = Side(rmap_n, Intervals(only_n), Intervals(unm_n), Intervals(free_n), arenas_new,
                    Intervals(unp_n), Intervals(rehandled_items), Intervals(heap_only_n))
    same_idx_iv = Intervals(rehandled_same_items)
    only_o = only_o + heap_only_o
    only_n = only_n + heap_only_n
    counts = {k: 0 for k in CLASSES}
    counts["masked"] = 0
    counts["reloc-rehandled-same-idx"] = 0   # sub-counts of reloc-rehandled (not classes; they sum to it)
    counts["reloc-rehandled-other-idx"] = 0
    sides = {}
    genuine, other = [], []
    ram_o, ram_n = old_img.ram, new_img.ram
    n = min(len(ram_o), len(ram_n))
    chunk = 65536
    for off in range(0, n, chunk):
        co, cn = ram_o[off:off + chunk], ram_n[off:off + chunk]
        if co == cn:
            continue
        m = min(len(co), len(cn)) // 4
        wo = struct.unpack_from("<%dI" % m, co)
        wn = struct.unpack_from("<%dI" % m, cn)
        for i in range(m):
            if wo[i] == wn[i]:
                continue
            x = off + i * 4
            if any(lo <= x < hi for lo, hi, _ in mask_rows):
                counts["masked"] += 1
                continue
            vo, vn = wo[i], wn[i]
            cls, so, sn = _classify_word(x, vo, vn, old_side, new_side, ram_o, ram_n, n)
            counts[cls] += 1
            if cls == "reloc-rehandled":
                counts["reloc-rehandled-same-idx" if same_idx_iv.find(x) is not None else "reloc-rehandled-other-idx"] += 1
            sides[(so, sn)] = sides.get((so, sn), 0) + 1
            if collect is not None:
                collect[x] = cls
            if cls == "genuine" and len(genuine) < list_cap:
                hit = rmap_o.find(x) or rmap_n.find(x)
                genuine.append({"addr": "0x%x" % x, "old": "0x%x" % vo, "new": "0x%x" % vn, "sides": "%s/%s" % (so, sn),
                                "record": "0x%x+0x%x" % (hit[0], hit[1]), "delta": hit[2]})
            elif cls == "other" and len(other) < list_cap:
                other.append({"addr": "0x%x" % x, "old": "0x%x" % vo, "new": "0x%x" % vn})
    # per matched record: fully equal under relocation?
    equal_records = 0
    moved_records = 0
    for o, nw, size in pairs:
        if o != nw:
            moved_records += 1
        if _equal_under_relocation(ram_o, o, ram_n, nw, size, rmap_o):
            equal_records += 1
    counts["identical_relocated_records"] = equal_records
    res.update({"census": census,
                "first_divergence": first_div,
                "records": {"matched": len(pairs), "moved": moved_records, "equal_under_relocation": equal_records,
                            "old_only": [{"lo": "0x%x" % lo, "size": size} for lo, size in sorted(only_o)],
                            "new_only": [{"lo": "0x%x" % lo, "size": size} for lo, size in sorted(only_n)],
                            "unmatched_old": [{"lo": "0x%x" % lo, "size": size} for lo, size in sorted(unm_o)],
                            "unmatched_new": [{"lo": "0x%x" % lo, "size": size} for lo, size in sorted(unm_n)],
                            "unpaired_old": [{"lo": "0x%x" % lo, "size": size} for lo, size in sorted(unp_o)],
                            "unpaired_new": [{"lo": "0x%x" % lo, "size": size} for lo, size in sorted(unp_n)],
                            "free_old": len(free_o), "free_new": len(free_n)},
                "relocation_map": [{"old": "0x%x" % o, "new": "0x%x" % nw, "size": s, "delta": nw - o}
                                   for o, nw, s in pairs if o != nw],
                "words": counts,
                "sides": [{"old": k[0], "new": k[1], "words": v} for k, v in sorted(sides.items(), key=lambda kv: -kv[1])],
                "genuine": genuine, "other": other})
    return res


def write_blocks(res, path):
    """The per-record table of both pools as TSV (address order per side)."""
    lay = res["census"].get("pool", {}).get("layout")
    if not lay:
        raise ValueError("no pool layout in the result (run compare with layout=True)")
    with open(path, "w") as f:
        f.write("# relocate_compare.py --blocks: OLD %s | NEW %s\n" % (res["old"], res["new"]))
        f.write("side\trank\tlo\tsize\tidx\tclass\tother\tdelta\tname\n")
        for r in lay["records"]:
            f.write("%s\t%s\t%s\t0x%x\t%s\t%s\t%s\t%s\t%s\n" % (r["side"], "" if r["rank"] is None else r["rank"], r["lo"], r["size"],
                                                               "" if r["idx"] is None else r["idx"], r["class"], r["other"] or "",
                                                               "" if r["delta"] is None else r["delta"], r["name"] or ""))


def render(res):
    out = []
    out.append("relocate_compare: OLD %s (vf %d) vs NEW %s (vf %d)" % (res["old"], res["old_vsync_frame"], res["new"], res["new_vsync_frame"]))
    c = res["census"]
    if "heap" in c:
        h = c["heap"]
        out.append("  heap: OLD %d blocks (used %d, free %d, alloc_calls %d) | NEW %d blocks (used %d, free %d, alloc_calls %d) | identical layout %d"
                   % (h["old_blocks"], h["old"]["used_cnt"], h["old"]["free_cnt"], h["old"]["alloc_calls"],
                      h["new_blocks"], h["new"]["used_cnt"], h["new"]["free_cnt"], h["new"]["alloc_calls"], h["identical_layout_blocks"]))
    if "pool" in c:
        p = c["pool"]
        out.append("  pool: OLD %d descriptors (used %d, free %d, pending %d, counter %s) | NEW %d (used %d, free %d, pending %d, counter %s) (base 0x%x size 0x%x)"
                   % (p["old"]["count"], p["old"]["used"], p["old"]["free"], p["old"]["pending"], p["old"]["used_cnt_word"],
                      p["new"]["count"], p["new"]["used"], p["new"]["free"], p["new"]["pending"], p["new"]["used_cnt_word"], p["old"]["base"], p["old"]["size"]))
        for x in p["old"]["pending_records"] + p["new"]["pending_records"]:
            out.append("    pending %s size 0x%x" % (x["lo"], x["size"]))
        b = p["by_idx"]
        out.append("  pool records by handle idx: pairs %d (equal under relocation %d) old-only %d new-only %d unmatched (same idx, other size) %d/%d | identical layout (addr,size,idx) %d"
                   % (p["idx_pairs"], p["idx_pairs_equal"], len(b["old_only"]), len(b["new_only"]), len(b["unmatched_old"]), len(b["unmatched_new"]),
                      p["identical_layout_records"]))
        out.append("  pool records after the content pass: rehandled %d (same handle idx %d = moved only, pass 1 failed on in-place words; other idx %d = allocation order differs)"
                   " -> matched %d (moved %d) | still old-only %d new-only %d unmatched %d/%d unpaired %d/%d"
                   % (p["rehandled"], p.get("rehandled_same_idx", 0), p.get("rehandled_other_idx", 0), p["matched"], p["moved"], len(p["old_only"]), len(p["new_only"]),
                      len(p["unmatched_old"]), len(p["unmatched_new"]), len(p["unpaired_old"]), len(p["unpaired_new"])))
        lay = p.get("layout")
        if lay:
            fo, fn, od = lay["free"]["old"], lay["free"]["new"], lay["order"]
            out.append("  pool free chunks: OLD %d (0x%x bytes, largest 0x%x; used 0x%x in %s-%s) | NEW %d (0x%x bytes, largest 0x%x; used 0x%x in %s-%s)"
                       % (fo["count"], fo["bytes"], fo["largest"], fo["used_bytes"], fo["used_span_lo"], fo["used_span_hi"],
                          fn["count"], fn["bytes"], fn["largest"], fn["used_bytes"], fn["used_span_lo"], fn["used_span_hi"]))
            out.append("  pool ORDER over the %d matched records (by OLD address): longest common order %d (%.1f%%), out of order %d"
                       % (od["matched"], od["longest_common_order"], 100.0 * od["in_order_fraction"], od["out_of_order"]))
            fx = od["first_out_of_order"]
            if fx:
                out.append("    first out of order: matched record #%d OLD %s+0x%x -> NEW %s (%s%s) -- %s"
                           % (fx["ordinal"], fx["old"], fx["size"], fx["new"], fx["how"], (", " + fx["name"]) if fx["name"] else "", fx["note"]))
            if lay["named"]["old"] or lay["named"]["new"]:
                out.append("  pool records named from the archive table: OLD %d NEW %d (of %d / %d used)"
                           % (lay["named"]["old"], lay["named"]["new"], p["old"]["used"], p["new"]["used"]))
        for x in b["old_only"] + b["unmatched_old"]:
            extra = (" (OLD alloc seq=%d F:%d)" % (x["old_alloc"]["seq"], x["old_alloc"]["frame"])) if "old_alloc" in x else ""
            out.append("    by-idx old-side %s size 0x%x idx %d%s" % (x["lo"], x["size"], x["idx"], extra))
        for x in b["new_only"] + b["unmatched_new"]:
            out.append("    by-idx new-side %s size 0x%x idx %d" % (x["lo"], x["size"], x["idx"]))
        for tag, lst in (("old-only", p["old_only"]), ("new-only", p["new_only"]), ("unmatched-old", p["unmatched_old"]), ("unmatched-new", p["unmatched_new"]),
                         ("unpaired-old", p["unpaired_old"]), ("unpaired-new", p["unpaired_new"])):
            for x in lst[:8]:
                extra = (" (OLD alloc seq=%d F:%d)" % (x["old_alloc"]["seq"], x["old_alloc"]["frame"])) if "old_alloc" in x else ""
                out.append("    %s %s size 0x%x idx %s%s" % (tag, x["lo"], x["size"], x.get("idx", "?"), extra))
    fd = res.get("first_divergence")
    if fd:
        o, nw = fd["old"], fd["new"]
        extra = (" = OLD alloc seq=%d F:%d" % (o["old_alloc"]["seq"], o["old_alloc"]["frame"])) if "old_alloc" in o else ""
        out.append("  first layout divergence at used record #%d: OLD %s+0x%x idx %d%s | NEW %s+0x%x idx %d"
                   % (fd["ordinal"], o["lo"], o["size"], o["idx"], extra, nw["lo"], nw["size"], nw["idx"]))
    r = res["records"]
    out.append("  units matched=%d (moved=%d, equal under relocation=%d) old-only=%d new-only=%d unmatched=%d/%d free=%d/%d"
               % (r["matched"], r["moved"], r["equal_under_relocation"], len(r["old_only"]), len(r["new_only"]),
                  len(r["unmatched_old"]), len(r["unmatched_new"]), r["free_old"], r["free_new"]))
    w = res["words"]
    total = sum(w[k] for k in CLASSES)
    out.append("  differing RAM0 words outside mask: %d (masked %d)" % (total, w["masked"]))
    for k in CLASSES:
        out.append("    %-18s %8d  (%5.1f%%)" % (k, w[k], 100.0 * w[k] / total if total else 0.0))
        if k == "reloc-rehandled" and ("reloc-rehandled-same-idx" in w):
            out.append("      of which same-idx %8d (records that kept their handle: moved only) | other-idx %8d (another handle: allocation order differs)"
                       % (w["reloc-rehandled-same-idx"], w["reloc-rehandled-other-idx"]))
    out.append("  side statuses (OLD/NEW): " + ", ".join("%s/%s=%d" % (s["old"], s["new"], s["words"]) for s in res["sides"][:12]))
    for g in res["genuine"][:10]:
        out.append("    genuine %s old=%s new=%s sides=%s record=%s delta=%d" % (g["addr"], g["old"], g["new"], g["sides"], g["record"], g["delta"]))
    return "\n".join(out)


# ------------------------------------------------------------------ selftest

def _synthetic(path, ram, meta, vf=0):
    import zlib
    spad = bytes(0x4000)
    body = b"PS2SAVE1" + struct.pack("<IIIIQ", 4, 3, vf, 0, 0)
    for tag, payload in ((state_hash.TAG_RAM, ram), (state_hash.TAG_SPAD, spad), (state_hash.TAG_META, meta)):
        body += struct.pack("<IIIIQ", tag, 1, 0, zlib.crc32(payload) & 0xFFFFFFFF, len(payload)) + payload
    body += struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(body)


def _build(ram, heap, blocks):
    lo, hi = 0x1000, 0x1000 + sum(16 + s for s, _ in blocks)
    struct.pack_into("<II", ram, heap + 4, lo, hi)
    p, prev, free_head, nfree, fb, ub = lo, 0, 0, 0, 0, 0
    for size, free in blocks:
        struct.pack_into("<IIII", ram, p, free_head if free else 0, 0, prev, size | (FREE_BIT if free else 0))
        if free:
            free_head = p
            nfree += 1
            fb += size + 16
        else:
            ub += size + 16
        prev = p
        p += 16 + size
    struct.pack_into("<IIIII", ram, heap, free_head, lo, hi, nfree, len(blocks) - nfree)
    struct.pack_into("<II", ram, heap + 0x14, fb, ub)
    return lo, hi


def _pool(ram, meta, pool, base, size, recs, used_addr=None, desc_at=None):
    """Write pool header + descriptors for records [(buf, size, idx, free)] (address order); free records are
    linked from the header's +0xc free head and counted at +0x4c. `desc_at` None = the descriptors in META (the
    workaround's shape, `0x02000000 + 0x4c + i*0x40`); a RAM0 address = the hardware's shape (page-pool chunks
    0x40 apart from that address)."""
    struct.pack_into("<II", ram, pool, base, size)
    in_meta = desc_at is None
    head = 0x4c if in_meta else desc_at
    mem = meta if in_meta else ram
    abs_base = META_BASE if in_meta else 0
    free_prev = 0
    nfree = 0
    for i, (buf, sz, idx, free) in enumerate(recs):
        d = head + i * 0x40
        nxt = (abs_base + head + (i + 1) * 0x40) if i + 1 < len(recs) else 0
        prv = (abs_base + head + (i - 1) * 0x40) if i > 0 else 0
        struct.pack_into("<IIII", mem, d, IDX_FREE if free else idx, buf, sz, 0)
        struct.pack_into("<IIII", mem, d + 0x10, 0, prv, nxt, 0x8001 if free else 0x24002)
        if free:
            if free_prev:
                struct.pack_into("<I", mem, free_prev + 0x10, abs_base + d)
            else:
                struct.pack_into("<I", ram, pool + 0xc, abs_base + d)
            free_prev = d
            nfree += 1
    struct.pack_into("<I", ram, pool + 0x4c, nfree)
    struct.pack_into("<I", ram, pool + 0x50, abs_base + head)
    if used_addr is not None:
        struct.pack_into("<I", ram, used_addr, len(recs) - nfree)


def selftest():
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)
    tmp = tempfile.mkdtemp(prefix="reloc_selftest_")
    HEAP, POOL, USED = 0x100, 0x200, 0x300
    ram_o, ram_n = bytearray(0x20000), bytearray(0x20000)
    meta_o, meta_n = bytearray(0x2000), bytearray(0x2000)
    # heap: OLD has one extra used block at the tail (the hook-induced allocation); 3 shared blocks identical
    _build(ram_o, HEAP, [(0x40, False), (0x80, False), (0x40, False), (0x30, False), (0x100, True)])
    _build(ram_n, HEAP, [(0x40, False), (0x80, False), (0x40, False), (0x140, True)])
    for base in (0x1010, 0x1060, 0x10f0):
        for k in range(0, 0x40, 4):
            struct.pack_into("<I", ram_o, base + k, 0x11110000 + base + k)
            struct.pack_into("<I", ram_n, base + k, 0x11110000 + base + k)
    struct.pack_into("<I", ram_o, 0x1140, 0xdeadbeef)  # OLD-only heap block content
    # the heap's free tail: stale bytes differ on both sides -> free-residue
    struct.pack_into("<I", ram_o, 0x1180, 0x5a5a5a5a); struct.pack_into("<I", ram_n, 0x1180, 0xa5a5a5a5)

    def fill(ram, lo, size, seed):
        for k in range(0, size, 4):
            struct.pack_into("<I", ram, lo + k, (seed * 0x9e3779b1 + k) & 0xffffffff)
    # pool over [0x8000, 0x10000): OLD used A(idx1,0x100)@0x8000, B(idx2,0x200)@0x8100, C(idx3,0x100)@0x8300,
    #   D(idx4,0x100)@0x8400, E(idx5,0x80)@0x8500, FREE(0x80)@0x8580 ; NEW: B@0x8000, A@0x8200, C@0x8300,
    #   N(idx9,0x100)@0x8400 [NEW-only, where OLD's D sits], E'(idx5,0x40)@0x8500 [same idx, other size = UNMATCHED],
    #   FREE(0xc0)@0x8540
    fill(ram_o, 0x8000, 0x100, 1); fill(ram_n, 0x8200, 0x100, 1)
    fill(ram_o, 0x8100, 0x200, 2); fill(ram_n, 0x8000, 0x200, 2)
    fill(ram_o, 0x8300, 0x100, 3); fill(ram_n, 0x8300, 0x100, 3)
    fill(ram_o, 0x8400, 0x100, 4); fill(ram_n, 0x8400, 0x100, 9)
    fill(ram_o, 0x8500, 0x80, 5); fill(ram_n, 0x8500, 0x40, 6)
    struct.pack_into("<I", ram_o, 0x8580, 0x11111111); struct.pack_into("<I", ram_n, 0x8580, 0x22222222)  # free on both sides
    struct.pack_into("<I", ram_o, 0x8540, 0x33333333); struct.pack_into("<I", ram_n, 0x8540, 0x44444444)  # OLD E / NEW free
    # a pointer inside C to A's 0x10th word, relocated in NEW; and one genuine difference in C
    struct.pack_into("<I", ram_o, 0x8340, 0x8010); struct.pack_into("<I", ram_n, 0x8340, 0x8210)
    struct.pack_into("<I", ram_o, 0x8350, 0x1234); struct.pack_into("<I", ram_n, 0x8350, 0x4321)
    # an outside-arena pointer table entry pointing into B, relocated
    struct.pack_into("<I", ram_o, 0x600, 0x8104); struct.pack_into("<I", ram_n, 0x600, 0x8004)
    # an outside-arena unrelated difference
    struct.pack_into("<I", ram_o, 0x700, 7); struct.pack_into("<I", ram_n, 0x700, 8)
    _pool(ram_o, meta_o, POOL, 0x8000, 0x8000,
          [(0x8000, 0x100, 1, False), (0x8100, 0x200, 2, False), (0x8300, 0x100, 3, False), (0x8400, 0x100, 4, False),
           (0x8500, 0x80, 5, False), (0x8580, 0x80, 0, True)], USED)
    _pool(ram_n, meta_n, POOL, 0x8000, 0x8000,
          [(0x8000, 0x200, 2, False), (0x8200, 0x100, 1, False), (0x8300, 0x100, 3, False), (0x8400, 0x100, 9, False),
           (0x8500, 0x40, 5, False), (0x8540, 0xc0, 0, True)], USED)
    po, pn = os.path.join(tmp, "old.ps2st"), os.path.join(tmp, "new.ps2st")
    _synthetic(po, bytes(ram_o), bytes(meta_o), 120)
    _synthetic(pn, bytes(ram_n), bytes(meta_n), 120)
    trace = os.path.join(tmp, "old_trace.txt")
    with open(trace, "w") as f:
        f.write("[ALLOC-TRACE] seq=7 F:96 op=ALLOC pool=0x200 size=0x100 commit=0x100 cnt_post=4 idx=0x4 struct=0x200010c buf=0x8400 ra=0x0\n")
    res = compare(Image(po), Image(pn), HEAP, POOL, META_BASE + 0x4c, [(0x700, 0x704, "P0-TEST")], pool_used=USED, old_trace=trace)
    r, w, p = res["records"], res["words"], res["census"]["pool"]
    check(res["census"]["heap"]["old_blocks"] == 5 and res["census"]["heap"]["new_blocks"] == 4, "heap walks meet the headers (5 / 4 blocks)")
    check(p["old"]["used"] == 5 and p["old"]["free"] == 1 and p["old"]["used_cnt_word"] == 5 and p["new"]["used"] == 5 and p["new"]["free"] == 1,
          "pool walk honours the free flag/free list/counts: OLD used 5 free 1 (counter 5), NEW used 5 free 1")
    check(r["free_old"] == 2 and r["free_new"] == 2, "free units (heap tail + pool free chunk) are not matched records -> %d/%d" % (r["free_old"], r["free_new"]))
    check(p["matched"] == 3 and p["moved"] == 2, "pool records matched by idx: A, B, C (A/B moved) -> matched=%d moved=%d" % (p["matched"], p["moved"]))
    check(p["idx_pairs_equal"] == 2 and p["rehandled"] == 1 and p["rehandled_same_idx"] == 1 and p["rehandled_other_idx"] == 0,
          "C (idx 3 both sides, one in-place genuine word) fails pass 1 and is content-paired as rehandled-SAME-idx (not an order difference) -> equal %d rehandled %d same %d other %d"
          % (p["idx_pairs_equal"], p["rehandled"], p["rehandled_same_idx"], p["rehandled_other_idx"]))
    check(p["rehandled_list"][0]["same_idx"] is True and p["rehandled_list"][0]["old_idx"] == 3 == p["rehandled_list"][0]["new_idx"],
          "the rehandled list carries both handle indices and the same-idx verdict -> %r" % p["rehandled_list"][0])
    check(w["reloc-rehandled"] == 0 and w["reloc-rehandled-same-idx"] + w["reloc-rehandled-other-idx"] == w["reloc-rehandled"],
          "C is unmoved so none of its equal words differ: reloc-rehandled 0 = same-idx %d + other-idx %d" % (w["reloc-rehandled-same-idx"], w["reloc-rehandled-other-idx"]))
    check([x["idx"] for x in p["old_only"]] == [4] and p["old_only"][0]["old_alloc"] == {"seq": 7, "frame": 96},
          "OLD-only record = idx 4 named from the OLD trace (seq 7 F:96) -> %r" % p["old_only"])
    check([x["idx"] for x in p["new_only"]] == [9], "NEW-only record = idx 9 -> %r" % p["new_only"])
    check([x["idx"] for x in p["unmatched_old"]] == [5] and [x["idx"] for x in p["unmatched_new"]] == [5],
          "same idx, different size -> UNMATCHED on both sides (never old-only) -> %r / %r" % (p["unmatched_old"], p["unmatched_new"]))
    check(res["first_divergence"]["ordinal"] == 0 and res["first_divergence"]["old"]["idx"] == 1 and res["first_divergence"]["new"]["idx"] == 2,
          "first layout divergence named at used record #0 (OLD idx 1 / NEW idx 2)")
    check(r["matched"] == 6 and r["moved"] == 2, "3 heap blocks + 3 records matched, A and B moved -> matched=%d moved=%d" % (r["matched"], r["moved"]))
    check(len(r["old_only"]) == 2 and r["old_only"][0]["lo"] == "0x1140", "the OLD-only heap block is named -> %r" % r["old_only"])
    check(r["equal_under_relocation"] == 5, "5 of 6 matched units equal under relocation (C carries one genuine word) -> %d" % r["equal_under_relocation"])
    check(w["reloc-content"] == 0xc0, "reloc-content words = 0xc0 (A/B swap, both sides explained) -> %d" % w["reloc-content"])
    check(w["reloc-pointer"] == 2, "reloc-pointer = 2 (inside C + the outside table) -> %d" % w["reloc-pointer"])
    check(w["genuine"] == 1 and res["genuine"][0]["addr"] == "0x8350", "one genuine word at 0x8350 -> %r" % res["genuine"])
    check(w["heap-old-only-block"] == 1 and w["old-only-record"] == 0, "the OLD-only heap block word (NEW side: heap free tail) -> heap-old-only-block=%d" % w["heap-old-only-block"])
    check(w["only-records-both"] == 0x40, "a NEW-only record over an OLD-only record: 0x40 words only-records-both -> %d" % w["only-records-both"])
    check(w["unmatched-record"] == 0x20, "the unmatched idx-5 record: 0x20 words (0x8500-0x8540 both sides, 0x8540-0x8580 OLD's E vs NEW free) -> %d" % w["unmatched-record"])
    check(w["free-residue"] == 5, "free residue: heap free tail word, 3 heap block-header words in no unit, the pool free word -> %d" % w["free-residue"])
    check(w["new-only-record"] == 0, "no one-sided NEW-only word in this layout -> %d" % w["new-only-record"])
    check(w["other"] == 4 and w["masked"] == 1,
          "other = the 4 heap-header words (free head, used count, free/used bytes); the masked word is not classified (other=%d masked=%d)" % (w["other"], w["masked"]))
    txt = render(res)
    check("matched=6" in txt and "first layout divergence" in txt, "render names the match count and the first divergence")
    check("rehandled 1 (same handle idx 1 = moved only" in txt, "render splits the content-pass line same-idx / other-idx")
    lay = compare(Image(po), Image(pn), HEAP, POOL, META_BASE + 0x4c, [], pool_used=USED, layout=True)["census"]["pool"]["layout"]
    cls_c = [r["class"] for r in lay["records"] if r["side"] == "old" and r["lo"] == "0x8300"]
    check(cls_c == ["rehandled-same-idx"], "the layout table classes C as rehandled-same-idx -> %r" % cls_c)
    # the BLOCKER case: a free descriptor whose flag says FREE but is not on a free list must fail the walk
    meta_bad = bytearray(meta_o)
    struct.pack_into("<I", meta_bad, 0x4c + 5 * 0x40 + 0x1c, 0x24002)  # the free record flagged used
    pb = os.path.join(tmp, "bad.ps2st")
    _synthetic(pb, bytes(ram_o), bytes(meta_bad), 120)
    try:
        walk_pool(Image(pb), POOL, META_BASE + 0x4c)
        check(False, "a descriptor on the free list flagged used must fail the walk")
    except ValueError as e:
        check("free-flagged" in str(e), "free-list / flag disagreement is an error -> %s" % e)
    ram_bad = bytearray(ram_o)
    struct.pack_into("<I", ram_bad, USED, 4)
    pb2 = os.path.join(tmp, "bad2.ps2st")
    _synthetic(pb2, bytes(ram_bad), bytes(meta_o), 120)
    try:
        walk_pool(Image(pb2), POOL, META_BASE + 0x4c, USED)
        check(False, "used count != the live-record counter must fail the walk")
    except ValueError as e:
        check("live-record counter" in str(e), "used count vs counter is checked -> %s" % e)
    same = compare(Image(po), Image(po), HEAP, POOL, META_BASE + 0x4c, [], pool_used=USED)
    check(sum(same["words"][k] for k in CLASSES) == 0 and same["records"]["moved"] == 0,
          "an image against itself: 0 differing words, 0 moved")
    # --- the descriptors wherever the header's chain points: the HARDWARE shape (RAM0 page-pool chunks, the META
    # section empty = an `hle.meta_arena=off` image) walked against the META shape (OLD's image), each side from its own
    # header word even when the caller hands over OLD's head (attribute_probe passes `old_img.w(pool + 0x50)`)
    ram_n2, meta_n2 = bytearray(ram_n), bytearray(0x2000)
    _pool(ram_n2, meta_n2, POOL, 0x8000, 0x8000,
          [(0x8000, 0x200, 2, False), (0x8200, 0x100, 1, False), (0x8300, 0x100, 3, False), (0x8400, 0x100, 9, False),
           (0x8500, 0x40, 5, False), (0x8540, 0xc0, 0, True)], USED, desc_at=0x2000)
    pn2 = os.path.join(tmp, "new_ram_desc.ps2st")
    _synthetic(pn2, bytes(ram_n2), bytes(meta_n2), 120)
    recs2, hdr2 = walk_pool(Image(pn2), POOL, None, USED)
    check(hdr2["count"] == 6 and hdr2["desc_in_ram"] == 6 and hdr2["desc_in_meta"] == 0 and hdr2["used"] == 5 and hdr2["free"] == 1
          and [r[4] for r in recs2] == [0x2000 + i * 0x40 for i in range(6)],
          "RAM-shape walk from the image's own header: 6 descriptors in RAM0 (META 0), used 5 free 1 -> %r" % {k: hdr2[k] for k in ("count", "desc_in_ram", "desc_in_meta", "used", "free")})
    try:
        walk_pool(Image(pn2), POOL, META_BASE + 0x4c, USED)
        check(False, "OLD's META head forced on a RAM-shape image must fail (its META is empty)")
    except ValueError as e:
        check("outside pool" in str(e), "OLD's META head forced on a RAM-shape image fails on the empty META -> %s" % e)
    mixed = compare(Image(po), Image(pn2), HEAP, POOL, META_BASE + 0x4c, [(0x700, 0x704, "P0-TEST")], pool_used=USED, old_trace=trace)
    pm = mixed["census"]["pool"]
    check(pm["old"]["desc_in_meta"] == 6 and pm["new"]["desc_in_ram"] == 6,
          "compare() with OLD's head given: OLD walked in META (6), NEW in RAM0 (6) -- each side its own header")
    check(pm["matched"] == 3 and pm["moved"] == 2 and [x["idx"] for x in pm["old_only"]] == [4] and [x["idx"] for x in pm["new_only"]] == [9]
          and mixed["first_divergence"]["ordinal"] == 0 and mixed["words"]["reloc-content"] == 0xc0 and mixed["words"]["genuine"] == 1,
          "the record analysis is the same as META vs META (matched 3, moved 2, old-only idx 4, new-only idx 9, 0xc0 reloc-content, 1 genuine)")
    desc_words = sum(1 for k in range(0x2000, 0x2000 + 6 * 0x40, 4) if struct.unpack_from("<I", ram_n2, k)[0] != 0)
    check(mixed["words"]["other"] == w["other"] + desc_words + 2,
          "the RAM0 descriptors' %d non-zero words + the header's chain/free heads (2) are 'other' words outside every arena -> %d (META shape %d)"
          % (desc_words, mixed["words"]["other"], w["other"]))
    # --- the MIXED shape (a restored image: nodes restored into META, later ones on the page pool): C's descriptor moved
    # from META to RAM0 with the chain re-linked across the two regions
    ram_m, meta_m = bytearray(ram_n), bytearray(meta_n)
    src, dst = 0x4c + 2 * 0x40, 0x2000
    ram_m[dst:dst + 0x20] = meta_m[src:src + 0x20]
    meta_m[src:src + 0x20] = bytes(0x20)
    struct.pack_into("<I", meta_m, 0x4c + 1 * 0x40 + 0x18, dst)              # B.next -> C in RAM0
    struct.pack_into("<I", meta_m, 0x4c + 3 * 0x40 + 0x14, dst)              # N.prev -> C in RAM0
    pm_path = os.path.join(tmp, "new_mixed_desc.ps2st")
    _synthetic(pm_path, bytes(ram_m), bytes(meta_m), 120)
    recs3, hdr3 = walk_pool(Image(pm_path), POOL, None, USED)
    check(hdr3["count"] == 6 and hdr3["desc_in_meta"] == 5 and hdr3["desc_in_ram"] == 1 and recs3[2][4] == dst and recs3[2][0] == 0x8300,
          "MIXED-shape walk: the chain crosses META -> RAM0 -> META (5 META + 1 RAM descriptor, C at 0x%x) -> %r" % (dst, {k: hdr3[k] for k in ("count", "desc_in_meta", "desc_in_ram")}))
    ram_dangling = bytearray(ram_m)
    struct.pack_into("<I", ram_dangling, dst + 0x18, 0x1fff0)                # C.next -> past RAM0's end
    pd = os.path.join(tmp, "new_dangling_desc.ps2st")
    _synthetic(pd, bytes(ram_dangling), bytes(meta_m), 120)
    try:
        walk_pool(Image(pd), POOL, None, USED)
        check(False, "a chain link outside RAM0 and META must fail the walk")
    except ValueError as e:
        check("outside RAM0 and META" in str(e), "a dangling link is an error, never a silent end of the chain -> %s" % e)
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)
    print("relocate_compare selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("old", nargs="?")
    ap.add_argument("new", nargs="?")
    ap.add_argument("--heap", help="title heap header address (title data; or 'none')")
    ap.add_argument("--pool", help="title record-pool header address (title data; or 'none')")
    ap.add_argument("--pool-used", help="address of the pool's live-record counter (title data); checked against the walk")
    ap.add_argument("--pool-head", default="auto", help="first pool descriptor (META address); auto = the pool header's +0x50 word")
    ap.add_argument("--old-trace", help="OLD [ALLOC-TRACE] log naming the OLD allocations (seq, frame) of the diverging records")
    ap.add_argument("--mask", default=os.path.join(HERE, "mask", "coldboot_elf_text.ranges"))
    ap.add_argument("--out")
    ap.add_argument("--list", type=int, default=200)
    ap.add_argument("--min-sim", type=float, default=0.9, help="heap pass-3 match: fraction of equal words required")
    ap.add_argument("--names", help="asset_names.py table: names each pool record holding a whole archive file at its start")
    ap.add_argument("--layout", action="store_true", help="per-record table + ORDER analysis + free-chunk census of the pool (in the JSON)")
    ap.add_argument("--blocks", help="write the per-record table (both sides, address order) as TSV")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not (a.old and a.new):
        ap.error("old and new images are required")
    if a.heap is None or a.pool is None:
        ap.error("--heap and --pool are required (title data; 'none' skips an allocator)")
    heap = None if a.heap == "none" else int(a.heap, 16)
    pool = None if a.pool == "none" else int(a.pool, 16)
    used = int(a.pool_used, 16) if a.pool_used else None
    old_img, new_img = Image(a.old), Image(a.new)
    head = None
    if pool is not None and a.pool_head != "auto":
        head = int(a.pool_head, 16)      # an explicit head applies to both sides; auto = each side's own header word
    if pool is not None and a.pool_head == "auto" and new_img.w(pool + 0x50) != old_img.w(pool + 0x50):
        print("pool list heads differ: OLD 0x%x NEW 0x%x (each side walked from its own header)" % (old_img.w(pool + 0x50), new_img.w(pool + 0x50)))
    names = None
    if a.names:
        import asset_names
        names = asset_names.load_table(a.names)
    res = compare(old_img, new_img, heap, pool, head, read_mask_ranges(a.mask), a.list, a.min_sim, pool_used=used, old_trace=a.old_trace,
                  names=names, layout=a.layout or bool(a.blocks))
    print(render(res))
    if a.blocks:
        write_blocks(res, a.blocks)
        print("blocks: %s" % a.blocks)
    if a.out:
        with open(a.out, "w") as f:
            json.dump(res, f, indent=1)
        print("json: %s" % a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
