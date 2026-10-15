// core/triage/input_log.h -- the ONE pad-script implementation: records the
// SERVED pad state per vsync frame as grammar tokens and replays such a
// script onto the served state (EQUIVALENCE_PLAN section 2 "input
// recorder"; TRIAGE_CAPTURE section 4.5).
//
// Grammar (one token per line in a .padlog, or comma-joined in a spec):
//     F1-F2:BTN          button held over vsync frames [F1,F2] inclusive
//     F1-F2:AX=<0..255>  axis segment, AX in LX/LY/RX/RY (0x80 = centre)
//     # ...              comment (the file header carries start frame + title)
// Button tokens cover the full DualShock wire word: SELECT L3 R3 START UP
// RIGHT DOWN LEFT L2 R2 L1 R1 TRIANGLE CIRCLE CROSS SQUARE (bits 0..15).
// The recorder's output is directly replayable: a loader joins non-comment
// lines with commas and feeds the same parser.
//
// The script's CLOCK -- which counter the frame numbers index -- is named by
// the header (`clock=frame` | `clock=tick`; absent = tick, the grammar's
// original):
//     tick   the logical input tick (the emulated time domain's field count).
//            In play that count is wall-paced (a late boundary consumes the
//            fields that elapsed; a park consumes the fields it waited), so a
//            tick-keyed recording of a play session names ticks the
//            deterministic profile reaches at other game frames.
//     frame  the game frame: the boundary ordinal the pad was served at,
//            the frames.fhash key of the same boundary (a cold boot: 1, 2, 3;
//            a restored run: restored_vsync_frame + 1, ...). The title reads
//            the pad once per boundary, so this is the title's own input
//            clock and is identical in every profile. The host recorder
//            writes this clock.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps2r::triage {

class InputLog {
public:
    enum class Clock : uint8_t { Tick, Frame };
    static const char* ClockToken(Clock c);
    // The key a script with this clock indexes for a boundary at logical
    // tick `tick` and game frame `frame`.
    uint32_t Key(uint64_t tick, uint32_t frame) const {
        return clock_ == Clock::Frame ? frame : static_cast<uint32_t>(tick);
    }
    Clock GetClock() const { return clock_; }
    void SetClock(Clock c) { clock_ = c; }
    // ---- recorder -----------------------------------------------------------
    // Feed the state SERVED for vsync frame `vf` (active-high buttons in wire
    // bit positions). Multiple calls per frame are fine; the LAST state seen
    // for a frame wins. Holds that frame until the next frame (or Flush), so
    // an earlier sample cannot leave a phantom press in the recording.
    void Feed(uint32_t vf, uint16_t buttons_active_high, const uint8_t axes[4], std::vector<std::string>* out);
    // Close every open range at frame `vf`. Recording may continue afterwards.
    void Flush(uint32_t vf, std::vector<std::string>* out);
    // The header line a recorder writes first. With a clock named, the line
    // carries `clock=<token>` (the loader reads it back); without one it is the
    // original header, which the loader reads as `clock=tick`.
    static std::string HeaderLine(uint32_t start_frame, const std::string& title);
    static std::string HeaderLine(uint32_t start_frame, const std::string& title, Clock clock);
    // The clock a header line names (`clock=frame` / `clock=tick`); Tick when the
    // line names none. False when `line` is not a padlog header at all.
    static bool ParseHeader(const std::string& line, Clock* out);

    // Wire bit -> token (0..15), nullptr for none; axis 0..3 -> LX/LY/RX/RY.
    static const char* ButtonToken(int bit);
    static const char* AxisToken(int axis);
    // Token -> wire mask (0 when unknown).
    static uint16_t ButtonMask(const std::string& token);

    // ---- replayer -----------------------------------------------------------
    struct Press { uint32_t f1 = 0, f2 = 0; uint16_t mask = 0; };
    struct Axis  { uint32_t f1 = 0, f2 = 0; uint8_t axis = 0, value = 0x80; };

    // Parse a comma-joined spec; unknown tokens are skipped (the OLD parser's
    // tolerance). Returns the number of tokens accepted.
    size_t ParseSpec(const std::string& spec);
    // Load a .padlog: trailing CR/space trimmed, blank and '#' lines skipped,
    // the rest comma-joined into ParseSpec. The first `# padlog` header line
    // sets the clock (absent: Tick). False when the file cannot open.
    bool LoadFile(const std::string& path, std::string* err);
    // Overlay the script onto a served state for frame `f`: held buttons are
    // cleared in the ACTIVE-LOW wire word; the last matching axis segment
    // wins. Returns true when anything matched.
    bool Apply(uint32_t f, uint16_t* wire_buttons_active_low, uint8_t axes[4]) const;

    const std::vector<Press>& Presses() const { return presses_; }
    const std::vector<Axis>&  Axes() const { return axes_; }
    bool Empty() const { return presses_.empty() && axes_.empty(); }

private:
    void CommitSample(uint32_t vf, uint16_t buttons, const uint8_t axes[4],
                      std::vector<std::string>* out);
    struct OpenRange { bool open = false; uint32_t f1 = 0; };
    struct OpenAxis  { bool open = false; uint32_t f1 = 0; uint8_t val = 0x80; };
    OpenRange btn_[16];
    OpenAxis  axis_[4];
    bool      seen_ = false;
    uint32_t  last_vf_ = 0;
    uint16_t  last_buttons_ = 0;
    uint8_t   last_axes_[4] = {0x80, 0x80, 0x80, 0x80};

    std::vector<Press> presses_;
    std::vector<Axis>  axes_;
    Clock              clock_ = Clock::Tick;
};

}  // namespace ps2r::triage
