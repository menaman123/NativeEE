#!/usr/bin/env python3
"""attribute_probe.py -- the residual attribution ledger of one probe pair.

  attribute_probe.py <old.ps2st> <new.ps2st> --classes verify/attribution/classes.tsv
                     [--mask verify/mask/coldboot_elf_text.ranges] [--leg <leg>] [--watch watch.log]
                     [--reloc] [--old-trace <OLD alloc log>] [--out ledger.json] [--unattributed N|all]
                     [--row-cap-words 64]
  attribute_probe.py --selftest

For every RAM0 + SPAD word that differs between the two images outside the mask (the score's
word set, `verify/score.py`), the class is decided in this order:

  1. the FIRST matching ROW/DP/OPEN rule of the classes TSV whose `legs` column admits `--leg` and
     whose writer regex (if any) matches the writer the `--watch` census names for the word
     (`watch=0xADDR:wN:bK:FUN_xxx+0xoff:old->new` lines; a rule with a regex never matches a word
     the census does not name);
  2. with `--reloc`: the structural class of the word -- `relocate_compare.py`'s (the title heap +
     record pool: the TSV's `@heap` / `@pool` / `@pool-used` directives), then `slab_walk.py`'s (the
     page pool: `@slabpool <addr> <classes>`), then `spad_stack.py`'s (the scratchpad stack:
     `@spad-fill <word>`, with `@spad-text lo-hi` / `@spad-decoder-ctx off:val,...` / `@spad-decoder-text lo-hi`
     for its frame-shape evidence and its per-pair conditions), all title data, all walked in both images;
     the class -> (ledger class, row,
     kind) mapping is the TSV's `@reloc-class <structural class> <ledger class> <row-or-DP> <kind> [legs]`
     directive (the optional `legs` token -- `*` or a comma list -- scopes the booking to the legs whose
     evidence proved it; on any other leg the class keeps its OPEN default); a structural class WITHOUT a directive is kind OPEN (`OPEN-RELOC-<class>`,
     `OPEN-SLAB-<class>`, `OPEN-SPAD-<class>`): nothing is booked to a row by the tool itself;
  3. the FIRST matching MIXED region rule of the TSV;
  4. else UNATTRIBUTED (every such word is in the JSON; the text lists up to `--unattributed`).

Kinds (from the TSV): ROW = attributed to a §7.1 OLD-defect row; DP = a decided DP without a row;
OPEN = an OPEN decision; MIXED = a named region whose per-word cause is one of several classes;
NONSTATE = words PROVED not to be guest state (dead stack below the live `$sp`, free allocator residue)
-- the DT-3 column: reported on its own, never folded into "attributed", never excluded from the total
(no TSV books a class to it until DT-3 is decided).
The honesty rule for ROW/DP rules: a rule is only as good as the record naming its words. A ROW/DP
rule wider than `--row-cap-words` (default 64) that has no writer regex and whose row column carries
no `recorded:<record>` token (the record that names the whole range, e.g. a DP's structural rule)
is DEMOTED to MIXED at load time and reported -- a region rule without a recorded word is never ROW.
The verdict line is `ATTRIBUTED-100%` only when every differing word is ROW/DP (no OPEN, MIXED or
UNATTRIBUTED word) -- the re-baseline rule's condition; `not_attributed` = MIXED + UNATTRIBUTED
is reported next to it.

DT-7 (DECISIONS_LOG; P8.30-T) -- the CONSOLE-PROVED booking. A ROW rule or `@reloc-class` directive may carry
`proof=console:<raw EE image>@<leg>:b<N>` (the image = PCSX2's RAM at the MOMENT matched to that probe pair;
a path is absolute or relative to the repo root / its parent). On that pair ONLY, a word the rule books is
`booked_console` when the console's word at the address equals NEW's and differs from OLD's -- the proof is
checked WORD BY WORD against the console bytes, never asserted from the row. `summary["booked_console"]` is
the count score.py subtracts from the raw outside-mask count to form `words_unbooked` (the decision number;
the raw count stays the reported one). A word whose rule carries the proof but where NEW != console is NOT
booked (reported as `console_ne_new`); SPAD words are never console-provable (the raw image is RAM only).
The FILED condition is enforced by `load_rules`: a `proof=console:` token is admitted only when the row id (the
first token of the row column / the directive's row) is a FILED §7.1 row per `diff_legs.known_row_ids()` --
an unfiled, proposed or mistyped id is REFUSED (SystemExit; nothing booked), as `apply_rebaseline.py` refuses.

Sets are built once per lane; files only.
"""
import argparse
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import state_hash  # noqa: E402
import relocate_compare as rc  # noqa: E402
import slab_walk  # noqa: E402
import spad_stack  # noqa: E402

SPAD_BASE = 0x70000000
KINDS = ("ROW", "DP", "OPEN", "MIXED", "NONSTATE", "UNATTRIBUTED")
RULE_KINDS = ("ROW", "DP", "OPEN", "MIXED")
DIRECTIVES = ("heap", "pool", "pool-used", "slabpool", "spad-fill", "spad-text", "spad-decoder-ctx", "spad-decoder-text", "reloc-class")
STRUCTURAL_CLASSES = tuple(c for c in rc.CLASSES if c != "other") + tuple(c for c in slab_walk.CLASSES if c != "other") + spad_stack.CLASSES
ROW_CAP_WORDS = 64
RECORDED_RE = re.compile(r"recorded:\S+")
PROOF_RE = re.compile(r"proof=console:(\S+?)@([A-Za-z0-9_]+):b(\d+)")
REPO_ROOT = os.path.dirname(HERE)
RAM_WINDOW = 0x02000000


def parse_proof(text):
    """`proof=console:<path>@<leg>:b<N>` inside a row column / directive -> (path, leg, boundary) or None."""
    m = PROOF_RE.search(text or "")
    return (m.group(1), m.group(2), int(m.group(3))) if m else None


def filed_row_ids():
    """The §7.1 row ids FILED in the docs (diff_legs.known_row_ids): the FILED condition of DT-7."""
    import diff_legs  # noqa: E402  (verify/diff_legs.py: known_row_ids over EQUIVALENCE_PLAN §7.1 / PHASE*_PLAN / PHASE*_IMPL)
    return diff_legs.known_row_ids()


def require_filed(row_col, path, n, known_ids):
    """DT-7: a `proof=console:` token is admitted on a ROW whose id (the first token of the row column) is FILED;
    an unfiled / proposed / mistyped id is REFUSED (SystemExit) -- never booked. `known_ids` None -> the docs'."""
    row_id = (row_col or "").split()[0] if (row_col or "").split() else ""
    ids = filed_row_ids() if known_ids is None else known_ids
    if row_id not in ids:
        raise SystemExit("%s:%d: proof=console: on an UNFILED row %r (DT-7 books FILED §7.1 rows only; "
                         "diff_legs.known_row_ids does not resolve it) -- file the row first" % (path, n, row_id))


def resolve_console_path(path):
    """An absolute path as is; else the first of <repo>/<path>, <repo>/../<path> that exists (None when none)."""
    if os.path.isabs(path):
        return path if os.path.isfile(path) else None
    for root in (REPO_ROOT, os.path.dirname(REPO_ROOT)):
        cand = os.path.join(root, path)
        if os.path.isfile(cand):
            return cand
    return None


def default_reloc_rules():
    """Every structural class (relocate_compare / slab_walk / spad_stack) -> (ledger class, row, kind OPEN)
    until the TSV's @reloc-class directive names the row the evidence supports."""
    out = {c: (c.upper(), "OPEN-RELOC-" + c, "OPEN") for c in rc.CLASSES if c != "other"}
    out.update({c: (c.upper(), "OPEN-SLAB-" + c, "OPEN") for c in slab_walk.CLASSES if c != "other"})
    out.update({c: (c.upper(), "OPEN-SPAD-" + c, "OPEN") for c in spad_stack.CLASSES})
    return out
WATCH_RE = re.compile(r"^watch=(0x[0-9a-f]+):w(\d+):b(\d+):([A-Za-z_0-9]+(?:\+0x[0-9a-f]+)?):")


class Rule:
    __slots__ = ("lo", "hi", "spad", "writer", "cls", "row", "kind", "legs", "line", "proof")

    def __init__(self, lo, hi, spad, writer, cls, row, kind, legs, line, proof=None):
        self.lo, self.hi, self.spad, self.writer, self.cls, self.row, self.kind, self.legs, self.line, self.proof = \
            lo, hi, spad, writer, cls, row, kind, legs, line, proof

    def admits_leg(self, leg):
        return self.legs == ["*"] or leg in self.legs


def load_rules(path, row_cap_words=ROW_CAP_WORDS, known_ids=None):
    """-> (rules, directives, demoted). Directives: `@heap 0x..` / `@pool 0x..` / `@pool-used 0x..`
    (the title's allocator addresses -- title data lives in the TSV, not in this code) and
    `@reloc-class <reloc class> <ledger class> <row-or-DP> <kind>`. `demoted` lists the ROW/DP rules
    demoted to MIXED (wider than the cap, no writer regex, no `recorded:` token). A `proof=console:` token
    (ROW rule or @reloc-class directive) is admitted only on a FILED row id (`known_ids`, default the docs'
    `diff_legs.known_row_ids()`): an unfiled id is refused (DT-7's FILED condition, enforced here)."""
    rules = []
    directives = {"heap": None, "pool": None, "pool-used": None, "slabpool": None, "spad-fill": None, "spad-text": None,
                  "spad-decoder-ctx": None, "spad-decoder-text": None, "reloc": default_reloc_rules()}
    demoted = []
    with open(path) as f:
        for n, raw in enumerate(f, 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            if line.startswith("@"):
                key, _, val = line[1:].partition(" ")
                if key not in DIRECTIVES:
                    raise SystemExit("%s:%d: unknown directive @%s" % (path, n, key))
                if key == "reloc-class":
                    parts = val.split()
                    proof_tok = [t for t in parts if t.startswith("proof=")]
                    parts = [t for t in parts if not t.startswith("proof=")]
                    proof = parse_proof(" ".join(proof_tok)) if proof_tok else None
                    if proof_tok and proof is None:
                        raise SystemExit("%s:%d: proof token must be proof=console:<path>@<leg>:b<N>" % (path, n))
                    if len(parts) not in (4, 5) or parts[0] not in STRUCTURAL_CLASSES or parts[3] not in KINDS[:-1]:
                        raise SystemExit("%s:%d: @reloc-class <structural class> <ledger class> <row-or-DP> <kind> [legs] [proof=console:...]" % (path, n))
                    if proof is not None:
                        if parts[3] != "ROW":
                            raise SystemExit("%s:%d: proof=console: is admitted on a ROW directive only (kind %s)" % (path, n, parts[3]))
                        require_filed(parts[2], path, n, known_ids)
                    # an optional 5th token scopes the booking to the legs whose evidence proved it (`*` or a comma list);
                    # on any other leg the structural class stays at its OPEN default. Several directives may name the
                    # same structural class with disjoint leg scopes (one row per leg family): they accumulate in file
                    # order and `attribute` takes the FIRST whose scope admits the leg.
                    legs = [x.strip() for x in parts[4].split(",")] if len(parts) == 5 else ["*"]
                    cur = directives["reloc"][parts[0]]
                    directives["reloc"][parts[0]] = (cur if isinstance(cur, list) else []) + [(parts[1], parts[2], parts[3], legs, proof)]
                elif key == "slabpool":
                    parts = val.split()
                    if len(parts) != 2:
                        raise SystemExit("%s:%d: @slabpool <header addr> <classes>" % (path, n))
                    directives[key] = (int(parts[0], 16), int(parts[1]))
                elif key in ("spad-text", "spad-decoder-text"):
                    lo, _, hi = val.strip().partition("-")
                    directives[key] = (int(lo, 16), int(hi, 16))
                elif key == "spad-decoder-ctx":
                    directives[key] = spad_stack.parse_ctx_spec(val.strip())
                else:
                    directives[key] = None if val.strip() == "none" else int(val.strip(), 16)
                continue
            cols = [c.strip() for c in (line.split("\t") if "\t" in line else line.split("|"))]
            if len(cols) != 6:
                raise SystemExit("%s:%d: expected 6 columns, got %d" % (path, n, len(cols)))
            rng, writer, cls, row, kind, legs = cols
            if kind not in RULE_KINDS:
                raise SystemExit("%s:%d: kind %r not in %r" % (path, n, kind, RULE_KINDS))
            spad = rng.startswith("SPAD:")
            rng = rng[5:] if spad else rng
            if "-" in rng:
                lo, hi = (int(x, 16) for x in rng.split("-", 1))
            else:
                lo = int(rng, 16)
                hi = lo + 4
            if spad != (SPAD_BASE <= lo < SPAD_BASE + 0x4000):
                raise SystemExit("%s:%d: SPAD prefix and address range disagree" % (path, n))
            if kind in ("ROW", "DP") and (hi - lo) // 4 > row_cap_words and not writer and not RECORDED_RE.search(row):
                demoted.append({"line": n, "range": "%s-0x%x" % (rng.split("-")[0], hi), "class": cls, "row": row, "words": (hi - lo) // 4})
                row = "DEMOTED-%s(%s)" % (kind, row)
                kind = "MIXED"
            proof = parse_proof(row) if kind == "ROW" else None
            if "proof=" in row and proof is None:
                raise SystemExit("%s:%d: proof token must be proof=console:<path>@<leg>:b<N> on a ROW rule" % (path, n))
            if proof is not None:
                require_filed(row, path, n, known_ids)
            rules.append(Rule(lo, hi, spad, re.compile(writer) if writer else None, cls, row, kind,
                              [x.strip() for x in legs.split(",")], n, proof))
    return rules, directives, demoted


def load_watch(path):
    """{addr: set(writers)} from a WordWatch census; a store names every word it touches (8-byte: +4; sub-word: its word)."""
    out = {}
    if not path:
        return out
    with open(path, errors="replace") as f:
        for line in f:
            m = WATCH_RE.match(line.strip())
            if not m:
                continue
            a, width, writer = int(m.group(1), 16), int(m.group(2)), m.group(4)
            lo, hi = a & ~3, (a + max(1, width) - 1) & ~3   # a sub-word store names the word(s) it touches
            for w in range(lo, hi + 4, 4):
                out.setdefault(w, set()).add(writer)
    return out


def differing_words(old_img, new_img, mask_rows):
    """[(guest_addr, old, new, lane)] outside the mask; the list is built once per lane."""
    out = []
    for lane, a, b, base in (("RAM0", old_img.ram, new_img.ram, 0), ("SPAD", old_img.spad, new_img.spad, SPAD_BASE)):
        n = min(len(a), len(b))
        chunk = 65536
        for off in range(0, n, chunk):
            ca, cb = a[off:off + chunk], b[off:off + chunk]
            if ca == cb:
                continue
            m = min(len(ca), len(cb)) // 4
            wa = struct.unpack_from("<%dI" % m, ca)
            wb = struct.unpack_from("<%dI" % m, cb)
            for i in range(m):
                if wa[i] != wb[i]:
                    x = base + off + i * 4
                    if any(lo <= x < hi for lo, hi, _ in mask_rows):
                        continue
                    out.append((x, wa[i], wb[i], lane))
    return out


def _first_rule(x, rules, leg, watch, mixed):
    for r in rules:
        if (r.kind == "MIXED") != mixed or not (r.lo <= x < r.hi) or not r.admits_leg(leg):
            continue
        if r.writer is not None:
            writers = watch.get(x)
            if not writers or not any(r.writer.search(w) for w in writers):
                continue
        return r
    return None


def _reloc_rule_for(reloc_rules, structural_class, leg):
    """The (ledger class, row, kind) a structural class books on `leg`: the first @reloc-class directive whose leg
    scope admits the leg, else the class's OPEN default (a directive's legs that do not admit this leg never book)."""
    rr = reloc_rules[structural_class]
    if isinstance(rr, list):
        for cand in rr:
            if cand[3] == ["*"] or leg in cand[3]:
                return cand
        return default_reloc_rules()[structural_class]
    if len(rr) == 4 and not (rr[3] == ["*"] or leg in rr[3]):
        return default_reloc_rules()[structural_class]
    return rr


def attribute(words, rules, leg, watch, reloc_classes, reloc_rules=None):
    """-> per-word list [(addr, old, new, lane, cls, row, kind)]. Order: the TSV's exact rules
    (ROW/DP/OPEN) -> relocate_compare's class (through `reloc_rules`) -> the TSV's MIXED region rules -> UNATTRIBUTED."""
    rows = []
    reloc_rules = reloc_rules or default_reloc_rules()
    for x, vo, vn, lane in words:
        cls = row = kind = proof = None
        r = _first_rule(x, rules, leg, watch, mixed=False)
        if r is not None:
            cls, row, kind, proof = r.cls, r.row, r.kind, r.proof
        if cls is None and reloc_classes is not None and reloc_classes.get(x, "other") != "other":
            rr = _reloc_rule_for(reloc_rules, reloc_classes[x], leg)
            cls, row, kind = rr[0], rr[1], rr[2]
            proof = rr[4] if len(rr) >= 5 else None
        if cls is None:
            r = _first_rule(x, rules, leg, watch, mixed=True)
            if r is not None:
                cls, row, kind = r.cls, r.row, r.kind
        if cls is None:
            cls, row, kind = "UNATTRIBUTED", "-", "UNATTRIBUTED"
        rows.append((x, vo, vn, lane, cls, row, kind, proof))
    return rows


class ConsoleImages:
    """The raw EE images named by proofs, read once each (32 MiB RAM dumps: offset = guest address)."""

    def __init__(self):
        self.blobs = {}

    def word(self, path, x):
        if path not in self.blobs:
            real = resolve_console_path(path)
            self.blobs[path] = open(real, "rb").read() if real else None
        blob = self.blobs[path]
        if blob is None or x + 4 > len(blob) or x >= RAM_WINDOW:
            return None
        return struct.unpack_from("<I", blob, x)[0]


def console_booking(rows, leg, boundary, images=None):
    """DT-7: over the attributed rows of pair (leg, boundary), the ROW words whose rule carries a proof for THIS pair
    and whose console word == NEW != OLD. Returns {'booked_console', 'proof_rows', 'console_ne_new', 'console_eq_old',
    'unreadable', 'by_class': {cls: booked}, 'images': [paths]}."""
    images = images or ConsoleImages()
    out = {"booked_console": 0, "proof_rows": 0, "console_ne_new": 0, "console_eq_old": 0, "unreadable": 0, "by_class": {}, "images": []}
    # console_eq_old: the console holds OLD's word (NEW deviates); console_ne_new: the console agrees with neither side
    if boundary is None:
        return out
    for x, vo, vn, lane, cls, row, kind, proof in rows:
        if kind != "ROW" or proof is None or proof[1] != leg or proof[2] != int(boundary):
            continue
        out["proof_rows"] += 1
        if proof[0] not in out["images"]:
            out["images"].append(proof[0])
        con = None if lane != "RAM0" else images.word(proof[0], x)
        if con is None:
            out["unreadable"] += 1
        elif con == vn and con != vo:
            out["booked_console"] += 1
            out["by_class"][cls] = out["by_class"].get(cls, 0) + 1
        elif con == vo:
            out["console_eq_old"] += 1      # OLD holds the console's word: NEW is the deviant here
        else:
            out["console_ne_new"] += 1      # the console agrees with neither side
    return out


def proved_rules_for(rules, directives, leg, boundary):
    """(exact ROW rules, reloc directives) carrying a console proof for pair (leg, boundary)."""
    exact = [r for r in rules if r.kind == "ROW" and r.proof and r.proof[1] == leg and r.proof[2] == int(boundary) and r.admits_leg(leg)]
    reloc = []
    for cands in directives["reloc"].values():
        for c in (cands if isinstance(cands, list) else []):
            if len(c) >= 5 and c[4] and c[4][1] == leg and c[4][2] == int(boundary) and (c[3] == ["*"] or leg in c[3]):
                reloc.append(c)
    return exact, reloc


def booked_console_only(old_path, new_path, classes, mask, leg, boundary, row_cap_words=ROW_CAP_WORDS, known_ids=None):
    """The DT-7 count for one pair without the full ledger: 0 at once when no rule carries a proof for the pair; else
    only the differing words INSIDE the proved exact rules' ranges go through `attribute` (an earlier rule may still
    take a word first -- the order is the full one). A proved @reloc-class directive needs the structural walk: the
    full `run(..., reloc=True)` is used then. Returns console_booking()'s dict plus 'method'."""
    rules, directives, demoted = load_rules(classes, row_cap_words, known_ids)
    exact, reloc = proved_rules_for(rules, directives, leg, boundary)
    if not exact and not reloc:
        out = console_booking([], leg, boundary)
        out["method"] = "no console-proved rule for %s b%s" % (leg, boundary)
        return out
    if reloc:
        res, rows = run(old_path, new_path, classes, mask, leg, None, True, None, None, 0, None, row_cap_words, boundary=boundary, known_ids=known_ids)
        out = dict(res["summary"]["console"])
        out["method"] = "full ledger with the structural walk (a proved @reloc-class directive)"
        return out
    old_img, new_img = rc.Image(old_path), rc.Image(new_path)
    mask_rows = rc.read_mask_ranges(mask)
    words = [w for w in differing_words(old_img, new_img, mask_rows) if any(r.lo <= w[0] < r.hi for r in exact)]
    rows = attribute(words, rules, leg, {}, None, directives["reloc"])
    out = console_booking(rows, leg, boundary)
    out["method"] = "exact proved rules only (%d words inside their ranges)" % len(words)
    return out


def _file_md5(path):
    import hashlib
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def summarize(rows):
    by_class = {}
    by_kind = {k: 0 for k in KINDS}
    for x, vo, vn, lane, cls, row, kind, *_ in rows:
        e = by_class.setdefault(cls, {"class": cls, "row": row, "kind": kind, "words": 0, "RAM0": 0, "SPAD": 0,
                                      "first": "0x%x" % x, "last": "0x%x" % x})
        e["words"] += 1
        e[lane] += 1
        e["last"] = "0x%x" % x
        by_kind[kind] += 1
    total = len(rows)
    verdict = "ATTRIBUTED-100%" if total and by_kind["OPEN"] == 0 and by_kind["MIXED"] == 0 and by_kind["UNATTRIBUTED"] == 0 \
        and by_kind["NONSTATE"] == 0 else ("IDENTICAL" if total == 0 else "BLOCKED")
    return {"total": total, "by_kind": by_kind, "not_attributed": by_kind["MIXED"] + by_kind["UNATTRIBUTED"],
            "attributed_row_dp": by_kind["ROW"] + by_kind["DP"], "nonstate_proved": by_kind["NONSTATE"],
            "by_class": sorted(by_class.values(), key=lambda e: -e["words"]), "verdict": verdict}


def render(leg, boundary, summary, rows, unattributed_cap, demoted=()):
    out = ["attribute_probe: %s boundary %s: %d differing words outside the mask -> %s" % (leg, boundary, summary["total"], summary["verdict"])]
    out.append("  kinds: " + " ".join("%s=%d" % (k, summary["by_kind"][k]) for k in KINDS)
               + "  | attributed (ROW+DP)=%d not-attributed (MIXED+UNATTRIBUTED)=%d OPEN=%d NONSTATE(proved, DT-3 column)=%d"
               % (summary["attributed_row_dp"], summary["not_attributed"], summary["by_kind"]["OPEN"], summary["by_kind"]["NONSTATE"]))
    c = summary.get("console")
    if c is not None:
        out.append("  DT-7: booked_console=%d (ROW words with proof=console for this pair where console == NEW != OLD; "
                   "proof rows %d, console == OLD %d, console == neither %d, unreadable %d; images %s) -> words_unbooked=%d"
                   % (c["booked_console"], c["proof_rows"], c["console_eq_old"], c["console_ne_new"], c["unreadable"],
                      ",".join(c["images"]) or "-", summary["total"] - c["booked_console"]))
    for d in demoted:
        out.append("  demoted to MIXED (region rule without a recorded word): line %d %s %s (%d words) row %s" % (d["line"], d["range"], d["class"], d["words"], d["row"]))
    out.append("  %-28s %-6s %7s %7s %7s  %-10s  %s" % ("class", "kind", "words", "RAM0", "SPAD", "first", "row / DP"))
    for e in summary["by_class"]:
        out.append("  %-28s %-6s %7d %7d %7d  %-10s  %s" % (e["class"], e["kind"], e["words"], e["RAM0"], e["SPAD"], e["first"], e["row"]))
    un = [r for r in rows if r[4] == "UNATTRIBUTED"]
    if un:
        cap = len(un) if unattributed_cap < 0 else unattributed_cap
        out.append("  UNATTRIBUTED words (%d; listed %d):" % (len(un), min(len(un), cap)))
        for x, vo, vn, lane, *_ in un[:cap]:
            out.append("    %s 0x%08x old=0x%08x new=0x%08x" % (lane, x, vo, vn))
    return "\n".join(out)


def run(old_path, new_path, classes, mask, leg, watch_path, reloc, heap, pool, unattributed_cap=40, old_trace=None,
        row_cap_words=ROW_CAP_WORDS, boundary=None, known_ids=None):
    old_img, new_img = rc.Image(old_path), rc.Image(new_path)
    mask_rows = rc.read_mask_ranges(mask)
    rules, directives, demoted = load_rules(classes, row_cap_words, known_ids)
    heap = directives["heap"] if heap is None else heap
    pool = directives["pool"] if pool is None else pool
    watch = load_watch(watch_path)
    reloc_classes = None
    reloc_res = None
    slab_res = spad_res = None
    if reloc:
        if heap is None and pool is None and directives["slabpool"] is None and directives["spad-fill"] is None:
            raise SystemExit("--reloc needs @heap / @pool / @slabpool / @spad-fill directives in the classes TSV")
        reloc_classes = {}
        if heap is not None or pool is not None:
            head = old_img.w(pool + 0x50) if pool is not None else None
            reloc_res = rc.compare(old_img, new_img, heap, pool, head, mask_rows, list_cap=0, collect=reloc_classes,
                                   pool_used=directives["pool-used"], old_trace=old_trace)
        if directives["slabpool"] is not None:
            # the page pool's words are "other" to relocate_compare (outside its arenas): slab_walk classes them
            arenas = []
            if reloc_res is not None:
                for side in ("heap", "pool"):
                    c = reloc_res["census"].get(side)
                    if c and side == "heap":
                        arenas.append((c["old"]["arena_lo"], c["old"]["arena_hi"]))
                    elif c:
                        arenas.append((c["old"]["base"], c["old"]["base"] + c["old"]["size"]))
            slab_classes = {}
            slab_res = slab_walk.compare(old_img, new_img, directives["slabpool"][0], directives["slabpool"][1], mask_rows, arenas,
                                         list_cap=0, collect=slab_classes)
            for x, cls in slab_classes.items():
                if cls != "other" and reloc_classes.get(x, "other") == "other":
                    reloc_classes[x] = cls
        if directives["spad-fill"] is not None:
            spad_classes = {}
            spad_res = spad_stack.compare(old_path, new_path, old_img.spad, new_img.spad, directives["spad-fill"], collect=spad_classes,
                                          text=directives["spad-text"], decoder_ctx=directives["spad-decoder-ctx"],
                                          decoder_text=directives["spad-decoder-text"])
            reloc_classes.update(spad_classes)
    words = differing_words(old_img, new_img, mask_rows)
    rows = attribute(words, rules, leg, watch, reloc_classes, directives["reloc"])
    summary = summarize(rows)
    if boundary is not None and str(boundary).isdigit():
        summary["console"] = console_booking(rows, leg, int(boundary))
    result = {"leg": leg, "old": old_path, "new": new_path, "old_vsync_frame": old_img.vf, "new_vsync_frame": new_img.vf,
              # the digests bind the ledger to the two images it attributed (apply_rebaseline.py refuses any other image)
              "old_md5": _file_md5(old_path), "new_md5": _file_md5(new_path),
              "summary": summary, "demoted_rules": demoted,
              "unattributed": [{"lane": r[3], "addr": "0x%x" % r[0], "old": "0x%x" % r[1], "new": "0x%x" % r[2]}
                               for r in rows if r[4] == "UNATTRIBUTED"],
              "reloc": None if reloc_res is None else {"census": reloc_res["census"], "records": reloc_res["records"],
                                                        "first_divergence": reloc_res["first_divergence"], "words": reloc_res["words"]},
              "slab": None if slab_res is None else {"pool": slab_res["pool"], "words": slab_res["words"]},
              "spad": None if spad_res is None else {k: spad_res[k] for k in ("old_sp", "new_sp", "sp_off", "live_end", "fill_run", "first_written_above_fill_run", "written_span_reach_below_sp",
                                                                              "chain_old", "chain_new", "threads_old", "threads_new", "conditions",
                                                                              "dead_evidence", "words")}}
    return result, rows


# ---------------------------------------------------------------- selftest

def selftest():
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)
    tmp = tempfile.mkdtemp(prefix="attr_selftest_")
    ram_o, ram_n = bytearray(0x4000), bytearray(0x4000)
    spad_o, spad_n = bytearray(0x4000), bytearray(0x4000)
    struct.pack_into("<I", ram_o, 0x100, 1); struct.pack_into("<I", ram_n, 0x100, 2)      # rule word (ROW)
    struct.pack_into("<I", ram_o, 0x200, 1); struct.pack_into("<I", ram_n, 0x200, 2)      # writer-gated rule
    struct.pack_into("<I", ram_o, 0x204, 1); struct.pack_into("<I", ram_n, 0x204, 2)      # same range, writer not named -> region rule
    struct.pack_into("<I", ram_o, 0x300, 1); struct.pack_into("<I", ram_n, 0x300, 2)      # masked
    struct.pack_into("<I", ram_o, 0x400, 1); struct.pack_into("<I", ram_n, 0x400, 2)      # coldboot-only rule, leg artdet -> UNATTRIBUTED
    struct.pack_into("<I", spad_o, 0x3f00, 1); struct.pack_into("<I", spad_n, 0x3f00, 2)  # SPAD rule (MIXED)
    import zlib

    def write(path, ram, spad):
        body = b"PS2SAVE1" + struct.pack("<IIIIQ", 4, 3, 2, 0, 0)
        for tag, payload in ((state_hash.TAG_RAM, bytes(ram)), (state_hash.TAG_SPAD, bytes(spad)), (state_hash.TAG_META, bytes(64))):
            body += struct.pack("<IIIIQ", tag, 1, 0, zlib.crc32(payload) & 0xFFFFFFFF, len(payload)) + payload
        body += struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)
        with open(path, "wb") as f:
            f.write(body)
    po, pn = os.path.join(tmp, "old.ps2st"), os.path.join(tmp, "new.ps2st")
    write(po, ram_o, spad_o)
    write(pn, ram_n, spad_n)
    tsv = os.path.join(tmp, "classes.tsv")
    with open(tsv, "w") as f:
        f.write("# test rules\n"
                "0x100||A-WORD|P9-TEST-ROW|ROW|*\n"
                "0x200-0x208|W_00001234|B-WRITER|P9-TEST-ROW|ROW|*\n"
                "0x200-0x208||B-REGION|OPEN-TEST|MIXED|*\n"
                "0x400||C-COLDBOOT|P9-TEST-ROW|ROW|coldboot\n"
                "SPAD:0x70003000-0x70004000||SPAD-STACK|P4-CKPT0-STATICS-class|MIXED|*\n")
    mask = os.path.join(tmp, "mask.ranges")
    with open(mask, "w") as f:
        f.write("0x300-0x304  P9-TEST-MASK  # test\n")
    watch = os.path.join(tmp, "watch.log")
    with open(watch, "w") as f:
        f.write("watch=0x200:w4:b1:W_00001234+0x10:0x1->0x2\n")
    res, rows = run(po, pn, tsv, mask, "artdet_f37152", watch, False, None, None)
    s = res["summary"]
    check(s["total"] == 5, "5 differing words outside the mask (the masked word skipped) -> %d" % s["total"])
    classes = {e["class"]: e for e in s["by_class"]}
    check(classes["A-WORD"]["words"] == 1 and classes["A-WORD"]["kind"] == "ROW", "exact-word rule -> A-WORD ROW")
    check(classes["B-WRITER"]["words"] == 1 and classes["B-REGION"]["words"] == 1,
          "writer-gated rule takes the censused word, the region rule the other -> %r" % {k: v["words"] for k, v in classes.items()})
    check(classes["SPAD-STACK"]["words"] == 1 and classes["SPAD-STACK"]["SPAD"] == 1, "SPAD rule by guest address")
    check(classes["UNATTRIBUTED"]["words"] == 1 and res["unattributed"][0]["addr"] == "0x400",
          "the coldboot-only rule does not apply to artdet -> UNATTRIBUTED 0x400")
    check(s["verdict"] == "BLOCKED", "verdict BLOCKED with MIXED/UNATTRIBUTED words -> %s" % s["verdict"])
    res2, _ = run(po, pn, tsv, mask, "coldboot", watch, False, None, None)
    check({e["class"] for e in res2["summary"]["by_class"]} >= {"C-COLDBOOT"}, "leg column: coldboot admits the rule")
    res3, _ = run(po, po, tsv, mask, "coldboot", None, False, None, None)
    check(res3["summary"]["verdict"] == "IDENTICAL" and res3["summary"]["total"] == 0, "an image against itself -> IDENTICAL")
    txt = render("artdet_f37152", 2, s, rows, 10)
    check("UNATTRIBUTED words (1" in txt and "-> BLOCKED" in txt, "render lists the UNATTRIBUTED words and the verdict")
    check(s["not_attributed"] == 3 and s["attributed_row_dp"] == 2, "not_attributed = MIXED+UNATTRIBUTED (3), attributed = ROW+DP (2)")
    # --- the honesty rules: (a) a wide ROW region rule without a recorded word is demoted to MIXED;
    #     a `recorded:` token or a writer regex keeps it
    tsv2 = os.path.join(tmp, "classes2.tsv")
    with open(tsv2, "w") as f:
        f.write("0x100-0x200||WIDE-NO-RECORD|P9-TEST-ROW|ROW|*\n"
                "0x400-0x500||WIDE-RECORDED|P9-TEST-ROW recorded:DP-99(4)|ROW|*\n"
                "0x600-0x700|W_0000|WIDE-WRITER|P9-TEST-ROW|ROW|*\n"
                "0x200||ONE-WORD|P9-TEST-ROW|ROW|*\n")
    rules2, _, demoted = load_rules(tsv2, row_cap_words=8)
    check([d["class"] for d in demoted] == ["WIDE-NO-RECORD"] and rules2[0].kind == "MIXED" and rules2[0].row.startswith("DEMOTED-ROW("),
          "a ROW region rule wider than the cap with no record is demoted to MIXED -> %r" % [(r.cls, r.kind) for r in rules2])
    check(rules2[1].kind == "ROW" and rules2[2].kind == "ROW" and rules2[3].kind == "ROW", "recorded:/writer/one-word ROW rules keep their kind")
    res4, rows4 = run(po, pn, tsv2, mask, "artdet_f37152", None, False, None, None, row_cap_words=8)
    c4 = {e["class"]: e for e in res4["summary"]["by_class"]}
    check(c4["WIDE-NO-RECORD"]["kind"] == "MIXED" and res4["demoted_rules"][0]["words"] == 64,
          "the demoted rule classes its words MIXED and is reported -> %r" % res4["demoted_rules"])
    # --- (b) relocation classes: a NEW-only record is a class of its own, kind OPEN by default;
    #     the @reloc-class directive is the only way to book a relocation class to a row
    ram_o2, ram_n2 = bytearray(0x20000), bytearray(0x20000)
    meta_o2, meta_n2 = bytearray(0x2000), bytearray(0x2000)
    HEAP, POOL, USED = 0x1100, 0x1200, 0x1300
    rc._build(ram_o2, HEAP, [(0x40, False), (0x40, True)])
    rc._build(ram_n2, HEAP, [(0x40, False), (0x40, True)])
    for k in range(0, 0x100, 4):
        struct.pack_into("<I", ram_o2, 0x8000 + k, 0xa0000 + k)   # OLD record A (idx 1)
        struct.pack_into("<I", ram_n2, 0x8100 + k, 0xa0000 + k)   # A moved in NEW
        struct.pack_into("<I", ram_n2, 0x8000 + k, 0xb0000 + k)   # NEW-only record N (idx 7) where A was
    struct.pack_into("<I", ram_o2, 0x8200, 0x11); struct.pack_into("<I", ram_n2, 0x8200, 0x22)  # free chunk both sides
    rc._pool(ram_o2, meta_o2, POOL, 0x8000, 0x8000, [(0x8000, 0x100, 1, False), (0x8100, 0x100, 2, False), (0x8200, 0x100, 0, True)], USED)
    rc._pool(ram_n2, meta_n2, POOL, 0x8000, 0x8000, [(0x8000, 0x100, 7, False), (0x8100, 0x100, 1, False), (0x8200, 0x100, 0, True)], USED)
    for k in range(0, 0x100, 4):
        struct.pack_into("<I", ram_o2, 0x8100 + k, 0xc0000 + k)   # OLD record B (idx 2): OLD-only (NEW has no idx 2)
    po2, pn2 = os.path.join(tmp, "old2.ps2st"), os.path.join(tmp, "new2.ps2st")
    rc._synthetic(po2, bytes(ram_o2), bytes(meta_o2), 120)
    rc._synthetic(pn2, bytes(ram_n2), bytes(meta_n2), 120)
    tsv3 = os.path.join(tmp, "classes3.tsv")
    with open(tsv3, "w") as f:
        f.write("@heap 0x%x\n@pool 0x%x\n@pool-used 0x%x\n" % (HEAP, POOL, USED))
    res5, rows5 = run(po2, pn2, tsv3, mask, "coldboot", None, True, None, None)
    c5 = {e["class"]: e for e in res5["summary"]["by_class"]}
    check(c5.get("NEW-ONLY-RECORD", {}).get("kind") == "OPEN" and c5["NEW-ONLY-RECORD"]["words"] == 0x40,
          "a NEW-only record over a matched (moved) OLD record -> its own class NEW-ONLY-RECORD, kind OPEN (no row by fiat) -> %r"
          % {k: (v["kind"], v["words"]) for k, v in c5.items()})
    check(c5.get("OLD-ONLY-RECORD", {}).get("words") == 0x40, "the OLD-only record under NEW's moved copy -> OLD-ONLY-RECORD 0x40")
    check(c5.get("FREE-RESIDUE", {}).get("kind") == "OPEN" and c5["FREE-RESIDUE"]["words"] == 1, "a free chunk's stale word -> FREE-RESIDUE, OPEN")
    check("RELOC-CONTENT" not in c5, "A's words are explained on both sides (A moved, N sits at A's old place: only-records) -> no reloc-content word here")
    check(res5["summary"]["verdict"] == "BLOCKED" and res5["summary"]["by_kind"]["ROW"] == 0, "nothing is ROW without a directive -> BLOCKED, ROW=0")
    with open(tsv3, "a") as f:
        f.write("@reloc-class free-residue HEAP-FREE-RESIDUE P9-TEST-ROW ROW\n")
    res6, _ = run(po2, pn2, tsv3, mask, "coldboot", None, True, None, None)
    c6 = {e["class"]: e for e in res6["summary"]["by_class"]}
    check(c6["HEAP-FREE-RESIDUE"]["kind"] == "ROW" and c6["HEAP-FREE-RESIDUE"]["row"] == "P9-TEST-ROW" and c6["NEW-ONLY-RECORD"]["kind"] == "OPEN",
          "@reloc-class books ONE relocation class to a row; the others stay OPEN")
    tsv3b = os.path.join(tmp, "classes3b.tsv")
    with open(tsv3b, "w") as f:
        f.write("@heap 0x%x\n@pool 0x%x\n@pool-used 0x%x\n@reloc-class free-residue HEAP-FREE-RESIDUE P9-TEST-ROW ROW coldboot,artdet_f37152\n" % (HEAP, POOL, USED))
    res6b, _ = run(po2, pn2, tsv3b, mask, "coldboot", None, True, None, None)
    res6c, _ = run(po2, pn2, tsv3b, mask, "coldboot_attract", None, True, None, None)
    c6b = {e["class"]: e for e in res6b["summary"]["by_class"]}
    c6c = {e["class"]: e for e in res6c["summary"]["by_class"]}
    check(c6b["HEAP-FREE-RESIDUE"]["kind"] == "ROW" and c6c.get("FREE-RESIDUE", {}).get("kind") == "OPEN" and "HEAP-FREE-RESIDUE" not in c6c,
          "a leg-scoped @reloc-class books only on its legs (coldboot ROW; coldboot_attract stays OPEN FREE-RESIDUE) -> %r / %r"
          % ({k: v["kind"] for k, v in c6b.items()}, {k: v["kind"] for k, v in c6c.items()}))
    # two directives on the SAME structural class with disjoint leg scopes: each leg books its own row (file order
    # decides only when scopes overlap); a directive is never overwritten by a later one for another leg family
    tsv3c = os.path.join(tmp, "classes3c.tsv")
    with open(tsv3c, "w") as f:
        f.write("@heap 0x%x\n@pool 0x%x\n@pool-used 0x%x\n" % (HEAP, POOL, USED))
        f.write("@reloc-class free-residue HEAP-FREE-RESIDUE P9-TEST-ROW-ATTRACT ROW coldboot_attract\n")
        f.write("@reloc-class free-residue HEAP-FREE-RESIDUE P9-TEST-ROW ROW coldboot,artdet_f37152\n")
    res6d, _ = run(po2, pn2, tsv3c, mask, "coldboot", None, True, None, None)
    res6e, _ = run(po2, pn2, tsv3c, mask, "coldboot_attract", None, True, None, None)
    res6f, _ = run(po2, pn2, tsv3c, mask, "f36935", None, True, None, None)
    c6d = {e["class"]: e for e in res6d["summary"]["by_class"]}
    c6e = {e["class"]: e for e in res6e["summary"]["by_class"]}
    c6f = {e["class"]: e for e in res6f["summary"]["by_class"]}
    check(c6d["HEAP-FREE-RESIDUE"]["row"] == "P9-TEST-ROW" and c6e["HEAP-FREE-RESIDUE"]["row"] == "P9-TEST-ROW-ATTRACT"
          and c6d["HEAP-FREE-RESIDUE"]["kind"] == "ROW" and c6e["HEAP-FREE-RESIDUE"]["kind"] == "ROW",
          "two @reloc-class directives on one structural class with disjoint scopes: coldboot -> P9-TEST-ROW, coldboot_attract -> "
          "P9-TEST-ROW-ATTRACT (the later directive does not overwrite the earlier) -> %r / %r"
          % (c6d["HEAP-FREE-RESIDUE"]["row"], c6e["HEAP-FREE-RESIDUE"]["row"]))
    check(c6f.get("FREE-RESIDUE", {}).get("kind") == "OPEN" and "HEAP-FREE-RESIDUE" not in c6f,
          "a leg in neither scope stays at the OPEN default")
    # --- (c) the page pool (slab_walk) and the scratchpad stack (spad_stack) through the same directive path;
    #     the NONSTATE kind is a column of its own and never makes a window ATTRIBUTED-100%
    P9, NC, DESC9, CTAB9, PAGES9, NP9 = 0x1000, 2, 0x2000, 0x2100, 0x10000, 3
    ram_o3, ram_n3 = bytearray(0x14000), bytearray(0x14000)
    slab_walk._build_pool(ram_o3, P9, NC, DESC9, CTAB9, PAGES9, NP9, {0: (1, 1024, {1, 2, 3})})
    slab_walk._build_pool(ram_n3, P9, NC, DESC9, CTAB9, PAGES9, NP9, {0: (1, 1024, {2, 3})})
    struct.pack_into("<I", ram_n3, PAGES9 + 1024 + 0x10, 0x77)          # NEW-only object word (OLD: free chunk)
    struct.pack_into("<I", ram_o3, PAGES9 + 2048 + 0x20, 0x1); struct.pack_into("<I", ram_n3, PAGES9 + 2048 + 0x20, 0x2)  # free both
    spad_o3, spad_n3 = bytearray(0x4000), bytearray(0x4000)
    for o in range(0, 0x4000, 4):
        struct.pack_into("<I", spad_o3, o, 0xa1b2c3d4); struct.pack_into("<I", spad_n3, o, 0xa1b2c3d4)
    for o in range(0x220, 0x270, 4):
        struct.pack_into("<I", spad_o3, o, 7); struct.pack_into("<I", spad_n3, o, 7)
    struct.pack_into("<I", spad_o3, 0x100, 1); struct.pack_into("<I", spad_n3, 0x100, 2)      # dead below sp
    struct.pack_into("<I", spad_o3, 0x240, 1); struct.pack_into("<I", spad_n3, 0x240, 2)      # live frame
    po3, pn3 = os.path.join(tmp, "o3.ps2st"), os.path.join(tmp, "n3.ps2st")
    spad_stack._state(po3, spad_o3, 0x70004220, [(0x5a0150, 0x59fd6c)])
    spad_stack._state(pn3, spad_n3, 0x70004220, [(0x5a0150, 0x59fd6c)])
    # splice the RAM into the THRD-carrying files: rebuild with both
    import zlib

    def write3(path, ram, spad, sp):
        _state_bytes = open(path, "rb").read()
        _, _, secs = state_hash.parse(path)
        thrd, dmac = secs[spad_stack.TAG_THRD][2], secs[spad_stack.TAG_DMAC][2]   # DMAC kept: C3 fails closed without it
        body = b"PS2SAVE1" + struct.pack("<IIIIQ", 4, 5, 2, 0, 0)
        for tag, payload in ((state_hash.TAG_RAM, bytes(ram)), (state_hash.TAG_SPAD, bytes(spad)), (state_hash.TAG_META, bytes(64)), (spad_stack.TAG_THRD, thrd),
                             (spad_stack.TAG_DMAC, dmac)):
            body += struct.pack("<IIIIQ", tag, 1, 0, zlib.crc32(payload) & 0xFFFFFFFF, len(payload)) + payload
        body += struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)
        with open(path, "wb") as f:
            f.write(body)
    write3(po3, ram_o3, spad_o3, 0x70004220)
    write3(pn3, ram_n3, spad_n3, 0x70004220)
    tsv4 = os.path.join(tmp, "classes4.tsv")
    with open(tsv4, "w") as f:
        f.write("@heap none\n@pool none\n@slabpool 0x%x %d\n@spad-fill 0xa1b2c3d4\n@spad-decoder-text 0x5b0000-0x5c0000\n" % (P9, NC))
    res7, _ = run(po3, pn3, tsv4, mask, "artdet_f37152", None, True, None, None)
    c7 = {e["class"]: e for e in res7["summary"]["by_class"]}
    check(c7.get("SLAB-ALLOC-DIFFERS", {}).get("kind") == "OPEN" and c7["SLAB-ALLOC-DIFFERS"]["words"] == 2
          and c7.get("SLAB-FREE-RESIDUE", {}).get("words") == 1,
          "page-pool words classed through @slabpool: alloc-differs 2 (the object word + OLD's free-chunk link under NEW's object), free-residue 1, kind OPEN -> %r" % {k: (v["kind"], v["words"]) for k, v in c7.items()})
    check(c7.get("SPAD-DEAD-BELOW-SP", {}).get("words") == 1 and c7.get("SPAD-LIVE-FRAME", {}).get("words") == 1 and c7["SPAD-DEAD-BELOW-SP"]["kind"] == "OPEN",
          "scratchpad words classed through @spad-fill: dead-below-sp 1, live-frame 1, kind OPEN")
    check(res7["spad"]["sp_off"] == "0x220" and res7["slab"]["pool"]["new"]["used_chunks"] == 2
          and res7["spad"]["conditions"]["C1_sp_in_spad_both_equal"] and res7["spad"]["conditions"]["C3_evaluable"]
          and res7["spad"]["conditions"]["threads_tested_new"] == 1 and "dead_evidence" in res7["spad"],
          "the JSON carries the spad span, its per-pair conditions (C3 evaluated over every thread) and the slab census")
    # without the decoder-text directive C3 is NOT evaluable: nothing is proved dead (fail closed)
    tsv4b = os.path.join(tmp, "classes4b.tsv")
    with open(tsv4b, "w") as f:
        f.write("@heap none\n@pool none\n@slabpool 0x%x %d\n@spad-fill 0xa1b2c3d4\n" % (P9, NC))
    res7b, _ = run(po3, pn3, tsv4b, mask, "artdet_f37152", None, True, None, None)
    c7b = {e["class"]: e for e in res7b["summary"]["by_class"]}
    check(c7b.get("SPAD-SP-UNPROVED", {}).get("words") == 2 and "SPAD-DEAD-BELOW-SP" not in c7b and not res7b["spad"]["conditions"]["C3_evaluable"],
          "no @spad-decoder-text directive: C3 not evaluable -> every SPAD word spad-sp-unproved -> %r" % {k: v["words"] for k, v in c7b.items() if k.startswith("SPAD")})
    with open(tsv4, "a") as f:
        f.write("@reloc-class spad-dead-below-sp DEAD-STACK DT-3-proposed NONSTATE\n")
    res8, _ = run(po3, pn3, tsv4, mask, "artdet_f37152", None, True, None, None)
    s8 = res8["summary"]
    check(s8["by_kind"]["NONSTATE"] == 1 and s8["nonstate_proved"] == 1 and s8["attributed_row_dp"] == 0 and s8["verdict"] == "BLOCKED",
          "NONSTATE is its own column: not attributed, not excluded, the window stays BLOCKED -> %r" % s8["by_kind"])
    check("NONSTATE(proved, DT-3 column)=1" in render("artdet_f37152", 2, s8, [], 0), "render prints the NONSTATE column")
    # --- (d) DT-7: the console-proved booking is checked WORD BY WORD against the console image, on the proof's pair only
    con = bytearray(0x4000)
    struct.pack_into("<I", con, 0x100, 2)   # console == NEW (2) != OLD (1) at the ROW word 0x100 -> booked
    struct.pack_into("<I", con, 0x400, 1)   # console == OLD at 0x400 -> not booked (console_eq_old)
    con_path = os.path.join(tmp, "console_ee.bin")
    with open(con_path, "wb") as f:
        f.write(bytes(con))
    tsv5 = os.path.join(tmp, "classes5.tsv")
    with open(tsv5, "w") as f:
        f.write("0x100||A-WORD|P9-TEST-ROW proof=console:%s@coldboot:b120|ROW|*\n"
                "0x400||C-WORD|P9-TEST-ROW proof=console:%s@coldboot:b120|ROW|*\n"
                "0x200-0x208||B-WORD|P9-TEST-ROW proof=console:%s@coldboot:b60|ROW|*\n"
                "SPAD:0x70003f00||S-WORD|P9-TEST-ROW proof=console:%s@coldboot:b120|ROW|*\n" % ((con_path,) * 4))
    TEST_IDS = {"P9-TEST-ROW"}   # the synthetic row is "filed" for the selftest by this set; the docs do not carry it
    try:
        load_rules(tsv5)
        check(False, "FILED condition: a proof=console: token on a row the docs do not file is REFUSED")
    except SystemExit as e:
        check("UNFILED row 'P9-TEST-ROW'" in str(e), "FILED condition: a proof=console: token on a row the docs do not file is REFUSED -> %s" % e)
    try:
        load_rules(tsv5, known_ids={"P9-OTHER-ROW"})
        check(False, "FILED condition: the row id is the FIRST token of the row column, matched against known_ids")
    except SystemExit:
        check(True, "FILED condition: the row id is the FIRST token of the row column, matched against known_ids")
    res9, rows9 = run(po, pn, tsv5, mask, "coldboot", None, False, None, None, boundary=120, known_ids=TEST_IDS)
    c9 = res9["summary"]["console"]
    check(c9["booked_console"] == 1 and c9["by_class"] == {"A-WORD": 1} and c9["console_eq_old"] == 1 and c9["unreadable"] == 1 and c9["proof_rows"] == 3,
          "DT-7 on the proof's pair (coldboot b120): 0x100 booked (console == NEW != OLD), 0x400 not (console == OLD), the SPAD word unreadable, "
          "the b60-proved rule not counted -> %r" % c9)
    res9b, _ = run(po, pn, tsv5, mask, "coldboot", None, False, None, None, boundary=60, known_ids=TEST_IDS)
    check(res9b["summary"]["console"]["booked_console"] == 0 and res9b["summary"]["console"]["console_ne_new"] == 2,
          "on another pair (b60) the b120 proofs do not apply; the b60-proved words hold what the console (0) does not -> not booked (console == neither)")
    res9c, _ = run(po, pn, tsv5, mask, "coldboot", None, False, None, None, known_ids=TEST_IDS)
    check("console" not in res9c["summary"], "without a boundary no DT-7 booking is computed")
    check("DT-7: booked_console=1" in render("coldboot", 120, res9["summary"], rows9, 0) and "words_unbooked=4" in render("coldboot", 120, res9["summary"], rows9, 0),
          "render prints the DT-7 line with words_unbooked = total - booked (5 - 1)")
    fast = booked_console_only(po, pn, tsv5, mask, "coldboot", 120, known_ids=TEST_IDS)
    check(fast["booked_console"] == 1 and fast["method"].startswith("exact proved rules only (3 words"), "the fast path agrees (exact proved rules only) -> %r" % fast["method"])
    fast2 = booked_console_only(po, pn, tsv5, mask, "artdet_f37152", 2, known_ids=TEST_IDS)
    check(fast2["booked_console"] == 0 and fast2["method"].startswith("no console-proved rule"), "a pair without a proved rule -> 0 at once")
    with open(tsv5, "a") as f:
        f.write("0x500||BAD|P9-TEST-ROW proof=console:nowhere|ROW|*\n")
    try:
        load_rules(tsv5, known_ids=TEST_IDS)
        check(False, "a malformed proof token is rejected")
    except SystemExit:
        check(True, "a malformed proof token is rejected")
    with open(tsv3, "a") as f:
        f.write("@reloc-class genuine GEN P9-TEST-ROW ROW coldboot proof=console:%s@coldboot:b120\n" % con_path)
    try:
        load_rules(tsv3)
        check(False, "FILED condition on a @reloc-class directive: an unfiled row id with a proof is REFUSED")
    except SystemExit as e:
        check("UNFILED row 'P9-TEST-ROW'" in str(e), "FILED condition on a @reloc-class directive: an unfiled row id with a proof is REFUSED -> %s" % e)
    rules_r, dirs_r, _ = load_rules(tsv3, known_ids=TEST_IDS)
    gen = [c for c in dirs_r["reloc"]["genuine"] if len(c) >= 5]
    check(gen and gen[0][4] == (con_path, "coldboot", 120) and proved_rules_for(rules_r, dirs_r, "coldboot", 120)[1] == gen,
          "a @reloc-class directive carries the proof token; proved_rules_for lists it for its pair")
    # the shipped TSV's proved directive IS filed: the docs resolve its row id, so the default (docs) check admits it
    shipped = os.path.join(HERE, "attribution", "classes.tsv")
    if os.path.isfile(shipped):
        rules_s, _, _ = load_rules(shipped)
        proved_s = [r for r in rules_s if r.proof]
        ids_s = filed_row_ids()
        check(all(r.row.split()[0] in ids_s for r in proved_s),
              "the shipped classes.tsv's proof-bearing rules (%d) name FILED rows -> %r" % (len(proved_s), sorted({r.row.split()[0] for r in proved_s})))
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)
    print("attribute_probe selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("old", nargs="?")
    ap.add_argument("new", nargs="?")
    ap.add_argument("--classes", default=os.path.join(HERE, "attribution", "classes.tsv"))
    ap.add_argument("--mask", default=os.path.join(HERE, "mask", "coldboot_elf_text.ranges"))
    ap.add_argument("--leg", default="*", help="leg name for the rules' legs column")
    ap.add_argument("--boundary", default="?")
    ap.add_argument("--watch", help="WordWatch census (watch.log) naming writers per word")
    ap.add_argument("--reloc", action="store_true", help="classify heap/pool words through relocate_compare")
    ap.add_argument("--old-trace", help="OLD [ALLOC-TRACE] log (names the OLD allocations of the diverging pool records)")
    ap.add_argument("--out")
    ap.add_argument("--unattributed", default="40", help="UNATTRIBUTED words listed in the text (N or 'all'); the JSON always carries all")
    ap.add_argument("--row-cap-words", type=int, default=ROW_CAP_WORDS)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not (a.old and a.new):
        ap.error("old and new images are required")
    cap = -1 if a.unattributed == "all" else int(a.unattributed)
    res, rows = run(a.old, a.new, a.classes, a.mask, a.leg, a.watch, a.reloc, None, None, cap, a.old_trace, a.row_cap_words, a.boundary)
    print(render(a.leg, a.boundary, res["summary"], rows, cap, res["demoted_rules"]))
    if a.out:
        with open(a.out, "w") as f:
            json.dump(res, f, indent=1)
        print("json: %s" % a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
