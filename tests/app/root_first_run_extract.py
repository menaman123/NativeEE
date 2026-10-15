#!/usr/bin/env python3
"""RootFirstRunExtract (CHARTER 2 C2, Bernoulli): the first-run extraction on the actual play binary
over a SYNTHETIC disc -- never the user's disc image.

The test builds an ISO9660 image with tests/lib make_synthetic_iso (raw 2352-byte .bin layout, the
hardest case) carrying a fake SLUS_123.45 of random bytes, computes that fake ELF's SHA-256 and
INJECTS it (`--set launcher.elf_sha256=<hex>`), then runs

    nativeee --extract-disc <fake.bin> --set paths.user_data=<tmp>

-- the launcher's extraction route without a window (the GUI drives the same FirstRun::Run). Pins:
  * exit 0 and the stderr ladder `extract verify: ... verified`, `extract elf:`, `extract disc:`,
    `extract memcards:`, `extract settings:`, `extract stamp:`, `extract: OK ...`;
  * <tmp>/game_files/example_title.rep/SLUS_123.45 byte-equal to the fake ELF;
  * <tmp>/game_files/example_title.rep/the title.iso is a PLAIN 2048 image (PVD at byte
    32768 reads `\\x01CD001`) -- the raw .bin was converted on the way;
  * <tmp>/memcards/{mc0,mc1,mc0_slot1,mc1_slot1} exist; <tmp>/ps2r.ini exists with `paths.iso = `;
  * <tmp>/game_files/.nativeee_build_id equals `nativeee --version` (the GameFilesStamp);
  * RED vectors on the same binary: the right serial with the BUILD's expectation (no injection)
    -> exit 1 `REFUSED -- SLUS_123.45 is present but it is not the ELF this build recompiled` and NO
    data dir; a PAL image (SLES_525.68) -> exit 1 `PAL release not supported`; a noise file -> exit 1
    `no ISO9660 volume descriptor`. Each refusal leaves no <tmp>/game_files behind;
  * a LATER-STAGE failure (<tmp>/memcards pre-created as a regular file, so the memcards stage cannot
    mkdir after the ELF + disc copy) -> exit 1 `REFUSED -- cannot create ... (the ELF written by this
    run was removed)`, the ELF gone, no stamp -- a half-finished extraction never reads as ready
    (review C2 loop 1).
Red-before: on a binary without `--extract-disc` the first run exits 2 (`unknown argument`) -- the
selftest pins the judge on canned transcripts (no process).
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

ELF_REL = os.path.join("example_title.rep", "SLUS_123.45")
ISO_REL = os.path.join("example_title.rep", "the title.iso")
LADDER = ["extract verify: the title SLUS_123.45 (NTSC-U) verified", "extract elf: ELF written",
          "extract disc: disc image in place", "extract memcards: memory cards ready",
          "extract settings: settings written", "extract stamp: stamp written", "extract: OK elf="]


def run(cmd, timeout):
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")


def judge_accept(code, err, data, elf_bytes, build_id):
    fails = []
    if code != 0:
        fails.append("exit %d (want 0)" % code)
    for step in LADDER:
        if step not in err:
            fails.append("stderr lacks `%s`" % step)
    elf = os.path.join(data, "game_files", ELF_REL)
    if not os.path.isfile(elf) or open(elf, "rb").read() != elf_bytes:
        fails.append("extracted ELF missing or differs: %s" % elf)
    iso = os.path.join(data, "game_files", ISO_REL)
    if not os.path.isfile(iso):
        fails.append("disc copy missing: %s" % iso)
    else:
        with open(iso, "rb") as f:
            f.seek(16 * 2048)
            pvd = f.read(6)
        if pvd != b"\x01CD001":
            fails.append("disc copy is not a plain 2048 ISO (PVD bytes %r)" % pvd)
        if os.path.isfile(iso + ".part"):
            fails.append(".part left behind")
    for card in ("mc0", "mc1", "mc0_slot1", "mc1_slot1"):
        if not os.path.isdir(os.path.join(data, "memcards", card)):
            fails.append("memcard dir missing: %s" % card)
    ini = os.path.join(data, "ps2r.ini")
    if not os.path.isfile(ini) or "paths.iso = " not in open(ini).read():
        fails.append("ps2r.ini missing or without paths.iso")
    stamp = os.path.join(data, "game_files", ".nativeee_build_id")
    if not os.path.isfile(stamp):
        fails.append("stamp missing")
    elif open(stamp).read().strip() != build_id.strip():
        fails.append("stamp %r != build id %r" % (open(stamp).read().strip(), build_id.strip()))
    return fails


def judge_reject(code, err, data, needle):
    fails = []
    if code != 1:
        fails.append("exit %d (want 1)" % code)
    if "REFUSED" not in err or needle not in err:
        fails.append("stderr lacks `REFUSED` with `%s`" % needle)
    if os.path.isdir(os.path.join(data, "game_files")):
        fails.append("a refused image left game_files behind")
    return fails


def judge_later_failure(code, err, data):
    """A failure AFTER the ELF stage (review C2 loop 1): the ELF this run wrote is
    removed again, the stamp was never written -- so the next start cannot read as
    'ready' over a half-finished extraction."""
    fails = []
    if code != 1:
        fails.append("exit %d (want 1)" % code)
    if "REFUSED -- cannot create" not in err or "the ELF written by this run was removed" not in err:
        fails.append("stderr lacks the memcards refusal naming the ELF removal")
    if "extract elf: ELF written" not in err:
        fails.append("stderr lacks `extract elf: ELF written` (the ELF stage must have run)")
    if os.path.isfile(os.path.join(data, "game_files", ELF_REL)):
        fails.append("ELF left behind after a later-stage failure")
    if os.path.isfile(os.path.join(data, "game_files", ".nativeee_build_id")):
        fails.append("stamp written despite the failure")
    return fails


def selftest():
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        elf = b"fake-elf"
        # Green transcript + a complete layout.
        data = os.path.join(tmp, "d")
        os.makedirs(os.path.join(data, "game_files", "example_title.rep"))
        open(os.path.join(data, "game_files", ELF_REL), "wb").write(elf)
        with open(os.path.join(data, "game_files", ISO_REL), "wb") as f:
            f.write(b"\0" * (16 * 2048))
            f.write(b"\x01CD001")
        for c in ("mc0", "mc1", "mc0_slot1", "mc1_slot1"):
            os.makedirs(os.path.join(data, "memcards", c))
        open(os.path.join(data, "ps2r.ini"), "w").write("paths.iso = \n")
        open(os.path.join(data, "game_files", ".nativeee_build_id"), "w").write("B1\n")
        err = "\n".join("nativeee: " + s + " x" for s in LADDER)
        ok &= judge_accept(0, err, data, elf, "B1") == []
        ok &= len(judge_accept(1, err, data, elf, "B1")) == 1
        ok &= len(judge_accept(0, err, data, elf, "B2")) == 1
        ok &= len(judge_accept(0, err.replace("stamp written", "stamp x"), data, elf, "B1")) == 1
        ok &= len(judge_accept(0, err, data, b"other", "B1")) == 1
        # Reject judge.
        ok &= judge_reject(1, "nativeee: extract: REFUSED -- PAL release not supported", os.path.join(tmp, "none"), "PAL release not supported") == []
        ok &= len(judge_reject(0, "nativeee: extract: OK", os.path.join(tmp, "none"), "PAL")) == 2
        ok &= len(judge_reject(1, "REFUSED -- PAL release not supported", data, "PAL release not supported")) == 1
        # Later-stage failure judge: a clean failure layout (disc copy may stay; ELF + stamp gone).
        d_fail = os.path.join(tmp, "f")
        os.makedirs(os.path.join(d_fail, "game_files", "example_title.rep"))
        err_fail = ("nativeee: extract elf: ELF written\nnativeee: extract: REFUSED -- cannot create x/memcards: Not a directory"
                    " (the ELF written by this run was removed)")
        ok &= judge_later_failure(1, err_fail, d_fail) == []
        ok &= len(judge_later_failure(1, err_fail, data)) == 2          # the complete layout: ELF + stamp present
        ok &= len(judge_later_failure(0, err_fail.replace(" (the ELF written by this run was removed)", ""), d_fail)) == 2
    print("root_first_run_extract selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary")
    ap.add_argument("--make-iso", help="tests/lib make_synthetic_iso")
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.binary or not os.path.isfile(args.binary) or not args.make_iso or not os.path.isfile(args.make_iso):
        print("SKIP: binary or make_synthetic_iso missing")
        return 77
    code, out, err = run([args.binary, "--version"], 30)
    if code != 0:
        print("FAIL: --version exit %d: %s" % (code, err))
        return 1
    build_id = out.strip()
    tmp = tempfile.mkdtemp(prefix="root_first_run_")
    fails = []
    try:
        elf_bytes = os.urandom(70000)
        elf_path = os.path.join(tmp, "fake.elf")
        open(elf_path, "wb").write(elf_bytes)
        digest = hashlib.sha256(elf_bytes).hexdigest()
        fake_bin = os.path.join(tmp, "fake.bin")
        code, out, err = run([args.make_iso, fake_bin, "raw", "--volume", "FAKE_TWIN", "SLUS_123.45=" + elf_path,
                              "SYSTEM.CNF=@BOOT2 = cdrom0:\\SLUS_123.45;1", "DATA/STREAM.IRX=@stub"], 60)
        if code != 0:
            print("FAIL: make_synthetic_iso: %s" % err)
            return 1
        # ACCEPT: the injected hash.
        data = os.path.join(tmp, "data")
        code, out, err = run([args.binary, "--extract-disc", fake_bin, "--set", "paths.user_data=" + data,
                              "--set", "launcher.elf_sha256=" + digest], args.timeout)
        print("--- accept (raw .bin, injected hash) exit=%d\n%s" % (code, err.strip()))
        fails += ["accept: " + f for f in judge_accept(code, err, data, elf_bytes, build_id)]
        # REJECT 1: the build's own expectation (no injection) refuses the fake ELF.
        d2 = os.path.join(tmp, "d_build_hash")
        code, out, err = run([args.binary, "--extract-disc", fake_bin, "--set", "paths.user_data=" + d2], args.timeout)
        print("--- reject build-hash exit=%d\n%s" % (code, err.strip()))
        fails += ["reject-hash: " + f for f in judge_reject(code, err, d2, "is present but it is not the ELF this build recompiled")]
        # REJECT 2: PAL.
        pal = os.path.join(tmp, "pal.iso")
        run([args.make_iso, pal, "plain", "SLES_525.68=" + elf_path], 60)
        d3 = os.path.join(tmp, "d_pal")
        code, out, err = run([args.binary, "--extract-disc", pal, "--set", "paths.user_data=" + d3,
                              "--set", "launcher.elf_sha256=" + digest], args.timeout)
        print("--- reject PAL exit=%d\n%s" % (code, err.strip()))
        fails += ["reject-pal: " + f for f in judge_reject(code, err, d3, "PAL release not supported")]
        # REJECT 3: not ISO9660.
        noise = os.path.join(tmp, "noise.bin")
        open(noise, "wb").write(os.urandom(2048 * 40))
        d4 = os.path.join(tmp, "d_noise")
        code, out, err = run([args.binary, "--extract-disc", noise, "--set", "paths.user_data=" + d4,
                              "--set", "launcher.elf_sha256=" + digest], args.timeout)
        print("--- reject noise exit=%d\n%s" % (code, err.strip()))
        fails += ["reject-noise: " + f for f in judge_reject(code, err, d4, "no ISO9660 volume descriptor")]
        # LATER-STAGE FAILURE: the accepted image, but <data>/memcards is a regular FILE,
        # so the memcards stage cannot mkdir after the ELF and the disc copy were written.
        d5 = os.path.join(tmp, "d_fail")
        os.makedirs(d5)
        open(os.path.join(d5, "memcards"), "wb").write(b"blocker")
        code, out, err = run([args.binary, "--extract-disc", fake_bin, "--set", "paths.user_data=" + d5,
                              "--set", "launcher.elf_sha256=" + digest], args.timeout)
        print("--- later-stage failure (memcards blocked) exit=%d\n%s" % (code, err.strip()))
        fails += ["later-failure: " + f for f in judge_later_failure(code, err, d5)]
    finally:
        if args.keep:
            print("kept %s" % tmp)
        else:
            shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print("FAIL root_first_run_extract:\n  " + "\n  ".join(fails))
        return 1
    print("PASS root_first_run_extract: accept (raw .bin -> plain .iso, ELF byte-equal, memcards, ps2r.ini, stamp == %s); "
          "3 refusals left nothing; a later-stage failure removed its ELF, no stamp" % build_id)
    return 0


if __name__ == "__main__":
    sys.exit(main())
