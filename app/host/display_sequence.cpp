#include "app/host/display_sequence.h"

#include <cstdlib>

namespace ps2r::app {

bool DisplaySequence::ModeOf(const std::string& mode, SessionControls::Action& action, int& value) {
    using Action = SessionControls::Action;
    if (mode == "windowed") { action = Action::SetDisplayMode; value = 0; return true; }
    if (mode == "fullscreen") { action = Action::SetDisplayMode; value = 1; return true; }
    if (mode == "borderless") { action = Action::SetDisplayMode; value = 2; return true; }
    if (mode.size() == 6 && mode.compare(0, 5, "scale") == 0 && mode[5] >= '1' && mode[5] <= '4') {
        action = Action::SetScale;
        value = mode[5] - '0';
        return true;
    }
    if (mode == "vsync") { action = Action::SetVSync; value = 1; return true; }
    if (mode == "novsync") { action = Action::SetVSync; value = 0; return true; }
    // The Display page's other rows (the same AspectSel / TextureFilterSel orders as the settings file).
    static const char* const kAspects[] = {"aspect_auto", "aspect_4_3", "aspect_16_9", "aspect_16_10", "aspect_21_9"};
    for (int i = 0; i < 5; ++i)
        if (mode == kAspects[i]) { action = Action::SetAspect; value = i; return true; }
    static const char* const kFilters[] = {"filter_title", "filter_point", "filter_bilinear", "filter_trilinear"};
    for (int i = 0; i < 4; ++i)
        if (mode == kFilters[i]) { action = Action::SetTextureFilter; value = i; return true; }
    static const int kAniso[] = {1, 2, 4, 8, 16};
    for (int a : kAniso)
        if (mode == "aniso" + std::to_string(a)) { action = Action::SetAnisotropy; value = a; return true; }
    if (mode == "overlay") { action = Action::SetPerfOverlay; value = 1; return true; }
    if (mode == "nooverlay") { action = Action::SetPerfOverlay; value = 0; return true; }
    if (mode == "rate60") { action = Action::SetRenderRate; value = 0; return true; }
    if (mode == "ratedisplay") { action = Action::SetRenderRate; value = 1; return true; }
    return false;
}

int DisplaySequence::Parse(const std::string& text, std::vector<Step>& out) {
    int skipped = 0;
    for (std::string rest = text; !rest.empty();) {
        const size_t comma = rest.find(',');
        const std::string item = rest.substr(0, comma);
        rest = comma == std::string::npos ? std::string() : rest.substr(comma + 1);
        const size_t colon = item.find(':');
        Step step;
        bool ok = colon != std::string::npos && colon >= 1 && colon + 1 < item.size();
        for (size_t i = 0; i < colon && ok; ++i) ok = item[i] >= '0' && item[i] <= '9';
        if (ok) {
            step.frame = std::strtoull(item.c_str(), nullptr, 10);
            step.mode = item.substr(colon + 1);
            ok = step.frame > 0 && ModeOf(step.mode, step.action, step.value);
        }
        if (!ok) { ++skipped; continue; }
        out.push_back(std::move(step));
    }
    return skipped;
}

int DisplaySequence::OnBoundary(uint64_t boundary_count, uint64_t padded, const std::function<uint64_t(const Step&)>& submit,
                                const std::function<bool(uint64_t)>& finished) {
    int n = 0;
    for (Step& step : steps_) {
        if (step.submitted) {
            // An open swap window: the presenter's report closes it after the settle.
            if (step.action == SessionControls::Action::SetScale || step.id == 0 || step.closed) continue;
            if (step.settle_until == 0) {
                if (finished && finished(step.id)) step.settle_until = boundary_count + kSettleBoundaries;
                continue;
            }
            if (boundary_count < step.settle_until) continue;
            step.closed = true;
            ++swap_closed_;
            if (padded > step.padded_at_submit) swap_padded_ += padded - step.padded_at_submit;
            continue;
        }
        if (boundary_count < step.frame) continue;
        step.submitted = true;
        step.padded_at_submit = padded;
        if (submitted_++ == 0) { first_frame_ = boundary_count; padded_at_first_ = padded; }
        ++n;
        step.id = submit(step);
    }
    return n;
}

int DisplaySequence::SwapOpen() const {
    int n = 0;
    for (const Step& step : steps_)
        n += step.submitted && step.action != SessionControls::Action::SetScale && step.id != 0 && !step.closed ? 1 : 0;
    return n;
}

int DisplaySequence::PresenterSteps() const {
    int n = 0;
    for (const Step& step : steps_) n += step.action != SessionControls::Action::SetScale ? 1 : 0;
    return n;
}

std::string DisplaySequence::Census(uint64_t padded_now, uint64_t work_max_since_first_us) const {
    if (steps_.empty()) return "off";
    const uint64_t padded_after = submitted_ > 0 && padded_now >= padded_at_first_ ? padded_now - padded_at_first_ : 0;
    return "steps=" + std::to_string(steps_.size()) + ",presenter_steps=" + std::to_string(PresenterSteps()) +
           ",submitted=" + std::to_string(submitted_) + ",skipped=" + std::to_string(skipped_) +
           ",first_frame=" + std::to_string(first_frame_) + ",padded_at_first=" + std::to_string(padded_at_first_) +
           ",padded_after=" + std::to_string(padded_after) +
           ",work_max_ms_after=" + std::to_string(work_max_since_first_us / 1000) +
           ",swap_padded=" + std::to_string(swap_padded_) + ",swap_closed=" + std::to_string(swap_closed_) +
           ",swap_open=" + std::to_string(SwapOpen());
}

}  // namespace ps2r::app
