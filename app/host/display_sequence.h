// app/host/display_sequence.h -- DisplaySequence: the harness's scripted display
// changes (`--set harness.display_sequence=<frame>:<mode>[,...]`, Charter 3 item 4,
// pin RootScreenModeSwap). Each step names the frame boundary from which the
// Display page's request for <mode> is submitted ONCE through SessionControls --
// the same route the menu widget takes -- so the swap itself runs where the
// product runs it: on the presenter's thread, between two presented frames,
// while the game thread keeps ticking. The parser is pure (the validator pinned
// the format; a malformed item here is skipped, never a fault).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "app/host/session_controls.h"

namespace ps2r::app {

class DisplaySequence {
public:
    struct Step {
        uint64_t frame = 0;                 // the boundary count from which the request is submitted
        SessionControls::Action action{};   // SetDisplayMode / SetScale / SetVSync
        int value = 0;                      // the request's value (mode 0..2, scale 1..4, vsync 0/1)
        std::string mode;                   // the item's mode name, for the loud line
        bool submitted = false;
        // The SWAP WINDOW of a presenter step (a mode / vsync change): from the
        // submitting boundary to kSettleBoundaries after the boundary that first
        // saw the request finished. The audio ring's pad growth over it is the
        // step's `swap_padded` -- a pad the swap caused (the game thread stalled,
        // the ring ran dry), as against a pad of the title's own late tick
        // elsewhere in the run (the clock contract's rule 4, pad mode).
        uint64_t id = 0;                    // SessionControls request id (0 = refused: no window)
        uint64_t padded_at_submit = 0;
        uint64_t settle_until = 0;          // 0 while the request is in flight; the closing boundary after
        bool closed = false;
    };
    static constexpr uint64_t kSettleBoundaries = 6;   // 100 ms after the presenter reported the swap done
    // "<frame>:<mode>[,...]" -> steps in the order written (frame > 0; mode one of
    // windowed | fullscreen | borderless | scale1..scale4 | vsync | novsync).
    // Returns the number of items that did not parse (0 for a validated string).
    static int Parse(const std::string& text, std::vector<Step>& out);
    // One item's mode name -> action/value; false for an unknown name.
    static bool ModeOf(const std::string& mode, SessionControls::Action& action, int& value);

    explicit DisplaySequence(const std::string& text) { skipped_ = Parse(text, steps_); }
    bool Empty() const { return steps_.empty(); }
    size_t Size() const { return steps_.size(); }
    int Skipped() const { return skipped_; }
    const std::vector<Step>& Steps() const { return steps_; }
    // The boundary: every step whose frame has come and that was not yet
    // submitted goes through `submit` and is marked. Returns the steps submitted
    // at this boundary (0 or more). The first submission's boundary is remembered
    // (FirstFrame) with the caller's `padded` reading (the audio ring's pad count
    // then: the census's base for "no pad since the first display change").
    // `submit` returns the request id (0 = refused). `finished(id)` reads whether
    // the presenter has reported the request terminal (SessionControls' recent
    // results); a presenter step's swap window closes kSettleBoundaries after
    // the first boundary that saw it finished, adding the ring's pad growth
    // since the submission to SwapPadded().
    int OnBoundary(uint64_t boundary_count, uint64_t padded, const std::function<uint64_t(const Step&)>& submit,
                   const std::function<bool(uint64_t)>& finished = {});
    int Submitted() const { return submitted_; }
    uint64_t FirstFrame() const { return first_frame_; }          // 0 = nothing submitted yet
    uint64_t PaddedAtFirst() const { return padded_at_first_; }
    // The steps that run on the presenter's thread (a mode or vsync change: the
    // SDL window / context call); a scale step runs on the GS consumer.
    int PresenterSteps() const;
    // The pad growth summed over the closed swap windows; the presenter steps
    // whose window closed / is still open (in flight or settling).
    uint64_t SwapPadded() const { return swap_padded_; }
    int SwapClosed() const { return swap_closed_; }
    int SwapOpen() const;
    // run.meta display_sequence=: steps / submitted / skipped / first_frame /
    // padded_at_first, plus the caller's current pad count and the game thread's
    // longest frame work since the first step (ms), swap_padded / swap_closed /
    // swap_open -- "off" without steps.
    std::string Census(uint64_t padded_now, uint64_t work_max_since_first_us) const;

private:
    std::vector<Step> steps_;
    int skipped_ = 0;
    int submitted_ = 0;
    uint64_t first_frame_ = 0;
    uint64_t padded_at_first_ = 0;
    uint64_t swap_padded_ = 0;
    int swap_closed_ = 0;
};

}  // namespace ps2r::app
