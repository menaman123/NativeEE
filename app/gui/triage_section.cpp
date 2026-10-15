#include "app/gui/triage_section.h"

#include <cstdio>

#include "imgui.h"

namespace ps2r::app {
namespace {
void Line(std::string& out, const char* key, const std::string& value) {
    out += key;
    out += ": ";
    out += value.empty() ? "-" : value;
    out += '\n';
}

void ResultLines(std::string& out, const char* heading, const std::vector<SessionControls::Result>& results) {
    out += heading;
    out += '\n';
    if (results.empty()) out += "  (none)\n";
    for (const auto& result : results) {
        char head[96];
        std::snprintf(head, sizeof(head), "  #%llu %s: %s", static_cast<unsigned long long>(result.request.id),
                      SessionControls::ActionLabel(result.request.action), SessionControls::PhaseLabel(result.phase));
        out += head;
        if (!result.message.empty()) { out += " -- "; out += result.message; }
        out += '\n';
    }
}
}  // namespace

std::string TriageSection::Report(const SessionControls::Snapshot& snapshot) {
    const auto& r = snapshot.runtime;
    std::string out = "the title host session report\n";
    Line(out, "game tick", std::to_string(r.frame));
    Line(out, "state", snapshot.closing ? "closing" : r.paused ? "paused" : "playing");
    Line(out, "presenter", "vsync " + std::string(r.vsync ? "on" : "off") + ", " +
         (r.fps_cap > 0 ? std::to_string(r.fps_cap) + " fps cap" : std::string("uncapped")) + ", window " +
         std::to_string(r.window_width) + "x" + std::to_string(r.window_height) +
         (r.fullscreen ? " fullscreen" : ""));
    Line(out, "input", r.input_name);
    Line(out, "recording", r.recording ? "on, " + r.recording_path : std::string("off"));
    Line(out, "selected reproduction", snapshot.restore_path);
    Line(out, "last saved reproduction", r.restore_path);
    Line(out, "game saves folder", r.game_saves_path);
    Line(out, "status", r.status);
    ResultLines(out, "pending commands:", snapshot.pending);
    ResultLines(out, "recent commands:", snapshot.recent);
    return out;
}

void TriageSection::Draw(const SessionControls::Snapshot& snapshot) {
    if (!ImGui::CollapsingHeader("Triage")) return;
    const auto& r = snapshot.runtime;
    ImGui::Text("Game tick %llu  |  %s", static_cast<unsigned long long>(r.frame),
                snapshot.closing ? "closing" : r.paused ? "paused" : "playing");
    ImGui::Text("Presenter: vsync %s, %s, window %dx%d%s", r.vsync ? "on" : "off",
                r.fps_cap > 0 ? (std::to_string(r.fps_cap) + " fps cap").c_str() : "uncapped",
                r.window_width, r.window_height, r.fullscreen ? " (fullscreen)" : "");
    ImGui::TextWrapped("Input: %s", r.input_name.empty() ? "-" : r.input_name.c_str());
    if (r.recording) ImGui::TextWrapped("Recording: %s", r.recording_path.c_str());
    if (!r.status.empty()) ImGui::TextWrapped("%s", r.status.c_str());
    if (ImGui::Button("Copy report to clipboard")) ImGui::SetClipboardText(Report(snapshot).c_str());
    ImGui::SameLine();
    ImGui::Checkbox("Show command history", &show_history_);
    if (!show_history_) return;
    ImGui::BeginChild("triage_history", ImVec2(-1.0f, ImGui::GetTextLineHeightWithSpacing() * 8.0f), true);
    for (const auto& result : snapshot.pending)
        ImGui::TextWrapped("#%llu %s: %s %s", static_cast<unsigned long long>(result.request.id),
                           SessionControls::ActionLabel(result.request.action),
                           SessionControls::PhaseLabel(result.phase), result.message.c_str());
    for (auto it = snapshot.recent.rbegin(); it != snapshot.recent.rend(); ++it)
        ImGui::TextWrapped("#%llu %s: %s %s", static_cast<unsigned long long>(it->request.id),
                           SessionControls::ActionLabel(it->request.action),
                           SessionControls::PhaseLabel(it->phase), it->message.c_str());
    ImGui::EndChild();
}

}  // namespace ps2r::app
