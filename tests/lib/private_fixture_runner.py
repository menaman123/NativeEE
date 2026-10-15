#!/usr/bin/env python3
"""private_fixture_runner.py -- run a ctest command only when its private fixtures exist.

  private_fixture_runner.py --source-root <dir> [--fixture <rel>]... -- <cmd> [args...]

The public tree carries no game bytes (cmake/Ps2rPrivateFixtures.cmake). The fixture root is
$NATIVEEE_PRIVATE_FIXTURES when set, else --source-root (a developer who put the files back in
place). Every --fixture is a repo-relative path that must exist under the root; otherwise the
runner prints `SKIP: ...` and exits 77 (ctest SKIP_RETURN_CODE) -- never a failure. The literal
token {PRIVATE_FIXTURES} in the command is replaced by the root; the command's exit code is
forwarded unchanged (its own 77 stays a SKIP).

  private_fixture_runner.py --selftest
"""
import os
import subprocess
import sys
import tempfile

ENV = "NATIVEEE_PRIVATE_FIXTURES"
SKIP = 77
TOKEN = "{PRIVATE_FIXTURES}"


def resolve_root(source_root, environ=os.environ):
    value = environ.get(ENV, "")
    return os.path.abspath(value) if value else os.path.abspath(source_root)


def missing(root, fixtures):
    return [rel for rel in fixtures if not os.path.exists(os.path.join(root, rel))]


def run(argv, environ=os.environ):
    if "--" not in argv:
        print("usage: private_fixture_runner.py --source-root DIR [--fixture REL]... -- CMD [ARGS...]")
        return 2
    split = argv.index("--")
    opts, command = argv[:split], argv[split + 1:]
    source_root, fixtures = None, []
    i = 0
    while i < len(opts):
        if opts[i] == "--source-root" and i + 1 < len(opts):
            source_root = opts[i + 1]; i += 2
        elif opts[i] == "--fixture" and i + 1 < len(opts):
            fixtures.append(opts[i + 1]); i += 2
        else:
            print("private_fixture_runner: unknown option %s" % opts[i])
            return 2
    if source_root is None or not command:
        print("private_fixture_runner: --source-root and a command are required")
        return 2
    root = resolve_root(source_root, environ)
    absent = missing(root, fixtures)
    if absent:
        where = "%s=%s" % (ENV, environ[ENV]) if environ.get(ENV) else "%s unset" % ENV
        for rel in absent:
            print("SKIP: private fixture absent: %s (%s)" % (os.path.join(root, rel), where))
        return SKIP
    command = [arg.replace(TOKEN, root) for arg in command]
    sys.stdout.flush()
    try:
        return subprocess.call(command)
    except OSError as e:
        print("private_fixture_runner: cannot run %s: %s" % (command[0], e))
        return 1


def selftest():
    fails = 0
    def check(cond, what):
        nonlocal fails
        if not cond:
            fails += 1
            print("selftest FAIL:", what)
    with tempfile.TemporaryDirectory() as td:
        src = os.path.join(td, "src"); priv = os.path.join(td, "priv")
        os.makedirs(os.path.join(priv, "games/t/probe")); os.makedirs(src)
        with open(os.path.join(priv, "games/t/probe/old_b2.ps2st"), "wb") as f:
            f.write(b"\0" * 16)
        base = ["--source-root", src, "--fixture", "games/t/probe/old_b2.ps2st", "--",
                sys.executable, "-c", "import sys; sys.exit(0 if sys.argv[1].endswith('/games/t') else 3)",
                TOKEN + "/games/t"]
        check(run(base, {}) == SKIP, "variable unset + absent in source root -> 77")
        check(run(base, {ENV: priv}) == 0, "variable set, fixture present -> command runs with the token substituted")
        check(run(["--source-root", priv] + base[2:], {}) == 0, "fixture present under the source root -> runs")
        check(run(base, {ENV: os.path.join(td, "nowhere")}) == SKIP, "variable set to a dir without the file -> 77")
        fwd = ["--source-root", priv, "--", sys.executable, "-c", "import sys; sys.exit(5)"]
        check(run(fwd, {}) == 5, "exit code forwarded")
        check(run(["--source-root", src], {}) == 2, "missing -- is a usage error")
    print("private_fixture_runner selftest: %s" % ("PASS" if not fails else "%d FAIL" % fails))
    return 1 if fails else 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(selftest())
    sys.exit(run(sys.argv[1:]))
