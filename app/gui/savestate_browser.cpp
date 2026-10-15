#include "app/gui/savestate_browser.h"

#include <cstdio>
#include <ctime>

#include "imgui.h"

namespace ps2r::app {

void SavestateBrowser::Rescan(const std::string& directory) {
    rows_.Scan(directory, error_);
}

bool SavestateBrowser::Draw(SessionControls& controls, const SessionControls::Snapshot& snapshot,
                            const std::string& directory) {
    // A newly completed reproduction save changes the folder: rescan once.
    if (snapshot.runtime.restore_path != last_restore_path_) {
        last_restore_path_ = snapshot.runtime.restore_path;
        rows_ = SavestateRows();
    }
    if (!rows_.Scanned() || rows_.Directory() != directory) Rescan(directory);
    bool selected = false;
    ImGui::TextWrapped("Reproduction folder: %s", directory.empty() ? "(none configured)" : directory.c_str());
    if (ImGui::SmallButton("Refresh")) Rescan(directory);
    if (!error_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.30f, 1.0f), "%s", error_.c_str());
        return false;
    }
    const auto& rows = rows_.Rows();
    if (rows.empty()) {
        ImGui::TextDisabled("No reproduction files yet. Save one with F5.");
        return false;
    }
    if (rows_.Truncated())
        ImGui::TextDisabled("Showing the first %zu files; move older files out of the folder to see the rest.",
                            SavestateRows::kMaxRows);
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    const float height = ImGui::GetTextLineHeightWithSpacing() * 8.0f;
    if (ImGui::BeginTable("reproductions", 4, flags, ImVec2(-1.0f, height))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Game tick", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Saved", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableHeadersRow();
        for (const auto& row : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const bool current = snapshot.restore_path == row.path;
            ImGui::PushID(row.path.c_str());
            if (ImGui::Selectable(row.name.c_str(), current, ImGuiSelectableFlags_SpanAllColumns)) {
                if (controls.SelectRestorePath(row.path)) selected = true;
            }
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            if (row.valid) ImGui::Text("%u", row.frame);
            else ImGui::TextColored(ImVec4(1.0f, 0.46f, 0.38f, 1.0f), "invalid header");
            ImGui::TableSetColumnIndex(2);
            if (row.valid && row.wall_ms) {
                const std::time_t seconds = static_cast<std::time_t>(row.wall_ms / 1000);
                char stamp[32] = {};
                if (std::tm* local = std::localtime(&seconds))
                    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", local);
                ImGui::TextUnformatted(stamp);
            } else {
                ImGui::TextDisabled("-");
            }
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.1f MiB", static_cast<double>(row.size) / (1024.0 * 1024.0));
        }
        ImGui::EndTable();
    }
    return selected;
}

}  // namespace ps2r::app
