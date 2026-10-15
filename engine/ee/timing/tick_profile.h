#pragma once
// TickProfile: the game tick's wall-time breakdown (Charter 3 item 3, FRAME
// SKIP). The clock contract (DT-14) lets a tick that cannot finish in one
// field deliver the elapsed fields -- a larger step, never slow motion -- but
// the step is VISIBLE; the standing rule is that no single tick exceeds the
// field period outside a declared load. To attribute a long tick the host
// work the game thread performs inside our hooks is booked, by SITE, into
// per-tick accumulators the boundary reads (BoundaryProtocol::
// ParkForPacerTick: `pace_long_trace`) and resets.
//
// Booking is EXCLUSIVE: a scope books its own wall time minus its nested
// scopes', so the sites of one tick add up to at most the tick's work and
// `other` (the title's recompiled code and every un-scoped hook) is the
// remainder. Only the bound game thread books (BindGameThread at the EE
// thread's start; a scope on any other thread costs one thread-local read).
// The deterministic profile never reads a sample (no pacer), so the lanes
// are unchanged by construction; the scopes themselves are one steady_clock
// read each way.
//
// Load windows (the loader census, in BOUNDARY ordinals): a tick whose
// loader site ran a file read (bytes moved or a completion waited) is a load
// tick; load ticks closer than kLoadGapTicks form one window. RootNoLongTick
// judges every tick OUTSIDE these windows against the field period.
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <string>

namespace ps2r::ee {

class TickProfile {
public:
    enum Site : uint8_t {
        Loader = 0,     // LoaderFileIo: the title's file windows read from the disc image
        Texture,        // TextureBindings/TextureAssets::Load: the authored texture converted
        Mesh,           // MeshProducer: model/material reads converted, models forgotten
        Shadow,         // ShadowLoads: shadow source models converted
        Sky,            // SkyPass: sky models / textures converted
        Savestate,      // the boundary's Savestate slot (probe, pending save, live restore)
        Allocator,      // the meta-arena routing of the title's descriptor allocator
        GsSubmit,       // FrameService send/layout: the frame record handed to the GS queue
        Boundary,       // the boundary sequence's other slots (tick body, ISRs, maint, IOP, RPC, verify)
        Vu1Wait,        // the game thread parked on the MTVU worker (backpressure park, a terminal Sync)
        GifDrain,       // the GIF arbiter's drain pass on the game thread (parser -> GsState -> DrawIR -> the GS queue)
        kSiteCount
    };
    static const char* Name(Site site);

    // RAII: books the exclusive wall time of the enclosed work into `site`.
    class Scope {
    public:
        explicit Scope(Site site);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        Site site_;
        bool active_;
        uint64_t t0_ns_ = 0;
        uint64_t saved_child_ns_ = 0;
    };

    // The EE (game) thread binds itself once; scopes on other threads are inert.
    static void BindGameThread();
    static bool IsGameThread();

    // The loader notes a tick-level load event (a file read moved bytes or a
    // completion was waited for): the tick becomes a load tick.
    static void NoteLoad(uint64_t bytes);

    // The TITLE'S chunk state machine (TR chunkloading.cpp / chunkdata.cpp):
    // the chunk unload/load transition IS a load, whether or not a file window
    // moved bytes on the tick. A title hook notes, per tick:
    //   Loading / Unloading -- a file loader stepped in its Loading or
    //                          Unloading state (ChunkLoaderBase::StepStates);
    //   Release            -- a chunk stepped while Releasing / Released (its
    //                          parts let go in steps, StepChunkData);
    //   Cut                -- a chunk's FIRST step in the Shown state: the
    //                          title's first update of the level it just read
    //                          (every agent's first frame: its state applied,
    //                          its script run, its ground raycast).
    // A tick with any chunk event is a load tick (BoundaryProtocol's windows
    // are derived from these events + the loader's, no tail).
    enum ChunkEvent : uint8_t { ChunkLoading = 1, ChunkUnloading = 2, ChunkRelease = 4, ChunkCut = 8 };
    static void NoteChunkEvent(ChunkEvent kind);

    // The TICK CENSUS (diag.tick_census): `other` is the title's own code,
    // which no host scope sees. With the census on, a host SAMPLER thread
    // reads the game thread's shadow call stack (the view the game thread
    // publishes through the StackViewProvider at BindGameThread: the generated
    // TU's thread_local frames and depth) every ~50 us while a tick is in progress and histograms the
    // innermost kChainDepth frames (f2 > f1 > f0, f0 innermost) -- a sampling
    // profile of the title's recompiled code, zero cost on the game thread.
    // The long trace prints the top chains as
    // "samples=overrun/total:f2>f1>f0:us:n:over,..." (us = the chain's share of
    // the tick's work by sample count; over = its samples past the budget) and
    // the innermost frames' self time as "self=f0:us:n,...".
    // The hot hooks (libc memcpy/memset/strlen, the descriptor allocator)
    // call Mark() on entry: counted as "marks=overrun/total" (census on: one
    // clock read; off: one thread-local read and a count).
    //   ArmOverrun(work_from_ns, period_us): the boundary arms the tick's budget
    //   and the sampler; Take disarms it.
    using FrameReader = bool (*)(uint32_t n, uint32_t* func_addr, uint32_t* ra);
    static void SetFrameReader(FrameReader reader);
    // A bound thread's shadow-stack storage: frame i's func_addr lives at
    // func_addrs[i * stride_u32]; *depth frames are live; capacity bounds it.
    // Every guest thread is a game thread in turn (one holds the EE run token
    // at a time): BindGameThread asks the provider for the CALLING thread's
    // view, and the sampler reads the view of the bound thread that last
    // booked a scope, a mark or the boundary's arm.
    struct StackView {
        const uint32_t* func_addrs = nullptr;
        size_t stride_u32 = 0;
        const size_t* depth = nullptr;
        size_t capacity = 0;
    };
    using StackViewProvider = bool (*)(StackView* out);
    static void SetStackViewProvider(StackViewProvider provider);
    static void EnableCensus(bool on);
    static void ArmOverrun(uint64_t work_from_ns, uint32_t period_us);
    static void Mark();
    static constexpr size_t kCallerSlots = 64;
    static constexpr size_t kChainDepth = 3;
    static constexpr size_t kChainsPrinted = 10;

    struct Chain {
        uint32_t f[kChainDepth] = {};   // f[0] = the innermost frame, f[1] its caller, f[2] the caller's caller
        uint32_t samples = 0;           // sampler hits (whole tick)
        uint32_t samples_overrun = 0;   // of which past the budget
    };
    struct Self {
        uint32_t f = 0;          // the innermost frame
        uint32_t samples = 0;    // the samples whose innermost frame it was (its self time)
    };
    // INCLUSIVE time by function: a sample counts once for every distinct
    // function on the shadow stack (the innermost kInclusiveDepth frames), so
    // the OWNER of a long tick's `other` -- the title's update that the thin
    // construction chains and the raycasts all run under -- reads directly as
    // the function whose inclusive share is the tick ("incl=f:us:n,...").
    static constexpr size_t kInclusiveDepth = 32;
    static constexpr size_t kInclusiveSlots = 256;
    static constexpr size_t kInclusivePrinted = 16;
    struct Sample {
        uint64_t us[kSiteCount] = {};
        uint32_t calls[kSiteCount] = {};
        uint64_t load_bytes = 0;
        uint32_t load_events = 0;
        uint32_t chunk_events = 0;   // title chunk-state events on the tick (NoteChunkEvent)
        uint8_t chunk_kinds = 0;     // the ChunkEvent kinds seen, or-ed
        Chain chains[kCallerSlots];
        Self selfs[kCallerSlots];
        Self inclusive[kInclusiveSlots];
        uint32_t samples = 0;         // sampler hits in the tick
        uint32_t samples_overrun = 0;
        uint32_t marks_overrun = 0;   // hook marks taken past the budget
        uint32_t marks_total = 0;     // hook marks taken in the tick
        uint64_t Sum() const;
    };
    // Game thread, at the boundary: the accumulators since the previous Take, reset.
    static Sample Take();
    // "loader=12/3,tex=0/0,...,other=1234" (us/calls per site, other = work - sum).
    static std::string Format(const Sample& sample, uint64_t work_us);

    static uint64_t NowNs() {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
};

}  // namespace ps2r::ee
