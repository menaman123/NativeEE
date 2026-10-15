#!/usr/bin/env python3
"""fmv_plane_check.py -- the FMV plane check (DP-12 (a)): is the picture the NEW leg
served in the title's RGB32 image plane, laid out exactly as the engine's FrameWriter
lays it? Writes <leg>/fmv_plane_check.txt, the evidence `diff_legs.py --fmv-pictures`
needs to declare the FMV window PAIRED-BY-PICTURE.

  fmv_plane_check.py --leg <leg dir> --og-root <OLD root> --manifest <golden.sha256>
                     [--oracle build/tests/ipu/fmv_picture_oracle]
                     [--layout build/tools/fmv_layout_ppm/fmv_layout_ppm]
                     [--movies FMV/VIVENDI.PSS,FMV/TTIDENT.PSS] [--work <dir>]
                     [--window LO-HI] [--planes 0xADDR,...]

Procedure (verify/README.md, `fmv_plane_check.txt` row):
  1. the leg's `state_b<N>.ps2st` images across its census `fmv_window=<lo>-<hi>`
     (boundaries; `--window` overrides) are the samples -- each holds RAM0 (32 MB);
  2. the oracle (`fmv_picture_oracle --dump-dir`) dumps every decoded picture of the
     movies as P6 PPMs, `movie<m>_pic_<k>.ppm` (k = the DECODED index);
  3. `fmv_layout_ppm` lays each picture out through the REAL FrameWriter::WriteRgb32Strips
     (16-pixel column strips of macroblock-aligned rows, alpha 0x80) -> the strip image;
  4. per state, the plane is located in RAM0 (the `--planes` hints first, then a
     high-entropy 4 KiB probe -- or, for low-entropy pictures, its most varied whole
     strip -- of each laid-out picture searched in RAM0, 16-byte aligned)
     and compared STRIP-WISE against every picture of the movie whose dimensions match:
     the picture with the most bit-exact strips is that plane's reading (every plane
     found in the state is read -- both halves of the title's double buffer);
  5. per movie, the line records the boundary with the most bit-exact strips (ties:
     the LATEST boundary -> the latest served picture that is still intact), as
       movie<N> boundary=<b> plane=0x<addr> picture=<k> strips_exact=<n>/<total>
     `#` lines carry every state's reading. diff_legs accepts only n == total.
The movie split follows the manifest's dimension runs (movie 1 = the first run).
Helpers: subprocess only (no multiprocessing); the golden byte sets are built once.
"""
import argparse
import glob
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
TAG_RAM0 = struct.unpack("<I", b"RAM0")[0]
PROBE_LEN = 4096


def read_meta(leg):
    meta = {}
    with open(os.path.join(leg, "run.meta")) as f:
        for line in f:
            if "=" in line:
                k, v = line.rstrip("\n").split("=", 1)
                meta[k] = v
    return meta


def manifest_movies(path):
    """[(w, h, count)] per dimension run of the golden manifest (movie order)."""
    runs = []
    with open(path) as f:
        for raw in f:
            line = raw.rstrip("\n")
            if not line.strip() or line.startswith("#"):
                continue
            parts = line.split("\t")
            w, h = int(parts[2]), int(parts[3])
            if runs and runs[-1][0] == w and runs[-1][1] == h:
                runs[-1][2] += 1
            else:
                runs.append([w, h, 1])
    return [tuple(r) for r in runs]


def ram0_of(path):
    """(vsync_frame, RAM0 bytes) of a PS2SAVE1 image."""
    with open(path, "rb") as f:
        data = f.read()
    magic, _ver, nsec, vsync, _flags, _wall = struct.unpack_from("<8sIIIIQ", data, 0)
    if magic != b"PS2SAVE1":
        raise SystemExit("%s: not a PS2SAVE1 image" % path)
    off = 32
    for _ in range(nsec):
        tag, _sver, _sflags, _crc, plen = struct.unpack_from("<IIIIQ", data, off)
        off += 24
        if tag == TAG_RAM0:
            return vsync, data[off:off + plen]
        off += plen
    raise SystemExit("%s: no RAM0 section" % path)


def best_probe(g, w=0, h=0):
    """The locate needle of a laid-out picture as (offset, length): the 4 KiB window
    with the most distinct dwords (>= 256), else -- for low-entropy pictures such as
    the fades a movie ends on -- the whole strip with the most distinct dwords
    (>= 16; a 30 KiB strip is not found by accident), else None (a constant picture
    cannot be located; planes already known from other states are still read)."""
    best = (None, 0)
    for off in range(0, len(g) - PROBE_LEN, PROBE_LEN):
        win = g[off:off + PROBE_LEN]
        d = len(set(win[i:i + 4] for i in range(0, PROBE_LEN, 4)))
        if d > best[1]:
            best = (off, d)
    if best[1] >= 256:
        return (best[0], PROBE_LEN)
    if w and h:
        strip = align16(h) * 16 * 4
        best = (None, 0)
        for s_i in range(align16(w) // 16):
            st = g[s_i * strip:(s_i + 1) * strip]
            d = len(set(st[i:i + 4] for i in range(0, len(st), 4)))
            if d > best[1]:
                best = (s_i * strip, d)
        if best[1] >= 16:
            return (best[0], strip)
    return None


def align16(v):
    return (v + 15) & ~15


def strips_exact(plane, g, w, h):
    """(exact strips, total strips) between a RAM plane and a laid-out golden."""
    strip = align16(h) * 16 * 4
    total = align16(w) // 16
    n = 0
    for s in range(total):
        lo = s * strip
        if plane[lo:lo + strip] == g[lo:lo + strip]:
            n += 1
    return n, total


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--leg", required=True)
    ap.add_argument("--og-root", required=True)
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--oracle", default=os.path.join(REPO, "build", "tests", "ipu", "fmv_picture_oracle"))
    ap.add_argument("--layout", default=os.path.join(REPO, "build", "tools", "fmv_layout_ppm", "fmv_layout_ppm"))
    ap.add_argument("--movies", default="FMV/VIVENDI.PSS,FMV/TTIDENT.PSS")
    ap.add_argument("--work", help="scratch dir for the PPM dumps and strip images (default <leg>/plane_check_work)")
    ap.add_argument("--window", help="LO-HI boundaries (default: the leg's run.meta fmv_window)")
    ap.add_argument("--planes", default="", help="comma list of 0x plane addresses to try first")
    a = ap.parse_args()

    leg = os.path.abspath(a.leg)
    meta = read_meta(leg)
    win = a.window or meta.get("fmv_window", "")
    m = re.match(r"^(\d+)-(\d+)$", win)
    if not m:
        print("fmv_plane_check: no usable window (run.meta fmv_window=%r, --window %r)" % (meta.get("fmv_window"), a.window))
        return 2
    lo, hi = int(m.group(1)), int(m.group(2))
    movies = manifest_movies(a.manifest)
    movie_files = [x for x in a.movies.split(",") if x]
    if len(movie_files) != len(movies):
        print("fmv_plane_check: manifest has %d dimension runs but %d --movies given" % (len(movies), len(movie_files)))
        return 2
    work = a.work or os.path.join(leg, "plane_check_work")
    ppm_dir, strip_dir = os.path.join(work, "ppm"), os.path.join(work, "strips")
    os.makedirs(ppm_dir, exist_ok=True)
    os.makedirs(strip_dir, exist_ok=True)

    # 2. the oracle's pictures
    if not glob.glob(os.path.join(ppm_dir, "movie1_pic_*.ppm")):
        cmd = [a.oracle, "--og-root", a.og_root] + sum([["--movie", f] for f in movie_files], []) + ["--dump-dir", ppm_dir]
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if r.returncode != 0:
            print("fmv_plane_check: oracle failed (%d): %s" % (r.returncode, r.stdout[-400:]))
            return 1
    # 3. laid out through the real FrameWriter; goldens[movie] = [(k, bytes)], built ONCE
    goldens = {}
    for mi, (w, h, _count) in enumerate(movies, 1):
        goldens[mi] = []
        for ppm in sorted(glob.glob(os.path.join(ppm_dir, "movie%d_pic_*.ppm" % mi))):
            k = int(re.search(r"_pic_(\d+)\.ppm$", ppm).group(1))
            out = os.path.join(strip_dir, "movie%d_%04d.bin" % (mi, k))
            if not os.path.isfile(out):
                r = subprocess.run([a.layout, ppm, out], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                if r.returncode != 0:
                    print("fmv_plane_check: layout failed: %s" % r.stdout.strip())
                    return 1
            with open(out, "rb") as f:
                g = f.read()
            if len(g) != align16(w) * align16(h) * 4:
                print("fmv_plane_check: movie%d picture %d laid out to %d bytes, manifest dims %dx%d expect %d"
                      % (mi, k, len(g), w, h, align16(w) * align16(h) * 4))
                return 1
            goldens[mi].append((k, g))
        print("movie%d: %d decoded pictures laid out (%dx%d, %d strips of %d bytes)"
              % (mi, len(goldens[mi]), w, h, align16(w) // 16, align16(h) * 64))
    probes = {mi: [(k, best_probe(g, movies[mi - 1][0], movies[mi - 1][1])) for k, g in goldens[mi]] for mi in goldens}

    # 4. the states across the window
    states = []
    for p in glob.glob(os.path.join(leg, "state_b*.ps2st")):
        b = int(re.search(r"state_b(\d+)\.ps2st$", p).group(1))
        if lo <= b <= hi:
            states.append((b, p))
    states.sort()
    if not states:
        print("fmv_plane_check: no state_b<N>.ps2st inside the window %d-%d under %s" % (lo, hi, leg))
        return 1
    hints = [int(x, 16) for x in a.planes.split(",") if x]
    known = {mi: list(hints) for mi in goldens}  # plane addresses that worked, per movie
    # readings[mi] = every (b, addr, k, exact, total) with exact > 0: one per PLANE found in
    # a state (both halves of the title's double buffer are examined -- the probe search
    # runs over EVERY decoded picture, it does not stop at the first hit).
    readings = {mi: [] for mi in goldens}
    for b, path in states:
        vsync, ram = ram0_of(path)
        line = "state_b%d (vsync %d):" % (b, vsync)
        for mi, (w, h, _c) in enumerate(movies, 1):
            glen = len(goldens[mi][0][1]) if goldens[mi] else 0
            if not glen:
                continue
            # locate every plane by probe: each picture's probe, every 16-aligned hit
            for k, po in probes[mi]:
                if po is None:
                    continue
                g = dict(goldens[mi])[k]
                needle = g[po[0]:po[0] + po[1]]
                po = po[0]
                i = ram.find(needle)
                while i >= 0:
                    if (i - po) % 16 == 0:
                        addr = i - po
                        if addr not in known[mi]:
                            known[mi].append(addr)
                    i = ram.find(needle, i + 1)
            found = []
            for addr in known[mi]:
                plane = ram[addr:addr + glen]
                best = None  # (exact, k, total) -- the picture this plane holds
                for k2, g2 in goldens[mi]:
                    n, total = strips_exact(plane, g2, w, h)
                    if best is None or n > best[0] or (n == best[0] and k2 > best[1]):
                        best = (n, k2, total)
                if best is not None and best[0] > 0:
                    found.append((addr, best[1], best[0], best[2]))
                    readings[mi].append((b, addr, best[1], best[0], best[2]))
            if not found:
                line += " movie%d: no plane found;" % mi
            else:
                for addr, k, n, total in sorted(found):
                    line += " movie%d: plane=0x%x picture=%d strips_exact=%d/%d;" % (mi, addr, k, n, total)
        print(line)

    # 5. the record: per movie, the LAST decoded (= last served) picture's best reading
    # -- the movie ran to its end and that final picture is laid out bit-exactly in a
    # plane. A movie whose last picture is never found in any plane fails the check.
    out_lines = ["# fmv_plane_check.py: leg %s, window boundaries %d-%d, %d states; manifest %s"
                 % (leg, lo, hi, len(states), os.path.abspath(a.manifest)),
                 "# per movie: the LAST decoded picture's reading with the most bit-exact strips "
                 "(ties -> the earliest boundary, before post-serve title writes); every plane of "
                 "every state is listed below; picture = the oracle's DECODED index"]
    verdict_ok = True
    for mi in sorted(readings):
        for (b, addr, k, n, total) in readings[mi]:
            out_lines.append("#   movie%d state_b%d plane=0x%x picture=%d strips_exact=%d/%d" % (mi, b, addr, k, n, total))
        if not goldens[mi]:
            out_lines.append("# movie%d: no decoded pictures" % mi)
            verdict_ok = False
            continue
        last_k = max(k for k, _g in goldens[mi])
        last = [r for r in readings[mi] if r[2] == last_k]
        if not last:
            out_lines.append("# movie%d: last decoded picture %d never found in a plane of any window state"
                             % (mi, last_k))
            verdict_ok = False
            continue
        pick = max(last, key=lambda r: (r[3], -r[0]))
        b, addr, k, n, total = pick
        out_lines.append("movie%d boundary=%d plane=0x%x picture=%d strips_exact=%d/%d" % (mi, b, addr, k, n, total))
        verdict_ok &= (n == total)
    with open(os.path.join(leg, "fmv_plane_check.txt"), "w") as f:
        f.write("\n".join(out_lines) + "\n")
    for l in out_lines:
        if not l.startswith("#"):
            print(l)
    print("fmv_plane_check: %s -> %s" % ("every movie bit-exact" if verdict_ok else "NOT every movie bit-exact",
                                          os.path.join(leg, "fmv_plane_check.txt")))
    return 0 if verdict_ok else 1


if __name__ == "__main__":
    sys.exit(main())
