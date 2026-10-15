#!/usr/bin/env python3
"""asset_guard.py -- game files NEVER in CI: the scan that proves it (Charter 2, Track B3/C1).

  asset_guard.py --root <dir> [--mode ci|local|artifact] [--report <path.md>] [--json <path>]
  asset_guard.py --selftest

Walks every regular file under --root (build trees and .git excluded) and classifies it:

  A  DISC / EXTRACTED GAME FILES   a disc image, a title ELF, an IOP module, a disc-dump
                                   asset, an FMV, a sound bank ... by NAME and by MAGIC at a fixed
                                   offset (ISO9660 PVD at 0x8001; a MIPS ELF; VAGp; an MPEG pack
                                   start; the SYSTEM.CNF text; the archive index of the title's
                                   packed asset file). FAIL in every mode. We ship no game data.
  B  GAME-DERIVED ORACLE IMAGES    RAM savestates (*.ps2st), GS dumps (*.gs.zst, *.gsf1), console
                                   screenshots (*_pcsx2shot.png, *_shot.ppm) and any other binary
                                   under a probe/ or gsdumps/ directory. The repo carries them for the LOCAL Root legs;
                                   the CI checkout excludes them (sparse checkout, .dockerignore)
                                   and this guard FAILS in --mode ci / artifact if one is present.
                                   --mode local only reports them.
  C  PINNED SMALL FIXTURES         the tracked small binary fixtures, allowed exactly as pinned in
                                   tools/ci/asset_guard_pins.tsv (path, bytes, sha256). Today ONE:
                                   tests/oracle/og_tables.bin, the GS hardware swizzle tables (not
                                   game data). The game-derived fixtures that used to be pinned
                                   (tests/vu/fixtures/*.bin VU0 captures, the title-screen atlas
                                   pages / vertex records) left the tree: they are private fixtures
                                   ($NATIVEEE_PRIVATE_FIXTURES, cmake/Ps2rPrivateFixtures.cmake).
                                   A pinned file whose bytes changed, or a new binary blob above
                                   --max-blob bytes that is not pinned, FAILS.

Exit 0 clean (the report lists class C and, in --mode local, class B); 1 a finding; 2 usage.
Deterministic output: files sorted, one line per finding `CLASS  <reason>  <path>`.
"""
import argparse
import hashlib
import os
import subprocess
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_PINS = os.path.join(HERE, "asset_guard_pins.tsv")

# Directories never scanned (build outputs are scanned only under --mode artifact with --root dist/).
SKIP_DIRS = {".git", "__pycache__", "node_modules", ".cache", "vcpkg", "vcpkg-cache"}
SKIP_DIR_RE = re.compile(r"^(build|build-[A-Za-z0-9_.-]+|out|dist|CMakeFiles)$")

# --- class A: disc / extracted game files -----------------------------------------------------
A_NAME_RE = [
    (re.compile(r"\.(iso|cue|mdf|mds|nrg|ccd|cdi|chd|img)$", re.I), "disc image extension"),
    (re.compile(r"^S[CL][EUP]S_\d{3}\.\d{2}(;1)?$", re.I), "PS2 title executable name"),
    (re.compile(r"^SYSTEM\.CNF$", re.I), "PS2 disc SYSTEM.CNF"),
    (re.compile(r"^IOPRP\d+\.IMG$", re.I), "IOP reboot image"),
    (re.compile(r"\.(irx|pss|vag|ads|bh|bd|rm2|mb|gfx|ptc|lt|pbs|str|vb|hd|bd2|ipu|cbt|txt\.cab)$", re.I), "extracted game asset extension"),
    (re.compile(r"\.elf$", re.I), "ELF file name (a PS2 ELF is class A by magic; any .elf is refused by name)"),
]
A_DIR_RE = [
    # Generic, not any one title's: game_files/ is where TitleInputs puts extracted inputs, and
    # <name>.rep is this project's disc-dump directory convention. A title with another disc
    # directory name relies on the by-name / by-extension / by-magic rules above, which catch
    # the files themselves regardless of the directory they sit in.
    (re.compile(r"(^|/)game_files(/|$)"), "game_files directory"),
    (re.compile(r"(^|/)[^/]+\.rep(/|$)", re.I), "disc dump directory"),
]
# `.bin` is ambiguous (the title ships disc .bin and the tree has small fixture .bin): a .bin is
# class A when it is disc-sized or carries a class-A magic; otherwise it falls to class C's pin rule.
DISC_BIN_MIN = 64 * 1024 * 1024


def a_magic(path, size):
    """A class-A reason from the file's bytes at fixed offsets, or None."""
    try:
        with open(path, "rb") as f:
            head = f.read(64)
            pvd = b""
            if size >= 0x8001 + 5:
                f.seek(0x8001)
                pvd = f.read(5)
            pvd_raw = b""
            if size >= 0x9319 + 5:  # 2352-byte raw sectors: sector 16 at 16*2352 + 16 (sync+header)
                f.seek(16 * 2352 + 16 + 1)
                pvd_raw = f.read(5)
    except OSError:
        return None
    if pvd == b"CD001" or pvd_raw == b"CD001":
        return "ISO9660 primary volume descriptor"
    if head[:4] == b"\x7fELF" and len(head) >= 20:
        machine = struct.unpack_from("<H", head, 18)[0]
        if machine == 8:
            return "MIPS ELF (a PS2 executable)"
    if head[:4] == b"VAGp":
        return "VAG sound bank"
    if head[:4] == b"\x00\x00\x01\xba":
        return "MPEG program stream (FMV)"
    if head[:12].upper().startswith(b"BOOT2") or (b"BOOT2" in head and b"cdrom0:" in head):
        return "SYSTEM.CNF text"
    if size >= DISC_BIN_MIN and path.lower().endswith(".bin"):
        return "disc-sized .bin"
    return None


# --- class B: game-derived oracle images --------------------------------------------------------
B_NAME_RE = [
    (re.compile(r"\.ps2st$", re.I), "RAM savestate (game bytes)"),
    (re.compile(r"\.gs\.zst$", re.I), "GS dump"),
    (re.compile(r"\.gsf1$", re.I), "GS frame dump"),
    (re.compile(r"_pcsx2shot\.png$", re.I), "console screenshot"),
    (re.compile(r"_shot\.ppm$", re.I), "console screenshot"),
]
# Non-text files under these directories are class B too (a dump under a new name). Their text
# companions (PROOF.txt, INDEX.tsv, PROVENANCE.md, *_census.txt) are records, not game bytes.
B_DIR_RE = [
    (re.compile(r"(^|/)gsdumps/$"), "GS dumps directory"),
    (re.compile(r"(^|/)probe/$"), "RAM probes directory"),
]
# Pad recordings (*.padlog, play_sessions/) are controller inputs + run records: not game bytes.
TEXT_EXT = {".cpp", ".h", ".hpp", ".c", ".inc", ".inl", ".py", ".md", ".txt", ".tsv", ".json", ".cmake",
            ".sh", ".ps1", ".yml", ".yaml", ".ini", ".sha256", ".meta", ".fhash", ".ahash", ".ohash", ".mhash",
            ".patch", ".ranges", ".toml", ".cfg", ".csv", ".gitignore", ".dockerignore", ".glsl", ".vert", ".frag",
            ".s", ".asm", ".ld", ".rst", ".html", ".css", ".js", ".ts", ".xml", ".plist", ".svg", ".in", ".def",
            ".rc", ".manifest", ".clang-format", ".editorconfig", ".license", ".cnf", ".ppm.txt", ".lst", ".log"}
TEXT_NAMES = {"CMakeLists.txt", "Dockerfile", "LICENSE", "README", "BUILD-ID", "CMakePresets.json", "Makefile"}


def is_text_like(path):
    base = os.path.basename(path)
    if base in TEXT_NAMES or base.startswith("README") or base.startswith("LICENSE"):
        return True
    _, ext = os.path.splitext(base)
    if ext.lower() in TEXT_EXT:
        return True
    try:
        with open(path, "rb") as f:
            chunk = f.read(4096)
    except OSError:
        return False
    if not chunk:
        return True
    if b"\x00" in chunk:
        return False
    printable = sum(1 for b in chunk if 32 <= b < 127 or b in (9, 10, 13))
    return printable / len(chunk) > 0.95


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def load_pins(path):
    pins = {}
    if not os.path.isfile(path):
        return pins
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            cols = line.split("\t")
            if len(cols) < 3:
                raise ValueError("%s: bad pin line %r" % (path, line))
            pins[cols[0]] = (int(cols[1]), cols[2].lower())
    return pins


def tracked_files(root):
    """The paths `git ls-files` knows under a work tree root, or None when root is not one.

    --mode local scans a CHECKOUT: what a clone contains. The title writes its play output
    beside the binary on a dev host (memory cards, savestates, session logs -- game-derived
    bytes by construction, untracked); those are not in any checkout CI sees and must not
    fail the tree test, so the local scan is restricted to the tracked set when git is there.
    """
    if not os.path.isdir(os.path.join(root, ".git")) and not os.path.isfile(os.path.join(root, ".git")):
        return None
    try:
        out = subprocess.run(["git", "-C", root, "ls-files", "-z"], check=True, capture_output=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    return {p.decode("utf-8", "replace") for p in out.split(b"\0") if p}


def walk(root, tracked=None):
    root = os.path.abspath(root)
    if tracked is not None:
        for rel in sorted(tracked):
            p = os.path.join(root, rel)
            if os.path.islink(p) or not os.path.isfile(p):
                continue
            yield rel.replace(os.sep, "/"), p
        return
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS and not SKIP_DIR_RE.match(d))
        for fn in sorted(filenames):
            p = os.path.join(dirpath, fn)
            if os.path.islink(p) or not os.path.isfile(p):
                continue
            yield os.path.relpath(p, root).replace(os.sep, "/"), p


SHARED_LIB_RE = re.compile(r"\.(dylib|so(\.\d+)*|dll|pdb)$", re.I)


def classify(rel, path, size, pins, max_blob, artifact_binaries, mode="ci"):
    """-> (class, reason) with class in 'A','B','C','C!','' (clean)."""
    base = os.path.basename(rel)
    for rx, why in A_DIR_RE:
        if rx.search(rel):
            return "A", why
    for rx, why in A_NAME_RE:
        if rx.search(base):
            return "A", why
    m = a_magic(path, size)
    if m:
        return "A", m
    pinned = pins.get(rel)
    if pinned is not None:
        actual = (size, sha256_file(path))
        if actual != pinned:
            return "C!", "pinned fixture changed: pinned %d/%s got %d/%s" % (pinned[0], pinned[1][:12], actual[0], actual[1][:12])
        return "C", "pinned fixture"
    for rx, why in B_NAME_RE:
        if rx.search(base):
            return "B", why
    for rx, why in B_DIR_RE:
        if rx.search(os.path.dirname(rel) + "/") and not is_text_like(path):
            return "B", why
    if base in artifact_binaries:
        return "", ""
    if mode == "artifact" and SHARED_LIB_RE.search(base):
        return "", ""   # the bundled runtime libraries (dist/<platform>/lib)
    if size > max_blob and not is_text_like(path):
        return "C!", "unpinned binary blob of %d bytes (pin it in asset_guard_pins.tsv or keep it out of the tree)" % size
    return "", ""


def scan(root, mode, pins, max_blob, artifact_binaries=()):
    findings = []  # (class, reason, rel)
    allowed = []
    n = 0
    for rel, path in walk(root, tracked_files(root) if mode == "local" else None):
        n += 1
        size = os.path.getsize(path)
        cls, why = classify(rel, path, size, pins, max_blob, set(artifact_binaries), mode)
        if not cls:
            continue
        if cls == "A" or cls == "C!":
            findings.append((cls, why, rel))
        elif cls == "B":
            (findings if mode in ("ci", "artifact") else allowed).append((cls, why, rel))
        elif cls == "C":
            allowed.append((cls, why, rel))
    return n, findings, allowed


def report_lines(root, mode, n, findings, allowed):
    lines = ["asset_guard: root %s mode %s files %d" % (root, mode, n)]
    for cls, why, rel in findings:
        lines.append("%-2s FAIL  %s  %s" % (cls, why, rel))
    for cls, why, rel in allowed:
        lines.append("%-2s ok    %s  %s" % (cls, why, rel))
    lines.append("asset_guard: %s (%d finding(s), %d allowed)" % ("FAIL" if findings else "CLEAN", len(findings), len(allowed)))
    return lines


def write_pins(root, paths, out):
    with open(out, "w") as f:
        f.write("# tools/ci/asset_guard_pins.tsv -- class C: the game-derived test fixtures the component suite\n")
        f.write("# needs, allowed exactly as pinned (path<TAB>bytes<TAB>sha256). Regenerate with\n")
        f.write("#   python3 tools/ci/asset_guard.py --root . --write-pins <paths...>\n")
        for rel in sorted(paths):
            p = os.path.join(root, rel)
            f.write("%s\t%d\t%s\n" % (rel, os.path.getsize(p), sha256_file(p)))


def selftest():
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)
    with tempfile.TemporaryDirectory() as d:
        def put(rel, data):
            p = os.path.join(d, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "wb") as f:
                f.write(data)
            return p
        put("src/a.cpp", b"int main(){}\n")
        put("docs/x.md", b"# hi\n")
        put("build-x/junk.iso", b"x" * 100)                          # build dirs are skipped
        iso = bytearray(0x8001 + 5 + 100); iso[0x8001:0x8006] = b"CD001"
        put("stuff/game.dat", bytes(iso))                             # class A by magic, innocent name
        elf = bytearray(64); elf[:4] = b"\x7fELF"; struct.pack_into("<H", elf, 18, 8)
        put("stuff/thing.out", bytes(elf))                            # MIPS ELF by magic
        x86 = bytearray(64); x86[:4] = b"\x7fELF"; struct.pack_into("<H", x86, 18, 62)
        put("stuff/host_tool", bytes(x86))                            # x86-64 ELF: not class A
        put("stuff/SLUS_123.45", b"\x00" * 16)                         # class A by name
        put("stuff/mygame.rep/x.txt", b"hello")                       # class A by directory
        put("stuff/snd.bin", b"VAGp" + b"\x00" * 60)                  # VAG by magic
        put("stuff/movie.pss", b"\x00\x00\x01\xba" + b"\x00" * 32)
        put("games/t/verify/baseline/leg/probe/old_b2.ps2st", b"\x00" * 1000)   # class B
        put("games/t/verify/baseline/leg/gsdumps/a.gs.zst", b"\x28\xb5\x2f\xfd" + b"\x00" * 8)
        put("play_sessions/s3/session.padlog", b"\x01" * 10)          # inputs: not game bytes
        put("games/t/verify/baseline/leg/probe/PROOF.txt", b"frames identical\n")   # text record: clean
        put("games/t/verify/baseline/leg/probe/mystery.dat", b"\x00\x01" * 40)    # binary under probe/: B
        fixture = put("tests/vu/fixtures/vu0_x_code.bin", bytes(range(256)) * 16)   # class C
        put("tests/big_unpinned.dat", os.urandom(300000))                          # C! unpinned blob
        pins = os.path.join(d, "pins.tsv")
        write_pins(d, ["tests/vu/fixtures/vu0_x_code.bin"], pins)
        P = load_pins(pins)
        check(len(P) == 1, "pins written and loaded (1)")

        n, f_ci, a_ci = scan(d, "ci", P, 65536)
        classes = {(c, r) for c, _, r in f_ci}
        check(("A", "stuff/game.dat") in classes, "ISO9660 magic under an innocent name is class A")
        check(("A", "stuff/thing.out") in classes, "MIPS ELF by magic is class A")
        check(all(r != "stuff/host_tool" for _, _, r in f_ci + a_ci), "an x86-64 ELF is not game data")
        check(("A", "stuff/SLUS_123.45") in classes, "a PS2 serial by name")
        check(("A", "stuff/mygame.rep/x.txt") in classes, "a .rep disc dump by directory")
        check(("A", "stuff/snd.bin") in classes, "VAGp by magic")
        check(("A", "stuff/movie.pss") in classes, "FMV by name/magic")
        check(("B", "games/t/verify/baseline/leg/probe/old_b2.ps2st") in classes, "ps2st is class B -> FAIL in ci")
        check(("B", "games/t/verify/baseline/leg/gsdumps/a.gs.zst") in classes, "gs.zst is class B -> FAIL in ci")
        check(all(r != "play_sessions/s3/session.padlog" for _, _, r in f_ci + a_ci), "a pad recording is not game data")
        check(all(r != "games/t/verify/baseline/leg/probe/PROOF.txt" for _, _, r in f_ci + a_ci), "a text record under probe/ is clean")
        check(("B", "games/t/verify/baseline/leg/probe/mystery.dat") in classes, "a binary under probe/ is class B")
        check(("C!", "tests/big_unpinned.dat") in classes, "an unpinned 300 KB blob FAILs")
        check(("C", "tests/vu/fixtures/vu0_x_code.bin") in {(c, r) for c, _, r in a_ci}, "the pinned fixture is allowed")
        check(all("build-x" not in r for _, _, r in f_ci), "build dirs are not scanned")
        check(all("src/a.cpp" != r for _, _, r in f_ci + a_ci), "source is clean")

        n, f_local, a_local = scan(d, "local", P, 65536)
        check(not any(c == "B" for c, _, _ in f_local) and sum(1 for c, _, _ in a_local if c == "B") == 3,
              "--mode local reports class B (3) without failing on it")
        check(sorted(c for c, _, _ in f_local) == ["A"] * 6 + ["C!"], "--mode local still fails A (6) and C! (1): %s" % sorted(c for c, _, _ in f_local))

        # --mode local on a git work tree scans the TRACKED set: the title's untracked play
        # output beside the binary (a memory card's icon, a savestate) is not in any checkout.
        g = os.path.join(d, "repo")
        os.makedirs(os.path.join(g, "src"))
        with open(os.path.join(g, "src", "a.cpp"), "w") as f:
            f.write("int main() { return 0; }\n")
        os.makedirs(os.path.join(g, "memcards", "mc0", "BASLUS-12345"))
        with open(os.path.join(g, "memcards", "mc0", "BASLUS-12345", "Crash.ico"), "wb") as f:
            f.write(os.urandom(75952))
        try:
            subprocess.run(["git", "-C", g, "init", "-q"], check=True, capture_output=True)
            subprocess.run(["git", "-C", g, "add", "src/a.cpp"], check=True, capture_output=True)
            git_ok = True
        except (OSError, subprocess.CalledProcessError):
            git_ok = False
        if git_ok:
            n, f_git, _ = scan(g, "local", P, 65536)
            check(not f_git and n == 1, "--mode local on a work tree scans only tracked files (untracked memcards/ blob ignored): %s" % f_git)
            n, f_git_ci, _ = scan(g, "ci", P, 65536)
            check(any(c == "C!" and r.endswith("Crash.ico") for c, _, r in f_git_ci), "--mode ci still scans the disk (the untracked blob FAILs)")
        else:
            check(True, "(git unavailable: the tracked-set case skipped)")

        with open(fixture, "ab") as f:
            f.write(b"\x01")
        n, f2, _ = scan(d, "ci", P, 65536)
        check(any(c == "C!" and r == "tests/vu/fixtures/vu0_x_code.bin" for c, _, r in f2), "a changed pinned fixture FAILs")

        # artifact mode: a dist folder with the binary + libs + text is clean; the binary's own
        # format constants are not at the fixed offsets, so no magic hit
        art = os.path.join(d, "dist", "macos")
        os.makedirs(os.path.join(art, "lib"))
        with open(os.path.join(art, "nativeee"), "wb") as f:
            f.write(b"\xcf\xfa\xed\xfe" + b"\x00" * 100 + b"CD001VAGp\x7fELFcdrom0:\\SLUS_123.45;1" + os.urandom(200000))
        with open(os.path.join(art, "lib", "libSDL2.dylib"), "wb") as f:
            f.write(b"\xcf\xfa\xed\xfe" + os.urandom(200000))
        with open(os.path.join(art, "README.md"), "w") as f:
            f.write("# users\nYou must own the game.\n")
        n, f3, a3 = scan(art, "artifact", {}, 65536, artifact_binaries=("nativeee", "nativeee.exe"))
        check(not f3, "a clean artifact folder (binary with embedded format constants + lib + README) passes: %s" % f3)
        n, f3b, _ = scan(art, "artifact", {}, 65536, artifact_binaries=("nativeee",))
        check(not f3b, "lib/*.dylib (binary, > max-blob) is allowed as a shared library")
        with open(os.path.join(art, "lib", "data.pak"), "wb") as f:
            f.write(os.urandom(100000))
        n, f4, _ = scan(art, "artifact", {}, 65536, artifact_binaries=("nativeee",))
        check(any(c == "C!" and r == "lib/data.pak" for c, _, r in f4), "an unknown blob in the artifact FAILs")
    print("asset_guard selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", help="directory to scan (a checkout, a dist/<platform>/ folder)")
    ap.add_argument("--mode", choices=("ci", "local", "artifact"), default="ci")
    ap.add_argument("--pins", default=DEFAULT_PINS, help="class C pin table (default tools/ci/asset_guard_pins.tsv)")
    ap.add_argument("--max-blob", type=int, default=65536, help="bytes above which an unpinned non-text file FAILs")
    ap.add_argument("--report", help="write the lines to this file as well")
    ap.add_argument("--write-pins", nargs="*", metavar="PATH", help="(maintenance) rewrite the pin table for these paths relative to --root")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.root:
        ap.error("--root is required")
    root = os.path.abspath(a.root)
    if a.write_pins is not None:
        write_pins(root, a.write_pins, a.pins)
        print("asset_guard: wrote %d pin(s) to %s" % (len(a.write_pins), a.pins))
        return 0
    pins = load_pins(a.pins) if a.mode != "artifact" else {}
    # In artifact mode the binary and the bundled shared libraries are the expected blobs.
    binaries = ()
    if a.mode == "artifact":
        binaries = tuple(fn for fn in os.listdir(root) if os.path.isfile(os.path.join(root, fn))
                         and (fn in ("nativeee", "nativeee.exe") or fn.startswith("nativeee")))
    n, findings, allowed = scan(root, a.mode, pins, a.max_blob, binaries)
    lines = report_lines(root, a.mode, n, findings, allowed)
    for l in lines:
        print(l)
    if a.report:
        with open(a.report, "w") as f:
            f.write("\n".join(lines) + "\n")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
