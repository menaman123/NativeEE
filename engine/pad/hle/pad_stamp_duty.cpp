// PadStampDuty implementation; the duty is described in the header.
#include "engine/pad/hle/pad_stamp_duty.h"

PadStampDuty::PadStampDuty(PadProtocol& protocol, const ps2r::triage::InputLog* replay,
                           ps2r::triage::InputLog* recorder)
    : protocol_(protocol), replay_(replay), recorder_(recorder) {}

PadProtocol::Input PadStampDuty::SampleFor(uint32_t frame) const {
    // The script grammar overlays the ACTIVE-LOW wire word and the LX LY RX RY
    // axes; PadProtocol takes the active-high sample.
    uint16_t wire = 0xFFFF;
    uint8_t axes[4] = {0x80, 0x80, 0x80, 0x80};
    if (replay_) replay_->Apply(frame, &wire, axes);
    PadProtocol::Input in;
    in.buttons = static_cast<uint16_t>(~wire);
    in.lx = axes[0];
    in.ly = axes[1];
    in.rx = axes[2];
    in.ry = axes[3];
    return in;
}

PadProtocol::Input PadStampDuty::Run(uint64_t now) { return Run(now, static_cast<uint32_t>(now)); }

PadProtocol::Input PadStampDuty::Run(uint64_t now, uint32_t script_key) {
    ++runs_;
    const PadProtocol::Input in = SampleFor(script_key);
    protocol_.Stamp(in, now);
    if (recorder_) {
        const uint8_t axes[4] = {in.lx, in.ly, in.rx, in.ry};
        recorder_->Feed(script_key, in.buttons, axes, &lines_);
    }
    return in;
}

void PadStampDuty::Flush(uint32_t frame) {
    if (recorder_) recorder_->Flush(frame, &lines_);
}
