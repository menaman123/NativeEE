#include "app/gui/sound_section.h"

#include <algorithm>

#include "imgui.h"

namespace ps2r::app {

void SoundSection::Draw(SessionControls& controls, const SessionControls::Snapshot& snapshot) {
    using Action = SessionControls::Action;
    if (!ImGui::CollapsingHeader("Sound", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (!snapshot.capabilities.volume) {
        ImGui::TextDisabled("No audio device is open in this session; the audio lane is still produced.");
        return;
    }
    const bool busy = std::any_of(snapshot.pending.begin(), snapshot.pending.end(), [](const auto& result) {
        return result.request.action == Action::SetVolume;
    });
    ImGui::BeginDisabled(busy || snapshot.closing);
    if (!volume_editing_) volume_ = snapshot.runtime.volume_percent;
    if (volume_ > 0) unmute_level_ = volume_;
    ImGui::SetNextItemWidth(220.0f);
    const bool changed = ImGui::SliderInt("Output volume", &volume_, 0, 100, "%d%%");
    volume_editing_ = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit() || (changed && !volume_editing_))
        controls.Submit(Action::SetVolume, volume_);
    ImGui::SameLine();
    const bool muted = snapshot.runtime.volume_percent == 0;
    if (ImGui::Button(muted ? "Unmute" : "Mute"))
        controls.Submit(Action::SetVolume, muted ? std::clamp(unmute_level_, 1, 100) : 0);
    ImGui::EndDisabled();
    ImGui::TextDisabled("Volume scales only the device copy; recorded audio hashes are unchanged.");
}

}  // namespace ps2r::app
