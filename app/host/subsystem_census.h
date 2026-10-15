#pragma once
#include <cstdint>

#include "app/host/iop_arm.h"

class Dmac;
class GifParser;
class GsStateSink;
class KickLowering;
namespace ps2r::ipu { class IpuDevice; class LibipuApi; }
namespace ps2r::iop::hle { class ThbaseModule; }
namespace ps2r::verify { class RunMeta; }

// Writes host-only reachability observations. Call on the EE device owner;
// the IOP module supplies a thread-safe snapshot of its observer counters.
class SubsystemCensus {
public:
    // The IOP thread-ops observer counters (thbase). The oracle binary snapshots
    // the module's; the play binary (M12, no IOP linked) reports the zero row --
    // the same `iop_thread_ops=` text the never-booted kernel reported.
    struct IopThreadOps {
        uint64_t priority_calls = 0, priority_changes = 0, priority_errors = 0;
        uint64_t delay_kernel = 0, delay_host = 0;
        uint64_t injected_wake_errors = 0, injected_signal_errors = 0;
    };
#if PS2R_IOP_ORACLE
    static IopThreadOps Snapshot(const ps2r::iop::hle::ThbaseModule& threads);
#endif
    static void RecordGraphics(ps2r::verify::RunMeta& meta, const KickLowering& lowering);
    // The same census under another key (the MTVU worker unit's lowering).
    static void RecordKickLowering(ps2r::verify::RunMeta& meta, const char* key, const KickLowering& lowering);
    // The byte-form GIF parse that executed (`gif_parse_executed`, `gif_parse`:
    // GifParser::ReceiveData deliveries / bytes per path) and what each path
    // delivered to the GS façade, parsed or lowered (`gs_sink_by_path`).
    static void RecordGifParse(ps2r::verify::RunMeta& meta, const GifParser& parser, const GsStateSink& sink);
    // The attribution rows (track 1c-2): PATH2 packets by class (what the parser
    // still executes on PATH2) and PATH1 tags/kicks by render bucket (the
    // geometry GsState assembles for the M8 owned-bucket handoff); `exact` =
    // the bucket hint was exact for the whole run (inline VIF1 feed).
    static void RecordGifAttribution(ps2r::verify::RunMeta& meta, const GsStateSink& sink, bool exact);
    static void Record(ps2r::verify::RunMeta& meta, const Dmac& dmac, const IopThreadOps& threads);
    // The IPU command/DMA model's rows (ledger L7): the oracle binary reads its
    // IpuDevice / LibipuApi; the play binary (the model not linked -- no symbol of it,
    // LintPlayBinaryNoIop) writes the same rows with their never-executed text.
#if PS2R_IOP_ORACLE
    static void RecordIpu(ps2r::verify::RunMeta& meta, const ps2r::ipu::IpuDevice& ipu,
                          const ps2r::ipu::LibipuApi& libipu, uint64_t ipu_dma_out_of_range);
#endif
    static void RecordIpuNotLinked(ps2r::verify::RunMeta& meta);
};
