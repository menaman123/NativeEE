#!/usr/bin/env python3
"""lint_artifact_licenses.py -- the artifact's licence set is complete and consistent (Charter 2 C1).

  lint_artifact_licenses.py --root <repo>                  the committed set + the recipes
  lint_artifact_licenses.py --root <repo> --dist <dir>     ... and a staged dist/<platform>/ folder
  lint_artifact_licenses.py --selftest

Checks (FAIL on any):
  committed set   tools/dist/licenses/<file> exists for every REQUIRED name and its SHA-256 equals
                  the one PROVENANCE.md pins (no silently edited licence text); imgui-LICENSE.txt is
                  byte-equal to app/gui/third_party/imgui/LICENSE.txt; imgl3w-UNLICENSE.txt's
                  dedication sentence appears in imgui_impl_opengl3_loader.h (the loader we ship IS
                  the one the text covers).
  FFmpeg recipe   tools/ci/ffmpeg_static.sh --print-configure names --disable-gpl, --disable-nonfree and
                  --disable-version3 and no --enable-gpl / --enable-nonfree / --enable-version3 /
                  --enable-lib* (an external library would be a licence of its own); the tag it names
                  is the tag PROVENANCE.md names for the FFmpeg texts.
  stagers         build.sh and build.ps1 copy every platform-required name from tools/dist/licenses,
                  generate FFmpeg-BUILD-CONFIGURATION.txt from the configuration string embedded in the
                  linked object (build.sh compares it with ffmpeg_static.sh --print-args) and refuse
                  --enable-gpl / -nonfree / -libx26x / -version3; README.md's Licence section names SDL2,
                  FFmpeg, LGPL, Dear ImGui; CMakePresets.json's macos-dist and linux-dist fetch SDL2
                  and point CMAKE_PREFIX_PATH at the static FFmpeg prefix.
  staged dist     (--dist) licenses/ holds every name the platform requires, each byte-equal to the
                  committed copy; FFmpeg-BUILD-CONFIGURATION.txt names no GPL / non-free flag;
                  THE BINARY: the configuration string libavcodec embeds in what was linked (the staged
                  nativeee[.exe] when static, the bundled libavcodec when dynamic) is exactly one string,
                  carries no GPL / non-free / version3 / external-library flag, is quoted verbatim in
                  FFmpeg-BUILD-CONFIGURATION.txt, and -- static -- equals the recipe's arguments
                  (tools/ci/ffmpeg_static.sh --print-args; --prefix and --disable-x86asm aside): a stale
                  or foreign FFmpeg fails even when every text file is clean (review lic3b 1a);
                  THIRD_PARTY_NOTICES.md names every bundled library and does not contradict the
                  folder (a "lib/" section only when lib/ exists; "STATICALLY" for FFmpeg only when
                  no libavcodec is bundled; "dynamically linked"/"replaceable" only when one is).
Prints one line per check it read.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

COMMON = [
    "SDL2-LICENSE.txt",
    "FFmpeg-LICENSE.md",
    "FFmpeg-COPYING.LGPLv2.1.txt",
    "imgui-LICENSE.txt",
    "imgl3w-UNLICENSE.txt",
]
LINUX_ONLY = ["GCC-COPYING3.txt", "GCC-RUNTIME-LIBRARY-EXCEPTION.txt"]
REQUIRED_COMMITTED = COMMON + LINUX_ONLY
GENERATED = "FFmpeg-BUILD-CONFIGURATION.txt"
GPL_FLAGS = re.compile(r"--enable-(gpl|nonfree|version3|lib\w+)\b")
# libavcodec's FFMPEG_CONFIGURATION as embedded in an object: the configure command line as run.
EMBEDDED_CONF = re.compile(rb"--prefix=[\x20-\x7e]{0,4000}")
# Not part of the recipe comparison: the prefix path and the x86-64-without-nasm host flag.
HOST_ONLY_ARGS = ("--disable-x86asm",)
REQUIRED_FF_FLAGS = ["--disable-gpl", "--disable-nonfree", "--disable-version3", "--disable-everything"]
NOTICE_LIBS = ["SDL2", "FFmpeg", "LGPL", "ImGui", "OpenGL loader", "licenses/"]
DEDICATION = "This is free and unencumbered software released into the public domain."


def bash():
    """The bash that runs the recipe: on Windows, Git's (CreateProcess finds System32's bash.exe, the WSL launcher, first)."""
    if os.name == "nt" and shutil.which("git"):
        d = os.path.dirname(shutil.which("git"))   # <Git>/cmd, <Git>/bin or <Git>/mingw64/bin
        for _ in range(3):
            d = os.path.dirname(d)
            for rel in (("bin", "bash.exe"), ("usr", "bin", "bash.exe")):
                if os.path.isfile(os.path.join(d, *rel)):
                    return os.path.join(d, *rel)
    return "bash"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        h.update(f.read())
    return h.hexdigest()


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


class Lint:
    def __init__(self):
        self.fails = []

    def check(self, ok, line):
        print(("ok   " if ok else "FAIL ") + line)
        if not ok:
            self.fails.append(line)


def provenance_table(text):
    """{file: sha256} and the FFmpeg tag from PROVENANCE.md's table."""
    pins = {}
    for m in re.finditer(r"^\| `([^`]+)` \|.*\| `([0-9a-f]{64})` \|\s*$", text, re.M):
        pins[m.group(1)] = m.group(2)
    tag = re.search(r"FFmpeg `(n[0-9.]+)`", text)
    return pins, (tag.group(1) if tag else None)


def platform_required(platform):
    return COMMON + (LINUX_ONLY if platform.startswith("linux") else []) + [GENERATED]


def lint_committed(lint, root):
    licdir = os.path.join(root, "tools", "dist", "licenses")
    prov_path = os.path.join(licdir, "PROVENANCE.md")
    if not os.path.isfile(prov_path):
        lint.check(False, "tools/dist/licenses/PROVENANCE.md missing")
        return None
    pins, ff_tag = provenance_table(read(prov_path))
    for name in REQUIRED_COMMITTED:
        path = os.path.join(licdir, name)
        if not os.path.isfile(path):
            lint.check(False, "committed %s: missing" % name)
            continue
        pinned = pins.get(name)
        actual = sha256(path)
        lint.check(pinned == actual, "committed %s: sha256 %s %s PROVENANCE %s" % (
            name, actual[:16], "==" if pinned == actual else "!=", (pinned or "unpinned")[:16]))
    imgui = os.path.join(root, "app", "gui", "third_party", "imgui", "LICENSE.txt")
    ours = os.path.join(licdir, "imgui-LICENSE.txt")
    if os.path.isfile(imgui) and os.path.isfile(ours):
        lint.check(sha256(imgui) == sha256(ours), "imgui-LICENSE.txt byte-equal to app/gui/third_party/imgui/LICENSE.txt")
    loader = os.path.join(root, "app", "gui", "third_party", "imgui", "backends", "imgui_impl_opengl3_loader.h")
    if os.path.isfile(loader):
        lint.check(DEDICATION in read(loader), "imgui_impl_opengl3_loader.h carries the public-domain dedication imgl3w-UNLICENSE.txt quotes")
    return ff_tag


def lint_ffmpeg_recipe(lint, root, ff_tag):
    script = os.path.join(root, "tools", "ci", "ffmpeg_static.sh")
    if not os.path.isfile(script):
        lint.check(False, "tools/ci/ffmpeg_static.sh missing")
        return
    out = subprocess.run([bash(), script, "--print-configure"], capture_output=True, text=True)
    lint.check(out.returncode == 0, "ffmpeg_static.sh --print-configure rc=%d" % out.returncode)
    text = out.stdout
    for flag in REQUIRED_FF_FLAGS:
        lint.check(flag in text, "ffmpeg recipe has %s" % flag)
    bad = GPL_FLAGS.findall(text)
    lint.check(not bad, "ffmpeg recipe enables no GPL / non-free / external library (%s)" % (
        "none" if not bad else ", ".join(sorted(set(bad)))))
    m = re.search(r"FFmpeg (n[0-9.]+)", text)
    lint.check(m is not None and m.group(1) == ff_tag,
               "ffmpeg recipe tag %s == PROVENANCE.md tag %s" % (m.group(1) if m else "?", ff_tag))


def lint_stagers(lint, root):
    sh = read(os.path.join(root, "build.sh")) if os.path.isfile(os.path.join(root, "build.sh")) else ""
    for name in REQUIRED_COMMITTED:
        lint.check(("copy_licence %s" % name) in sh, "build.sh stages %s" % name)
    lint.check("strings" in sh and "--prefix=" in sh and GENERATED in sh,
               "build.sh generates %s from the configuration string embedded in the linked object" % GENERATED)
    lint.check("--print-args" in sh, "build.sh compares the embedded string with ffmpeg_static.sh --print-args")
    lint.check("lint_artifact_licenses.py" in sh, "build.sh runs this lint on the staged folder")
    for flag in ["--enable-gpl", "--enable-nonfree", "--enable-libx26", "--enable-version3"]:
        lint.check(flag in sh, "build.sh refuses an FFmpeg configured %s" % flag)
    ps1 = read(os.path.join(root, "build.ps1")) if os.path.isfile(os.path.join(root, "build.ps1")) else ""
    for name in COMMON:
        lint.check(('"%s"' % name) in ps1, "build.ps1 stages %s" % name)
    lint.check(GENERATED in ps1 and "--prefix=" in ps1, "build.ps1 writes %s from the embedded configuration string" % GENERATED)
    for flag in ["--enable-gpl", "--enable-nonfree", "--enable-libx26", "--enable-version3"]:
        lint.check(flag in ps1, "build.ps1 refuses an FFmpeg configured %s" % flag)
    readme = read(os.path.join(root, "README.md")) if os.path.isfile(os.path.join(root, "README.md")) else ""
    lic = readme[readme.find("### Licence"):] if "### Licence" in readme else ""
    for word in ["SDL2", "FFmpeg", "LGPL", "ImGui", "licenses/", GENERATED]:
        lint.check(word in lic, "README.md Licence section names %s" % word)
    lint.check("`lib/` (SDL2, FFmpeg)" not in readme, "README.md download table no longer lists a lib/ for macOS")
    presets_path = os.path.join(root, "CMakePresets.json")
    if os.path.isfile(presets_path):
        presets = json.load(open(presets_path))
        by_name = {p["name"]: p for p in presets["configurePresets"]}
        for name in ["macos-dist", "linux-dist"]:
            p = by_name.get(name, {})
            cv = p.get("cacheVariables", {})
            lint.check(cv.get("PS2R_FETCH_SDL2") == "ON" and cv.get("CMAKE_DISABLE_FIND_PACKAGE_SDL2") == "ON",
                       "preset %s builds SDL2 from the pinned release (static)" % name)
            lint.check("third_party/ffmpeg" in cv.get("CMAKE_PREFIX_PATH", ""),
                       "preset %s finds FFmpeg in its static prefix" % name)


def lint_dist(lint, root, dist):
    licdir = os.path.join(root, "tools", "dist", "licenses")
    base = os.path.basename(os.path.normpath(dist))
    platform = "linux" if base.startswith("linux") else ("windows" if base.startswith("windows") else "macos")
    staged = os.path.join(dist, "licenses")
    lint.check(os.path.isdir(staged), "%s/licenses/ present" % base)
    for name in platform_required(platform):
        path = os.path.join(staged, name)
        if not os.path.isfile(path):
            lint.check(False, "%s/licenses/%s: missing" % (base, name))
            continue
        if name == GENERATED:
            text = read(path)
            bad = GPL_FLAGS.findall(text)
            lint.check(not bad and ("--disable-gpl" in text or "configure line" in text),
                       "%s/licenses/%s: no GPL / non-free flag (%s)" % (base, name, "none" if not bad else ",".join(sorted(set(bad)))))
        else:
            lint.check(sha256(path) == sha256(os.path.join(licdir, name)),
                       "%s/licenses/%s byte-equal to the committed copy" % (base, name))
    notices_path = os.path.join(dist, "THIRD_PARTY_NOTICES.md")
    if not os.path.isfile(notices_path):
        lint.check(False, "%s/THIRD_PARTY_NOTICES.md missing" % base)
        return
    notices = read(notices_path)
    for word in NOTICE_LIBS:
        lint.check(word in notices, "%s/THIRD_PARTY_NOTICES.md names %s" % (base, word))
    if platform == "linux":
        lint.check("Runtime Library Exception" in notices, "%s/THIRD_PARTY_NOTICES.md names the GCC Runtime Library Exception" % base)
    libdir = os.path.join(dist, "lib")
    has_lib = os.path.isdir(libdir) and bool(os.listdir(libdir))
    bundled = []
    for d in [dist, libdir]:
        if os.path.isdir(d):
            bundled += [f for f in os.listdir(d) if re.match(r"(lib)?avcodec", f, re.I)]
    lint.check(("## Bundled dynamic libraries" in notices) == (has_lib or platform == "windows"),
               "%s/THIRD_PARTY_NOTICES.md has a bundled-libraries section iff libraries are bundled (lib/=%s)" % (base, has_lib))
    if bundled:
        lint.check("replaceable" in notices and "STATICALLY with ONLY" not in notices,
                   "%s/THIRD_PARTY_NOTICES.md: FFmpeg described as dynamic (bundled %s)" % (base, bundled[0]))
    else:
        lint.check("STATICALLY" in notices and "replaceable" not in notices,
                   "%s/THIRD_PARTY_NOTICES.md: FFmpeg described as static (no libavcodec bundled)" % base)
    for f in bundled:
        lint.check(not re.search(r"x26[45]", " ".join(os.listdir(libdir) if has_lib else os.listdir(dist))),
                   "%s: no x264/x265 (GPL) library bundled beside %s" % (base, f))
    lint_dist_binary(lint, root, dist, base, platform, bundled, libdir)


def embedded_configurations(path):
    """The distinct FFmpeg configuration strings an object embeds (sorted)."""
    with open(path, "rb") as f:
        data = f.read()
    return sorted(set(m.group(0).decode("ascii") for m in EMBEDDED_CONF.finditer(data)))


def recipe_args(root):
    """ffmpeg_static.sh --print-args as a list, or None when the script is absent."""
    script = os.path.join(root, "tools", "ci", "ffmpeg_static.sh")
    if not os.path.isfile(script):
        return None
    out = subprocess.run([bash(), script, "--print-args"], capture_output=True, text=True)
    if out.returncode != 0:
        return None
    return [l for l in out.stdout.splitlines() if l.strip()]


def lint_dist_binary(lint, root, dist, base, platform, bundled, libdir):
    """What was LINKED: libavcodec's embedded configuration string in the staged object."""
    if bundled:
        name = bundled[0]
        obj = os.path.join(libdir if os.path.isfile(os.path.join(libdir, name)) else dist, name)
        how = "dynamic"
    else:
        name = "nativeee.exe" if platform == "windows" else "nativeee"
        obj = os.path.join(dist, name)
        how = "static"
    if not os.path.isfile(obj):
        lint.check(False, "%s/%s: missing -- no object to read the linked FFmpeg's configuration from" % (base, name))
        return
    confs = embedded_configurations(obj)
    lint.check(len(confs) == 1, "%s/%s embeds exactly one FFmpeg configuration string (found %d)" % (base, name, len(confs)))
    if not confs:
        return
    conf = confs[0]
    bad = sorted(set(GPL_FLAGS.findall(conf)))
    lint.check(not bad, "%s/%s: the linked FFmpeg's configuration enables no GPL / non-free / version3 / external library (%s)" % (
        base, name, "none" if not bad else ", ".join(bad)))
    gen = os.path.join(dist, "licenses", GENERATED)
    if os.path.isfile(gen):
        lint.check(conf in read(gen), "%s/licenses/%s quotes the configuration string embedded in %s verbatim" % (base, GENERATED, name))
    if how == "static":
        recipe = recipe_args(root)
        if recipe is None:
            lint.check(False, "%s/%s: static FFmpeg but tools/ci/ffmpeg_static.sh --print-args unavailable" % (base, name))
            return
        linked = [a for a in conf.split(" ") if a and not a.startswith("--prefix=") and a not in HOST_ONLY_ARGS]
        same = linked == recipe
        detail = "" if same else " -- recipe-only: %s; linked-only: %s" % (
            " ".join(sorted(set(recipe) - set(linked))) or "-", " ".join(sorted(set(linked) - set(recipe))) or "-")
        lint.check(same, "%s/%s: the linked FFmpeg's configuration == the recipe (ffmpeg_static.sh --print-args, %d arguments)%s" % (
            base, name, len(recipe), detail))


def selftest():
    import tempfile
    fails = 0
    with tempfile.TemporaryDirectory() as tmp:
        root = os.path.join(tmp, "repo")
        lic = os.path.join(root, "tools", "dist", "licenses")
        os.makedirs(lic)
        texts = {n: ("text of %s\n" % n) for n in REQUIRED_COMMITTED}
        rows = []
        for n, t in texts.items():
            with open(os.path.join(lic, n), "w", newline="\n") as f:
                f.write(t)
            rows.append("| `%s` | x | y | z | `%s` |" % (n, hashlib.sha256(t.encode()).hexdigest()))
        with open(os.path.join(lic, "PROVENANCE.md"), "w", newline="\n") as f:
            f.write("| `FFmpeg-LICENSE.md` | FFmpeg `n6.1.2` |\n" + "\n".join(rows) + "\n")
        l = Lint()
        ff_tag = lint_committed(l, root)
        committed_ok = not [x for x in l.fails if x.startswith("committed ")]
        if not committed_ok or ff_tag != "n6.1.2":
            print("selftest: committed set with matching pins must pass"); fails += 1
        # a tampered text fails
        with open(os.path.join(lic, "SDL2-LICENSE.txt"), "a", newline="\n") as f:
            f.write("tamper\n")
        l2 = Lint(); lint_committed(l2, root)
        if not any("SDL2-LICENSE.txt" in x for x in l2.fails):
            print("selftest: an edited licence text must fail"); fails += 1
        # a fake recipe (the real one is read from the repo; here a fixed list) and a fake binary that
        # embeds libavcodec's configuration string -- the lint reads WHAT WAS LINKED from the object
        recipe = ["--disable-everything", "--disable-gpl", "--disable-nonfree", "--disable-version3", "--enable-swscale"]
        os.makedirs(os.path.join(root, "tools", "ci"))
        with open(os.path.join(root, "tools", "ci", "ffmpeg_static.sh"), "w", newline="\n") as f:
            f.write("#!/usr/bin/env bash\n[ \"$1\" = --print-args ] && printf '%s\\n' " + " ".join(recipe) + "\n")
        linked_ok = "--prefix=/x/third_party/ffmpeg " + " ".join(recipe)

        def write_binary(path, conf, times=3):
            with open(path, "wb") as f:
                f.write(b"\x7fELF" + (b"\x00junk\x00" + conf.encode() + b"\x00") * times + b"\x00tail")

        def write_generated(path, conf):
            with open(path, "w", newline="\n") as f:
                f.write("embedded:\n%s\nrecipe:\n%s\n" % (conf, "\n".join(recipe)))

        # a staged dist: static notices + complete set + binary == recipe == text passes
        dist = os.path.join(tmp, "dist", "linux-aarch64")
        os.makedirs(os.path.join(dist, "licenses"))
        with open(os.path.join(lic, "SDL2-LICENSE.txt"), "w", newline="\n") as f:
            f.write(texts["SDL2-LICENSE.txt"])
        for n in REQUIRED_COMMITTED:
            with open(os.path.join(dist, "licenses", n), "w", newline="\n") as f:
                f.write(texts[n])
        write_generated(os.path.join(dist, "licenses", GENERATED), linked_ok)
        write_binary(os.path.join(dist, "nativeee"), linked_ok)
        with open(os.path.join(dist, "THIRD_PARTY_NOTICES.md"), "w", newline="\n") as f:
            f.write("SDL2 FFmpeg LGPL ImGui OpenGL loader licenses/ Runtime Library Exception linked STATICALLY with ONLY\n")
        l3 = Lint(); lint_dist(l3, root, dist)
        if l3.fails:
            print("selftest: a complete static dist must pass: %s" % l3.fails); fails += 1
        # the lic3b defect: the text quotes the recipe, the BINARY was linked against a prefix built
        # without --disable-version3 -> the embedded string differs from the recipe and from the text
        stale = linked_ok.replace(" --disable-version3", "")
        write_binary(os.path.join(dist, "nativeee"), stale)
        l3b = Lint(); lint_dist(l3b, root, dist)
        if not any("== the recipe" in x and "--disable-version3" in x for x in l3b.fails):
            print("selftest: a binary whose embedded configuration differs from the recipe must fail: %s" % l3b.fails); fails += 1
        if not any("quotes the configuration string" in x for x in l3b.fails):
            print("selftest: a text not quoting the binary's embedded configuration must fail"); fails += 1
        # a GPL flag IN THE BINARY fails even when the text file is clean
        write_binary(os.path.join(dist, "nativeee"), linked_ok + " --enable-gpl --enable-libx264")
        l3c = Lint(); lint_dist(l3c, root, dist)
        if not any("enables no GPL" in x and "gpl" in x and "libx264" in x for x in l3c.fails):
            print("selftest: a GPL flag in the binary's embedded configuration must fail: %s" % l3c.fails); fails += 1
        # a binary without any configuration string (no FFmpeg linked / stripped) fails: nothing to attest
        write_binary(os.path.join(dist, "nativeee"), "no ffmpeg here")
        l3d = Lint(); lint_dist(l3d, root, dist)
        if not any("exactly one FFmpeg configuration string (found 0)" in x for x in l3d.fails):
            print("selftest: a binary embedding no configuration string must fail: %s" % l3d.fails); fails += 1
        # two different FFmpegs linked fails
        with open(os.path.join(dist, "nativeee"), "ab") as f:
            f.write(b"\x00" + linked_ok.encode() + b"\x00" + stale.encode() + b"\x00")
        l3e = Lint(); lint_dist(l3e, root, dist)
        if not any("(found 2)" in x for x in l3e.fails):
            print("selftest: two distinct embedded configurations must fail: %s" % l3e.fails); fails += 1
        write_binary(os.path.join(dist, "nativeee"), linked_ok)
        # a GPL flag in the text file fails
        with open(os.path.join(dist, "licenses", GENERATED), "w", newline="\n") as f:
            f.write("./configure --enable-gpl --enable-libx264\n")
        l4 = Lint(); lint_dist(l4, root, dist)
        if not any(GENERATED in x and "no GPL / non-free flag" in x for x in l4.fails):
            print("selftest: a GPL configure line in the staged dist must fail"); fails += 1
        write_generated(os.path.join(dist, "licenses", GENERATED), linked_ok)
        os.remove(os.path.join(dist, "licenses", "GCC-COPYING3.txt"))
        l5 = Lint(); lint_dist(l5, root, dist)
        if not any("GCC-COPYING3.txt: missing" in x for x in l5.fails):
            print("selftest: a Linux dist without the GCC texts must fail"); fails += 1
        # a dynamic macOS dist described as static contradicts itself
        mdist = os.path.join(tmp, "dist", "macos")
        os.makedirs(os.path.join(mdist, "lib"))
        os.makedirs(os.path.join(mdist, "licenses"))
        for n in COMMON:
            with open(os.path.join(mdist, "licenses", n), "w", newline="\n") as f:
                f.write(texts[n])
        with open(os.path.join(mdist, "licenses", GENERATED), "w", newline="\n") as f:
            f.write("configure line --disable-gpl\n")
        write_binary(os.path.join(mdist, "lib", "libavcodec.62.dylib"), "--prefix=/opt/homebrew/Cellar/ffmpeg/8.1.1 --enable-shared")
        write_binary(os.path.join(mdist, "nativeee"), "no string: the dynamic case reads the bundled libavcodec")
        with open(os.path.join(mdist, "THIRD_PARTY_NOTICES.md"), "w", newline="\n") as f:
            f.write("SDL2 FFmpeg LGPL ImGui OpenGL loader licenses/ linked STATICALLY with ONLY\n")
        l6 = Lint(); lint_dist(l6, root, mdist)
        if not any("described as dynamic" in x or "bundled-libraries section" in x for x in l6.fails):
            print("selftest: a bundled libavcodec described as static must fail"); fails += 1
        if any("libavcodec.62.dylib embeds exactly one" in x or "libavcodec.62.dylib: the linked" in x for x in l6.fails):
            print("selftest: the dynamic case must read the bundled libavcodec's string, not the binary: %s" % l6.fails); fails += 1
        # the dynamic case: a GPL libavcodec fails on the library's own string
        write_binary(os.path.join(mdist, "lib", "libavcodec.62.dylib"), "--prefix=/opt/homebrew/Cellar/ffmpeg/8.1.1 --enable-shared --enable-gpl --enable-libx265")
        l7 = Lint(); lint_dist(l7, root, mdist)
        if not any("libavcodec.62.dylib: the linked FFmpeg's configuration enables no GPL" in x and "libx265" in x for x in l7.fails):
            print("selftest: a GPL string in the bundled libavcodec must fail: %s" % l7.fails); fails += 1
    print("selftest: %s" % ("PASS" if fails == 0 else "FAIL (%d)" % fails))
    return 0 if fails == 0 else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root")
    ap.add_argument("--dist")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.root:
        ap.error("--root or --selftest")
    lint = Lint()
    ff_tag = lint_committed(lint, a.root)
    lint_ffmpeg_recipe(lint, a.root, ff_tag)
    lint_stagers(lint, a.root)
    if a.dist:
        if os.path.isdir(a.dist):
            lint_dist(lint, a.root, a.dist)
        else:
            print("skip %s: no staged folder (build.sh / build.ps1 stage it)" % a.dist)
    print("lint_artifact_licenses: %s (%d failure%s)" % (
        "PASS" if not lint.fails else "FAIL", len(lint.fails), "" if len(lint.fails) == 1 else "s"))
    return 0 if not lint.fails else 1


if __name__ == "__main__":
    sys.exit(main())
