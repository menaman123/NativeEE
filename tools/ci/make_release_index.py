#!/usr/bin/env python3
"""make_release_index.py -- a release index in the GitHub Releases API shape over a directory of assets.

  make_release_index.py --dir <assets dir> --tag <tag> --base-url <url the assets are served from>
                        [--out releases.json] [--prerelease] [--name NAME]

Writes [{tag_name, name, draft, prerelease, published_at, assets: [{name, size, browser_download_url,
digest: "sha256:<hex>"}]}] -- exactly the fields ps2r_prepare reads from api.github.com, so the offline
fixture server (python3 -m http.server over <assets dir>) and the real thing are the same code path.
Also writes <dir>/SHA256SUMS (the fallback when an index carries no digests).
"""
import argparse
import datetime
import hashlib
import json
import os
import sys


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", required=True)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--base-url", required=True)
    ap.add_argument("--out", default="")
    ap.add_argument("--name", default="")
    ap.add_argument("--prerelease", action="store_true")
    a = ap.parse_args(argv)
    assets, sums = [], []
    for name in sorted(os.listdir(a.dir)):
        p = os.path.join(a.dir, name)
        if not os.path.isfile(p) or name in ("releases.json", "SHA256SUMS") or name.endswith(".sha256"):
            continue
        digest = sha256(p)
        sums.append("%s  %s" % (digest, name))
        assets.append({"name": name, "size": os.path.getsize(p), "browser_download_url": a.base_url.rstrip("/") + "/" + name,
                       "digest": "sha256:" + digest, "content_type": "application/octet-stream"})
    with open(os.path.join(a.dir, "SHA256SUMS"), "w") as f:
        f.write("\n".join(sums) + "\n")
    assets.append({"name": "SHA256SUMS", "size": os.path.getsize(os.path.join(a.dir, "SHA256SUMS")),
                   "browser_download_url": a.base_url.rstrip("/") + "/SHA256SUMS", "content_type": "text/plain"})
    index = [{"tag_name": a.tag, "name": a.name or a.tag, "draft": False, "prerelease": a.prerelease,
              "published_at": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"), "assets": assets}]
    out = a.out or os.path.join(a.dir, "releases.json")
    with open(out, "w") as f:
        json.dump(index, f, indent=2)
        f.write("\n")
    print("make_release_index: %s -- %s, %d asset(s)" % (out, a.tag, len(assets) - 1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
