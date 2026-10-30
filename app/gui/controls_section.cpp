#include "app/gui/controls_section.h"

#include <algorithm>

#include "imgui.h"

namespace ps2r::app {

void ControlsSection::Draw(SessionControls& controls, const SessionControls::Snapshot& snapshot) {
    using Action = SessionControls::Action;
    if (!ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (snapshot.capabilities.keyboard_input) {
        const bool busy = std::any_of(snapshot.pending.begin(), snapshot.pending.end(), [](const auto& result) {
            return result.request.action == Action::SetKeyboardInput;
        });
        bool keyboard = snapshot.runtime.keyboard_input;
        ImGui::BeginDisabled(busy || snapshot.closing);
        if (ImGui::Checkbox("Keyboard and mouse drive the pad", &keyboard))
            controls.Submit(Action::SetKeyboardInput, keyboard ? 1 : 0);
        ImGui::EndDisabled();
    }
    if (snapshot.capabilities.camera_invert) {
        const auto busy = [&](Action a) {
            return std::any_of(snapshot.pending.begin(), snapshot.pending.end(),
                               [a](const auto& result) { return result.request.action == a; });
        };
        bool invert_x = snapshot.runtime.invert_camera_x, invert_y = snapshot.runtime.invert_camera_y;
        ImGui::BeginDisabled(busy(Action::SetInvertCameraX) || snapshot.closing);
        if (ImGui::Checkbox("Invert camera X (left / right)", &invert_x)) controls.Submit(Action::SetInvertCameraX, invert_x ? 1 : 0);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(busy(Action::SetInvertCameraY) || snapshot.closing);
        if (ImGui::Checkbox("Invert camera Y (up / down)", &invert_y)) controls.Submit(Action::SetInvertCameraY, invert_y ? 1 : 0);
        ImGui::EndDisabled();
    }
    if (!HasModel()) {
        ImGui::TextDisabled("Key bindings are configured when the session starts (the launcher's Controls page).");
        return;
    }
    ImGui::TextDisabled("Click a PS2 button, then press the key for it. Backspace unbinds, Escape cancels. "
                        "A key bound to another row moves. Changes apply at once and persist.");
    const std::vector<Row> rows = model_.rows();
    const int armed = model_.armed_row ? model_.armed_row() : -1;
    if (ImGui::BeginTable("controls_keys", 4, ImGuiTableFlags_SizingStretchSame)) {
        for (size_t i = 0; i < rows.size(); i++) {
            ImGui::TableNextColumn();
            const bool is_armed = armed == static_cast<int>(i);
            const bool unbound = rows[i].value.empty() || rows[i].value == "none";
            const std::string label = rows[i].name + ": " + (is_armed ? "press a key..." : (unbound ? "(unbound)" : rows[i].value)) +
                                      "##controls_key" + std::to_string(i);
            if (is_armed) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.5f, 0.1f, 1));
            if (ImGui::Button(label.c_str(), ImVec2(-1, 0)) && model_.arm) model_.arm(static_cast<int>(i));
            if (is_armed) ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }
    if (model_.status) {
        const std::string status = model_.status();
        if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
    }
}

}  // namespace ps2r::app
