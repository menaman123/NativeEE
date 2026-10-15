#pragma once
// The GIF packet parser: walks GIFtag + payload streams arriving on any of the
// three paths and hands every GS state mutation to a GsSink in stream order.
// One instance serves all paths (the GIF is a single-state machine); a tag
// left incomplete by one call resumes on the next, and bytes that do not end
// on a quadword boundary wait in a tail buffer.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/gif/gif_regs.h"
#include "engine/gif/gif_tag.h"
#include "engine/gif/gs_sink.h"

// Parser knobs. `bulk_lanes` selects the strip3 / rowable PACKED lanes for
// eligible tags; off routes every PACKED quadword through the generic per-QW
// body. Both produce the identical sink sequence (proved by the equivalence
// test); the knob exists for that proof, it is not a configuration setting.
struct GifParserSettings {
    bool bulk_lanes = true;
};

class GifParser {
public:
    GifParser(GsSink& sink, GifRegs& regs, GifParserSettings settings = GifParserSettings{});

    // Deliver `size` bytes arriving on `path`. A PATH3 packet arriving while a
    // tag is still in transfer abandons that tag and any buffered tail (each
    // PATH3 chain is a self-contained stream starting with a GIFtag); PATH1/2
    // continuations are legal. While MODE.M3R is set the data is held in the
    // tail and not parsed, whatever the path.
    void ReceiveData(GifPath path, const uint8_t* data, size_t size);

    // After a drain pass that delivered PATH3: an incomplete IMAGE transfer is
    // kept open for the next DMA and nothing else happens; any other
    // incomplete tag is abandoned, the GS is told to finish, the tail is cleared.
    void FinishPath3();

    size_t BufferedBytes() const { return tail_.size(); }
    const GifTransferState& Transfer() const { return regs_.Transfer(); }
    // A producer-owned complete draw may bypass parsing only between streams.
    // Query on the parser/drain owner; it does not consume buffered input.
    bool CanAcceptDirect() const {
        return tail_.empty() && !regs_.Transfer().in_transfer && !regs_.Path3Masked();
    }

    // Lane census (plain counters, single writer): strip3 triples and rowable
    // rows consumed by the bulk lanes, and quadwords consumed by the bulk branch.
    uint64_t Strip3Triples() const { return strip3_triples_; }
    uint64_t RowableRows() const { return rowable_rows_; }
    uint64_t BulkQuads() const { return bulk_quads_; }
    // Parse census (plain counters, single writer): ReceiveData deliveries and
    // bytes per path (index = GifPath), i.e. the byte-form GIF parse that
    // executed. All zero = the parser never ran.
    uint64_t Receives(GifPath path) const { return receives_[static_cast<unsigned>(path) & 3]; }
    uint64_t ReceivedBytes(GifPath path) const { return received_bytes_[static_cast<unsigned>(path) & 3]; }

private:
    void ProcessBuffer();
    size_t ParseSpan(const uint8_t* buf, size_t len);
    bool AcceptTag(const uint8_t* p);
    size_t ConsumeImage(const uint8_t* p, size_t avail_qw);
    size_t ConsumePackedBulk(const uint8_t* p, size_t avail_qw);
    void ConsumePackedGeneric(const uint8_t* qw);
    void ConsumeReglist(const uint8_t* qw);
    void ProcessPacked(uint8_t reg, const uint8_t* qw);
    void ProcessPackedKnown(uint8_t reg, const uint8_t* qw);
    void DispatchRegister(uint8_t reg, uint64_t value);
    void StageTriple(const uint8_t* p, bool xyzf);
    void AdvanceDescriptor();

    GsSink& sink_;
    GifRegs& regs_;
    GifParserSettings settings_;
    GifPath active_path_ = GifPath::Path3;
    GifTagClass tag_class_{};
    std::vector<uint8_t> tail_;
    uint64_t strip3_triples_ = 0;
    uint64_t rowable_rows_ = 0;
    uint64_t bulk_quads_ = 0;
    uint64_t receives_[4] = {};
    uint64_t received_bytes_[4] = {};
};
