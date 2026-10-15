#!/usr/bin/env python3
"""clock_pair.py -- scene-aligned pairing of a leg's OLD probe images with NEW boundaries by the
title's own clock (DECISIONS_LOG DT-6; the `coldboot_attract` leg).

  clock_pair.py --keys <leg>/SCENE_KEYS.tsv --read <image.ps2st> ...
  clock_pair.py --keys ... --census <leg dir> [--out census.tsv]
  clock_pair.py --keys ... --pair --old-probes <probe dir> --new-census <leg dir|census.tsv>
                --edges old_b<B>=new_b<N>[,old_b<B2>=new_b<N2>...] [--new-images <dir>]
  clock_pair.py --selftest

The guest addresses of the two words are TITLE DATA and live next to the leg
(`games/<title>/verify/baseline/<leg>/SCENE_KEYS.tsv`: `clock_word <addr>` / `boot_sm_word <addr>`, `--keys`);
this tool holds none (the verify/ tree is title-neutral, LintEnginePurity).
The two words every image carries (RAM0 of the PS2SAVE1 envelope, verify/state_hash.py):
  * the title clock word (u32): the title's tick accumulator. Measured law on BOTH trees
    while the title runs (census of this stage): +149 per EE boundary, plus a periodic +2048
    (every ~500-600 boundaries) and +4 across the attract movie / hub reload. During the boot
    FMVs and the Beach load the word runs at each tree's own pace (NEW: +1136..+1828 per
    boundary inside the FMVs; OLD: ~+540 per vsync frame through its wall-pumped boot, the
    DP-17 class), so its VALUE at a scene moment is tree-specific (OLD's title-entry value is
    ~1.0 M, NEW's ~0.36 M): equal clock VALUES never meet inside one leg -- `--pair` still
    tries the exact match and reports `exact-clock match: none (NEW range lo..hi)` honestly.
  * the boot state machine word (u64): case = bits 44..49, bit 63 = the in-engine
    cutscene flag (DECISIONS_LOG HW-1). Case 6 -> 7 is the Beach hub load's completion = the
    title's entry (cutscene pass 1 starts); the same edge after the attract movie's reload
    starts pass 2.

The pairing key (the second key the DT-6 text allows for): the SCENE EDGE plus the clock's
advance since it. The title's scene timers count its ticks from the case-7 entry (the attract
idle fires 2939 boundaries after it on both trees), so an OLD probe at boundary B pairs with
the NEW boundary N = B + (new_edge - old_edge) of the SAME edge (the last edge at or before B
on the OLD side). --edges names the edges as `old_b<B>=new_b<N>` pairs (OLD's from its
`[BOOT-SM-EDGE]` log + the leg's vf - boundary relation; NEW's from a `--state-every 1`
census around the 60-cadence bracket). Every pair reports: the offset, the OLD and NEW
clock values, NEW's clock residual against the 149/boundary law from the edge's nearest
census image (`new_clock_residual_vs_law`: the 2048-jump phase, REPORTED, never tolerated
silently; OLD's advance since its edge is not computed here -- OLD's edge is a log line, its
edge image is recorded in the leg's FREEZE.md), and the scene words of both images (case +
bit 63 must agree: a SCENE-MISMATCH pair is an error, exit 1). The result is the `pair_map`
column of the leg's LEG.tsv
(`old_b<B>=new_b<N>,...`, the edge first), consumed by probe_images.py / score.py /
diff_legs.py.
"""
import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import state_hash  # noqa: E402
import memory_capture  # noqa: E402

KEY_NAMES = ("clock_word", "boot_sm_word")   # the SCENE_KEYS.tsv rows: the title's tick word (u32), the boot-SM word (u64)
TICKS_PER_BOUNDARY = 149   # the measured steady-state advance of the clock word per EE boundary (both trees)
CENSUS_COLUMNS = ("boundary", "vsync_frame", "clock", "bootsm", "case", "bit63")


def read_keys(path):
    """{'clock_word': addr, 'boot_sm_word': addr} from a SCENE_KEYS.tsv (`<key>\t<hex addr>\t<what>`; `#` comments).
    A missing key or a malformed address is a ValueError naming the file."""
    keys = {}
    with open(path) as f:
        for ln, raw in enumerate(f, 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split("\t")
            if len(parts) < 2 or parts[0] not in KEY_NAMES:
                raise ValueError("%s:%d: expected `<%s>\t<addr>\t<what>`" % (path, ln, "|".join(KEY_NAMES)))
            try:
                keys[parts[0]] = int(parts[1], 16)
            except ValueError:
                raise ValueError("%s:%d: address %r is not hex" % (path, ln, parts[1]))
    missing = [k for k in KEY_NAMES if k not in keys]
    if missing:
        raise ValueError("%s: missing keys %s" % (path, ",".join(missing)))
    return keys


def words(path, keys):
    """Scene words from a runtime save or a NEW boundary memory capture."""
    raw = re.fullmatch(r"memory_b(\d+)\.bin", os.path.basename(path))
    if raw:
        with open(path, "rb") as source:
            ram = source.read()
        if len(ram) != 32 * 1024 * 1024:
            raise ValueError("incomplete memory capture: %s" % path)
        vf = int(raw.group(1))
    else:
        _, vf, sections = state_hash.parse(path)
        ram = sections[state_hash.TAG_RAM][2]
    clock = struct.unpack_from("<I", ram, keys["clock_word"])[0]
    sm = struct.unpack_from("<Q", ram, keys["boot_sm_word"])[0]
    return decode(vf, clock, sm)


def decode(vf, clock, sm):
    return {"vsync_frame": vf, "clock": clock, "bootsm": sm, "case": (sm >> 44) & 0x3f, "bit63": sm >> 63}


def scene_text(w):
    return "case %d bit63 %d" % (w["case"], w["bit63"])


def census(leg_dir, keys):
    """Scene census from NEW diagnostic captures or older runtime snapshots."""
    return {boundary: words(memory_capture.path(leg_dir, boundary), keys)
            for boundary in memory_capture.boundaries(leg_dir)}


def census_lines(c):
    lines = ["\t".join(CENSUS_COLUMNS)]
    for b in sorted(c):
        w = c[b]
        lines.append("%d\t%d\t%d\t0x%016x\t%d\t%d" % (b, w["vsync_frame"], w["clock"], w["bootsm"], w["case"], w["bit63"]))
    return lines


def read_census(path):
    """A census.tsv back into {boundary: words}."""
    out = {}
    with open(path) as f:
        header = None
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if header is None:
                header = parts
                if tuple(header) != CENSUS_COLUMNS:
                    raise ValueError("%s: header %r is not %r" % (path, header, CENSUS_COLUMNS))
                continue
            b, vf, clock, sm = int(parts[0]), int(parts[1]), int(parts[2]), int(parts[3], 16)
            out[b] = decode(vf, clock, sm)
    return out


def load_census(spec, keys):
    return read_census(spec) if os.path.isfile(spec) else census(spec, keys)


EDGE_RE = re.compile(r"^old_b(\d+)=new_b(\d+)$")


def parse_edges(text):
    """`old_b850=new_b1128,old_b3833=new_b4111` -> [(850, 1128), (3833, 4111)] sorted by the OLD boundary."""
    out = []
    for tok in (text or "").split(","):
        tok = tok.strip()
        if not tok:
            continue
        m = EDGE_RE.match(tok)
        if not m:
            raise ValueError("edge token %r is not old_b<B>=new_b<N>" % tok)
        out.append((int(m.group(1)), int(m.group(2))))
    if not out:
        raise ValueError("no edges given")
    return sorted(out)


def edge_for(b, edges):
    """The last (old_edge, new_edge) with old_edge <= b, or None when b precedes every edge."""
    chosen = None
    for oe, ne in edges:
        if oe <= b:
            chosen = (oe, ne)
    return chosen


def nearest_census(c, b):
    """The census boundary nearest to b (ties -> the lower), or None."""
    if not c:
        return None
    return min(c, key=lambda k: (abs(k - b), k))


def expected_clock(c, b):
    """The clock the 149/boundary law predicts at boundary b from the nearest census image
    (None without a census): census clock + 149 * (b - census boundary). The periodic +2048
    jumps and the attract's +4 are not modelled: a residual of that size is the jump phase."""
    k = nearest_census(c, b)
    if k is None:
        return None, None
    return c[k]["clock"] + TICKS_PER_BOUNDARY * (b - k), k


def pair(old_probes, new_census, edges, keys, new_images=None):
    """[{old_b, new_b, offset, edge, old_clock, new_clock|None, exact, new_clock_residual_vs_law, ...}] for every
    `old_b<B>.ps2st` in old_probes. Exact-clock match = a census boundary whose clock equals the OLD image's
    (never expected inside one leg; reported). new_images: a dir holding `new_b<N>.ps2st` (the mapped images,
    probe_images.py's output) -- when present the pair's NEW words come from the image itself and the scene
    words are checked; else the NEW scene word comes from the nearest census image (reported as such)."""
    rows = []
    probes = []
    for name in os.listdir(old_probes):
        m = re.match(r"^old_b(\d+)\.ps2st$", name)
        if m:
            probes.append((int(m.group(1)), os.path.join(old_probes, name)))
    clocks = {b: w["clock"] for b, w in new_census.items()}
    lo, hi = (min(clocks.values()), max(clocks.values())) if clocks else (None, None)
    for b, path in sorted(probes):
        wo = words(path, keys)
        exact = sorted(k for k, v in clocks.items() if v == wo["clock"])
        e = edge_for(b, edges)
        row = {"old_b": b, "old_vsync_frame": wo["vsync_frame"], "old_clock": wo["clock"], "old_scene": scene_text(wo),
               "exact_match": exact, "new_range": (lo, hi)}
        if e is None:
            row.update({"new_b": b, "offset": 0, "edge": None, "rule": "before the first edge: paired by boundary index"})
        else:
            oe, ne = e
            row.update({"new_b": b + (ne - oe), "offset": ne - oe, "edge": e,
                        "rule": "scene edge old_b%d=new_b%d (case-7 entry), offset %+d" % (oe, ne, ne - oe)})
        n = row["new_b"]
        img = os.path.join(new_images, "new_b%d.ps2st" % n) if new_images else None
        if img and os.path.isfile(img):
            wn = words(img, keys)
            if wn["vsync_frame"] != n:
                raise ValueError("%s: header vsync_frame %d != boundary %d" % (img, wn["vsync_frame"], n))
            row.update({"new_clock": wn["clock"], "new_scene": scene_text(wn), "new_source": "image"})
            row["scene_ok"] = (wn["case"], wn["bit63"]) == (wo["case"], wo["bit63"])
        else:
            pred, k = expected_clock(new_census, n)
            row.update({"new_clock": pred, "new_source": "census b%s + 149/boundary" % k if k is not None else "none"})
            if k is not None:
                wk = new_census[k]
                row["new_scene"] = scene_text(wk) + " (census b%d)" % k
                row["scene_ok"] = (wk["case"], wk["bit63"]) == (wo["case"], wo["bit63"])
            else:
                row["new_scene"], row["scene_ok"] = "none", None
        # the clock's advance since the edge: OLD from its own images is not available here (the edge image is
        # OLD's log line), so the comparable figure is the boundary distance; the NEW side's advance from the
        # census anchor is what the law predicts -- the per-pair residual against 149/boundary is reported.
        if row["new_clock"] is not None and e is not None:
            pred_from_edge = None
            ek = nearest_census(new_census, e[1])
            if ek is not None:
                pred_from_edge = new_census[ek]["clock"] + TICKS_PER_BOUNDARY * (n - ek)
                row["new_clock_residual_vs_law"] = row["new_clock"] - pred_from_edge
        rows.append(row)
    return rows


def pair_map_text(rows, edges):
    toks = ["old_b%d=new_b%d" % e for e in edges]
    toks += ["old_b%d=new_b%d" % (r["old_b"], r["new_b"]) for r in rows]
    return ",".join(toks)


def render(rows, edges):
    out = ["edges (case-7 entry, OLD boundary = NEW boundary): " + ", ".join("old_b%d=new_b%d (offset %+d)" % (o, n, n - o) for o, n in edges)]
    out.append("%-10s %-10s %-8s %-9s %-11s %-11s %-24s %-24s %s" % ("old_b", "new_b", "offset", "old_vf", "old_clock", "new_clock", "old_scene", "new_scene", "note"))
    for r in rows:
        lo, hi = r["new_range"]
        exact = ("exact-clock match: new_b%s" % ",".join(str(k) for k in r["exact_match"])) if r["exact_match"] else \
            ("exact-clock match: none (NEW census range %s..%s)" % (lo, hi))
        scene = {True: "scene OK", False: "SCENE-MISMATCH", None: "scene unverified"}[r.get("scene_ok")]
        resid = ("; clock residual vs 149/boundary law %+d ticks" % r["new_clock_residual_vs_law"]) if "new_clock_residual_vs_law" in r else ""
        out.append("%-10d %-10d %+-8d %-9d %-11d %-11s %-24s %-24s %s; %s (%s); %s%s"
                   % (r["old_b"], r["new_b"], r["offset"], r["old_vsync_frame"], r["old_clock"],
                      str(r["new_clock"]) if r["new_clock"] is not None else "-", r["old_scene"], r["new_scene"],
                      r["rule"], scene, r.get("new_source", "-"), exact, resid))
    out.append("pair_map=" + pair_map_text(rows, edges))
    return out


# ---------------------------------------------------------------- selftest

TEST_KEYS = {"clock_word": 0x1234, "boot_sm_word": 0x2340}   # synthetic addresses for the selftest's images


def _image(path, clock, bootsm, vf):
    import zlib
    ram = bytearray(0x4000)
    struct.pack_into("<I", ram, TEST_KEYS["clock_word"], clock)
    struct.pack_into("<Q", ram, TEST_KEYS["boot_sm_word"], bootsm)
    ram = bytes(ram)
    body = b"PS2SAVE1" + struct.pack("<IIIIQ", 4, 3, vf, 0, 0)
    for tag, payload in ((state_hash.TAG_RAM, ram), (state_hash.TAG_SPAD, bytes(16)), (state_hash.TAG_META, bytes(16))):
        body += struct.pack("<IIIIQ", tag, 1, 0, zlib.crc32(payload) & 0xFFFFFFFF, len(payload)) + payload
    body += struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(body)


CASE7_SET = 0x8060718000190000
CASE7_CLR = 0x0060718000190000
CASE6 = 0x0060614000010000


def selftest():
    import shutil
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)

    tmp = tempfile.mkdtemp(prefix="clock_pair_")
    # a NEW census leg: edge at 1100 (case 6 at 1080, case 7 + flag at 1140), 149/boundary + one 2048 jump
    leg = os.path.join(tmp, "leg")
    os.makedirs(leg)
    base = 352950
    for b in range(1080, 1561, 60):
        clock = base + TICKS_PER_BOUNDARY * (b - 1080) + (2048 if b >= 1320 else 0)
        _image(os.path.join(leg, "state_b%d.ps2st" % b), clock, CASE6 if b < 1100 else CASE7_SET, b)
    kpath = os.path.join(tmp, "SCENE_KEYS.tsv")
    with open(kpath, "w") as f:
        f.write("# test\nclock_word\t0x1234\tt\nboot_sm_word\t0x2340\tb\n")
    check(read_keys(kpath) == TEST_KEYS, "SCENE_KEYS.tsv read: the two guest addresses")
    for bad in ("clock_word\t0x1234\tt\n", "clock_word\tzz\tt\nboot_sm_word\t0x2340\tb\n", "other\t0x1\tx\n"):
        with open(kpath + ".bad", "w") as f:
            f.write(bad)
        try:
            read_keys(kpath + ".bad")
            check(False, "a keys file missing a key / with a non-hex address / an unknown key is rejected")
        except ValueError:
            check(True, "a keys file missing a key / with a non-hex address / an unknown key is rejected")
    c = census(leg, TEST_KEYS)
    check(sorted(c) == list(range(1080, 1561, 60)) and c[1140]["case"] == 7 and c[1140]["bit63"] == 1 and c[1080]["case"] == 6,
          "census reads boundary, clock and the boot-SM case / bit 63 from every image")
    tsv = os.path.join(tmp, "census.tsv")
    with open(tsv, "w") as f:
        f.write("\n".join(census_lines(c)) + "\n")
    check(read_census(tsv) == c, "census.tsv round-trips")
    # OLD probes: b1500 inside pass 1 (clock far above NEW's range: OLD's boot pumped the word), b2700 at the title
    probes = os.path.join(tmp, "probe")
    os.makedirs(probes)
    _image(os.path.join(probes, "old_b1500.ps2st"), 1100801, CASE7_SET, 2030)
    _image(os.path.join(probes, "old_b1200.ps2st"), 1055000, CASE7_SET, 1730)
    edges = parse_edges("old_b850=new_b1100")
    rows = pair(probes, c, edges, TEST_KEYS)
    r1500 = [r for r in rows if r["old_b"] == 1500][0]
    check(r1500["new_b"] == 1750 and r1500["offset"] == 250 and r1500["exact_match"] == [],
          "old_b1500 -> new_b1750 by the edge offset +250; exact clock match none -> %r" % r1500["new_b"])
    check(r1500["new_clock"] == c[1560]["clock"] + 149 * 190 and r1500["new_source"].startswith("census b1560"),
          "without the mapped image the NEW clock is predicted from the nearest census image by the 149/boundary law")
    check(r1500["scene_ok"] is True and "scene OK" in "\n".join(render(rows, edges)), "scene words agree (case 7, bit 63 set) -> scene OK")
    # the mapped image present: its words are read and checked; a scene mismatch is named
    imgs = os.path.join(tmp, "new")
    os.makedirs(imgs)
    _image(os.path.join(imgs, "new_b1750.ps2st"), c[1560]["clock"] + 149 * 190, CASE7_SET, 1750)
    _image(os.path.join(imgs, "new_b1450.ps2st"), c[1440]["clock"] + 149 * 10, CASE7_CLR, 1450)
    rows2 = pair(probes, c, edges, TEST_KEYS, imgs)
    r1500, r1200 = [r for r in rows2 if r["old_b"] == 1500][0], [r for r in rows2 if r["old_b"] == 1200][0]
    check(r1500["new_source"] == "image" and r1500["scene_ok"] is True and r1500["new_clock_residual_vs_law"] == 2048,
          "the mapped image's clock and scene are read; the residual vs the 149/boundary law from the edge's census "
          "image is the one +2048 jump between them (reported, never hidden)")
    check(r1200["new_b"] == 1450 and r1200["scene_ok"] is False and "SCENE-MISMATCH" in "\n".join(render(rows2, edges)),
          "a mapped image whose scene word differs is SCENE-MISMATCH")
    # an exact clock match, when one exists, is reported with the boundary
    _image(os.path.join(probes, "old_b1200.ps2st"), c[1320]["clock"], CASE7_SET, 1730)
    rows3 = pair(probes, c, edges, TEST_KEYS)
    check([r for r in rows3 if r["old_b"] == 1200][0]["exact_match"] == [1320], "an exact clock match names the census boundary")
    # a wrong header on the mapped image is an error, never a silent pair
    _image(os.path.join(imgs, "new_b1750.ps2st"), 0, CASE7_SET, 1751)
    try:
        pair(probes, c, edges, TEST_KEYS, imgs)
        check(False, "mapped image with the wrong boundary header rejected")
    except ValueError as e:
        check("vsync_frame 1751 != boundary 1750" in str(e), "mapped image with the wrong boundary header rejected")
    # edges: the last edge at or before B decides; before the first edge the boundary index pairs
    e2 = parse_edges("old_b3833=new_b4111,old_b850=new_b1100")
    check(e2 == [(850, 1100), (3833, 4111)] and edge_for(4200, e2) == (3833, 4111) and edge_for(1500, e2) == (850, 1100)
          and edge_for(600, e2) is None, "edges sort by the OLD boundary; the last edge <= B decides; none before the first")
    check(pair_map_text(rows, edges) == "old_b850=new_b1100,old_b1200=new_b1450,old_b1500=new_b1750", "pair_map text: the edges first, then the probes")
    for bad in ("old_b850", "b850=new_b1100", ""):
        try:
            parse_edges(bad)
            check(False, "bad edge token %r rejected" % bad)
        except ValueError:
            check(True, "bad edge token %r rejected" % bad)
    shutil.rmtree(tmp, ignore_errors=True)
    print("clock_pair selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keys", help="the leg's SCENE_KEYS.tsv (the two guest addresses; required except --selftest)")
    ap.add_argument("--read", nargs="+", metavar="IMAGE")
    ap.add_argument("--census", metavar="LEG_DIR")
    ap.add_argument("--out", help="--census: write the census.tsv here")
    ap.add_argument("--pair", action="store_true")
    ap.add_argument("--old-probes", help="--pair: the frozen OLD probe dir (old_b<B>.ps2st)")
    ap.add_argument("--new-census", help="--pair: a NEW leg dir with state_b<N>.ps2st images, or a census.tsv")
    ap.add_argument("--edges", help="--pair: old_b<B>=new_b<N>[,...] the case-7 entry edges")
    ap.add_argument("--new-images", help="--pair: dir with the mapped new_b<N>.ps2st images (verifies scene + clock)")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.keys:
        ap.error("--keys <leg>/SCENE_KEYS.tsv is required")
    keys = read_keys(a.keys)
    if a.read:
        for p in a.read:
            w = words(p, keys)
            print("%s\tvsync_frame=%d\tclock=%d (0x%x)\tbootsm=0x%016x\tcase=%d\tbit63=%d"
                  % (os.path.basename(p), w["vsync_frame"], w["clock"], w["clock"], w["bootsm"], w["case"], w["bit63"]))
        return 0
    if a.census:
        lines = census_lines(census(a.census, keys))
        print("\n".join(lines))
        if a.out:
            with open(a.out, "w") as f:
                f.write("\n".join(lines) + "\n")
        return 0
    if a.pair:
        if not (a.old_probes and a.new_census and a.edges):
            ap.error("--pair needs --old-probes, --new-census and --edges")
        rows = pair(a.old_probes, load_census(a.new_census, keys), parse_edges(a.edges), keys, a.new_images)
        print("\n".join(render(rows, parse_edges(a.edges))))
        return 1 if any(r.get("scene_ok") is False for r in rows) else 0
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
