#!/usr/bin/env python3
"""rebaseline.py -- the re-baseline MANIFEST of a frozen leg and the EFFECTIVE REFERENCE it defines
(DECISIONS_LOG DT-4, proposed). The frozen OLD baseline files (`frames.fhash`, `audio.ahash`, `mem.mhash`,
`run.meta`, `probe/old_b<N>.ps2st`) are NEVER edited; a window re-baselined from NEW is recorded NEXT to them:

  games/<title>/verify/baseline/<leg>/REBASELINE.tsv       one row per re-based window (tab-separated, `#` comments):
      lane  window  rows  commit  proof  old_line  new_line  image  note
        lane     mem | audio | frames
        window   the lane's key as written in the lane file, prefixed: mem `b<N>` (checkpoint N = probe
                 boundary N), audio `W<N>` (window N), frames `k<N>` (vsync key N)
        rows     comma list of the §7.1 OLD-defect row ids that attribute EVERY residual word of the window
        commit   the NEW source commit whose leg produced the reference (`git rev-parse --short HEAD`)
        proof    the ledger file (relative to the leg dir) whose kinds the gate checked: 0 OPEN / 0 MIXED /
                 0 UNATTRIBUTED, every word ROW / DP (+ NONSTATE)
        old_line the frozen OLD lane line the reference replaces (kept for the record); `-` for an
                 IMAGE-ONLY window (below)
        new_line NEW's lane line for the window (the effective reference); `-` for an image-only window
        image    for the mem lane: `rebased/new_b<N>.ps2st`, NEW's probe image at the boundary (the
                 effective reference of the probe pair); `-` for the other lanes
  IMAGE-ONLY windows: the mem lane's checkpoints are every 60 boundaries, but the score's probe pairs sit
  also at boundary 2 (the anchors' `old_b2`), where neither tree writes a `mem.mhash` line. Such a window
  is a mem row with `old_line` = `new_line` = `-` and the image set: the probe pair's reference is the
  image, the lane lines are left untouched (`effective_lines` skips the row; `effective_probe` serves it).
  games/<title>/verify/baseline/<leg>/rebased/              the referenced NEW artefacts (the proof copy, the image)

Readers (`diff_legs.py`, `score.py`, `gate_run.py`/`battery.py` through them) compare the lane window / probe
pair against the effective reference and REPORT every re-based window -- the verdict line carries
`rebased: mem b120 (ROW-ID, ...)`; score.py a `rebased_pairs` column -- never silently. The writer is
`apply_rebaseline.py` (ledger-gated). This module is the reader + the grammar; no file is written here.
"""
import os
import re

MANIFEST = "REBASELINE.tsv"
REBASED_DIR = "rebased"
LANES = ("mem", "audio", "frames")
LANE_FILE = {"mem": "mem.mhash", "audio": "audio.ahash", "frames": "frames.fhash"}
WINDOW_PREFIX = {"mem": "b", "audio": "W", "frames": "k"}
COLUMNS = ("lane", "window", "rows", "commit", "proof", "old_line", "new_line", "image", "note")
ROW_ID_RE = re.compile(r"^P[0-9]+-[A-Z0-9][A-Z0-9-]*$")


class ManifestError(ValueError):
    """A manifest that must not be applied: bad grammar, an unknown lane, a window that does not parse."""


def parse_window(lane, window):
    """`b120` / `W12` / `k69` -> the integer key; a bare integer is accepted too."""
    p = WINDOW_PREFIX[lane]
    w = window.strip()
    if w.startswith(p):
        w = w[len(p):]
    if not w.isdigit():
        raise ManifestError("window %r is not %s<N> for lane %s" % (window, p, lane))
    return int(w)


def window_text(lane, key):
    return "%s%d" % (WINDOW_PREFIX[lane], key)


def read_manifest(leg_dir):
    """-> [{lane, window, key, rows:[...], commit, proof, old_line, new_line, image, note, line}] of the leg's
    REBASELINE.tsv; [] when the leg carries none. Grammar errors raise ManifestError naming the line."""
    path = os.path.join(leg_dir, MANIFEST)
    out = []
    if not os.path.isfile(path):
        return out
    with open(path) as f:
        for n, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            cols = line.split("\t")
            if len(cols) < 8:
                raise ManifestError("%s:%d: expected %d tab-separated columns, got %d" % (path, n, len(COLUMNS), len(cols)))
            cols = cols + [""] * (len(COLUMNS) - len(cols))
            e = dict(zip(COLUMNS, (c.strip() for c in cols)))
            if e["lane"] not in LANES:
                raise ManifestError("%s:%d: lane %r not in %r" % (path, n, e["lane"], LANES))
            e["key"] = parse_window(e["lane"], e["window"])
            e["rows"] = [r for r in e["rows"].split(",") if r]
            if not e["rows"] or not all(ROW_ID_RE.match(r) for r in e["rows"]):
                raise ManifestError("%s:%d: rows %r must be a comma list of row ids" % (path, n, e["rows"]))
            if e["new_line"] == "-":
                # an image-only window: mem lane, the image is the reference, no lane line on either side
                if e["lane"] != "mem" or not e["image"] or e["image"] == "-":
                    raise ManifestError("%s:%d: new_line '-' (image-only window) needs lane mem and an image" % (path, n))
                if e["old_line"] != "-":
                    raise ManifestError("%s:%d: an image-only window carries old_line '-' too, got %r" % (path, n, e["old_line"]))
            elif "," not in e["new_line"] or not e["new_line"].split(",", 1)[0].strip().isdigit() \
                    or int(e["new_line"].split(",", 1)[0]) != e["key"]:
                raise ManifestError("%s:%d: new_line %r does not carry key %d" % (path, n, e["new_line"], e["key"]))
            e["image_only"] = e["new_line"] == "-"
            e["line"] = n
            out.append(e)
    return out


def by_lane(entries, lane):
    """{key: entry} of one lane."""
    return {e["key"]: e for e in entries if e["lane"] == lane}


def effective_lines(leg_dir, lane, lines, entries=None):
    """The frozen lane lines with every re-based window's line replaced by the manifest's new_line.
    -> (lines, [entry ...] applied). A re-based key absent from the frozen lines is appended in key order
    (an OLD leg that skipped the key -- a coalesced present -- gains the NEW reference line)."""
    entries = read_manifest(leg_dir) if entries is None else entries
    table = {k: e for k, e in by_lane(entries, lane).items() if not e.get("image_only")}   # image-only windows leave the lane untouched
    if not table:
        return list(lines), []
    out, applied, seen = [], [], set()
    for l in lines:
        k = l.split(",", 1)[0].strip()
        if k.isdigit() and int(k) in table:
            e = table[int(k)]
            out.append(e["new_line"])
            applied.append(e)
            seen.add(int(k))
        else:
            out.append(l)
    for k in sorted(set(table) - seen):
        out.append(table[k]["new_line"])
        applied.append(table[k])
    return out, applied


def effective_probe(leg_dir, boundary, entries=None):
    """The probe pair's reference at `boundary`: (path, entry|None) -- the manifest's rebased image when the
    mem window b<N> is re-based (the file must exist), else the frozen `probe/old_b<N>.ps2st`."""
    entries = read_manifest(leg_dir) if entries is None else entries
    e = by_lane(entries, "mem").get(boundary)
    frozen = os.path.join(leg_dir, "probe", "old_b%d.ps2st" % boundary)
    if e is None or not e["image"] or e["image"] == "-":
        return frozen, None
    path = os.path.join(leg_dir, e["image"])
    if not os.path.isfile(path):
        raise ManifestError("%s: rebased image %s is missing" % (os.path.join(leg_dir, MANIFEST), e["image"]))
    return path, e


def report_text(applied):
    """`rebased: mem b120 (ROW-A, ROW-B), audio W1 (ROW-C)` for the verdict line; '' when nothing applied."""
    if not applied:
        return ""
    parts = ["%s %s (%s)" % (e["lane"], e["window"], ", ".join(e["rows"])) for e in applied]
    return "rebased: " + ", ".join(parts)


def format_row(e):
    return "\t".join(str(e.get(c, "")) if c != "rows" else ",".join(e["rows"]) for c in COLUMNS)


def header():
    return "# " + "\t".join(COLUMNS) + "\n"


def selftest():
    import tempfile
    import shutil
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)
    tmp = tempfile.mkdtemp(prefix="rebaseline_")
    leg = os.path.join(tmp, "leg")
    os.makedirs(os.path.join(leg, "probe"))
    os.makedirs(os.path.join(leg, "rebased"))
    check(read_manifest(leg) == [], "a leg without a manifest reads as []")
    with open(os.path.join(leg, MANIFEST), "w") as f:
        f.write(header())
        f.write("mem\tb120\tP9-TEST-ROW,P9-TEST-ROW2\tabc1234\trebased/mem_b120.ledger.json\t120,0xold\t120,0xnew\trebased/new_b120.ps2st\tnote\n")
        f.write("audio\tW3\tP9-TEST-ROW\tabc1234\trebased/audio_W3.ledger.json\t3,0xold3\t3,0xnew3\t-\t\n")
        f.write("mem\tb2\tP9-TEST-ROW\tabc1234\trebased/mem_b2.ledger.json\t-\t-\trebased/new_b2.ps2st\timage-only\n")
    ents = read_manifest(leg)
    check(len(ents) == 3 and ents[0]["key"] == 120 and ents[0]["rows"] == ["P9-TEST-ROW", "P9-TEST-ROW2"] and ents[1]["key"] == 3 and ents[2]["key"] == 2
          and ents[2]["image_only"] and not ents[0]["image_only"],
          "three rows parsed: mem b120 (two rows), audio W3, the IMAGE-ONLY mem b2 -> %r" % [(e["lane"], e["key"], e["image_only"]) for e in ents])
    lines, applied = effective_lines(leg, "mem", ["0,0xa", "60,0xb", "120,0xold"], ents)
    check(lines == ["0,0xa", "60,0xb", "120,0xnew"] and [e["window"] for e in applied] == ["b120"],
          "the mem line of the re-based window is replaced; the image-only b2 row leaves the lane untouched (not appended) -> %r" % lines)
    open(os.path.join(leg, "rebased", "new_b2.ps2st"), "wb").write(b"y")
    pb2, eb2 = effective_probe(leg, 2, ents)
    check(pb2.endswith("rebased/new_b2.ps2st") and eb2["image_only"], "the probe pair at the image-only window b2 is served the rebased image")
    lines2, applied2 = effective_lines(leg, "frames", ["1,0x0"], ents)
    check(lines2 == ["1,0x0"] and applied2 == [], "a lane without a re-based window is untouched")
    lines3, applied3 = effective_lines(leg, "audio", ["0,0x0", "1,0x0"], ents)
    check(lines3 == ["0,0x0", "1,0x0", "3,0xnew3"] and len(applied3) == 1, "a re-based key the frozen lane lacks is appended -> %r" % lines3)
    check(report_text(applied + applied3) == "rebased: mem b120 (P9-TEST-ROW, P9-TEST-ROW2), audio W3 (P9-TEST-ROW)", "the verdict-line report text")
    for bad in ("audio\tW1\tP9-X\tc\tp\t-\t-\t-\n", "mem\tb1\tP9-X\tc\tp\t-\t-\t-\n", "mem\tb1\tP9-X\tc\tp\t1,0x1\t-\trebased/new_b1.ps2st\n"):
        with open(os.path.join(leg, MANIFEST), "w") as f:
            f.write(bad)
        try:
            read_manifest(leg)
            check(False, "image-only shape accepted wrongly: %r" % bad)
        except ManifestError as ex:
            check(True, "image-only shape refused (%s)" % str(ex).split(": ", 1)[-1][:70])
    with open(os.path.join(leg, MANIFEST), "w") as f:
        f.write(header())
        f.write("mem\tb120\tP9-TEST-ROW,P9-TEST-ROW2\tabc1234\trebased/mem_b120.ledger.json\t120,0xold\t120,0xnew\trebased/new_b120.ps2st\tnote\n")
        f.write("audio\tW3\tP9-TEST-ROW\tabc1234\trebased/audio_W3.ledger.json\t3,0xold3\t3,0xnew3\t-\t\n")
    ents = read_manifest(leg)
    try:
        effective_probe(leg, 120, ents)
        check(False, "a missing rebased image must raise")
    except ManifestError as e:
        check("missing" in str(e), "a missing rebased image raises -> %s" % e)
    open(os.path.join(leg, "rebased", "new_b120.ps2st"), "wb").write(b"x")
    p, e = effective_probe(leg, 120, ents)
    check(p.endswith("rebased/new_b120.ps2st") and e["window"] == "b120", "the probe reference at a re-based boundary is the rebased image")
    p2, e2 = effective_probe(leg, 60, ents)
    check(p2.replace(os.sep, "/").endswith("probe/old_b60.ps2st") and e2 is None, "a boundary without a row keeps the frozen probe")
    for bad in ("foo\tb1\tP9-X\tc\tp\to\t1,0x1\t-\n", "mem\tW1\tP9-X\tc\tp\to\t1,0x1\t-\n", "mem\tb1\t\tc\tp\to\t1,0x1\t-\n",
                "mem\tb1\tP9-X\tc\tp\to\t2,0x1\t-\n", "mem\tb1\tP9-X\tc\n", "mem\tb1\tbad id\tc\tp\to\t1,0x1\t-\n"):
        with open(os.path.join(leg, MANIFEST), "w") as f:
            f.write(bad)
        try:
            read_manifest(leg)
            check(False, "bad row accepted: %r" % bad)
        except ManifestError as ex:
            check(True, "bad row refused (%s)" % str(ex).split(": ", 1)[-1][:60])
    check(parse_window("mem", "120") == 120 and window_text("audio", 3) == "W3", "bare integer window accepted; window_text")
    shutil.rmtree(tmp, ignore_errors=True)
    print("rebaseline selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    import sys
    sys.exit(selftest() if "--selftest" in sys.argv else (print(__doc__) or 0))
