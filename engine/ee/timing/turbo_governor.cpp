#include "engine/ee/timing/turbo_governor.h"

void TurboGovernor::SetManual(uint32_t turbo_setting) {
    if (turbo_setting > TickSource::kTurboKMax) turbo_setting = TickSource::kTurboKMax;
    manual_ = turbo_setting;
}

void TurboGovernor::NoteLoadActivity(uint64_t now_ms) {
    if (!have_activity_ || now_ms - last_activity_ms_ > kGapMs) {
        activity_start_ms_ = now_ms;  // a gap restarts the engage window
        have_activity_ = true;
    }
    last_activity_ms_ = now_ms;
}

void TurboGovernor::NoteInput(uint64_t now_ms) {
    last_input_ms_ = now_ms;
    engaged_ = false;  // input releases at once
}

void TurboGovernor::Update(uint64_t now_ms) {
    if (!load_turbo_ || manual_ != 0) {
        engaged_ = false;
        return;
    }
    if (engaged_) {
        if (now_ms - last_activity_ms_ > kReleaseMs) engaged_ = false;
        return;
    }
    if (!have_activity_) return;
    const bool sustained = now_ms - activity_start_ms_ >= kEngageMs && now_ms - last_activity_ms_ <= kGapMs;
    const bool input_quiet = last_input_ms_ == 0 || now_ms - last_input_ms_ >= kInputQuietMs;
    engaged_ = sustained && input_quiet;
}

TickSource::Mode TurboGovernor::Mode() const {
    if (manual_ == 1) return TickSource::Mode::Freerun;
    if (manual_ >= TickSource::kTurboKMin) return TickSource::Mode::Turbo;
    if (engaged_) return TickSource::Mode::Freerun;  // load turbo's shipped form
    return TickSource::Mode::Realtime;
}

uint32_t TurboGovernor::K() const {
    if (manual_ >= TickSource::kTurboKMin) return manual_;
    if (manual_ == 1 || engaged_) return 0;  // freerun forms have no K
    return 1;
}

void TurboGovernor::Apply(TickSource& source) const { source.SetMode(Mode(), K()); }
