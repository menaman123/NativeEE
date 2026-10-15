#!/usr/bin/env bash
# tests/gs/invariants/skip_stub.sh -- an honest SKIP for an invariant ctest whose owning
# module is outside Phase 6 (see README.md). Prints one line and exits 77 (SKIP_RETURN_CODE).
#   usage: skip_stub.sh <ctest name> <owner> <reason...>
set -u
if [ "$#" -lt 3 ]; then
    echo "usage: $0 <ctest name> <owner> <reason...>" >&2
    exit 2
fi
name="$1"; owner="$2"; shift 2
echo "SKIP ${name}: owned by ${owner} -- $*"
exit 77
