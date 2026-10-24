#!/usr/bin/env python3
"""engine_bundle.py -- stage the redistributable ENGINE BUNDLE a release ships (decision D-A).

The bundle is everything a player's machine needs to turn ITS OWN disc into the game binary, minus
the compiler (a separate, pinned archive) and minus anything derived from the game:

  dist/engine-bundle-<platform>/
    BUNDLE.json           release tag, engine + title commits, platform, build id, VU1 AOT ABI key, and the
                          EXACT command templates (placeholders below) for every on-machine step: extract,
                          recompile, continuations, IOP recompile, PCH, shard / common / IOP / hle-TU compile,
                          link -- taken from this build's own build.ninja + compile_commands.json, so the
                          prepared binary is made the way the developer build was
    SHA256SUMS            every file below, `shasum -a 256` format
    lib/                  the engine's static archives (ps2r_*.a / .lib) + the prebuilt title archives that
                          carry no generated code (ps2r_title_hle, ps2r_title_play, the session libs ...)
    lib/external/         shared libraries the DEVELOPMENT build linked from the system (SDL2, FFmpeg) -- a
                          release build links them statically; listed in BUNDLE.json so nobody ships this
                          shape by accident
    host/                 the host's own objects (main.cpp.o, build_id.cpp.o: the build id IS the release's)
    include/              the header tree the generated TUs and the on-machine title TU compile against
                          (engine/, app/, games/<title>/ headers; generated/*.h; external SDL2 headers)
    tools/                ps2r_disc_extract, ps2r_elf_embedded, ps2r_recompiler_v2, ps2r_continuations (+ its
                          audits json), ps2r_iop_recompiler, vu1_aot_pack, nativeee-prepare
    vu1_aot_prelude/      the staged VU1 AOT prelude tree the first run's compile uses
    title/<title>/        the title's NON-game-derived inputs: TITLE_INPUTS.sha256, recomp/MANIFEST.sha256,
                          iop/*/MANIFEST.sha256, the tables the binary reads beside itself (hle/sdk_bindings.tsv,
                          data/, verify/restore_mhash_expected.tsv), the on-machine sources (hle/iop_host.cpp)
    LICENSE, THIRD_PARTY_NOTICES.md

Placeholders in BUNDLE.json commands: ${bundle} (this directory), ${gen_root} (the player's generated tree,
= <build>/generated/games/<title> of the developer build), ${obj} (object output root; per-target
subdirectories), ${cxx} (the bundled compiler's C++ driver), ${sysroot} (the SDK, see D-P3), ${iso} (the
player's disc image), ${out} (the game binary), ${jobs}. Everything game-derived stays under ${gen_root}
and ${obj} on the player's machine.

  engine_bundle.py --build B --source S --games-dir G --title T --out D --platform P [--release-tag TAG]
                   [--on-machine-targets a,b,c] [--binary-name sly2] [--sysroot SDK]

Exit 0 when staged; 1 on a shape the script cannot template (it says which path); 2 usage.
"""
import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import platform as _platform

HEADER_EXT = (".h", ".hpp", ".hh", ".inc", ".inl", ".ipp", ".tsv")
TOOL_NAMES = ["ps2r_disc_extract", "ps2r_elf_embedded", "ps2r_recompiler_v2", "ps2r_continuations",
              "ps2r_iop_recompiler", "vu1_aot_pack", "nativeee-prepare"]
TITLE_TABLES = ["hle/sdk_bindings.tsv", "data", "verify/restore_mhash_expected.tsv"]


def die(msg, code=1):
    sys.stderr.write("engine_bundle: %s\n" % msg)
    sys.exit(code)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def git_rev(path):
    try:
        return subprocess.run(["git", "-C", path, "rev-parse", "HEAD"], capture_output=True, text=True,
                              check=True).stdout.strip()
    except Exception:
        return ""


def host_platform():
    s = _platform.system().lower()
    m = _platform.machine().lower()
    arch = {"arm64": "arm64", "aarch64": "arm64", "x86_64": "x86_64", "amd64": "x86_64"}.get(m, m)
    return {"darwin": "macos", "windows": "windows", "linux": "linux"}.get(s, s) + "-" + arch


# --- build.ninja ------------------------------------------------------------------------------
class Ninja:
    """The build statements of a CMake Ninja build (build.ninja + rules.ninja), enough to read
    link lines, archive membership and the custom commands."""

    def __init__(self, build_dir):
        self.build_dir = build_dir
        self.rules = {}
        self.globals = {}
        self.builds = []   # dict(outputs, rule, inputs, implicit, order_only, vars)
        self._parse(os.path.join(build_dir, "build.ninja"))
        # ${cmake_ninja_workdir} and friends in outputs / inputs / variables
        def sub(t):
            for k, v in self.globals.items():
                t = t.replace("${%s}" % k, v)
            return t
        for b in self.builds:
            for key in ("outputs", "inputs", "implicit", "order_only"):
                b[key] = [sub(t) for t in b[key]]
            b["vars"] = {k: sub(v) for k, v in b["vars"].items()}
        rules = os.path.join(build_dir, "CMakeFiles", "rules.ninja")
        if os.path.exists(rules):
            self._parse(rules)

    @staticmethod
    def _unescape(tok):
        return tok.replace("$ ", " ").replace("$:", ":").replace("$$", "$")

    def _parse(self, path):
        with open(path, encoding="utf-8") as f:
            lines = f.read().split("\n")
        # join continuation lines ("$" at end)
        joined, cur = [], ""
        for ln in lines:
            if ln.endswith("$"):
                cur += ln[:-1]
                continue
            joined.append(cur + ln)
            cur = ""
        i = 0
        while i < len(joined):
            ln = joined[i]
            if ln.startswith("rule "):
                name = ln[5:].strip()
                vars_ = {}
                i += 1
                while i < len(joined) and joined[i].startswith("  "):
                    k, _, v = joined[i].strip().partition(" = ")
                    vars_[k] = v
                    i += 1
                self.rules[name] = vars_
                continue
            if re.match(r"^[A-Za-z_][A-Za-z0-9_]* = ", ln):
                k, _, v = ln.partition(" = ")
                self.globals[k.strip()] = v.strip()
                i += 1
                continue
            if ln.startswith("build "):
                head, _, _rest = ln[6:].partition(": ")
                head = head.replace(" | ", " ")   # implicit outputs count as outputs here
                outs = list(dict.fromkeys(self._unescape(t) for t in re.split(r"(?<!\$) ", head.strip()) if t and t != "|"))
                rule, _, rest = _rest.partition(" ")
                # inputs | implicit || order-only
                parts = re.split(r" \|\| ", rest)
                oo = parts[1] if len(parts) > 1 else ""
                parts = re.split(r" \| ", parts[0])
                imp = parts[1] if len(parts) > 1 else ""
                ins = parts[0]
                split = lambda s: [self._unescape(t) for t in re.split(r"(?<!\$) ", s.strip()) if t]
                vars_ = {}
                i += 1
                while i < len(joined) and joined[i].startswith("  "):
                    k, _, v = joined[i].strip().partition(" = ")
                    vars_[k] = v
                    i += 1
                self.builds.append({"outputs": outs, "rule": rule, "inputs": split(ins), "implicit": split(imp),
                                    "order_only": split(oo), "vars": vars_})
                continue
            i += 1

    def find_output(self, suffix):
        for b in self.builds:
            for o in b["outputs"]:
                if o == suffix or o.endswith("/" + suffix):
                    return b
        return None

    def expand(self, rule, vars_, target_file):
        cmd = self.rules[rule]["command"]
        env = {k: v for k, v in vars_.items() if isinstance(v, str)}
        env["TARGET_FILE"] = target_file
        env["in"] = " ".join(vars_.get("__in", []))
        env["out"] = target_file
        for k in sorted(env, key=len, reverse=True):
            cmd = cmd.replace("$" + k, env[k])
        cmd = re.sub(r"\$[A-Z_]+", "", cmd)   # unset PRE_LINK / POST_BUILD / RESTAT
        cmd = re.sub(r"\s*&&\s*&&\s*", " && ", cmd)
        cmd = re.sub(r"^\s*&&\s*", "", cmd)
        cmd = re.sub(r"\s*&&\s*$", "", cmd)
        return cmd.strip()


# --- the templating ----------------------------------------------------------------------------
class Templater:
    def __init__(self, build, source, games_dir, title, out, cxx, sysroot, iso):
        self.build = os.path.realpath(build)
        self.source = os.path.realpath(source)
        self.games_dir = os.path.realpath(games_dir)
        self.title = title
        self.out = out
        self.cxx = cxx
        self.sysroot = sysroot
        self.sysroots = {sysroot, os.path.realpath(sysroot)} - {""} if sysroot else set()
        self.iso = iso
        self.external_libs = {}      # abs path -> bundle rel path
        self.external_includes = {}  # abs dir -> bundle rel path
        self.unresolved = set()
        self.on_machine_objs = set()  # object paths (build-relative) compiled on the player's machine

    def _is_external_lib(self, p):
        return p.endswith((".dylib", ".so", ".dll", ".lib")) and not p.startswith(self.build) \
            and ".framework" not in p and not p.startswith(("/usr/lib", "/System/"))

    def map_path(self, p):
        """One absolute or build-relative path -> its templated form."""
        gen_root = os.path.join(self.build, "generated", "games", self.title)
        q = p if os.path.isabs(p) else os.path.join(self.build, p)
        q = os.path.normpath(q)
        if self.iso and q == os.path.normpath(self.iso):
            return "${iso}"
        if q.startswith(gen_root):
            return "${gen_root}" + q[len(gen_root):]
        if q.startswith(os.path.join(self.build, "generated")):
            return "${bundle}/include/generated" + q[len(os.path.join(self.build, "generated")):]
        if q.startswith(os.path.join(self.build, "vu1_aot_prelude")):
            return "${bundle}/vu1_aot_prelude" + q[len(os.path.join(self.build, "vu1_aot_prelude")):]
        if q.startswith(self.build):
            rel = q[len(self.build) + 1:]
            m = re.search(r"CMakeFiles/([^/]+)\.dir/(.*)$", rel)
            if m:
                target, tail = m.group(1), m.group(2)
                base = os.path.basename(tail)
                if target in ("nativeee",) and not tail.endswith(".pch"):
                    return "${bundle}/host/" + base
                if tail.endswith(".pch") or tail.endswith("cmake_pch.hxx"):
                    return "${obj}/" + target + "/" + base
                return "${obj}/" + target + "/" + base
            base = os.path.basename(rel)
            if base in TOOL_NAMES or base + ".exe" in TOOL_NAMES or os.path.splitext(base)[0] in TOOL_NAMES:
                return "${bundle}/tools/" + base
            if rel.endswith((".a", ".lib")):
                return "${bundle}/lib/" + base
            if rel.startswith("games/" + self.title + "/"):
                # a working directory of a generation step (tool_cwd, the per-module iop dirs): scratch
                return "${gen_root}/cwd/" + rel[len("games/" + self.title + "/"):]
            if rel:
                self.unresolved.add(rel)
            return "${build}/" + rel
        if q.startswith(self.source):
            rel = q[len(self.source) + 1:]
            if rel.startswith("tools/") and os.path.isfile(q):
                return "${bundle}/tools/" + os.path.basename(rel)
            return "${bundle}/include/" + rel if rel else "${bundle}/include"
        if q.startswith(os.path.join(self.games_dir, self.title)):
            return "${bundle}/title/" + self.title + q[len(os.path.join(self.games_dir, self.title)):]
        if q == os.path.normpath(os.path.join(self.games_dir, "..")) or q == self.games_dir:
            return "${bundle}/include"
        for sr in sorted(self.sysroots, key=len, reverse=True):
            if q == sr or q.startswith(sr + "/"):
                return "${sysroot}" + q[len(sr):]
        if q == os.path.normpath(self.cxx) or os.path.basename(q) in ("c++", "clang++", "g++", "clang-cl.exe"):
            return "${cxx}"
        if self._is_external_lib(q):
            rel = "lib/external/" + os.path.basename(q)
            self.external_libs[q] = rel
            return "${bundle}/" + rel
        if os.path.isdir(q) and not q.startswith(("/usr", "/System", "/Library/Developer")):
            rel = "include/external/" + os.path.basename(q)
            self.external_includes[q] = rel
            return "${bundle}/" + rel
        if q.startswith(("/opt", "/home", "/Users")):
            self.unresolved.add(q)
        return p

    def map_token(self, tok):
        for flag in ("-L", "-Wl,-rpath,"):
            if tok.startswith(flag) and len(tok) > len(flag):
                path = tok[len(flag):]
                if path.startswith(self.build):
                    return flag + "${bundle}/lib"
                return flag + "${bundle}/lib/external"   # where the dev build's system dylibs are staged
        for flag in ("-I", "-isystem", "-iframework", "-isysroot", "-F", "-include", "-include-pch"):
            if tok.startswith(flag) and len(tok) > len(flag) and (tok[len(flag)] == "/" or flag.endswith(",")):
                return flag + self.map_path(tok[len(flag):])
        if "/" in tok and not tok.startswith("-"):
            # a path, possibly with a trailing quote or shell noise
            return self.map_path(tok)
        return tok

    def map_argv(self, argv):
        return [self.map_token(t) for t in argv]


def split_shell_chain(cmd):
    """`cd X && a b c && d e` -> list of argv lists (the cd is kept as its own entry)."""
    return [shlex.split(part) for part in re.split(r"\s+&&\s+", cmd.strip()) if part.strip()]


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", required=True)
    ap.add_argument("--source", required=True)
    ap.add_argument("--games-dir", required=True)
    ap.add_argument("--title", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--platform", default=host_platform())
    ap.add_argument("--release-tag", default="")
    ap.add_argument("--on-machine-targets", default="")
    ap.add_argument("--binary-name", default="")
    ap.add_argument("--cxx", default="")
    ap.add_argument("--sysroot", default="")
    ap.add_argument("--iso", default="")
    ap.add_argument("--host-target", default="nativeee")
    a = ap.parse_args(argv)

    build, source, games_dir, title, out = (os.path.realpath(a.build), os.path.realpath(a.source),
                                            os.path.realpath(a.games_dir), a.title, os.path.realpath(a.out))
    on_machine = set(t for t in a.on_machine_targets.split(",") if t)
    on_machine |= {"ps2r_title_recomp", "ps2r_title_recomp_shards"}
    cc_path = os.path.join(build, "compile_commands.json")
    if not os.path.exists(cc_path):
        die("no compile_commands.json in %s (CMAKE_EXPORT_COMPILE_COMMANDS)" % build)
    cc = json.load(open(cc_path))
    ninja = Ninja(build)
    host = ninja.find_output("app/host/" + a.host_target) or ninja.find_output("app/host/" + a.host_target + ".exe")
    if not host:
        die("no link statement for %s in build.ninja" % a.host_target)
    link_rule = ninja.rules.get(host["rule"], {}).get("command", "")
    cxx = a.cxx or (shlex.split(link_rule.split("&&")[1] if "&&" in link_rule else link_rule)[0] if link_rule else "c++")
    for part in re.split(r"\s+&&\s+", link_rule):
        toks = shlex.split(part) if part.strip() else []
        if toks and toks[0].startswith("$"):
            continue
        if toks and (toks[0].endswith(("c++", "clang++", "g++", "clang-cl", "clang-cl.exe", "link.exe", "lld-link", "lld-link.exe"))):
            cxx = toks[0]
            break
    sysroot = a.sysroot
    if not sysroot and sys.platform == "darwin":
        try:
            sysroot = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True, check=True).stdout.strip()
        except Exception:
            sysroot = ""
    T = Templater(build, source, games_dir, title, out, cxx, sysroot, a.iso)

    if os.path.isdir(out):
        shutil.rmtree(out)
    for d in ("lib", "lib/external", "host", "include", "tools", "title/" + title):
        os.makedirs(os.path.join(out, d), exist_ok=True)
    report = {"copied": 0, "bytes": 0}

    def put(src, rel):
        dst = os.path.join(out, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
        report["copied"] += 1
        report["bytes"] += os.path.getsize(dst)

    # --- which objects / archives are made on the player's machine ------------------------
    on_machine_objs = set()
    on_machine_archives = set()
    for b in ninja.builds:
        for o in b["outputs"]:
            m = re.search(r"CMakeFiles/([^/]+)\.dir/", o)
            if m and m.group(1) in on_machine:
                on_machine_objs.add(o)
        if b["rule"].startswith("CXX_STATIC_LIBRARY_LINKER__"):
            tgt = b["rule"][len("CXX_STATIC_LIBRARY_LINKER__"):].rsplit("_", 1)[0]
            if tgt in on_machine:
                on_machine_archives.update(b["outputs"])
    on_machine_archives = {os.path.normpath(os.path.join(build, p)) for p in on_machine_archives}

    # --- the link line -----------------------------------------------------------------------
    link_vars = dict(host["vars"])
    link_vars["__in"] = host["inputs"]
    link_cmd = ninja.expand(host["rule"], link_vars, host["outputs"][0])
    link_argv = []
    for seg in re.split(r"\s+&&\s+", link_cmd):
        toks = shlex.split(seg) if seg.strip() else []
        if toks and toks[0] != ":" and os.path.basename(toks[0]) not in ("cmake", "cmake.exe"):
            link_argv = toks
    if not link_argv:
        die("cannot find the compiler/linker segment of the link rule: %r" % link_cmd[:200])
    # every archive the link names: copy the prebuilt ones; the on-machine ones become ${obj} archives
    link_out = []
    archives = []
    for tok in link_argv:
        q = os.path.normpath(os.path.join(build, tok)) if (tok.endswith((".a", ".lib")) and not os.path.isabs(tok)) else tok
        if tok.endswith((".a", ".lib")) and os.path.exists(q):
            if q in on_machine_archives:
                tgt = os.path.basename(q)
                link_out.append("${obj}/archives/" + tgt)
                members = []
                for b in ninja.builds:
                    if any(os.path.normpath(os.path.join(build, o)) == q for o in b["outputs"]):
                        members = [T.map_path(os.path.join(build, i)) for i in b["inputs"] if i.endswith((".o", ".obj"))]
                archives.append({"archive": "${obj}/archives/" + tgt, "objects": members})
            else:
                put(q, "lib/" + os.path.basename(q))
                link_out.append("${bundle}/lib/" + os.path.basename(q))
            continue
        if tok.endswith(".o") or tok.endswith(".obj"):
            q = os.path.normpath(os.path.join(build, tok))
            mapped = T.map_path(q)
            if mapped.startswith("${bundle}/host/"):
                put(q, "host/" + os.path.basename(q))
            link_out.append(mapped)
            continue
        if tok == "-o":
            link_out.append(tok)
            continue
        if tok.endswith("/" + a.host_target) or tok == a.host_target or tok.endswith(a.host_target + ".exe"):
            link_out.append("${out}")
            continue
        link_out.append(T.map_token(tok))
    for src, rel in T.external_libs.items():
        put(src, rel)

    # --- the on-machine compile commands (compile_commands.json) ------------------------------
    def cc_entries_for(pred):
        rows = []
        for e in cc:
            argv_ = shlex.split(e["command"]) if "command" in e else e["arguments"]
            try:
                o = argv_[argv_.index("-o") + 1]
            except ValueError:
                o = ""
            if pred(e, o):
                rows.append((e, argv_, o))
        return rows

    def target_of(o):
        m = re.search(r"CMakeFiles/([^/]+)\.dir/", o)
        return m.group(1) if m else ""

    compile_templates = {}
    on_machine_sources = []
    pch = None
    for e, argv_, o in cc_entries_for(lambda e, o: target_of(o) in on_machine):
        tgt = target_of(o)
        src = os.path.realpath(e["file"])
        mapped_argv = T.map_argv(argv_)
        row = {"target": tgt, "source": T.map_path(src), "object": T.map_path(o), "command": mapped_argv}
        if src.endswith("cmake_pch.hxx.cxx"):
            hxx = src[:-4]
            hxx_text = open(hxx, encoding="utf-8").read()
            for inc in re.findall(r'#include "([^"]+)"', hxx_text):
                hxx_text = hxx_text.replace(inc, T.map_path(inc))
            pch = dict(row, header=T.map_path(hxx), header_text=hxx_text, source_text=open(src, encoding="utf-8").read())
            continue
        if not src.startswith(build):   # a source-tree TU compiled on the player's machine: ship the source
            rel = os.path.relpath(src, os.path.join(games_dir, title)) if src.startswith(os.path.join(games_dir, title)) else None
            if rel is None:
                die("on-machine TU outside the title directory: %s" % src)
            put(src, "title/%s/%s" % (title, rel))
            on_machine_sources.append(row)
        else:
            compile_templates.setdefault(tgt, []).append(row)

    # --- the custom commands: extract, recompile, continuations, IOP recompile ------------------
    custom = []
    for b in ninja.builds:
        if b["rule"] != "CUSTOM_COMMAND":
            continue
        cmd = b["vars"].get("COMMAND", "")
        if not any(t in cmd for t in ("ps2r_disc_extract", "ps2r_elf_embedded", "ps2r_recompiler_v2",
                                      "ps2r_continuations", "ps2r_iop_recompiler")):
            continue
        chain = []
        for argv_ in split_shell_chain(cmd):
            if argv_ and argv_[0] == "cd":
                chain.append({"cd": T.map_path(argv_[1])})
                continue
            if argv_ and os.path.basename(argv_[0]) == "cmake" and "-P" in argv_:
                continue   # the manifest verification script: the prepare tool checks the manifest itself
            chain.append({"argv": T.map_argv(argv_)})
        kind = ("extract" if "ps2r_disc_extract" in cmd else "embedded" if "ps2r_elf_embedded" in cmd else
                "iop_recompile" if "ps2r_iop_recompiler" in cmd else "recompile")
        custom.append({"kind": kind, "outputs": [T.map_path(o) for o in b["outputs"]], "steps": chain})

    # --- the archive rule (ar qc + ranlib, or llvm-lib / lib.exe): the tool paths become placeholders ---
    archive_rule = []
    for name, r in ninja.rules.items():
        if name.startswith("CXX_STATIC_LIBRARY_LINKER__"):
            for seg in re.split(r"\s+&&\s+", r.get("command", "")):
                toks = shlex.split(seg) if seg.strip() else []
                if not toks or toks[0].startswith("$") or os.path.basename(toks[0]) in ("cmake", "cmake.exe"):
                    continue
                base = os.path.basename(toks[0]).lower()
                if base in ("ar", "llvm-ar", "llvm-ar.exe", "ar.exe"):
                    archive_rule.append(["${ar}"] + [t.replace("$TARGET_FILE", "${archive}").replace("$in", "${objects}")
                                                     for t in toks[1:] if t != "$LINK_FLAGS"])
                elif base in ("ranlib", "llvm-ranlib", "llvm-ranlib.exe"):
                    archive_rule.append(["${ranlib}"] + [t.replace("$TARGET_FILE", "${archive}") for t in toks[1:]])
                elif base in ("lib.exe", "llvm-lib.exe", "llvm-lib", "lib"):
                    archive_rule.append(["${lib}"] + [t.replace("$TARGET_FILE", "${archive}").replace("$in", "${objects}")
                                                      for t in toks[1:] if t != "$LINK_FLAGS"])
            break

    # --- tools, prelude, headers, title tables, licences ----------------------------------------
    tools = {}
    for name in TOOL_NAMES:
        found = None
        for b in ninja.builds:
            if b["rule"] == "phony":
                continue
            for o in b["outputs"]:
                if os.path.basename(o) in (name, name + ".exe") and "CMakeFiles" not in o:
                    found = os.path.join(build, o)
        if found and os.path.exists(found):
            put(found, "tools/" + os.path.basename(found))
            tools[name] = "${bundle}/tools/" + os.path.basename(found)
    audits = os.path.join(source, "tools", "recompiler_v2", "continuation_audits.json")
    if os.path.exists(audits):
        put(audits, "tools/continuation_audits.json")
    prelude = os.path.join(build, "vu1_aot_prelude")
    if os.path.isdir(prelude):
        shutil.copytree(prelude, os.path.join(out, "vu1_aot_prelude"))
    for root_dir in (os.path.join(source, "engine"), os.path.join(source, "app"), os.path.join(games_dir, title)):
        for dp, dns, fns in os.walk(root_dir):
            # the title's verify/ (judges, fixtures) is not a header tree; the engine's engine/core/verify is
            dns[:] = [d for d in dns if d not in (".git", "__pycache__") and not (d == "verify" and dp == root_dir and root_dir.startswith(games_dir))]
            for fn in fns:
                if fn.endswith(HEADER_EXT):
                    p = os.path.join(dp, fn)
                    rel = os.path.relpath(p, source) if p.startswith(source) else "games/" + os.path.relpath(p, games_dir)
                    put(p, "include/" + rel)
    gen = os.path.join(build, "generated")
    for fn in os.listdir(gen):
        if fn.endswith(".h"):
            put(os.path.join(gen, fn), "include/generated/" + fn)
    for src, rel in T.external_includes.items():
        shutil.copytree(src, os.path.join(out, rel), dirs_exist_ok=True)
    tdir = os.path.join(games_dir, title)
    for rel in ["TITLE_INPUTS.sha256", "recomp/MANIFEST.sha256"] + TITLE_TABLES:
        p = os.path.join(tdir, rel)
        if os.path.isdir(p):
            shutil.copytree(p, os.path.join(out, "title", title, rel), dirs_exist_ok=True)
        elif os.path.exists(p):
            put(p, "title/%s/%s" % (title, rel))
    iop_dir = os.path.join(tdir, "iop")
    if os.path.isdir(iop_dir):
        for d in sorted(os.listdir(iop_dir)):
            mp = os.path.join(iop_dir, d, "MANIFEST.sha256")
            if os.path.exists(mp):
                put(mp, "title/%s/iop/%s/MANIFEST.sha256" % (title, d))
    for lic in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        p = os.path.join(source, lic)
        if os.path.exists(p):
            put(p, lic)

    # --- identity -------------------------------------------------------------------------------
    build_id = ""
    bid = os.path.join(gen, "build_id.cpp")
    if os.path.exists(bid):
        m = re.search(r'"(NATIVEEE-BUILD-ID [^"]+)"', open(bid).read())
        build_id = m.group(1) if m else ""
    abi_key = ""
    kp = os.path.join(gen, "vu1_aot_abi_key.h")
    if os.path.exists(kp):
        m = re.search(r'"(abi-[0-9a-f]+)"', open(kp).read())
        abi_key = m.group(1) if m else ""
    title_profile = {}
    tp = os.path.join(tdir, "hle", "title_profile.cpp")
    if os.path.exists(tp):
        txt = open(tp).read()
        for key in ("serial", "elf_path", "elf_dir", "elf_sha256", "sdk_bindings_file", "disc_image_file", "display_name"):
            m = re.search(r"t\.%s\s*=\s*\"([^\"]*)\"" % key, txt)
            if m:
                title_profile[key] = m.group(1)
    binary_name = a.binary_name or (title_profile.get("display_name", title).lower().split()[0] if title_profile.get("display_name") else title)
    binary_name = re.sub(r"[^a-z0-9_]+", "", binary_name) or "game"

    manifest = {
        "schema": 1,
        "product": "nativeee",
        "release_tag": a.release_tag,
        "platform": a.platform,
        "engine_commit": git_rev(source),
        "title_commit": git_rev(tdir) or git_rev(games_dir),
        "build_id": build_id,
        "vu1_aot_abi_key": abi_key,
        "toolchain": {"cxx": cxx, "sysroot": T.sysroot, "note": "the compiler this bundle was built with; the player's "
                      "machine uses the pinned compiler archive of the same release (BUNDLE.json compile/link templates take ${cxx})"},
        "title": dict(title_profile, id=title, binary_name=binary_name + (".exe" if a.platform.startswith("windows") else ""),
                      inputs_table="${bundle}/title/%s/TITLE_INPUTS.sha256" % title,
                      recomp_manifest="${bundle}/title/%s/recomp/MANIFEST.sha256" % title,
                      iop_manifests={d: "${bundle}/title/%s/iop/%s/MANIFEST.sha256" % (title, d)
                                     for d in (sorted(os.listdir(iop_dir)) if os.path.isdir(iop_dir) else [])
                                     if os.path.exists(os.path.join(iop_dir, d, "MANIFEST.sha256"))},
                      tables=["${bundle}/title/%s/%s" % (title, t) for t in TITLE_TABLES if os.path.exists(os.path.join(tdir, t))]),
        "tools": tools,
        "steps": custom,
        "pch": pch,
        "compile": compile_templates,
        "on_machine_sources": on_machine_sources,
        "link": {"command": link_out, "archives_on_machine": archives, "archive_rule": archive_rule},
        "external": {"shared_libraries": sorted(T.external_libs.values()), "include_dirs": sorted(T.external_includes.values()),
                     "warning": ("the development build linked these from the host system; a RELEASE bundle must be built with "
                                 "static SDL2 / FFmpeg (PS2R_FETCH_SDL2=ON, tools/ci/ffmpeg_static.sh) so this list is empty"
                                 if T.external_libs else "")},
        "ini": {   # what the prepare tool writes into <folder>/nativeee.ini (relative to the game folder)
            "paths.user_data": "cache", "paths.mc_dir": "memcards", "paths.game_files": "game",
            "engine.vu1_aot_prelude": "vu1_aot_prelude", "engine.vu1_aot_pack": "",
            "note": "engine.vu1_aot_compiler is the bundled compiler's C++ driver under <folder>/cache/compiler, written by the prepare tool"},
        "unresolved_paths": sorted(T.unresolved),
    }
    # every include directory a template names must exist in the bundle (an anchor directory that holds
    # no header -- engine/iop/abi/anchor -- would otherwise be dropped by the compiler and its "../host_app"
    # includes would not resolve)
    def include_dirs(obj):
        if isinstance(obj, str):
            for flag in ("-I", "-isystem", "-iframework"):
                if obj.startswith(flag + "${bundle}/"):
                    yield obj[len(flag) + len("${bundle}/"):]
        elif isinstance(obj, dict):
            for v in obj.values():
                yield from include_dirs(v)
        elif isinstance(obj, list):
            for v in obj:
                yield from include_dirs(v)
    for rel in set(include_dirs(manifest)):
        os.makedirs(os.path.join(out, rel), exist_ok=True)
        keep = os.path.join(out, rel, ".keep")
        if not os.listdir(os.path.join(out, rel)):
            open(keep, "w").close()
    with open(os.path.join(out, "BUNDLE.json"), "w") as f:
        json.dump(manifest, f, indent=2, sort_keys=False)
        f.write("\n")
    # SHA256SUMS over everything (BUNDLE.json included)
    rows = []
    for dp, _dns, fns in os.walk(out):
        for fn in sorted(fns):
            p = os.path.join(dp, fn)
            rel = os.path.relpath(p, out)
            if rel == "SHA256SUMS":
                continue
            rows.append("%s  %s" % (sha256_file(p), rel))
    with open(os.path.join(out, "SHA256SUMS"), "w") as f:
        f.write("\n".join(sorted(rows, key=lambda r: r.split("  ", 1)[1])) + "\n")
    total = sum(os.path.getsize(os.path.join(dp, fn)) for dp, _d, fns in os.walk(out) for fn in fns)
    print("engine_bundle: %s -- %d files, %.1f MiB, build id %r, abi key %s, %d link inputs, %d on-machine TUs, %d steps%s"
          % (out, len(rows) + 1, total / (1 << 20), build_id, abi_key, len(link_out), sum(len(v) for v in compile_templates.values())
             + len(on_machine_sources), len(custom), "" if not T.unresolved else "; UNRESOLVED: " + ", ".join(sorted(T.unresolved))))
    return 0 if not T.unresolved else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
