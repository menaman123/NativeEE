#pragma once
// Delivers HLE-produced packets in their existing ordered output slot. The
// supported producer shape is one complete PACKED tag ending in EOP with its
// whole payload and nothing after it: the {ST, RGBAQ, XYZ2/XYZF2} strip packet
// (vertices pre-decoded, straight to the GS assembly / DrawIR sink path), the
// A+D register block a material pass hands through before its geometry (one
// descriptor 0x0E; address/value pairs through the parser's register
// dispatcher), and any other descriptor list (the sprite, particle and decal
// tags; each quadword through the parser's PACKED dispatcher). Multi-tag,
// no-EOP, truncated, REGLIST and IMAGE output retains the raw route. Byte
// construction by the producer remains a dependency of this interim boundary.
#include <cstdint>
#include <memory>
#include <vector>

class GifArbiter;
class GsSink;

class KickLowering {
public:
    struct Census {
        uint64_t submitted = 0;         // Submit calls, including short packets
        uint64_t queued = 0;            // supported packets queued for direct delivery
        uint64_t lowered = 0;           // direct deliveries completed
        uint64_t queued_state = 0;      // of `queued`: A+D register blocks
        uint64_t lowered_state = 0;     // of `lowered`: A+D register blocks
        uint64_t queued_packed = 0;     // of `queued`: other single-tag PACKED descriptor lists
        uint64_t lowered_packed = 0;    // of `lowered`: other single-tag PACKED descriptor lists
        uint64_t raw_fallback = 0;      // raw route selected (submit or delivery)
        uint64_t unsupported = 0;       // packet is outside the bounded producer shape
        uint64_t unconfigured = 0;      // no direct sink supplied
        uint64_t ordering_fallback = 0; // parser busy/buffered/masked at delivery
    };
    // The producing program of one submitted kick, as the library names it:
    // the registered program hash (the registration table's key), the entry's
    // name and the MSCAL entry pc. Observation only; a default Origin is "unknown".
    struct Origin {
        uint64_t program_hash = 0;
        const char* name = nullptr;
        uint16_t entry_pc = 0;
    };
    // One census row: unsupported submits attributed to their producing program
    // and the packet shape that was declined (`<format>:<regs>[:noeop][:tail|trunc]`,
    // e.g. `packed:e` is an A+D state packet, `packed:215:tail` geometry followed
    // by more data, `short` fewer than 16 bytes).
    struct UnsupportedByProgram {
        Origin origin;
        char shape[24] = {};
        uint64_t count = 0;
    };

    // Raw-only compatibility constructor.
    explicit KickLowering(GifArbiter& arbiter);
    // `sink` is the same consumer/fanout used by the raw parser. It must outlive
    // all queued deliveries. GS state is read/mutated only on the drain owner.
    KickLowering(GifArbiter& arbiter, GsSink& sink);
    virtual ~KickLowering();

    // One kick -> one arbiter slot, preserving existing submit/drain observers,
    // MTVU markers, inherited state and primitive carry across packet boundaries.
    void Submit(std::vector<uint8_t>&& kick);
    // The same, naming the producing program for the unsupported census.
    void Submit(std::vector<uint8_t>&& kick, const Origin& origin);

    uint64_t submitted() const;
    Census census() const;
    // The unsupported submits by producing program, largest count first.
    std::vector<UnsupportedByProgram> unsupported_by_program() const;

protected:
    // True means an owned direct candidate occupies the original slot. A
    // preceding partial raw packet can still require raw delivery in that slot.
    virtual bool TryLower(const std::vector<uint8_t>& kick);

private:
    struct Counters;
    void NoteUnsupported(const std::vector<uint8_t>& kick);
    GifArbiter& arbiter_;
    GsSink* sink_ = nullptr;
    std::shared_ptr<Counters> counters_;
    Origin origin_;  // the program of the Submit in progress (one thread per lowering)
};
