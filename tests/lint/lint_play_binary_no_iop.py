#!/usr/bin/env python3
"""lint_play_binary_no_iop.py -- the PLAY binary links no IOP (M12, TRACK 3b D2/D3).

  lint_play_binary_no_iop.py --play <nativeee> [--oracle <nativeee_oracle>]
  lint_play_binary_no_iop.py --selftest

Runs `nm` on the play binary, demangles with `c++filt`, and FAILS when any symbol
names the retained IOP arm: the IOP kernel / modules / libsd model (`ps2r::iop::`),
the recompiled STREAM.IRX and its plugin (`stream_irx::`, `iop_stubs::`,
`iop_recompiled`), the SIF DMA model (`EeRpcServer`, `MainMemoryEeRamSink`,
`SifRpcBridge`), and -- ledger L7 -- the IPU command/DMA model (`ps2r::ipu::IpuDevice`,
`IpuVlc`, `IpuColor`, `MpegDecode`, `LibipuApi`, `LibipuBindings`, `DmacChannelFaceAdapter`,
`LibmpegBindings::RetainedInitialization`). With --oracle it also requires the oracle
binary to carry > 0 of them (the pattern is not vacuous). Prints the counts it read.

The live movie path (`ps2r::ipu::MpegApi`, `LibmpegBindings` rows, `FfmpegVideoDecoder`,
`PssDemux`, the adapters) is NOT a model symbol and stays in the play binary.

The EE-side libsdr rows (`ps2r::sound::hle::LibsdrBindings`, `SdrRemote`,
`SdBlockTrans`) are NOT IOP symbols: they are the title's SDK face over SpuCore
and stay in the play binary (ledger D3 note).
"""
import argparse
import re
import subprocess
import sys

IOP_PATTERNS = [
    re.compile(r"\bps2r::iop::"),
    re.compile(r"\bstream_irx::"),
    re.compile(r"\biop_stubs::"),
    re.compile(r"iop_recompiled"),
    re.compile(r"\bEeRpcServer\b"),
    re.compile(r"\bMainMemoryEeRamSink\b"),
    re.compile(r"\bSifRpcBridge\b"),
    re.compile(r"\bLibsdModule\b"),
    # ledger L7: the IPU command/DMA model (ps2r_ipu_model)
    re.compile(r"\bps2r::ipu::(IpuDevice|IpuVlc|IpuColor|MpegDecode|LibipuApi|LibipuBindings|DmacChannelFaceAdapter)\b"),
    re.compile(r"\bLibmpegBindings::RetainedInitialization\b"),
]


def demangled_symbols(binary):
    nm = subprocess.run(["nm", binary], capture_output=True, text=True, check=True)
    filt = subprocess.run(["c++filt"], input=nm.stdout, capture_output=True, text=True, check=True)
    return filt.stdout.splitlines()


def iop_hits(lines):
    hits = []
    for line in lines:
        for pat in IOP_PATTERNS:
            if pat.search(line):
                hits.append(line)
                break
    return hits


def selftest():
    sample = [
        "0000 T ps2r::iop::IopKernel::Dispatch()",
        "0000 T ps2r::title::stream_irx::StreamIrxPlugin::Boot(unsigned char const*, unsigned long)",
        "0000 T iop_stubs::sceSdInit()",
        "0000 T EeRpcServer::Service(EmotionEngineState&)",
        "0000 T ps2r::sound::hle::LibsdrBindings::RemoteInit(EmotionEngineState&)",   # NOT an IOP symbol
        "0000 T ps2r::sound::hle::SdrRemote::Calls() const",                           # NOT an IOP symbol
        "0000 T ps2r::app::ServiceTimeline::ServiceTimeline(bool, EmuDomain&)",
        "0000 T ps2r::ipu::IpuDevice::Write(unsigned int, unsigned int, unsigned long long)",   # L7 model
        "0000 T ps2r::ipu::LibmpegBindings::RetainedInitialization(ps2r::ipu::LibipuApi&, ps2r::ipu::DmacChannelFace&)",  # L7 model
        "0000 T ps2r::ipu::MpegApi::GetPicture(unsigned int)",                       # NOT a model symbol (the live decoder)
        "0000 T ps2r::ipu::LibmpegBindings::Bind(SdkBindingManifest&, ps2r::ipu::LibmpegBindings*)",  # NOT a model symbol
    ]
    hits = iop_hits(sample)
    ok = len(hits) == 6 and not any("Libsdr" in h or "SdrRemote" in h or "MpegApi" in h or "LibmpegBindings::Bind" in h for h in hits)
    print("lint_play_binary_no_iop selftest: %s (%d/6 hits)" % ("PASS" if ok else "FAIL", len(hits)))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--play")
    ap.add_argument("--oracle")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.play:
        ap.error("--play <binary> or --selftest")
    play = demangled_symbols(a.play)
    play_hits = iop_hits(play)
    print("play binary %s: symbols=%d iop_symbols=%d" % (a.play, len(play), len(play_hits)))
    for h in play_hits[:40]:
        print("  IOP symbol in play binary: " + h)
    ok = not play_hits
    if a.oracle:
        oracle = demangled_symbols(a.oracle)
        oracle_hits = iop_hits(oracle)
        print("oracle binary %s: symbols=%d iop_symbols=%d" % (a.oracle, len(oracle), len(oracle_hits)))
        if not oracle_hits:
            print("  FAIL: the oracle binary carries no IOP symbol -- the pattern is vacuous or the oracle lost its arm")
            ok = False
    print("lint_play_binary_no_iop: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
