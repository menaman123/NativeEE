#include "app/gui/session_panel.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "imgui.h"

namespace ps2r::app {
namespace {
using Action = SessionControls::Action;
using Phase = SessionControls::Phase;

bool Busy(const SessionControls::Snapshot& snapshot, Action action) {
    return std::any_of(snapshot.pending.begin(), snapshot.pending.end(), [action](const auto& result) {
        return result.request.action == action;
    });
}

const SessionControls::Result* LatestResult(const SessionControls::Snapshot& snapshot) {
    for (auto it = snapshot.recent.rbegin(); it != snapshot.recent.rend(); ++it)
        if (it->request.action != Action::ToggleMenu) return &*it;
    return nullptr;
}

void ResultLine(const SessionControls::Result& result) {
    ImVec4 color(0.75f, 0.78f, 0.83f, 1.0f);
    if (result.phase == Phase::Failed || result.phase == Phase::Refused)
        color = ImVec4(1.0f, 0.46f, 0.38f, 1.0f);
    else if (result.phase == Phase::Waiting || result.phase == Phase::Queued || result.phase == Phase::Working)
        color = ImVec4(1.0f, 0.78f, 0.30f, 1.0f);
    else if (result.phase == Phase::Succeeded)
        color = ImVec4(0.43f, 0.88f, 0.64f, 1.0f);
    ImGui::TextColored(color, "%s: %s", SessionControls::ActionLabel(result.request.action),
                       SessionControls::PhaseLabel(result.phase));
    ImGui::TextWrapped("%s", result.message.c_str());
}
}  // namespace

void SessionPanel::DrawSession(SessionControls& controls, const SessionControls::Snapshot& snapshot) {
    const bool pause_busy = Busy(snapshot, Action::TogglePause) || Busy(snapshot, Action::Step);
    ImGui::Text("%s  |  Game tick %llu  |  game %u Hz  |  render %u fps  |  doubled %llu", snapshot.runtime.paused ? "Paused" : "Playing",
                static_cast<unsigned long long>(snapshot.runtime.frame), snapshot.runtime.game_hz, snapshot.runtime.render_fps,
                static_cast<unsigned long long>(snapshot.runtime.doubled_frames));
    ImGui::BeginDisabled(!snapshot.capabilities.pause || pause_busy || snapshot.closing);
    if (ImGui::Button(snapshot.runtime.paused ? "Resume (F9)" : "Pause (F9)"))
        controls.Submit(Action::TogglePause);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!snapshot.capabilities.single_step || !snapshot.runtime.paused ||
                         pause_busy || snapshot.closing);
    if (ImGui::Button("One tick (F10)")) controls.Submit(Action::Step);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Hide menu (F1)")) controls.Submit(Action::ToggleMenu);
}

// The F1 menu is the DEV panel (CHARTER 2 track A): the player's settings --
// fullscreen, keyboard and mouse, the performance overlay -- live in the game's
// own options menu (the title's added 'Display' / 'Controls' pages, MenuExtension)
// and are only REPORTED here, next to the census readers.
void SessionPanel::DrawSettings(SessionControls&, const SessionControls::Snapshot& snapshot) {
    if (!ImGui::CollapsingHeader("Session state", ImGuiTreeNodeFlags_DefaultOpen)) return;
    const auto& r = snapshot.runtime;
    if (!r.input_name.empty()) ImGui::TextWrapped("Input: %s", r.input_name.c_str());
    ImGui::TextDisabled("Display: %s, perf overlay %s (the Display page below, or the game's own options page)",
                        r.fullscreen ? "fullscreen" : "windowed", r.perf_overlay ? "on" : "off");
    // Presenter readers (video.vsync / video.fps_cap, Config-owned): shown,
    // not edited here; resize changes the window only, never the game's
    // logical render target.
    ImGui::TextDisabled("Presentation: vsync %s, %s, window %dx%d", r.vsync ? "on" : "off",
                        r.fps_cap > 0 ? (std::to_string(r.fps_cap) + " fps cap").c_str() : "uncapped",
                        r.window_width, r.window_height);
}

// ui.perf_overlay: the clock contract's rule-5 readout (game Hz / render fps)
// in the top-right corner of the presented frame, menu open or closed. A fixed
// dark box with white text, so a pin on the presented frame can find it.
void SessionPanel::DrawPerfOverlay(const SessionControls::Snapshot& snapshot) {
    if (!snapshot.runtime.perf_overlay) return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x - 12.0f, 12.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.85f));
    if (ImGui::Begin("Performance", nullptr, flags)) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "game %u Hz", snapshot.runtime.game_hz);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "render %u fps", snapshot.runtime.render_fps);
        // The pacer's doubled frames so far: a picture shown > 1.2 fields after the previous one.
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "doubled %llu", static_cast<unsigned long long>(snapshot.runtime.doubled_frames));
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void SessionPanel::DrawReproduction(SessionControls& controls, const SessionControls::Snapshot& snapshot) {
    if (!ImGui::CollapsingHeader("Reproduction saves", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::TextWrapped("Capture this session to reproduce a problem. "
                       "These files are separate from the progress saved inside the game.");
    const bool busy = Busy(snapshot, Action::SaveReproduction) || Busy(snapshot, Action::RestoreReproduction);
    ImGui::BeginDisabled(!snapshot.capabilities.save_reproduction || busy || snapshot.closing);
    if (ImGui::Button("Save reproduction (F5)")) controls.Submit(Action::SaveReproduction);
    ImGui::EndDisabled();
    if (!snapshot.capabilities.save_reproduction)
        ImGui::TextDisabled("Reproduction saving is unavailable in this session.");

    browser_.Draw(controls, snapshot, snapshot.runtime.reproductions_path);
    if (!path_editing_) std::snprintf(path_.data(), path_.size(), "%s", snapshot.restore_path.c_str());
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##restore_path", "Or enter a reproduction file path",
                                 path_.data(), path_.size()))
        controls.SelectRestorePath(path_.data());
    path_editing_ = ImGui::IsItemActive();
    ImGui::BeginDisabled(!snapshot.capabilities.restore_reproduction || busy ||
                         snapshot.closing || path_[0] == '\0');
    if (ImGui::Button("Restore selected (F8)")) controls.Submit(Action::RestoreReproduction);
    ImGui::EndDisabled();
    if (snapshot.capabilities.restore_reproduction)
        ImGui::TextDisabled("Restore replaces the current session after validating the file.");
    else
        ImGui::TextDisabled("Reproduction restore is unavailable in this session.");
    if (!snapshot.runtime.game_saves_path.empty())
        ImGui::TextWrapped("In-game progress folder: %s", snapshot.runtime.game_saves_path.c_str());
}

void SessionPanel::DrawRecording(SessionControls& controls, const SessionControls::Snapshot& snapshot) {
    if (!ImGui::CollapsingHeader("Input recording", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::TextUnformatted(snapshot.runtime.recording ? "Recording served game input." : "Input recording is off.");
    if (!snapshot.runtime.recording_path.empty())
        ImGui::TextWrapped("Recording file: %s", snapshot.runtime.recording_path.c_str());
    if (snapshot.capabilities.recording) {
        ImGui::BeginDisabled(Busy(snapshot, Action::SetRecording) || snapshot.closing);
        if (ImGui::Button(snapshot.runtime.recording ? "Stop recording" : "Start recording"))
            controls.Submit(Action::SetRecording, !snapshot.runtime.recording);
        ImGui::EndDisabled();
    } else {
        ImGui::TextDisabled("Recording is configured when the session starts.");
    }
}

void SessionPanel::DrawResults(const SessionControls::Snapshot& snapshot) {
    if (!snapshot.runtime.status.empty()) ImGui::TextWrapped("%s", snapshot.runtime.status.c_str());
    for (const auto& result : snapshot.pending) ResultLine(result);
    if (!LatestResult(snapshot)) return;
    ImGui::SeparatorText("Recent actions");
    // The menu toggle itself is not useful as an operation result.
    unsigned shown = 0;
    for (auto it = snapshot.recent.rbegin(); it != snapshot.recent.rend() && shown < 5; ++it) {
        if (it->request.action == Action::ToggleMenu) continue;
        ResultLine(*it);
        ++shown;
    }
}

bool SessionPanel::WantsFrame(const SessionControls::Snapshot& snapshot, double now_seconds) {
    if (const auto* result = LatestResult(snapshot)) {
        if (result->request.id != notice_id_ || result->phase != notice_phase_) {
            notice_id_ = result->request.id;
            notice_phase_ = result->phase;
            notice_until_ = now_seconds + 6.0;
        }
    }
    return snapshot.menu_open || snapshot.runtime.perf_overlay || !snapshot.pending.empty() ||
           now_seconds < notice_until_;
}

void SessionPanel::DrawNotice(const SessionControls::Snapshot& snapshot) {
    const auto* result = snapshot.pending.empty() ? LatestResult(snapshot) : &snapshot.pending.front();
    if (!result) return;
    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(std::min(460.0f, ImGui::GetIO().DisplaySize.x - 32.0f), 0));
    ImGui::SetNextWindowBgAlpha(0.94f);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("Session notice", nullptr, flags)) ResultLine(*result);
    ImGui::End();
}

// The pause menu's Resume (the button, Escape with the menu open): the menu closes and
// the pause IT issued is released; a pause the user took with F9 stays.
void SessionPanel::Resume(SessionControls& controls, const SessionControls::Snapshot& snapshot) {
    if (snapshot.menu_paused && snapshot.runtime.paused && !Busy(snapshot, Action::TogglePause))
        controls.Submit(Action::TogglePause);
    controls.SetMenuPaused(false);
    controls.Submit(Action::ToggleMenu);
}

void SessionPanel::Draw(SessionControls& controls, const SessionControls::Snapshot& snapshot) {
    DrawPerfOverlay(snapshot);
    if (!snapshot.menu_open) { DrawNotice(snapshot); return; }
    const ImVec2 available = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(24, 24), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::min(620.0f, std::max(260.0f, available.x - 48.0f)),
                                   std::min(760.0f, std::max(240.0f, available.y - 48.0f))),
                             ImGuiCond_FirstUseEver);
    bool open = true;
    const char* title = snapshot.pause_menu ? "Paused" : "the title";
    if (ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse)) {
        if (snapshot.pause_menu) {
            // The pause menu's two verbs first: Resume (Escape) and Quit (the window close).
            ImGui::BeginDisabled(snapshot.closing);
            if (ImGui::Button("Resume (Esc)", ImVec2(160, 0))) Resume(controls, snapshot);
            ImGui::SameLine();
            if (ImGui::Button("Quit", ImVec2(160, 0))) controls.Submit(Action::Close);
            ImGui::EndDisabled();
            ImGui::Spacing();
        }
        DrawSession(controls, snapshot);
        ImGui::Spacing();
        DrawResults(snapshot);
        DrawSettings(controls, snapshot);
        display_.Draw(controls, snapshot);
        controls_page_.Draw(controls, snapshot);
        sound_.Draw(controls, snapshot);
        DrawReproduction(controls, snapshot);
        DrawRecording(controls, snapshot);
        triage_.Draw(snapshot);
        ImGui::Separator();
        ImGui::BeginDisabled(snapshot.closing);
        if (ImGui::Button(snapshot.pause_menu ? "Quit to desktop" : "Close session")) controls.Submit(Action::Close);
        ImGui::EndDisabled();
        if (snapshot.closing) ImGui::TextUnformatted("Closing session...");
    }
    ImGui::End();
    if (!open) {
        if (snapshot.pause_menu) Resume(controls, snapshot);
        else controls.Submit(Action::ToggleMenu);
    }
}

}  // namespace ps2r::app
