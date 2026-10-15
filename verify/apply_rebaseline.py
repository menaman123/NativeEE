#!/usr/bin/env python3
"""apply_rebaseline.py -- re-baseline ONE window of ONE lane of a frozen leg from NEW, LEDGER-GATED
(DECISIONS_LOG DT-4, proposed; the policy's re-baseline rule: "a window is re-baselined from NEW only with a
row naming the OLD defect and only after every residual word in the window is attributed").

  apply_rebaseline.py --leg L --lane mem|audio|frames --window b120|W3|k69 --rows R1,R2
                      --ledger <ledger.json> --source <NEW leg dir> [--image <new_b<N>.ps2st>]
                      [--baseline games/<title>/verify/baseline] [--commit <sha>] [--note TEXT] [--dry-run]
  apply_rebaseline.py --selftest

The gate (every condition must hold, else the tool REFUSES with exit 3 and writes nothing):
  1. the ledger names the leg and the window: attribute_probe.py's JSON (`leg`, the NEW image `new_b<N>`) for the
     mem lane; attribute_audio.py's JSON (`leg`, `first_divergent_window`, `rebaseline_gate`) for the audio lane;
  2. the ledger's kinds over the window read 0 OPEN / 0 MIXED / 0 UNATTRIBUTED and at least one ROW/DP word
     (`summary.by_kind` / `rebaseline_gate.by_kind`): every residual word is ROW or DP (+ NONSTATE, the DT-3 column,
     which never blocks on its own but never carries a window alone either, and is listed on the manifest row);
  3. every row id the ledger books (`summary.by_class[*].row`, the ROW kind; DP entries name their DP) is in
     --rows, every --rows id heads a §7.1 row of the docs (diff_legs.known_row_ids) -- an unfiled row refuses;
  4. the NEW source carries the window: the lane file (`<lane>.<ext>` of a run_leg.py dir, or the probe set's
     `f<N>_<lane>.<ext>` written by probe_images.py) with the window's key -- or, for a mem window whose boundary
     has NO lane line on either side (the anchors' b2: the lane writes every 60 boundaries), the image alone (an
     IMAGE-ONLY window, manifest lines `-`);
  5. the PROOF IS BOUND TO THE ARTEFACTS (mem): the ledger names the digests of the two images it attributed
     (`old_md5` / `new_md5`, `old_vsync_frame` / `new_vsync_frame`, attribute_probe.py writes them) and the tool refuses
     unless the frozen `probe/old_b<N>.ps2st` has the ledger's `old_md5` + vsync frame, the `--image` has the ledger's
     `new_md5` + vsync frame, and (when the source has a lane line) the image's chained state hash IS the NEW line's
     hash -- so the image installed as the effective reference is the image the ledger attributed, from the leg whose
     line is recorded. Audio: the frozen lane line's hash must be the ledger's `old_window_hash` and the source line's
     the ledger's `new_window_hash` (attribute_audio.py writes them);
  6. no manifest row exists yet for (lane, window).
What it writes (and nothing else): `<leg>/REBASELINE.tsv` (+ one row), `<leg>/rebased/<lane>_<window>.ledger.json`
(the proof copy), `<leg>/rebased/new_b<N>.ps2st` (mem). The frozen files are hashed before and after and the tool
FAILS (exit 4) if any changed -- it never edits `frames.fhash` / `audio.ahash` / `mem.mhash` / `run.meta` / `probe/*`.
The row's `commit` is `git describe --always --dirty` (a `-dirty` suffix says the producing tree was uncommitted) and
the note carries the producing binary's build id from the source's `run.meta` when present.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rebaseline as rb  # noqa: E402
import leg_spec  # noqa: E402
import state_hash  # noqa: E402

FROZEN = ("frames.fhash", "audio.ahash", "mem.mhash", "run.meta")
EXIT_REFUSED = 3
EXIT_FROZEN_TOUCHED = 4


class Refusal(Exception):
    pass


def _md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def frozen_digests(leg_dir):
    out = {}
    for n in FROZEN:
        p = os.path.join(leg_dir, n)
        if os.path.isfile(p):
            out[n] = _md5(p)
    pdir = os.path.join(leg_dir, "probe")
    if os.path.isdir(pdir):
        for n in sorted(os.listdir(pdir)):
            out["probe/" + n] = _md5(os.path.join(pdir, n))
    return out


def lane_line(lane_path, key):
    if not os.path.isfile(lane_path):
        return None
    with open(lane_path) as f:
        for l in f:
            l = l.strip()
            if l and l.split(",", 1)[0].strip() == str(key):
                return l
    return None


def source_lane_files(source, lane):
    """The lane files a NEW source dir may carry: run_leg.py's `<lane file>` and probe_images.py's `f<N>_<lane file>`."""
    name = rb.LANE_FILE[lane]
    out = [os.path.join(source, name)] if os.path.isfile(os.path.join(source, name)) else []
    if os.path.isdir(source):
        out += sorted(os.path.join(source, n) for n in os.listdir(source) if re.match(r"^f\d+_%s$" % re.escape(name), n))
    return out


def source_lane_line(source, lane, key):
    """(line, file) of the window's key in the source's lane files; (None, None) when no file carries it."""
    for path in source_lane_files(source, lane):
        l = lane_line(path, key)
        if l is not None:
            return l, path
    return None, None


def source_build_id(source):
    for name in sorted(os.listdir(source)) if os.path.isdir(source) else []:
        if name == "run.meta" or re.match(r"^f\d+_run\.meta$", name):
            for line in open(os.path.join(source, name)):
                k, _, v = line.rstrip("\n").partition("=")
                if k == "build_id" and v:
                    return v
    return None


def image_identity(path):
    """(md5, vsync_frame, chained state hash `0x...`) of a .ps2st image; the parse raises on a non-image."""
    _, vf, secs = state_hash.parse(path)
    h = state_hash.FNV_OFFSET
    for t in (state_hash.TAG_RAM, state_hash.TAG_SPAD, state_hash.TAG_META):
        h = state_hash.fnv1a64(secs[t][2], h)
    return _md5(path), vf, "0x%016x" % h


def line_hash(line):
    return line.split(",", 1)[1].strip() if line and "," in line else None


def bind_mem(ledger, frozen_probe, image, new_line):
    """Condition 5 for the mem lane: the ledger's digests/vsync frames == the frozen probe's and the image's; the image's
    state hash == the NEW line's hash when a line exists. Raises Refusal naming the first mismatch."""
    for k in ("old_md5", "new_md5", "old_vsync_frame", "new_vsync_frame"):
        if ledger.get(k) in (None, ""):
            raise Refusal("the ledger carries no %s: the proof is not bound to its images (re-run attribute_probe.py)" % k)
    if not os.path.isfile(frozen_probe):
        raise Refusal("the frozen probe image is missing: %s" % frozen_probe)
    try:
        o_md5, o_vf, _ = image_identity(frozen_probe)
        n_md5, n_vf, n_hash = image_identity(image)
    except Exception as ex:   # a non-image given as --image
        raise Refusal("not a .ps2st image: %s" % ex)
    if o_md5 != ledger["old_md5"]:
        raise Refusal("the frozen probe %s (md5 %s) is not the image the ledger attributed (old_md5 %s)" % (os.path.basename(frozen_probe), o_md5, ledger["old_md5"]))
    if o_vf != ledger["old_vsync_frame"]:
        raise Refusal("the frozen probe's vsync_frame %d != the ledger's old_vsync_frame %s" % (o_vf, ledger["old_vsync_frame"]))
    if n_md5 != ledger["new_md5"]:
        raise Refusal("the image %s (md5 %s) is not the image the ledger attributed (new_md5 %s)" % (image, n_md5, ledger["new_md5"]))
    if n_vf != ledger["new_vsync_frame"]:
        raise Refusal("the image's vsync_frame %d != the ledger's new_vsync_frame %s" % (n_vf, ledger["new_vsync_frame"]))
    if new_line is not None and line_hash(new_line) != n_hash:
        raise Refusal("the image's state hash %s is not the NEW source line's %r: the image is not from the recorded leg" % (n_hash, new_line))
    return {"old_md5": o_md5, "new_md5": n_md5, "new_state_hash": n_hash}


def bind_audio(ledger, old_line, new_line):
    """Condition 5 for the audio lane: the frozen line's hash == the ledger's old_window_hash, the source line's == new_window_hash."""
    for k in ("old_window_hash", "new_window_hash"):
        if ledger.get(k) in (None, ""):
            raise Refusal("the ledger carries no %s: the proof is not bound to the lane values (re-run attribute_audio.py)" % k)
    if line_hash(old_line) != ledger["old_window_hash"]:
        raise Refusal("the frozen audio line %r is not the window the ledger attributed (old_window_hash %s)" % (old_line, ledger["old_window_hash"]))
    if line_hash(new_line) != ledger["new_window_hash"]:
        raise Refusal("the NEW source line %r is not the value the ledger attributed (new_window_hash %s)" % (new_line, ledger["new_window_hash"]))


def ledger_gate(ledger, lane, leg, key):
    """-> (by_kind, rows_booked, dps) after the leg/window checks; raises Refusal."""
    if ledger.get("leg") != leg:
        raise Refusal("the ledger names leg %r, not %r (a ledger without a leg is refused)" % (ledger.get("leg"), leg))
    if lane == "mem":
        s = ledger.get("summary")
        if not s or "by_kind" not in s:
            raise Refusal("the ledger carries no summary.by_kind (not an attribute_probe.py ledger)")
        new_img = os.path.basename(str(ledger.get("new", "")))
        m = re.match(r"^new_b(\d+)\.ps2st$", new_img)
        if not m or int(m.group(1)) != key:
            raise Refusal("the ledger's NEW image %r is not the window's new_b%d.ps2st" % (new_img, key))
        by_kind = s["by_kind"]
        rows = sorted({e["row"] for e in s["by_class"] if e["kind"] == "ROW"})
        dps = sorted({e["row"] for e in s["by_class"] if e["kind"] == "DP"})
    else:
        g = ledger.get("rebaseline_gate")
        if not g or "by_kind" not in g:
            raise Refusal("the ledger carries no rebaseline_gate (not an attribute_audio.py ledger with the audibility gate)")
        if ledger.get("first_divergent_window") != key or g.get("window") != key:
            raise Refusal("the ledger's window is W%s (gate W%s), not W%d" % (ledger.get("first_divergent_window"), g.get("window"), key))
        by_kind = g["by_kind"]
        rows = sorted(g.get("rows", []))
        dps = sorted(g.get("dps", []))
    total = sum(by_kind.get(k, 0) for k in ("ROW", "DP", "OPEN", "ORDER", "MIXED", "NONSTATE", "UNATTRIBUTED"))
    blocking = {k: by_kind.get(k, 0) for k in ("OPEN", "ORDER", "MIXED", "UNATTRIBUTED") if by_kind.get(k, 0)}
    if blocking:
        raise Refusal("the window is not 100 %% attributed: %s (ROW %d, DP %d, NONSTATE %d)"
                      % (", ".join("%s %d" % kv for kv in sorted(blocking.items())), by_kind.get("ROW", 0), by_kind.get("DP", 0), by_kind.get("NONSTATE", 0)))
    if total == 0:
        raise Refusal("the ledger has no differing word in the window (IDENTICAL: nothing to re-baseline)")
    if by_kind.get("ROW", 0) + by_kind.get("DP", 0) == 0:
        raise Refusal("the window has no ROW/DP word (NONSTATE %d only): nothing attributes its difference to an OLD defect" % by_kind.get("NONSTATE", 0))
    return by_kind, rows, dps


def apply(leg, lane, window, rows, ledger_path, source, image=None, baseline=None, commit=None, note="", dry_run=False, known_ids=None):
    """-> the manifest entry written (or that would be written with dry_run). Raises Refusal (gate) or
    RuntimeError (a frozen file changed)."""
    baseline = baseline or leg_spec.BASELINE_DIR
    leg_dir = os.path.join(baseline, leg)
    if not os.path.isdir(leg_dir) or not os.path.isfile(os.path.join(leg_dir, rb.LANE_FILE[lane])):
        raise Refusal("no frozen leg %s with %s under %s" % (leg, rb.LANE_FILE[lane], baseline))
    key = rb.parse_window(lane, window)
    window = rb.window_text(lane, key)
    rows = [r for r in rows if r]
    if not rows or not all(rb.ROW_ID_RE.match(r) for r in rows):
        raise Refusal("--rows must be a comma list of row ids, got %r" % rows)
    if known_ids is None:
        from diff_legs import known_row_ids
        known_ids = known_row_ids()
    unknown = [r for r in rows if r not in known_ids]
    if unknown:
        raise Refusal("row id(s) not filed in the docs (EQUIVALENCE_PLAN §7.1 / PHASE*_PLAN / PHASE*_IMPL): %s" % ", ".join(unknown))
    with open(ledger_path) as f:
        ledger = json.load(f)
    by_kind, booked, dps = ledger_gate(ledger, lane, leg, key)
    missing = [r for r in booked if r not in rows]
    if missing:
        raise Refusal("the ledger books rows not in --rows: %s" % ", ".join(missing))
    before = frozen_digests(leg_dir)
    entries = rb.read_manifest(leg_dir)
    if key in rb.by_lane(entries, lane):
        raise Refusal("a manifest row for %s %s exists already (line %d)" % (lane, window, rb.by_lane(entries, lane)[key]["line"]))
    old_line = lane_line(os.path.join(leg_dir, rb.LANE_FILE[lane]), key)
    new_line, new_file = source_lane_line(source, lane, key)
    img_rel = "-"
    image_only = False
    if lane == "mem":
        frozen_probe = os.path.join(leg_dir, "probe", "old_b%d.ps2st" % key)
        if old_line is None and new_line is None:
            # the probe pair's boundary has no lane line on either side: an image-only window
            if not os.path.isfile(frozen_probe):
                raise Refusal("boundary %d has neither a frozen %s line nor a frozen probe image: not a window of this leg" % (key, rb.LANE_FILE[lane]))
            image_only = True
        elif new_line is None:
            raise Refusal("the NEW source %s has no %s line for key %d (files looked at: %s)"
                          % (source, rb.LANE_FILE[lane], key, ", ".join(os.path.basename(x) for x in source_lane_files(source, lane)) or "none"))
        image = image or os.path.join(source, "new_b%d.ps2st" % key)
        if not os.path.isfile(image):
            raise Refusal("the NEW probe image for boundary %d is missing: %s" % (key, image))
        bind_mem(ledger, frozen_probe, image, new_line)
        img_rel = "%s/new_b%d.ps2st" % (rb.REBASED_DIR, key)
    else:
        if new_line is None:
            raise Refusal("the NEW source %s has no %s line for key %d" % (source, rb.LANE_FILE[lane], key))
        if old_line is None:
            raise Refusal("the frozen %s has no line for key %d" % (rb.LANE_FILE[lane], key))
        if lane == "audio":
            bind_audio(ledger, old_line, new_line)
    if commit is None:
        try:
            commit = subprocess.run(["git", "describe", "--always", "--dirty"], cwd=os.path.dirname(HERE), stdout=subprocess.PIPE,
                                    stderr=subprocess.DEVNULL, text=True).stdout.strip() or "unknown"
        except OSError:
            commit = "unknown"
    proof_rel = "%s/%s_%s.ledger.json" % (rb.REBASED_DIR, lane, window)
    kinds_note = "kinds ROW %d DP %d NONSTATE %d" % (by_kind.get("ROW", 0), by_kind.get("DP", 0), by_kind.get("NONSTATE", 0))
    if dps:
        kinds_note += "; DP " + ",".join(dps)
    if image_only:
        kinds_note += "; image-only window (no %s line at boundary %d on either side; the image is the reference)" % (rb.LANE_FILE[lane], key)
    if lane == "mem":
        kinds_note += "; bound: old_md5 %s new_md5 %s vf %s/%s" % (ledger["old_md5"][:8], ledger["new_md5"][:8], ledger["old_vsync_frame"], ledger["new_vsync_frame"])
    bid = source_build_id(source)
    if bid:
        kinds_note += "; build_id %s" % bid
    old_line = old_line or "-"
    new_line = new_line or "-"
    entry = {"lane": lane, "window": window, "rows": rows, "commit": commit, "proof": proof_rel, "old_line": old_line, "new_line": new_line,
             "image": img_rel, "note": (note + "; " if note else "") + kinds_note, "key": key}
    if dry_run:
        return entry
    rdir = os.path.join(leg_dir, rb.REBASED_DIR)
    os.makedirs(rdir, exist_ok=True)
    shutil.copyfile(ledger_path, os.path.join(leg_dir, proof_rel))
    if lane == "mem":
        shutil.copyfile(image, os.path.join(leg_dir, img_rel))
    mpath = os.path.join(leg_dir, rb.MANIFEST)
    fresh = not os.path.isfile(mpath)
    with open(mpath, "a") as f:
        if fresh:
            f.write(rb.header())
        f.write(rb.format_row(entry) + "\n")
    rb.read_manifest(leg_dir)   # the row written must parse
    after = frozen_digests(leg_dir)
    if before != after:
        changed = sorted(k for k in set(before) | set(after) if before.get(k) != after.get(k))
        raise RuntimeError("FROZEN FILE CHANGED: %s" % ", ".join(changed))
    return entry


def render(entry, leg):
    return ("apply_rebaseline: %s %s %s -> effective reference %r (frozen %r kept), rows %s, commit %s, proof %s, image %s; %s"
            % (leg, entry["lane"], entry["window"], entry["new_line"], entry["old_line"], ",".join(entry["rows"]), entry["commit"],
               entry["proof"], entry["image"], entry["note"]))


# ------------------------------------------------------------------ selftest

def _ledger_mem(path, leg, b, by_kind, rows=(), dps=(), bind=None, drop=()):
    by_class = [{"class": "C%d" % i, "row": r, "kind": "ROW", "words": 1} for i, r in enumerate(rows)]
    by_class += [{"class": "D%d" % i, "row": d, "kind": "DP", "words": 1} for i, d in enumerate(dps)]
    d = {"leg": leg, "old": "x/old_b%d.ps2st" % b, "new": "y/new_b%d.ps2st" % b,
         "summary": {"by_kind": by_kind, "by_class": by_class, "total": sum(by_kind.values())}}
    d.update(bind or {})
    for k in drop:
        d.pop(k, None)
    with open(path, "w") as f:
        json.dump(d, f)


def _image(path, words, vf):
    """A minimal parsable image (RAM0/SPAD/META) with the vsync frame given."""
    import struct
    import zlib
    ram = struct.pack("<%dI" % len(words), *words)
    spad, meta = bytes(16), bytes(16)
    body = b"PS2SAVE1" + struct.pack("<IIIIQ", 4, 3, vf, 0, 0)
    for tag, payload in ((state_hash.TAG_RAM, ram), (state_hash.TAG_SPAD, spad), (state_hash.TAG_META, meta)):
        body += struct.pack("<IIIIQ", tag, 1, 0, zlib.crc32(payload) & 0xFFFFFFFF, len(payload)) + payload
    body += struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(body)
    return image_identity(path)


def selftest():
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)
    tmp = tempfile.mkdtemp(prefix="apply_rebaseline_")
    base = os.path.join(tmp, "baseline")
    leg = "coldboot"
    ld = os.path.join(base, leg)
    os.makedirs(os.path.join(ld, "probe"))
    # the frozen OLD probe (vf 121) and NEW's image (vf 120) are REAL parsable images; the lane lines carry their state hashes
    o_md5, o_vf, o_hash = _image(os.path.join(ld, "probe", "old_b120.ps2st"), [1, 2, 3], 121)
    src = os.path.join(tmp, "newleg")
    os.makedirs(src)
    n_md5, n_vf, n_hash = _image(os.path.join(src, "new_b120.ps2st"), [1, 2, 4], 120)
    for n, body in (("mem.mhash", "0,0xa\n60,0xb\n120,%s\n" % o_hash), ("audio.ahash", "0,0x0\n1,0x1\n"), ("frames.fhash", "1,0x0\n"), ("run.meta", "tree=legacy\n")):
        open(os.path.join(ld, n), "w").write(body)
    open(os.path.join(src, "mem.mhash"), "w").write("0,0xa\n60,0xb\n120,%s\n" % n_hash)
    open(os.path.join(src, "run.meta"), "w").write("build_id=NATIVEEE-BUILD-ID test\n")
    BIND = {"old_md5": o_md5, "new_md5": n_md5, "old_vsync_frame": o_vf, "new_vsync_frame": n_vf}
    known = {"P9-TEST-ROW", "P9-TEST-ROW2"}
    before = frozen_digests(ld)
    full = {"ROW": 10, "DP": 2, "OPEN": 0, "MIXED": 0, "NONSTATE": 3, "UNATTRIBUTED": 0}

    def refused(by_kind, rows=("P9-TEST-ROW",), booked=("P9-TEST-ROW",), dps=("DP-99",), what="", bind=BIND, drop=(), **kw):
        lp = os.path.join(tmp, "l.json")
        _ledger_mem(lp, kw.pop("lleg", leg), kw.pop("lb", 120), by_kind, booked, dps, bind, drop)
        try:
            apply(leg, "mem", kw.pop("window", "b120"), list(rows), lp, kw.pop("source", src), baseline=base, commit="abc1234", known_ids=known, **kw)
            check(False, "must refuse: " + what)
            return None
        except Refusal as e:
            check(True, "REFUSED %s -> %s" % (what, str(e)[:100]))
            return str(e)
    refused(dict(full, OPEN=1), what="1 OPEN word")
    refused(dict(full, MIXED=5), what="5 MIXED words")
    refused(dict(full, UNATTRIBUTED=1), what="1 UNATTRIBUTED word")
    refused(dict(full, OPEN=2, MIXED=1, UNATTRIBUTED=4), what="OPEN+MIXED+UNATTRIBUTED")
    refused({"ROW": 0, "DP": 0, "OPEN": 0, "MIXED": 0, "NONSTATE": 0, "UNATTRIBUTED": 0}, what="an IDENTICAL ledger (nothing to re-baseline)")
    refused({"ROW": 0, "DP": 0, "OPEN": 0, "MIXED": 0, "NONSTATE": 9, "UNATTRIBUTED": 0}, rows=("P9-TEST-ROW",), booked=(), dps=(), what="NONSTATE-only words (nothing attributed to a row)")
    refused(full, rows=("P9-UNFILED",), booked=("P9-UNFILED",), what="a row id not filed in the docs")
    refused(full, rows=("P9-TEST-ROW",), booked=("P9-TEST-ROW", "P9-TEST-ROW2"), what="a booked row missing from --rows")
    refused(full, lb=60, what="a ledger of another boundary (new_b60 for window b120)")
    refused(full, lleg="artdet_f37152", what="a ledger of another leg")
    refused(full, drop=("leg",), what="a ledger WITHOUT a leg key (fail-closed)")
    refused(full, window="b60", lb=60, what="a window the NEW source has no line for (frozen b60 line exists, source has none)")
    refused(full, rows=(), booked=(), what="no rows")
    refused(dict(full, ORDER=2), what="2 ORDER voices (slot permutations are hash-relevant, never NONSTATE)")
    # the artefact binding (review MAJOR 2): an unrelated image, a wrong digest, a wrong vsync frame, a missing digest, a non-image
    other = os.path.join(tmp, "other_b120.ps2st")
    _image(other, [9, 9, 9], 1)
    refused(full, image=other, what="an UNRELATED image as --image (its md5 is not the ledger's new_md5)")
    refused(full, bind=dict(BIND, new_md5="0" * 32), what="a ledger whose new_md5 is not the image's")
    refused(full, bind=dict(BIND, old_md5="0" * 32), what="a ledger whose old_md5 is not the frozen probe's")
    refused(full, bind=dict(BIND, new_vsync_frame=1), what="a ledger whose new_vsync_frame is not the image's")
    refused(full, bind=dict(BIND, old_vsync_frame=1), what="a ledger whose old_vsync_frame is not the frozen probe's")
    refused(full, drop=("old_md5",), what="a ledger without digests (not bound)")
    src2 = os.path.join(tmp, "newleg2")
    os.makedirs(src2)
    open(os.path.join(src2, "mem.mhash"), "w").write("0,0xa\n60,0xb\n120,0xdeadbeef\n")
    shutil.copyfile(os.path.join(src, "new_b120.ps2st"), os.path.join(src2, "new_b120.ps2st"))
    refused(full, source=src2, what="an image whose state hash is not the source leg's line (not from the recorded leg)")
    notimg = os.path.join(tmp, "notimg.ps2st")
    open(notimg, "wb").write(b"NEW")
    refused(full, image=notimg, what="a file that is not a .ps2st image")
    check(frozen_digests(ld) == before and not os.path.isfile(os.path.join(ld, rb.MANIFEST)), "every refusal wrote NOTHING (frozen digests equal, no manifest)")
    # the probe-set layout (review MINOR 6): f<N>_mem.mhash + new_b<N>.ps2st, dry run
    src3 = os.path.join(tmp, "probeset")
    os.makedirs(src3)
    open(os.path.join(src3, "f121_mem.mhash"), "w").write("0,0xa\n60,0xb\n120,%s\n" % n_hash)
    open(os.path.join(src3, "f121_run.meta"), "w").write("build_id=NATIVEEE-BUILD-ID probeset\n")
    shutil.copyfile(os.path.join(src, "new_b120.ps2st"), os.path.join(src3, "new_b120.ps2st"))
    lp = os.path.join(tmp, "ok.json")
    _ledger_mem(lp, leg, 120, full, ("P9-TEST-ROW", "P9-TEST-ROW2"), ("DP-99",), BIND)
    e3 = apply(leg, "mem", "b120", ["P9-TEST-ROW", "P9-TEST-ROW2"], lp, src3, baseline=base, commit="abc1234", known_ids=known, dry_run=True)
    check(e3["new_line"] == "120," + n_hash and "build_id NATIVEEE-BUILD-ID probeset" in e3["note"] and "bound: old_md5" in e3["note"],
          "the probe-set layout (f121_mem.mhash) is accepted as --source; the note carries the build id and the binding -> %s" % e3["note"][-120:])
    check(not os.path.isfile(os.path.join(ld, rb.MANIFEST)), "the dry run wrote nothing")
    # the accepted case
    e = apply(leg, "mem", "b120", ["P9-TEST-ROW", "P9-TEST-ROW2"], lp, src, baseline=base, commit="abc1234", known_ids=known, note="test")
    check(e["new_line"] == "120," + n_hash and e["old_line"] == "120," + o_hash and e["image"] == "rebased/new_b120.ps2st" and "DP DP-99" in e["note"],
          "accepted: the entry carries NEW's line, the frozen line, the image and the DP -> %r" % e["note"])
    ents = rb.read_manifest(ld)
    check(len(ents) == 1 and ents[0]["rows"] == ["P9-TEST-ROW", "P9-TEST-ROW2"] and ents[0]["commit"] == "abc1234", "the manifest row parses back")
    check(_md5(os.path.join(ld, "rebased", "new_b120.ps2st")) == n_md5 and os.path.isfile(os.path.join(ld, "rebased", "mem_b120.ledger.json")),
          "rebased/ holds NEW's image and the proof copy")
    check(frozen_digests(ld) == before, "the frozen files are byte-identical after the apply (%d digests)" % len(before))
    check(open(os.path.join(ld, "mem.mhash")).read() == "0,0xa\n60,0xb\n120,%s\n" % o_hash, "the frozen mem.mhash still reads the OLD line")
    p, pe = rb.effective_probe(ld, 120)
    check(p.endswith("rebased/new_b120.ps2st") and pe["window"] == "b120", "the effective probe reference is the rebased image")
    lines, applied = rb.effective_lines(ld, "mem", open(os.path.join(ld, "mem.mhash")).read().split())
    check(lines[-1] == "120," + n_hash and rb.report_text(applied) == "rebased: mem b120 (P9-TEST-ROW, P9-TEST-ROW2)", "the effective mem lines + the report text")
    refused(full, booked=("P9-TEST-ROW",), what="a second row for the same window")
    # the IMAGE-ONLY window (review MAJOR 3): boundary 2 has no mem.mhash line on either side; the image is the reference
    o2_md5, o2_vf, _ = _image(os.path.join(ld, "probe", "old_b2.ps2st"), [5, 5, 5], 3)
    n2_md5, n2_vf, _ = _image(os.path.join(src, "new_b2.ps2st"), [5, 5, 6], 2)
    before2 = frozen_digests(ld)
    BIND2 = {"old_md5": o2_md5, "new_md5": n2_md5, "old_vsync_frame": o2_vf, "new_vsync_frame": n2_vf}
    lp2 = os.path.join(tmp, "ok2.json")
    _ledger_mem(lp2, leg, 2, full, ("P9-TEST-ROW",), ("DP-99",), BIND2)
    e2 = apply(leg, "mem", "b2", ["P9-TEST-ROW"], lp2, src, baseline=base, commit="abc1234", known_ids=known)
    check(e2["old_line"] == "-" and e2["new_line"] == "-" and e2["image"] == "rebased/new_b2.ps2st" and "image-only window" in e2["note"],
          "an image-only window (b2, no lane line on either side) is accepted with `-` lines and the image -> %s" % e2["note"][:90])
    ents2 = rb.read_manifest(ld)
    check(any(x["key"] == 2 and x["image_only"] for x in ents2), "the b2 row parses back as image-only")
    lines2, applied2 = rb.effective_lines(ld, "mem", open(os.path.join(ld, "mem.mhash")).read().split())
    check(len(lines2) == 3 and [x["key"] for x in applied2] == [120], "the image-only row leaves the mem lane lines untouched (b120's replaced only)")
    p2, pe2 = rb.effective_probe(ld, 2)
    check(p2.endswith("rebased/new_b2.ps2st") and pe2["image_only"] and frozen_digests(ld) == before2, "the b2 probe pair is served the rebased image; frozen files untouched")
    _ledger_mem(lp2, leg, 2, full, ("P9-TEST-ROW",), ("DP-99",), dict(BIND2, new_md5=n_md5))
    try:
        apply(leg, "mem", "b2", ["P9-TEST-ROW"], lp2, src, baseline=base, commit="abc1234", known_ids=known)
        check(False, "an image-only window with the wrong digest must refuse")
    except Refusal as ex:
        check("exists already" in str(ex) or "new_md5" in str(ex), "an image-only window is bound too -> %s" % str(ex)[:80])
    # the audio lane through attribute_audio's gate block
    la = os.path.join(tmp, "a.json")
    open(os.path.join(src, "audio.ahash"), "w").write("0,0x0\n1,0x9\n")
    json.dump({"leg": leg, "first_divergent_window": 1, "old_window_hash": "0x1", "new_window_hash": "0x8",
               "rebaseline_gate": {"window": 1, "by_kind": {"ROW": 3, "OPEN": 0, "MIXED": 0, "UNATTRIBUTED": 0}, "rows": ["P9-TEST-ROW"]}}, open(la, "w"))
    try:
        apply(leg, "audio", "W1", ["P9-TEST-ROW"], la, src, baseline=base, commit="abc1234", known_ids=known)
        check(False, "an audio ledger whose new_window_hash is not the source line's must refuse")
    except Refusal as ex:
        check("new_window_hash" in str(ex), "audio binding: the source line's hash must be the ledger's -> %s" % str(ex)[:80])
    json.dump({"leg": leg, "first_divergent_window": 1, "old_window_hash": "0x1", "new_window_hash": "0x9",
               "rebaseline_gate": {"window": 1, "by_kind": {"ROW": 3, "OPEN": 0, "MIXED": 0, "UNATTRIBUTED": 0}, "rows": ["P9-TEST-ROW"]}}, open(la, "w"))
    ea = apply(leg, "audio", "W1", ["P9-TEST-ROW"], la, src, baseline=base, commit="abc1234", known_ids=known)
    check(ea["new_line"] == "1,0x9" and ea["image"] == "-" and len(rb.read_manifest(ld)) == 3, "audio W1 re-based through the audibility gate block (bound to the lane values); no image")
    json.dump({"leg": leg, "first_divergent_window": 2, "old_window_hash": "0x1", "new_window_hash": "0x9",
               "rebaseline_gate": {"window": 2, "by_kind": {"ROW": 1, "OPEN": 0, "ORDER": 3, "MIXED": 0, "UNATTRIBUTED": 0}, "rows": ["P9-TEST-ROW"]}}, open(la, "w"))
    try:
        apply(leg, "audio", "W2", ["P9-TEST-ROW"], la, src, baseline=base, commit="abc1234", known_ids=known)
        check(False, "an audio window with ORDER voices must refuse")
    except Refusal as ex:
        check("ORDER 3" in str(ex), "an audio window with slot permutations (ORDER) refuses -> %s" % str(ex)[:80])
    json.dump({"leg": leg, "first_divergent_window": 1, "rebaseline_gate": {"window": 1, "by_kind": {"ROW": 0, "OPEN": 3, "MIXED": 0, "UNATTRIBUTED": 0}, "rows": []}}, open(la, "w"))
    try:
        apply(leg, "audio", "W1", ["P9-TEST-ROW"], la, src, baseline=base, commit="abc1234", known_ids=known)
        check(False, "an OPEN audio window must refuse")
    except Refusal as ex:
        check("OPEN 3" in str(ex) or "exists already" in str(ex), "an audio window with OPEN voices refuses -> %s" % str(ex)[:80])
    check(frozen_digests(ld) == before2, "frozen digests still equal after the audio apply (the probe set gained old_b2 before the b2 case only)")
    check("effective reference '120," in render(e, leg), "render")
    shutil.rmtree(tmp, ignore_errors=True)
    print("apply_rebaseline selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--leg")
    ap.add_argument("--lane", choices=rb.LANES)
    ap.add_argument("--window")
    ap.add_argument("--rows", default="")
    ap.add_argument("--ledger")
    ap.add_argument("--source", help="the NEW leg dir carrying the lane file (and, for mem, new_b<N>.ps2st unless --image)")
    ap.add_argument("--image", help="the NEW probe image for the mem window (default <source>/new_b<N>.ps2st)")
    ap.add_argument("--baseline", default=leg_spec.BASELINE_DIR)
    ap.add_argument("--commit", help="the NEW source commit (default: git rev-parse --short HEAD)")
    ap.add_argument("--note", default="")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not (a.leg and a.lane and a.window and a.ledger and a.source):
        ap.error("--leg, --lane, --window, --rows, --ledger and --source are required")
    try:
        e = apply(a.leg, a.lane, a.window, a.rows.split(","), a.ledger, a.source, a.image, a.baseline, a.commit, a.note, a.dry_run)
    except Refusal as ex:
        print("apply_rebaseline: REFUSED (%s %s %s): %s" % (a.leg, a.lane, a.window, ex))
        return EXIT_REFUSED
    except RuntimeError as ex:
        print("apply_rebaseline: %s" % ex)
        return EXIT_FROZEN_TOUCHED
    print(("DRY RUN " if a.dry_run else "") + render(e, a.leg))
    return 0


if __name__ == "__main__":
    sys.exit(main())
