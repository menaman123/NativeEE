#pragma once
// GsStateSink: the GsSink the GIF parser feeds, implemented over the GsState
// façade. Each parser call maps onto the GS-side mutation it stands for (GS
// User's Manual chapter 7): a GIFtag resets Q (7.2); A+D / REGLIST / PACKED
// descriptors outside the decoded set are 64-bit register writes (7.3.2-7.3.3);
// the decoded PACKED items are re-packed into their PACKED layout for the
// façade's WritePacked (7.3.1); a vertex is a pre-decoded kick; one IMAGE
// quadword is two HWREG halves (7.3.4); EOP is the GifEop flush; a PATH3 pass
// that ended with no IMAGE transfer mid-way flushes a non-empty draw buffer.
// The counters are the composition root's census of what reached the façade.
//
// The identity stamp (DrawIR invariant 28): a kick an HLE program produced is
// submitted to the arbiter long before the drain delivers it to the parser,
// interleaved with PATH2/PATH3 packets, so the identity cannot be applied at
// submit time. The sink carries it across the queue: StageKickIdentity (the
// HLE runtime's observer, before the kick's submit) holds the next PATH1
// slot's identity; OnArbiterSubmit (the arbiter's submit observer) appends the
// staged identity -- or "raw" when nothing is staged -- to a FIFO, one entry per
// PATH1 packet the arbiter accepted; OnArbiterDeliver (the drain observer,
// before the packet reaches the parser) pops the entry and applies
// GsState::SetIdentity (HLE) or ClearIdentity (raw); PATH2/PATH3 packets clear
// it. PATH1 has one producer, so the FIFO order is the delivered order under
// every drain arrangement; the lock pays for the MTVU worker's submits.
#include <cstdint>
#include <deque>
#include <mutex>

#include "engine/gif/gs_sink.h"

struct GifArbiterPacket;

namespace ps2r {
namespace gs {
class GsState;
}  // namespace gs
}  // namespace ps2r

class GsStateSink : public GsSink {
public:
    explicit GsStateSink(ps2r::gs::GsState& gs) : gs_(gs) {}

    void BeginTag(const GifTag& tag, GifPath path) override;
    void WriteRegister(uint8_t reg, uint64_t value) override;
    void PackedRgbaq(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
    void PackedSt(float s, float t, float q) override;
    void PackedUv(uint16_t u, uint16_t v) override;
    void PackedFog(uint8_t f) override;
    void KickVertex(float x, float y, float z, uint8_t fog, bool draw) override;
    void ImageData(uint64_t lo, uint64_t hi) override;
    void ImageSpan(const uint8_t* data, size_t qwords) override;
    void EndOfPacket() override;
    void FinishPath3() override;

    uint64_t Tags() const { return tags_; }
    uint64_t RegisterWrites() const { return register_writes_; }  // WriteRegister calls forwarded
    uint64_t PackedWrites() const { return packed_writes_; }      // RGBAQ / ST / UV / FOG PACKED items
    uint64_t Kicks() const { return kicks_; }
    uint64_t ImageQwords() const { return image_qwords_; }
    uint64_t Eops() const { return eops_; }
    uint64_t Path3Finishes() const { return path3_finishes_; }
    // The same census by the path of the tag in progress (index = GifPath):
    // what each path delivered to the façade, parsed or lowered.
    uint64_t TagsOn(GifPath path) const { return tags_by_path_[static_cast<unsigned>(path) & 3]; }
    uint64_t KicksOn(GifPath path) const { return kicks_by_path_[static_cast<unsigned>(path) & 3]; }
    uint64_t RegisterWritesOn(GifPath path) const { return regs_by_path_[static_cast<unsigned>(path) & 3]; }
    uint64_t ImageQwordsOn(GifPath path) const { return image_by_path_[static_cast<unsigned>(path) & 3]; }

    // ---- attribution rows (track 1c-2) ----
    // PATH1 tags / kicks by the title's render bucket (TR renderer.h:431; 28 =
    // no hint). The host sets the hint from the chain walk (the bucket whose
    // `first` tag the walk passed last); it is exact only when the VIF1 feed is
    // inline (the deterministic profile) -- the MTVU staged feed runs the
    // programs after the walk, so the host passes -1 and the row is unattributed.
    static constexpr unsigned kBuckets = 28;
    void SetBucketHint(int bucket) { bucket_ = bucket < 0 || bucket >= int(kBuckets) ? kBuckets : unsigned(bucket); }
    uint64_t Path1TagsInBucket(unsigned bucket) const { return tags_by_bucket_[bucket <= kBuckets ? bucket : kBuckets]; }
    uint64_t Path1KicksInBucket(unsigned bucket) const { return kicks_by_bucket_[bucket <= kBuckets ? bucket : kBuckets]; }
    // PATH2 packets by the class their first GIFtag names (the submit observer
    // sees the DIRECT payload): Image (FLG IMAGE), TexRegs (A+D led by
    // TEXFLUSH: the title's WriteTextureRegisters), TransferHead (A+D led by a
    // BITBLTBUF/TRXPOS/TRXREG/TRXDIR), State (any other A+D), Geometry (PACKED
    // naming a vertex register or PRE), Reglist, Other.
    enum class Path2Class : unsigned { Image, TexRegs, TransferHead, State, Geometry, Reglist, Other, Count };
    static const char* Path2ClassName(Path2Class c);
    uint64_t Path2Packets(Path2Class c) const { return path2_packets_[static_cast<unsigned>(c)]; }
    uint64_t Path2Bytes(Path2Class c) const { return path2_bytes_[static_cast<unsigned>(c)]; }
    static Path2Class ClassifyPath2(const uint8_t* data, size_t size);

    // ---- identity stamp (invariant 28) ----
    // The HLE runtime's per-kick report: the next PATH1 submit carries it.
    void StageKickIdentity(uint32_t hle_handler, uint64_t image_hash, uint16_t kick_pc, uint16_t kick_ord);
    void ClearStagedIdentity();
    // The frame instance stamped with every identity (the root's boundary count).
    void SetFrameInstance(uint32_t frame_inst) { frame_inst_ = frame_inst; }
    // GifArbiter::SubmitObserver / DrainObserver trampolines (`user` = this sink).
    static void OnArbiterSubmit(void* user, GifPath path, const uint8_t* data, size_t size);
    static void OnArbiterDeliver(void* user, const GifArbiterPacket& pkt);

    uint64_t IdentityStamped() const { return identity_stamped_; }    // PATH1 packets delivered with an HLE identity
    uint64_t IdentityRaw() const { return identity_raw_; }            // PATH1 packets delivered raw
    uint64_t IdentityUnderflows() const { return identity_underflows_; }  // PATH1 packets with no FIFO entry (tripwire)
    size_t IdentityPending() const;

private:
    struct SlotIdentity {
        bool hle;
        uint32_t hle_handler;
        uint64_t image_hash;
        uint16_t kick_pc;
        uint16_t kick_ord;
    };
    void PushSlot();
    void ApplySlot(bool path1, bool has_data);

    ps2r::gs::GsState& gs_;
    SlotIdentity staged_{};
    bool staged_valid_ = false;
    uint32_t frame_inst_ = 0;
    mutable std::mutex fifo_mtx_;
    std::deque<SlotIdentity> fifo_;
    uint64_t identity_stamped_ = 0;
    uint64_t identity_raw_ = 0;
    uint64_t identity_underflows_ = 0;
    uint64_t tags_ = 0;
    uint64_t register_writes_ = 0;
    uint64_t packed_writes_ = 0;
    uint64_t kicks_ = 0;
    uint64_t image_qwords_ = 0;
    uint64_t eops_ = 0;
    uint64_t path3_finishes_ = 0;
    unsigned path_ = 3;  // the path of the tag in progress
    uint64_t tags_by_path_[4] = {};
    uint64_t kicks_by_path_[4] = {};
    uint64_t regs_by_path_[4] = {};
    uint64_t image_by_path_[4] = {};
    unsigned bucket_ = kBuckets;
    uint64_t tags_by_bucket_[kBuckets + 1] = {};
    uint64_t kicks_by_bucket_[kBuckets + 1] = {};
    uint64_t path2_packets_[static_cast<unsigned>(Path2Class::Count)] = {};
    uint64_t path2_bytes_[static_cast<unsigned>(Path2Class::Count)] = {};
};
