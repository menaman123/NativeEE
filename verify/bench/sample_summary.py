#!/usr/bin/env python3
"""Summarise a macOS `sample` call-graph report: per-thread totals, inclusive samples
per function (deduplicated by the function's name; a recursive function is counted
once per sample path) and self samples (leaf frames). Also classifies every sample
by the first matching bucket pattern along its stack (outermost first, so a GS
pipeline frame under recompiled EE code lands in the GS bucket).

Usage: sample_summary.py <sample.txt> [--thread NAME] [--top N]
"""
import argparse
import re
import sys

LINE = re.compile(r"^(\s*)([+!:| ]*)\s*(\d+) (.*?)(?:  \(in ([^)]*)\))?(?: \+ \d+)?(?:  \[0x[0-9a-f]+\])?\s*$")

# Buckets, outermost-first match along the stack (the first pattern that matches any frame wins;
# patterns are tried in this order per sample, each pattern against every frame from the leaf up).
BUCKETS = [
    ("gs:texres", r"TextureResolver::|ContentKey::|DecodeCache::|TextureDecoder::|MipSampler::"),
    ("gs:upload", r"DrawPipeline::OnUpload|::Upload\(|UploadVRAMTexture|UploadLevel"),
    ("gs:targets", r"RenderTargetCache::|TargetRefresher::|RenderTarget::|PendingRtSyncLedger::"),
    ("gs:backend-gl", r"gl[A-Z][A-Za-z]*\b|GlBackend|render::.*Backend|libGL|GLEngine|gleRun|AppleMetalOpenGL"),
    ("gs:drawpipeline", r"DrawPipeline::|ClassRegistry::|Renderer::|Presenter::|ScanoutComposer::"),
    ("gs:drawir-lift", r"DrawIrLift|DrawIrVerifier|RegisterOpStream|DrawlistRing|GsStateMachine|GifParse|GifArbiter|GsSink|Gs[A-Z]\w*::"),
    ("gs:vram", r"VramImage::"),
    ("vif/vu1", r"Vif[01]?::|VifUnpack|Vu1|VU1|vu1_|MicroProgram|MtvuRing|Mtvu"),
    ("vu0", r"Vu0|VU0|vu0_|Cop2|COP2|VCALLMS|vcallms"),
    ("dmac", r"Dmac|DMAC|DmaChain|ChainWalker"),
    ("audio", r"AudioMix|SoundOutput|Spu2|SPU2|MusicLibrary|SoundSamples|AudioPullRing|Mixer"),
    ("iop/sif", r"Iop[A-Z]|IopKernel|Sif[A-Z]|SifRpc|StreamIrx|Stream\w*Library|FileSchedule|CdvdDrive"),
    ("hle:graphics-lib", r"ps2r::\w*Graphics|GraphicsService|GraphicsOutput|StripRenderer|ShadowPass|SkyPass|SpriteRenderer|TextRenderer"),
    ("hle:math", r"MathLibrary|CullingLibrary|TrigMath|CollisionMath|JointLibrary|AnimationMath|BoundsMath"),
    ("harness", r"FrameHash|LaneWriter|Harness|VerifyHost|fhash|ahash|mhash|xxh|Fnv1a"),
    ("sync/wait", r"__psynch_cvwait|__ulock_wait|pthread_cond|semaphore_wait|mach_msg|__semwait|usleep|nanosleep"),
    ("libc/memops", r"_platform_memmove|_platform_memset|_platform_memcmp|_platform_bzero|memcpy|memmove|memset"),
    ("malloc", r"malloc|free\b|_free|operator new|operator delete|szone|nanov2|tiny_"),
    ("ee:dispatch", r"DispatchTable::|recomp_call|ObservedBody|std::__function|std::__invoke|std::function"),
    ("ee:recomp", r"^FUN_[0-9a-f]+|initialize_recompiled_functions"),
]


def parse(path):
    threads = []
    cur = None
    stack = []  # (depth, frame_name, count)
    samples = []  # per leaf: (thread, [frames outermost..leaf], count)
    in_graph = False
    with open(path, errors="replace") as f:
        for raw in f:
            line = raw.rstrip("\n")
            if line.startswith("Call graph:"):
                in_graph = True
                continue
            if not in_graph:
                continue
            if line.startswith("Total number in stack") or line.startswith("Sort by top of stack"):
                break
            if not line.strip():
                continue
            m = re.match(r"^(\s*)((?:[+!:|] |[+!:|]|\s)*?)(\d+) (.*)$", line)
            if not m:
                continue
            prefix = m.group(1) + m.group(2)
            depth = len(prefix)
            count = int(m.group(3))
            rest = m.group(4)
            name = re.sub(r"\s+\(in [^)]*\)(\s+\+ \d+)?(\s+\[0x[0-9a-f]+\])?\s*$", "", rest)
            name = re.sub(r"\s+load address 0x[0-9a-f]+ \+ 0x[0-9a-f]+\s+\[0x[0-9a-f]+\]$", "", name)
            if "Thread_" in name and depth <= 4:
                cur = (name, count)
                threads.append(cur)
                stack = []
                continue
            # pop to parent
            while stack and stack[-1][0] >= depth:
                popped = stack.pop()
                # leaf?  a popped frame whose children sum < count has self time; we account self
                # via explicit tracking below
            if stack:
                stack[-1][3].append(count)   # child counts under parent
            stack.append((depth, name, count, []))
            samples.append((cur[0] if cur else "?", [s[1] for s in stack], count, stack[-1]))
    # self = count - sum(children)
    out = []
    for th, frames, count, node in samples:
        self_n = count - sum(node[3])
        out.append((th, frames, count, max(self_n, 0)))
    return threads, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--thread", default="ps2r-main")
    ap.add_argument("--top", type=int, default=40)
    a = ap.parse_args()
    threads, nodes = parse(a.path)
    print("threads:")
    for name, n in threads:
        print("  %6d %s" % (n, name))
    sel = [x for x in nodes if a.thread in x[0]]
    total = next((n for name, n in threads if a.thread in name), 0)
    print("\nthread %s total samples %d" % (a.thread, total))

    # inclusive per function (unique per path)
    incl = {}
    for th, frames, count, self_n in sel:
        pass
    # inclusive: for every node, count toward the node's own function once (if the same function
    # does not appear in an ancestor frame)
    for th, frames, count, self_n in sel:
        fn = frames[-1]
        if fn not in frames[:-1]:
            incl[fn] = incl.get(fn, 0) + count
    # self
    selfm = {}
    for th, frames, count, self_n in sel:
        if self_n:
            selfm[frames[-1]] = selfm.get(frames[-1], 0) + self_n
    # buckets: per leaf-node self time, classify by the stack
    comp = [re.compile(p) for _, p in BUCKETS]
    bucket = {}
    for th, frames, count, self_n in sel:
        if not self_n:
            continue
        b = "other"
        for (bname, _), rx in zip(BUCKETS, comp):
            if any(rx.search(fr) for fr in frames):
                b = bname
                break
        bucket[b] = bucket.get(b, 0) + self_n

    def short(s, n=150):
        return s if len(s) <= n else s[:n] + "..."

    print("\n== buckets (self samples by first matching pattern along the stack, leaf-up) ==")
    for b, n in sorted(bucket.items(), key=lambda kv: -kv[1]):
        print("  %6d  %5.1f%%  %s" % (n, 100.0 * n / max(total, 1), b))
    print("\n== top self ==")
    for fn, n in sorted(selfm.items(), key=lambda kv: -kv[1])[: a.top]:
        print("  %6d  %5.1f%%  %s" % (n, 100.0 * n / max(total, 1), short(fn)))
    print("\n== top inclusive (excluding dispatch plumbing) ==")
    skip = re.compile(r"DispatchTable|recomp_call|std::__function|std::__invoke|std::function|deduplicated_symbol|EeBootstrap|^start$|^main$|^entry\(|initialize_recompiled_functions")
    shown = 0
    for fn, n in sorted(incl.items(), key=lambda kv: -kv[1]):
        if skip.search(fn):
            continue
        print("  %6d  %5.1f%%  %s" % (n, 100.0 * n / max(total, 1), short(fn)))
        shown += 1
        if shown >= a.top:
            break


if __name__ == "__main__":
    main()
