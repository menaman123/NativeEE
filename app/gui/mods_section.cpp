#include "app/gui/mods_section.h"

#include <algorithm>
#include <string>

#include "imgui.h"

namespace ps2r::app {
namespace {
using Action = SessionControls::Action;

bool Busy(const SessionControls::Snapshot& snapshot, Action action) {
    return std::any_of(snapshot.pending.begin(), snapshot.pending.end(), [action](const auto& result) {
        return result.request.action == action;
    });
}

// "episode/level" -> the two halves (the whole name as the level when there is no slash).
void Split(const std::string& name, std::string* episode, std::string* level) {
    const size_t slash = name.find('/');
    if (slash == std::string::npos) { episode->clear(); *level = name; return; }
    *episode = name.substr(0, slash);
    *level = name.substr(slash + 1);
}
}  // namespace

void ModsSection::Draw(SessionControls& controls, const SessionControls::Snapshot& snapshot) {
    if (!snapshot.capabilities.mods) return;
    if (!ImGui::CollapsingHeader("Mods (for testing)")) return;
    const auto& r = snapshot.runtime;
    ImGui::TextDisabled("Testing aids. Jumping to a level loads it the way the game itself does, with the saved progress as it is; "
                        "a level the story has not reached may be missing its objects or its mission.");
    // Skip cutscene: the movie playing or the in-engine cutscene; disabled when neither is on.
    {
        const bool can = r.mods_cutscene && !Busy(snapshot, Action::ModsSkipCutscene) && !snapshot.closing;
        ImGui::BeginDisabled(!can);
        if (ImGui::Button("Skip cutscene", ImVec2(160, 0))) controls.Submit(Action::ModsSkipCutscene);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (r.mods_fast_forward) ImGui::TextUnformatted("Fast-forwarding the cutscene.");
        else if (r.mods_cutscene) ImGui::TextUnformatted("A movie or cutscene is playing. Key: Backspace; controller: Select + Start.");
        else ImGui::TextDisabled("No movie or cutscene is playing. Key: Backspace; controller: Select + Start.");
    }
    ImGui::Spacing();
    if (r.mods_levels.empty()) {
        ImGui::TextDisabled("The level list appears once the game has loaded its level table.");
        return;
    }
    // The current level's row.
    const SessionControls::ModsLevel* current = nullptr;
    for (const auto& level : r.mods_levels)
        if (level.wid == r.mods_level) current = &level;
    if (current) ImGui::Text("Current level: %s (%d)", current->name.c_str(), current->wid);
    else ImGui::TextDisabled("Current level: unknown");
    const bool busy = Busy(snapshot, Action::ModsWarp) || snapshot.closing;
    // Grouped by episode in table order; a header per episode, one row per level.
    int last_episode = -1;
    bool open = false;
    for (const auto& level : r.mods_levels) {
        if (level.episode != last_episode) {
            if (last_episode >= 0 && open) ImGui::TreePop();
            last_episode = level.episode;
            std::string episode, rest;
            Split(level.name, &episode, &rest);
            const std::string header = "Episode " + std::to_string(level.episode) + ": " + episode + "##mods_ep" + std::to_string(level.episode);
            open = ImGui::TreeNodeEx(header.c_str(), level.episode == (current ? current->episode : -1) ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        }
        if (!open) continue;
        std::string episode, name;
        Split(level.name, &episode, &name);
        const bool is_current = level.wid == r.mods_level;
        if (is_current) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
        ImGui::Text("%2d  %s%s%s", level.wid, name.c_str(), level.kind == "hub" ? "  (hub)" : "", is_current ? "  <- here" : "");
        if (is_current) ImGui::PopStyleColor();
        ImGui::SameLine(300);
        ImGui::BeginDisabled(busy);
        const std::string go = "Go##mods_go" + std::to_string(level.wid);
        if (ImGui::SmallButton(go.c_str())) controls.Submit(Action::ModsWarp, level.wid);
        ImGui::EndDisabled();
    }
    if (last_episode >= 0 && open) ImGui::TreePop();
}

}  // namespace ps2r::app
