#include "app/host/launcher_settings.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>

#include "app/gui/savestate_rows.h"
#include "app/host/savestate_slots.h"
#include "engine/core/config/config.h"
#include "engine/core/config/settings_file.h"
#include "imgui.h"

namespace ps2r::app {
namespace {

const char* kAspects[] = {"auto", "4:3", "16:9", "16:10", "21:9"};

}  // namespace

LauncherSettings::LauncherSettings(Config& cfg, std::string settings_file, std::string user_data_root)
    : cfg_(cfg), settings_file_(std::move(settings_file)), user_data_root_(std::move(user_data_root)), bindings_(user_data_root_) {
    fixes_ = {
        {"fixes.look_rest_seconds", "Head-look rest time initialised",
         "RETAIL_BUGS.md Characters: LookController::Construct FUN_00147310 never stores restSeconds (+0x60); a new head-look "
         "rests for the heap's leftover float.", &cfg_.fixes.look_rest_seconds},
        {"fixes.skid_mark_init", "Skid-mark strips initialised",
         "RETAIL_BUGS.md Vehicles: ConstructSkidMarks FUN_00161ac0 never sets lastPoint (+0x10) or the two strip colours "
         "(+0x30/+0x34); the first strip of each trail is coloured from heap leftovers.", &cfg_.fixes.skid_mark_init},
        {"", "Cutscene music loop bit defined (QueueObjectMovie FUN_0029e5f8)",
         "RETAIL_BUGS.md Movies: bits 20-31 of the music request word come from the stack; whether the music loops is chance.", nullptr},
        {"", "Lopsided turning (WalkController::AskTurn FUN_0014f330)", "RETAIL_BUGS.md Characters: the 45-135 degree band eases "
         "turns by |(wanted-45)/90| for both signs -- one way 0.5-1, the other 1-1.5.", nullptr},
        {"", "Knee drop never waits (JumpController::Rise FUN_001491e0)", "RETAIL_BUGS.md Characters: bit 25 cleared then tested.", nullptr},
        {"", "Pushed body kept at any distance (KeepPushedBody FUN_00133c58)", "RETAIL_BUGS.md Characters: the length test runs on a "
         "normalised vector.", nullptr},
        {"", "Rollerbrawl camera yaw clamp (RollerbrawlYaw FUN_00142af8)", "RETAIL_BUGS.md Cameras: above 50 u/s the speed is replaced "
         "by the most yaw speed in radians (~3.49).", nullptr},
        {"", "Cutscene volume fade too fast (UpdateCutsceneVolumes FUN_001df670)", "RETAIL_BUGS.md Sound: fade -= time / fade.", nullptr},
        {"", "Camera posed at frame 0xFFFF after a skip (PlayCameraTrackFrame FUN_0029a068)", "RETAIL_BUGS.md Movies: the cut channel "
         "is read at the end frame.", nullptr},
        {"", "Overlapping bodies separate slowly (CollideRigidBodies FUN_002498c8)", "RETAIL_BUGS.md Physics: the push is scaled by "
         "depth/distance twice.", nullptr},
    };
    bindings_.Load();
}

void LauncherSettings::Note(const std::string& line) {
    log_.push_back(line);
    if (log_.size() > 12) log_.erase(log_.begin());
}

bool LauncherSettings::Persist(const char* key, const std::string& value) {
    std::string err;
    if (!SettingsFile::Set(settings_file_, key, value, &err)) {
        Note(std::string("could not write ") + key + ": " + err);
        std::fprintf(stderr, "launcher: settings write failed for %s: %s\n", key, err.c_str());
        return false;
    }
    Note(std::string(key) + " = " + value);
    return true;
}

std::vector<std::string> LauncherSettings::TexturePacks() const {
    std::vector<std::string> packs;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(cfg_.paths.texture_packs, ec)) {
        const std::string name = e.path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        std::error_code dir_ec;
        if (e.is_directory(dir_ec)) packs.push_back(name);
    }
    std::sort(packs.begin(), packs.end());
    return packs;
}

void LauncherSettings::DrawDisplayPage() {
    ImGui::TextDisabled("Mirrors the in-game Options > Display page; applied when the game starts, persisted to ps2r.ini.");
    ImGui::Spacing();
    if (ImGui::Checkbox("FPS / game Hz overlay (ui.perf_overlay)", &cfg_.ui.perf_overlay)) PersistBool("ui.perf_overlay", cfg_.ui.perf_overlay);
    if (ImGui::Checkbox("VSync (video.vsync)", &cfg_.video.vsync)) PersistBool("video.vsync", cfg_.video.vsync);
    int aspect = static_cast<int>(cfg_.video.aspect);
    if (ImGui::Combo("Aspect (video.aspect)", &aspect, kAspects, 5)) {
        cfg_.video.aspect = static_cast<decltype(cfg_.video.aspect)>(aspect);
        Persist("video.aspect", kAspects[aspect]);
    }
    int scale = cfg_.video.scale;
    if (ImGui::SliderInt("Internal scale (video.scale)", &scale, 1, 4) && scale != cfg_.video.scale) {
        cfg_.video.scale = scale;
        Persist("video.scale", std::to_string(scale));
    }
    int cap = cfg_.video.fps_cap;
    if (ImGui::SliderInt("Render fps cap (video.fps_cap; 0 = none)", &cap, 0, 240) && cap != cfg_.video.fps_cap) {
        cfg_.video.fps_cap = cap;
        Persist("video.fps_cap", std::to_string(cap));
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Texture pack (video.texture_pack)");
    const std::vector<std::string> packs = TexturePacks();
    ImGui::TextDisabled("%s", cfg_.paths.texture_packs.c_str());
    bool none = cfg_.video.texture_pack == "none" || cfg_.video.texture_pack.empty();
    if (ImGui::RadioButton("none (the converted textures)", none) && !none) { cfg_.video.texture_pack = "none"; Persist("video.texture_pack", "none"); }
    for (const std::string& p : packs)
        if (ImGui::RadioButton(p.c_str(), cfg_.video.texture_pack == p) && cfg_.video.texture_pack != p) {
            cfg_.video.texture_pack = p;
            Persist("video.texture_pack", p);
        }
    if (packs.empty()) ImGui::TextDisabled("(no packs: textures/<pack>/<hash>.png under the folder above; --dump-textures names the hashes)");
}

bool LauncherSettings::SaveBindings() {
    std::string err;
    if (!bindings_.Save(&err)) { Note(err); return false; }
    cfg_.input.bindings_file = bindings_.Path();
    return Persist("input.bindings_file", bindings_.Path());
}

void LauncherSettings::OfferKey(const std::string& sdl_key_name) {
    if (bindings_.OfferKey(sdl_key_name)) SaveBindings();
}

void LauncherSettings::OfferPadControl(const std::string& control_name) {
    if (bindings_.OfferPadControl(control_name)) SaveBindings();
}

// One press-to-bind table: a button per PS2 control; the armed one is amber
// and reads "press ...". Clicking the armed row disarms it.
void LauncherSettings::DrawBindingTable(const char* id, ControlsBindings::Target target,
                                        const std::vector<ControlsBindings::Row>& rows) {
    if (!ImGui::BeginTable(id, 4, ImGuiTableFlags_SizingStretchSame)) return;
    const char* prompt = target == ControlsBindings::Target::Pad ? "press a pad button..." : "press a key...";
    for (size_t i = 0; i < rows.size(); i++) {
        ImGui::TableNextColumn();
        const bool armed = bindings_.Armed() == target && bindings_.ArmedRow() == static_cast<int>(i);
        const bool unbound = rows[i].value.empty() || rows[i].value == "none";
        const std::string label = std::string(rows[i].name) + ": " + (armed ? prompt : (unbound ? "(unbound)" : rows[i].value)) +
                                  "##" + id + std::to_string(i);
        if (armed) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.5f, 0.1f, 1));
        if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) bindings_.Arm(target, static_cast<int>(i));
        if (armed) ImGui::PopStyleColor();
    }
    ImGui::EndTable();
}

void LauncherSettings::DrawControlsPage(const std::vector<std::string>& controllers) {
    ImGui::TextDisabled("Mirrors the in-game Options > Controls page.");
    if (controllers.empty()) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "No game controller detected (keyboard + mouse drive the pad).");
    for (const std::string& c : controllers) ImGui::BulletText("Controller: %s", c.c_str());
    ImGui::Spacing();
    if (ImGui::Checkbox("Keyboard and mouse as the pad (input.kbm_pad)", &cfg_.input.kbm_pad)) PersistBool("input.kbm_pad", cfg_.input.kbm_pad);
    if (ImGui::Checkbox("Rumble (input.rumble)", &cfg_.input.rumble)) PersistBool("input.rumble", cfg_.input.rumble);
    bool pad_enabled = bindings_.GamepadEnabled();
    if (ImGui::Checkbox("Game controller enabled", &pad_enabled)) { bindings_.SetGamepadEnabled(pad_enabled); SaveBindings(); }
    if (ImGui::SliderFloat("Stick deadzone", &bindings_.Deadzone(), 0.0f, 0.9f, "%.2f")) { /* preview */ }
    if (ImGui::IsItemDeactivatedAfterEdit()) SaveBindings();
    if (ImGui::SliderFloat("Stick sensitivity", &bindings_.Sensitivity(), 0.5f, 2.0f, "%.2f")) { /* preview */ }
    if (ImGui::IsItemDeactivatedAfterEdit()) SaveBindings();
    ImGui::Spacing();
    ImGui::SeparatorText("Gamepad buttons (click a PS2 button, then press the pad button or trigger for it)");
    ImGui::TextDisabled("Default: SDL's standard layout = the DualShock 2 layout (A cross, B circle, X square, Y triangle). "
                        "A control bound to another row moves; Escape cancels.");
    DrawBindingTable("pad", ControlsBindings::Target::Pad, bindings_.Pad());
    ImGui::Spacing();
    ImGui::SeparatorText("Keyboard bindings (click, then press a key; Backspace unbinds, Escape cancels)");
    DrawBindingTable("keys", ControlsBindings::Target::Key, bindings_.Keys());
    ImGui::TextDisabled("Written to %s (input.bindings_file).", bindings_.Path().c_str());
}

std::vector<LauncherSettings::SlotRow> LauncherSettings::SaveSlots() const {
    std::vector<SlotRow> out;
    SavestateRows rows;
    std::string err;
    if (!rows.Scan(SavestateSlots::Directory(cfg_), err)) return out;
    for (const SavestateRows::Row& r : rows.Rows()) {
        SlotRow s;
        s.path = r.path;
        s.frame = r.frame;
        s.bytes = r.size;
        const size_t p = r.name.find("slot");
        s.slot = p == std::string::npos ? -1 : std::atoi(r.name.c_str() + p + 4);
        SavestateSlots::Meta meta;
        if (SavestateSlots::ReadMeta(SavestateSlots::MetaPath(r.path), &meta, &err)) s.build_id = meta.build_id;
        out.push_back(s);
    }
    return out;
}

void LauncherSettings::DrawSaveStatesPage() {
    ImGui::TextDisabled("F5 writes slot<save.slot>.ps2st, F8 restores it. %s", SavestateSlots::Directory(cfg_).c_str());
    int slot = cfg_.save.slot;
    if (ImGui::SliderInt("Active slot (save.slot)", &slot, 0, 9) && slot != cfg_.save.slot) {
        cfg_.save.slot = slot;
        Persist("save.slot", std::to_string(slot));
    }
    const std::vector<SlotRow> slots = SaveSlots();
    if (slots.empty()) { ImGui::TextDisabled("(no save states yet)"); return; }
    if (ImGui::BeginTable("slots", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("slot"); ImGui::TableSetupColumn("frame"); ImGui::TableSetupColumn("size");
        ImGui::TableSetupColumn("build"); ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < slots.size(); i++) {
            const SlotRow& s = slots[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool active = s.slot == cfg_.save.slot;
            if (ImGui::Selectable((std::to_string(s.slot) + (active ? " (active)" : "")).c_str(), selected_slot_ == static_cast<int>(i)))
                selected_slot_ = static_cast<int>(i);
            ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)s.frame);
            ImGui::TableNextColumn(); ImGui::Text("%llu KB", (unsigned long long)(s.bytes >> 10));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(s.build_id.empty() ? "?" : s.build_id.c_str());
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(i));
            if (s.slot >= 0 && !active && ImGui::SmallButton("select")) { cfg_.save.slot = s.slot; Persist("save.slot", std::to_string(s.slot)); }
            ImGui::SameLine();
            // Delete asks first: the row turns into "really delete? yes / no" and
            // nothing is removed until "yes" (review C2 loop 1 -- one click on a
            // save state is not enough to lose it).
            if (pending_delete_ == s.path) {
                ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "really delete?");
                ImGui::SameLine();
                if (ImGui::SmallButton("yes")) {
                    std::remove(s.path.c_str());
                    std::remove(SavestateSlots::MetaPath(s.path).c_str());
                    Note("deleted " + s.path);
                    pending_delete_.clear();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("no")) pending_delete_.clear();
            } else if (ImGui::SmallButton("delete")) {
                pending_delete_ = s.path;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void LauncherSettings::DrawFixesPage() {
    ImGui::TextDisabled("Retail defects documented by the decomp (TR docs/RETAIL_BUGS.md). Default OFF = the game as shipped.");
    ImGui::Spacing();
    for (Fix& f : fixes_) {
        if (f.value) {
            if (ImGui::Checkbox(f.title, f.value)) PersistBool(f.key, *f.value);
        } else {
            bool off = false;
            ImGui::BeginDisabled();
            ImGui::Checkbox((std::string(f.title) + " (unimplemented)").c_str(), &off);
            ImGui::EndDisabled();
        }
        ImGui::Indent();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", f.citation);
        ImGui::PopTextWrapPos();
        ImGui::Unindent();
    }
}

std::string LauncherSettings::DrawDataPage(const std::string& game_files_dir, const std::string& disc_path,
                                           const std::string& stamp_state) {
    std::string request;
    ImGui::Text("Data folder: %s", user_data_root_.c_str());
    ImGui::Text("Game files:  %s  (stamp: %s)", game_files_dir.c_str(), stamp_state.c_str());
    ImGui::Text("Disc image:  %s", disc_path.c_str());
    ImGui::Text("Settings:    %s", settings_file_.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Open data folder")) request = "open-folder";
    ImGui::SameLine();
    if (ImGui::Button("Change disc image...")) request = "change-disc";
    ImGui::SameLine();
    if (ImGui::Button("Re-extract game files")) request = "re-extract";
    if (ImGui::Checkbox("Show this screen on every start (launcher.show)", &cfg_.launcher.show)) PersistBool("launcher.show", cfg_.launcher.show);
    ImGui::TextDisabled("Off: a plain start goes straight to the game once the files exist; `nativeee --launcher` brings it back.");
    ImGui::Spacing();
    ImGui::SeparatorText("Quality of life");
    if (ImGui::Checkbox("START / CROSS skips movies, the attract movie included (qol.skip_movies)", &cfg_.qol.skip_movies))
        PersistBool("qol.skip_movies", cfg_.qol.skip_movies);
    int ff = cfg_.qol.fast_forward;
    if (ImGui::SliderInt("Cutscene fast-forward while R2 is held (qol.fast_forward; 1 = off)", &ff, 1, 8) && ff != cfg_.qol.fast_forward) {
        cfg_.qol.fast_forward = ff;
        Persist("qol.fast_forward", std::to_string(ff));
    }
    ImGui::TextDisabled("In-engine cutscenes run N times faster while R2 is held; the game clock itself stays at 60 Hz.");
    return request;
}

}  // namespace ps2r::app
