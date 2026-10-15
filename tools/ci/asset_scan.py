#!/usr/bin/env python3
"""asset_scan.py -- the release-artifact asset guard (Charter 2, Track C1).

We ship NO game data: the artifact is the recompiled code + the native engine and its
bundled runtime libraries, nothing else. This scan is the authority that an artifact
(a staged folder, a .tar.gz / .zip, or any single file such as an AppImage) carries none
of the title's bytes, by three independent checks:

  1. NAMES      file names / extensions that are disc images, PS2 executables, IOP modules
                or the title's asset formats (.iso .bin .img .cue .mdf .nrg .elf .irx .pss
                .vag .bh .bd .rm2 .mb .bin ..., SLUS_*/SLES_*/SCUS_*, IOPRP*, a disc-dump directory ...).
  2. MAGIC      structural signatures at the offsets the formats define (never a free
                substring search, which the launcher's own ISO code would trip): ISO 9660
                primary volume descriptor "CD001" at 0x8001 / 0x9311 (raw 2352-byte sectors),
                a MIPS ELF (e_machine 8 -- the title's EE ELF and every IRX), an MPEG program
                stream (00 00 01 BA at offset 0 -- the .pss FMVs), a VAG ("VAGp") and a PS2
                memory-card image ("Sony PS2 Memory Card Format" at offset 0).
  3. WINDOWS    byte windows of the title ELF searched INSIDE every file (tar /
                zip members included): no copy of the game's code or data, whole or in part,
                may be embedded in the artifact. The windows come from `--make-windows --elf`
                (local, where the game files exist; the ELF md5 is gated) and are stored as
                an 8-byte anchor + the SHA-256 of the 64-byte window in asset_scan_windows.json
                -- the scan needs no game files, so CI runs it. With `--elf` the full live
                windows are searched as well (the stronger, local form).

  asset_scan.py <artifact> [<artifact> ...] [--windows FILE] [--elf SLUS_123.45] [--report MD]
  asset_scan.py --tree <dir> [--windows FILE] [--elf SLUS_123.45] [--report MD]
  asset_scan.py --make-windows --elf <SLUS_123.45> [--windows FILE] [--count N] [--size N]
  asset_scan.py --selftest

--tree <dir> is the SOURCE-TREE gate (the public repository carries no game bytes either): it
walks the whole tree (skipping .git, build*/, out/, dist/, caches) and applies the three checks
above to every file, plus

  4. IMAGES     any raster image (.png .ppm .pgm .tga .bmp .jpg .gif .webp .tif .dds ...) outside
                TREE_IMAGE_ALLOWLIST -- the game-derived oracle shots (console / PCSX2 screenshots,
                title-screen captures) live outside the tree; a non-game image (a docs icon) must
                be allowlisted with a reason;
  5. SIZE       any file above TREE_SIZE_LIMIT (1 MB) outside TREE_SIZE_ALLOWLIST (each entry
                carries its reason: generated recompiler output, the transplanted tool source,
                the phase logs);
  6. ARRAYS     a text file holding a comma-separated run of >= TREE_ARRAY_MIN_LITERALS hex
                literals (a dumped VU microprogram, texture page or RAM image pasted as a C array)
                outside TREE_ARRAY_ALLOWLIST (synthetic PNG vectors, the generated entry table).

The names check in tree mode honours TREE_NAME_ALLOWLIST (tests/oracle/og_tables.bin: the GS
hardware swizzle tables, not game data). `python3 tools/ci/asset_scan.py --tree .` must exit 0
on the public tree; .github/workflows/ci.yml runs it as the first gate.

Exit 0 = clean; 1 = a finding; 2 = usage / unreadable input. Every finding names the file
(and member), the check and the evidence. Never a result the operator did not read: the
report lists what was scanned (files, bytes, windows) even when clean.
"""
import argparse
import fnmatch
import hashlib
import io
import json
import os
import re
import struct
import subprocess
import sys
import tarfile
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
# The byte-window fingerprint is derived from a title's ELF, so it is TITLE data and lives
# beside the title, never in tools/. Generate one with --make-windows --elf <your ELF>.
ROOT = os.path.dirname(os.path.dirname(HERE))
def default_windows():
    title = os.environ.get("PS2R_TITLE")
    games = os.path.join(ROOT, "games")
    if not title and os.path.isdir(games):
        found = [d for d in sorted(os.listdir(games))
                 if os.path.isdir(os.path.join(games, d, "hle"))]
        title = found[0] if len(found) == 1 else None
    return os.path.join(games, title, "asset_scan_windows.json") if title else ""

# --- 1. names ----------------------------------------------------------------------------
BAD_EXTENSIONS = {
    ".iso", ".bin", ".img", ".cue", ".mdf", ".mds", ".nrg", ".ccd", ".cso", ".chd", ".gz.iso",
    ".elf", ".irx", ".pss", ".vag", ".bh", ".bd", ".rm2", ".mb", ".hdr", ".ipu", ".ps2st",
    ".gs", ".gsdump", ".mc2", ".ps2", ".psu", ".max", ".cbs", ".xps", ".sps",
}
BAD_NAME_PREFIXES = ("SLUS_", "SLES_", "SCUS_", "SCES_", "SLPM_", "SCPS_", "SLPS_", "IOPRP",
                     "SYSTEM.CNF")
# game_files/ is where TitleInputs puts extracted inputs; ".rep" is this project's disc-dump
# directory convention. A title with other directory names adds them to its own guard config.
BAD_PATH_PARTS = ("game_files", ".rep", "extracted_particles")
# Files that are legitimately part of the artifact and share an extension with the list above.
# None today: the artifact's own files are nativeee[.exe], lib/*.so*, *.dll, *.pdb, BUILD-ID,
# README.md, LICENSE, THIRD_PARTY_NOTICES.md.


def name_findings(path_in_artifact):
    base = os.path.basename(path_in_artifact)
    lower = base.lower()
    out = []
    for ext in BAD_EXTENSIONS:
        if lower.endswith(ext):
            out.append(("names", "extension %s" % ext))
            break
    for prefix in BAD_NAME_PREFIXES:
        if base.upper().startswith(prefix):
            out.append(("names", "name prefix %s" % prefix))
            break
    for part in path_in_artifact.replace("\\", "/").lower().split("/")[:-1]:
        if part in BAD_PATH_PARTS:
            out.append(("names", "path component %s" % part))
            break
    return out


# --- 2. magic ------------------------------------------------------------------------------
def magic_findings(data):
    out = []
    if len(data) >= 0x8006 and data[0x8001:0x8006] == b"CD001":
        out.append(("magic", "ISO 9660 primary volume descriptor at 0x8001 (a disc image)"))
    if len(data) >= 0x9316 and data[0x9311:0x9316] == b"CD001":
        out.append(("magic", "ISO 9660 volume descriptor at 0x9311 (a raw 2352-byte-sector image)"))
    if len(data) >= 0x14 and data[:4] == b"\x7fELF":
        e_machine = struct.unpack_from("<H", data, 0x12)[0] if data[5] == 1 else \
            struct.unpack_from(">H", data, 0x12)[0]
        if e_machine == 8:
            out.append(("magic", "MIPS ELF (e_machine 8: a PS2 EE executable or an IOP module)"))
    if len(data) >= 4 and data[:4] == b"\x00\x00\x01\xba":
        out.append(("magic", "MPEG program stream pack header at 0 (a .pss FMV)"))
    if len(data) >= 4 and data[:4] == b"VAGp":
        out.append(("magic", "VAG ADPCM header at 0"))
    if data[:28] == b"Sony PS2 Memory Card Format ":
        out.append(("magic", "PS2 memory card image header at 0"))
    return out


# --- 3. ELF byte windows -------------------------------------------------------------------
def elf_md5(data):
    return hashlib.md5(data).hexdigest()


def choose_windows(elf, count, size, seed_stride=None):
    """`count` windows of `size` bytes spread over the ELF, each with enough byte diversity
    that an accidental 8-byte anchor match in an unrelated binary is implausible."""
    windows = []
    if len(elf) < size:
        return windows
    stride = seed_stride or max(size, (len(elf) - size) // max(1, count))
    at = 0
    while at + size <= len(elf) and len(windows) < count:
        w = elf[at:at + size]
        anchor = w[:8]
        if len(set(w)) >= 28 and len(set(anchor)) >= 6 and anchor != b"\0" * 8:
            windows.append({"offset": at, "anchor": anchor.hex(),
                            "sha256": hashlib.sha256(w).hexdigest(), "size": size})
            at += stride
        else:
            at += 16   # skip padding / zero runs / low-entropy tables, look again nearby
    return windows


def make_windows(elf_path, out_path, count, size):
    with open(elf_path, "rb") as f:
        elf = f.read()
    got = elf_md5(elf)
    windows = choose_windows(elf, count, size)
    doc = {"source": os.path.basename(elf_path), "elf_md5": got, "elf_bytes": len(elf),
           "window_size": size, "count": len(windows),
           "note": "8-byte anchors + SHA-256 of each window; the scan searches every anchor in "
                   "every artifact file and hashes the window at each hit. Regenerate with "
                   "tools/ci/asset_scan.py --make-windows --elf <SLUS_123.45>.",
           "windows": windows}
    with open(out_path, "w") as f:
        json.dump(doc, f, indent=1)
    print("asset_scan: wrote %d windows of %d bytes (elf %d bytes, md5 %s) -> %s"
          % (len(windows), size, len(elf), got, out_path))


def load_windows(path):
    if not os.path.isfile(path):
        return None
    with open(path) as f:
        doc = json.load(f)
    return [(bytes.fromhex(w["anchor"]), w["sha256"], w["size"], w["offset"])
            for w in doc.get("windows", [])]


def window_findings(data, windows, live_elf=None):
    """Anchor search at C speed (bytes.find), then the window hash at every hit; with the
    live ELF, the full windows are searched too (local runs only)."""
    out = []
    for anchor, digest, size, offset in windows:
        start = 0
        while True:
            hit = data.find(anchor, start)
            if hit < 0:
                break
            if hashlib.sha256(data[hit:hit + size]).hexdigest() == digest:
                out.append(("windows", "ELF window @ELF+0x%x (%d bytes) found at file offset 0x%x"
                            % (offset, size, hit)))
                break
            start = hit + 1
        if live_elf is not None:
            w = live_elf[offset:offset + size]
            hit = data.find(w)
            if hit >= 0 and not any(f[1].startswith("ELF window @ELF+0x%x " % offset) for f in out):
                out.append(("windows", "ELF window @ELF+0x%x (%d bytes, live) found at file offset 0x%x"
                            % (offset, size, hit)))
    return out


# --- 4-6. the source-tree checks (--tree) -------------------------------------------------
# Directories never walked in tree mode: VCS, build / staging outputs, caches.
TREE_SKIP_DIRS = {".git", "__pycache__", ".cache", "node_modules", "vcpkg", "vcpkg-cache", ".vs", ".idea"}
TREE_SKIP_DIR_RE = re.compile(r"^(build|build[-_][A-Za-z0-9_.-]+|out|dist|CMakeFiles|generated)$")

# Raster image extensions: a screenshot of the running game, a texture dump, a GS frame grab.
TREE_IMAGE_EXTENSIONS = (".png", ".ppm", ".pgm", ".pbm", ".pnm", ".tga", ".bmp", ".jpg", ".jpeg", ".gif",
                         ".webp", ".tif", ".tiff", ".dds", ".ico", ".icns", ".psd", ".exr", ".hdr")
# Non-game images the tree may carry: (fnmatch pattern, the reason). EMPTY today -- the tree has
# no icons or diagrams in raster form. Add an entry per image with a justification, never a glob
# over a directory that could receive a game shot:
#   ("docs/img/logo.png", "the project logo, drawn for this repository"),
TREE_IMAGE_ALLOWLIST = [
]

TREE_SIZE_LIMIT = 1 << 20   # 1 MB: nothing hand-written is that large; dumps and images are
# Files above the limit the tree may carry: (fnmatch pattern, the reason).
TREE_SIZE_ALLOWLIST = [
    # The recompiler's generated output: the title's EE / IOP code translated to C++ (text; it holds
    # no raw ELF bytes -- the windows check scans these files like every other). Whether the public
    # tree carries them is the recompiled-TU stage's decision; their size is not a data signal.
    ("games/*/recomp/recompiled*.cpp", "generated recompiler output (C++ text; windows-checked)"),
    ("games/*/recomp/recompiled.h", "generated recompiler output (declarations)"),
    ("games/*/recomp/continuations.h", "generated entry-address switch table (code addresses only)"),
    ("games/*/iop/stream_irx/iop_recompiled.cpp", "generated IOP recompiler output (C++ text; windows-checked)"),
    # The transplanted recompiler tool: 1.6 MB of C++ source (tools/recompiler_v2/PROVENANCE.md).
    ("tools/recompiler_v2/recompiler_tool/recompiler.cpp", "the recompiler tool's source (PROVENANCE.md)"),
    ("tools/recompiler_v2/include/json.hpp", "vendored nlohmann/json single header"),
    # Phase implementation logs: prose that grew past 1 MB by accretion over the project's life.
    ("docs/phases/*.md", "phase implementation log (prose)"),
    # The dear imgui amalgamation (vendored, byte-identical to its release).
    ("app/gui/third_party/imgui/*", "vendored Dear ImGui source"),
]

# Comma-separated hex-literal runs this long are a byte dump pasted as a C array (a 4 KB VU
# microprogram is 1024 u32 / 4096 u8 literals; a texture page more); the allowlist names the two
# tables that are legitimately that long.
TREE_ARRAY_MIN_LITERALS = 512
TREE_ARRAY_TEXT_EXTENSIONS = (".h", ".hpp", ".hh", ".inc", ".inl", ".c", ".cc", ".cpp", ".cxx", ".py", ".json",
                              ".txt", ".tsv", ".csv", ".md", ".cmake", ".js", ".ts", ".rs", ".go", ".java", ".cs",
                              ".s", ".asm", ".glsl", ".vert", ".frag", ".ini", ".toml", ".yml", ".yaml", ".xml")
TREE_ARRAY_RE = re.compile(rb"(?:0[xX][0-9a-fA-F]{1,16}[uUlL]{0,3}\s*,\s*){%d,}" % TREE_ARRAY_MIN_LITERALS)
TREE_ARRAY_ALLOWLIST = [
    # Generated by the script in tests/core/test_png.cpp's header comment: Python zlib streams over
    # hand-filtered synthetic rows (37x23 and smaller images) + their expected RGBA; no game pixel.
    ("tests/core/png_vectors.h", "synthetic PNG decode vectors (generated, see tests/core/test_png.cpp)"),
    # The recompiler's entry-address table: a monotonic list of the title's code addresses
    # (0x100000u, 0x100008u, ...), not data bytes. Its presence is the recompiled-TU stage's call.
    ("games/*/recomp/continuations.h", "generated entry-address table (code addresses, no data bytes)"),
]

# Names the artifact check rejects by extension that the TREE may carry, each with its proof.
TREE_NAME_ALLOWLIST = [
    # The PS2 GS hardware block / column swizzle tables (PSMCT32 block table {0,1,4,5,16,17,20,21,
    # 2,3,6,7,...}, columnTable32 {0,1,4,5,8,9,12,13,...}: GS User's Manual, PCSX2 GSLocalMemory)
    # lifted from the previous project's SOURCE by tests/oracle/lift_og_tables.py -- hardware
    # constants, not bytes of the title. Pinned by size + sha256 in tools/ci/asset_guard_pins.tsv.
    ("tests/oracle/og_tables.bin", "GS hardware swizzle tables (lift_og_tables.py), not game data"),
]


def allowlisted(rel, table):
    """The reason when `rel` (posix, tree-relative) matches an allowlist entry, else None."""
    for pattern, reason in table:
        if fnmatch.fnmatchcase(rel, pattern):
            return reason
    return None


def tree_extra_findings(rel, data):
    """Checks 4-6 for one tree file (names/magic/windows are applied by the caller)."""
    out = []
    lower = rel.lower()
    if lower.endswith(TREE_IMAGE_EXTENSIONS) and allowlisted(rel, TREE_IMAGE_ALLOWLIST) is None:
        out.append(("images", "raster image %s outside TREE_IMAGE_ALLOWLIST (a game shot / texture dump?)"
                    % os.path.splitext(rel)[1]))
    if len(data) > TREE_SIZE_LIMIT and allowlisted(rel, TREE_SIZE_ALLOWLIST) is None:
        out.append(("size", "%d bytes > %d (TREE_SIZE_LIMIT) and not in TREE_SIZE_ALLOWLIST" % (len(data), TREE_SIZE_LIMIT)))
    if lower.endswith(TREE_ARRAY_TEXT_EXTENSIONS) and allowlisted(rel, TREE_ARRAY_ALLOWLIST) is None:
        m = TREE_ARRAY_RE.search(data)
        if m:
            out.append(("arrays", "a run of %d hex literals at offset 0x%x (a dumped byte array?) not in TREE_ARRAY_ALLOWLIST"
                        % (m.group(0).count(b","), m.start())))
    return out


def git_tracked_files(root):
    """The files git would commit/ship from root: tracked + untracked-but-not-ignored. None when
    root is not a git work tree (or git is unavailable). This is the authoritative set -- the
    directory-name skips below must never hide a tracked file (e.g. tools/build/, tools/dist/)."""
    if not os.path.exists(os.path.join(root, ".git")):
        return None
    try:
        out = subprocess.run(["git", "-C", root, "ls-files", "-z", "--cached", "--others",
                              "--exclude-standard"], check=True, capture_output=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    return sorted({p.decode("utf-8", "surrogateescape") for p in out.split(b"\0") if p})


def iter_tree(root):
    """Yields (tree-relative posix path, bytes) for every regular file under root. In a git work
    tree: exactly the files git would ship (tracked + untracked-not-ignored), no name-based skips.
    Otherwise: an os.walk skipping TREE_SKIP_DIRS / TREE_SKIP_DIR_RE directories and symlinks.
    Sorted, deterministic."""
    root = os.path.abspath(root)
    tracked = git_tracked_files(root)
    if tracked is not None:
        for rel in tracked:
            p = os.path.join(root, rel)
            if os.path.islink(p) or not os.path.isfile(p):
                continue
            with open(p, "rb") as f:
                yield rel.replace(os.sep, "/"), f.read()
        return
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if d not in TREE_SKIP_DIRS and not TREE_SKIP_DIR_RE.match(d)
                             and not os.path.islink(os.path.join(dirpath, d)))
        for name in sorted(filenames):
            p = os.path.join(dirpath, name)
            if os.path.islink(p) or not os.path.isfile(p):
                continue
            with open(p, "rb") as f:
                yield os.path.relpath(p, root).replace(os.sep, "/"), f.read()


def scan_tree(root, windows, live_elf=None):
    if not os.path.isdir(root):
        sys.exit("asset_scan: --tree needs a directory: %s" % root)
    findings, files, total, allowed = [], 0, 0, []
    for rel, data in iter_tree(root):
        files += 1
        total += len(data)
        names = name_findings(rel)
        if names:
            reason = allowlisted(rel, TREE_NAME_ALLOWLIST)
            if reason is not None:
                allowed.append((rel, "names", reason))
                names = []
        for check, evidence in (names + magic_findings(data) + window_findings(data, windows or [], live_elf) +
                                tree_extra_findings(rel, data)):
            findings.append((rel, check, evidence))
    return findings, files, total, allowed


# --- walking the artifact -----------------------------------------------------------------
def iter_members(artifact):
    """Yields (display_path, bytes) for every file in a folder, tar(.gz/.xz/.bz2), zip or
    a single file. Nested archives inside an artifact are opened one level deep."""
    if os.path.isdir(artifact):
        for root, _dirs, files in os.walk(artifact):
            for name in sorted(files):
                p = os.path.join(root, name)
                if os.path.islink(p):
                    continue
                with open(p, "rb") as f:
                    yield os.path.relpath(p, artifact), f.read()
        return
    if tarfile.is_tarfile(artifact):
        with tarfile.open(artifact) as tf:
            for m in tf:
                if m.isfile():
                    yield "%s::%s" % (os.path.basename(artifact), m.name), tf.extractfile(m).read()
        return
    if zipfile.is_zipfile(artifact):
        with zipfile.ZipFile(artifact) as zf:
            for info in zf.infolist():
                if not info.is_dir():
                    yield "%s::%s" % (os.path.basename(artifact), info.filename), zf.read(info)
        return
    with open(artifact, "rb") as f:
        yield os.path.basename(artifact), f.read()


def scan(artifacts, windows, live_elf=None):
    findings, files, total = [], 0, 0
    for artifact in artifacts:
        if not os.path.exists(artifact):
            sys.exit("asset_scan: no such artifact: %s" % artifact)
        for path, data in iter_members(artifact):
            files += 1
            total += len(data)
            for check, evidence in (name_findings(path) + magic_findings(data) +
                                    window_findings(data, windows or [], live_elf)):
                findings.append((path, check, evidence))
            # one level of nested archive (a tar inside a zip, an AppImage's payload is squashfs
            # and is scanned as raw bytes above)
            if path.lower().endswith((".tar", ".tar.gz", ".tgz", ".zip")) and len(data) < (1 << 31):
                with tempfile.NamedTemporaryFile(suffix=os.path.splitext(path)[1], delete=False) as tmp:
                    tmp.write(data)
                try:
                    for inner, inner_data in iter_members(tmp.name):
                        files += 1
                        total += len(inner_data)
                        for check, evidence in (name_findings(inner) + magic_findings(inner_data) +
                                                window_findings(inner_data, windows or [], live_elf)):
                            findings.append((path + "::" + inner, check, evidence))
                finally:
                    os.unlink(tmp.name)
    return findings, files, total


def selftest():
    fails = 0
    def expect(cond, what):
        nonlocal fails
        if not cond:
            fails += 1
            print("selftest FAIL:", what)
    # names
    expect(name_findings("dist/x/SLUS_123.45"), "SLUS_ prefix")
    expect(name_findings("a/b/game.iso"), ".iso")
    expect(name_findings("game_files/DATA.BH"), "path component + name")
    expect(not name_findings("dist/linux-x86_64/nativeee"), "binary clean")
    expect(not name_findings("dist/linux-x86_64/lib/libSDL2-2.0.so.0"), "lib clean")
    expect(not name_findings("THIRD_PARTY_NOTICES.md"), "notices clean")
    # magic
    iso = bytearray(0x8010); iso[0x8001:0x8006] = b"CD001"
    expect(magic_findings(bytes(iso)), "ISO 9660")
    mips = bytearray(b"\x7fELF\x01\x01\x01" + b"\0" * 9 + struct.pack("<HH", 2, 8) + b"\0" * 40)
    expect(magic_findings(bytes(mips)), "MIPS ELF")
    x86 = bytearray(mips); x86[0x12:0x14] = struct.pack("<H", 62)
    expect(not magic_findings(bytes(x86)), "x86-64 ELF is fine")
    expect(magic_findings(b"\x00\x00\x01\xba" + b"\0" * 16), "PSS")
    expect(magic_findings(b"VAGp" + b"\0" * 16), "VAG")
    expect(not magic_findings(b"CD001 in a string table is not an ISO"), "free substring ignored")
    # windows
    fake_elf = bytes((i * 37 + (i >> 5)) & 0xff for i in range(8192))
    ws = choose_windows(fake_elf, 4, 64)
    expect(len(ws) == 4, "4 windows chosen (%d)" % len(ws))
    table = [(bytes.fromhex(w["anchor"]), w["sha256"], w["size"], w["offset"]) for w in ws]
    carrier = b"host code " * 50 + fake_elf[ws[1]["offset"]:ws[1]["offset"] + 64] + b" more host"
    expect(window_findings(carrier, table), "embedded window found")
    expect(not window_findings(b"host code only " * 100, table), "clean carrier")
    # the anchor alone (first 8 bytes) without the rest of the window is NOT a finding
    expect(not window_findings(b"x" * 10 + fake_elf[ws[0]["offset"]:ws[0]["offset"] + 8] + b"y" * 100, table),
           "anchor without window is not a finding")
    # archives
    with tempfile.TemporaryDirectory() as td:
        tar_path = os.path.join(td, "art.tar.gz")
        with tarfile.open(tar_path, "w:gz") as tf:
            info = tarfile.TarInfo("dist/nativeee"); payload = b"\x7fELF" + b"\0" * 60
            info.size = len(payload); tf.addfile(info, io.BytesIO(payload))
            info = tarfile.TarInfo("dist/data/level.iso"); payload = bytes(iso)
            info.size = len(payload); tf.addfile(info, io.BytesIO(payload))
        findings, files, _ = scan([tar_path], table)
        expect(files == 2, "2 members (%d)" % files)
        expect(any(f[1] == "names" for f in findings) and any(f[1] == "magic" for f in findings),
               "tar member flagged by names and magic")
    # tree mode
    with tempfile.TemporaryDirectory() as td:
        def put(rel, payload):
            p = os.path.join(td, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "wb") as f:
                f.write(payload)
        put("engine/core/a.cpp", b"int main() { return 0; }\n")
        put("docs/README.md", b"# fine\n")
        put("build-release/out.iso", bytes(iso))                    # skipped: a build dir
        put(".git/objects/aa", bytes(iso))                           # skipped: VCS
        put("tests/oracle/og_tables.bin", b"OGT1" + b"\0" * 32)      # names hit, allowlisted
        findings, files, _total, allowed = scan_tree(td, table)
        expect(files == 3, "tree: 3 files walked, build-*/ and .git skipped (%d)" % files)
        expect(not findings, "tree: a clean tree has no findings (%r)" % findings)
        expect(any(a[0] == "tests/oracle/og_tables.bin" for a in allowed), "tree: og_tables.bin allowlisted by name")
        put("games/t/verify/baseline/leg/gsdumps/x_shot.ppm", b"P6\n1 1\n255\n\0\0\0")
        put("games/t/verify/baseline/leg/probe/old_b2.ps2st", b"\0" * 64)
        put("big/blob.dat", b"\xaa" * (TREE_SIZE_LIMIT + 1))
        put("engine/vu/dump.inc", b"static const uint32_t k[] = {" + b"0x12345678u, " * (TREE_ARRAY_MIN_LITERALS + 1) + b"};\n")
        put("engine/vu/small_table.inc", b"static const uint32_t k[] = {" + b"0x12345678u, " * 64 + b"};\n")
        put("tests/core/png_vectors.h", b"static const unsigned char k[] = {" + b"0x89, " * (TREE_ARRAY_MIN_LITERALS + 1) + b"};\n")
        put("docs/phases/HUGE.md", b"# log\n" + b"x" * (TREE_SIZE_LIMIT + 1))
        put("carrier/host.bin.txt", b"host " * 20 + fake_elf[ws[1]["offset"]:ws[1]["offset"] + 64])
        findings, files, _total, allowed = scan_tree(td, table)
        kinds = {(f[0], f[1]) for f in findings}
        expect(("games/t/verify/baseline/leg/gsdumps/x_shot.ppm", "images") in kinds, "tree: a .ppm outside the allowlist")
        expect(("games/t/verify/baseline/leg/probe/old_b2.ps2st", "names") in kinds, "tree: a .ps2st by name")
        expect(("big/blob.dat", "size") in kinds, "tree: > 1 MB flagged")
        expect(("engine/vu/dump.inc", "arrays") in kinds, "tree: a %d-literal array flagged" % (TREE_ARRAY_MIN_LITERALS + 1))
        expect(not any(f[0] == "engine/vu/small_table.inc" for f in findings), "tree: a 64-entry table is fine")
        expect(not any(f[0] == "tests/core/png_vectors.h" for f in findings), "tree: png_vectors.h allowlisted (arrays)")
        expect(not any(f[0] == "docs/phases/HUGE.md" for f in findings), "tree: docs/phases/*.md allowlisted (size)")
        expect(("carrier/host.bin.txt", "windows") in kinds, "tree: an embedded ELF window flagged")
        expect(allowlisted("games/x/recomp/recompiled_shard_003.cpp", TREE_SIZE_ALLOWLIST), "size allowlist: generated shards")
        expect(not allowlisted("games/x/verify/baseline/a/probe/old_b2.ps2st", TREE_SIZE_ALLOWLIST), "size allowlist: a probe is not")
    print("asset_scan selftest: %s" % ("PASS" if not fails else "%d FAIL" % fails))
    return 1 if fails else 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("artifacts", nargs="*")
    ap.add_argument("--tree", metavar="DIR", help="scan a whole source tree (checks 1-6; skips .git, build*/)")
    ap.add_argument("--windows", default=default_windows())
    ap.add_argument("--elf-md5", help="refuse an --elf whose md5 differs (the title records its own)")
    ap.add_argument("--elf", help="the NTSC ELF (local only): --make-windows source, and live windows")
    ap.add_argument("--make-windows", action="store_true")
    ap.add_argument("--count", type=int, default=128)
    ap.add_argument("--size", type=int, default=64)
    ap.add_argument("--report", help="write a markdown report here as well")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if a.make_windows:
        if not a.elf:
            ap.error("--make-windows needs --elf")
        make_windows(a.elf, a.windows, a.count, a.size)
        return 0
    if not a.artifacts and not a.tree:
        ap.error("an artifact path or --tree <dir> is required")
    if a.artifacts and a.tree:
        ap.error("--tree and artifact paths are exclusive")
    windows = load_windows(a.windows)
    live_elf = None
    if a.elf:
        with open(a.elf, "rb") as f:
            live_elf = f.read()
        if a.elf_md5 and elf_md5(live_elf) != a.elf_md5:
            sys.exit("asset_scan: --elf md5 %s != --elf-md5 %s" % (elf_md5(live_elf), a.elf_md5))
        if windows is None:
            windows = [(bytes.fromhex(w["anchor"]), w["sha256"], w["size"], w["offset"])
                       for w in choose_windows(live_elf, a.count, a.size)]
    if windows is None:
        print("asset_scan: WARNING no windows file at %s (checks 1-2 only)" % a.windows)
    if a.tree:
        findings, files, total, allowed = scan_tree(a.tree, windows, live_elf)
        what, subject = "tree %s" % os.path.abspath(a.tree), "the tree"
        checks = ("checks: names (extensions/prefixes/paths), magic (ISO9660/MIPS ELF/PSS/VAG/MC), "
                  "windows (NTSC ELF byte windows inside every file), images (raster files outside the "
                  "allowlist), size (> %d bytes outside the allowlist), arrays (>= %d-literal hex runs "
                  "outside the allowlist)" % (TREE_SIZE_LIMIT, TREE_ARRAY_MIN_LITERALS))
    else:
        findings, files, total = scan(a.artifacts, windows, live_elf)
        allowed = []
        what, subject = ", ".join(a.artifacts), "the artifact"
        checks = ("checks: names (extensions/prefixes/paths), magic (ISO9660/MIPS ELF/PSS/VAG/MC), "
                  "windows (NTSC ELF byte windows inside every file)")
    lines = ["# asset scan -- %s" % what, "",
             "files scanned: %d, bytes: %d, ELF windows: %d%s" % (
                 files, total, len(windows or []), " (+ live ELF)" if live_elf is not None else ""),
             checks, ""]
    if allowed:
        lines.append("allowlisted (%d):" % len(allowed))
        lines += ["- %s: %s -- %s" % x for x in allowed]
        lines.append("")
    if findings:
        lines.append("FINDINGS (%d):" % len(findings))
        lines += ["- %s: %s -- %s" % f for f in findings]
        verdict = "asset_scan: FAIL -- %d finding(s); %s carries game data" % (len(findings), subject)
    else:
        verdict = "asset_scan: CLEAN -- %d files, %d bytes, 0 findings" % (files, total)
    lines += ["", verdict]
    text = "\n".join(lines)
    print(text)
    if a.report:
        with open(a.report, "w") as f:
            f.write(text + "\n")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
