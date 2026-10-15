#!/usr/bin/env python3
"""leg_spec.py -- the per-leg run contract `games/<title>/verify/baseline/<leg>/LEG.tsv` (DECISIONS_LOG DT-5).

The twelve original legs carry no LEG.tsv: their frame count is derived from the frozen
`frames.fhash` (battery.leg_frames), their wall bound is the caller's `--timeout` (or
run_leg.py's default) and their stall watchdog is run_leg.py's (`RB_STALL_S` 60 s NEW /
300 s OLD); their probe boundaries are probe_images.py's built-in plans (anchors b2/b60,
coldboot b60/b120). A leg that needs more than that -- the 13th leg `coldboot_attract`
runs thousands of frames (its LEG.tsv `frames` is the count) through two boot FMVs, the
Beach hub load, the attract movie and the hub reload after it, all progress-less for the
stall watchdog -- declares it HERE, and every runner reads it:

  <BASELINE_DIR>/<leg>/LEG.tsv         tab-separated, `#` comments, ONE header row + ONE value row:
      frames  state_every  timeout_s  stall_s  anchor  probes  notes
        frames       the leg's length: OLD `--bench-frames` (vsync frames) = NEW `--frames`
        state_every  the `--state-every` cadence of the NEW probe leg (probe_images.py runs the
                     leg ONCE at this cadence and keeps the probe boundaries' images); `0` = no
                     images. The OLD freeze rep's own image cadence is FREEZE.md's record
        timeout_s    the wall bound passed as `run_leg.py --timeout` (a caller's `--timeout`
                     never lowers it: the effective bound is the larger of the two)
        stall_s      the progress watchdog (`RB_STALL_S`) for the leg's runs, OLD and NEW
        anchor       `coldboot` (a cold boot of the title) or the anchor savestate, repo-relative
                     (`save_states/<name>.ps2st`); without a LEG.tsv the leg NAME decides
                     (`coldboot` boots, anything else is `save_states/<leg>.ps2st`)
        probes       comma list of the probe boundaries `B[@F]`: B = the boundary of the probe
                     pair (`probe/old_b<B>.ps2st` vs `new_b<B>.ps2st`; a multiple of the mem
                     lane's 60 so the OLD image is proved by its `mem.mhash` line, and a multiple
                     of `state_every` so NEW's single `--state-every` leg lands an image there);
                     `@F` = the vsync-frame count the OLD probe run needs to pass boundary B+1
                     (OLD keys its run length by vsync frames, which run ahead of its EE
                     boundaries once the FMVs coalesced presents; default = `frames`)
        notes        free text (the measured timeline the numbers come from)
      Two OPTIONAL trailing columns (DECISIONS_LOG DT-6, P8.25-T; absent = no scene pairing):
        pair_map     `old_b<B>=new_b<N>,...`: the SCENE-ALIGNED pairing of this leg's OLD boundaries with
                     NEW boundaries (verify/clock_pair.py: the boot-SM case-7 entry edge on each side, then
                     every probe at edge + its OLD distance). The FIRST token is the edge pair (not a probe);
                     every probe boundary B of `probes` has a token; the NEW boundary N need not lie on the
                     `state_every` cadence (probe_images.py runs a dedicated leg for it). score.py compares
                     `probe/old_b<B>.ps2st` with `new_b<N>.ps2st` and reports the offset on the pair;
                     diff_legs.py pairs the frames/audio/mem lane windows from the edge by the same offset,
                     reported on every verdict line. `-` or absent = pairing by boundary index (the twelve).
                     SEMANTICS (P8.30-T): the map is the REFERENCE measured once; a gate derives the run's own NEW
                     edge (verify/scene_pairing.py, PAIRING.tsv next to the probe images) and every consumer
                     reports the deviation from this reference -- the frozen tokens are never silently applied
                     once a run record exists.
        vf_offset    OLD's `vsync_frame - boundary` constant once its boot FMVs coalesced (coldboot_attract:
                     530; FREEZE.md) -- the frames lane keys OLD lines by vsync frame, so the edge's OLD
                     frames key = edge boundary + vf_offset. Required with pair_map.
      Two further OPTIONAL trailing columns (a `source=new` leg frozen from a NEW-recorded play session,
      docs/design/PLAY_SESSIONS.md; they follow pair_map/vf_offset, which such a leg writes as `-`/empty):
        pad_script   the replayed input script, TITLE-relative (`verify/pads/<leg>.padlog`): every rep passes
                     it as `run_leg.py --inputs`; `-` = the harness's neutral pad (the thirteen).
        mc_dir       the persistent game-save directory the leg starts from, LEG-relative (`mc_before`): each
                     rep gets a fresh copy as `--set paths.mc_dir=<copy>` so a save written by the replay never
                     leaks into the next rep or into the frozen tree; `-` = the binary's default directory.

Absent file = None everywhere: every caller keeps today's behaviour for the twelve. A file
that is present but malformed is a usage error (LegSpecError), never a silent default.
"""
import os

# The frozen baselines are TITLE DATA under `games/<title>/verify/baseline/` (CONSTRUCTION_PLAN 4.8; moved
# there from verify/baseline/ by path, byte-identical); every tool under verify/ defaults its `--baseline` to it.
# The harness names no title (LintEnginePurity): the title directory is the ONE `games/*/verify/baseline`
# the tree has; several or none is an error at import.
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _title_baseline_dir():
    cands = sorted(d for d in __import__("glob").glob(os.path.join(REPO_ROOT, "games", "*", "verify", "baseline"))
                   if os.path.isdir(d))
    if len(cands) != 1:
        raise SystemExit("leg_spec: expected exactly one games/<title>/verify/baseline, found %d: %s" % (len(cands), cands))
    return cands[0]


BASELINE_DIR = _title_baseline_dir()
TITLE_DIR = os.path.dirname(os.path.dirname(BASELINE_DIR))

FILE = "LEG.tsv"
COLUMNS = ("frames", "state_every", "timeout_s", "stall_s", "anchor", "probes", "notes")
OPTIONAL_COLUMNS = ("pair_map", "vf_offset")
SESSION_COLUMNS = ("pad_script", "mc_dir")
INT_COLUMNS = ("frames", "state_every", "timeout_s", "stall_s")
PAIR_RE = __import__("re").compile(r"^old_b(\d+)=new_b(\d+)$")


class LegSpecError(ValueError):
    """A LEG.tsv that must not be used: missing column, non-integer field, bad probe token."""


def parse_probes(text, default_frames):
    """`900@1600,3000,4200@4900` -> [(900, 1600), (3000, default_frames), (4200, 4900)]; `-` or '' -> []."""
    text = (text or "").strip()
    if text in ("", "-"):
        return []
    out = []
    for tok in text.split(","):
        tok = tok.strip()
        if not tok:
            continue
        b, _, f = tok.partition("@")
        if not b.isdigit() or (f and not f.isdigit()):
            raise LegSpecError("probe token %r is not B[@F]" % tok)
        out.append((int(b), int(f) if f else int(default_frames)))
    return out


def parse_pair_map(text):
    """`old_b850=new_b1128,old_b1500=new_b1778` -> (edge (850, 1128), {1500: 1778, ...}); `-` / '' -> (None, {}).
    The first token is the edge pair; a duplicate OLD boundary or a malformed token is a LegSpecError."""
    text = (text or "").strip()
    if text in ("", "-"):
        return None, {}
    edge, mapping = None, {}
    for tok in text.split(","):
        tok = tok.strip()
        if not tok:
            continue
        m = PAIR_RE.match(tok)
        if not m:
            raise LegSpecError("pair_map token %r is not old_b<B>=new_b<N>" % tok)
        b, n = int(m.group(1)), int(m.group(2))
        if edge is None:
            edge = (b, n)
            continue
        if b in mapping or b == edge[0]:
            raise LegSpecError("pair_map names old_b%d twice" % b)
        mapping[b] = n
    return edge, mapping


def parse(text):
    """The LEG.tsv text -> dict (ints for the INT_COLUMNS, `probes` as [(B, F)], `notes` str; `pair_edge`
    (old_b, new_b) | None, `pair_map` {old_b: new_b}, `vf_offset` int | None from the optional columns)."""
    rows = [l.rstrip("\n") for l in text.splitlines() if l.strip() and not l.lstrip().startswith("#")]
    if len(rows) != 2:
        raise LegSpecError("%s needs exactly one header row and one value row, got %d rows" % (FILE, len(rows)))
    header = [c.strip() for c in rows[0].split("\t")]
    values = rows[1].split("\t")
    columns = list(COLUMNS)
    if header == list(COLUMNS) + list(OPTIONAL_COLUMNS) + list(SESSION_COLUMNS):
        columns = list(COLUMNS) + list(OPTIONAL_COLUMNS) + list(SESSION_COLUMNS)
    elif header == list(COLUMNS) + list(OPTIONAL_COLUMNS):
        columns = list(COLUMNS) + list(OPTIONAL_COLUMNS)
    elif header != list(COLUMNS):
        raise LegSpecError("%s header must be %s (optionally + %s [+ %s]), got %s"
                           % (FILE, "\t".join(COLUMNS), "\t".join(OPTIONAL_COLUMNS), "\t".join(SESSION_COLUMNS),
                              "\t".join(header)))
    if len(values) < len(COLUMNS) - 1:
        raise LegSpecError("%s value row has %d fields, needs %d" % (FILE, len(values), len(COLUMNS)))
    values += [""] * (len(columns) - len(values))
    spec = {"pair_map": {}, "pair_edge": None, "vf_offset": None, "pad_script": None, "mc_dir": None}
    for name, v in zip(columns, values):
        v = v.strip()
        if name in INT_COLUMNS:
            if not v.isdigit():
                raise LegSpecError("%s field %s must be a non-negative integer, got %r" % (FILE, name, v))
            spec[name] = int(v)
        else:
            spec[name] = v
    if spec["frames"] <= 0:
        raise LegSpecError("%s frames must be > 0" % FILE)
    if not spec["anchor"] or spec["anchor"] == "-":
        raise LegSpecError("%s anchor must be `coldboot` or a repo-relative .ps2st path" % FILE)
    spec["probes"] = parse_probes(spec["probes"], spec["frames"])
    for b, _ in spec["probes"]:
        if spec["state_every"] and b % spec["state_every"] != 0:
            raise LegSpecError("probe boundary %d is not a multiple of state_every %d" % (b, spec["state_every"]))
    if "pair_map" in columns:
        spec["pair_edge"], spec["pair_map"] = parse_pair_map(spec["pair_map"])
        vo = (spec.get("vf_offset") or "").strip()
        if spec["pair_edge"] is not None:
            if not vo.isdigit():
                raise LegSpecError("%s vf_offset must be a non-negative integer when pair_map is set, got %r" % (FILE, vo))
            spec["vf_offset"] = int(vo)
            missing = [b for b, _ in spec["probes"] if b not in spec["pair_map"]]
            if missing:
                raise LegSpecError("pair_map lacks the probe boundaries %s" % ",".join(str(b) for b in missing))
            extra = [b for b in spec["pair_map"] if b not in dict(spec["probes"])]
            if extra:
                raise LegSpecError("pair_map names non-probe boundaries %s (only the first token is the edge)" % ",".join(str(b) for b in extra))
        else:
            spec["vf_offset"] = int(vo) if vo.isdigit() else None
    if "pad_script" in columns:
        for name in SESSION_COLUMNS:
            v = (spec.get(name) or "").strip()
            if v in ("", "-"):
                spec[name] = None
                continue
            if os.path.isabs(v) or v.startswith(".") or "\\" in v:
                raise LegSpecError("%s %s must be a relative path without `.` components, got %r" % (FILE, name, v))
            spec[name] = v
        if spec["pad_script"] is not None and not spec["pad_script"].endswith(".padlog"):
            raise LegSpecError("%s pad_script must name a .padlog, got %r" % (FILE, spec["pad_script"]))
    else:
        spec["pad_script"] = spec["mc_dir"] = None
    return spec


def mapped_boundary(spec, b):
    """The NEW boundary paired with OLD probe boundary b: the spec's pair_map entry, else b itself."""
    if spec is None:
        return b
    return spec.get("pair_map", {}).get(b, b)


def read(leg_dir):
    """The leg's spec dict, or None when the leg carries no LEG.tsv."""
    path = os.path.join(leg_dir, FILE)
    if not os.path.isfile(path):
        return None
    with open(path) as f:
        return parse(f.read())


def read_leg(baseline_dir, leg):
    return read(os.path.join(baseline_dir, leg))


def effective_timeout(spec, requested):
    """The wall bound for one leg: the larger of the caller's `--timeout` (None = unset) and the
    spec's `timeout_s` (None spec = the caller's value as is)."""
    if spec is None or not spec.get("timeout_s"):
        return requested
    if requested is None:
        return spec["timeout_s"]
    return max(int(requested), spec["timeout_s"])


def stall_s(spec):
    """The spec's stall watchdog in seconds, or None (= the runner's default)."""
    if spec is None or not spec.get("stall_s"):
        return None
    return spec["stall_s"]


def pad_script_for(spec, title_dir):
    """The absolute padlog the leg replays (`run_leg.py --inputs`), or None for the neutral pad."""
    if spec is None or not spec.get("pad_script"):
        return None
    return os.path.join(title_dir, spec["pad_script"])


def mc_dir_for(spec, leg_dir):
    """The absolute frozen game-save directory the leg starts from, or None (the binary's default)."""
    if spec is None or not spec.get("mc_dir"):
        return None
    return os.path.join(leg_dir, spec["mc_dir"])


def anchor_for(spec, leg, repo, anchor_dir="save_states"):
    """The leg's anchor argument for run_leg.py: `coldboot`, or the absolute path of its savestate.
    From the spec's `anchor` column when the leg has one; else by the leg's name (the twelve)."""
    name = spec["anchor"] if spec is not None else ("coldboot" if leg == "coldboot" else os.path.join(anchor_dir, leg + ".ps2st"))
    if name == "coldboot":
        return "coldboot"
    return name if os.path.isabs(name) else os.path.join(repo, name)


def format_spec(frames, state_every, timeout_s, stall_s_, anchor, probes, notes, pair_map=None, vf_offset=None,
                pad_script=None, mc_dir=None):
    """The file text for a spec (the freeze protocol writes it with this; `probes` = [(B, F)]; `pair_map` =
    the `old_b<B>=new_b<N>,...` text (edge first) with `vf_offset` -> the two optional columns; `pad_script` /
    `mc_dir` -> the two session columns, written with `-` pair_map/vf_offset placeholders when no pairing)."""
    ptxt = ",".join("%d@%d" % (b, f) if f != frames else "%d" % b for b, f in probes) or "-"
    cols, tail = list(COLUMNS), ""
    if pair_map:
        cols += list(OPTIONAL_COLUMNS)
        tail = "\t%s\t%s" % (pair_map, vf_offset)
    if pad_script or mc_dir:
        if not pair_map:
            cols += list(OPTIONAL_COLUMNS)
            tail = "\t-\t"
        cols += list(SESSION_COLUMNS)
        tail += "\t%s\t%s" % (pad_script or "-", mc_dir or "-")
    return ("# games/<title>/verify/baseline/<leg>/LEG.tsv -- the leg's run contract (verify/leg_spec.py)\n"
            "%s\n%d\t%d\t%d\t%d\t%s\t%s\t%s%s\n" % ("\t".join(cols), frames, state_every, timeout_s, stall_s_, anchor, ptxt,
                                                  notes.replace("\t", " ").replace("\n", " "), tail))


def selftest():
    import shutil
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)

    tmp = tempfile.mkdtemp(prefix="leg_spec_")
    leg = os.path.join(tmp, "coldboot")
    os.makedirs(leg)
    check(read(leg) is None, "no LEG.tsv -> None (the twelve legs keep today's behaviour)")
    check(effective_timeout(None, 900) == 900 and effective_timeout(None, None) is None and stall_s(None) is None,
          "None spec passes the caller's timeout through and names no stall")
    text = format_spec(9000, 300, 3000, 600, "coldboot", [(900, 1500), (3000, 9000), (6000, 6700)], "title at vf 1380; attract A..E")
    with open(os.path.join(leg, FILE), "w") as f:
        f.write(text)
    s = read(leg)
    check(s["frames"] == 9000 and s["state_every"] == 300 and s["timeout_s"] == 3000 and s["stall_s"] == 600,
          "the four integer columns read back -> %r" % {k: s[k] for k in INT_COLUMNS})
    check(s["probes"] == [(900, 1500), (3000, 9000), (6000, 6700)],
          "probes: B@F tokens, a bare B defaults F to frames -> %r" % s["probes"])
    check(s["notes"].startswith("title at vf 1380"), "notes kept")
    check(s["anchor"] == "coldboot" and anchor_for(s, "coldboot_attract", "/repo") == "coldboot",
          "anchor column: coldboot -> the run_leg.py `coldboot` anchor whatever the leg's name")
    check(anchor_for(None, "coldboot", "/repo") == "coldboot" and anchor_for(None, "artdet_f37152", "/repo") == "/repo/save_states/artdet_f37152.ps2st",
          "no spec: the leg's name decides (coldboot boots; anchors are save_states/<leg>.ps2st)")
    s_a = parse("\t".join(COLUMNS) + "\n600\t0\t0\t0\tsave_states/x_f1.ps2st\t-\t\n")
    check(anchor_for(s_a, "whatever", "/repo") == "/repo/save_states/x_f1.ps2st", "a repo-relative anchor resolves under the repo")
    check(effective_timeout(s, 900) == 3000 and effective_timeout(s, 4000) == 4000 and effective_timeout(s, None) == 3000,
          "effective_timeout = max(caller, spec); None caller -> spec")
    check(stall_s(s) == 600, "stall_s from the spec")
    check(read_leg(tmp, "coldboot") == s, "read_leg(baseline, leg) is read(<baseline>/<leg>)")
    # malformed files are errors, never defaults
    bad = []
    for body in ("frames\tstate_every\n9000\t300\n",
                 "\t".join(COLUMNS) + "\nabc\t300\t3000\t600\tcoldboot\t-\tx\n",
                 "\t".join(COLUMNS) + "\n9000\t300\t3000\t600\tcoldboot\t900@x\tx\n",
                 "\t".join(COLUMNS) + "\n9000\t300\t3000\t600\tcoldboot\t901\tx\n",
                 "\t".join(COLUMNS) + "\n0\t300\t3000\t600\tcoldboot\t-\tx\n",
                 "\t".join(COLUMNS) + "\n9000\t300\t3000\t600\t-\t-\tx\n",
                 "\t".join(COLUMNS) + "\n9000\t300\t3000\t600\tcoldboot\t-\tx\n9000\t300\t3000\t600\tcoldboot\t-\ty\n"):
        try:
            parse(body)
            bad.append(False)
        except LegSpecError:
            bad.append(True)
    check(all(bad) and len(bad) == 7, "bad header / non-integer / bad probe token / probe off the cadence / frames 0 / no anchor / two rows -> LegSpecError")
    s0 = parse("\t".join(COLUMNS) + "\n600\t0\t0\t0\tcoldboot\t-\t\n")
    check(s0["probes"] == [] and effective_timeout(s0, 900) == 900 and stall_s(s0) is None,
          "zero columns mean 'unset': no probes, caller's timeout, runner's stall")
    s1 = parse("\t".join(COLUMNS) + "\n600\t0\t0\t0\tcoldboot\t60,120\t\n")
    check(s1["probes"] == [(60, 600), (120, 600)], "state_every 0 imposes no cadence on the probes")
    check(parse_probes("-", 5) == [] and parse_probes("", 5) == [], "`-` / empty probes -> []")
    # the optional pair_map / vf_offset columns (DT-6): absent -> no pairing; present -> edge + per-probe map
    check(s["pair_map"] == {} and s["pair_edge"] is None and s["vf_offset"] is None and mapped_boundary(s, 900) == 900
          and mapped_boundary(None, 900) == 900, "no pair_map: empty map, no edge, mapped_boundary is the identity")
    text_p = format_spec(6000, 60, 3000, 600, "coldboot", [(1500, 2100), (2700, 3300)], "n",
                         pair_map="old_b850=new_b1128,old_b1500=new_b1778,old_b2700=new_b2978", vf_offset=530)
    sp = parse(text_p)
    check(sp["pair_edge"] == (850, 1128) and sp["pair_map"] == {1500: 1778, 2700: 2978} and sp["vf_offset"] == 530
          and mapped_boundary(sp, 1500) == 1778 and mapped_boundary(sp, 2700) == 2978,
          "pair_map parses: the first token is the edge, the rest the probes; vf_offset read -> %r %r" % (sp["pair_edge"], sp["pair_map"]))
    check(text_p.splitlines()[1] == "\t".join(COLUMNS + OPTIONAL_COLUMNS), "format_spec writes the two optional columns when a pair_map is given")
    bad_p = []
    for pm, vo in (("old_b850=new_b1128,old_b1500=new_b1778", "530"),          # a probe (2700) lacks a token
                   ("old_b850=new_b1128,old_b1500=new_b1778,old_b2700=new_b2978", ""),   # no vf_offset
                   ("old_b850=new_b1128,old_b1500=new_b1778,old_b2700=new_b2978,old_b3000=new_b3100", "530"),  # a non-probe
                   ("old_b850=new_b1128,old_b1500=new_b1778,old_b1500=new_b1779,old_b2700=new_b2978", "530"),  # duplicate
                   ("850=1128,old_b1500=new_b1778,old_b2700=new_b2978", "530")):   # malformed token
        try:
            parse(format_spec(6000, 60, 3000, 600, "coldboot", [(1500, 2100), (2700, 3300)], "n", pair_map=pm, vf_offset=vo))
            bad_p.append(False)
        except LegSpecError:
            bad_p.append(True)
    check(all(bad_p) and len(bad_p) == 5, "pair_map lacking a probe / no vf_offset / a non-probe / a duplicate / a malformed token -> LegSpecError")
    s_dash = parse("\t".join(COLUMNS + OPTIONAL_COLUMNS) + "\n600\t0\t0\t0\tcoldboot\t60\tx\t-\t\n")
    check(s_dash["pair_map"] == {} and s_dash["pair_edge"] is None, "pair_map `-` with the optional header = no pairing")
    check(s_dash["pad_script"] is None and s_dash["mc_dir"] is None and pad_script_for(s_dash, "/t") is None
          and pad_script_for(None, "/t") is None and mc_dir_for(s, "/l") is None,
          "no session columns: neutral pad, default save directory")
    # the session columns of a `source=new` leg (PLAY_SESSIONS.md): pad_script title-relative, mc_dir leg-relative
    text_s = format_spec(36000, 0, 4000, 600, "coldboot", [], "user session 1", pad_script="verify/pads/play_session_1.padlog",
                         mc_dir="mc_before")
    ss = parse(text_s)
    check(text_s.splitlines()[1] == "\t".join(COLUMNS + OPTIONAL_COLUMNS + SESSION_COLUMNS) and ss["pair_map"] == {}
          and ss["pad_script"] == "verify/pads/play_session_1.padlog" and ss["mc_dir"] == "mc_before"
          and pad_script_for(ss, "/t") == "/t/verify/pads/play_session_1.padlog" and mc_dir_for(ss, "/b/leg") == "/b/leg/mc_before",
          "session columns: written with `-` pairing placeholders, read back, resolved against title / leg dirs")
    ss_only = parse(format_spec(600, 0, 0, 0, "coldboot", [], "n", pad_script="verify/pads/x.padlog"))
    check(ss_only["pad_script"] == "verify/pads/x.padlog" and ss_only["mc_dir"] is None, "pad_script alone: mc_dir `-` -> None")
    bad_s = []
    for ps, md in (("/abs/x.padlog", "-"), ("verify/pads/x.txt", "-"), ("-", "../escape"), ("./x.padlog", "-")):
        try:
            parse(format_spec(600, 0, 0, 0, "coldboot", [], "n", pad_script=ps, mc_dir=md))
            bad_s.append(False)
        except LegSpecError:
            bad_s.append(True)
    check(all(bad_s) and len(bad_s) == 4, "absolute / non-.padlog / dot-escaping session paths -> LegSpecError")
    shutil.rmtree(tmp, ignore_errors=True)
    print("leg_spec selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    import sys
    sys.exit(selftest())
