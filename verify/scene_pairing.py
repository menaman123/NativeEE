#!/usr/bin/env python3
"""scene_pairing.py -- the PER-RUN scene-edge pairing of a leg whose timelines differ from OLD's
(DECISIONS_LOG DT-6; the `coldboot_attract` leg).

  scene_pairing.py --selftest
  (library: probe_images.py derives, score.py / gate_run.py / diff_legs.py consume)

WHY PER RUN. The leg's LEG.tsv `pair_map` (verify/leg_spec.py) is a FROZEN constant: the boot-SM
case 6 -> 7 edge measured ONCE on each tree (`old_b850=new_b1133`, offset +283). Any NEW change that
moves the load timeline (fewer drive requests, another delivery model) moves NEW's edge, the frozen
offset goes stale, and the probe pairs compare DIFFERENT SCENES again -- the artefact DT-6 exists to
remove (P8.29-A: `coldboot_attract b3780 +486,677 words` under a model that is console-exact on its
class). So the edge is derived from the run's OWN images, the frozen map is kept as the REFERENCE
value, and every deviation is REPORTED on the pair's note and the verdict line -- never silently.

THE EDGE. The boot state machine's word (SCENE_KEYS.tsv `boot_sm_word`; case = bits 44..49) goes
6 (the Beach hub load) -> 7 (the title: cutscene pass 1) exactly once on the first pass; the first
boundary whose image reads case 7 after a case-6 image is the edge E'. OLD's edge E is the frozen
map's first token (its image is recorded in the leg's FREEZE.md). The pass-2 edge (after the attract
movie's hub reload, case 14 -> 6 -> 7) is PREDICTED at E2 + (E' - E) -- the inter-edge distance was
measured equal on both trees (INT-P825T-EDGE2-SAME-OFFSET: title idle + 43 movie boundaries + the
reload) -- and, with `verify_edge2`, CHECKED by two one-image legs (case 6 at E2' - 1, case 7 at E2');
a failed check is reported as EDGE2-MISMATCH on every pass-2 pair (the images are still produced; the
mismatch is on the record, the score never hides it).

THE METHOD (image census; chosen over the alternatives, cheapest EXACT one that needs no engine change):
one NEW leg `--frames E_ref' + margin --state-every 1` whose images are read for the two scene words AS
THEY LAND and deleted at once (the harness writes `state_b<N>.ps2st.tmp` then renames -- ee_bootstrap.cpp
-- so a file that exists is complete; at most two images live on disk). Cost: ~0.25 s per boundary of
image I/O on top of the run (~6 min for 1,250 boundaries), deterministic under the harness profile
(the SM word is the title's own state). The edge is EXACT only on a DENSE census: `find_edge` requires
the case-6 image at b - 1 to be PRESENT (a harness-refused image -- `state_b%llu refused` -- or an
unreadable one at the edge gives no edge, never an edge one boundary late); `census_gaps` names every
missing boundary on the log line. Rejected: the frames lane (hashes are not portable between
runs: the load screen animates, the title frames differ by render class), the audio lane (`audio_first_
nonsilent_window` lags the edge by 1-2 windows -- a HINT, used only to sanity-check the margin), the mem
lane (every 60 boundaries). The zero-cost EXACT method needs an app/host census field (`boot_sm_case7_b=`
in run.meta when the SM word first reads 7) -- specified for a builder stage; until then this one.

THE RECORD. `<probe_dir>/<leg>/PAIRING.tsv` (grammar below) is written next to the mapped images and read
by score.py (the pair's `new_boundary` + the note), gate_run.py (SUMMARY) and diff_legs.py (`--pairing`):
  old_edge  new_edge  offset  ref_old  ref_new  ref_offset  deviation  status  method  census_frames
  edge_words  edge2_old  edge2_new  edge2_status  pair_map
`status` = EDGE-FOUND | EDGE-NOT-FOUND (no case 6 -> 7 inside the census: no mapped image is produced,
the pairs count as MISSING = the OLD image's full word count); `edge2_status` = PREDICTED |
VERIFIED | EDGE2-MISMATCH(case@E2'-1, case@E2') | n/a. The note every consumer prints:
  paired by scene edge (run: old_b<E>=new_b<E'>, offset +N; frozen reference +R, deviation +D)
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import clock_pair  # noqa: E402
import leg_spec  # noqa: E402
import memory_capture  # noqa: E402

FILE = "PAIRING.tsv"
COLUMNS = ("old_edge", "new_edge", "offset", "ref_old", "ref_new", "ref_offset", "deviation", "status", "method",
           "census_frames", "edge_words", "edge2_old", "edge2_new", "edge2_status", "pair_map")
CASE_LOAD, CASE_TITLE = 6, 7
STATE_RE = re.compile(r"^state_b(\d+)\.ps2st$")
DEFAULT_MARGIN = 150


# ---------------------------------------------------------------- the census leg

def census_leg(cmd, work, keys, poll_s=0.5, log=None):
    """Run a leg command (run_leg.py argv; images land in `work` as state_b<N>.ps2st) and census every image
    for the two scene words AS IT LANDS, deleting it at once. An image is read only once a HIGHER boundary's
    file exists or the process has exited (a file that exists is complete -- the harness renames .tmp -> final;
    the ordering rule is belt and braces). Returns (census {b: words}, exit code, log tail)."""
    os.makedirs(work, exist_ok=True)
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    census = {}
    tail = []

    def harvest(final):
        present = memory_capture.boundaries(work)
        if not present:
            return
        newest = present[-1]
        for b in present:
            if b == newest and not final:
                break
            try:
                census[b] = clock_pair.words(memory_capture.path(work, b), keys)
            except (ValueError, struct.error, OSError) as e:
                if final:
                    census[b] = None
                    if log:
                        log("census: state_b%d unreadable: %s" % (b, e))
                else:
                    continue
            memory_capture.remove(work, b)

    import threading
    lines = []

    def reader():
        for line in proc.stdout:
            lines.append(line.rstrip("\n"))
    t = threading.Thread(target=reader, daemon=True)
    t.start()
    while proc.poll() is None:
        harvest(final=False)
        time.sleep(poll_s)
    t.join(timeout=5)
    harvest(final=True)
    tail = lines[-1] if lines else ""
    return census, proc.returncode, tail


def find_edge(census, case_from=CASE_LOAD, case_to=CASE_TITLE):
    """The first census boundary reading `case_to` whose DENSE predecessor (b - 1, present in the census) reads
    `case_from`: (edge_b, edge_b - 1) or None. An unreadable image (None) or a missing boundary (a harness-refused
    image, `state_b%llu refused`) at the edge breaks the adjacency -> None (never a guess; the edge is exact to the
    boundary or not reported)."""
    for b in sorted(census):
        w = census[b]
        prev = census.get(b - 1)
        if w is not None and prev is not None and w["case"] == case_to and prev["case"] == case_from:
            return b, b - 1
    return None


def census_gaps(census):
    """The boundaries missing between the census's first and last keys (a harness-refused image leaves a hole)."""
    if not census:
        return []
    return sorted(set(range(min(census), max(census) + 1)) - set(census))


def census_text(census, lo, hi):
    """`b<N>:case<c>/bit63<f>` tokens for the census boundaries in [lo, hi] (the record of the edge's neighbourhood)."""
    toks = []
    for b in sorted(census):
        if lo <= b <= hi:
            w = census[b]
            toks.append("b%d:%s" % (b, "unreadable" if w is None else "case%d/bit63=%d/clock%d" % (w["case"], w["bit63"], w["clock"])))
    return ";".join(toks) or "-"


# ---------------------------------------------------------------- the record

def pair_map_text(spec, offset):
    """The run's map: the edge first, then every probe at B + offset (the frozen map's shape)."""
    e_old, _ = spec["pair_edge"]
    toks = ["old_b%d=new_b%d" % (e_old, e_old + offset)]
    toks += ["old_b%d=new_b%d" % (b, b + offset) for b, _ in spec["probes"]]
    return ",".join(toks)


def make(spec, new_edge, status, method, census_frames, edge_words, edge2=None):
    """The pairing dict for a run. new_edge None = EDGE-NOT-FOUND (offset/deviation `-`, no map)."""
    e_old, e_ref = spec["pair_edge"]
    ref_offset = e_ref - e_old
    p = {"old_edge": e_old, "ref_old": e_old, "ref_new": e_ref, "ref_offset": ref_offset, "status": status, "method": method,
         "census_frames": census_frames, "edge_words": edge_words or "-", "edge2_old": None, "edge2_new": None, "edge2_status": "n/a",
         "new_edge": None, "offset": None, "deviation": None, "pair_map": "-"}
    if new_edge is not None:
        p.update({"new_edge": new_edge, "offset": new_edge - e_old, "deviation": (new_edge - e_old) - ref_offset,
                  "pair_map": pair_map_text(spec, new_edge - e_old)})
    if edge2:
        p.update({"edge2_old": edge2[0], "edge2_new": edge2[1], "edge2_status": edge2[2]})
    return p


def format_pairing(p):
    vals = []
    for c in COLUMNS:
        v = p.get(c)
        vals.append("-" if v is None else str(v))
    return ("# <probe dir>/<leg>/PAIRING.tsv -- the run's scene-edge pairing (verify/scene_pairing.py; DT-6 per run)\n"
            "%s\n%s\n" % ("\t".join(COLUMNS), "\t".join(vals)))


def parse_pairing(text):
    rows = [l.rstrip("\n") for l in text.splitlines() if l.strip() and not l.lstrip().startswith("#")]
    if len(rows) != 2:
        raise ValueError("%s needs one header row and one value row, got %d" % (FILE, len(rows)))
    header = [c.strip() for c in rows[0].split("\t")]
    if header != list(COLUMNS):
        raise ValueError("%s header must be %s" % (FILE, "\t".join(COLUMNS)))
    vals = rows[1].split("\t")
    if len(vals) != len(COLUMNS):
        raise ValueError("%s value row has %d fields, needs %d" % (FILE, len(vals), len(COLUMNS)))
    p = {}
    for c, v in zip(COLUMNS, vals):
        v = v.strip()
        if c in ("old_edge", "new_edge", "offset", "ref_old", "ref_new", "ref_offset", "deviation", "census_frames", "edge2_old", "edge2_new"):
            p[c] = None if v == "-" else int(v)
        else:
            p[c] = v
    if p["status"] not in ("EDGE-FOUND", "EDGE-NOT-FOUND"):
        raise ValueError("%s status %r" % (FILE, p["status"]))
    if p["status"] == "EDGE-FOUND" and (p["new_edge"] is None or p["offset"] != p["new_edge"] - p["old_edge"]
                                        or p["deviation"] != p["offset"] - p["ref_offset"]):
        raise ValueError("%s: offset/deviation do not follow from the edges" % FILE)
    return p


def write(leg_out_dir, p):
    os.makedirs(leg_out_dir, exist_ok=True)
    path = os.path.join(leg_out_dir, FILE)
    with open(path, "w") as f:
        f.write(format_pairing(p))
    return path


def read(leg_out_dir):
    """The run's pairing record under a probe leg dir, or None when the run carries none (frozen map applies).
    A malformed file is a ValueError (never a silent default)."""
    if not leg_out_dir:
        return None
    path = os.path.join(leg_out_dir, FILE)
    if not os.path.isfile(path):
        return None
    with open(path) as f:
        return parse_pairing(f.read())


def mapped_boundary(spec, pairing, b):
    """The NEW boundary paired with OLD probe boundary b: by the RUN's offset when a pairing record with an edge
    exists (every mapped probe moves with it), else the frozen map's (leg_spec.mapped_boundary)."""
    if spec is None or b not in spec.get("pair_map", {}):
        return leg_spec.mapped_boundary(spec, b)
    if pairing and pairing["status"] == "EDGE-FOUND":
        return b + pairing["offset"]
    return leg_spec.mapped_boundary(spec, b)


def note(spec, pairing, b):
    """The pair's note text (None when the probe is not scene-paired)."""
    if spec is None or b not in spec.get("pair_map", {}):
        return None
    e_old, e_ref = spec["pair_edge"]
    if pairing is None:
        return "paired by scene edge old_b%d=new_b%d (offset %+d) [frozen map; no per-run record]" % (e_old, e_ref, e_ref - e_old)
    if pairing["status"] != "EDGE-FOUND":
        return ("paired by scene edge: EDGE-NOT-FOUND in the run's census (%s frames; frozen reference old_b%d=new_b%d %+d): "
                "no mapped image" % (pairing["census_frames"], e_old, e_ref, e_ref - e_old))
    txt = "paired by scene edge (run: old_b%d=new_b%d, offset %+d; frozen reference %+d, deviation %+d)" % (
        e_old, pairing["new_edge"], pairing["offset"], pairing["ref_offset"], pairing["deviation"])
    if pairing.get("edge2_status", "n/a") != "n/a" and pairing["edge2_old"] is not None and b >= pairing["edge2_old"]:
        txt += "; pass-2 edge old_b%d=new_b%d %s" % (pairing["edge2_old"], pairing["edge2_new"], pairing["edge2_status"])
    return txt


def verdict_text(pairing, spec):
    """The lane verdict's scene-pair text for diff_legs (the run's edge when present)."""
    e_old, e_ref = spec["pair_edge"]
    if pairing is None or pairing["status"] != "EDGE-FOUND":
        return None
    return "scene-paired: offset %+d from old_b%d=new_b%d (run; frozen reference %+d, deviation %+d)" % (
        pairing["offset"], e_old, pairing["new_edge"], pairing["ref_offset"], pairing["deviation"])


# ---------------------------------------------------------------- the derivation (probe_images.py calls this)

def derive(spec, keys, run_leg_argv, work_root, margin=DEFAULT_MARGIN, log=print, image_case=None, verify_edge2=False):
    """Find the run's edge: the census leg `--frames ref_new + margin + 1 --state-every 1` (one retry with 4x the
    margin when no edge lands inside), then the pass-2 edge predicted (and checked when verify_edge2: image_case(N)
    -> the SM case of a one-image leg at N). run_leg_argv(frames, state_every, work) -> the run_leg.py command.
    Returns the pairing dict (status EDGE-FOUND / EDGE-NOT-FOUND)."""
    e_old, e_ref = spec["pair_edge"]
    attempts = [e_ref + margin, e_ref + 4 * margin]
    census, frames_run = {}, 0
    for frames in attempts:
        work = os.path.join(work_root, "edge_census_f%d" % frames)
        shutil.rmtree(work, ignore_errors=True)
        t0 = time.time()
        census, rc, tail = census_leg(run_leg_argv(frames, 1, work), work, keys, log=log)
        shutil.rmtree(work, ignore_errors=True)
        try:
            os.remove(work + ".log")
        except OSError:
            pass
        frames_run = frames
        edge = find_edge(census)
        gaps = census_gaps(census)
        log("scene_pairing: census leg --frames %d --state-every 1: %d images read, exit=%d, %.0f s; edge %s%s%s"
            % (frames, len(census), rc, time.time() - t0, "none" if edge is None else "new_b%d (case 6 at b%d)" % edge,
               "" if rc == 0 else "; tail: %s" % tail,
               "" if not gaps else "; MISSING boundaries %s (%d)" % (",".join("b%d" % g for g in gaps[:8]) + ("…" if len(gaps) > 8 else ""), len(gaps))))
        if edge is not None:
            break
    if edge is None:
        return make(spec, None, "EDGE-NOT-FOUND", "image-census", frames_run, census_text(census, max(census) - 3, max(census)) if census else "-")
    e_new, e_before = edge
    words = census_text(census, e_before - 1, e_new + 2)
    p = make(spec, e_new, "EDGE-FOUND", "image-census", frames_run, words)
    # the pass-2 edge: predicted at the same offset; verified by two one-image legs when asked
    edge2_old = _second_edge_old(spec)
    if edge2_old is not None:
        e2_new = edge2_old + p["offset"]
        status = "PREDICTED"
        if verify_edge2 and image_case is not None:
            c_before, c_at = image_case(e2_new - 1), image_case(e2_new)
            status = "VERIFIED" if (c_before, c_at) == (CASE_LOAD, CASE_TITLE) else "EDGE2-MISMATCH(case%s@b%d,case%s@b%d)" % (c_before, e2_new - 1, c_at, e2_new)
            log("scene_pairing: pass-2 edge check at new_b%d-1/new_b%d: case %s / %s -> %s" % (e2_new, e2_new, c_before, c_at, status))
        p.update({"edge2_old": edge2_old, "edge2_new": e2_new, "edge2_status": status})
    return p


def _second_edge_old(spec):
    """OLD's pass-2 edge boundary from the leg's notes (`case 6->7 at vf N = bM = cutscene pass 2`), or None."""
    m = re.search(r"case 6->7 at vf \d+ = b(\d+) = cutscene pass 2", spec.get("notes", ""))
    return int(m.group(1)) if m else None


# ---------------------------------------------------------------- selftest

def _writer_main(argv):
    """`--selftest-writer <dir> <n> <edge>`: writes synthetic images state_b1..n one by one (case 6 below edge, 7 from it)."""
    d, n, edge = argv[0], int(argv[1]), int(argv[2])
    os.makedirs(d, exist_ok=True)
    for b in range(1, n + 1):
        sm = clock_pair.CASE6 if b < edge else clock_pair.CASE7_SET
        tmp = os.path.join(d, "state_b%d.ps2st.tmp" % b)
        clock_pair._image(tmp, 1000 + 149 * b, sm, b)
        os.rename(tmp, os.path.join(d, "state_b%d.ps2st" % b))
        time.sleep(0.02)
    print("writer done")
    return 0


def selftest():
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)

    tmp = tempfile.mkdtemp(prefix="scene_pairing_")
    spec = leg_spec.parse("\t".join(leg_spec.COLUMNS + leg_spec.OPTIONAL_COLUMNS)
                          + "\n6000\t60\t3000\t600\tcoldboot\t1500@2100,3780@4400,4200@4800\tcase 6->7 at vf 4363 = b3833 = cutscene pass 2"
                          "\told_b850=new_b1133,old_b1500=new_b1783,old_b3780=new_b4063,old_b4200=new_b4483\t530\n")
    # 1. find_edge over a synthetic census: first case-7 after a case-6 image; unreadable images break adjacency
    c = {b: clock_pair.decode(b, 149 * b, clock_pair.CASE6 if b < 1049 else clock_pair.CASE7_SET) for b in range(1040, 1060)}
    check(find_edge(c) == (1049, 1048), "find_edge: the first case-7 boundary after a case-6 image -> (1049, 1048)")
    c2 = dict(c)
    c2[1048] = None
    check(find_edge(c2) is None, "an unreadable image at the edge's predecessor -> no edge (never a guess)")
    c3 = {b: c[b] for b in c if b != 1048}   # a harness-refused image AT the edge: the census skips b1048
    check(find_edge(c3) is None and census_gaps(c3) == [1048],
          "a MISSING boundary at the edge (dense adjacency b == prev + 1 required) -> no edge; census_gaps names b1048")
    c4 = {b: c[b] for b in c if b != 1043}   # a gap away from the edge does not move it
    check(find_edge(c4) == (1049, 1048) and census_gaps(c4) == [1043] and census_gaps(c) == [],
          "a gap away from the edge leaves the edge exact; a dense census has no gaps")
    check(find_edge({b: c[b] for b in c if b >= 1049}) is None, "a census that starts at case 7 has no edge")
    # 2. the record: make / format / parse round trip; the note and the mapping follow the run's offset
    p = make(spec, 1049, "EDGE-FOUND", "image-census", 1283, census_text(c, 1047, 1051))
    check(p["offset"] == 199 and p["deviation"] == -84 and p["ref_offset"] == 283
          and p["pair_map"] == "old_b850=new_b1049,old_b1500=new_b1699,old_b3780=new_b3979,old_b4200=new_b4399",
          "make: offset +199, deviation -84 vs the frozen +283, the run's map -> %r" % p["pair_map"])
    leg_out = os.path.join(tmp, "coldboot_attract")
    write(leg_out, p)
    q = read(leg_out)
    check(q == p, "PAIRING.tsv round-trips -> %r" % (q == p))
    check(read(os.path.join(tmp, "none")) is None and read(None) is None, "no record -> None (the frozen map applies)")
    check(mapped_boundary(spec, q, 1500) == 1699 and mapped_boundary(spec, q, 3780) == 3979 and mapped_boundary(spec, None, 1500) == 1783
          and mapped_boundary(spec, q, 60) == 60 and mapped_boundary(None, q, 60) == 60,
          "mapped_boundary: the run's offset on every mapped probe; the frozen map without a record; non-probes unchanged")
    n = note(spec, q, 1500)
    check(n == "paired by scene edge (run: old_b850=new_b1049, offset +199; frozen reference +283, deviation -84)", "the note names run edge, offset, reference, deviation -> %r" % n)
    check(note(spec, None, 1500) == "paired by scene edge old_b850=new_b1133 (offset +283) [frozen map; no per-run record]" and note(spec, q, 60) is None,
          "without a record the note says so; a non-probe has none")
    check(verdict_text(q, spec) == "scene-paired: offset +199 from old_b850=new_b1049 (run; frozen reference +283, deviation -84)", "the lane verdict text")
    # a pass-2 edge recorded: pass-2 probes carry it
    p2 = make(spec, 1049, "EDGE-FOUND", "image-census", 1283, "-", edge2=(3833, 4032, "PREDICTED"))
    check(note(spec, p2, 4200).endswith("; pass-2 edge old_b3833=new_b4032 PREDICTED") and "pass-2" not in note(spec, p2, 1500),
          "the pass-2 edge status is on the pass-2 pairs' notes only")
    # 3. EDGE-NOT-FOUND: no map, the note says so, mapped_boundary falls back to the frozen map (the image is not produced anyway)
    pn = make(spec, None, "EDGE-NOT-FOUND", "image-census", 1733, "b1731:case6/bit63=0/clock1")
    write(leg_out, pn)
    qn = read(leg_out)
    check(qn["status"] == "EDGE-NOT-FOUND" and qn["offset"] is None and qn["pair_map"] == "-" and "EDGE-NOT-FOUND" in note(spec, qn, 1500)
          and mapped_boundary(spec, qn, 1500) == 1783, "EDGE-NOT-FOUND round-trips; the note names it; no run offset")
    # malformed records are errors
    for bad in ("old_edge\tx\n1\t2\n", format_pairing(p).replace("\t199\t", "\t200\t"), format_pairing(p).replace("EDGE-FOUND", "MAYBE")):
        try:
            parse_pairing(bad)
            check(False, "a malformed PAIRING.tsv is rejected")
        except ValueError:
            check(True, "a malformed PAIRING.tsv is rejected")
    # 4. the census leg end to end with a synthetic writer process: every image read and deleted, the edge found
    keys = dict(clock_pair.TEST_KEYS)
    work = os.path.join(tmp, "census")
    cmd = [sys.executable, "-u", os.path.abspath(__file__), "--selftest-writer", work, "40", "27"]
    census, rc, tail = census_leg(cmd, work, keys, poll_s=0.05)
    check(rc == 0 and sorted(census) == list(range(1, 41)) and find_edge(census) == (27, 26) and not os.listdir(work),
          "census_leg: 40 images read as they land, edge (27, 26), every image deleted -> %d read, %d left" % (len(census), len(os.listdir(work))))
    # A runtime save may be refused while memory still proves the exact edge.
    # Coexisting older snapshots must not override that memory or leak on disk.
    memory_work = os.path.join(tmp, "memory_census")
    os.makedirs(memory_work)
    for boundary, case in ((26, clock_pair.CASE6), (27, clock_pair.CASE7_SET)):
        image = bytearray(32 * 1024 * 1024)
        struct.pack_into("<I", image, keys["clock_word"], 149 * boundary)
        struct.pack_into("<Q", image, keys["boot_sm_word"], case)
        with open(os.path.join(memory_work, "memory_b%d.bin" % boundary), "wb") as output:
            output.write(image)
        with open(os.path.join(memory_work, "state_b%d.refused" % boundary), "w") as output:
            output.write("Graphics: unpublished draws\n")
        clock_pair._image(os.path.join(memory_work, "state_b%d.ps2st" % boundary),
                          0, clock_pair.CASE6, boundary)
    del image
    memory_census, rc, _ = census_leg([sys.executable, "-c", "pass"], memory_work, keys, poll_s=0.05)
    check(rc == 0 and find_edge(memory_census) == (27, 26) and not os.listdir(memory_work),
          "memory capture proves edge despite refused save; all matching artifacts removed")
    broken = os.path.join(memory_work, "memory_b28.bin")
    with open(broken, "wb") as output:
        output.write(b"truncated")
    try:
        clock_pair.words(broken, keys)
        check(False, "truncated memory capture rejected")
    except ValueError:
        check(True, "truncated memory capture rejected")
    # 5. derive() through the writer: the edge, the deviation, the pass-2 prediction and its check
    spec_t = leg_spec.parse("\t".join(leg_spec.COLUMNS + leg_spec.OPTIONAL_COLUMNS)
                            + "\n200\t10\t30\t6\tcoldboot\t60@100,120@150\tcase 6->7 at vf 700 = b110 = cutscene pass 2"
                            "\told_b20=new_b30,old_b60=new_b70,old_b120=new_b130\t5\n")
    calls = []

    def argv(frames, every, w):
        calls.append((frames, every))
        return [sys.executable, "-u", os.path.abspath(__file__), "--selftest-writer", w, str(frames), "27"]
    pd = derive(spec_t, keys, argv, tmp, margin=5, log=lambda s: None, image_case=lambda n: CASE_LOAD if n < 117 else CASE_TITLE, verify_edge2=True)
    check(pd["status"] == "EDGE-FOUND" and pd["new_edge"] == 27 and pd["offset"] == 7 and pd["deviation"] == -3 and calls == [(35, 1)]
          and pd["edge2_old"] == 110 and pd["edge2_new"] == 117 and pd["edge2_status"] == "VERIFIED",
          "derive: one census leg (ref 30 + margin 5), edge new_b27 offset +7 deviation -3; pass-2 edge 117 VERIFIED -> %r" % {k: pd[k] for k in ("new_edge", "offset", "deviation", "edge2_new", "edge2_status")})
    pd2 = derive(spec_t, keys, argv, tmp, margin=5, log=lambda s: None, image_case=lambda n: CASE_LOAD, verify_edge2=True)
    check(pd2["edge2_status"].startswith("EDGE2-MISMATCH(case6@b116,case6@b117)"), "a failed pass-2 check is EDGE2-MISMATCH with both cases -> %r" % pd2["edge2_status"])
    calls.clear()

    def argv_no_edge(frames, every, w):
        calls.append((frames, every))
        return [sys.executable, "-u", os.path.abspath(__file__), "--selftest-writer", w, str(frames), "999"]
    pd3 = derive(spec_t, keys, argv_no_edge, tmp, margin=5, log=lambda s: None)
    check(pd3["status"] == "EDGE-NOT-FOUND" and calls == [(35, 1), (50, 1)] and pd3["census_frames"] == 50,
          "no edge inside the census: one retry with 4x the margin, then EDGE-NOT-FOUND -> %r %r" % (pd3["status"], calls))
    shutil.rmtree(tmp, ignore_errors=True)
    print("scene_pairing selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--selftest-writer":
        sys.exit(_writer_main(sys.argv[2:]))
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    print(__doc__)
    sys.exit(2)
