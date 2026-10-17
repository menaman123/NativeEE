#include "app/gui/display_section.h"

#include <algorithm>

#include "imgui.h"

namespace ps2r::app {
namespace {
using Action = SessionControls::Action;

bool Busy(const SessionControls::Snapshot& snapshot, Action action) {
    return std::any_of(snapshot.pending.begin(), snapshot.pending.end(), [action](const auto& result) {
        return result.request.action == action;
    });
}
}  // namespace

void DisplaySection::Combo(SessionControls& controls, const SessionControls::Snapshot& snapshot, Action action,
                           const char* label, const char* const* names, int count, int current, const int* values) {
    // `values` maps the combo index to the submitted value (anisotropy: 1,2,4,8,16); the
    // identity otherwise. `current` is the live VALUE.
    int index = 0;
    for (int i = 0; i < count; i++)
        if ((values ? values[i] : i) == current) index = i;
    ImGui::BeginDisabled(Busy(snapshot, action) || snapshot.closing);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo(label, &index, names, count)) controls.Submit(action, values ? values[index] : index);
    ImGui::EndDisabled();
}

void DisplaySection::Draw(SessionControls& controls, const SessionControls::Snapshot& snapshot) {
    if (!ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) return;
    const auto& caps = snapshot.capabilities;
    const auto& r = snapshot.runtime;
    if (!caps.display_rows && !caps.fullscreen && !caps.texture_filter && !caps.perf_overlay) {
        ImGui::TextDisabled("No display settings are available in this session.");
        return;
    }
    static const char* const kModes[] = {"Windowed", "Fullscreen", "Borderless"};
    static const char* const kAspects[] = {"Auto (window)", "4:3", "16:9", "16:10", "21:9"};
    static const char* const kRates[] = {"60 (one frame per game tick)", "Display rate"};
    static const char* const kScales[] = {"Native window", "1x", "2x", "3x", "4x"};
    static const char* const kFilters[] = {"Title (as the game set it)", "Point", "Bilinear", "Trilinear"};
    static const char* const kAniso[] = {"Off", "2x", "4x", "8x", "16x"};
    static const int kAnisoValues[] = {1, 2, 4, 8, 16};
    if (caps.fullscreen) Combo(controls, snapshot, Action::SetDisplayMode, "Display mode", kModes, 3, r.display_mode);
    if (caps.display_rows) {
        Combo(controls, snapshot, Action::SetAspect, "Aspect ratio", kAspects, 5, r.aspect);
        Combo(controls, snapshot, Action::SetScale, "Internal scale", kScales, 5, r.scale);
        Combo(controls, snapshot, Action::SetRenderRate, "Render rate", kRates, 2, r.render_rate);
        bool vsync = r.vsync;
        ImGui::BeginDisabled(Busy(snapshot, Action::SetVSync) || snapshot.closing);
        if (ImGui::Checkbox("VSync", &vsync)) controls.Submit(Action::SetVSync, vsync ? 1 : 0);
        ImGui::EndDisabled();
    }
    if (caps.texture_filter) {
        Combo(controls, snapshot, Action::SetTextureFilter, "Texture filter", kFilters, 4, r.texture_filter);
        Combo(controls, snapshot, Action::SetAnisotropy, "Anisotropic filtering", kAniso, 5, r.anisotropy, kAnisoValues);
    }
    if (caps.perf_overlay) {
        bool overlay = r.perf_overlay;
        ImGui::BeginDisabled(Busy(snapshot, Action::SetPerfOverlay) || snapshot.closing);
        if (ImGui::Checkbox("Performance overlay (game Hz / render fps)", &overlay))
            controls.Submit(Action::SetPerfOverlay, overlay ? 1 : 0);
        ImGui::EndDisabled();
    }
    ImGui::TextDisabled("Applied live by the presenter and saved to the settings file (ps2r.ini). Window %dx%d.",
                        r.window_width, r.window_height);
}

}  // namespace ps2r::app
