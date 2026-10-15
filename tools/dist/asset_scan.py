#!/usr/bin/env python3
"""tools/dist/asset_scan.py -- the asset scan at the path the release tooling documents.

A thin forwarder to tools/ci/asset_scan.py (the one implementation: the artifact scan build.sh /
build.ps1 run over dist/, and the `--tree <dir>` source-tree gate CI runs first). Every argument
is passed through unchanged; the exit code is the scan's (0 clean, 1 a finding, 2 usage).

    python3 tools/dist/asset_scan.py --tree .
    python3 tools/dist/asset_scan.py dist/macos --report build/asset_scan_dist.md
"""
import os
import runpy
import sys

TARGET = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ci", "asset_scan.py")

if __name__ == "__main__":
    sys.argv[0] = os.path.abspath(TARGET)
    runpy.run_path(TARGET, run_name="__main__")
