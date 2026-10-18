// core/savestate/savestate_format.h -- the on-disk .ps2st state-file
// envelope (docs: SAVESTATE_SCHEMA). FROZEN bit-for-bit: every anchor state
// the harness replays was written in this layout and must re-serialize
// byte-identical (tests/core/test_savestate_format.cpp).
//
// Layout, all little-endian:
//
//   FileHeader                         (32 bytes)
//   SectionHeader + payload  x N       (24-byte header each)
//   u32 trailer_crc                    (CRC32 of EVERY byte before it)
//
// Section payloads are opaque byte blobs to this module; SectionRegistry
// owns which tags exist and who serializes them. Pure stdio + vector so the
// full writer / reader / validation matrix is unit-testable without a VM.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ps2r::savestate {

// CRC32, IEEE 802.3 reflected polynomial (zlib-compatible). `seed` is a
// previously FINALIZED crc; passing it chains over consecutive buffers.
uint32_t Crc32(const void* data, size_t len, uint32_t seed = 0);

// ---- format constants ------------------------------------------------------
constexpr char kMagic[8] = {'P', 'S', '2', 'S', 'A', 'V', 'E', '1'};

// File versions. The FILE version says which OPTIONAL sections are present so
// an older reader rejects the file outright instead of half-restoring it:
//   1  original; 2  GSST section v2 (latched GS vertex state);
//   3  TIMR (live EE timer state) present;  4  SNDC (host sound core) present.
// A writer stamps the LOWEST version that covers the sections it includes,
// so a file without TIMR/SNDC stays a byte-identical v2 file.
constexpr uint32_t kFileVersion     = 2;
constexpr uint32_t kFileVersionTimr = 3;
constexpr uint32_t kFileVersionSnd  = 4;
constexpr uint32_t kMinFileVersion  = 1;
constexpr uint32_t kMaxFileVersion  = 4;

// Section flag: a reader MUST reject the file if it does not know this tag.
constexpr uint32_t kSectionRequired = 0x1;

// Sanity cap on one section payload (main RAM + slack); larger = corrupt.
constexpr uint64_t kMaxSectionPayload = 64ull << 20;

// FourCC packing (first character in the low byte).
constexpr uint32_t Tag(char a, char b, char c, char d) {
    return (uint32_t)(uint8_t)a | ((uint32_t)(uint8_t)b << 8) | ((uint32_t)(uint8_t)c << 16) |
           ((uint32_t)(uint8_t)d << 24);
}

// Section tags of the frozen schema. Every v1 section is REQUIRED; the
// payload codecs live beside the module that owns the state (Phases 3-8).
constexpr uint32_t TAG_RAM  = Tag('R', 'A', 'M', '0');  // 32 MB main RAM
constexpr uint32_t TAG_SPAD = Tag('S', 'P', 'A', 'D');  // 16 KB scratchpad
constexpr uint32_t TAG_META = Tag('M', 'E', 'T', 'A');  // meta arena
constexpr uint32_t TAG_VRAM = Tag('V', 'R', 'A', 'M');  // 4 MB GS local memory
constexpr uint32_t TAG_VU0C = Tag('V', 'U', '0', 'C');
constexpr uint32_t TAG_VU0D = Tag('V', 'U', '0', 'D');
constexpr uint32_t TAG_VU1C = Tag('V', 'U', '1', 'C');
constexpr uint32_t TAG_VU1D = Tag('V', 'U', '1', 'D');
constexpr uint32_t TAG_THRD = Tag('T', 'H', 'R', 'D');  // thread table + contexts + chains
constexpr uint32_t TAG_SEMA = Tag('S', 'E', 'M', 'A');
constexpr uint32_t TAG_EVFL = Tag('E', 'V', 'F', 'L');
constexpr uint32_t TAG_INTC = Tag('I', 'N', 'T', 'C');
constexpr uint32_t TAG_DMAC = Tag('D', 'M', 'A', 'C');
constexpr uint32_t TAG_TIMR = Tag('T', 'I', 'M', 'R');
constexpr uint32_t TAG_GSPR = Tag('G', 'S', 'P', 'R');  // privileged regs + video mode
constexpr uint32_t TAG_GSST = Tag('G', 'S', 'S', 'T');  // GS drawing state
constexpr uint32_t TAG_VIFS = Tag('V', 'I', 'F', 'S');
constexpr uint32_t TAG_GIFS = Tag('G', 'I', 'F', 'S');
constexpr uint32_t TAG_PADS = Tag('P', 'A', 'D', 'S');
constexpr uint32_t TAG_FILE = Tag('F', 'I', 'L', 'E');  // open-file table {path, offset, mode}
constexpr uint32_t TAG_CNTR = Tag('C', 'N', 'T', 'R');  // frame / boundary counters
constexpr uint32_t TAG_MISC = Tag('M', 'I', 'S', 'C');  // allocators, boundary thread id
constexpr uint32_t TAG_HEAP = Tag('H', 'E', 'A', 'P');  // HLE heap + meta-arena bookkeeping
constexpr uint32_t TAG_STRM = Tag('S', 'T', 'R', 'M');  // CD streaming HLE state
constexpr uint32_t TAG_SIFS = Tag('S', 'I', 'F', 'S');  // SIF mailbox / handshake + pending RPCs
constexpr uint32_t TAG_VU1R = Tag('V', 'U', '1', 'R');  // VU1 register file + running flag
constexpr uint32_t TAG_SDRX = Tag('S', 'D', 'R', 'X');  // SPU2 block transfers (libsdr)
constexpr uint32_t TAG_MCHL = Tag('M', 'C', 'H', 'L');  // memory-card HLE statics
constexpr uint32_t TAG_IOPM = Tag('I', 'O', 'P', 'M');  // IOP RAM (2 MB) + IOP statics
constexpr uint32_t TAG_SNDC = Tag('S', 'N', 'D', 'C');  // host sound core (SPU RAM + voices)
constexpr uint32_t TAG_IOPH = Tag('I', 'O', 'P', 'H');  // a hosted IOP: RAM + kernel tables + module/library/RPC records

#pragma pack(push, 1)
struct FileHeader {
    char     magic[8];
    uint32_t version;
    uint32_t section_count;
    uint32_t vsync_frame;
    uint32_t flags;
    uint64_t wall_ms;
};
struct SectionHeader {
    uint32_t tag;
    uint32_t version;
    uint32_t flags;
    uint32_t crc32;        // of the payload bytes
    uint64_t payload_len;
};
#pragma pack(pop)
static_assert(sizeof(FileHeader) == 32, "FileHeader layout is frozen");
static_assert(sizeof(SectionHeader) == 24, "SectionHeader layout is frozen");

// One section as carried between the registry and the codec.
struct Blob {
    uint32_t tag = 0;
    uint32_t version = 1;
    uint32_t flags = kSectionRequired;
    std::vector<uint8_t> payload;
};

// File-level fields. `file_version` is what the writer stamps and what the
// reader saw (kept so a parsed file re-serializes byte-identical).
struct FileMeta {
    uint32_t vsync_frame = 0;
    uint32_t flags = 0;
    uint64_t wall_ms = 0;
    uint32_t file_version = kFileVersion;
};

// Serialize a complete state file image (header, sections, trailer CRC).
std::vector<uint8_t> SerializeStateFile(const FileMeta& meta, const std::vector<Blob>& sections);

// Write a complete state file ATOMICALLY: the image goes to `path + ".tmp"`,
// is flushed and closed, then renamed into place -- the final path is either
// the complete new file or byte-untouched, never partial. Returns false +
// *err on any failure (the tmp file is removed).
bool WriteStateFile(const std::string& path, const FileMeta& meta, const std::vector<Blob>& sections,
                    std::string* err);

// Parse + fully validate a state image: magic, version range, per-section
// CRC, unknown-REQUIRED-section policy (known_tags == nullptr accepts every
// tag), trailer CRC, truncation. Returns false + *err on any failure.
bool ParseStateFile(const uint8_t* data, size_t len, FileMeta* meta, std::vector<Blob>* sections,
                    std::string* err, const uint32_t* known_tags = nullptr, size_t known_tag_count = 0);

// ParseStateFile over a file's bytes.
bool ReadStateFile(const std::string& path, FileMeta* meta, std::vector<Blob>* sections, std::string* err,
                   const uint32_t* known_tags = nullptr, size_t known_tag_count = 0);

// Printable FourCC ("RAM0") for messages.
std::string TagName(uint32_t tag);

// Little-endian POD append helpers for payload assembly.
template <typename T>
void PutPod(std::vector<uint8_t>& v, const T& x);
void PutBytes(std::vector<uint8_t>& v, const void* p, size_t n);

}  // namespace ps2r::savestate

#include "engine/core/savestate/savestate_format.inl"
