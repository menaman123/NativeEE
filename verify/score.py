#!/usr/bin/env python3
"""score.py -- the score instrument (DECISIONS_LOG DT-1).

  score.py <battery_dir> [--probes <probe_dir>] [--probe-words <json>]
           [--mask verify/mask/coldboot_elf_text.ranges] [--baseline games/<title>/verify/baseline]
           [--ctest-log <log>] [--out score.json] [--legs a,b]
  score.py --compare A.json B.json
  score.py --regression REF.tsv candidate/score.json        (DT-8: PASS/FAIL, exit 0/1; verify/regression_ref.py)
  score.py --write-regression-ref gate/score.json REF.tsv   (DT-8: the reference from a gate of record)
  score.py --selftest

The tuple `(legs_identical, outside_mask_words, pins_green)`:

* `legs_identical` -- the battery legs whose table row (battery.py's output in
  `<battery_dir>/battery.log`, or `--table <file>`) reads
  `ADMISSIBLE META-OK IDENTICAL(frames,audio,mem)` (every requested hash lane
  identical to the frozen OLD baseline on every rep). With `--reps 1` the reps
  column is `n/a`; the leg still counts when its cross-engine cell is IDENTICAL
  over the three lanes (a 1-rep preview, named as such in the output).
* `outside_mask_words` -- the sum over the probe pairs (the frozen OLD images
  `games/<title>/verify/baseline/<leg>/probe/old_b<N>.ps2st` against the NEW images
  `<probe_dir>/<leg>/new_b<N>.ps2st` of the same boundary) of the RAM0 + SPAD
  words that differ outside the mask (I8.3-G's `region_summary.py` rule: a word
  is masked when its guest address lies in a mask range; SPAD addresses are
  `0x70000000 + offset`; META is never counted). The differing-offset list is
  built ONCE per lane per pair. A pair whose NEW image is missing (the leg
  stalled or parked before the boundary) counts as the OLD image's FULL word
  count (RAM0 + SPAD) -- a stall is never a free pass. With `--probe-words`
  the per-pair counts come precomputed (gate_run.py scores each pair as its
  images appear, so the transient NEW images need not coexist on disk).
  SCENE-ALIGNED PAIRS (DECISIONS_LOG DT-6): a leg whose LEG.tsv carries a `pair_map`
  (verify/leg_spec.py; found by verify/clock_pair.py from the boot-SM case-7 entry edge
  on each side) compares `probe/old_b<B>.ps2st` with the NEW image at the MAPPED boundary
  `<probe_dir>/<leg>/new_b<N>.ps2st` (N = pair_map[B]); the pair keeps its `leg@b<B>` key,
  carries `new_boundary` / `pairing` in the JSON and the note
  `paired by scene edge old_b<E>=new_b<E'> (offset +N)` in the table -- never silently.
  The EFFECTIVE REFERENCE (DECISIONS_LOG DT-4, proposed): a probe pair whose
  boundary is re-based in the leg's `games/<title>/verify/baseline/<leg>/REBASELINE.tsv`
  (mem `b<N>`; written only by apply_rebaseline.py, ledger-gated) is compared
  against the NEW reference image the manifest names (`rebased/new_b<N>.ps2st`)
  instead of the frozen `probe/old_b<N>.ps2st`; every such pair is listed in the
  `rebased_pairs` column of the score (JSON + table, `REBASED (<row ids>)` in the
  pair's note) -- never silently. The frozen files are never edited.
  PER-RUN PAIRING (P8.30-T; verify/scene_pairing.py): when `<probe_dir>/<leg>/PAIRING.tsv` exists
  (probe_images.py derived the run's own edge), the mapped boundary is `B + the run's offset` and the
  note reads `paired by scene edge (run: old_b<E>=new_b<E'>, offset +N; frozen reference +R,
  deviation +D)`; without the record the frozen map applies and the note says so.
* `pins_green` -- the `tests passed` count parsed from `--ctest-log`
  (`100% tests passed, 0 tests failed out of N` -> N - failed), or -1 = not run.

DT-7 (DECISIONS_LOG, P8.30-T) -- the UNBOOKED decision number. Per pair three counts are reported:
`words_raw` (= `total`, the outside-mask count above, UNCHANGED), `words_booked_console` (the words
attribute_probe.py books to a FILED 7.1 row whose directive carries `proof=console:<image>@<leg>:b<N>`
for THIS pair and whose console word == NEW != OLD -- checked word by word, attribute_probe.
booked_console_only / console_booking) and `words_unbooked = raw - booked_console`. The score prints
TWO tuples: `tuple = (legs_identical, outside_mask_words, pins)` -- the gate's REPORTED number, Section
6's definition unchanged -- and `tuple_unbooked = (legs_identical, outside_mask_words_unbooked, pins)`,
the number `--compare` DECIDES on (both printed). Nothing is masked and no baseline moves: a booked word
still counts in the raw total; it is discounted only from the decision because the console proves
OLD wrong there (PCSX2 is the reference, OLD is evidence). A pair with no console-proved rule books 0.

DT-8 (DECISIONS_LOG, P8.35-C) -- the REGRESSION gate. The memory-identity goal against OLD is retired; the battery
is a regression gate only: `--regression REF.tsv candidate/score.json` (verify/regression_ref.py) PASSES when
`legs_identical` is not lower, the raw AND unbooked word totals are not higher, every leg's frames/audio
first-divergent key is >= the reference's (IDENTICAL above every key), no leg is missing and no pins regression;
the mem lane and the per-leg words are reported only. The reference is `REGRESSION_REF.tsv` next to the frozen legs,
written from the gate of record by `--write-regression-ref`; gate_run.py prints the verdict into SUMMARY.md.
`--compare`'s BETTER/WORSE stays available as a report.

Also reported (progress, not score): per leg and lane the first-divergent key
(`FIRST DIVERGENT ... at line L (key K)` of the battery's `<leg>.diff.out` or of
the leg rows in battery.log) and the per-pair word counts.

`--compare A.json B.json` prints `BETTER` / `WORSE` / `EQUAL` for B against A by
the decision loop's rule: legs_identical higher wins; if equal, outside_mask_words
lower wins; if equal, pins_green not lower is EQUAL -- and ANY pins regression
(B.pins_green < A.pins_green, both run) is WORSE regardless of the first two
components ("strictly improves with no pin/test regression"). Under DT-7 the rule is
applied to `tuple_unbooked` when BOTH files carry it (the DECISION line) and to the raw
tuple as well (the REPORTED line); a file without the unbooked tuple (a score before
DT-7) decides on the raw tuple and says so.
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
import rebaseline  # noqa: E402
import leg_spec  # noqa: E402
import scene_pairing  # noqa: E402

DEFAULT_CLASSES = os.path.join(HERE, "attribution", "classes.tsv")

SPAD_BASE = 0x70000000
LANE_TAGS = (("RAM0", state_hash.TAG_RAM, 0), ("SPAD", state_hash.TAG_SPAD, SPAD_BASE))
ROW_RE = re.compile(r"^(\S+)\s+(\d+)\s+(\S+)\s+(META-OK IDENTICAL\([^)]*\)|META-OK DIVERGENT\([^)]*\)|META-OK|META-FAIL|RUN-FAIL|-)\s+(\S+)\s*(.*)$")
IDENTICAL_RE = re.compile(r"^META-OK IDENTICAL\(([^)]*)\)$")
FIRST_DIV_RE = re.compile(r"^(frames\.fhash|audio\.ahash|mem\.mhash): FIRST DIVERGENT .*? at (?:key (\d+)|line \d+ \(key (\d+)\))")
PASSED_RE = re.compile(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)")
SCORE_LANES = ("frames", "audio", "mem")


# ---------------------------------------------------------------- mask + pairs

def read_mask_ranges(path):
    """[(lo, hi, row)] guest-address ranges (HI exclusive) of a mask file; the
    grammar is diff_legs.py's (`0xLO-0xHI  <ROW-ID>  # why`)."""
    rows = []
    if not path:
        return rows
    rx = re.compile(r"^\s*(0x[0-9a-fA-F]+)\s*-\s*(0x[0-9a-fA-F]+)\s+(\S+)")
    with open(path) as f:
        for line in f:
            m = rx.match(line.split("#", 1)[0])
            if m:
                rows.append((int(m.group(1), 16), int(m.group(2), 16), m.group(3)))
    return rows


def masked(addr, rows):
    for lo, hi, _ in rows:
        if lo <= addr < hi:
            return True
    return False


def outside_mask_words(a_blob, b_blob, base, rows):
    """Count of 32-bit words differing between two lane blobs whose guest address
    (base + offset) lies in no mask range. Chunk-compare first; the differing
    offsets are collected once per lane."""
    n = min(len(a_blob), len(b_blob))
    count = 0
    chunk = 65536
    for off in range(0, n, chunk):
        ca = a_blob[off:off + chunk]
        cb = b_blob[off:off + chunk]
        if ca == cb:
            continue
        m = min(len(ca), len(cb)) // 4
        wa = struct.unpack_from("<%dI" % m, ca)
        wb = struct.unpack_from("<%dI" % m, cb)
        for i in range(m):
            if wa[i] != wb[i] and not masked(base + off + i * 4, rows):
                count += 1
    # a length difference: the tail words of the longer image count as differing
    count += (max(len(a_blob), len(b_blob)) - n) // 4
    return count


def image_sections(path):
    _, vf, sections = state_hash.parse(path)
    return vf, sections


def pair_words(old_path, new_path, rows):
    """{'RAM0': n, 'SPAD': n, 'total': n, 'old_vsync_frame': v, 'new_vsync_frame': v|None,
    'missing': bool}. A missing NEW image counts the OLD image's full RAM0 + SPAD word count."""
    vf_old, so = image_sections(old_path)
    out = {"old_vsync_frame": vf_old, "new_vsync_frame": None, "missing": False}
    if not new_path or not os.path.isfile(new_path):
        out["missing"] = True
        for name, tag, _ in LANE_TAGS:
            out[name] = len(so[tag][2]) // 4
    else:
        vf_new, sn = image_sections(new_path)
        out["new_vsync_frame"] = vf_new
        for name, tag, base in LANE_TAGS:
            out[name] = outside_mask_words(so[tag][2], sn[tag][2], base, rows)
    out["total"] = sum(out[name] for name, _, _ in LANE_TAGS)
    return out


def pairing_note(spec, b, pairing=None):
    """(mapped NEW boundary, note text | None) of OLD probe boundary b under the leg's LEG.tsv pair_map (DT-6),
    by the RUN's edge when the probe dir carries a PAIRING.tsv (scene_pairing.read), else the frozen map's."""
    if spec is None or b not in spec.get("pair_map", {}):
        return b, None
    n = scene_pairing.mapped_boundary(spec, pairing, b)
    if pairing is None:
        e = spec["pair_edge"]
        return n, "paired by scene edge old_b%d=new_b%d (offset %+d)" % (e[0], e[1], n - b)
    return n, scene_pairing.note(spec, pairing, b)


def run_pairing(probe_dir, leg):
    """The run's PAIRING.tsv under <probe_dir>/<leg>, or None (a malformed one is a usage error)."""
    if not probe_dir:
        return None
    try:
        return scene_pairing.read(os.path.join(probe_dir, leg))
    except ValueError as e:
        raise SystemExit("score: %s" % e)


def probe_pairs(baseline_dir, probe_dir, legs=None):
    """[(leg, boundary, reference_path, new_path|None)] for every frozen OLD probe image. The reference is the
    frozen `probe/old_b<N>.ps2st` unless the leg's REBASELINE.tsv re-bases mem b<N>: then the manifest's
    `rebased/new_b<N>.ps2st` (see rebased_pairs() for the report). The NEW image is `new_b<N>.ps2st` at the
    leg's pair_map boundary N (DT-6; = b without a map)."""
    pairs = []
    for leg in sorted(os.listdir(baseline_dir)):
        if legs and leg not in legs:
            continue
        pdir = os.path.join(baseline_dir, leg, "probe")
        if not os.path.isdir(pdir):
            continue
        entries = rebaseline.read_manifest(os.path.join(baseline_dir, leg))
        spec = leg_spec.read_leg(baseline_dir, leg)
        found = []
        for name in os.listdir(pdir):
            m = re.match(r"^old_b(\d+)\.ps2st$", name)
            if m:
                found.append((int(m.group(1)), name))
        pairing = run_pairing(probe_dir, leg)
        for b, name in sorted(found):
            n, _ = pairing_note(spec, b, pairing)
            new_path = os.path.join(probe_dir, leg, "new_b%d.ps2st" % n) if probe_dir else None
            ref, _ = rebaseline.effective_probe(os.path.join(baseline_dir, leg), b, entries)
            pairs.append((leg, b, ref, new_path))
    return pairs


def pairing_notes(baseline_dir, legs=None, probe_dir=None):
    """{'<leg>@b<B>': {'new_boundary': N, 'pairing': text, 'pairing_record': {...}|None}} for every scene-paired probe
    (DT-6); the run's PAIRING.tsv under probe_dir decides N and the note when present."""
    out = {}
    for leg in sorted(os.listdir(baseline_dir)):
        if legs and leg not in legs:
            continue
        spec = leg_spec.read_leg(baseline_dir, leg)
        if spec is None or not spec.get("pair_map"):
            continue
        pairing = run_pairing(probe_dir, leg)
        for b in spec["pair_map"]:
            n, note = pairing_note(spec, b, pairing)
            if note:
                out["%s@b%d" % (leg, b)] = {"new_boundary": n, "pairing": note,
                                            "pairing_record": None if pairing is None else {k: pairing[k] for k in ("status", "new_edge", "offset", "ref_offset", "deviation", "edge2_status")}}
    return out


def rebased_pairs(baseline_dir, legs=None):
    """{'<leg>@b<N>': {'rows': [...], 'commit', 'proof', 'image'}} for every probe pair whose mem window is
    re-based in its leg's manifest (the `rebased_pairs` column)."""
    out = {}
    for leg in sorted(os.listdir(baseline_dir)):
        if legs and leg not in legs:
            continue
        leg_dir = os.path.join(baseline_dir, leg)
        if not os.path.isdir(os.path.join(leg_dir, "probe")):
            continue
        for e in rebaseline.by_lane(rebaseline.read_manifest(leg_dir), "mem").values():
            if os.path.isfile(os.path.join(leg_dir, "probe", "old_b%d.ps2st" % e["key"])):
                out["%s@b%d" % (leg, e["key"])] = {"rows": e["rows"], "commit": e["commit"], "proof": e["proof"], "image": e["image"]}
    return out


# ---------------------------------------------------------------- battery table

def parse_battery_table(lines):
    """{leg: {'frames', 'reps', 'cross', 'fp', 'note', 'identical_lanes'}} from battery.py's
    output (the leg rows of the final table; the header and the summary are skipped)."""
    rows = {}
    for raw in lines:
        line = raw.rstrip("\n")
        if not line or line.startswith("leg ") or line.startswith("battery:"):
            continue
        m = ROW_RE.match(line)
        if not m:
            continue
        leg, frames, reps, cross, fp, note = m.groups()
        ident = IDENTICAL_RE.match(cross)
        lanes = tuple(x.strip() for x in ident.group(1).split(",") if x.strip()) if ident else ()
        rows[leg] = {"frames": int(frames), "reps": reps, "cross": cross, "fp": fp, "note": note,
                     "identical_lanes": lanes}
    return rows


def leg_is_identical(row, reps_required=True):
    """IDENTICAL on frames, audio and mem, and (when reps > 1) ADMISSIBLE."""
    if not all(l in row["identical_lanes"] for l in SCORE_LANES):
        return False
    if row["reps"] == "ADMISSIBLE":
        return True
    return row["reps"] == "n/a" and not reps_required


def first_divergent_keys(diff_out_path):
    """{lane: key} from a diff_legs report file (first-divergent line per lane)."""
    out = {}
    if not diff_out_path or not os.path.isfile(diff_out_path):
        return out
    with open(diff_out_path, errors="replace") as f:
        for line in f:
            m = FIRST_DIV_RE.match(line.strip())
            if m:
                out.setdefault(m.group(1).split(".")[0], int(m.group(2) or m.group(3)))
    return out


def parse_ctest_log(path):
    """The pass count from a ctest log (`N% tests passed, F tests failed out of T` -> T - F),
    or -1 when no such line exists / no log given."""
    if not path or not os.path.isfile(path):
        return -1
    passed = -1
    with open(path, errors="replace") as f:
        for line in f:
            m = PASSED_RE.search(line)
            if m:
                passed = int(m.group(3)) - int(m.group(2))
    return passed


# ---------------------------------------------------------------- the score

def book_pair(w, leg, b, old_path, new_path, classes, mask):
    """DT-7: fill `words_raw` / `words_booked_console` / `words_unbooked` on a pair's dict (in place). The booking
    needs both images and a classes TSV; a MISSING pair or no classes books 0 (raw = unbooked)."""
    w["words_raw"] = w["total"]
    if "words_booked_console" not in w:
        booked, method = 0, "not computed (no images / no classes)"
        if classes and old_path and new_path and os.path.isfile(old_path) and os.path.isfile(new_path):
            import attribute_probe
            res = attribute_probe.booked_console_only(old_path, new_path, classes, mask, leg, b)
            booked, method = res["booked_console"], res["method"]
        w["words_booked_console"] = booked
        w["booking"] = method
    w["words_unbooked"] = w["total"] - w["words_booked_console"]
    return w


def compute_score(battery_dir, table_lines, probe_pairs_list, rows, ctest_log=None,
                  precomputed=None, reps_required=True, rebased=None, pairing=None, classes=None, mask=None):
    table = parse_battery_table(table_lines)
    legs = sorted(table)
    identical = [l for l in legs if leg_is_identical(table[l], reps_required)]
    per_leg = {}
    for leg in legs:
        d = os.path.join(battery_dir, leg + ".diff.out")
        per_leg[leg] = {"row": table[leg]["cross"], "reps": table[leg]["reps"],
                        "first_divergent": first_divergent_keys(d) if os.path.isfile(d) else {}}
    pairs = []
    total = 0
    for leg, b, old_path, new_path in probe_pairs_list:
        key = "%s@b%d" % (leg, b)
        if precomputed and key in precomputed:
            w = precomputed[key]
        else:
            w = pair_words(old_path, new_path, rows)
        w = dict(w)
        w.update({"leg": leg, "boundary": b})
        if rebased and key in rebased:
            w["rebased"] = rebased[key]
        if pairing and key in pairing:
            w.update(pairing[key])
        book_pair(w, leg, b, old_path, new_path, classes, mask)
        pairs.append(w)
        total += w["total"]
    pins = parse_ctest_log(ctest_log)
    booked = sum(w["words_booked_console"] for w in pairs)
    return {"tuple": [len(identical), total, pins], "tuple_unbooked": [len(identical), total - booked, pins],
            "legs_identical": len(identical), "outside_mask_words": total, "pins_green": pins,
            "outside_mask_words_unbooked": total - booked, "booked_console_words": booked,
            "identical_legs": identical, "legs_in_table": len(legs), "per_leg": per_leg,
            "pairs": pairs, "reps_required": reps_required,
            "rebased_pairs": sorted(k for k in (rebased or {}) if any("%s@b%d" % (p["leg"], p["boundary"]) == k for p in pairs))}


def compare(a, b, key="tuple"):
    """BETTER / WORSE / EQUAL for B against A (the decision loop's rule) over the named tuple."""
    ta, tb = a[key], b[key]
    if ta[2] >= 0 and tb[2] >= 0 and tb[2] < ta[2]:
        return "WORSE"
    if tb[0] != ta[0]:
        return "BETTER" if tb[0] > ta[0] else "WORSE"
    if tb[1] != ta[1]:
        return "BETTER" if tb[1] < ta[1] else "WORSE"
    return "EQUAL"


def compare_lines(sa, sb):
    """The two --compare lines (DT-7): the REPORTED raw verdict and the DECISION on the unbooked tuple (raw when a
    side lacks it, said so). Returns (lines, decision verdict)."""
    raw = compare(sa, sb, "tuple")
    lines = ["compare: A=%s B=%s -> %s" % (tuple(sa["tuple"]), tuple(sb["tuple"]), raw) + "  [REPORTED: raw outside-mask words, Section 6]"]
    if "tuple_unbooked" in sa and "tuple_unbooked" in sb:
        dec = compare(sa, sb, "tuple_unbooked")
        lines.append("compare (DT-7, unbooked): A=%s B=%s -> %s  [DECISION: raw minus the console-proved booked words; booked A=%d B=%d]"
                     % (tuple(sa["tuple_unbooked"]), tuple(sb["tuple_unbooked"]), dec, sa.get("booked_console_words", 0), sb.get("booked_console_words", 0)))
    else:
        dec = raw
        lines.append("compare (DT-7, unbooked): not available (%s carries no tuple_unbooked: a score before DT-7) -> DECISION on the raw tuple: %s"
                     % ("A" if "tuple_unbooked" not in sa else "B", dec))
    lines.append("decision: %s" % dec)
    return lines, dec


def render_table(score):
    out = []
    out.append("score: legs_identical=%d/%d outside_mask_words=%d pins_green=%d  tuple=(%d, %d, %d)%s"
               % (score["legs_identical"], score["legs_in_table"], score["outside_mask_words"],
                  score["pins_green"], score["tuple"][0], score["tuple"][1], score["tuple"][2],
                  "" if score.get("reps_required", True) else "  [1-rep preview: reps not required]"))
    if "tuple_unbooked" in score:
        tu = score["tuple_unbooked"]
        out.append("score (DT-7): booked_console_words=%d outside_mask_words_unbooked=%d  tuple_unbooked=(%d, %d, %d)  [the decision number; the raw tuple above stays the reported one]"
                   % (score["booked_console_words"], score["outside_mask_words_unbooked"], tu[0], tu[1], tu[2]))
    out.append("%-40s %-12s %-44s %s" % ("leg", "reps", "cross-engine", "first divergent key (frames/audio/mem)"))
    for leg in sorted(score["per_leg"]):
        p = score["per_leg"][leg]
        fd = p["first_divergent"]
        keys = "/".join(str(fd.get(l, "-")) for l in SCORE_LANES)
        out.append("%-40s %-12s %-44s %s" % (leg, p["reps"], p["row"], keys))
    out.append("%-40s %8s %10s %10s %10s %8s %10s  %s" % ("probe pair", "boundary", "RAM0", "SPAD", "total", "booked", "unbooked", "note"))
    for w in score["pairs"]:
        note = ("NEW image MISSING -> full word count" if w["missing"] else
                "old vf %s / new vf %s" % (w["old_vsync_frame"], w["new_vsync_frame"]))
        if w.get("rebased"):
            note += "  REBASED (%s; commit %s)" % (", ".join(w["rebased"]["rows"]), w["rebased"]["commit"])
        if w.get("pairing"):
            note += "  new_b%d: %s" % (w["new_boundary"], w["pairing"])
        if w.get("words_booked_console"):
            note += "  booked_console %d (%s)" % (w["words_booked_console"], w.get("booking", "precomputed"))
        out.append("%-40s %8d %10d %10d %10d %8d %10d  %s" % (w["leg"], w["boundary"], w["RAM0"], w["SPAD"], w["total"],
                                                          w.get("words_booked_console", 0), w.get("words_unbooked", w["total"]), note))
    rb = score.get("rebased_pairs", [])
    out.append("rebased_pairs=%d%s" % (len(rb), (": " + ", ".join(rb)) if rb else " (every pair against the frozen OLD probe)"))
    return "\n".join(out)


# ---------------------------------------------------------------- selftest

def _synthetic_image(path, ram_words, spad_words, vsync_frame=0):
    """A minimal valid PS2SAVE1 envelope with RAM0 / SPAD / META payloads."""
    import zlib
    ram = struct.pack("<%dI" % len(ram_words), *ram_words)
    spad = struct.pack("<%dI" % len(spad_words), *spad_words)
    meta = bytes(16)
    body = b"PS2SAVE1" + struct.pack("<IIIIQ", 4, 3, vsync_frame, 0, 0)
    for tag, payload in ((state_hash.TAG_RAM, ram), (state_hash.TAG_SPAD, spad), (state_hash.TAG_META, meta)):
        body += struct.pack("<IIIIQ", tag, 1, 0, zlib.crc32(payload) & 0xFFFFFFFF, len(payload)) + payload
    body += struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(body)


def selftest():
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)

    tmp = tempfile.mkdtemp(prefix="score_selftest_")
    # 1. the mask rule: words 0..7 of RAM0 differ; the mask covers 0x8-0x10 (words 2,3);
    #    SPAD word 1 differs at guest 0x70000004 (never masked here).
    a = os.path.join(tmp, "old.ps2st")
    b = os.path.join(tmp, "new.ps2st")
    _synthetic_image(a, [1, 2, 3, 4, 5, 6, 7, 8], [10, 11, 12, 13], 60)
    _synthetic_image(b, [9, 9, 9, 9, 9, 9, 9, 9], [10, 99, 12, 13], 60)
    rows = [(0x8, 0x10, "P0-TEST")]
    w = pair_words(a, b, rows)
    check(w["RAM0"] == 6 and w["SPAD"] == 1 and w["total"] == 7 and not w["missing"],
          "pair_words counts outside-mask words (RAM0 6, SPAD 1) -> %r" % w)
    w0 = pair_words(a, b, [])
    check(w0["total"] == 9, "no mask -> every differing word counts (9) -> %d" % w0["total"])
    wm = pair_words(a, None, rows)
    check(wm["missing"] and wm["total"] == 12, "missing NEW image -> OLD's full word count 8+4=12 -> %r" % wm["total"])
    same = pair_words(a, a, rows)
    check(same["total"] == 0, "an image against itself -> 0")
    # 2. the battery table parser and the identity rule
    table = [
        "leg                frames reps        cross-engine (lanes=frames,audio,mem)     fingerprint note",
        "coldboot             1500 ADMISSIBLE  META-OK IDENTICAL(frames,audio,mem)      FP-OK       frames=1500",
        "artdet_f37152         600 UNSTABLE    META-OK IDENTICAL(frames,audio,mem)      FP-OK       frames=600",
        "ps2state_slot0_f1     600 ADMISSIBLE  META-OK DIVERGENT(audio,mem)             FP-OK       frames=600",
        "ps2state_slot0_f2     600 n/a         META-OK IDENTICAL(frames,audio,mem)      FP-OK       frames=600",
        "ps2state_slot0_f3     600 ADMISSIBLE  META-OK IDENTICAL(mem)                   FP-OK       frames=600",
        "ps2state_slot0_f4     600 -           RUN-FAIL                                 -           run_leg exit=1: x",
        "battery: META-OK 5/6 legs; FAIL",
    ]
    rows_t = parse_battery_table(table)
    check(len(rows_t) == 6, "six leg rows parsed -> %d" % len(rows_t))
    ident = [l for l in sorted(rows_t) if leg_is_identical(rows_t[l])]
    check(ident == ["coldboot"], "reps required: only the ADMISSIBLE + IDENTICAL(frames,audio,mem) leg -> %r" % ident)
    ident1 = [l for l in sorted(rows_t) if leg_is_identical(rows_t[l], reps_required=False)]
    check(ident1 == ["coldboot", "ps2state_slot0_f2"], "1-rep preview: n/a reps count, UNSTABLE never -> %r" % ident1)
    # 3. the ctest log parser
    log = os.path.join(tmp, "ctest.log")
    with open(log, "w") as f:
        f.write("...\n99% tests passed, 2 tests failed out of 321\n")
    check(parse_ctest_log(log) == 319, "ctest log -> 321 - 2 = 319")
    check(parse_ctest_log(None) == -1, "no ctest log -> -1")
    # 4. first-divergent keys
    d = os.path.join(tmp, "x.diff.out")
    with open(d, "w") as f:
        f.write("frames.fhash: FIRST DIVERGENT frame at key 69 (paired by key): (66 keys paired before it)\n"
                "audio.ahash: FIRST DIVERGENT audio window at line 143 (key 142): A: x B: y\n"
                "mem.mhash: FIRST DIVERGENT mem checkpoint at line 1 (key 0): A: x B: y\n")
    check(first_divergent_keys(d) == {"frames": 69, "audio": 142, "mem": 0}, "first-divergent keys per lane (frames by key, audio/mem by line (key))")
    # 5. compare: the lexicographic rule with the pins override
    S = lambda a_, b_, c_: {"tuple": [a_, b_, c_]}  # noqa: E731
    check(compare(S(3, 100, 321), S(4, 500, 321)) == "BETTER", "more identical legs wins even with more words")
    check(compare(S(3, 100, 321), S(3, 99, 321)) == "BETTER", "equal legs: fewer words wins")
    check(compare(S(3, 100, 321), S(3, 101, 321)) == "WORSE", "equal legs: more words loses")
    check(compare(S(3, 100, 321), S(3, 100, 321)) == "EQUAL", "same tuple -> EQUAL")
    check(compare(S(3, 100, 321), S(3, 100, 322)) == "EQUAL", "more pins alone is not an improvement -> EQUAL")
    check(compare(S(3, 100, 321), S(4, 0, 320)) == "WORSE", "ANY pins regression -> WORSE regardless")
    check(compare(S(3, 100, -1), S(4, 0, 320)) == "BETTER", "pins not run on one side: no regression claim")
    # 6. end to end over synthetic battery dir + probes
    bdir = os.path.join(tmp, "battery")
    os.makedirs(os.path.join(bdir), exist_ok=True)
    with open(os.path.join(bdir, "battery.log"), "w") as f:
        f.write("\n".join(table) + "\n")
    with open(os.path.join(bdir, "coldboot.diff.out"), "w") as f:
        f.write("mem.mhash: FIRST DIVERGENT mem checkpoint at line 2 (key 60): A: x B: y\n")
    base = os.path.join(tmp, "baseline")
    probes = os.path.join(tmp, "probes")
    os.makedirs(os.path.join(base, "coldboot", "probe"))
    os.makedirs(os.path.join(base, "artdet_f37152", "probe"))
    os.makedirs(os.path.join(probes, "coldboot"))
    _synthetic_image(os.path.join(base, "coldboot", "probe", "old_b60.ps2st"), [1, 2, 3, 4], [5, 6], 60)
    _synthetic_image(os.path.join(probes, "coldboot", "new_b60.ps2st"), [1, 2, 0, 4], [5, 6], 60)
    _synthetic_image(os.path.join(base, "artdet_f37152", "probe", "old_b2.ps2st"), [1, 2, 3, 4], [5, 6], 2)
    pairs = probe_pairs(base, probes)
    check([(p[0], p[1]) for p in pairs] == [("artdet_f37152", 2), ("coldboot", 60)], "probe pairs enumerated -> %r" % [(p[0], p[1]) for p in pairs])
    # a leg whose probe boundaries are its own (LEG.tsv-driven, e.g. coldboot_attract's) enumerates in boundary order
    os.makedirs(os.path.join(base, "coldboot_attract", "probe"))
    for b in (3840, 900, 4200, 3780):
        _synthetic_image(os.path.join(base, "coldboot_attract", "probe", "old_b%d.ps2st" % b), [1, 2, 3, 4], [5, 6], b + 530)
    pairs_a = probe_pairs(base, probes, ["coldboot_attract"])
    check([(p[0], p[1]) for p in pairs_a] == [("coldboot_attract", 900), ("coldboot_attract", 3780), ("coldboot_attract", 3840), ("coldboot_attract", 4200)],
          "a LEG.tsv leg's probe pairs at its own boundaries, in order -> %r" % [p[1] for p in pairs_a])
    score_a = compute_score(bdir, table, pairs_a, [], log)
    check(score_a["outside_mask_words"] == 4 * 6 and all(w["missing"] for w in score_a["pairs"]), "its four NEW-missing pairs count the OLD word counts (4 x 6)")
    # DT-6: a LEG.tsv pair_map maps the probe to the NEW image at the mapped boundary, reported in the note
    with open(os.path.join(base, "coldboot_attract", leg_spec.FILE), "w") as f:
        f.write(leg_spec.format_spec(6000, 60, 3000, 600, "coldboot", [(900, 1500), (3780, 4400), (3840, 4400), (4200, 4800)], "n",
                                     pair_map="old_b850=new_b1133,old_b900=new_b1183,old_b3780=new_b4063,old_b3840=new_b4123,old_b4200=new_b4483", vf_offset=530))
    os.makedirs(os.path.join(probes, "coldboot_attract"))
    _synthetic_image(os.path.join(probes, "coldboot_attract", "new_b1183.ps2st"), [1, 2, 3, 0], [5, 6], 1183)
    pairs_m = probe_pairs(base, probes, ["coldboot_attract"])
    check([os.path.basename(p[3]) for p in pairs_m] == ["new_b1183.ps2st", "new_b4063.ps2st", "new_b4123.ps2st", "new_b4483.ps2st"],
          "with a pair_map the NEW image of each probe is the MAPPED boundary's -> %r" % [os.path.basename(p[3]) for p in pairs_m])
    notes = pairing_notes(base, ["coldboot_attract"])
    check(notes["coldboot_attract@b900"] == {"new_boundary": 1183, "pairing": "paired by scene edge old_b850=new_b1133 (offset +283)", "pairing_record": None}
          and len(notes) == 4, "pairing_notes names the edge and the offset per probe -> %r" % notes.get("coldboot_attract@b900"))
    score_m = compute_score(bdir, table, pairs_m, [], log, pairing=notes)
    check(score_m["pairs"][0]["total"] == 1 and score_m["pairs"][0]["new_vsync_frame"] == 1183 and score_m["pairs"][0]["new_boundary"] == 1183
          and sum(w["missing"] for w in score_m["pairs"]) == 3, "the mapped image scores (1 word, header 1183); the unproduced mapped images count OLD's words")
    check("new_b1183: paired by scene edge old_b850=new_b1133 (offset +283)" in render_table(score_m), "the table row carries the pairing note")
    check(pairing_notes(base, ["coldboot"]) == {}, "a leg without a pair_map has no pairing notes")
    # PER-RUN pairing (P8.30-T): a PAIRING.tsv under the probe leg dir moves every mapped probe by the RUN's offset
    spec_a = leg_spec.read_leg(base, "coldboot_attract")
    scene_pairing.write(os.path.join(probes, "coldboot_attract"), scene_pairing.make(spec_a, 1049, "EDGE-FOUND", "image-census", 1283, "-"))
    _synthetic_image(os.path.join(probes, "coldboot_attract", "new_b1099.ps2st"), [1, 2, 3, 0], [5, 0], 1099)
    pairs_run = probe_pairs(base, probes, ["coldboot_attract"])
    check([os.path.basename(p[3]) for p in pairs_run] == ["new_b1099.ps2st", "new_b3979.ps2st", "new_b4039.ps2st", "new_b4399.ps2st"],
          "with a PAIRING.tsv the NEW images are at B + the run's offset (+199) -> %r" % [os.path.basename(p[3]) for p in pairs_run])
    notes_run = pairing_notes(base, ["coldboot_attract"], probes)
    check(notes_run["coldboot_attract@b900"]["new_boundary"] == 1099
          and notes_run["coldboot_attract@b900"]["pairing"] == "paired by scene edge (run: old_b850=new_b1049, offset +199; frozen reference +283, deviation -84)"
          and notes_run["coldboot_attract@b900"]["pairing_record"]["deviation"] == -84,
          "the note names the run's edge, offset, the frozen reference and the deviation -> %r" % notes_run["coldboot_attract@b900"]["pairing"])
    score_run = compute_score(bdir, table, pairs_run, [], log, pairing=notes_run)
    check(score_run["pairs"][0]["total"] == 2 and score_run["pairs"][0]["new_boundary"] == 1099 and "deviation -84" in render_table(score_run),
          "the run-mapped image scores (2 words) and the table carries the deviation")
    import shutil as _sh
    _sh.rmtree(os.path.join(probes, "coldboot_attract"))
    _sh.rmtree(os.path.join(base, "coldboot_attract"))
    score = compute_score(bdir, table, pairs, [], log)
    check(score["tuple"] == [1, 1 + 6, 319], "end-to-end tuple (1 leg, 1 word + 6 missing, 319) -> %r" % score["tuple"])
    check(score["per_leg"]["coldboot"]["first_divergent"] == {"mem": 60}, "per-leg first-divergent key read from <leg>.diff.out")
    pre = {"coldboot@b60": {"RAM0": 42, "SPAD": 0, "total": 42, "old_vsync_frame": 60, "new_vsync_frame": 60, "missing": False}}
    score2 = compute_score(bdir, table, pairs, [], log, precomputed=pre)
    check(score2["outside_mask_words"] == 42 + 6, "--probe-words precomputed counts are used -> %d" % score2["outside_mask_words"])
    # DT-7: the booked column; the raw tuple unchanged, the unbooked tuple next to it; --compare decides on unbooked
    check(score2["tuple_unbooked"] == [1, 48, 319] and score2["booked_console_words"] == 0 and all(w["words_unbooked"] == w["total"] for w in score2["pairs"]),
          "no console-proved rule: booked 0, unbooked == raw -> %r" % score2["tuple_unbooked"])
    pre_b = {"coldboot@b60": dict(pre["coldboot@b60"], words_booked_console=40, booking="precomputed")}
    score3 = compute_score(bdir, table, pairs, [], log, precomputed=pre_b)
    check(score3["tuple"] == [1, 48, 319] and score3["tuple_unbooked"] == [1, 8, 319] and score3["booked_console_words"] == 40
          and score3["pairs"][1]["words_unbooked"] == 2 and score3["pairs"][1]["words_raw"] == 42,
          "a precomputed booked_console 40 leaves the raw tuple (1, 48) and makes the unbooked tuple (1, 8) -> %r %r" % (score3["tuple"], score3["tuple_unbooked"]))
    txt3 = render_table(score3)
    check("tuple_unbooked=(1, 8, 319)" in txt3 and "tuple=(1, 48, 319)" in txt3 and "booked_console 40" in txt3, "the table prints both tuples and the pair's booked count")
    lines_c, dec = compare_lines({"tuple": [0, 100, 5], "tuple_unbooked": [0, 100, 5], "booked_console_words": 0},
                                 {"tuple": [0, 102, 5], "tuple_unbooked": [0, 90, 5], "booked_console_words": 12})
    check(dec == "BETTER" and lines_c[0].endswith("-> WORSE  [REPORTED: raw outside-mask words, Section 6]") and "-> BETTER  [DECISION" in lines_c[1] and lines_c[2] == "decision: BETTER",
          "--compare: raw WORSE is REPORTED, the unbooked BETTER DECIDES (DT-7) -> %r" % lines_c)
    lines_d, dec_d = compare_lines({"tuple": [0, 100, 5]}, {"tuple": [0, 102, 5], "tuple_unbooked": [0, 90, 5]})
    check(dec_d == "WORSE" and "A carries no tuple_unbooked" in lines_d[1], "a side without the unbooked tuple decides on the raw tuple and says so")
    lines_e, dec_e = compare_lines({"tuple": [0, 100, 5], "tuple_unbooked": [0, 50, 5]}, {"tuple": [0, 90, 5], "tuple_unbooked": [0, 60, 5]})
    check(dec_e == "WORSE", "raw BETTER but unbooked WORSE -> the decision is WORSE (booked words never buy a KEEP)")
    txt = render_table(score)
    check("tuple=(1, 7, 319)" in txt and "NEW image MISSING" in txt and "rebased_pairs=0" in txt, "table renders the tuple, the missing note and rebased_pairs=0")
    # 7. the effective reference (DT-4): a re-based mem window compares the NEW probe against the manifest's image
    rdir = os.path.join(base, "coldboot", "rebased")
    os.makedirs(rdir)
    _synthetic_image(os.path.join(rdir, "new_b60.ps2st"), [1, 2, 0, 4], [5, 6], 60)
    with open(os.path.join(base, "coldboot", rebaseline.MANIFEST), "w") as f:
        f.write(rebaseline.header() + "mem\tb60\tP9-TEST-ROW\tabc1234\trebased/mem_b60.ledger.json\t60,0xold\t60,0xnew\trebased/new_b60.ps2st\t\n")
    pairs_r = probe_pairs(base, probes)
    check([p[2].endswith("rebased/new_b60.ps2st") for p in pairs_r] == [False, True], "the coldboot b60 pair's reference is the rebased image; artdet's stays frozen")
    reb = rebased_pairs(base)
    check(list(reb) == ["coldboot@b60"] and reb["coldboot@b60"]["rows"] == ["P9-TEST-ROW"], "rebased_pairs() lists the re-based pair with its rows -> %r" % reb)
    score_r = compute_score(bdir, table, pairs_r, [], log, rebased=reb)
    check(score_r["tuple"] == [1, 0 + 6, 319] and score_r["rebased_pairs"] == ["coldboot@b60"],
          "against the effective reference the pair counts 0 words and is reported in rebased_pairs -> %r %r" % (score_r["tuple"], score_r["rebased_pairs"]))
    txt_r = render_table(score_r)
    check("REBASED (P9-TEST-ROW; commit abc1234)" in txt_r and "rebased_pairs=1: coldboot@b60" in txt_r, "the table carries the REBASED note and the column")
    check(open(os.path.join(base, "coldboot", "probe", "old_b60.ps2st"), "rb").read() == open(os.path.join(base, "coldboot", "probe", "old_b60.ps2st"), "rb").read()
          and pair_words(os.path.join(base, "coldboot", "probe", "old_b60.ps2st"), os.path.join(probes, "coldboot", "new_b60.ps2st"), [])["total"] == 1,
          "the frozen probe still differs by 1 word from NEW (untouched)")
    # 8. an IMAGE-ONLY window (the anchors' b2: no mem.mhash line on either side): manifest lines `-`, the image is the reference
    rdir2 = os.path.join(base, "artdet_f37152", "rebased")
    os.makedirs(rdir2)
    _synthetic_image(os.path.join(rdir2, "new_b2.ps2st"), [1, 2, 3, 4], [5, 6], 2)
    with open(os.path.join(base, "artdet_f37152", rebaseline.MANIFEST), "w") as f:
        f.write(rebaseline.header() + "mem\tb2\tP9-TEST-ROW\tabc1234\trebased/mem_b2.ledger.json\t-\t-\trebased/new_b2.ps2st\timage-only\n")
    pairs_i = probe_pairs(base, probes)
    check([os.path.basename(p[2]) for p in pairs_i] == ["new_b2.ps2st", "new_b60.ps2st"] and pairs_i[0][2].endswith("artdet_f37152/rebased/new_b2.ps2st"),
          "the artdet b2 pair's reference is the image-only rebased image -> %r" % [p[2][-40:] for p in pairs_i])
    reb_i = rebased_pairs(base)
    check(sorted(reb_i) == ["artdet_f37152@b2", "coldboot@b60"], "rebased_pairs() lists the image-only window too -> %r" % sorted(reb_i))
    score_i = compute_score(bdir, table, pairs_i, [], log, rebased=reb_i)
    check(score_i["rebased_pairs"] == ["artdet_f37152@b2", "coldboot@b60"] and "rebased_pairs=2: artdet_f37152@b2, coldboot@b60" in render_table(score_i),
          "the image-only window scores against its image and is reported -> %r" % score_i["rebased_pairs"])
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)
    print("score selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("battery_dir", nargs="?")
    ap.add_argument("--probes", help="NEW probe image dir (<dir>/<leg>/new_b<N>.ps2st)")
    ap.add_argument("--probe-words", help="JSON {leg@bN: pair_words dict} precomputed by gate_run.py")
    ap.add_argument("--mask", default=os.path.join(HERE, "mask", "coldboot_elf_text.ranges"))
    ap.add_argument("--baseline", default=leg_spec.BASELINE_DIR)
    ap.add_argument("--table", help="battery.py output file (default <battery_dir>/battery.log)")
    ap.add_argument("--ctest-log")
    ap.add_argument("--out", help="score.json path (default <battery_dir>/score.json)")
    ap.add_argument("--legs", help="restrict the probe pairs to these legs")
    ap.add_argument("--reps-not-required", action="store_true",
                    help="1-rep preview: a leg counts without an ADMISSIBLE reps cell")
    ap.add_argument("--compare", nargs=2, metavar=("A.json", "B.json"))
    ap.add_argument("--classes", default=DEFAULT_CLASSES, help="DT-7: the attribution rules whose console-proved ROW rules book words")
    ap.add_argument("--book-from", metavar="PROBE_DIR",
                    help="DT-7: with --probe-words, compute the booked column for the pairs whose NEW image exists under this dir")
    ap.add_argument("--regression", nargs=2, metavar=("REF.tsv", "candidate_score.json"),
                    help="DT-8: PASS/FAIL the candidate against the regression reference (verify/regression_ref.py); exit 0 PASS, 1 FAIL")
    ap.add_argument("--write-regression-ref", nargs=2, metavar=("score.json", "out.tsv"),
                    help="DT-8: write a REGRESSION_REF.tsv from a gate of record's score.json (--ref-source names the gate)")
    ap.add_argument("--ref-source", help="the gate name written into --write-regression-ref's header (default: the score.json's dir name)")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if a.regression:
        import regression_ref
        return regression_ref.run(a.regression[0], a.regression[1])
    if a.write_regression_ref:
        import regression_ref
        print(regression_ref.write_ref(a.write_regression_ref[0], a.write_regression_ref[1], a.ref_source), end="")
        return 0
    if a.compare:
        with open(a.compare[0]) as f:
            sa = json.load(f)
        with open(a.compare[1]) as f:
            sb = json.load(f)
        lines, _ = compare_lines(sa, sb)
        print("\n".join(lines))
        return 0
    if not a.battery_dir:
        ap.error("battery_dir is required")
    table_path = a.table or os.path.join(a.battery_dir, "battery.log")
    if not os.path.isfile(table_path):
        raise SystemExit("battery table not found: %s" % table_path)
    with open(table_path, errors="replace") as f:
        table_lines = f.read().splitlines()
    rows = read_mask_ranges(a.mask)
    legs = [x.strip() for x in a.legs.split(",")] if a.legs else None
    pre = None
    if a.probe_words:
        with open(a.probe_words) as f:
            pre = json.load(f)
    probes = a.probes or a.book_from
    pairs = probe_pairs(a.baseline, probes, legs)
    if pre is not None:
        # score only the pairs the precomputed file knows; the rest need --probes
        pairs = [p for p in pairs if ("%s@b%d" % (p[0], p[1])) in pre or a.probes]
        if a.book_from:
            # re-book the pairs whose booked column was never computed (an older probe_words.json, or no images then)
            for k in pre:
                if pre[k].get("booking", "").startswith("not computed"):
                    pre[k].pop("words_booked_console", None)
    score = compute_score(a.battery_dir, table_lines, pairs, rows, a.ctest_log, pre,
                          reps_required=not a.reps_not_required, rebased=rebased_pairs(a.baseline, legs),
                          pairing=pairing_notes(a.baseline, legs, probes), classes=a.classes, mask=a.mask)
    score["mask"] = a.mask
    score["battery_dir"] = os.path.abspath(a.battery_dir)
    out = a.out or os.path.join(a.battery_dir, "score.json")
    with open(out, "w") as f:
        json.dump(score, f, indent=1, sort_keys=True)
    print(render_table(score))
    print("score.json: %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
