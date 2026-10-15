// core/log/ring_format.h -- binary layout of the always-on event ring and of its
// .rlog dump files. FROZEN: shared by the in-process writer (RingLog), the
// decoder (RingDecoder) and every offline tool that reads dumps. POD only.
//
// FILE LAYOUT (.rlog):
//   FileHeader
//   ring_count x { RingHeader, name bytes (name_len, no NUL),
//                  retained x Event (oldest first) }
// All little-endian. Event is exactly 24 bytes; the static_asserts pin every
// size so writer and decoder cannot drift.
#pragma once
#include <cstdint>

namespace ps2r {
namespace ringlog {

enum Channel : uint8_t {
    CH_PERF  = 0,
    CH_EE    = 1,
    CH_GFX   = 2,
    CH_INPUT = 3,
    CH_MEM   = 4,
    CH_COUNT = 5,
};

enum EventFlags : uint8_t {
    EVF_VERBOSE = 0x01,   // emitted through the verbose tier
};

struct Event {
    uint16_t tag;      // ring_tags.h id
    uint8_t  channel;  // Channel
    uint8_t  flags;    // EventFlags
    uint32_t frame;    // presented-frame counter at emit time
    uint64_t a0;       // tag-defined
    uint64_t a1;       // tag-defined
};
static_assert(sizeof(Event) == 24, "Event must be 24 bytes");

constexpr char     kMagic[8] = {'P', 'S', '2', 'R', 'L', 'O', 'G', '1'};
constexpr uint32_t kVersion  = 1;
constexpr uint32_t kNameMax  = 15;   // ring name bytes (+ NUL in-process)

enum DumpReason : uint32_t {
    DUMP_CRASH        = 1,  // signal handler
    DUMP_HOTKEY       = 2,  // user hotkey
    DUMP_EXIT         = 3,  // normal shutdown
    DUMP_FATAL_NOJUMP = 4,  // generated could-not-jump terminal path
    DUMP_TEST         = 5,  // unit tests / synthetic
};

struct FileHeader {
    char     magic[8];        // kMagic
    uint32_t version;         // kVersion
    uint32_t event_size;      // sizeof(Event) == 24
    uint32_t ring_count;      // sections that follow
    uint32_t reason;          // DumpReason
    uint32_t vsync_frame;     // frame counter at dump time
    uint32_t slots_exhausted; // threads that could not get a ring
    uint64_t wall_ms;         // unix wall clock ms at dump time
    uint64_t total_drops;     // sum over rings of (emitted - retained)
};
static_assert(sizeof(FileHeader) == 48, "FileHeader must be 48 bytes");

struct RingHeader {
    uint32_t name_len;        // bytes of name that follow (<= 15)
    uint32_t retained;        // events in this section
    uint64_t total_emitted;   // lifetime emit count for this ring
};
static_assert(sizeof(RingHeader) == 16, "RingHeader must be 16 bytes");

}  // namespace ringlog
}  // namespace ps2r
