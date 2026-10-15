#!/usr/bin/env python3
"""platform_identity.py -- the cross-platform hash-identity check (Charter 2, Track B).

The same binary tree built on another host (Linux x86-64 / aarch64, Windows) replays the
same padlog on the deterministic profile; this script compares its lanes with the macOS
reference recorded under games/<title>/verify/platform_ref/<leg>/ and names the first
divergent key per lane.

  platform_identity.py --ref <platform_ref/<leg>> --candidate <leg dir>
                       [--lanes audio,drawir,gsops,mem,frames] [--require audio,drawir,gsops,mem]
                       [--report <path.md>]
  platform_identity.py --selftest

Lanes and what identity on each means:
  audio    audio.ahash   the mixer output per 100 ms -- CPU arithmetic only. MUST be identical.
  drawir   drawir.ohash  the owned draw-IR stream per frame -- renderer-independent. MUST be identical.
  gsops    gsops.ohash   the GS operation stream per frame -- renderer-independent. MUST be identical.
  mem      mem.mhash     the RAM/SPAD hash every 60 boundaries -- the same tree on another host is the
                         same program: MUST be identical (the OLD/NEW advisory rule does not apply).
  frames   frames.fhash  the GPU-rasterised native-scale framebuffer per present. The user's identity
                         claim; across GL implementations (Apple GL vs Mesa) a divergence here with the
                         four lanes above identical is a RASTERISER difference, attributed as such in the
                         report (the first divergent key and the count), never silently accepted.

Exit codes: 0 every required lane identical (and frames identical when required); 1 a required lane
diverges; 2 usage / missing file; 3 run.meta refuses (different profile, not finalized, different
frame count). `--require` defaults to the four CPU lanes; add `frames` to make the pixel lane a gate.
Pairing is BY KEY (the `<key>,<hash>` line grammar every lane shares); a key one side lacks is a length
divergence. Values are never tolerated.
"""
import argparse
import os
import sys
import tempfile

LANE_FILES = {
    "frames": "frames.fhash",
    "audio": "audio.ahash",
    "drawir": "drawir.ohash",
    "gsops": "gsops.ohash",
    "mem": "mem.mhash",
    # The component-suite identity lane (verify/component_identity.py): the math oracles' per-channel
    # digests. Not in the default lane list (a game-leg reference has none); name it with --lanes.
    "oracles": "oracles.ohash",
}
DEFAULT_LANES = "audio,drawir,gsops,mem,frames"
DEFAULT_REQUIRE = "audio,drawir,gsops,mem"
META_GATES = ("profile", "frames", "finalized")


def read_meta(path):
    meta = {}
    if not os.path.isfile(path):
        return meta
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            if "=" in line:
                k, v = line.split("=", 1)
                meta[k] = v
    return meta


def read_lane(path):
    """[(key, hash)] in file order; keys are the first field of every `key,hash` line."""
    rows = []
    with open(path) as f:
        for n, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if "," not in line:
                raise ValueError("%s:%d: not a `key,hash` line: %r" % (path, n, line))
            k, h = line.split(",", 1)
            rows.append((k.strip(), h.strip().lower()))
    return rows


def compare_lane(ref_rows, cand_rows):
    """dict(identical, first_divergent_key, divergent_count, ref_len, cand_len, missing_in_candidate,
    missing_in_ref). Values paired by key; a key on one side only is a length divergence."""
    ref = dict(ref_rows)
    cand = dict(cand_rows)
    if len(ref) != len(ref_rows) or len(cand) != len(cand_rows):
        raise ValueError("duplicate keys in a lane file")
    missing_in_cand = [k for k, _ in ref_rows if k not in cand]
    missing_in_ref = [k for k, _ in cand_rows if k not in ref]
    first = None
    count = 0
    for k, h in ref_rows:
        if k in cand and cand[k] != h:
            count += 1
            if first is None:
                first = k
    identical = first is None and not missing_in_cand and not missing_in_ref
    return {
        "identical": identical,
        "first_divergent_key": first,
        "divergent_count": count,
        "ref_len": len(ref_rows),
        "cand_len": len(cand_rows),
        "missing_in_candidate": missing_in_cand,
        "missing_in_ref": missing_in_ref,
    }


def meta_refusal(ref_meta, cand_meta):
    """A reason string when the two runs are not comparable, else None."""
    for k in META_GATES:
        if ref_meta.get(k) != cand_meta.get(k):
            return "run.meta %s differs: ref=%r candidate=%r" % (k, ref_meta.get(k), cand_meta.get(k))
    if cand_meta.get("finalized") != "1":
        return "candidate run.meta finalized=%r (not 1)" % cand_meta.get("finalized")
    return None


def verdict(results, require):
    """(exit_code, [lines]) from per-lane results. Lanes in `require` gate; the others report."""
    lines = []
    failed = []
    for lane, r in results.items():
        if r is None:
            lines.append("%-7s MISSING (lane file absent on one side)" % lane)
            if lane in require:
                failed.append(lane)
            continue
        if r["identical"]:
            lines.append("%-7s IDENTICAL (%d keys)" % (lane, r["ref_len"]))
            continue
        detail = []
        if r["first_divergent_key"] is not None:
            detail.append("first divergent key %s, %d differing" % (r["first_divergent_key"], r["divergent_count"]))
        if r["missing_in_candidate"]:
            detail.append("%d keys missing in candidate (first %s)" % (len(r["missing_in_candidate"]), r["missing_in_candidate"][0]))
        if r["missing_in_ref"]:
            detail.append("%d keys missing in reference (first %s)" % (len(r["missing_in_ref"]), r["missing_in_ref"][0]))
        tag = "DIVERGENT" if lane in require else "DIVERGENT (reported, not gated)"
        lines.append("%-7s %s: %s; ref %d / candidate %d keys" % (lane, tag, "; ".join(detail), r["ref_len"], r["cand_len"]))
        if lane in require:
            failed.append(lane)
    cpu_lanes = [l for l in ("audio", "drawir", "gsops", "mem") if l in results and results[l] and results[l]["identical"]]
    fr = results.get("frames")
    if fr is not None and not fr["identical"] and len(cpu_lanes) == 4:
        lines.append("attribution: the four CPU lanes are identical and only the pixel lane differs -> a "
                     "rasteriser difference between the two GL implementations (gl_renderer), not a game-state "
                     "divergence; attribute per draw before accepting")
    return (1 if failed else 0), lines


def run(ref_dir, cand_dir, lanes, require, report_path=None):
    ref_meta = read_meta(os.path.join(ref_dir, "run.meta"))
    cand_meta = read_meta(os.path.join(cand_dir, "run.meta"))
    if not ref_meta or not cand_meta:
        print("platform_identity: run.meta missing (ref=%s candidate=%s)" % (bool(ref_meta), bool(cand_meta)))
        return 2
    header = ["platform_identity: ref %s (%s, %s)" % (ref_dir, ref_meta.get("gl_renderer"), ref_meta.get("build_id")),
              "platform_identity: candidate %s (%s, %s)" % (cand_dir, cand_meta.get("gl_renderer"), cand_meta.get("build_id"))]
    refusal = meta_refusal(ref_meta, cand_meta)
    if refusal:
        for h in header:
            print(h)
        print("platform_identity: REFUSED -- " + refusal)
        return 3
    results = {}
    for lane in lanes:
        rf = os.path.join(ref_dir, LANE_FILES[lane])
        cf = os.path.join(cand_dir, LANE_FILES[lane])
        if not os.path.isfile(rf) or not os.path.isfile(cf):
            results[lane] = None
            continue
        results[lane] = compare_lane(read_lane(rf), read_lane(cf))
    code, lines = verdict(results, require)
    out = header + lines + ["platform_identity: %s (required lanes: %s)" % ("IDENTICAL" if code == 0 else "DIVERGENT", ",".join(require))]
    for l in out:
        print(l)
    if report_path:
        with open(report_path, "w") as f:
            f.write("# platform identity\n\n")
            for l in out:
                f.write("- " + l + "\n")
    return code


def _write(path, text):
    with open(path, "w") as f:
        f.write(text)


def selftest():
    def check(cond, what):
        if not cond:
            print("SELFTEST FAIL: " + what)
            sys.exit(1)
    with tempfile.TemporaryDirectory() as d:
        ref, cand = os.path.join(d, "ref"), os.path.join(d, "cand")
        os.makedirs(ref)
        os.makedirs(cand)
        meta = "build_id=X\ngl_renderer=Apple M4\nprofile=deterministic\nframes=3\nfinalized=1\n"
        _write(os.path.join(ref, "run.meta"), meta)
        _write(os.path.join(cand, "run.meta"), meta.replace("Apple M4", "llvmpipe"))
        for lane, fn in LANE_FILES.items():
            _write(os.path.join(ref, fn), "1,0xaa\n2,0xbb\n3,0xcc\n")
            _write(os.path.join(cand, fn), "1,0xaa\n2,0xbb\n3,0xcc\n")
        # identical everywhere
        check(run(ref, cand, DEFAULT_LANES.split(","), DEFAULT_REQUIRE.split(",")) == 0, "identical -> 0")
        # frames diverge only: reported, attributed, not gated by default; gated with --require frames
        _write(os.path.join(cand, "frames.fhash"), "1,0xaa\n2,0xBB\n3,0xdd\n")
        check(run(ref, cand, DEFAULT_LANES.split(","), DEFAULT_REQUIRE.split(",")) == 0, "frames-only divergence not gated by default")
        r = compare_lane(read_lane(os.path.join(ref, "frames.fhash")), read_lane(os.path.join(cand, "frames.fhash")))
        check(r["first_divergent_key"] == "3" and r["divergent_count"] == 1, "case-insensitive hash, first key 3")
        check(run(ref, cand, DEFAULT_LANES.split(","), DEFAULT_REQUIRE.split(",") + ["frames"]) == 1, "frames gated -> 1")
        # a CPU lane diverges -> 1
        _write(os.path.join(cand, "audio.ahash"), "1,0xaa\n2,0xbb\n3,0xce\n")
        check(run(ref, cand, DEFAULT_LANES.split(","), DEFAULT_REQUIRE.split(",")) == 1, "audio divergence -> 1")
        # length divergence (missing key) -> 1
        _write(os.path.join(cand, "audio.ahash"), "1,0xaa\n2,0xbb\n")
        r = compare_lane(read_lane(os.path.join(ref, "audio.ahash")), read_lane(os.path.join(cand, "audio.ahash")))
        check(not r["identical"] and r["missing_in_candidate"] == ["3"], "missing key is a divergence")
        # meta refusal: a different frame count
        _write(os.path.join(cand, "run.meta"), meta.replace("frames=3", "frames=4"))
        check(run(ref, cand, DEFAULT_LANES.split(","), DEFAULT_REQUIRE.split(",")) == 3, "frames= mismatch refuses")
        _write(os.path.join(cand, "run.meta"), meta.replace("finalized=1", "finalized=0"))
        check(run(ref, cand, DEFAULT_LANES.split(","), DEFAULT_REQUIRE.split(",")) == 3, "unfinalized refuses")
        # a missing lane file is a failure when required
        _write(os.path.join(cand, "run.meta"), meta)
        os.remove(os.path.join(cand, "mem.mhash"))
        check(run(ref, cand, DEFAULT_LANES.split(","), ["mem"]) == 1, "missing required lane -> 1")
        # a real reference set named by the ctest (PS2R_PLATFORM_REF=<platform_ref/<leg>>) parses
        # and is self-identical on every lane; unset = the fixture-only selftest
        real = os.environ.get("PS2R_PLATFORM_REF")
        if real:
            check(os.path.isdir(real), "PS2R_PLATFORM_REF %s is a directory" % real)
            check(run(real, real, DEFAULT_LANES.split(","), DEFAULT_REQUIRE.split(",") + ["frames"]) == 0, "reference set self-identical")
    print("platform_identity selftest OK")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ref", help="the reference leg dir (games/<title>/verify/platform_ref/<leg>)")
    ap.add_argument("--candidate", help="the other host's leg dir")
    ap.add_argument("--lanes", default=DEFAULT_LANES, help="lanes to compare (default %s)" % DEFAULT_LANES)
    ap.add_argument("--require", default=DEFAULT_REQUIRE, help="lanes that gate the exit code (default %s)" % DEFAULT_REQUIRE)
    ap.add_argument("--report", help="write the verdict lines to this markdown file")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.ref or not a.candidate:
        ap.error("--ref and --candidate are required")
    lanes = [l.strip() for l in a.lanes.split(",") if l.strip()]
    require = [l.strip() for l in a.require.split(",") if l.strip()]
    for l in lanes + require:
        if l not in LANE_FILES:
            ap.error("unknown lane %r (known: %s)" % (l, ",".join(LANE_FILES)))
    return run(os.path.abspath(a.ref), os.path.abspath(a.candidate), lanes, require, a.report)


if __name__ == "__main__":
    sys.exit(main())
