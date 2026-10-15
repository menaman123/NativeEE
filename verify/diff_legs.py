#!/usr/bin/env python3
"""diff_legs.py -- compare two harness legs (EQUIVALENCE_PLAN.md section 2).

  diff_legs.py A B [--allow-build-diff] [--cross-engine]
                   [--allow-set-difference KEY[,KEY...]]
                   [--lanes meta,frames,audio,mem] [--mask-ranges FILE]
                   [--fmv-pictures MANIFEST [--fmv-window-legacy LO-HI]] [--selftest]

Order of business (the comparison discipline, section 4):
  1. run.meta equality is ASSERTED first: gl_renderer, profile, env_hash
     (config fingerprint minus harness plumbing). build_id/tree may differ
     only with --allow-build-diff (the cross-tree mode).
     --allow-set-difference KEY[,KEY] (the DT-11/DT-12 self-oracle toggle pair,
     same binary, same inputs, one declared one-line selection): env_hash may
     differ ONLY because the declared Config keys differ in env_ps2. Every
     differing key is listed as `NOTE (declared selection): key: A vs B`
     (`<default>` for the side that left it at its default) and carried on the
     verdict line as `declared selection: k1,k2`; an undeclared fingerprint
     difference is META MISMATCH (exit 2) before any lane compare, and a declared
     key that does not differ is a NOTE. gl_renderer/profile are still asserted;
     build_id/tree still follow --allow-build-diff. Not combinable with
     --cross-engine; never edit run.meta to pass the gate instead.
     --cross-engine (OLD vs NEW binary, CONSTRUCTION_PLAN section 2): the two
     trees have different config vocabularies, so the gate is `profile`
     equality plus finalized=1 on both; gl_renderer / env_hash / build_id /
     tree differences are reported as NOTEs, never gated.
  2. frames.fhash: paired BY KEY (the `vf` of each `vf,hash` line), never by
     line index (accepted-difference class DP-9 (a), user decision 2026-10-01):
     the original's graphics thread skipped a present when the next queued job
     was also a VSync, so an OLD leg carries one line per PRESENT and lacks the
     keys of those coalesced presents (coldboot keys 64/65, ...), while NEW
     writes one line per boundary. When exactly one leg is `tree=legacy`, a key
     the legacy leg lacks inside its key span is a COALESCED PRESENT: skipped,
     listed, and counted on the verdict line (`coalesced N`) -- never silent.
     Between two legs of the same tree (OLD vs OLD, NEW vs NEW) a missing key
     on either side is a divergence, so the admissibility self-diffs stay
     strict. Values are never tolerated: the first key both legs carry with
     different hashes is the first divergent frame. Different last keys are a
     length divergence (as before).
     --fmv-pictures MANIFEST [--fmv-window-legacy LO-HI] (DP-12 (a)): the FMV
     window is paired BY PICTURE, not by frame hash -- see fmv_window_report().
     The restore-point present (DP-26 (a)): a restored anchor's key 1 is NOT a
     value pair. The original's line 1 is its boot-era present BEFORE the load
     (no readable scanout -> hash 0 by construction) and its first post-load
     present is keyed `restored_vsync_frame + 1`; NEW's boundary 0 IS the
     restore point, so its key 1 is the restored scanout (DISPFB out of the
     restored local memory) -- an event the original has no line for. When
     exactly one leg is `tree=legacy` with line 1 `1,0x0` and the other leg's
     run.meta carries `restored_vsync_frame=<vf>` != 0, key 1 is reported as
     `restore-point present (OLD has none)` -- listed, and carried on the verdict
     line -- and the values are compared from key vf + 1 (see
     restore_point_rule()). A cold boot (restored_vsync_frame 0 / absent) and a
     legacy line 1 that is not 0 are unaffected: key 1 pairs by value as before.
     The SCENE-EDGE rule (DECISIONS_LOG DT-6; verify/clock_pair.py): when exactly one
     leg is `tree=legacy` and a leg dir carries a LEG.tsv with a `pair_map` (its first
     token `old_b<E>=new_b<E'>` = the boot-SM case-7 entry edge on each side), the lanes
     are paired from the edge by the boundary offset E' - E: frames keys from the legacy
     edge key E + vf_offset pair with NEW keys from E' + 1 (derived from the lane files:
     NEW's `--frames N` leg writes N frames lines keyed 1..N, so boundary b presents at key
     b + 1; OLD's key is its vsync frame = boundary + vf_offset once its boot FMVs
     coalesced, and its edge log line carries that frame -- FREEZE.md "Scene-aligned
     pairing"); audio windows from E // 6
     with E' // 6; mem lines from the edge only when the offset is a multiple of the
     lane cadence (60), else the post-edge lines are UNPAIRED (the probe pairs carry the
     lane, score.py). Keys before the LOWER edge pair by key as before; the keys between
     the lower edge and the longer side's own edge are its load-duration keys (the DP-17
     class) -- UNPAIRED, listed and counted, never absent/coalesced. Every report line
     and the verdict line carry `scene-paired: offset +N from old_b<E>=new_b<E'>`.
     PER RUN (P8.30-T): `--pairing <probe leg dir | PAIRING.tsv>` names the run's own edge
     (verify/scene_pairing.py, written by probe_images.py): the offset is the RUN's and the
     text reads `scene-paired: offset +N from old_b<E>=new_b<E'> (run; frozen reference +R,
     deviation +D)`; the frozen map is the reference, never the silent default once a run
     record exists. A record with EDGE-NOT-FOUND leaves the frozen rule in force and is named.
  3. audio.ahash: first divergent 100 ms window.
  4. mem.mhash:   first divergent checkpoint.
  The EFFECTIVE REFERENCE (DECISIONS_LOG DT-4, proposed): a leg dir carrying
  `REBASELINE.tsv` (verify/rebaseline.py's grammar; written only by
  apply_rebaseline.py, ledger-gated) has the lane line of every re-based window
  replaced by the manifest's NEW line BEFORE lanes 2-4 compare -- the frozen
  files are never edited. Every substitution is listed (`rebased: <lane> <window>
  (<row ids>)` report line) and carried on the verdict line, never silently; a
  manifest that does not parse is a usage error (exit 2). The masked mem
  compare (--mask-ranges, state images) does not read the manifest.
  --lanes restricts 2-4 to a comma list of meta,frames,audio,mem ("meta" =
  gate only). Default: all four.
  --mask-ranges FILE (EQUIVALENCE_PLAN section 7 accepted differences): the
  mem lane is then compared UNDER A MASK, which a whole-RAM hash line cannot
  express -- it compares the state_b<N>.ps2st images both legs dropped
  (`--state-every`), hashing RAM0 and SPAD with every masked byte range
  zeroed, then META (never maskable) -- state_hash.py's chain. FILE lines:
      0xLO-0xHI  <ROW-ID>  # why
  (HI exclusive; blank lines and # comments ignored). EVERY range names the
  accepted-difference row that justifies it: <ROW-ID> matches
  ^P[0-9]+-[A-Z0-9][A-Z0-9-]*$ and heads a row of a section-7.1 table in
  docs/design/EQUIVALENCE_PLAN.md or a docs/phases/PHASE*_PLAN.md /
  PHASE*_IMPL.md; a line whose row id is missing or does not resolve is a
  usage error (exit 2) naming the line. Ranges inside 0x00000000-0x02000000
  mask RAM0; ranges inside 0x70000000-0x70004000 mask the scratchpad (offset =
  addr & 0x3fff) and are accepted ONLY inside SPAD_MASK_WINDOW (the boot
  stack's dead words -- user decision 2, see the constant); any other range is
  a usage error. The report names the mask file and every range with its lane
  and row; the verdict word is IDENTICAL-UNDER-MASK (never plain lane
  identity) and EVERY verdict line -- per image and the final one -- carries
  the masked byte count per lane, so a mask can never be silent (a plain run
  prints `masked 0 B`). Images missing on either side are reported as MISSING
  (a divergence). Masking is symmetric: a leg compared with itself is
  IDENTICAL-UNDER-MASK with the same counts on both sides.

Exit codes: 0 identical (all requested lanes), 1 divergent, 2 usage/meta-gate
error (a rejected mask file is a usage error). The final line names the lane
set the verdict covers, the masked byte counts and the coalesced-present count,
e.g. `diff_legs: IDENTICAL (lanes=meta, masked 0 B, coalesced 0)` -- a META-only
pass is not lane identity. --selftest runs the built-in unit checks (no legs
needed) and exits.
"""
import collections
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import state_hash  # noqa: E402  (verify/state_hash.py: the .ps2st envelope parser)
import rebaseline  # noqa: E402  (verify/rebaseline.py: the re-baseline manifest reader)
import leg_spec  # noqa: E402  (verify/leg_spec.py: LEG.tsv -- the scene-edge pair_map, DT-6)
import scene_pairing  # noqa: E402  (verify/scene_pairing.py: the run's own edge, PAIRING.tsv)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The two state blobs a mask may touch, as guest address windows (HI exclusive).
# The META arena (the HLE bookkeeping blob) is never maskable.
RAM0_WINDOW = (0x00000000, 0x02000000)   # main RAM, 32 MiB
SPAD_WINDOW = (0x70000000, 0x70004000)   # scratchpad, 16 KiB (state offset = addr & 0x3fff)
# SPAD mask hard limit -- user decision 2 (INTEGRATION_IMPL I5.1-H, 2026-09-29): the ONLY
# scratchpad bytes a mask may cover are the boot stack's 13 dead words, word-inclusive
# 0x70003ef0..0x70003f40 in PHASE4_IMPL :942 / user decision 2 (first word 0x3ef0, last
# word 0x3f40); HI exclusive here, so the same 13 words are 0x70003ef0-0x70003f44 (84 B;
# accepted-difference row P4-CKPT0-STATICS). A SPAD range that is not entirely inside this
# window is a usage error, so a mask can never grow to hide live scratchpad state. Change
# this constant only with a new user decision on the record.
SPAD_MASK_WINDOW = (0x70003ef0, 0x70003f44)

MASK_LANES = ("RAM0", "SPAD", "META")
ROW_ID_RE = re.compile(r"^P[0-9]+-[A-Z0-9][A-Z0-9-]*$")
ROW_ID_IN_CELL_RE = re.compile(r"(?<![A-Z0-9-])P[0-9]+-[A-Z0-9][A-Z0-9-]*")
ROW_DOCS = ("docs/design/EQUIVALENCE_PLAN.md", "docs/phases/PHASE*_PLAN.md",
            "docs/phases/PHASE*_IMPL.md")

# One mask range as parsed: lane = RAM0 | SPAD, lo/hi = offsets INTO that lane's blob
# (HI exclusive), row = the accepted-difference row id, text = the range as written.
MaskRange = collections.namedtuple("MaskRange", "lane lo hi row text")


class MaskError(ValueError):
    """A mask file that must not be applied: bad grammar, an unresolvable row id, or a
    range outside the maskable windows. The message names the offending line."""


def read_meta(leg):
    path = os.path.join(leg, "run.meta")
    meta = {}
    with open(path) as f:
        for line in f:
            if "=" in line:
                k, v = line.rstrip("\n").split("=", 1)
                meta[k] = v
    return meta


def read_lines(leg, name):
    path = os.path.join(leg, name)
    if not os.path.isfile(path):
        return None
    with open(path) as f:
        return [l.rstrip("\n") for l in f if l.strip()]


def first_divergence(a_lines, b_lines):
    """Returns (index, a_line, b_line) of the first divergence or None.
    Length mismatch counts as divergence at the shorter length."""
    n = min(len(a_lines), len(b_lines))
    for i in range(n):
        if a_lines[i] != b_lines[i]:
            return (i, a_lines[i], b_lines[i])
    if len(a_lines) != len(b_lines):
        i = n
        return (i,
                a_lines[i] if i < len(a_lines) else "<absent>",
                b_lines[i] if i < len(b_lines) else "<absent>")
    return None


def known_row_ids(root=REPO_ROOT):
    """The row ids that head a table row of a section-7.1 table (`### 7.1 ...` up to the
    next heading) in the accepted-difference registries (ROW_DOCS). Only the FIRST cell
    of a row counts, so an id merely mentioned in a row's text does not resolve."""
    ids = set()
    for pattern in ROW_DOCS:
        for path in sorted(glob.glob(os.path.join(root, pattern))):
            in_71 = False
            with open(path, encoding="utf-8") as f:
                for line in f:
                    if re.match(r"^#{1,6}\s", line):
                        in_71 = bool(re.match(r"^#{1,6}\s*7\.1\b", line))
                    elif in_71 and line.startswith("|"):
                        ids.update(ROW_ID_IN_CELL_RE.findall(line[1:].split("|", 1)[0]))
    return ids


def classify_range(lo, hi):
    """(lane, lane_lo, lane_hi) for a guest address range [lo, hi), or MaskError."""
    if RAM0_WINDOW[0] <= lo and hi <= RAM0_WINDOW[1]:
        return "RAM0", lo, hi
    if SPAD_WINDOW[0] <= lo and hi <= SPAD_WINDOW[1]:
        if not (SPAD_MASK_WINDOW[0] <= lo and hi <= SPAD_MASK_WINDOW[1]):
            raise MaskError("SPAD range 0x%x-0x%x is outside the SPAD mask window 0x%x-0x%x "
                            "(user decision 2: only the boot stack's dead words may be masked)"
                            % (lo, hi, SPAD_MASK_WINDOW[0], SPAD_MASK_WINDOW[1]))
        return "SPAD", lo - SPAD_WINDOW[0], hi - SPAD_WINDOW[0]
    raise MaskError("range 0x%x-0x%x lies in neither RAM0 0x%x-0x%x nor SPAD 0x%x-0x%x "
                    "(META is never maskable)"
                    % (lo, hi, RAM0_WINDOW[0], RAM0_WINDOW[1], SPAD_WINDOW[0], SPAD_WINDOW[1]))


def read_mask(path, known_ids=None):
    """Parse a mask file into [MaskRange, ...]. MaskError names the offending line.
    known_ids: the resolvable row ids (default: known_row_ids() from the docs)."""
    if known_ids is None:
        known_ids = known_row_ids()
    ranges = []
    with open(path) as f:
        for ln, raw in enumerate(f, 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            where = "%s:%d: %r" % (path, ln, raw.rstrip("\n"))
            m = re.match(r"^(0x[0-9a-fA-F]+|\d+)\s*-\s*(0x[0-9a-fA-F]+|\d+)(?:\s+(\S+))?$", line)
            if not m:
                raise MaskError("%s: expected `0xLO-0xHI  <ROW-ID>  # why`" % where)
            lo, hi, row = int(m.group(1), 0), int(m.group(2), 0), m.group(3)
            if hi <= lo:
                raise MaskError("%s: empty range" % where)
            if not row:
                raise MaskError("%s: missing accepted-difference row id (every mask range "
                                "names its section-7.1 row)" % where)
            if not ROW_ID_RE.match(row):
                raise MaskError("%s: malformed row id %r (expected %s)" % (where, row, ROW_ID_RE.pattern))
            if row not in known_ids:
                raise MaskError("%s: unknown row id %r (heads no section-7.1 table row in %s)"
                                % (where, row, ", ".join(ROW_DOCS)))
            try:
                lane, llo, lhi = classify_range(lo, hi)
            except MaskError as e:
                raise MaskError("%s: %s" % (where, e))
            ranges.append(MaskRange(lane, llo, lhi, row, "0x%08x-0x%08x" % (lo, hi)))
    return ranges


def coalesce(spans):
    """Sorted, merged [(lo, hi)] so overlapping ranges are zeroed and counted once."""
    out = []
    for lo, hi in sorted(spans):
        if out and lo <= out[-1][1]:
            out[-1] = (out[-1][0], max(out[-1][1], hi))
        else:
            out.append((lo, hi))
    return out


def masked_counts_text(counts):
    """`masked RAM0 68 B, SPAD 84 B, META 0 B` -- or `masked 0 B` when no mask is in play."""
    if counts is None:
        return "masked 0 B"
    return "masked " + ", ".join("%s %d B" % (l, counts.get(l, 0)) for l in MASK_LANES)


def masked_state_hash(path, ranges):
    """(FNV-1a64 over RAM0 -> SPAD -> META of a .ps2st file with the masked bytes zeroed,
    {lane: bytes actually zeroed}). Ranges are clipped to the blob; META is never touched."""
    _, _, sections = state_hash.parse(path)
    tags = {"RAM0": state_hash.TAG_RAM, "SPAD": state_hash.TAG_SPAD, "META": state_hash.TAG_META}
    for lane in MASK_LANES:
        if tags[lane] not in sections:
            raise ValueError("%s: missing section %s" % (path, lane))
    counts = {}
    h = state_hash.FNV_OFFSET
    for lane in MASK_LANES:
        blob = sections[tags[lane]][2]
        spans = coalesce([(r.lo, r.hi) for r in ranges if r.lane == lane])
        n = 0
        if spans:
            blob = bytearray(blob)
            for lo, hi in spans:
                hi = min(hi, len(blob))
                if lo < hi:
                    blob[lo:hi] = bytes(hi - lo)
                    n += hi - lo
            blob = bytes(blob)
        counts[lane] = n
        h = state_hash.fnv1a64(blob, h)
    return h, counts


def state_files(leg):
    """{N: path} of the leg's state_b<N>.ps2st drops."""
    out = {}
    for name in os.listdir(leg):
        m = re.match(r"^state_b(\d+)\.ps2st$", name)
        if m:
            out[int(m.group(1))] = os.path.join(leg, name)
    return out


def diff_mem_masked(a, b, mask_path, ranges, rep, stats):
    """The mem lane under a mask (ranges = read_mask(mask_path)). Returns True when every
    paired image is identical under it. stats['masked'] receives the per-lane byte counts
    actually zeroed (the maximum over the compared images; all zero when none was compared)."""
    cover = {l: sum(hi - lo for lo, hi in coalesce([(r.lo, r.hi) for r in ranges if r.lane == l]))
             for l in MASK_LANES}
    rep.append("mem: MASKED compare (mask %s: %d ranges, %s)" % (mask_path, len(ranges),
                                                                  masked_counts_text(cover)))
    for r in ranges:
        rep.append("  %s  %-4s  %8d B  %s" % (r.text, r.lane, r.hi - r.lo, r.row))
    applied = dict.fromkeys(MASK_LANES, 0)
    stats["masked"] = applied
    fa, fb = state_files(a), state_files(b)
    keys = sorted(set(fa) | set(fb))
    if not keys:
        rep.append("mem: MISSING state_b<N>.ps2st in both legs (run with --state-every)")
        return False
    identical = True
    for n in keys:
        if n not in fa or n not in fb:
            rep.append("state_b%d.ps2st: MISSING in %s" % (n, a if n not in fa else b))
            identical = False
            continue
        (ha, ca), (hb, cb) = masked_state_hash(fa[n], ranges), masked_state_hash(fb[n], ranges)
        for lane in MASK_LANES:
            applied[lane] = max(applied[lane], ca[lane], cb[lane])
        if ha == hb:
            rep.append("state_b%d.ps2st: IDENTICAL-UNDER-MASK (%s) hash 0x%016x"
                       % (n, masked_counts_text(ca), ha))
        else:
            identical = False
            rep.append("state_b%d.ps2st: DIVERGENT under mask: A 0x%016x B 0x%016x (%s)"
                       % (n, ha, hb, masked_counts_text(ca)))
        if cb != ca:
            rep.append("  NOTE: B's image was masked differently (%s): blob lengths differ"
                       % masked_counts_text(cb))
    return identical


LANES = ("meta", "frames", "audio", "mem")
LANE_FILES = {"frames": ("frames.fhash", "frame"),
              "audio": ("audio.ahash", "audio window"),
              "mem": ("mem.mhash", "mem checkpoint")}

LEGACY_TREE = "legacy"          # run.meta `tree=` of an OLD leg (the present-skip path)
COALESCED_LIST_MAX = 24         # keys listed on the coalesced-presents line before `...`


class FramesError(ValueError):
    """A frames.fhash line that is not `<key>,<hash>` with a decimal key."""


def keyed_lines(lines, name):
    """{key: value} and the ordered key list of `<key>,<hash>` lines. A duplicate key or
    a line without a decimal key is a FramesError naming the line."""
    by_key, order = {}, []
    for i, line in enumerate(lines):
        if "," not in line:
            raise FramesError("%s line %d: %r is not `<key>,<hash>`" % (name, i + 1, line))
        k, v = line.split(",", 1)
        if not k.isdigit():
            raise FramesError("%s line %d: key %r is not decimal" % (name, i + 1, k))
        k = int(k)
        if k in by_key:
            raise FramesError("%s line %d: duplicate key %d" % (name, i + 1, k))
        by_key[k] = v
        order.append(k)
    return by_key, order


def legacy_side(ma, mb):
    """'A' / 'B' when exactly one leg is an OLD (`tree=legacy`) leg, else None. Only
    that leg may lack keys (its coalesced presents); between two legs of the same
    tree a missing key is a divergence."""
    la, lb = ma.get("tree") == LEGACY_TREE, mb.get("tree") == LEGACY_TREE
    if la == lb:
        return None
    return "A" if la else "B"


def pair_frames_by_key(la, lb, legacy, window_a=None, window_b=None, fmv_mode=False):
    """Pair two frames.fhash line lists BY KEY.

    legacy: 'A' | 'B' | None -- the leg whose missing keys are coalesced presents.
    window_a / window_b: each leg's FMV window as an inclusive key range (DP-12 (a))
    or None; fmv_mode: True when --fmv-pictures is on (the windows then come from
    the request / census and may be None when a leg declares none). Each leg's OWN
    window is removed from its key set before pairing; keys inside a window are
    never compared (paired by picture instead). Keys ABOVE a window are paired by
    KEY OFFSET from each window's end (A's hi_a + n pairs with B's hi_b + n; the
    legacy leg's coalesce allowance holds there too, its missing key listed as
    hi_legacy + n). The two windows need not START at the same key: pre-window
    keys are paired only below the LOWER start; a leg's keys from the lower start
    up to its own window are UNPAIRED (reported and counted, never ABSENT, never
    coalesced). When fmv_mode is on and a leg declares no window, its keys at or
    above the other leg's start are unpaired and no post-window pairing exists
    (the lane is already NOT PAIRED by fmv_window_report).
    Returns dict(paired, coalesced=[legacy keys], divergence=None | (kind, key,
    a_text, b_text), max_a, max_b, unpaired_a, unpaired_b, pre_limit). kinds:
    'value' (both carry the key, hashes differ), 'absent' (a key missing on a side
    that may not lack keys), 'length' (the last keys differ). The first divergence
    in key order is reported."""
    da, oa = keyed_lines(la, "A frames.fhash")
    db, ob = keyed_lines(lb, "B frames.fhash")
    starts = [w[0] for w in (window_a, window_b) if w is not None]
    pre_limit = min(starts) if (fmv_mode and starts) else None
    both_windows = window_a is not None and window_b is not None

    def labels(d, win):
        """{label: key}; label = (0, key) before every window, (2, key - hi) above
        the leg's own window (only when both legs have one)."""
        out, unpaired = {}, []
        for k in d:
            if pre_limit is None:
                if win is None or k < win[0]:
                    out[(0, k)] = k
                elif k > win[1]:
                    out[(2, k - win[1])] = k
                continue
            if k < pre_limit:
                out[(0, k)] = k
            elif win is None or k < win[0]:
                unpaired.append(k)
            elif k > win[1] and both_windows:
                out[(2, k - win[1])] = k
        return out, unpaired

    (lab_a, unpaired_a), (lab_b, unpaired_b) = labels(da, window_a), labels(db, window_b)
    win_legacy = {"A": window_a, "B": window_b}.get(legacy)
    max_a = max(lab_a) if lab_a else (0, 0)
    max_b = max(lab_b) if lab_b else (0, 0)
    common_max = min(max_a, max_b)
    paired, coalesced, divergence = 0, [], None
    for lab in sorted(set(lab_a) | set(lab_b)):
        if lab > common_max:
            break
        in_a, in_b = lab in lab_a, lab in lab_b
        ka, kb = lab_a.get(lab), lab_b.get(lab)
        if in_a and in_b:
            if da[ka] != db[kb]:
                divergence = ("value", ka, "%d,%s" % (ka, da[ka]), "%d,%s" % (kb, db[kb]))
                break
            paired += 1
        elif (legacy == "A" and not in_a) or (legacy == "B" and not in_b):
            # the LEGACY leg's missing key: the label's key below the windows, else
            # its own window's end + the offset
            coalesced.append(lab[1] if lab[0] == 0 or win_legacy is None else win_legacy[1] + lab[1])
        else:
            divergence = ("absent", ka if ka is not None else kb,
                          "%d,%s" % (ka, da[ka]) if in_a else "<absent>",
                          "%d,%s" % (kb, db[kb]) if in_b else "<absent>")
            break
    if divergence is None and max_a != max_b and (both_windows or pre_limit is None):
        divergence = ("length", lab_a.get(common_max, lab_b.get(common_max, 0)),
                      "last key %d" % lab_a.get(max_a, 0), "last key %d" % lab_b.get(max_b, 0))
    return dict(paired=paired, coalesced=coalesced, divergence=divergence,
                max_a=lab_a.get(max_a, 0), max_b=lab_b.get(max_b, 0), keys_a=oa, keys_b=ob,
                unpaired_a=unpaired_a, unpaired_b=unpaired_b, pre_limit=pre_limit)


def coalesced_text(keys):
    if not keys:
        return "none"
    shown = ",".join(str(k) for k in keys[:COALESCED_LIST_MAX])
    return shown + (",..." if len(keys) > COALESCED_LIST_MAX else "")


RESTORE_POINT_KEY = 1           # the first frames.fhash key of every leg (DP-9 (c))


def restored_vsync_frame(meta):
    """The NEW leg's `restored_vsync_frame=<vf>` census as an int; 0 when absent,
    0, or not decimal (a cold boot)."""
    v = (meta.get("restored_vsync_frame") or "0").strip()
    return int(v) if v.isdigit() else 0


def restore_point_rule(la, lb, ma, mb, legacy):
    """DP-26 (a): is key 1 of this pair the restore-point present? Yes when exactly
    one leg is legacy (`legacy` = 'A' | 'B'), the LEGACY leg's line 1 is key 1 with
    hash 0 (its pre-load present: no readable scanout) and the OTHER leg's run.meta
    carries restored_vsync_frame != 0 (a restored anchor). Returns None when the
    rule does not apply, else dict(vf, legacy_line, new_line, la, lb) where la / lb
    are the two line lists WITHOUT key 1 (the value comparison starts at vf + 1).
    Raises FramesError when a leg carries a key between 1 and vf (exclusive) -- a
    restored leg's keys are 1, vf + 1, vf + 2, ...; anything else is not the shape
    this rule describes and is never skipped silently."""
    if legacy is None:
        return None
    leg_lines, new_lines, new_meta = (la, lb, mb) if legacy == "A" else (lb, la, ma)
    vf = restored_vsync_frame(new_meta)
    if vf == 0 or not leg_lines or not new_lines:
        return None
    lk, lv = (leg_lines[0].split(",", 1) + [""])[:2]
    nk, nv = (new_lines[0].split(",", 1) + [""])[:2]
    if not (lk.isdigit() and int(lk) == RESTORE_POINT_KEY and nk.isdigit() and int(nk) == RESTORE_POINT_KEY):
        return None
    try:
        if int(lv, 16) != 0:
            return None
    except ValueError:
        return None
    rest = {"A": [], "B": []}
    for side, lines in (("A", la), ("B", lb)):
        for i, line in enumerate(lines[1:]):
            k = line.split(",", 1)[0]
            if k.isdigit() and 1 < int(k) <= vf:
                raise FramesError("%s frames.fhash line %d: key %s lies between the restore-point key 1 and "
                                  "restored_vsync_frame %d (a restored leg's keys are 1, %d, %d, ...)"
                                  % (side, i + 2, k, vf, vf + 1, vf + 2))
            rest[side].append(line)
    return dict(vf=vf, legacy_line=leg_lines[0], new_line=new_lines[0], la=rest["A"], lb=rest["B"])


AUDIO_BOUNDARIES_PER_WINDOW = 6     # both trees push 800 mixer frames per EE boundary into 4800-frame windows
MEM_CADENCE = 60                    # mem.mhash writes every 60 boundaries


def scene_edge_rule(leg_dirs, legacy, pairing=None):
    """DT-6: the scene-edge pairing of a legacy-vs-NEW pair when a leg dir carries a LEG.tsv with a
    pair_map. Returns None when it does not apply, else dict(old_b, new_b, offset, vf_offset, text).
    The spec is read from either dir (the baseline dir carries it); a LegSpecError is raised as is
    (never a silent default). `pairing` = the run's scene_pairing record: with EDGE-FOUND its edge
    replaces the frozen map's and the text names run, reference and deviation."""
    if legacy is None:
        return None
    for d in leg_dirs:
        spec = leg_spec.read(d) if d else None
        if spec is not None and spec.get("pair_edge") is not None:
            e_old, e_new = spec["pair_edge"]
            if pairing is not None and pairing.get("status") == "EDGE-FOUND":
                e_new = pairing["new_edge"]
                return dict(old_b=e_old, new_b=e_new, offset=e_new - e_old, vf_offset=spec["vf_offset"],
                            text=scene_pairing.verdict_text(pairing, spec))
            text = "scene-paired: offset %+d from old_b%d=new_b%d" % (e_new - e_old, e_old, e_new)
            if pairing is not None:
                text += " (frozen map: the run's record reads %s)" % pairing.get("status")
            return dict(old_b=e_old, new_b=e_new, offset=e_new - e_old, vf_offset=spec["vf_offset"], text=text)
    return None


def pair_lines_by_edge(la, lb, edge_a, edge_b, legacy, name):
    """Pair two keyed lane line lists from a scene edge: keys below min(edge_a, edge_b) by KEY; keys from
    each side's own edge by OFFSET from it (A's edge_a + n <-> B's edge_b + n); a side's keys between the
    lower edge and its own edge are UNPAIRED (listed, counted). A key the legacy side lacks where the other
    has one is a coalesced present (frames only: legacy != None), listed and counted; on a same-tree pair or
    the other side it is an ABSENT divergence. Returns dict(paired, coalesced, unpaired_a, unpaired_b,
    divergence (kind, key_a_text, key_b_text) | None, last_a, last_b)."""
    da, _ = keyed_lines(la, "A " + name)
    db, _ = keyed_lines(lb, "B " + name)
    low = min(edge_a, edge_b)

    def labels(d, edge):
        out, unpaired = {}, []
        for k in d:
            if k < low:
                out[(0, k)] = k
            elif k < edge:
                unpaired.append(k)
            else:
                out[(1, k - edge)] = k
        return out, unpaired

    (lab_a, un_a), (lab_b, un_b) = labels(da, edge_a), labels(db, edge_b)
    max_a = max(lab_a) if lab_a else (0, 0)
    max_b = max(lab_b) if lab_b else (0, 0)
    common = min(max_a, max_b)
    paired, coalesced, divergence = 0, [], None
    for lab in sorted(set(lab_a) | set(lab_b)):
        if lab > common:
            break
        ka, kb = lab_a.get(lab), lab_b.get(lab)
        if ka is not None and kb is not None:
            if da[ka] != db[kb]:
                divergence = ("value", "%d,%s" % (ka, da[ka]), "%d,%s" % (kb, db[kb]))
                break
            paired += 1
        elif (legacy == "A" and ka is None) or (legacy == "B" and kb is None):
            coalesced.append(lab[1] if lab[0] == 0 else (edge_a if legacy == "A" else edge_b) + lab[1])
        else:
            divergence = ("absent", "%d,%s" % (ka, da[ka]) if ka is not None else "<absent>",
                          "%d,%s" % (kb, db[kb]) if kb is not None else "<absent>")
            break
    if divergence is None and max_a != max_b:
        divergence = ("length", "last key %d" % lab_a.get(max_a, 0), "last key %d" % lab_b.get(max_b, 0))
    return dict(paired=paired, coalesced=coalesced, unpaired_a=un_a, unpaired_b=un_b, divergence=divergence,
                last_a=lab_a.get(max_a, 0), last_b=lab_b.get(max_b, 0))


def diff_lane_by_edge(name, what, la, lb, legacy, rule, rep, stats, cadence, keys_a_fn, keys_b_fn, allow_coalesce):
    """One keyed lane under the scene-edge rule. keys_*_fn map the edge boundary to the lane's key on that
    side; `cadence` > 1 (mem) requires the offset to be a multiple of it, else the post-edge lines are
    unpaired and the lane cannot be identical. Returns True when identical."""
    edge_a, edge_b = (keys_a_fn(rule["old_b"]), keys_b_fn(rule["new_b"])) if legacy == "A" else (keys_a_fn(rule["new_b"]), keys_b_fn(rule["old_b"]))
    rep.append("%s: %s -- %s keys from %d pair with %s keys from %d; keys below %d by key"
               % (name, rule["text"], "A", edge_a, "B", edge_b, min(edge_a, edge_b)))
    if cadence > 1 and rule["offset"] % cadence != 0:
        da, _ = keyed_lines(la, "A " + name)
        db, _ = keyed_lines(lb, "B " + name)
        low = min(edge_a, edge_b)
        pre_a = [l for l in la if int(l.split(",", 1)[0]) < low]
        pre_b = [l for l in lb if int(l.split(",", 1)[0]) < low]
        post_a, post_b = len(da) - len(pre_a), len(db) - len(pre_b)
        stats["unpaired"] += post_a + post_b
        rep.append("%s: offset %+d is not a multiple of the lane cadence %d: the %d A + %d B lines from the edges are "
                   "UNPAIRED (the probe pairs carry this lane, score.py); the %d pre-edge lines compare by key"
                   % (name, rule["offset"], cadence, post_a, post_b, min(len(pre_a), len(pre_b))))
        d = first_divergence(pre_a, pre_b)
        if d is None:
            rep.append("%s: pre-edge lines identical (%d lines); post-edge NOT PAIRED" % (name, len(pre_a)))
        else:
            i, xa, xb = d
            rep.append("%s: FIRST DIVERGENT %s at line %d (key %s):" % (name, what, i + 1, xa.split(",")[0] if "," in xa else str(i)))
            rep.append("  A: %s" % xa)
            rep.append("  B: %s" % xb)
        return False
    r = pair_lines_by_edge(la, lb, edge_a, edge_b, legacy if allow_coalesce else None, name)
    stats["unpaired"] += len(r["unpaired_a"]) + len(r["unpaired_b"])
    for side, keys, e in (("A", r["unpaired_a"], edge_a), ("B", r["unpaired_b"], edge_b)):
        if keys:
            rep.append("%s: keys %s of %s are unpaired (%d; its keys between the lower edge and its own edge %d: the two "
                       "loads' durations differ, the DP-17 class)" % (name, coalesced_text(keys), side, len(keys), e))
    if r["coalesced"]:
        stats["coalesced"] += len(r["coalesced"])
        rep.append("%s: coalesced presents (keys the %s leg lacks, the original's VSync coalesce skip): %d -- %s"
                   % (name, legacy, len(r["coalesced"]), coalesced_text(r["coalesced"])))
    d = r["divergence"]
    if d is None:
        rep.append("%s: identical by scene-edge pairing (%d keys paired, coalesced=%d, last keys %d / %d)"
                   % (name, r["paired"], len(r["coalesced"]), r["last_a"], r["last_b"]))
        return True
    kind, xa, xb = d
    ka = xa.split(",")[0] if kind == "value" else "-"
    head = {"value": "FIRST DIVERGENT %s at key %s (paired by scene edge; B key %s):" % (what, ka, xb.split(",")[0]),
            "absent": "%s key ABSENT on a side that may not lack keys (scene-edge pairing):" % what,
            "length": "LENGTH divergence (last keys differ, scene-edge pairing):"}[kind]
    rep.append("%s: %s (%d keys paired before it, coalesced=%d)" % (name, head, r["paired"], len(r["coalesced"])))
    rep.append("  A: %s" % xa)
    rep.append("  B: %s" % xb)
    return False


def diff_frames(la, lb, ma, mb, rep, stats, fmv=None, leg_dirs=("", ""), pairing=None):
    """The frames lane: by-key pairing (DP-9 (a)) with the coalesced presents of a
    legacy leg reported and counted; the FMV window (fmv = the parsed
    --fmv-pictures request, see fmv_window_report) excluded from the pairing and
    paired by picture; the restore-point present of a restored anchor (DP-26 (a),
    restore_point_rule) reported and removed from the value pairing. leg_dirs =
    (A, B) for the plane-check file. Returns True when the lane is identical."""
    legacy = legacy_side(ma, mb)
    win_a, win_b = None, None
    fmv_ok = True
    rule = scene_edge_rule(leg_dirs, legacy, pairing)
    if rule is not None:
        if fmv is not None:
            rep.append("frames.fhash: the FMV window rule is not combined with the scene-edge rule (the FMV keys lie "
                       "below the edge and pair by key there); --fmv-pictures ignored for this leg")
        stats["scene_pair"] = rule["text"]
        try:
            return diff_lane_by_edge("frames.fhash", "frame", la, lb, legacy, rule, rep, stats, 1,
                                     lambda b: b + rule["vf_offset"], lambda b: b + 1, True)
        except FramesError as e:
            rep.append("frames.fhash: %s" % e)
            return False
    if fmv is not None:
        fmv_ok, win_a, win_b = fmv_window_report(ma, mb, legacy, fmv, rep, leg_dirs)
    try:
        rp = restore_point_rule(la, lb, ma, mb, legacy)
        if rp is not None:
            stats["restore_point"] = RESTORE_POINT_KEY
            rep.append("frames.fhash: restore-point present (OLD has none): key %d -- %s: %s (the original's "
                       "pre-load present, no readable scanout), %s: %s (the restored scanout); values compared "
                       "from key %d (restored_vsync_frame %d + 1)"
                       % (RESTORE_POINT_KEY, legacy, rp["legacy_line"], "B" if legacy == "A" else "A",
                          rp["new_line"], rp["vf"] + 1, rp["vf"]))
            la, lb = rp["la"], rp["lb"]
        r = pair_frames_by_key(la, lb, legacy, win_a, win_b, fmv_mode=fmv is not None)
    except FramesError as e:
        rep.append("frames.fhash: %s" % e)
        stats["coalesced"] = 0
        stats["unpaired"] = 0
        return False
    stats["coalesced"] = len(r["coalesced"])
    stats["unpaired"] = len(r["unpaired_a"]) + len(r["unpaired_b"])
    if fmv is not None and r["pre_limit"] is not None:
        # The windows' starts: pre-window keys pair only below the lower start; a leg's
        # keys from there to its own window (or all of them above it when it declares
        # no window) are unpaired -- listed and counted, never coalesced or ABSENT.
        starts = {"A": win_a[0] if win_a else None, "B": win_b[0] if win_b else None}
        if win_a is not None and win_b is not None and win_a[0] != win_b[0]:
            later = "A" if win_a[0] > win_b[0] else "B"
            keys = r["unpaired_a"] if later == "A" else r["unpaired_b"]
            rep.append("frames.fhash: FMV windows start at different keys: A %d, B %d -- keys %s of %s are "
                       "unpaired (%d; pre-window keys pair only below %d)"
                       % (starts["A"], starts["B"], coalesced_text(keys), later, len(keys), r["pre_limit"]))
        for side, keys in (("A", r["unpaired_a"]), ("B", r["unpaired_b"])):
            if keys and starts[side] is None:
                rep.append("frames.fhash: keys %s of %s are unpaired (%d): the %s leg declares no FMV window "
                           "(no post-window pairing)" % (coalesced_text(keys), side, len(keys), side))
    if r["coalesced"]:
        rep.append("frames.fhash: coalesced presents (keys the %s leg lacks, the original's VSync "
                   "coalesce skip): %d -- %s" % (legacy, len(r["coalesced"]), coalesced_text(r["coalesced"])))
    d = r["divergence"]
    if d is None:
        rep.append("frames.fhash: identical by key (%d keys paired, coalesced=%d, last key %d)"
                   % (r["paired"], len(r["coalesced"]), r["max_a"]))
        return fmv_ok
    kind, key, xa, xb = d
    what = {"value": "FIRST DIVERGENT frame at key %d (paired by key):" % key,
            "absent": "frame key %d ABSENT on a side that may not lack keys (%s):"
                      % (key, "same tree, no coalesce allowance" if legacy is None
                         else "only the %s leg's missing keys are coalesced presents" % legacy),
            "length": "LENGTH divergence after key %d (last keys differ):" % key}[kind]
    rep.append("frames.fhash: %s (%d keys paired before it, coalesced=%d)"
               % (what, r["paired"], len(r["coalesced"])))
    rep.append("  A: %s" % xa)
    rep.append("  B: %s" % xb)
    return False


# --- the FMV window, paired by picture (DP-12 (a)) --------------------------------

FMV_WINDOW_RE = re.compile(r"^(\d+)-(\d+)$")
PLANE_CHECK_FILE = "fmv_plane_check.txt"
PLANE_LINE_RE = re.compile(r"^movie(\d+)\s+boundary=(\d+)\s+plane=0x([0-9a-fA-F]+)\s+"
                           r"picture=(\d+)\s+strips_exact=(\d+)/(\d+)\s*$")


def read_picture_manifest(path):
    """The FmvPictureOracle golden (tests/ipu/fixtures/coldboot_pictures.sha256):
    `# ... served_total=N` header + one `<index>\\t<sha256>\\t<w>\\t<h>` row per served
    picture. Returns (served_total, rows) -- rows as [(index, sha256, w, h)]."""
    served_total, rows = None, []
    with open(path) as f:
        for raw in f:
            line = raw.rstrip("\n")
            if not line.strip():
                continue
            if line.startswith("#"):
                m = re.search(r"served_total=(\d+)", line)
                if m:
                    served_total = int(m.group(1))
                continue
            parts = line.split("\t")
            if len(parts) != 4 or not parts[0].isdigit():
                raise ValueError("%s: bad manifest row %r" % (path, line))
            rows.append((int(parts[0]), parts[1], int(parts[2]), int(parts[3])))
    if served_total is None:
        raise ValueError("%s: no `served_total=` header" % path)
    return served_total, rows


def read_plane_check(leg):
    """<leg>/fmv_plane_check.txt -- the plane check's record, one line per movie:
        movie<N> boundary=<b> plane=0x<ee_addr> picture=<decoded_index> strips_exact=<n>/<total>
    (written by the plane-check procedure in verify/README.md: the leg's
    state_b<b>.ps2st at a serve boundary holds the golden picture laid out by
    FrameWriter's RGB32 strip layout at <ee_addr>; a movie PASSES only with every
    strip bit-exact). Returns {movie: (b, addr, picture, exact, total)} or None when
    the file is absent. A malformed line is a ValueError naming it."""
    path = os.path.join(leg, PLANE_CHECK_FILE)
    if not os.path.isfile(path):
        return None
    out = {}
    with open(path) as f:
        for ln, raw in enumerate(f, 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            m = PLANE_LINE_RE.match(line)
            if not m:
                raise ValueError("%s:%d: %r is not `movie<N> boundary=<b> plane=0x<addr> "
                                 "picture=<k> strips_exact=<n>/<total>`" % (path, ln, raw.rstrip("\n")))
            movie = int(m.group(1))
            out[movie] = (int(m.group(2)), int(m.group(3), 16), int(m.group(4)),
                          int(m.group(5)), int(m.group(6)))
    return out


def parse_fmv_request(manifest_path, legacy_window):
    """The --fmv-pictures request: (manifest path, legacy window (lo, hi) or None).
    ValueError on a malformed window."""
    win = None
    if legacy_window:
        m = FMV_WINDOW_RE.match(legacy_window)
        if not m or int(m.group(1)) > int(m.group(2)):
            raise ValueError("--fmv-window-legacy expects LO-HI with LO <= HI, got %r" % legacy_window)
        win = (int(m.group(1)), int(m.group(2)))
    return (manifest_path, win)


def new_fmv_window(meta):
    """The NEW leg's FMV window as frame KEYS, from its run.meta census
    `fmv_window=<first_boundary>-<last_boundary>` (the boundaries at which the
    Verify slot sampled an active movie; key = boundary + 1). None when absent or
    `none`."""
    v = meta.get("fmv_window", "")
    m = FMV_WINDOW_RE.match(v)
    if not m:
        return None
    return (int(m.group(1)) + 1, int(m.group(2)) + 1)


def fmv_window_report(ma, mb, legacy, fmv, rep, leg_dirs):
    """DP-12 (a): the FMV window is excluded from the by-key pairing on both legs and
    declared PAIRED-BY-PICTURE when (1) the NEW leg's census `fmv_pictures_served`
    equals the manifest's `served_total` and its row count (NEW served exactly the
    golden's pictures; the oracle ctest FmvPictureOracle proves the golden against
    the movie files offline), and (2) the plane check (<leg>/fmv_plane_check.txt)
    records every manifest movie with all strips bit-exact. Anything less is a
    divergence of the lane, reported with the counts. Returns (ok, window of A,
    window of B) as inclusive key ranges or None. The window of the legacy leg comes from
    --fmv-window-legacy (its presents inside the FMV: the hold frame, both movies and
    the splash presents between them); the NEW leg's from its own `fmv_window` census."""
    manifest_path, legacy_win = fmv
    try:
        served_total, rows = read_picture_manifest(manifest_path)
    except (OSError, ValueError) as e:
        rep.append("frames: FMV window NOT PAIRED: manifest unreadable: %s" % e)
        return False, None, None
    movies_in_manifest = 2 if rows and any(r[2:] != rows[0][2:] for r in rows) else 1
    new_meta = mb if legacy == "A" else ma
    new_leg_name = "B" if legacy == "A" else "A"
    new_leg_dir = leg_dirs[1] if legacy == "A" else leg_dirs[0]
    win_new = new_fmv_window(new_meta)
    windows = {"A": None, "B": None}
    if legacy is not None:
        windows[legacy] = legacy_win
    windows[new_leg_name] = win_new
    problems = []
    if legacy is not None and legacy_win is None:
        problems.append("the %s (legacy) leg's window needs --fmv-window-legacy LO-HI" % legacy)
    if win_new is None:
        problems.append("the %s leg's run.meta has no `fmv_window=<first>-<last>` census" % new_leg_name)
    served = new_meta.get("fmv_pictures_served")
    served_n = int(served) if served is not None and served.isdigit() else None
    if served_n is None:
        problems.append("the %s leg's run.meta has no `fmv_pictures_served`" % new_leg_name)
    elif not (served_n == served_total == len(rows)):
        problems.append("pictures served %d != manifest served_total %d / rows %d"
                        % (served_n, served_total, len(rows)))
    plane = None
    plane_ok = 0
    try:
        plane = read_plane_check(new_leg_dir)
    except ValueError as e:
        problems.append("plane check: %s" % e)
    if plane is None:
        problems.append("plane check: %s absent in the %s leg (procedure: verify/README.md)"
                        % (PLANE_CHECK_FILE, new_leg_name))
    else:
        for movie in range(1, movies_in_manifest + 1):
            rec = plane.get(movie)
            if rec is None:
                problems.append("plane check: movie%d has no line" % movie)
            elif rec[3] != rec[4] or rec[4] == 0:
                problems.append("plane check: movie%d strips_exact=%d/%d (not bit-exact)" % (movie, rec[3], rec[4]))
            else:
                plane_ok += 1
    rep.append("frames: FMV window excluded from the by-key pairing (post-window keys paired by key offset "
               "from each window's end): A keys %s, B keys %s"
               % ("%d-%d" % windows["A"] if windows["A"] else "none",
                  "%d-%d" % windows["B"] if windows["B"] else "none"))
    counts = "%s/%d, plane-check %d/%d movies" % (served_n if served_n is not None else "?",
                                                  served_total, plane_ok, movies_in_manifest)
    if problems:
        rep.append("frames: FMV window NOT PAIRED (%s): %s" % (counts, "; ".join(problems)))
        return False, windows["A"], windows["B"]
    rep.append("frames: FMV window PAIRED-BY-PICTURE (%s)" % counts)
    return True, windows["A"], windows["B"]


def parse_lanes(spec):
    """'meta,frames' -> ['meta', 'frames']; None/'' -> all. Unknown = ValueError."""
    if not spec:
        return list(LANES)
    lanes = [x.strip() for x in spec.replace("|", ",").split(",") if x.strip()]
    for l in lanes:
        if l not in LANES:
            raise ValueError("unknown lane %r (choose from %s)" % (l, ",".join(LANES)))
    return lanes


def parse_env_ps2(text):
    """The run.meta `env_ps2` fingerprint (Config::Fingerprint: every NON-DEFAULT
    `key=value;` followed by `profile=<name>`) as a {key: value} dict. A key the
    binary left at its default is absent, so a toggle pair shows up as a key
    present on one side only; the caller treats absence as "default"."""
    out = {}
    for item in (text or "").split(";"):
        if not item:
            continue
        key, _, value = item.partition("=")
        out[key] = value
    return out


def declared_selection_gate(ma, mb, allowed, rep):
    """The reviewed toggle-pair comparator (DT-11/DT-12 self-oracle arm (a)): the two
    legs' env_ps2 may differ ONLY in the keys the reviewer declared with
    --allow-set-difference. Every actually differing key is listed (`NOTE (declared
    selection): key: A vs B`, `<default>` for an absent side); an undeclared difference
    is META MISMATCH (exit 2) before any lane compare; a declared key that does not
    differ is a NOTE too, so a stale declaration is visible. gl_renderer and profile are
    still asserted equal by the caller; env_hash's difference is then the declared
    selection's, which is reported, never silently accepted."""
    ea, eb = ma.get("env_ps2"), mb.get("env_ps2")
    if ea is None or eb is None:
        rep.append("META MISMATCH env_ps2: missing on %s; a declared selection difference "
                   "cannot be attributed without the fingerprint" % ("both" if ea is None and eb is None else "A" if ea is None else "B"))
        return False
    da, db = parse_env_ps2(ea), parse_env_ps2(eb)
    differing = sorted(k for k in set(da) | set(db) if da.get(k) != db.get(k))
    undeclared = [k for k in differing if k not in allowed]
    if undeclared:
        rep.append("META MISMATCH env_ps2: undeclared selection difference in %s (declared: %s)"
                   % (",".join(undeclared), ",".join(sorted(allowed)) or "-"))
        for k in undeclared:
            rep.append("  %s: %r vs %r" % (k, da.get(k, "<default>"), db.get(k, "<default>")))
        return False
    for k in differing:
        rep.append("NOTE (declared selection): %s: %r vs %r" % (k, da.get(k, "<default>"), db.get(k, "<default>")))
    for k in sorted(allowed):
        if k not in differing:
            rep.append("NOTE (declared selection): %s does not differ between the legs" % k)
    if differing:
        rep.append("meta: env_hash differs by the declared selection only (%s): %r vs %r"
                   % (",".join(differing), ma.get("env_hash"), mb.get("env_hash")))
    return True


def diff_legs(a, b, allow_build_diff=False, quiet=False, cross_engine=False,
              lanes=None, mask_ranges=None, stats=None, fmv=None, pairing=None,
              allow_set_difference=None):
    """Returns (exit_code, report_lines). mask_ranges: a mask file path (mem lane
    compared over state_b<N>.ps2st images with the ranges zeroed) or None; a mask
    file that does not parse is a usage error (2). fmv: the parse_fmv_request()
    tuple (--fmv-pictures) or None. stats: an optional dict that receives
    {'masked': {lane: bytes}} when a mask is in play, {'coalesced': n} (the
    coalesced presents skipped by the frames lane; 0 when the lane did not run) and
    {'restore_point': k} (the key the restore-point rule removed; 0 when none).
    allow_set_difference: None, or the declared set of Config keys (the one-line
    toggles) that may differ between the legs' env_ps2 fingerprints -- the reviewed
    toggle-pair comparator; any other fingerprint difference is still META MISMATCH."""
    rep = []
    lanes = lanes or list(LANES)
    stats = stats if stats is not None else {}
    stats["coalesced"] = 0
    stats["unpaired"] = 0
    stats["restore_point"] = 0
    stats["rebased"] = []
    stats["scene_pair"] = ""
    stats["declared_selection"] = []
    allowed = set(allow_set_difference or ())
    mask = None
    try:
        manifests = {leg: rebaseline.read_manifest(leg) for leg in (a, b)}
    except rebaseline.ManifestError as e:
        return 2, ["rebaseline manifest: %s" % e]
    if mask_ranges:
        try:
            mask = read_mask(mask_ranges)
        except (OSError, MaskError) as e:
            return 2, ["mask: %s" % e]
        stats["masked"] = dict.fromkeys(MASK_LANES, 0)
    try:
        ma, mb = read_meta(a), read_meta(b)
    except OSError as e:
        return 2, ["meta read failed: %s" % e]

    # 1. meta gate
    if cross_engine:
        if ma.get("profile") != mb.get("profile"):
            rep.append("META MISMATCH profile: %r vs %r" %
                       (ma.get("profile"), mb.get("profile")))
            return 2, rep
        for leg, m in ((a, ma), (b, mb)):
            if m.get("finalized") != "1":
                rep.append("META MISMATCH finalized: %s is not finalized (%r)" %
                           (leg, m.get("finalized")))
                return 2, rep
        for key in ("gl_renderer", "env_hash", "build_id", "tree"):
            if ma.get(key) != mb.get(key):
                rep.append("NOTE (cross-engine): meta %s differs: %r vs %r" %
                           (key, ma.get(key), mb.get(key)))
        rep.append("meta: gate PASS (profile=%s, both finalized)" % ma.get("profile"))
    else:
        for key in ("gl_renderer", "profile"):
            if ma.get(key) != mb.get(key):
                rep.append("META MISMATCH %s: %r vs %r" %
                           (key, ma.get(key), mb.get(key)))
                return 2, rep
        if allowed:
            # the declared toggle pair: the fingerprint may differ ONLY in the declared keys
            if not declared_selection_gate(ma, mb, allowed, rep):
                return 2, rep
            da, db = parse_env_ps2(ma.get("env_ps2")), parse_env_ps2(mb.get("env_ps2"))
            stats["declared_selection"] = sorted(k for k in set(da) | set(db) if da.get(k) != db.get(k))
        elif ma.get("env_hash") != mb.get("env_hash"):
            rep.append("META MISMATCH env_hash: %r vs %r (a one-line toggle pair needs "
                       "--allow-set-difference KEY[,KEY] naming the reviewed selection)" %
                       (ma.get("env_hash"), mb.get("env_hash")))
            return 2, rep
        for key in ("build_id", "tree"):
            if ma.get(key) != mb.get(key):
                msg = "meta %s differs: %r vs %r" % (key, ma.get(key),
                                                     mb.get(key))
                if allow_build_diff:
                    rep.append("NOTE (cross-tree): " + msg)
                else:
                    rep.append("META MISMATCH " + msg +
                               " (use --allow-build-diff for cross-tree)")
                    return 2, rep

    identical = True
    for lane in lanes:
        if lane == "meta":
            continue
        if lane == "mem" and mask is not None:
            try:
                if not diff_mem_masked(a, b, mask_ranges, mask, rep, stats):
                    identical = False
            except (OSError, ValueError) as e:
                rep.append("mem: mask compare failed: %s" % e)
                identical = False
            continue
        name, what = LANE_FILES[lane]
        la, lb = read_lines(a, name), read_lines(b, name)
        if la is None or lb is None:
            rep.append("%s: MISSING in %s" % (name,
                                              a if la is None else b))
            identical = False
            continue
        for leg, lines_ref in ((a, "la"), (b, "lb")):
            lines, applied = rebaseline.effective_lines(leg, lane, la if lines_ref == "la" else lb, manifests[leg])
            if applied:
                if lines_ref == "la":
                    la = lines
                else:
                    lb = lines
                stats["rebased"] += applied
                for e in applied:
                    rep.append("%s: rebased %s %s (%s) in %s: frozen %r -> effective %r (commit %s, proof %s)"
                               % (name, e["lane"], e["window"], ", ".join(e["rows"]), leg, e["old_line"], e["new_line"], e["commit"], e["proof"]))
        if lane == "frames":
            try:
                if not diff_frames(la, lb, ma, mb, rep, stats, fmv=fmv, leg_dirs=(a, b), pairing=pairing):
                    identical = False
            except leg_spec.LegSpecError as e:
                return 2, ["LEG.tsv: %s" % e]
            continue
        legacy = legacy_side(ma, mb)
        try:
            rule = scene_edge_rule((a, b), legacy, pairing)
        except leg_spec.LegSpecError as e:
            return 2, ["LEG.tsv: %s" % e]
        if rule is not None:
            stats["scene_pair"] = rule["text"]
            # audio windows = boundary // 6 (both trees); mem lines = boundary (every 60)
            try:
                if lane == "audio":
                    ok = diff_lane_by_edge(name, what, la, lb, legacy, rule, rep, stats, 1,
                                           lambda b: b // AUDIO_BOUNDARIES_PER_WINDOW, lambda b: b // AUDIO_BOUNDARIES_PER_WINDOW, False)
                    ra, rb = rule["old_b"] % AUDIO_BOUNDARIES_PER_WINDOW, rule["new_b"] % AUDIO_BOUNDARIES_PER_WINDOW
                    if ra != rb:
                        rep.append("%s: NOTE the edges sit %d and %d boundaries into their windows (window offset %+d, residual %+d boundary)"
                                   % (name, ra, rb, rule["new_b"] // AUDIO_BOUNDARIES_PER_WINDOW - rule["old_b"] // AUDIO_BOUNDARIES_PER_WINDOW, rb - ra))
                else:
                    ok = diff_lane_by_edge(name, what, la, lb, legacy, rule, rep, stats, MEM_CADENCE,
                                           lambda b: b, lambda b: b, False)
            except FramesError as e:
                rep.append("%s: %s" % (name, e))
                ok = False
            if not ok:
                identical = False
            continue
        d = first_divergence(la, lb)
        if d is None:
            rep.append("%s: identical (%d lines)" % (name, len(la)))
        else:
            identical = False
            i, xa, xb = d
            key = xa.split(",")[0] if "," in xa else str(i)
            rep.append("%s: FIRST DIVERGENT %s at line %d (key %s):" %
                       (name, what, i + 1, key))
            rep.append("  A: %s" % xa)
            rep.append("  B: %s" % xb)
    return (0 if identical else 1), rep


def selftest_declared_selection(A, C, meta):
    """--allow-set-difference unit checks (the reviewed toggle-pair comparator); legs A/C are
    restored to `meta` afterwards. Returns True when every check holds."""
    ok = True
    # meta gate: different env_hash must exit 2 before any hash compare
    open(os.path.join(C, "run.meta"), "w").write(
        meta.replace("env_hash=0x1", "env_hash=0x2"))
    code, _ = diff_legs(A, C)
    ok &= (code == 2)
    # --allow-set-difference: the declared toggle pair. A's fingerprint leaves the
    # key at its default; C's carries it. Only the declared key may differ.
    pair = meta.replace("env_ps2=E", "env_ps2=engine.mtvu=off;profile=p")
    open(os.path.join(A, "run.meta"), "w").write(pair)
    open(os.path.join(C, "run.meta"), "w").write(
        pair.replace("env_ps2=engine.mtvu=off;", "env_ps2=engine.mtvu=off;hle.graphics_movie=on;")
            .replace("env_hash=0x1", "env_hash=0x2"))
    open(os.path.join(C, "frames.fhash"), "w").write("1,0xa\n2,0xb\n3,0xc\n")
    st = {}
    code, rep = diff_legs(A, C, allow_set_difference=["hle.graphics_movie"], stats=st)
    ok &= (code == 0 and st["declared_selection"] == ["hle.graphics_movie"]
           and any("NOTE (declared selection): hle.graphics_movie: '<default>' vs 'on'" in l for l in rep)
           and any("env_hash differs by the declared selection only (hle.graphics_movie)" in l for l in rep))
    ok &= ("declared selection: hle.graphics_movie" in verdict(code, ["frames"], declared_selection=st["declared_selection"]))
    ok &= (diff_legs(A, C)[0] == 2)                                   # undeclared -> still gated
    code, rep = diff_legs(A, C, allow_set_difference=["hle.graphics_sky"])  # wrong declaration
    ok &= (code == 2 and any("undeclared selection difference in hle.graphics_movie" in l for l in rep))
    code, rep = diff_legs(A, C, allow_set_difference=["hle.graphics_movie", "hle.graphics_sky"])
    ok &= (code == 0 and any("hle.graphics_sky does not differ" in l for l in rep))
    # a value change (not just default vs set) and a second undeclared key
    open(os.path.join(C, "run.meta"), "w").write(
        pair.replace("env_ps2=engine.mtvu=off;", "env_ps2=engine.mtvu=on;hle.graphics_movie=on;")
            .replace("env_hash=0x1", "env_hash=0x2"))
    code, rep = diff_legs(A, C, allow_set_difference=["hle.graphics_movie"])
    ok &= (code == 2 and any("engine.mtvu: 'off' vs 'on'" in l for l in rep))
    # identical fingerprints under a declaration: no gate failure, the stale declaration is a NOTE
    open(os.path.join(C, "run.meta"), "w").write(pair)
    code, rep = diff_legs(A, C, allow_set_difference=["hle.graphics_movie"], stats=st)
    ok &= (code == 0 and st["declared_selection"] == [] and any("does not differ" in l for l in rep))
    # a leg without env_ps2 cannot attribute the difference
    open(os.path.join(C, "run.meta"), "w").write(
        pair.replace("env_ps2=engine.mtvu=off;profile=p\n", "").replace("env_hash=0x1", "env_hash=0x2"))
    code, rep = diff_legs(A, C, allow_set_difference=["hle.graphics_movie"])
    ok &= (code == 2 and any("env_ps2: missing on B" in l for l in rep))
    # gl_renderer still gated under a declaration
    open(os.path.join(C, "run.meta"), "w").write(pair.replace("gl_renderer=G", "gl_renderer=H"))
    ok &= (diff_legs(A, C, allow_set_difference=["hle.graphics_movie"])[0] == 2)
    ok &= (parse_env_ps2("a=1;b=x=y;profile=p") == {"a": "1", "b": "x=y", "profile": "p"})
    ok &= (parse_env_ps2(None) == {})
    open(os.path.join(A, "run.meta"), "w").write(meta)
    open(os.path.join(C, "run.meta"), "w").write(meta)
    return bool(ok)


def selftest():
    import tempfile
    ok = True
    d = tempfile.mkdtemp(prefix="diff_legs_selftest.")
    A, B, C = [os.path.join(d, x) for x in "ABC"]
    meta = ("build_id=X\ngl_renderer=G\ntree=legacy\nprofile=p\n"
            "env_ps2=E\nenv_hash=0x1\nframes=3\nwall_ms=1\nfinalized=1\n")
    for leg, frames in ((A, ["1,0xa", "2,0xb", "3,0xc"]),
                        (B, ["1,0xa", "2,0xb", "3,0xc"]),
                        (C, ["1,0xa", "2,0xZ", "3,0xc"])):
        os.makedirs(leg, exist_ok=True)
        open(os.path.join(leg, "run.meta"), "w").write(meta)
        open(os.path.join(leg, "frames.fhash"), "w").write(
            "\n".join(frames) + "\n")
        open(os.path.join(leg, "audio.ahash"), "w").write("0,0x1\n")
        open(os.path.join(leg, "mem.mhash"), "w").write("60,0x2\n")
    code, _ = diff_legs(A, B)
    ok &= (code == 0)
    code, rep = diff_legs(A, C)
    ok &= (code == 1)
    ok &= any("FIRST DIVERGENT frame at key 2 (paired by key)" in l for l in rep)
    # lanes: meta-only ignores the frame divergence; frames alone sees it
    ok &= (diff_legs(A, C, lanes=["meta"])[0] == 0)
    ok &= (diff_legs(A, C, lanes=["audio", "mem"])[0] == 0)
    ok &= (diff_legs(A, C, lanes=["frames"])[0] == 1)
    ok &= (parse_lanes("meta|frames") == ["meta", "frames"])
    ok &= (parse_lanes(None) == list(LANES))
    try:
        parse_lanes("bogus")
        ok = False
    except ValueError:
        pass
    # cross-engine: env_hash/gl_renderer/build_id/tree differences are NOTEs,
    # profile mismatch or an unfinalized leg still fails the gate
    open(os.path.join(C, "run.meta"), "w").write(
        meta.replace("env_hash=0x1", "env_hash=0x2").replace("tree=legacy", "tree=new")
            .replace("gl_renderer=G", "gl_renderer=none").replace("build_id=X", "build_id=Y"))
    code, rep = diff_legs(A, C, cross_engine=True, lanes=["meta"])
    ok &= (code == 0 and any("gate PASS" in l for l in rep)
           and sum("NOTE (cross-engine)" in l for l in rep) == 4)
    ok &= (diff_legs(A, C, cross_engine=True)[0] == 1)   # frames still diverge
    open(os.path.join(C, "run.meta"), "w").write(meta.replace("profile=p", "profile=q"))
    ok &= (diff_legs(A, C, cross_engine=True, lanes=["meta"])[0] == 2)
    open(os.path.join(C, "run.meta"), "w").write(meta.replace("finalized=1", "finalized=0"))
    ok &= (diff_legs(A, C, cross_engine=True, lanes=["meta"])[0] == 2)
    open(os.path.join(C, "run.meta"), "w").write(meta)
    ok &= selftest_declared_selection(A, C, meta)
    # build_id difference: rejected by default, allowed with the flag
    open(os.path.join(C, "run.meta"), "w").write(
        meta.replace("build_id=X", "build_id=Y"))
    open(os.path.join(C, "frames.fhash"), "w").write(
        "1,0xa\n2,0xb\n3,0xc\n")
    ok &= (diff_legs(A, C)[0] == 2)
    ok &= (diff_legs(A, C, allow_build_diff=True)[0] == 0)
    # length mismatch = divergence
    open(os.path.join(B, "frames.fhash"), "w").write("1,0xa\n2,0xb\n")
    ok &= (diff_legs(A, B)[0] == 1)

    # --mask-ranges. Synthetic images: 128 B RAM0, a full 16 KiB SPAD, 16 B META.
    import struct as _st
    import zlib as _zl

    def _state(path, ram, spad=None):
        secs = [(state_hash.TAG_RAM, ram), (state_hash.TAG_SPAD, spad or b"\x00" * SPAD_SIZE),
                (state_hash.TAG_META, b"\x00" * 16)]
        body = bytearray(state_hash.MAGIC + _st.pack("<IIIIQ", 4, len(secs), 0, 0, 0))
        for t, payload in secs:
            body += _st.pack("<IIIIQ", t, 1, 0, _zl.crc32(payload) & 0xFFFFFFFF, len(payload)) + payload
        body += _st.pack("<I", _zl.crc32(bytes(body)) & 0xFFFFFFFF)
        open(path, "wb").write(bytes(body))

    def _spad(fill, lo=SPAD_MASK_WINDOW[0] - SPAD_WINDOW[0], hi=SPAD_MASK_WINDOW[1] - SPAD_WINDOW[0]):
        s = bytearray(SPAD_SIZE)
        s[lo:hi] = bytes([fill]) * (hi - lo)
        return bytes(s)

    def check(name, cond):
        print("diff_legs selftest: %s  %s" % ("PASS" if cond else "FAIL", name))
        return bool(cond)

    SPAD_SIZE = SPAD_WINDOW[1] - SPAD_WINDOW[0]
    ROW = "P4-CKPT0-STATICS"                       # a real section-7.1 row (PHASE4_PLAN.md)
    open(os.path.join(B, "frames.fhash"), "w").write("1,0xa\n2,0xb\n3,0xc\n")
    _state(os.path.join(A, "state_b0.ps2st"), b"\x11" * 64 + b"\x00" * 64)
    _state(os.path.join(B, "state_b0.ps2st"), b"\x22" * 64 + b"\x00" * 64)
    mask = os.path.join(d, "mask.txt")

    def run_mask(text, a=A, b=B):
        open(mask, "w").write(text)
        st = {}
        code, rep = diff_legs(a, b, lanes=["mem"], mask_ranges=mask, stats=st)
        return code, rep, st

    # RAM0 identical under the mask; the difference outside a narrower mask stays divergent
    code, rep, st = run_mask("# the first 64 bytes\n0x0-0x40  %s  # selftest\n" % ROW)
    ok &= check("RAM0 range with a row id: identical under mask, counts reported",
                code == 0 and any("IDENTICAL-UNDER-MASK (masked RAM0 64 B, SPAD 0 B, META 0 B)" in l
                                  for l in rep)
                and st["masked"] == {"RAM0": 64, "SPAD": 0, "META": 0}
                and any(l.strip().startswith("0x00000000-0x00000040  RAM0") and ROW in l for l in rep))
    code, rep, st = run_mask("0x0-0x20  %s\n" % ROW)
    ok &= check("difference outside the mask stays DIVERGENT, counts reported",
                code == 1 and any("DIVERGENT under mask" in l
                                  and "(masked RAM0 32 B, SPAD 0 B, META 0 B)" in l for l in rep)
                and st["masked"] == {"RAM0": 32, "SPAD": 0, "META": 0})
    # grammar: a range without a row id, an unknown row id, a malformed row id, extra tokens -> 2
    code, rep, _ = run_mask("0x0-0x40\n")
    ok &= check("range without a row id rejected (exit 2, line named)",
                code == 2 and "mask.txt:1" in rep[0] and "missing accepted-difference row id" in rep[0])
    code, rep, _ = run_mask("0x0-0x40  P9-NO-SUCH-ROW-I51H  # why\n")
    ok &= check("unknown row id rejected (exit 2, line named)",
                code == 2 and "mask.txt:1" in rep[0] and "unknown row id 'P9-NO-SUCH-ROW-I51H'" in rep[0])
    code, rep, _ = run_mask("0x0-0x40  bogus-id\n")
    ok &= check("malformed row id rejected (exit 2)", code == 2 and "malformed row id" in rep[0])
    code, rep, _ = run_mask("0x0-0x40  %s extra\n" % ROW)
    ok &= check("extra tokens rejected (exit 2)", code == 2 and "expected `0xLO-0xHI" in rep[0])
    code, rep, _ = run_mask("0x40-0x40  %s\n" % ROW)
    ok &= check("empty range rejected (exit 2)", code == 2 and "empty range" in rep[0])
    # SPAD: the dead-stack window is accepted and masks the SPAD blob at addr & 0x3fff
    _state(os.path.join(A, "state_b0.ps2st"), b"\x00" * 128, _spad(0xAA))
    _state(os.path.join(B, "state_b0.ps2st"), b"\x00" * 128, _spad(0xBB))
    spad_line = "0x%x-0x%x  %s  # the boot stack's dead words\n" % (SPAD_MASK_WINDOW + (ROW,))
    code, rep, st = run_mask(spad_line)
    ok &= check("SPAD window accepted: identical under mask, SPAD 84 B reported",
                code == 0 and st["masked"] == {"RAM0": 0, "SPAD": 84, "META": 0}
                and any("IDENTICAL-UNDER-MASK (masked RAM0 0 B, SPAD 84 B, META 0 B)" in l for l in rep)
                and any(l.strip().startswith("0x70003ef0-0x70003f44  SPAD") for l in rep))
    # HI is exclusive: a word AT the window's end is not hidden
    _state(os.path.join(B, "state_b0.ps2st"), b"\x00" * 128, _spad(0xAA, 0x3ef0, 0x3f48))
    code, rep, st = run_mask(spad_line)
    ok &= check("SPAD word at the window end (HI exclusive) stays DIVERGENT",
                code == 1 and st["masked"] == {"RAM0": 0, "SPAD": 84, "META": 0})
    # SPAD ranges outside the window, and ranges in neither blob, are usage errors
    for bad, why in (("0x70003ee0-0x70003f44", "starts before the window"),
                     ("0x70003ef0-0x70003f48", "ends after the window"),
                     ("0x70000000-0x70000010", "elsewhere in SPAD"),
                     ("0x70003ef0-0x70004010", "runs past SPAD")):
        code, rep, _ = run_mask("%s  %s\n" % (bad, ROW))
        ok &= check("SPAD range %s rejected (%s)" % (bad, why),
                    code == 2 and ("outside the SPAD mask window" in rep[0] or "neither RAM0" in rep[0]))
    code, rep, _ = run_mask("0x10000000-0x10000010  %s\n" % ROW)
    ok &= check("range in neither RAM0 nor SPAD rejected (META unmaskable)",
                code == 2 and "neither RAM0" in rep[0])
    code, rep, _ = run_mask("0x01fffff0-0x02000010  %s\n" % ROW)
    ok &= check("range straddling the RAM0 end rejected", code == 2 and "neither RAM0" in rep[0])
    # symmetry: a leg against itself under the mask is IDENTICAL-UNDER-MASK with equal counts
    both = "0x0-0x40  %s\n%s" % (ROW, spad_line)
    code, rep, st = run_mask(both, A, A)
    ok &= check("symmetry: A vs A under mask is IDENTICAL-UNDER-MASK with equal counts",
                code == 0 and st["masked"] == {"RAM0": 64, "SPAD": 84, "META": 0}
                and any("IDENTICAL-UNDER-MASK (masked RAM0 64 B, SPAD 84 B, META 0 B)" in l for l in rep)
                and not any("masked differently" in l for l in rep))
    # overlapping ranges are counted once
    code, rep, st = run_mask("0x0-0x40  %s\n0x20-0x60  %s\n" % (ROW, ROW), A, A)
    ok &= check("overlapping ranges counted once", code == 0 and st["masked"]["RAM0"] == 0x60)
    # a missing image is a divergence (exit 1), never a usage error
    os.remove(os.path.join(B, "state_b0.ps2st"))
    code, rep, _ = run_mask(spad_line)
    ok &= check("missing image = divergent", code == 1 and any("MISSING" in l for l in rep))
    # the row-id resolver: the shipped mask file's rows resolve, an invented id does not
    ids = known_row_ids()
    ok &= check("known_row_ids resolves the shipped file's rows",
                {"P4-CKPT0-STATICS", "P4-CKPT0-ELF-TEXT-RESIDENCY", "P4-CKPT0-STREAM-REPLY-PATCH",
                 "P4-CKPT0-LIBDMA-TABLE", "P4-CKPT0-MCSERV-VERSION", "P4-LIBGRAPH-SDK-SHAPE"} <= ids
                and "P9-NO-SUCH-ROW-I51H" not in ids)
    shipped = read_mask(os.path.join(REPO_ROOT, "verify", "mask", "coldboot_elf_text.ranges"))
    ok &= check("shipped mask file parses; its one SPAD range is exactly the window",
                [(r.lo, r.hi) for r in shipped if r.lane == "SPAD"]
                == [(SPAD_MASK_WINDOW[0] & 0x3fff, SPAD_MASK_WINDOW[1] & 0x3fff)]
                and all(ROW_ID_RE.match(r.row) for r in shipped))
    # the verdict line names the lane set it covers and the masked counts
    ok &= (verdict(0, ["mem"], masked={"RAM0": 64, "SPAD": 80, "META": 0})
           == "diff_legs: IDENTICAL-UNDER-MASK (lanes=mem, masked RAM0 64 B, SPAD 80 B, META 0 B, coalesced 0)")
    ok &= (verdict(1, ["mem"], masked={"RAM0": 64, "SPAD": 80, "META": 0})
           == "diff_legs: DIVERGENT (lanes=mem, masked RAM0 64 B, SPAD 80 B, META 0 B, coalesced 0)")
    ok &= (verdict(0, ["meta"]) == "diff_legs: IDENTICAL (lanes=meta, masked 0 B, coalesced 0)")
    ok &= (verdict(0, list(LANES)) == "diff_legs: IDENTICAL (lanes=meta,frames,audio,mem, masked 0 B, coalesced 0)")
    ok &= (verdict(1, ["meta", "frames"]) == "diff_legs: DIVERGENT (lanes=meta,frames, masked 0 B, coalesced 0)")
    ok &= (verdict(2, ["meta"]) == "diff_legs: META-GATE-FAIL (lanes=meta, masked 0 B, coalesced 0)")
    ok &= (verdict(0, ["frames"], coalesced=4) == "diff_legs: IDENTICAL (lanes=frames, masked 0 B, coalesced 4)")

    # --- the frames lane by key (DP-9 (a)) ------------------------------------------
    def leg(path, tree, frames, extra_meta=""):
        os.makedirs(path, exist_ok=True)
        open(os.path.join(path, "run.meta"), "w").write(meta.replace("tree=legacy", "tree=" + tree) + extra_meta)
        open(os.path.join(path, "frames.fhash"), "w").write("".join("%d,%s\n" % kv for kv in frames))
        open(os.path.join(path, "audio.ahash"), "w").write("0,0x1\n")
        open(os.path.join(path, "mem.mhash"), "w").write("60,0x2\n")
        return path

    OLD = leg(os.path.join(d, "old"), "legacy", [(1, "0xa"), (2, "0xb"), (3, "0xc"), (6, "0xf"), (7, "0xg")])
    NEW = leg(os.path.join(d, "new"), "new", [(1, "0xa"), (2, "0xb"), (3, "0xc"), (4, "0xd"), (5, "0xe"),
                                               (6, "0xf"), (7, "0xg")])
    st = {}
    code, rep = diff_legs(OLD, NEW, cross_engine=True, lanes=["frames"], stats=st)
    ok &= check("by-key pairing: keys the legacy leg lacks are coalesced, counted, listed, never silent",
                code == 0 and st["coalesced"] == 2
                and any("coalesced presents (keys the A leg lacks" in l and "2 -- 4,5" in l for l in rep)
                and any("identical by key (5 keys paired, coalesced=2, last key 7)" in l for l in rep))
    st = {}
    code, rep = diff_legs(NEW, OLD, cross_engine=True, lanes=["frames"], stats=st)
    ok &= check("by-key pairing is symmetric in the leg order (legacy on the B side)",
                code == 0 and st["coalesced"] == 2 and any("keys the B leg lacks" in l for l in rep))
    # a value difference under a shared key is the first divergent frame, keyed
    NEW2 = leg(os.path.join(d, "new2"), "new", [(1, "0xa"), (2, "0xb"), (3, "0xc"), (4, "0xd"), (5, "0xe"),
                                                 (6, "0xZ"), (7, "0xg")])
    st = {}
    code, rep = diff_legs(OLD, NEW2, cross_engine=True, lanes=["frames"], stats=st)
    ok &= check("value difference under a shared key = FIRST DIVERGENT at that key (coalesced still counted)",
                code == 1 and st["coalesced"] == 2
                and any("FIRST DIVERGENT frame at key 6 (paired by key)" in l for l in rep)
                and any(l == "  A: 6,0xf" for l in rep) and any(l == "  B: 6,0xZ" for l in rep))
    # a key the NEW leg lacks is never a coalesced present: a divergence
    NEW3 = leg(os.path.join(d, "new3"), "new", [(1, "0xa"), (2, "0xb"), (4, "0xd"), (5, "0xe"), (6, "0xf"), (7, "0xg")])
    code, rep = diff_legs(OLD, NEW3, cross_engine=True, lanes=["frames"])
    ok &= check("a key the NEW leg lacks is ABSENT = divergent (only the legacy leg coalesces)",
                code == 1 and any("frame key 3 ABSENT" in l for l in rep))
    # same tree on both sides: no coalesce allowance (admissibility self-diffs stay strict)
    OLD2 = leg(os.path.join(d, "old2"), "legacy", [(1, "0xa"), (2, "0xb"), (3, "0xc"), (5, "0xe"), (6, "0xf"), (7, "0xg")])
    st = {}
    code, rep = diff_legs(OLD, OLD2, lanes=["frames"], stats=st)
    ok &= check("OLD vs OLD with different coalesce patterns stays DIVERGENT (same tree, no allowance)",
                code == 1 and st["coalesced"] == 0 and any("frame key 5 ABSENT" in l and "same tree" in l for l in rep))
    st = {}
    code, rep = diff_legs(OLD, OLD, lanes=["frames"], stats=st)
    ok &= check("OLD vs OLD self-diff IDENTICAL with coalesced 0",
                code == 0 and st["coalesced"] == 0
                and verdict(code, ["frames"], coalesced=st["coalesced"])
                == "diff_legs: IDENTICAL (lanes=frames, masked 0 B, coalesced 0)")
    # different last keys = a length divergence, as before
    NEW4 = leg(os.path.join(d, "new4"), "new", [(1, "0xa"), (2, "0xb"), (3, "0xc"), (4, "0xd"), (5, "0xe"), (6, "0xf")])
    code, rep = diff_legs(OLD, NEW4, cross_engine=True, lanes=["frames"])
    ok &= check("different last keys = LENGTH divergence",
                code == 1 and any("LENGTH divergence after key 6" in l for l in rep)
                and any(l == "  A: last key 7" for l in rep) and any(l == "  B: last key 6" for l in rep))
    # The restore-point rule (DP-26 (a)): OLD's line 1 is `1,0x0` (its pre-load
    # present), NEW's line 1 is the restored scanout; values pair from vf + 1.
    Z = "0x0000000000000000"
    ROLD = leg(os.path.join(d, "rold"), "legacy", [(1, Z), (101, "0xp"), (102, "0xq"), (104, "0xs")])
    RNEW = leg(os.path.join(d, "rnew"), "new", [(1, "0xr1"), (101, "0xp"), (102, "0xq"), (103, "0xr"), (104, "0xs")],
               extra_meta="restored_vsync_frame=100\n")
    st = {}
    code, rep = diff_legs(ROLD, RNEW, cross_engine=True, lanes=["frames"], stats=st)
    ok &= check("restore-point rule: key 1 reported (OLD has none), values from vf + 1 identical, coalesce allowance kept",
                code == 0 and st["restore_point"] == 1 and st["coalesced"] == 1
                and any(l.startswith("frames.fhash: restore-point present (OLD has none): key 1 -- A: 1,%s" % Z)
                        and "B: 1,0xr1" in l and "values compared from key 101 (restored_vsync_frame 100 + 1)" in l
                        for l in rep)
                and any("identical by key (3 keys paired, coalesced=1, last key 104)" in l for l in rep)
                and verdict(code, ["frames"], coalesced=st["coalesced"], restore_point=st["restore_point"])
                == "diff_legs: IDENTICAL (lanes=frames, masked 0 B, coalesced 1, restore-point present (OLD has none): key 1)")
    st = {}
    code, rep = diff_legs(RNEW, ROLD, cross_engine=True, lanes=["frames"], stats=st)
    ok &= check("restore-point rule is side-symmetric (legacy on B)",
                code == 0 and st["restore_point"] == 1
                and any("restore-point present (OLD has none): key 1 -- B: 1,%s" % Z in l and "A: 1,0xr1" in l for l in rep))
    RNEW2 = leg(os.path.join(d, "rnew2"), "new", [(1, "0xr1"), (101, "0xp"), (102, "0xZ"), (103, "0xr"), (104, "0xs")],
                extra_meta="restored_vsync_frame=100\n")
    st = {}
    code, rep = diff_legs(ROLD, RNEW2, cross_engine=True, lanes=["frames"], stats=st)
    ok &= check("restore-point rule: a value difference at vf + 2 is still the FIRST DIVERGENT frame (1 key paired before it)",
                code == 1 and st["restore_point"] == 1
                and any("FIRST DIVERGENT frame at key 102 (paired by key): (1 keys paired before it" in l for l in rep)
                and verdict(code, ["frames"], coalesced=st["coalesced"], restore_point=st["restore_point"]).endswith(
                    "restore-point present (OLD has none): key 1)"))
    # the shapes the rule does NOT touch: a cold boot (no restored_vsync_frame; OLD's 0 at
    # key 1 then IS a value difference), a legacy line 1 that is not 0, a same-tree pair
    COLD = leg(os.path.join(d, "cold"), "new", [(1, "0xr1"), (101, "0xp"), (102, "0xq"), (104, "0xs")])
    st = {}
    code, rep = diff_legs(ROLD, COLD, cross_engine=True, lanes=["frames"], stats=st)
    ok &= check("cold boot (restored_vsync_frame absent): key 1 pairs by value -> FIRST DIVERGENT at key 1, no restore-point tail",
                code == 1 and st["restore_point"] == 0 and any("FIRST DIVERGENT frame at key 1 " in l for l in rep)
                and not any("restore-point" in l for l in rep)
                and verdict(code, ["frames"], coalesced=st["coalesced"], restore_point=st["restore_point"])
                == "diff_legs: DIVERGENT (lanes=frames, masked 0 B, coalesced 0)")
    ROLD2 = leg(os.path.join(d, "rold2"), "legacy", [(1, "0xr1"), (101, "0xp"), (102, "0xq"), (104, "0xs")])
    st = {}
    code, rep = diff_legs(ROLD2, RNEW, cross_engine=True, lanes=["frames"], stats=st)
    ok &= check("legacy line 1 not 0: no rule, key 1 pairs by value (identical here)",
                code == 0 and st["restore_point"] == 0 and not any("restore-point" in l for l in rep)
                and any("identical by key (4 keys paired, coalesced=1" in l for l in rep))
    RNEW3 = leg(os.path.join(d, "rnew3"), "new", [(1, Z), (101, "0xp"), (102, "0xq"), (103, "0xr"), (104, "0xs")],
                extra_meta="restored_vsync_frame=100\n")
    st = {}
    code, rep = diff_legs(RNEW3, RNEW, lanes=["frames"], stats=st)
    ok &= check("same tree on both sides: no restore-point rule (key 1 differs -> DIVERGENT at key 1)",
                code == 1 and st["restore_point"] == 0 and any("FIRST DIVERGENT frame at key 1 " in l for l in rep))
    RBAD = leg(os.path.join(d, "rbad"), "new", [(1, "0xr1"), (50, "0xx"), (101, "0xp")],
               extra_meta="restored_vsync_frame=100\n")
    st = {}
    code, rep = diff_legs(ROLD, RBAD, cross_engine=True, lanes=["frames"], stats=st)
    ok &= check("a key between 1 and vf on a restored leg is named, never skipped",
                code == 1 and any("key 50 lies between the restore-point key 1 and restored_vsync_frame 100" in l for l in rep))
    # the frozen restored anchors vs the P8.13-A NEW shape: OLD's 1,0x0 vs NEW's restored key 1
    base_art = os.path.join(leg_spec.BASELINE_DIR, "artdet_f37152")
    if os.path.isdir(base_art):
        ART = leg(os.path.join(d, "art_new"), "new", [(1, "0x9aada0a287ced70c"), (37157, "0x464ce75c4932bfe4")],
                  extra_meta="restored_vsync_frame=37156\n")
        ma_art = read_meta(base_art)
        open(os.path.join(ART, "run.meta"), "w").write(
            "build_id=Y\ngl_renderer=none\ntree=new\nprofile=%s\nenv_ps2=E\nenv_hash=0x2\nframes=2\nwall_ms=1\n"
            "finalized=1\nrestored_vsync_frame=37156\n" % ma_art.get("profile"))
        st = {}
        code, rep = diff_legs(base_art, ART, cross_engine=True, lanes=["frames"], stats=st)
        ok &= check("frozen artdet baseline vs a NEW restored shape: key 1 is the restore-point present, first value pair 37157",
                    code == 1 and st["restore_point"] == 1
                    and any("restore-point present (OLD has none): key 1 -- A: 1,%s" % Z in l for l in rep)
                    and any("FIRST DIVERGENT frame at key 37157 (paired by key): (0 keys paired before it" in l
                            for l in rep))
    # a malformed line is a divergence of the lane, named
    BAD = leg(os.path.join(d, "bad"), "new", [(1, "0xa")])
    open(os.path.join(BAD, "frames.fhash"), "a").write("nokey\n")
    code, rep = diff_legs(OLD, BAD, cross_engine=True, lanes=["frames"])
    ok &= check("malformed frames line named, lane divergent", code == 1 and any("line 2" in l and "nokey" in l for l in rep))
    # the 13 frozen baselines against themselves: IDENTICAL, coalesced 0 (the symmetric case);
    # 12 (coldboot + 11 anchors, 2026-09-25) + coldboot_attract (P8.22-T, 2026-10-04) -- the count pins the set
    base_root = leg_spec.BASELINE_DIR
    base_legs = sorted(x for x in os.listdir(base_root) if os.path.isfile(os.path.join(base_root, x, "frames.fhash")))
    self_ok = len(base_legs) == 13 and "coldboot_attract" in base_legs
    for name in base_legs:
        st = {}
        code, rep = diff_legs(os.path.join(base_root, name), os.path.join(base_root, name), lanes=["frames"], stats=st)
        self_ok &= (code == 0 and st["coalesced"] == 0)
    ok &= check("the 13 frozen baselines vs themselves: IDENTICAL, coalesced 0 (%d legs)" % len(base_legs), self_ok)

    # --- the FMV window paired by picture (DP-12 (a)) -------------------------------
    manifest = os.path.join(d, "pictures.sha256")
    open(manifest, "w").write("# source=selftest served_total=3\n0\taa\t640\t480\n1\tbb\t640\t480\n2\tcc\t512\t448\n")
    # OLD presents the FMV over keys 4-6 (hold + pictures) and coalesced key 3; NEW's window is
    # key 4 only (its boundary 3); post-window content re-aligns by order: OLD 7,8 <-> NEW 5,6.
    OLDF = leg(os.path.join(d, "oldf"), "legacy", [(1, "0xa"), (2, "0xb"), (4, "0x1"), (5, "0x2"), (6, "0x3"),
                                                   (7, "0xp"), (8, "0xq")])
    NEWF = leg(os.path.join(d, "newf"), "new", [(1, "0xa"), (2, "0xb"), (3, "0xc"), (4, "0x9"), (5, "0xp"), (6, "0xq")],
               extra_meta="fmv_pictures_served=3\nfmv_window=3-3\n")
    req = parse_fmv_request(manifest, "4-6")
    st = {}
    code, rep = diff_legs(OLDF, NEWF, cross_engine=True, lanes=["frames"], stats=st, fmv=req)
    ok &= check("FMV window without a plane check: NOT PAIRED (divergent), census counted, keys outside paired",
                code == 1 and any("FMV window NOT PAIRED (3/3, plane-check 0/2 movies)" in l and "absent" in l for l in rep)
                and any("A keys 4-6, B keys 4-4" in l for l in rep)
                and any("identical by key (4 keys paired, coalesced=1" in l for l in rep) and st["coalesced"] == 1)
    open(os.path.join(NEWF, PLANE_CHECK_FILE), "w").write(
        "movie1 boundary=2 plane=0x5fc500 picture=1 strips_exact=40/40\nmovie2 boundary=2 plane=0x5a6d00 picture=2 strips_exact=32/32\n")
    code, rep = diff_legs(OLDF, NEWF, cross_engine=True, lanes=["frames"], fmv=req)
    ok &= check("FMV window PAIRED-BY-PICTURE (3/3, plane-check 2/2 movies) with both movies bit-exact",
                code == 0 and any(l == "frames: FMV window PAIRED-BY-PICTURE (3/3, plane-check 2/2 movies)" for l in rep))
    open(os.path.join(NEWF, PLANE_CHECK_FILE), "w").write(
        "movie1 boundary=2 plane=0x5fc500 picture=1 strips_exact=40/40\nmovie2 boundary=2 plane=0x5a6d00 picture=2 strips_exact=30/32\n")
    code, rep = diff_legs(OLDF, NEWF, cross_engine=True, lanes=["frames"], fmv=req)
    ok &= check("a movie with strips_exact 30/32 is NOT PAIRED (no tolerance)",
                code == 1 and any("plane-check 1/2 movies" in l and "movie2 strips_exact=30/32" in l for l in rep))
    leg(os.path.join(d, "newf"), "new", [(1, "0xa"), (2, "0xb"), (3, "0xc"), (4, "0x9"), (5, "0xp"), (6, "0xq")],
        extra_meta="fmv_pictures_served=2\nfmv_window=3-3\n")
    open(os.path.join(NEWF, PLANE_CHECK_FILE), "w").write(
        "movie1 boundary=2 plane=0x5fc500 picture=1 strips_exact=40/40\nmovie2 boundary=2 plane=0x5a6d00 picture=2 strips_exact=32/32\n")
    code, rep = diff_legs(OLDF, NEWF, cross_engine=True, lanes=["frames"], fmv=req)
    ok &= check("served count != manifest total is NOT PAIRED",
                code == 1 and any("pictures served 2 != manifest served_total 3" in l for l in rep))
    code, rep = diff_legs(OLDF, NEWF, cross_engine=True, lanes=["frames"], fmv=parse_fmv_request(manifest, None))
    ok &= check("a legacy leg without --fmv-window-legacy is NOT PAIRED (window undeclared)",
                code == 1 and any("needs --fmv-window-legacy" in l for l in rep))
    # --- windows that START at different keys (the I8.2-A review's MAJOR 2) -----------
    # OLD's window 4-6 vs NEW's census window key 3 only (boundary 2): OLD's key 3 is a
    # pre-window present NEW swallowed into its window -> unpaired (reported, counted),
    # never ABSENT, never coalesced; the keys below 3 pair; post-window by offset.
    OLDS = leg(os.path.join(d, "olds"), "legacy", [(1, "0xa"), (2, "0xb"), (3, "0xc"), (4, "0x1"), (5, "0x2"), (6, "0x3"),
                                                   (7, "0xp"), (8, "0xq")])
    NEWS = leg(os.path.join(d, "news"), "new", [(1, "0xa"), (2, "0xb"), (3, "0x9"), (4, "0xp"), (5, "0xq")],
               extra_meta="fmv_pictures_served=3\nfmv_window=2-2\n")
    open(os.path.join(NEWS, PLANE_CHECK_FILE), "w").write(
        "movie1 boundary=2 plane=0x5fc500 picture=1 strips_exact=40/40\nmovie2 boundary=2 plane=0x5a6d00 picture=2 strips_exact=32/32\n")
    st = {}
    code, rep = diff_legs(OLDS, NEWS, cross_engine=True, lanes=["frames"], stats=st, fmv=parse_fmv_request(manifest, "4-6"))
    ok &= check("windows starting at different keys: the later-starting leg's pre-window keys are UNPAIRED (named, counted), not ABSENT",
                code == 0 and st["coalesced"] == 0 and st["unpaired"] == 1
                and any(l.startswith("frames.fhash: FMV windows start at different keys: A 4, B 3 -- keys 3 of A are unpaired (1;") for l in rep)
                and not any("ABSENT" in l for l in rep)
                and any("identical by key (4 keys paired, coalesced=0" in l for l in rep)
                and verdict(code, ["frames"], coalesced=st["coalesced"], unpaired=st["unpaired"])
                == "diff_legs: IDENTICAL (lanes=frames, masked 0 B, coalesced 0, unpaired 1)")
    st = {}
    code, rep = diff_legs(OLDS, NEWS, cross_engine=True, lanes=["frames"], stats=st, fmv=parse_fmv_request(manifest, "3-6"))
    ok &= check("the same pair with the legacy LO aligned to NEW's first window key: nothing unpaired, no verdict tail",
                code == 0 and st["unpaired"] == 0 and not any("start at different keys" in l for l in rep)
                and verdict(code, ["frames"], coalesced=0, unpaired=0) == "diff_legs: IDENTICAL (lanes=frames, masked 0 B, coalesced 0)")
    # NEW leg without a census (fmv_window=none): its keys inside the legacy window are
    # UNPAIRED (never coalesced presents of the legacy leg), the lane NOT PAIRED.
    NEWN = leg(os.path.join(d, "newn"), "new", [(1, "0xa"), (2, "0xb"), (3, "0xc"), (4, "0xd"), (5, "0xe")],
               extra_meta="fmv_pictures_served=3\nfmv_window=none\n")
    st = {}
    code, rep = diff_legs(OLDF, NEWN, cross_engine=True, lanes=["frames"], stats=st, fmv=parse_fmv_request(manifest, "4-6"))
    ok &= check("a NEW leg without a census: its keys in the legacy window are unpaired (counted), coalesced stays 0, NOT PAIRED",
                code == 1 and st["coalesced"] == 0 and st["unpaired"] == 2
                and any("has no `fmv_window=<first>-<last>` census" in l for l in rep)
                and any(l.startswith("frames.fhash: keys 4,5 of B are unpaired (2): the B leg declares no FMV window") for l in rep)
                and not any("LENGTH divergence" in l or "coalesced presents" in l for l in rep))
    # a coalesced present ABOVE the windows (paired by key offset): OLD lacks key 8 = hi_a 6 + 2
    OLDP = leg(os.path.join(d, "oldp"), "legacy", [(1, "0xa"), (2, "0xb"), (4, "0x1"), (5, "0x2"), (6, "0x3"),
                                                   (7, "0xp"), (9, "0xr"), (10, "0xs")])
    NEWP = leg(os.path.join(d, "newp"), "new", [(1, "0xa"), (2, "0xb"), (3, "0xc"), (4, "0x9"), (5, "0xp"), (6, "0xq"),
                                                (7, "0xr"), (8, "0xs")],
               extra_meta="fmv_pictures_served=3\nfmv_window=3-3\n")
    open(os.path.join(NEWP, PLANE_CHECK_FILE), "w").write(
        "movie1 boundary=2 plane=0x5fc500 picture=1 strips_exact=40/40\nmovie2 boundary=2 plane=0x5a6d00 picture=2 strips_exact=32/32\n")
    st = {}
    code, rep = diff_legs(OLDP, NEWP, cross_engine=True, lanes=["frames"], stats=st, fmv=parse_fmv_request(manifest, "4-6"))
    ok &= check("a post-window coalesced present lists the LEGACY leg's missing key (hi + offset = 8), pre-window 3 too",
                code == 0 and st["coalesced"] == 2 and st["unpaired"] == 0
                and any("coalesced presents (keys the A leg lacks" in l and "2 -- 3,8" in l for l in rep)
                and any("identical by key (5 keys paired, coalesced=2, last key 10)" in l for l in rep))
    try:
        parse_fmv_request(manifest, "6-4")
        ok = check("malformed --fmv-window-legacy rejected", False)
    except ValueError:
        ok &= check("malformed --fmv-window-legacy rejected", True)
    # the shipped golden parses: 547 rows, served_total=547, two movies' dims
    total, rows = read_picture_manifest(os.path.join(REPO_ROOT, "tests", "ipu", "fixtures", "coldboot_pictures.sha256"))
    ok &= check("shipped coldboot_pictures.sha256 parses (served_total=547, 547 rows, 2 picture sizes)",
                total == 547 and len(rows) == 547 and len(set(r[2:] for r in rows)) == 2)
    # --- the effective reference (DT-4): a leg dir with REBASELINE.tsv compares the re-based window against
    #     the manifest's NEW line, reports it, and the frozen file stays as it was
    R, N = os.path.join(d, "R"), os.path.join(d, "N")
    for ldir, tree, mem in ((R, "legacy", ["0,0xa", "60,0xb", "120,0xold"]), (N, "new", ["0,0xa", "60,0xb", "120,0xnew"])):
        os.makedirs(ldir, exist_ok=True)
        open(os.path.join(ldir, "run.meta"), "w").write(meta.replace("tree=legacy", "tree=" + tree))
        open(os.path.join(ldir, "frames.fhash"), "w").write("1,0xa\n2,0xb\n3,0xc\n")
        open(os.path.join(ldir, "audio.ahash"), "w").write("0,0x1\n")
        open(os.path.join(ldir, "mem.mhash"), "w").write("\n".join(mem) + "\n")
    st = {}
    code, rep = diff_legs(R, N, cross_engine=True, lanes=["mem"], stats=st)
    ok &= check("without a manifest the re-based window is DIVERGENT at key 120",
                code == 1 and any("FIRST DIVERGENT mem checkpoint at line 3 (key 120)" in l for l in rep) and st["rebased"] == [])
    open(os.path.join(R, rebaseline.MANIFEST), "w").write(
        rebaseline.header() + "mem\tb120\tP9-TEST-ROW\tabc1234\trebased/mem_b120.ledger.json\t120,0xold\t120,0xnew\trebased/new_b120.ps2st\t\n")
    frozen_before = open(os.path.join(R, "mem.mhash")).read()
    st = {}
    code, rep = diff_legs(R, N, cross_engine=True, lanes=["mem"], stats=st)
    line = verdict(code, ["mem"], coalesced=0, rebased=st["rebased"])
    ok &= check("with the manifest the mem lane is IDENTICAL against the effective reference and the report names it",
                code == 0 and any(l.startswith("mem.mhash: rebased mem b120 (P9-TEST-ROW)") for l in rep)
                and line.endswith("coalesced 0, rebased: mem b120 (P9-TEST-ROW))") and len(st["rebased"]) == 1)
    ok &= check("the frozen mem.mhash is untouched by the compare", open(os.path.join(R, "mem.mhash")).read() == frozen_before)
    ok &= check("a lane without a re-based window is unaffected (frames identical, audio identical, no report)",
                diff_legs(R, N, cross_engine=True, lanes=["frames", "audio"])[0] == 0
                and not any("rebased" in l for l in diff_legs(R, N, cross_engine=True, lanes=["frames", "audio"])[1]))
    open(os.path.join(R, rebaseline.MANIFEST), "w").write("mem\tnot-a-window\tP9-TEST-ROW\tc\tp\to\t1,0x1\t-\n")
    code, rep = diff_legs(R, N, cross_engine=True, lanes=["mem"])
    ok &= check("a manifest that does not parse is a usage error (2) naming it", code == 2 and rep[0].startswith("rebaseline manifest:"))
    os.remove(os.path.join(R, rebaseline.MANIFEST))
    # --- the scene-edge rule (DT-6): OLD edge b10 (vf_offset 5 -> frames key 15), NEW edge b13 (key 14): offset +3
    SO = os.path.join(d, "so")
    SN = os.path.join(d, "sn")
    # OLD frames keys: 1..13 pre-edge (coalesced 4), 14 = its own extra loading key below its edge key 15 (unpaired),
    # 15.. post-edge (p,q,r,s); NEW keys 1..13 pre-edge, 14.. post-edge (p,q,r,s). Lower edge key = 14.
    leg(SO, "legacy", [(1, "0xa"), (2, "0xb"), (3, "0xc"), (5, "0xe"), (6, "0xf"), (7, "0xg"), (8, "0xh"), (9, "0xi"), (10, "0xj"),
                       (11, "0xk"), (12, "0xl"), (13, "0xm"), (14, "0xn"), (15, "0xp"), (16, "0xq"), (17, "0xr"), (18, "0xs")])
    leg(SN, "new", [(1, "0xa"), (2, "0xb"), (3, "0xc"), (4, "0xd"), (5, "0xe"), (6, "0xf"), (7, "0xg"), (8, "0xh"), (9, "0xi"), (10, "0xj"),
                    (11, "0xk"), (12, "0xl"), (13, "0xm"), (14, "0xp"), (15, "0xq"), (16, "0xr"), (17, "0xs")])
    open(os.path.join(SO, leg_spec.FILE), "w").write(leg_spec.format_spec(20, 60, 0, 0, "coldboot", [(60, 20)], "t",
                                                                           pair_map="old_b10=new_b13,old_b60=new_b63", vf_offset=5))
    # audio: windows = boundary // 6: OLD edge window 1, NEW edge window 2 -> offset +1; mem: offset +3 is off the 60 cadence
    open(os.path.join(SO, "audio.ahash"), "w").write("0,0x1\n1,0xw1\n2,0xw2\n3,0xw3\n")
    open(os.path.join(SN, "audio.ahash"), "w").write("0,0x1\n1,0xv\n2,0xw1\n3,0xw2\n4,0xw3\n")
    open(os.path.join(SO, "mem.mhash"), "w").write("0,0xm0\n60,0xm60\n")
    open(os.path.join(SN, "mem.mhash"), "w").write("0,0xm0\n60,0xn60\n")
    st = {}
    code, rep = diff_legs(SO, SN, cross_engine=True, lanes=["frames"], stats=st)
    ok &= check("scene-edge rule, frames: keys below the lower edge key (14) by key (coalesced 4 kept), A's key 14 (its loading key "
                "below its edge key 15) unpaired, post-edge keys paired by offset +3 -> IDENTICAL, all reported",
                code == 0 and st["coalesced"] == 1 and st["unpaired"] == 1 and st["scene_pair"] == "scene-paired: offset +3 from old_b10=new_b13"
                and any("frames.fhash: scene-paired: offset +3 from old_b10=new_b13 -- A keys from 15 pair with B keys from 14; keys below 14 by key" == l for l in rep)
                and any(l.startswith("frames.fhash: keys 14 of A are unpaired (1; its keys between the lower edge and its own edge 15") for l in rep)
                and any("identical by scene-edge pairing (16 keys paired, coalesced=1, last keys 18 / 17)" in l for l in rep)
                and verdict(code, ["frames"], coalesced=1, unpaired=st["unpaired"], scene_pair=st["scene_pair"])
                == "diff_legs: IDENTICAL (lanes=frames, masked 0 B, coalesced 1, unpaired 1, scene-paired: offset +3 from old_b10=new_b13)")
    st = {}
    code, rep = diff_legs(SO, SN, cross_engine=True, lanes=["audio"], stats=st)
    ok &= check("scene-edge rule, audio: windows from OLD 1 / NEW 2 pair by offset +1 (NEW's window 1 unpaired); identical; the edge residual noted",
                code == 0 and st["unpaired"] == 1 and any("audio.ahash: identical by scene-edge pairing (4 keys paired" in l for l in rep)
                and any("NOTE the edges sit 4 and 1 boundaries into their windows (window offset +1, residual -3 boundary)" in l for l in rep))
    st = {}
    code, rep = diff_legs(SO, SN, cross_engine=True, lanes=["mem"], stats=st)
    ok &= check("scene-edge rule, mem: offset +3 off the 60 cadence -> post-edge lines UNPAIRED (named), pre-edge by key, lane not identical",
                code == 1 and any("offset +3 is not a multiple of the lane cadence 60: the 1 A + 1 B lines from the edges are UNPAIRED" in l for l in rep)
                and any("mem.mhash: pre-edge lines identical (1 lines); post-edge NOT PAIRED" in l for l in rep) and st["unpaired"] == 2)
    # PER RUN (P8.30-T): a run record whose edge is new_b12 (offset +2) re-pairs the frames: NEW keys from 13 (its b12 presents at
    # key 13) <-> OLD keys from 15; with the frozen +3 the lane was identical, with the run's +2 it diverges at the first post-edge key
    spec_so = leg_spec.read(SO)
    run_rec = scene_pairing.make(spec_so, 12, "EDGE-FOUND", "image-census", 40, "-")
    st = {}
    code, rep = diff_legs(SO, SN, cross_engine=True, lanes=["frames"], stats=st, pairing=run_rec)
    ok &= check("per-run pairing: the run's edge (new_b12, offset +2, deviation -1) replaces the frozen +3 on the verdict and the pairing",
                code == 1 and st["scene_pair"] == "scene-paired: offset +2 from old_b10=new_b12 (run; frozen reference +3, deviation -1)"
                and any("frames.fhash: scene-paired: offset +2 from old_b10=new_b12 (run; frozen reference +3, deviation -1) -- A keys from 15 pair with B keys from 13" in l for l in rep)
                and any("FIRST DIVERGENT frame at key 15 (paired by scene edge; B key 13)" in l for l in rep))
    not_found = scene_pairing.make(spec_so, None, "EDGE-NOT-FOUND", "image-census", 40, "-")
    st = {}
    code, rep = diff_legs(SO, SN, cross_engine=True, lanes=["frames"], stats=st, pairing=not_found)
    ok &= check("a run record with EDGE-NOT-FOUND leaves the frozen rule in force and names it",
                code == 0 and st["scene_pair"] == "scene-paired: offset +3 from old_b10=new_b13 (frozen map: the run's record reads EDGE-NOT-FOUND)")
    leg(SN, "new", [(1, "0xa"), (2, "0xb"), (3, "0xc"), (4, "0xd"), (5, "0xe"), (6, "0xf"), (7, "0xg"), (8, "0xh"), (9, "0xi"), (10, "0xj"),
                    (11, "0xk"), (12, "0xl"), (13, "0xm"), (14, "0xp"), (15, "0xQ"), (16, "0xr"), (17, "0xs")])
    code, rep = diff_legs(SO, SN, cross_engine=True, lanes=["frames"])
    ok &= check("scene-edge rule: a value difference one key after the edges is the FIRST DIVERGENT frame, both keys named",
                code == 1 and any("FIRST DIVERGENT frame at key 16 (paired by scene edge; B key 15):" in l for l in rep)
                and any(l == "  A: 16,0xq" for l in rep) and any(l == "  B: 15,0xQ" for l in rep))
    # same tree (NEW vs NEW) or no pair_map: the rule does not apply
    st = {}
    code, rep = diff_legs(SN, SN, lanes=["frames"], stats=st)
    ok &= check("same-tree pair: no scene-edge rule even with the LEG.tsv absent on both -> plain by-key identity",
                code == 0 and st["scene_pair"] == "" and not any("scene-paired" in l for l in rep))
    open(os.path.join(SO, leg_spec.FILE), "w").write("frames\tbad\n1\t2\n")
    code, rep = diff_legs(SO, SN, cross_engine=True, lanes=["frames"])
    ok &= check("a LEG.tsv that does not parse is a usage error (2) naming it", code == 2 and rep[0].startswith("LEG.tsv:"))
    os.remove(os.path.join(SO, leg_spec.FILE))
    print("diff_legs selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def verdict(code, lanes, masked=None, coalesced=0, unpaired=0, restore_point=0, rebased=(), scene_pair="",
            declared_selection=()):
    """The final line: the verdict word, the lane set it covers (so a META-only pass,
    'IDENTICAL (lanes=meta, ...)', cannot be read as lane identity), the masked byte
    count per lane -- `masked 0 B` for a plain run -- so a mask can never be silent,
    and the coalesced-present count the frames lane skipped (`coalesced 0` when none
    or when the lane did not run), so a skipped key can never be silent either.
    unpaired: the keys --fmv-pictures left unpaired at the windows' edges (window
    starts differing / a leg without a window); appended as `unpaired N` only when
    N > 0 so a plain run's line is unchanged and an unpaired key is never silent.
    restore_point: the key the restore-point rule (DP-26 (a)) removed from the value
    pairing (0 = none); appended as `restore-point present (OLD has none): key K`
    only when set, so the skipped key is on the verdict line itself.
    masked: None (no --mask-ranges) or {lane: bytes zeroed}.
    rebased: the manifest entries whose NEW line replaced a frozen lane line (DT-4);
    appended as `rebased: mem b120 (ROW-ID, ...)` so a re-based window is never silent.
    declared_selection: the Config keys that actually differed under
    --allow-set-difference; appended as `declared selection: k1,k2` so a toggle-pair
    verdict can never be read as a same-config identity."""
    word = {0: "IDENTICAL", 1: "DIVERGENT", 2: "META-GATE-FAIL"}[code]
    if masked is not None and code == 0 and "mem" in lanes:
        word = "IDENTICAL-UNDER-MASK"
    tail = ", unpaired %d" % unpaired if unpaired else ""
    if restore_point:
        tail += ", restore-point present (OLD has none): key %d" % restore_point
    if rebased:
        tail += ", " + rebaseline.report_text(rebased)
    if scene_pair:
        tail += ", " + scene_pair
    if declared_selection:
        tail += ", declared selection: " + ",".join(declared_selection)
    return "diff_legs: %s (lanes=%s, %s, coalesced %d%s)" % (word, ",".join(lanes),
                                                             masked_counts_text(masked), coalesced, tail)


def main():
    argv = [a for a in sys.argv[1:]]
    if "--selftest" in argv:
        return selftest()
    allow = "--allow-build-diff" in argv
    cross = "--cross-engine" in argv
    lanes_spec = None
    mask_path = None
    allow_sets = None
    if "--allow-set-difference" in argv:
        i = argv.index("--allow-set-difference")
        if i + 1 >= len(argv):
            print(__doc__)
            return 2
        allow_sets = [k for k in argv[i + 1].split(",") if k]
        del argv[i:i + 2]
        if not allow_sets:
            print("diff_legs: --allow-set-difference needs at least one Config key")
            return 2
        if cross:
            print("diff_legs: --allow-set-difference is a same-binary toggle-pair mode; it cannot be combined with --cross-engine")
            return 2
    if "--mask-ranges" in argv:
        i = argv.index("--mask-ranges")
        if i + 1 >= len(argv):
            print(__doc__)
            return 2
        mask_path = argv[i + 1]
        del argv[i:i + 2]
    if "--lanes" in argv:
        i = argv.index("--lanes")
        if i + 1 >= len(argv):
            print(__doc__)
            return 2
        lanes_spec = argv[i + 1]
        del argv[i:i + 2]
    pairing = None
    if "--pairing" in argv:
        i = argv.index("--pairing")
        if i + 1 >= len(argv):
            print(__doc__)
            return 2
        pp = argv[i + 1]
        del argv[i:i + 2]
        try:
            pairing = scene_pairing.read(pp if os.path.isdir(pp) else os.path.dirname(pp)) if (os.path.isdir(pp) or os.path.isfile(pp)) else None
        except ValueError as e:
            print("diff_legs: --pairing %s" % e)
            return 2
        if pairing is None:
            print("diff_legs: --pairing %s: no %s there" % (pp, scene_pairing.FILE))
            return 2
    fmv_manifest, fmv_legacy_window = None, None
    for opt in ("--fmv-pictures", "--fmv-window-legacy"):
        if opt in argv:
            i = argv.index(opt)
            if i + 1 >= len(argv):
                print(__doc__)
                return 2
            if opt == "--fmv-pictures":
                fmv_manifest = argv[i + 1]
            else:
                fmv_legacy_window = argv[i + 1]
            del argv[i:i + 2]
    fmv = None
    if fmv_manifest or fmv_legacy_window:
        if not fmv_manifest:
            print("diff_legs: --fmv-window-legacy needs --fmv-pictures MANIFEST")
            return 2
        try:
            fmv = parse_fmv_request(fmv_manifest, fmv_legacy_window)
        except ValueError as e:
            print("diff_legs: %s" % e)
            return 2
    argv = [a for a in argv if a not in ("--allow-build-diff", "--cross-engine")]
    if len(argv) != 2:
        print(__doc__)
        return 2
    try:
        lanes = parse_lanes(lanes_spec)
    except ValueError as e:
        print("diff_legs: %s" % e)
        return 2
    if mask_path:
        try:
            read_mask(mask_path)          # a rejected mask is a usage error, before any compare
        except (OSError, MaskError) as e:
            print("diff_legs: mask %s" % e)
            return 2
    stats = {}
    code, rep = diff_legs(argv[0], argv[1], allow, cross_engine=cross, lanes=lanes,
                          mask_ranges=mask_path, stats=stats, fmv=fmv, pairing=pairing,
                          allow_set_difference=allow_sets)
    for l in rep:
        print(l)
    print(verdict(code, lanes, masked=stats.get("masked") if mask_path else None,
                  coalesced=stats.get("coalesced", 0), unpaired=stats.get("unpaired", 0),
                  restore_point=stats.get("restore_point", 0), rebased=stats.get("rebased", ()),
                  scene_pair=stats.get("scene_pair", ""),
                  declared_selection=stats.get("declared_selection", ())))
    return code


if __name__ == "__main__":
    sys.exit(main())
