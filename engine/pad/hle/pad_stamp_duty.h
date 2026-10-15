#pragma once
// PadStampDuty: the maintenance slot's third duty (after CD completion
// delivery and the RPC end-callbacks) -- one pad sample per boundary, stamped
// into every open pad area through PadProtocol.
//
// The sample is the neutral wire (no button, sticks centred) overlaid with the
// replay script when one is loaded (InputLog::Apply for the boundary's frame);
// with no script the pad is idle, so a leg without inputs is deterministic.
// When a recorder is attached the SERVED sample is fed to it per boundary, so
// the recorder's lines replay to the identical slot images (the round trip
// PadRoundTrip pins).
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/triage/input_log.h"
#include "engine/pad/hle/pad_protocol.h"

class PadStampDuty {
public:
    // `replay` may be null (neutral input); `recorder` may be null (no recording).
    PadStampDuty(PadProtocol& protocol, const ps2r::triage::InputLog* replay, ps2r::triage::InputLog* recorder);

    // The boundary duty for `now` (the boundary's logical tick, the stamp's
    // clock). The script is indexed by `script_key` -- the key the loaded
    // script's clock names for this boundary (InputLog::Key); the one-argument
    // form indexes it by `now` (a tick-clock script).
    PadProtocol::Input Run(uint64_t now);
    PadProtocol::Input Run(uint64_t now, uint32_t script_key);

    // The sample the script yields for `frame` (neutral when no script matches).
    PadProtocol::Input SampleFor(uint32_t frame) const;

    // Recorder output: the lines completed so far; Flush closes the open ranges
    // at `frame` (end of run) and appends them.
    const std::vector<std::string>& Lines() const { return lines_; }
    void Flush(uint32_t frame);

    uint64_t Runs() const { return runs_; }
    bool Replaying() const { return replay_ != nullptr && !replay_->Empty(); }
    bool Recording() const { return recorder_ != nullptr; }

private:
    PadProtocol& protocol_;
    const ps2r::triage::InputLog* replay_;
    ps2r::triage::InputLog* recorder_;
    std::vector<std::string> lines_;
    uint64_t runs_ = 0;
};
