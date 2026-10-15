#pragma once
// LauncherSettings -- the settings screen of the launcher (CHARTER 2 C2 / A5):
// the Display and Controls mirrors of the in-game pages, texture packs, the
// save-slot manager (F5/F8 slots: list / select / delete-with-confirm), the 'Fixes' page
// (RETAIL_BUGS toggles, default off, each with its decomp citation), and the
// data page (disc image, open the data folder, re-extract). Every change is
// applied to the Config the game is about to start with AND persisted through
// SettingsFile (the one writer of ps2r.ini). The controller bindings are
// held by ControlsBindings (keyboard rows, the gamepad-button remap rows,
// deadzone / sensitivity / enable; press-to-bind), written as <user
// data>/bindings.ini (the InputMapper's grammar); input.bindings_file points
// at it. ImGui drawing; main thread; no SDL calls beyond the key / control
// names and the controller list it is handed.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "app/host/controls_bindings.h"

namespace ps2r { class Config; }

namespace ps2r::app {

class LauncherSettings {
public:
    struct Fix {
        const char* key;        // Config key ("" = listed as unimplemented)
        const char* title;
        const char* citation;   // TR docs/RETAIL_BUGS.md entry
        bool* value;            // the Config field (nullptr when unimplemented)
    };
    struct SlotRow {
        int slot = 0;
        std::string path;
        uint64_t frame = 0;
        std::string build_id;
        uint64_t bytes = 0;
    };
    LauncherSettings(Config& cfg, std::string settings_file, std::string user_data_root);

    // The pages; `controllers` = the detected SDL game controllers' names.
    void DrawDisplayPage();
    void DrawControlsPage(const std::vector<std::string>& controllers);
    void DrawSaveStatesPage();
    void DrawFixesPage();
    // Returns a request: "" | "change-disc" | "open-folder" | "re-extract".
    std::string DrawDataPage(const std::string& game_files_dir, const std::string& disc_path, const std::string& stamp_state);

    // Press-to-bind: the launcher forwards key names while a KEY row is armed
    // and pad control names (ControlsBindings::PadControlFromEvent) while a
    // PAD row is armed; either lands the binding and writes bindings.ini.
    bool KeyBindingArmed() const { return bindings_.Armed() == ControlsBindings::Target::Key; }
    bool PadBindingArmed() const { return bindings_.Armed() == ControlsBindings::Target::Pad; }
    void OfferKey(const std::string& sdl_key_name);
    void OfferPadControl(const std::string& control_name);
    void CancelBinding() { bindings_.Disarm(); }
    const ControlsBindings& Bindings() const { return bindings_; }

    const std::vector<std::string>& Log() const { return log_; }
    const std::vector<Fix>& Fixes() const { return fixes_; }
    std::vector<std::string> TexturePacks() const;
    std::vector<SlotRow> SaveSlots() const;

private:
    bool Persist(const char* key, const std::string& value);
    bool PersistBool(const char* key, bool value) { return Persist(key, value ? "on" : "off"); }
    bool SaveBindings();
    void DrawBindingTable(const char* id, ControlsBindings::Target target, const std::vector<ControlsBindings::Row>& rows);
    void Note(const std::string& line);

    Config& cfg_;
    std::string settings_file_;
    std::string user_data_root_;
    std::vector<Fix> fixes_;
    ControlsBindings bindings_;
    std::vector<std::string> log_;
    int selected_slot_ = -1;
    std::string pending_delete_;   // the save-state path whose delete awaits confirmation (review C2 loop 1)
};

}  // namespace ps2r::app
