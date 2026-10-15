#include "app/host/subsystem_census.h"

#include <cstdio>
#include <string>

#include "engine/core/verify/run_meta.h"
#include "engine/dmac/dmac.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/hle/gs_state_sink.h"
#if PS2R_IOP_ORACLE
#include "engine/iop/hle/thbase/thbase_module.h"
#endif
#if PS2R_IOP_ORACLE
#include "engine/ipu/ipu_device.h"
#include "engine/ipu/hle/libipu_api.h"
#endif
#include "engine/vu/hle/kick_lowering.h"

namespace {
void Add(std::string& out, const char* name, uint64_t value) {
    if (!out.empty()) out += ',';
    out += name;
    out += '=';
    out += std::to_string(value);
}

void RecordDma(ps2r::verify::RunMeta& meta, const Dmac& dmac) {
    const DmacTransferStats& counts = dmac.TransferStats();
    static const char* const from_names[] = {"from_normal", "from_chain", "from_interleave", "from_reserved"};
    static const char* const to_names[] = {"to_normal", "to_chain", "to_interleave", "to_reserved"};
    std::string text;
    uint64_t unsupported = 0, faults = 0, completed = 0, quadwords = 0;
    for (unsigned mode = 0; mode < 4; ++mode) {
        Add(text, from_names[mode], counts.spr_from[mode].starts);
        Add(text, to_names[mode], counts.spr_to[mode].starts);
        for (const DmacTransferCount* count : {&counts.spr_from[mode], &counts.spr_to[mode]}) {
            unsupported += count->unsupported;
            faults += count->faults;
            completed += count->completed;
            quadwords += count->quadwords;
        }
    }
    Add(text, "unsupported", unsupported);
    Add(text, "faults", faults);
    Add(text, "unsupported_ctrl_writes", counts.unsupported_spr_ctrl_writes);
    Add(text, "completed", completed);
    Add(text, "quadwords", quadwords);
    meta.Set("dma_spr", text);
}

#if PS2R_IOP_ORACLE
void RecordIpu(ps2r::verify::RunMeta& meta, const ps2r::ipu::IpuDevice& ipu) {
    static const char* const names[] = {
        "bclr", "idec", "bdec", "vdec", "fdec", "setiq", "setvq", "csc", "pack", "setth",
    };
    std::string commands, unsupported;
    uint64_t unknown = 0, unknown_unsupported = 0, unmodelled = 0;
    for (uint8_t code = 0; code < 16; ++code) {
        const uint64_t count = ipu.CommandCount(code);
        const uint64_t misses = ipu.UnsupportedCommandCount(code);
        unmodelled += misses;
        if (code < 10) {
            Add(commands, names[code], count);
            Add(unsupported, names[code], misses);
        } else {
            unknown += count;
            unknown_unsupported += misses;
        }
    }
    Add(commands, "unknown", unknown);
    Add(unsupported, "unknown", unknown_unsupported);
    meta.Set("ipu_commands", commands);
    meta.Set("ipu_unsupported", unsupported);
    meta.Set("ipu_unmodelled", std::to_string(unmodelled));
}
#endif
}  // namespace

void SubsystemCensus::RecordGraphics(ps2r::verify::RunMeta& meta, const KickLowering& lowering) {
    RecordKickLowering(meta, "graphics_kick_lowering", lowering);
}

void SubsystemCensus::RecordKickLowering(ps2r::verify::RunMeta& meta, const char* key, const KickLowering& lowering) {
    const auto counts = lowering.census();
    std::string text;
    Add(text, "submitted", counts.submitted);
    Add(text, "queued", counts.queued);
    Add(text, "lowered", counts.lowered);
    Add(text, "queued_state", counts.queued_state);
    Add(text, "lowered_state", counts.lowered_state);
    Add(text, "queued_packed", counts.queued_packed);
    Add(text, "lowered_packed", counts.lowered_packed);
    Add(text, "raw", counts.raw_fallback);
    Add(text, "unsupported", counts.unsupported);
    Add(text, "unconfigured", counts.unconfigured);
    Add(text, "ordering_fallback", counts.ordering_fallback);
    meta.Set(key, text);
    // The unsupported submits attributed to their producing program, largest
    // first: `<program hash>:<program name>@<entry pc>:<declined shape>:<count>`.
    std::string by_program;
    for (const KickLowering::UnsupportedByProgram& row : lowering.unsupported_by_program()) {
        if (!by_program.empty()) by_program += ',';
        char head[96];
        std::snprintf(head, sizeof(head), "0x%016llx:%s@0x%04x:%s:", (unsigned long long)row.origin.program_hash,
                      row.origin.name ? row.origin.name : "unknown", (unsigned)row.origin.entry_pc, row.shape);
        by_program += head;
        by_program += std::to_string(row.count);
    }
    meta.Set(std::string(key) + "_unsupported_by_program", by_program.empty() ? "none" : by_program);
}

void SubsystemCensus::RecordGifParse(ps2r::verify::RunMeta& meta, const GifParser& parser, const GsStateSink& sink) {
    // `gif_parse_executed=0` = the parser never ran: the DMAC->VIF->GIF packet
    // path delivered nothing to the GS in byte form for the whole run.
    static const GifPath kPaths[] = {GifPath::Path1, GifPath::Path2, GifPath::Path3};
    static const char* const kNames[] = {"path1", "path2", "path3"};
    uint64_t receives = 0;
    std::string parse, by_path;
    for (unsigned i = 0; i < 3; ++i) receives += parser.Receives(kPaths[i]);
    Add(parse, "receives", receives);
    for (unsigned i = 0; i < 3; ++i) {
        parse += std::string(",") + kNames[i] + "=" + std::to_string(parser.Receives(kPaths[i])) + "/" +
                 std::to_string(parser.ReceivedBytes(kPaths[i]));
        if (!by_path.empty()) by_path += ',';
        by_path += std::string(kNames[i]) + "=tags:" + std::to_string(sink.TagsOn(kPaths[i])) +
                   "/regs:" + std::to_string(sink.RegisterWritesOn(kPaths[i])) +
                   "/kicks:" + std::to_string(sink.KicksOn(kPaths[i])) +
                   "/image_qw:" + std::to_string(sink.ImageQwordsOn(kPaths[i]));
    }
    meta.Set("gif_parse_executed", receives != 0 ? "1" : "0");
    meta.Set("gif_parse", parse);
    meta.Set("gs_sink_by_path", by_path);
}

void SubsystemCensus::RecordGifAttribution(ps2r::verify::RunMeta& meta, const GsStateSink& sink, bool exact) {
    std::string by_class;
    for (unsigned c = 0; c < static_cast<unsigned>(GsStateSink::Path2Class::Count); ++c) {
        const auto k = static_cast<GsStateSink::Path2Class>(c);
        if (!by_class.empty()) by_class += ',';
        by_class += std::string(GsStateSink::Path2ClassName(k)) + ":" + std::to_string(sink.Path2Packets(k)) + "/" +
                    std::to_string(sink.Path2Bytes(k));
    }
    meta.Set("gif_path2_by_class", by_class);
    std::string by_bucket = std::string("exact=") + (exact ? "1" : "0");
    for (unsigned b = 0; b <= GsStateSink::kBuckets; ++b) {
        const uint64_t tags = sink.Path1TagsInBucket(b), kicks = sink.Path1KicksInBucket(b);
        if (!tags && !kicks) continue;
        by_bucket += "," + (b == GsStateSink::kBuckets ? std::string("none") : std::to_string(b)) + ":" +
                     std::to_string(tags) + "/" + std::to_string(kicks);
    }
    meta.Set("gs_sink_path1_by_bucket", by_bucket);
}

void SubsystemCensus::Record(ps2r::verify::RunMeta& meta, const Dmac& dmac, const IopThreadOps& counts) {
    meta.Set("subsystem_census", "1");
    RecordDma(meta, dmac);
    std::string text;
    Add(text, "priority_calls", counts.priority_calls);
    Add(text, "priority_changes", counts.priority_changes);
    Add(text, "priority_errors", counts.priority_errors);
    Add(text, "delay_kernel", counts.delay_kernel);
    Add(text, "delay_host", counts.delay_host);
    Add(text, "injected_wake_errors", counts.injected_wake_errors);
    Add(text, "injected_signal_errors", counts.injected_signal_errors);
    meta.Set("iop_thread_ops", text);
}

#if PS2R_IOP_ORACLE
void SubsystemCensus::RecordIpu(ps2r::verify::RunMeta& meta, const ps2r::ipu::IpuDevice& ipu,
                               const ps2r::ipu::LibipuApi& libipu, uint64_t ipu_dma_out_of_range) {
    ::RecordIpu(meta, ipu);
    meta.Set("ipu_dma_snapshot_errors", std::to_string(libipu.DmaSnapshotErrorCount()));
    const auto operations = libipu.Count();
    std::string ipu_operations;
    Add(ipu_operations, "init", operations.init);
    Add(ipu_operations, "sync", operations.sync);
    Add(ipu_operations, "stop", operations.stop);
    Add(ipu_operations, "restart", operations.restart);
    meta.Set("ipu_operations", ipu_operations);
    meta.Set("ipu", "divergent=" + std::to_string(ipu.DivergentCommandCount()) +
                   ",unhandled_writes=" + std::to_string(ipu.UnhandledWriteCount()) +
                   ",dmac_face_oor=" + std::to_string(ipu_dma_out_of_range));
}
#endif

void SubsystemCensus::RecordIpuNotLinked(ps2r::verify::RunMeta& meta) {
    // The play binary (ledger L7): the IPU model is not linked -- the same rows,
    // the never-executed text (the s3/s5 play census read exactly these values).
    meta.Set("ipu_commands", "bclr=0,idec=0,bdec=0,vdec=0,fdec=0,setiq=0,setvq=0,csc=0,pack=0,setth=0,unknown=0");
    meta.Set("ipu_unsupported", "bclr=0,idec=0,bdec=0,vdec=0,fdec=0,setiq=0,setvq=0,csc=0,pack=0,setth=0,unknown=0");
    meta.Set("ipu_unmodelled", "0");
    meta.Set("ipu_dma_snapshot_errors", "0");
    meta.Set("ipu_operations", "init=0,sync=0,stop=0,restart=0");
    meta.Set("ipu", "divergent=0,unhandled_writes=0,dmac_face_oor=0");
}

#if PS2R_IOP_ORACLE
SubsystemCensus::IopThreadOps SubsystemCensus::Snapshot(const ps2r::iop::hle::ThbaseModule& threads) {
    const auto c = threads.Count();
    IopThreadOps o;
    o.priority_calls = c.priority_calls;
    o.priority_changes = c.priority_changes;
    o.priority_errors = c.priority_errors;
    o.delay_kernel = c.delay_kernel;
    o.delay_host = c.delay_host;
    o.injected_wake_errors = c.injected_wake_errors;
    o.injected_signal_errors = c.injected_signal_errors;
    return o;
}
#endif
