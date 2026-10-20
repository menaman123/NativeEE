// core/config/deterministic_profile.cpp -- PROFILE.md rows -> Config fields.
//
//   PS2_TIME_EMU_REGIME=1    -> engine.time_regime_pin = 1
//   PS2_NOVSYNC=1            -> video.vsync = off (already the peak default)
//   PS2_VBLANK_FREERUN=1     -> engine.pacing = freerun
//   PS2_MTVU=0               -> engine.mtvu = off
//   PS2_CDVD_INSTANT=1       -> cdvd.latency_model = instant
//   PS2_DVD_LATENCY_SCALE=0  -> cdvd.latency_scale = 0
//   PS2_MC_LATENCY=0         -> cdvd.mc_latency_polls = 0
//   PS2_FPS_CAP=0            -> video.fps_cap = 0
//   PS2_GS_INTERP=0          -> video.interp = off
//   PS2_GUI_OPEN=0           -> ui.gui_open = off
//   PS2_GS_SCALE=1           -> video.scale = 1
//   (A2 enhancement off)     -> video.texture_filter = title, video.anisotropy = 1
//   PS2_IOP_DISPATCH=frame   -> iop.dispatch = frame
//   (M11 oracle arm)         -> hle.startup = retained
//   PS2_RTC_FIXED=1          -> game.rtc_fixed = on
//   PS2_TICK_DETERMINISTIC=1 -> time.tick_deterministic = on
//   PS2_MAINT_EEPACED=1      -> time.maint_eepaced = on
//   PS2_T0_TICKPACED=1       -> time.t0_tickpaced = on
//   PS2_TICK_SYNC=1          -> time.tick_sync = on
//   PS2_LOG=0                -> dev.log_level = quiet (already the default)
//   PS2_TIME_EMU / PS2_T0_SUBTICK=0 / PS2_HB_TICKCLOCK / PS2_SND_TICKCLOCK /
//   PS2_FIXC_TICKCLOCK / PS2_IOP_KERNEL / PS2_EE_RUNTOKEN are NORM in NEW
//   (the winning arm is the only arm) and need no field.
//   audio.enabled stays on: the device is suppressed by time.tick_sync.
#include "engine/core/config/deterministic_profile.h"

#include <cstring>

namespace ps2r {

void DeterministicProfile::Apply(Config& c) {
    c.video.scale = 1;
    c.video.texture_filter = TextureFilterSel::Title;   // the title's own sampler choice: lanes identical
    c.video.anisotropy = 1;
    c.video.interp = false;
    c.video.fps_cap = 0;
    c.video.vsync = false;
    c.cdvd.latency_model = CdvdLatencyModel::Instant;
    c.cdvd.latency_scale = 0.0;
    c.cdvd.mc_latency_polls = 0;
    c.cdvd.load_turbo = false;
    c.engine.mtvu = false;
    c.engine.pacing = Pacing::Freerun;
    c.engine.time_regime_pin = 1;
    c.time.tick_sync = true;
    c.time.tick_deterministic = true;
    c.time.t0_tickpaced = true;
    c.time.maint_eepaced = true;
    c.iop.dispatch = IopDispatch::Frame;
    c.audio.interpolation = AudioInterpolation::Linear;   // the audio lane's hashes are cut with the 2-tap resampler
    c.hle.startup = StartupMode::Retained;   // the IOP oracle arm: lanes identical, STREAM.IRX booted
    c.game.rtc_fixed = true;
    c.ui.gui_open = false;
    c.dev.log_level = LogLevelSetting::Quiet;
    c.meta.profile = kName;
}

bool DeterministicProfile::ApplyNamed(const char* name, Config& c) {
    if (!name) return false;
    if (std::strcmp(name, "default") == 0) {
        c.meta.profile = "default";
        return true;
    }
    if (std::strcmp(name, kName) == 0) {
        Apply(c);
        return true;
    }
    return false;
}

}  // namespace ps2r
