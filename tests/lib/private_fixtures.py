"""private_fixtures.py -- the Python face of cmake/Ps2rPrivateFixtures.cmake.

The public tree carries no game bytes. A judge or audit that needs one of the removed files
(RAM savestate probes, PCSX2 GS dumps, console shots, the title's .data word dump ...) asks
for it by its ORIGINAL repo-relative path:

    from tests.lib.private_fixtures import private_fixture, require_private_fixture, SKIP
    path = require_private_fixture("games/<title>/data/ram_data_map_v50.txt")   # exits 77 when absent
    path = private_fixture("games/<title>/verify/baseline/coldboot/probe/old_b120.ps2st")  # or None

The root is $NATIVEEE_PRIVATE_FIXTURES when set, else the source tree itself (a developer who put
the files back in place). Importable without a package: callers that are run as scripts add the
repo root to sys.path, or use `load_private_fixtures(repo_root)` from a path.
"""
import os
import sys

ENV = "NATIVEEE_PRIVATE_FIXTURES"
SKIP = 77
_HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", ".."))


def private_fixture_root(environ=os.environ):
    value = environ.get(ENV, "")
    return os.path.abspath(value) if value else REPO_ROOT


def private_fixture(rel, environ=os.environ):
    """The absolute path of `rel` under the private root, or None when it is absent."""
    path = os.path.join(private_fixture_root(environ), rel)
    return path if os.path.exists(path) else None


def require_private_fixture(rel, what=None, environ=os.environ):
    """The path, or print `SKIP: ...` and exit 77 (ctest SKIP_RETURN_CODE)."""
    path = private_fixture(rel, environ)
    if path is None:
        where = "%s=%s" % (ENV, environ[ENV]) if environ.get(ENV) else "%s unset" % ENV
        print("SKIP: %sprivate fixture absent: %s (%s)" % ((what + ": ") if what else "", rel, where))
        sys.stdout.flush()
        sys.exit(SKIP)
    return path


def selftest():
    import tempfile
    fails = 0
    def check(cond, msg):
        nonlocal fails
        if not cond:
            fails += 1
            print("selftest FAIL:", msg)
    with tempfile.TemporaryDirectory() as td:
        os.makedirs(os.path.join(td, "games/t/data"))
        with open(os.path.join(td, "games/t/data/map.txt"), "w") as f:
            f.write("0x0:0x0\n")
        check(private_fixture_root({}) == REPO_ROOT, "unset -> repo root")
        check(private_fixture_root({ENV: td}) == os.path.abspath(td), "set -> the variable")
        check(private_fixture("games/t/data/map.txt", {ENV: td}) == os.path.join(td, "games/t/data/map.txt"), "present -> path")
        check(private_fixture("games/t/data/none.txt", {ENV: td}) is None, "absent -> None")
        check(private_fixture("games/t/data/map.txt", {}) is None, "absent under the repo root -> None")
    print("private_fixtures selftest: %s" % ("PASS" if not fails else "%d FAIL" % fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(selftest() if sys.argv[1:] == ["--selftest"] else 2)
