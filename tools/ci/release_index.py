#!/usr/bin/env python3
"""tools/ci/release_index.py -- the last step before a release is attached: over a folder of release assets write
SHA256SUMS, release-index.json and NOTES.md, and REFUSE what must not ship.

  release_index.py --dir DIR --tag TAG --repo OWNER/NAME --manifest launcher/title.json
                   [--engine-commit SHA] [--title-commit SHA] [--allow-missing PLATFORM,...] [--selftest]

release-index.json has the SHAPE of one element of the GitHub Releases API array (GET /repos/{repo}/releases):
`tag_name`, `name`, `draft`, `prerelease`, `published_at`, `html_url`, `assets[{name, size, content_type,
browser_download_url, digest}]` -- exactly the fields the launcher reads from the live API, so the launcher's offline
fixture server (launcher_cold_judge) can serve this file as `[ <this object> ]` plus the files, and the launcher cannot
tell the two apart. One extension block, `x-nativeee` (a key GitHub never emits), names the assets BY ROLE per platform:

  "x-nativeee": { "format": 1, "title": {...}, "engine": {"commit": ...}, "compiler": {"llvm": "20.1.7"},
                  "platforms": { "<platform>": { "launcher": "<asset name>", "engine_bundle": "<asset name>",
                                                  "compiler": "<asset name>", "bundle_folder": "<folder in the zip>" } },
                  "not_in_this_release": ["<platform>", ...], "unsupported": [ the manifest's `unsupported` rows ] }

Roles (D-A): launcher, engine_bundle, compiler -- per platform in D-B's set (macos-arm64, macos-x86_64, linux-x86_64,
windows-x86_64 -- the key tools/prepare computes at run time). Names: `<product>-launcher-<tag>-<platform>.<ext>`,
`engine-bundle-<platform>.tar.xz`, `compiler-<platform>.tar.xz` (the last two are tools/prepare's lookup names). Nothing game-derived is in the folder
(the asset scan already ran on each file; this script re-checks the names against the roles and refuses unknown files).

Refusals: a launcher whose BUILD-ID says STUB (looked up inside the .dmg? no: the packaging writes
<asset>.STUB beside a stub -- its presence here fails); a platform missing any role unless --allow-missing names it;
an asset not matching any role.
"""
import argparse, datetime, hashlib, json, os, re, sys

PLATFORMS = ["macos-arm64", "macos-x86_64", "linux-x86_64", "windows-x86_64"]
LAUNCHER_EXT = {"macos-arm64": ".dmg", "macos-x86_64": ".dmg", "linux-x86_64": ".AppImage", "windows-x86_64": ".zip"}
CONTENT_TYPES = {".zip": "application/zip", ".dmg": "application/x-apple-diskimage", ".AppImage": "application/x-executable",
                 ".xz": "application/x-xz", ".json": "application/json", "": "text/plain", ".md": "text/markdown", ".sha256": "text/plain"}


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def expand(pattern, **kw):
    out = pattern
    for k, v in kw.items():
        out = out.replace("${%s}" % k, v)
    return out


def roles_for(manifest, tag):
    """asset name per (platform, role) from the manifest's release block (defaults when absent)."""
    product = manifest["product"].lower()
    rel = manifest.get("release", {})
    # The engine bundle's and the compiler's names are tools/prepare's contract (engine/tools/prepare/prepare.cpp:
    # `engine-bundle-<platform>.tar.xz`, `compiler-<platform>.tar.xz`, both looked up by name in the release's
    # assets and verified against SHA256SUMS / the asset digest); a manifest may override them ONLY if that tool
    # learns the override (release.engine_bundle_pattern / release.compiler_pattern) -- today it does not.
    bundle_pat = rel.get("engine_bundle_pattern", "engine-bundle-${platform}.tar.xz")
    folder_pat = rel.get("bundle_folder", "engine-bundle-${platform}")
    launcher_pat = rel.get("launcher_pattern", "${product}-launcher-${tag}-${platform}${ext}")
    compiler_pat = rel.get("compiler_pattern", "compiler-${platform}.tar.xz")
    return product, bundle_pat, folder_pat, launcher_pat, compiler_pat


def build(args):
    manifest = json.load(open(args.manifest))
    product, bundle_pat, folder_pat, launcher_pat, compiler_pat = roles_for(manifest, args.tag)
    files = sorted(f for f in os.listdir(args.dir) if os.path.isfile(os.path.join(args.dir, f)))
    files = [f for f in files if f not in ("SHA256SUMS", "release-index.json", "NOTES.md")]
    stubs = [f for f in files if f.endswith(".STUB")]
    if stubs:
        sys.exit("release_index: REFUSING -- stub artefacts in %s: %s (a pipeline dry run, not a launcher build)" % (args.dir, " ".join(stubs)))
    llvm = args.llvm
    if not llvm:
        pins = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "toolchain", "COMPILER_PINS.sha256")
        m = re.search(r"LLVM-([0-9.]+)-macOS-ARM64", open(pins).read())
        llvm = m.group(1) if m else "unknown"
    allow_missing = set(filter(None, (args.allow_missing or "").split(",")))
    platforms, missing, claimed = {}, [], set()
    for p in PLATFORMS:
        want = {
            "launcher": expand(launcher_pat, product=product, tag=args.tag, platform=p, ext=LAUNCHER_EXT[p]),
            "engine_bundle": expand(bundle_pat, product=product, tag=args.tag, platform=p),
            "compiler": expand(compiler_pat, platform=p, llvm=llvm),
        }
        have = {role: name for role, name in want.items() if name in files}
        if len(have) < 3:
            lacking = sorted(set(want) - set(have))
            if p in allow_missing:
                missing.append(p)
                print("release_index: %s not in this release (missing %s; allowed)" % (p, ", ".join(lacking)))
                claimed.update(have.values())
                continue
            sys.exit("release_index: REFUSING -- %s lacks %s (expected %s); --allow-missing %s to publish without it"
                     % (p, ", ".join(lacking), ", ".join(want[r] for r in lacking), p))
        claimed.update(have.values())
        platforms[p] = dict(have, bundle_folder=expand(folder_pat, product=product, platform=p))
    unknown = [f for f in files if f not in claimed and not f.endswith(".sha256")]
    if unknown:
        sys.exit("release_index: REFUSING -- files that match no role: %s" % " ".join(unknown))
    if not platforms:
        sys.exit("release_index: REFUSING -- no platform is complete")
    # SHA256SUMS over the role assets (not the per-file .sha256 twins, which are not attached)
    assets, sums = [], []
    for f in sorted(claimed):
        path = os.path.join(args.dir, f)
        digest = sha256_of(path)
        sums.append("%s  %s" % (digest, f))
        ext = os.path.splitext(f)[1] if "." in f else ""
        assets.append({
            "name": f, "size": os.path.getsize(path), "content_type": CONTENT_TYPES.get(ext, "application/octet-stream"),
            "browser_download_url": "https://github.com/%s/releases/download/%s/%s" % (args.repo, args.tag, f),
            "digest": "sha256:" + digest, "state": "uploaded",
        })
    open(os.path.join(args.dir, "SHA256SUMS"), "w").write("\n".join(sums) + "\n")
    sums_path = os.path.join(args.dir, "SHA256SUMS")
    assets.append({"name": "SHA256SUMS", "size": os.path.getsize(sums_path), "content_type": "text/plain",
                   "browser_download_url": "https://github.com/%s/releases/download/%s/SHA256SUMS" % (args.repo, args.tag),
                   "digest": "sha256:" + sha256_of(sums_path), "state": "uploaded"})
    now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    index = {
        "tag_name": args.tag, "name": "%s %s" % (manifest.get("name", product), args.tag),
        "draft": True, "prerelease": "-" in args.tag, "published_at": now,
        "html_url": "https://github.com/%s/releases/tag/%s" % (args.repo, args.tag),
        "assets": assets,
        "x-nativeee": {
            "format": 1,
            "title": {"id": manifest["id"], "name": manifest.get("name"), "product": manifest["product"], "commit": args.title_commit},
            "engine": {"commit": args.engine_commit},
            "compiler": {"llvm": llvm, "pins": "engine/tools/toolchain/COMPILER_PINS.sha256"},
            "platforms": platforms,
            "not_in_this_release": missing,
            "unsupported": manifest.get("unsupported", []),
            "game_data": "none: every asset passed engine/tools/ci/asset_scan.py; the game program is prepared on the player's machine from the player's own disc",
        },
    }
    json.dump(index, open(os.path.join(args.dir, "release-index.json"), "w"), indent=1)
    # NOTES.md: the release page's text (plain words; the same roles)
    labels = {p: manifest.get("targets", {}).get(p, {}).get("label", p) for p in PLATFORMS}
    lines = [
        "%s %s" % (manifest.get("name", product), args.tag), "",
        "**This release contains no game data and nothing made from a game disc.** Download the launcher for your computer,",
        "open it, choose your own disc image, and press Play: the launcher fetches the two other files for your computer from",
        "this page, checks them against SHA256SUMS, and prepares the game on your machine (\"Preparing your game\"). You need",
        "your own disc.", "",
        "| Your computer | Launcher | The launcher also downloads |", "|---|---|---|",
    ]
    for p, r in platforms.items():
        lines.append("| %s | `%s` | `%s`, `%s` |" % (labels[p], r["launcher"], r["engine_bundle"], r["compiler"]))
    if missing:
        lines += ["", "Not in this release (the build did not produce it): %s." % ", ".join(labels[p] for p in missing)]
    for u in manifest.get("unsupported", []):
        lines += ["", "**%s:** %s" % (u.get("label", "?"), u.get("reason", ""))]
    lines += ["", "### Checksums", "", "| File | SHA-256 |", "|---|---|"]
    for s in sums:
        d, f = s.split("  ", 1)
        lines.append("| %s | `%s` |" % (f, d))
    lines += ["", "`release-index.json` lists the same files for the launcher (the shape of this page as the GitHub API returns it)."]
    open(os.path.join(args.dir, "NOTES.md"), "w").write("\n".join(lines) + "\n")
    print("release_index: %d assets, %d platforms (%s), missing: %s" % (len(assets), len(platforms), " ".join(platforms), " ".join(missing) or "-"))
    return 0


def selftest():
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        mf = os.path.join(d, "title.json")
        json.dump({"id": "SCUS-97316", "name": "Sly 2", "product": "sly2",
                   "unsupported": [{"label": "PlayStation Vita", "reason": "no"}]}, open(mf, "w"))
        rd = os.path.join(d, "rel"); os.mkdir(rd)
        for n in ["sly2-launcher-v1.0.0-macos-arm64.dmg", "engine-bundle-macos-arm64.tar.xz", "compiler-macos-arm64.tar.xz",
                  "sly2-launcher-v1.0.0-linux-x86_64.AppImage", "engine-bundle-linux-x86_64.tar.xz"]:
            open(os.path.join(rd, n), "wb").write(n.encode())
        a = argparse.Namespace(dir=rd, tag="v1.0.0", repo="o/r", manifest=mf, engine_commit="e", title_commit="t", llvm="20.1.7", allow_missing="macos-x86_64,windows-x86_64")
        try:
            build(a); return "selftest: incomplete linux-x86_64 was not refused"
        except SystemExit as e:
            assert "linux-x86_64 lacks compiler" in str(e), str(e)
        a.allow_missing = "linux-x86_64,macos-x86_64,windows-x86_64"
        build(a)
        idx = json.load(open(os.path.join(rd, "release-index.json")))
        assert set(idx["x-nativeee"]["platforms"]) == {"macos-arm64"}, idx["x-nativeee"]["platforms"]
        assert idx["x-nativeee"]["not_in_this_release"] == ["macos-x86_64", "linux-x86_64", "windows-x86_64"], idx["x-nativeee"]["not_in_this_release"]
        assert any(x["name"] == "SHA256SUMS" for x in idx["assets"])
        assert all(x["digest"].startswith("sha256:") for x in idx["assets"])
        assert idx["x-nativeee"]["platforms"]["macos-arm64"]["bundle_folder"] == "engine-bundle-macos-arm64"
        sums = open(os.path.join(rd, "SHA256SUMS")).read()
        assert "engine-bundle-macos-arm64.tar.xz" in sums and "sly2-launcher-v1.0.0-linux-x86_64.AppImage" in sums
        open(os.path.join(rd, "sly2-launcher-v1.0.0-macos-arm64.dmg.STUB"), "w").write("")
        try:
            build(a); return "selftest: a STUB marker was not refused"
        except SystemExit as e:
            assert "stub" in str(e).lower()
        os.remove(os.path.join(rd, "sly2-launcher-v1.0.0-macos-arm64.dmg.STUB"))
        open(os.path.join(rd, "SCUS_973.16"), "wb").write(b"x")
        try:
            build(a); return "selftest: an unknown file was not refused"
        except SystemExit as e:
            assert "match no role" in str(e)
    print("release_index: selftest ok")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir"); ap.add_argument("--tag"); ap.add_argument("--repo"); ap.add_argument("--manifest")
    ap.add_argument("--engine-commit", default=""); ap.add_argument("--title-commit", default=""); ap.add_argument("--llvm", default="")
    ap.add_argument("--allow-missing", default=None); ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        r = selftest()
        sys.exit(0 if r == 0 else r)
    for k in ("dir", "tag", "repo", "manifest"):
        if not getattr(a, k):
            ap.error("--%s is required" % k)
    sys.exit(build(a))


if __name__ == "__main__":
    main()
