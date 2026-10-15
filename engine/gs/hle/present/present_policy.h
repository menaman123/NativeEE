// gs/hle/present/present_policy.h -- PresentPolicy: what the play-mode host
// shows at each frame boundary (INTEGRATION_PLAN row 6.7).
//
// The console scans out a picture every field whether or not the game drew a
// new one: the CRTC re-reads the frame buffer at the display rate, so a frame
// the game did not finish, a load screen or a movie between pictures shows the
// LAST frame buffer again -- never nothing, never black. The policy is that
// rule, decided once per boundary (under the wall-locked Play policy one
// boundary = one pacer field, so "every boundary" IS the display rate):
//
//   Live  -- the GS produced a grabbable image this boundary: show it (and it
//            becomes the held image);
//   Hold  -- no new image this boundary (or the cap skipped this field): show
//            the held image again;
//   None  -- no image exists yet (before the title's first scanout).
//
// The FMV and load phases take the same table: a served picture the title put
// on the GS is a Live image, the boundaries between pictures are Holds.
//
// video.fps_cap (Config, bound here, 0 = uncapped): a cap below the display
// rate turns some live fields into Holds (an even accumulator share, never a
// sleep); the first image is always shown. Pure: no clock, no GPU, no I/O --
// the caller owns the pixels. The deterministic policy never consults it (the
// harness lanes are decided by the Verify slot's own rule).
#pragma once

#include <cstdint>
#include <string>

namespace ps2r {
namespace gs {

class PresentPolicy {
public:
    enum class Decision : uint8_t { None = 0, Live = 1, Hold = 2 };
    // The pacer's field rate: one present opportunity per wall vblank.
    static constexpr uint32_t kDisplayRateHz = 60;

    static const char* DecisionName(Decision d);

    // 0 = uncapped (every field); >= kDisplayRateHz = every field; else the share.
    void SetFpsCap(uint32_t cap);
    uint32_t FpsCap() const { return fps_cap_; }

    // One boundary: `live_available` = the GS produced a grabbable image this
    // boundary; `fmv_active` = a movie is in flight (census only: the table is
    // the same).
    Decision Decide(bool live_available, bool fmv_active);

    // A first image has been shown (Hold is possible).
    bool HasImage() const { return has_image_; }
    // A live restore (F8 in the running session): the held image is the previous
    // session's; forget it -- the restored session shows nothing until its own
    // first image, as a fresh process does. Counts are kept (one process).
    void ForgetImage();
    uint64_t Forgotten() const { return forgotten_; }
    uint64_t Lives() const { return lives_; }
    uint64_t Holds() const { return holds_; }
    uint64_t Nones() const { return nones_; }
    uint64_t Boundaries() const { return lives_ + holds_ + nones_; }
    uint64_t FmvBoundaries() const { return fmv_boundaries_; }
    uint64_t CapSkips() const { return cap_skips_; }
    // "live=<n>,hold=<n>,none=<n>,cap=<k>,cap_skips=<n>,fmv=<n>"
    std::string Census() const;

private:
    bool CapAdmits();

    uint32_t fps_cap_ = 0;
    uint32_t cap_acc_ = 0;
    bool has_image_ = false;
    uint64_t forgotten_ = 0;
    uint64_t lives_ = 0;
    uint64_t holds_ = 0;
    uint64_t nones_ = 0;
    uint64_t fmv_boundaries_ = 0;
    uint64_t cap_skips_ = 0;
};

}  // namespace gs
}  // namespace ps2r
