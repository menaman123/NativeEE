// core/log/ring_tags.h -- the ring-log tag registry: the SINGLE source of truth
// for every event tag. FROZEN schema (docs: RING_LOG_SCHEMA): ids, channels
// and names are read by the decoder and by offline tooling, so the table is
// carried verbatim from the frozen original.
//
// RING_TAG(sym, id, channel, arg0_doc, arg1_doc)
//   id is namespaced by channel: 0x01xx EE, 0x02xx GFX, 0x03xx INPUT,
//   0x04xx PERF, 0x05xx MEM. Packing conventions in the arg docs:
//   x<<32|y = two u32 in one u64 (hi=x, lo=y); chars8 = first 8 ASCII bytes.
#pragma once
#include <cstddef>
#include <cstdint>

#include "engine/core/log/ring_format.h"

#define PS2R_RING_TAG_TABLE(X)                                                 \
    X(BOOT_SM_EDGE,    0x0101, CH_EE,    "prev_case<<32|cur_case",             \
                                          "state word0 (raw u64)")             \
    X(MANUAL_DISPATCH, 0x0102, CH_EE,    "dispatcher EE addr",                 \
                                          "a0_reg<<32|ra_reg")                 \
    X(INDIRECT_JUMP,   0x0103, CH_EE,    "jump target EE addr",                \
                                          "source func EE addr (innermost)")   \
    X(FMV_PEND_EDGE,   0x0104, CH_EE,    "state word0 BEFORE (raw u64)",       \
                                          "state word0 AFTER (raw u64)")       \
    X(FMV_OPEN,        0x0105, CH_EE,    "movie name chars8",                  \
                                          "pstate_pre16<<48|pstate_post16<<32|ret32") \
    X(FMV_ADV_ARGS,    0x0106, CH_EE,    "fmv_idx<<32|skip_flag",              \
                                          "v0 return value")                   \
    X(TSIG,            0x0201, CH_GFX,   "FRAME.FBP byte addr (0xFFFFFFFF=overflow slot)", \
                                          "DrawBatch count this frame")        \
    X(DISPFB_FLIP,     0x0202, CH_GFX,   "new DISPFB fb byte addr",            \
                                          "flip sequence number")              \
    X(FRAME_PRESENT,   0x0203, CH_GFX,   "cumulative live draw count",         \
                                          "render queue depth at present")     \
    X(CLUT_TEX_MINT,   0x0204, CH_GFX,   "cache key (tbp/csa-derived)",        \
                                          "CLUT-tex cache size after insert")  \
    X(TEX_MINT,        0x0205, CH_GFX,   "cache key (tbp-derived)",            \
                                          "tex cache size after insert")       \
    X(INLAT_T1,        0x0301, CH_INPUT, "t_us (steady clock)",                \
                                          "old_buttons16<<16|new_buttons16")   \
    X(INLAT_T2,        0x0302, CH_INPUT, "t_us (steady clock)",                \
                                          "syncv32<<32|old_wire16<<16|new_wire16") \
    X(INLAT_T3,        0x0303, CH_INPUT, "t_us (steady clock)",                \
                                          "syncv32<<32|old_served16<<16|new_served16") \
    X(INLAT_T5,        0x0304, CH_INPUT, "t_us (steady clock)",                \
                                          "ee_frame the reaction belongs to")  \
    X(INLAT_RATE,      0x0305, CH_INPUT, "site (1=input_frame 3=padread)",     \
                                          "syncv32<<32|calls_per_s32")         \
    X(INLAT_T4G,       0x0306, CH_INPUT, "t_us (steady clock)",                \
                                          "ee_frame_armed32<<32|prev_served16<<16|served16") \
    X(FRAME_TIME,      0x0401, CH_PERF,  "t_us at present (steady clock)",     \
                                          "frame_interval_us (delta to prev present)") \
    X(REGIME_CENSUS,   0x0402, CH_PERF,  "inj_presents_1s<<32|dispfb_flips_1s", \
                                          "ee_pushes_1s<<32|vblank_tick_idx32") \
    X(MTVU_SYNC_SITE,  0x0403, CH_PERF,  "site id (vu1 worker sync_site order)", \
                                          "hits_1s<<32|blocked_1s")            \
    X(DEFER_CENSUS,    0x0404, CH_PERF,  "bnd_enq_1s<<32|bnd_del_1s",          \
                                          "fence_1s<<32|wbusy_ms_x10_1s")      \
    X(MTVU_BACKPRESSURE, 0x0405, CH_PERF, "pipeline bytes at engage",          \
                                          "wait_ns spent in the drain loop")   \
    X(RUNAHEAD_DEPTH,  0x0406, CH_PERF,  "ee vsync pushes (EE frames declared)", \
                                          "ee frames processed (presented); depth-in-frames = a0-a1") \
    X(RUNAHEAD_ALARM,  0x0408, CH_PERF,  "episode peak depth_frames",          \
                                          "cap_frames<<32|overrun_presents")   \
    X(SLOWSPOT,        0x0407, CH_PERF,  "window_p50_us<<32|window_p95_us",    \
                                          "threshold_us<<32|window_presents")  \
    X(MEM_GAUGE,       0x0501, CH_MEM,   "gauge_id (see ringlog::GaugeId)",    \
                                          "bytes held (u64)")                  \
    X(MEM_GAUGE_COUNT, 0x0502, CH_MEM,   "gauge_id",                           \
                                          "entry count (u64)")                 \
    X(RSS_SAMPLE,      0x0503, CH_MEM,   "phys_footprint bytes",               \
                                          "resident_size bytes")               \
    X(TEST_EVENT,      0x0001, CH_PERF,  "test-defined",                       \
                                          "test-defined")

namespace ps2r {
namespace ringlog {

enum Tag : uint16_t {
#define PS2R_RING_TAG_ENUM(sym, id, ch, d0, d1) TAG_##sym = id,
    PS2R_RING_TAG_TABLE(PS2R_RING_TAG_ENUM)
#undef PS2R_RING_TAG_ENUM
};

struct TagInfo {
    uint16_t    id;
    uint8_t     channel;
    const char* name;
    const char* arg0_doc;
    const char* arg1_doc;
};

// The registry table (for decoders / schema tooling) and a lookup by id.
const TagInfo* TagTable(size_t* count);
const TagInfo* FindTag(uint16_t id);

}  // namespace ringlog
}  // namespace ps2r
