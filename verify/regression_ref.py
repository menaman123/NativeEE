#!/usr/bin/env python3
"""regression_ref.py -- the standing REGRESSION gate over the 13-leg battery (DECISIONS_LOG DT-8, P8.35-C).

The reference table `REGRESSION_REF.tsv` (written next to the frozen legs, `games/<title>/verify/baseline/` today) carries, per
leg, the first-divergent key of the frames / audio / mem lanes and the raw / booked / unbooked outside-mask word
counts of the gate of record, plus one `ALL` row with the battery totals; its header comments carry the gate's
`legs_identical`, `pins_green` and source. `score.py --write-regression-ref <score.json> <out.tsv>` writes it from a
gate's score.json; `score.py --regression <ref.tsv> <candidate score.json>` decides PASS / FAIL.

The rule (the user's decision of 2026-10-06 11:23, SHARED.md "PHASE 8 PRODUCT HALF" (2)(1)):
  * the RAW tuple and the UNBOOKED tuple must not worsen: `legs_identical` not lower, the battery's outside-mask word
    total (raw AND unbooked) not higher;
  * every leg's FRAMES and AUDIO first-divergent key must stay at or above the reference's (a key that moved LATER is
    better; a lane that is IDENTICAL end to end -- no first-divergent line -- ranks above every key and is written
    `IDENTICAL`; a leg missing from the candidate, RUN-FAIL or META-FAIL, FAILS);
  * the MEM lane and the per-leg word counts are REPORTED, never a criterion;
  * a pins regression (both sides ran ctest and the candidate passed fewer) FAILS; pins not run on a side is reported.
A key of 0 is a REAL key (audio W0, mem b0 on several legs), so identity is never encoded as 0 -- `IDENTICAL` is the
token (the prompt's `0 = identical lane` shorthand would collide with f36935/f39807/f57292/slowspot_f40298's audio W0).

`--selftest` = ctest `VerifyRegressionRefSelftest` (a worse key, a better key, an identical lane, a worse raw total, a
worse unbooked total with a better raw one, a missing leg, a pins regression, the TSV round trip).
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FILE = "REGRESSION_REF.tsv"
COLUMNS = ("leg", "frames_first_div", "audio_first_div", "mem_first_div", "raw_words", "booked", "unbooked", "source_gate")
LANES = ("frames", "audio", "mem")
DECIDING_LANES = ("frames", "audio")
IDENTICAL = "IDENTICAL"
ALL = "ALL"
DIVERGENT_RE = re.compile(r"DIVERGENT\(([^)]*)\)")
HEADER_RE = re.compile(r"^#\s*(legs_identical|legs_in_table|pins_green|source|tuple|tuple_unbooked)=(.*)$")


def default_path(baseline_dir):
    return os.path.join(baseline_dir, FILE)


# ---------------------------------------------------------------- from a score.json

def lane_key(per_leg_entry, lane):
    """The lane's first-divergent key from a score.json per_leg entry: an int, IDENTICAL when the leg's row does not
    list the lane as divergent (META-OK and no first-divergent line), or None when the row is not META-OK or the
    lane is listed divergent but no key was read (fail-closed: the caller treats None as the worst key)."""
    row = per_leg_entry.get("row", "")
    fd = per_leg_entry.get("first_divergent", {})
    if not row.startswith("META-OK"):
        return None
    if lane in fd:
        return int(fd[lane])
    m = DIVERGENT_RE.search(row)
    divergent = tuple(x.strip() for x in m.group(1).split(",")) if m else ()
    return None if lane in divergent else IDENTICAL


def rows_from_score(score, source):
    """[{column: value}] per leg (sorted) + the ALL row, from a score.json dict."""
    per_leg = score.get("per_leg", {})
    words = {}
    for p in score.get("pairs", []):
        w = words.setdefault(p["leg"], [0, 0])
        w[0] += int(p.get("words_raw", p["total"]))
        w[1] += int(p.get("words_booked_console", 0))
    rows = []
    for leg in sorted(per_leg):
        raw, booked = words.get(leg, [0, 0])
        keys = {}
        for lane in LANES:
            k = lane_key(per_leg[leg], lane)
            keys[lane] = "?" if k is None else str(k)
        rows.append({"leg": leg, "frames_first_div": keys["frames"], "audio_first_div": keys["audio"],
                     "mem_first_div": keys["mem"], "raw_words": str(raw), "booked": str(booked),
                     "unbooked": str(raw - booked), "source_gate": source})
    raw_all = int(score["outside_mask_words"])
    booked_all = int(score.get("booked_console_words", 0))
    rows.append({"leg": ALL, "frames_first_div": "-", "audio_first_div": "-", "mem_first_div": "-",
                 "raw_words": str(raw_all), "booked": str(booked_all), "unbooked": str(raw_all - booked_all),
                 "source_gate": source})
    return rows


def header_lines(score, source):
    t = score["tuple"]
    tu = score.get("tuple_unbooked", t)
    return ["# %s -- the standing regression gate's reference (DECISIONS_LOG DT-8; written by score.py --write-regression-ref)" % FILE,
            "# source=%s" % source,
            "# legs_identical=%d" % int(score["legs_identical"]),
            "# legs_in_table=%d" % int(score.get("legs_in_table", len(score.get("per_leg", {})))),
            "# pins_green=%d" % int(score["pins_green"]),
            "# tuple=%s" % json.dumps(list(t)),
            "# tuple_unbooked=%s" % json.dumps(list(tu)),
            "# rule: score.py --regression <this file> <candidate score.json> -> PASS only if legs_identical is not lower, the raw AND",
            "#   unbooked word totals (the ALL row) are not higher, every leg's frames/audio first-divergent key is >= the reference",
            "#   (IDENTICAL = the lane never diverges, above every key; 0 is a real key), no leg is missing, and no pins regression.",
            "#   mem_first_div and the per-leg word columns are REPORTED only. Never edited by hand: regenerate from the gate of record."]


def format_ref(score, source):
    lines = header_lines(score, source) + ["\t".join(COLUMNS)]
    for r in rows_from_score(score, source):
        lines.append("\t".join(r[c] for c in COLUMNS))
    return "\n".join(lines) + "\n"


def write_ref(score_json_path, out_path, source=None):
    with open(score_json_path) as f:
        score = json.load(f)
    source = source or os.path.basename(os.path.dirname(os.path.abspath(score_json_path)))
    text = format_ref(score, source)
    with open(out_path, "w") as f:
        f.write(text)
    return text


# ---------------------------------------------------------------- the reference file

def read_ref(path):
    """{'header': {...}, 'legs': {leg: row dict}, 'all': row dict}; malformed = ValueError (never a silent default)."""
    header, legs, all_row, columns = {}, {}, None, None
    with open(path) as f:
        for n, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            if not line.strip():
                continue
            if line.startswith("#"):
                m = HEADER_RE.match(line.strip())
                if m:
                    header[m.group(1)] = m.group(2).strip()
                continue
            cells = line.split("\t")
            if columns is None:
                if tuple(cells) != COLUMNS:
                    raise ValueError("%s:%d: header columns %r != %r" % (path, n, cells, COLUMNS))
                columns = cells
                continue
            if len(cells) != len(COLUMNS):
                raise ValueError("%s:%d: %d columns, expected %d" % (path, n, len(cells), len(COLUMNS)))
            row = dict(zip(COLUMNS, cells))
            for c in ("raw_words", "booked", "unbooked"):
                if not row[c].lstrip("-").isdigit():
                    raise ValueError("%s:%d: %s=%r is not an integer" % (path, n, c, row[c]))
            if row["leg"] == ALL:
                all_row = row
            else:
                for c in ("frames_first_div", "audio_first_div", "mem_first_div"):
                    if not (row[c] == IDENTICAL or row[c].isdigit()):
                        raise ValueError("%s:%d: %s=%r must be an integer key or %s" % (path, n, c, row[c], IDENTICAL))
                legs[row["leg"]] = row
    if columns is None or all_row is None or not legs:
        raise ValueError("%s: no header row / no ALL row / no leg rows" % path)
    for k in ("legs_identical", "pins_green"):
        if k not in header:
            raise ValueError("%s: header line '# %s=' missing" % (path, k))
    return {"header": header, "legs": legs, "all": all_row}


# ---------------------------------------------------------------- the decision

def key_rank(k):
    """Order of a first-divergent key: IDENTICAL above every integer; None (unknown) below every integer."""
    if k == IDENTICAL:
        return float("inf")
    if k is None:
        return float("-inf")
    return int(k)


def key_text(k):
    return "?" if k is None else str(k)


def decide(ref, score):
    """(verdict 'PASS'|'FAIL', reasons [str], per-leg table rows [dict]) for a candidate score.json dict against a ref."""
    reasons = []
    table = []
    cand_rows = {r["leg"]: r for r in rows_from_score(score, "candidate")}
    cand_all = cand_rows.pop(ALL)
    # (1) the tuples
    ref_li = int(ref["header"]["legs_identical"])
    cand_li = int(score["legs_identical"])
    if cand_li < ref_li:
        reasons.append("legs_identical %d < reference %d" % (cand_li, ref_li))
    for col, name in (("raw_words", "raw"), ("unbooked", "unbooked")):
        rv, cv = int(ref["all"][col]), int(cand_all[col])
        if cv > rv:
            reasons.append("%s outside-mask words %d > reference %d (+%d)" % (name, cv, rv, cv - rv))
    ref_pins, cand_pins = int(ref["header"]["pins_green"]), int(score["pins_green"])
    if ref_pins >= 0 and cand_pins >= 0 and cand_pins < ref_pins:
        reasons.append("pins_green %d < reference %d" % (cand_pins, ref_pins))
    # (2) per leg: frames/audio keys not lower; mem and words reported
    for leg, r in sorted(ref["legs"].items()):
        c = cand_rows.get(leg)
        row = {"leg": leg}
        if c is None:
            reasons.append("%s: missing from the candidate (RUN-FAIL / not run)" % leg)
            for lane in LANES:
                row[lane] = "%s -> MISSING" % r[lane + "_first_div"]
            row["words"] = "%s/%s -> MISSING" % (r["raw_words"], r["unbooked"])
            row["verdict"] = "FAIL"
            table.append(row)
            continue
        leg_ok = True
        for lane in LANES:
            rk = r[lane + "_first_div"]
            ck = c[lane + "_first_div"]
            ck_v = None if ck == "?" else ck
            mark = ""
            if key_rank(ck_v) < key_rank(rk):
                mark = " WORSE" if lane in DECIDING_LANES else " (worse, reported)"
                if lane in DECIDING_LANES:
                    leg_ok = False
                    reasons.append("%s: %s first-divergent key %s < reference %s" % (leg, lane, key_text(ck_v), rk))
            elif key_rank(ck_v) > key_rank(rk):
                mark = " better"
            row[lane] = "%s -> %s%s" % (rk, key_text(ck_v), mark)
        dr, dc = int(c["raw_words"]) - int(r["raw_words"]), int(c["unbooked"]) - int(r["unbooked"])
        row["words"] = "%s/%s -> %s/%s (%+d/%+d, reported)" % (r["raw_words"], r["unbooked"], c["raw_words"], c["unbooked"], dr, dc)
        row["verdict"] = "ok" if leg_ok else "FAIL"
        table.append(row)
    for leg in sorted(set(cand_rows) - set(ref["legs"])):
        table.append({"leg": leg, "frames": "(not in the reference)", "audio": "-", "mem": "-", "words": "-", "verdict": "reported"})
    return ("PASS" if not reasons else "FAIL"), reasons, table


def render(ref_path, score, verdict, reasons, table):
    ref = read_ref(ref_path)
    t, tu = score["tuple"], score.get("tuple_unbooked", score["tuple"])
    out = ["regression: %s vs %s" % (verdict, os.path.basename(ref_path)),
           "  reference: %s legs_identical=%s pins_green=%s raw=%s unbooked=%s" % (
               ref["header"].get("source", "?"), ref["header"]["legs_identical"], ref["header"]["pins_green"], ref["all"]["raw_words"], ref["all"]["unbooked"]),
           "  candidate: legs_identical=%d pins_green=%d raw=%d unbooked=%d  tuple=%s tuple_unbooked=%s" % (
               int(score["legs_identical"]), int(score["pins_green"]), int(t[1]), int(tu[1]), tuple(t), tuple(tu))]
    for r in reasons:
        out.append("  FAIL: %s" % r)
    out.append("  %-38s %-26s %-22s %-22s %-48s %s" % ("leg", "frames (ref -> cand)", "audio", "mem (reported)", "raw/unbooked words (reported)", "verdict"))
    for row in table:
        out.append("  %-38s %-26s %-22s %-22s %-48s %s" % (row["leg"], row.get("frames", "-"), row.get("audio", "-"), row.get("mem", "-"), row.get("words", "-"), row["verdict"]))
    out.append("  rule: raw + unbooked tuples not worse, frames/audio first-divergent keys not lower per leg (IDENTICAL > every key), no missing leg, no pins regression; mem + per-leg words reported only (DT-8)")
    return "\n".join(out)


def run(ref_path, score_json_path):
    """The --regression entry: prints the report, returns 0 PASS / 1 FAIL / 2 usage."""
    if not os.path.isfile(ref_path):
        print("regression: no reference at %s (usage error)" % ref_path)
        return 2
    try:
        ref = read_ref(ref_path)
    except ValueError as e:
        print("regression: malformed reference: %s" % e)
        return 2
    with open(score_json_path) as f:
        score = json.load(f)
    verdict, reasons, table = decide(ref, score)
    print(render(ref_path, score, verdict, reasons, table))
    return 0 if verdict == "PASS" else 1


# ---------------------------------------------------------------- selftest

def _score(legs, raw_total, booked_total, li=0, pins=448, rows=None):
    """A synthetic score.json: legs = {leg: (frames_key|IDENTICAL|None, audio_key, mem_key, raw, booked)}."""
    per_leg, pairs = {}, []
    for leg, (fk, ak, mk, raw, booked) in legs.items():
        fd = {}
        div = []
        for lane, k in (("frames", fk), ("audio", ak), ("mem", mk)):
            if k == IDENTICAL:
                continue
            div.append(lane)
            if k is not None:
                fd[lane] = k
        row = (rows or {}).get(leg) or ("META-OK DIVERGENT(%s)" % ",".join(div) if div else "META-OK IDENTICAL(frames,audio,mem)")
        per_leg[leg] = {"row": row, "reps": "ADMISSIBLE", "first_divergent": fd}
        pairs.append({"leg": leg, "boundary": 60, "total": raw, "words_raw": raw, "words_booked_console": booked, "words_unbooked": raw - booked})
    return {"tuple": [li, raw_total, pins], "tuple_unbooked": [li, raw_total - booked_total, pins], "legs_identical": li,
            "legs_in_table": len(legs), "outside_mask_words": raw_total, "booked_console_words": booked_total,
            "outside_mask_words_unbooked": raw_total - booked_total, "pins_green": pins, "per_leg": per_leg, "pairs": pairs}


def selftest():
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)

    tmp = tempfile.mkdtemp(prefix="regression_ref_")
    base = {"coldboot": (69, 142, 0, 1000, 100), "artdet": (37157, 1, 0, 500, 0), "f36935": (36936, 0, 0, 200, 0)}
    ref_score = _score(base, 1700, 100)
    ref_path = os.path.join(tmp, FILE)
    sj = os.path.join(tmp, "score.json")
    with open(sj, "w") as f:
        json.dump(ref_score, f)
    text = write_ref(sj, ref_path, "S-test")
    ref = read_ref(ref_path)
    check(set(ref["legs"]) == set(base) and ref["all"]["raw_words"] == "1700" and ref["all"]["unbooked"] == "1600"
          and ref["header"]["legs_identical"] == "0" and ref["header"]["pins_green"] == "448" and ref["header"]["source"] == "S-test",
          "the TSV round trip: three leg rows, the ALL row (raw 1700 / unbooked 1600), the header -> %r" % ref["all"])
    check(ref["legs"]["coldboot"]["frames_first_div"] == "69" and ref["legs"]["f36935"]["audio_first_div"] == "0" and ref["legs"]["coldboot"]["unbooked"] == "900",
          "per-leg keys and words (coldboot frames 69, f36935 audio W0 is a real key, coldboot unbooked 900)")
    check("\t".join(COLUMNS) in text and text.count("\n") == len(header_lines(ref_score, "x")) + 1 + len(base) + 1, "the file has the header comments, the column header, one row per leg and the ALL row")
    # the reference against itself: PASS
    v, reasons, table = decide(ref, ref_score)
    check(v == "PASS" and not reasons and all(r["verdict"] == "ok" for r in table), "the reference's own score PASSES against its ref -> %r" % reasons)
    # a worse key (frames earlier) -> FAIL on that leg
    s = _score(dict(base, coldboot=(68, 142, 0, 1000, 100)), 1700, 100)
    v, reasons, table = decide(ref, s)
    check(v == "FAIL" and any("coldboot: frames first-divergent key 68 < reference 69" in r for r in reasons), "a frames key that moved EARLIER fails the leg -> %r" % reasons)
    # a better key (audio later) -> PASS, marked better
    s = _score(dict(base, coldboot=(69, 150, 0, 1000, 100)), 1700, 100)
    v, reasons, table = decide(ref, s)
    check(v == "PASS" and any("142 -> 150 better" in r["audio"] for r in table), "an audio key that moved LATER passes and is marked better")
    # an identical lane in the candidate ranks above every key; an identical lane in the ref beaten by a key fails
    s = _score(dict(base, artdet=(IDENTICAL, 1, 0, 500, 0)), 1700, 100)
    v, reasons, table = decide(ref, s)
    check(v == "PASS" and any("37157 -> IDENTICAL better" in r["frames"] for r in table), "a lane that became IDENTICAL passes (above every key)")
    ref2_score = _score(dict(base, artdet=(IDENTICAL, 1, 0, 500, 0)), 1700, 100)
    ref2_path = os.path.join(tmp, "ref2.tsv")
    with open(os.path.join(tmp, "s2.json"), "w") as f:
        json.dump(ref2_score, f)
    write_ref(os.path.join(tmp, "s2.json"), ref2_path, "S-test2")
    v, reasons, _ = decide(read_ref(ref2_path), _score(base, 1700, 100))
    check(v == "FAIL" and any("artdet: frames first-divergent key 37157 < reference IDENTICAL" in r for r in reasons), "a reference IDENTICAL lane that now diverges fails")
    # a worse mem key is REPORTED only
    s = _score(dict(base, coldboot=(69, 142, None, 1000, 100)), 1700, 100)
    v, reasons, table = decide(ref, s)
    check(v == "PASS" and any("(worse, reported)" in r["mem"] for r in table), "the mem lane is reported, never a criterion (an unknown mem key passes)")
    # a worse raw total -> FAIL; a better raw but worse unbooked -> FAIL; both equal -> PASS
    v, reasons, _ = decide(ref, _score(dict(base, coldboot=(69, 142, 0, 1001, 100)), 1701, 100))
    check(v == "FAIL" and any("raw outside-mask words 1701 > reference 1700 (+1)" in r for r in reasons), "a higher raw total fails")
    v, reasons, _ = decide(ref, _score(dict(base, coldboot=(69, 142, 0, 999, 50)), 1699, 50))
    check(v == "FAIL" and any("unbooked outside-mask words 1649 > reference 1600 (+49)" in r for r in reasons) and not any("raw" in r for r in reasons),
          "a lower raw total with a higher unbooked total fails on the unbooked tuple -> %r" % reasons)
    v, reasons, _ = decide(ref, _score(dict(base, coldboot=(69, 142, 0, 990, 100)), 1690, 100))
    check(v == "PASS", "fewer words with equal keys passes")
    # per-leg words are reported, not decided: one leg up, another down, totals equal -> PASS
    v, reasons, table = decide(ref, _score(dict(base, coldboot=(69, 142, 0, 1010, 100), artdet=(37157, 1, 0, 490, 0)), 1700, 100))
    check(v == "PASS" and any("(+10/+10, reported)" in r["words"] for r in table), "per-leg word moves are reported, the totals decide")
    # legs_identical lower -> FAIL; higher -> PASS
    s = _score(base, 1700, 100, li=1)
    v, reasons, _ = decide(ref, s)
    check(v == "PASS", "more identical legs passes")
    with open(os.path.join(tmp, "s3.json"), "w") as f:
        json.dump(s, f)
    write_ref(os.path.join(tmp, "s3.json"), os.path.join(tmp, "ref3.tsv"), "S3")
    v, reasons, _ = decide(read_ref(os.path.join(tmp, "ref3.tsv")), _score(base, 1700, 100))
    check(v == "FAIL" and "legs_identical 0 < reference 1" in reasons, "fewer identical legs fails")
    # a missing leg -> FAIL
    v, reasons, table = decide(ref, _score({k: v_ for k, v_ in base.items() if k != "f36935"}, 1500, 100))
    check(v == "FAIL" and any("f36935: missing from the candidate" in r for r in reasons) and any(r["verdict"] == "FAIL" and "MISSING" in r["frames"] for r in table),
          "a leg missing from the candidate fails")
    # pins: regression fails; not run on one side is not a claim
    v, reasons, _ = decide(ref, _score(base, 1700, 100, pins=447))
    check(v == "FAIL" and "pins_green 447 < reference 448" in reasons, "a pins regression fails")
    v, reasons, _ = decide(ref, _score(base, 1700, 100, pins=-1))
    check(v == "PASS", "pins not run on the candidate: no regression claim")
    # a divergent lane without a key is fail-closed (unknown = worst)
    s = _score(dict(base, coldboot=(None, 142, 0, 1000, 100)), 1700, 100)
    v, reasons, _ = decide(ref, s)
    check(v == "FAIL" and any("coldboot: frames first-divergent key ? < reference 69" in r for r in reasons), "a divergent frames lane with no readable key fails closed")
    # a RUN-FAIL row -> every lane unknown -> FAIL
    s = _score(base, 1700, 100, rows={"artdet": "RUN-FAIL"})
    v, reasons, _ = decide(ref, s)
    check(v == "FAIL" and any("artdet: frames first-divergent key ? < reference 37157" in r for r in reasons), "a RUN-FAIL leg fails")
    # malformed refs refuse
    bad = os.path.join(tmp, "bad.tsv")
    with open(bad, "w") as f:
        f.write("# legs_identical=0\n# pins_green=1\n" + "\t".join(COLUMNS) + "\ncoldboot\t0\tx\t0\t1\t0\t1\tS\nALL\t-\t-\t-\t1\t0\t1\tS\n")
    try:
        read_ref(bad)
        check(False, "a malformed key refuses")
    except ValueError as e:
        check("audio_first_div='x'" in str(e), "a malformed key refuses -> %s" % e)
    check(run(os.path.join(tmp, "none.tsv"), sj) == 2, "no reference file -> usage error 2")
    check(run(ref_path, sj) == 0, "run(): the reference against its own score -> 0 (PASS)")
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)
    print("regression_ref selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(selftest() if "--selftest" in sys.argv or len(sys.argv) == 1 else run(sys.argv[1], sys.argv[2]))
