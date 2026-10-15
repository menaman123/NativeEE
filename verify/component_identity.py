#!/usr/bin/env python3
"""component_identity.py -- the cross-platform identity CI can check WITHOUT the disc (Charter 2, B3).

The game-files identity (frames/audio/drawir/gsops/mem of a padlog replay, platform_identity.py)
needs the extracted game files and a game process: it runs on a developer's machine only. CI has
neither, so CI's identity claim is the component suite's deterministic oracles: the math library
oracles (generated NTSC bodies as the model arm, the native MathLibrary as the selected arm, the
captured originals as the control arm) run the same synthesized inputs on every host, and in
`--digest` mode each streams the bytes its DT-12 receipt would hold -- inputs, the three arms'
outputs, the three counter streams, the fixtures, the completion record -- into one SHA-256 per
channel. The lane `oracles.ohash` is those digests keyed `<Oracle>/<channel>`; two hosts whose
lanes are identical ran the same arithmetic (fp-contract off, wrapv, the rounding-mode sections)
on the same inputs through the same generated code. A divergence names the oracle and channel.

  component_identity.py --collect <digest dir> --out <lanes dir> [--build-id ID] [--host NAME]
      <digest dir> = PS2R_MATH_ORACLE_DIGEST_DIR of a configured build after `ctest -R Oracle`;
      writes <lanes dir>/oracles.ohash + run.meta (profile=component-oracles, frames=<keys>,
      finalized=1). Refuses an empty collection.
  component_identity.py --compare <ref lanes dir> <candidate lanes dir> [--report <path.md>]
      platform_identity.py over the `oracles` lane, required. Exit 0 identical / 1 divergent /
      3 run.meta refuses (a different oracle set = a different frames= count).
  component_identity.py --selftest
"""
import argparse
import glob
import os
import platform
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import platform_identity  # noqa: E402

LANE = "oracles"
PROFILE = "component-oracles"


def read_digest(path):
    rows = []
    with open(path) as f:
        for n, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if "," not in line:
                raise ValueError("%s:%d: not a `channel,sha256` line: %r" % (path, n, line))
            ch, h = line.split(",", 1)
            rows.append((ch.strip(), h.strip().lower()))
    return rows


def collect(digest_dir, out_dir, build_id, host):
    files = sorted(glob.glob(os.path.join(digest_dir, "*.digest")))
    if not files:
        print("component_identity: no *.digest under %s (configure with -DPS2R_MATH_ORACLE_DIGEST_DIR=<dir> and run ctest -R Oracle)" % digest_dir)
        return 2
    rows = []
    for path in files:
        oracle = os.path.basename(path)[:-len(".digest")]
        for ch, h in read_digest(path):
            rows.append(("%s/%s" % (oracle, ch), h))
    keys = [k for k, _ in rows]
    if len(set(keys)) != len(keys):
        print("component_identity: duplicate keys in the digests")
        return 2
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, platform_identity.LANE_FILES[LANE]), "w") as f:
        f.write("# component identity lane: <Oracle>/<channel>,sha256 (math oracle --digest streams)\n")
        for k, h in rows:
            f.write("%s,%s\n" % (k, h))
    with open(os.path.join(out_dir, "run.meta"), "w") as f:
        f.write("profile=%s\nframes=%d\nfinalized=1\nbuild_id=%s\ngl_renderer=none\nhost=%s\noracles=%d\n"
                % (PROFILE, len(rows), build_id or "unknown", host or default_host(), len(files)))
    print("component_identity: %d oracles, %d channel digests -> %s" % (len(files), len(rows), out_dir))
    for path in files:
        print("  " + os.path.basename(path)[:-len(".digest")])
    return 0


def default_host():
    return "%s-%s" % (platform.system().lower(), platform.machine().lower())


def compare(ref, cand, report):
    return platform_identity.run(os.path.abspath(ref), os.path.abspath(cand), [LANE], [LANE], report)


def selftest():
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)
    with tempfile.TemporaryDirectory() as d:
        dig = os.path.join(d, "dig")
        os.makedirs(dig)
        for name, tail in (("MathLibraryTrigOracle", "aa"), ("MathLibraryBoundsOracle", "bb")):
            with open(os.path.join(dig, name + ".digest"), "w") as f:
                f.write("# header\ninputs.bin,%s11\nmodel.outputs.bin,%s22\nselected.outputs.bin,%s33\ncomplete.json,%s44\n" % ((tail,) * 4))
        ref = os.path.join(d, "ref")
        check(collect(dig, ref, "B1", "macos-arm64") == 0, "collect writes the lane")
        rows = platform_identity.read_lane(os.path.join(ref, "oracles.ohash"))
        check(len(rows) == 8 and rows[0][0] == "MathLibraryBoundsOracle/inputs.bin", "8 keys, <Oracle>/<channel>, sorted by oracle: %s" % rows[0][0])
        meta = platform_identity.read_meta(os.path.join(ref, "run.meta"))
        check(meta["profile"] == PROFILE and meta["frames"] == "8" and meta["finalized"] == "1" and meta["build_id"] == "B1", "run.meta: %s" % meta)
        cand = os.path.join(d, "cand")
        check(collect(dig, cand, "B2", "linux-x86_64") == 0 and compare(ref, cand, None) == 0, "the same digests on another host -> IDENTICAL (exit 0)")
        with open(os.path.join(dig, "MathLibraryTrigOracle.digest"), "w") as f:
            f.write("inputs.bin,aa11\nmodel.outputs.bin,aa22\nselected.outputs.bin,ff33\ncomplete.json,aa44\n")
        cand2 = os.path.join(d, "cand2")
        collect(dig, cand2, "B3", "windows-amd64")
        check(compare(ref, cand2, os.path.join(d, "r.md")) == 1, "one differing channel (Trig selected.outputs) -> DIVERGENT (exit 1)")
        text = open(os.path.join(d, "r.md")).read()
        check("MathLibraryTrigOracle/selected.outputs.bin" in text, "the report names the oracle and channel")
        os.remove(os.path.join(dig, "MathLibraryBoundsOracle.digest"))
        cand3 = os.path.join(d, "cand3")
        collect(dig, cand3, "B4", "x")
        check(compare(ref, cand3, None) == 3, "a different oracle set (frames= differs) REFUSES (exit 3)")
        empty = os.path.join(d, "empty")
        os.makedirs(empty)
        check(collect(empty, os.path.join(d, "x"), "", "") == 2, "an empty digest dir is refused (exit 2)")
    print("component_identity selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--collect", metavar="DIGEST_DIR")
    ap.add_argument("--out", metavar="LANES_DIR")
    ap.add_argument("--build-id", default="")
    ap.add_argument("--host", default="")
    ap.add_argument("--compare", nargs=2, metavar=("REF", "CANDIDATE"))
    ap.add_argument("--report")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if a.collect:
        if not a.out:
            ap.error("--collect needs --out")
        return collect(a.collect, a.out, a.build_id, a.host)
    if a.compare:
        return compare(a.compare[0], a.compare[1], a.report)
    ap.error("one of --collect, --compare, --selftest")


if __name__ == "__main__":
    sys.exit(main())
