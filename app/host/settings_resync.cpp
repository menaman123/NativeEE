// app/host/settings_resync.cpp -- see settings_resync.h.
#include "app/host/settings_resync.h"

#include <cstdlib>

namespace ps2r::app {
namespace {

using Action = SessionControls::Action;

int IndexOf(const std::string& v, const char* const* names, int count) {
    for (int i = 0; i < count; i++)
        if (v == names[i]) return i;
    return -1;
}

int OnOff(const std::string& v) {
    if (v == "on" || v == "true" || v == "1") return 1;
    if (v == "off" || v == "false" || v == "0") return 0;
    return -1;
}

bool Int(const std::string& v, int lo, int hi, int* out) {
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (v.empty() || !end || *end || n < lo || n > hi) return false;
    *out = static_cast<int>(n);
    return true;
}

}  // namespace

std::vector<SettingsResync::Request> SettingsResync::Plan(const std::vector<Entry>& entries,
                                                          const SessionControls::RuntimeState& r,
                                                          const SessionControls::Capabilities& caps) {
    // The same name tables as the settings file's enums (config.h) and the
    // writers in SessionRuntime::ProcessCommands.
    static const char* const kModes[] = {"windowed", "fullscreen", "borderless"};
    static const char* const kAspects[] = {"auto", "4:3", "16:9", "16:10", "21:9", "32:9"};
    static const char* const kCaps[] = {"off", "30", "60"};
    static const char* const kRates[] = {"60", "display"};
    static const char* const kFilters[] = {"title", "point", "bilinear", "trilinear"};
    std::vector<Request> out;
    for (const Entry& e : entries) {
        const std::string& k = e.first;
        const std::string& v = e.second;
        int n = -1;
        if (k == "video.fullscreen" && caps.fullscreen) {
            if ((n = IndexOf(v, kModes, 3)) >= 0 && n != r.display_mode) out.emplace_back(Action::SetDisplayMode, n);
        } else if (k == "video.scale" && caps.display_rows) {
            if (Int(v, 0, 4, &n) && n != r.scale) out.emplace_back(Action::SetScale, n);
        } else if (k == "video.vsync" && caps.display_rows) {
            if ((n = OnOff(v)) >= 0 && (n != 0) != r.vsync) out.emplace_back(Action::SetVSync, n);
        } else if (k == "video.aspect" && caps.display_rows) {
            if ((n = IndexOf(v, kAspects, 6)) >= 0 && n != r.aspect) out.emplace_back(Action::SetAspect, n);
        } else if (k == "video.frame_limit" && caps.frame_limit) {
            if (Int(v, 0, 1000, &n) && n != r.frame_limit) out.emplace_back(Action::SetFrameLimit, n);
        } else if (k == "video.motion_blur" && caps.motion_blur) {
            if ((n = OnOff(v)) >= 0 && (n != 0) != r.motion_blur) out.emplace_back(Action::SetMotionBlur, n);
        } else if (k == "input.invert_camera_x" && caps.camera_invert) {
            if ((n = OnOff(v)) >= 0 && (n != 0) != r.invert_camera_x) out.emplace_back(Action::SetInvertCameraX, n);
        } else if (k == "input.invert_camera_y" && caps.camera_invert) {
            if ((n = OnOff(v)) >= 0 && (n != 0) != r.invert_camera_y) out.emplace_back(Action::SetInvertCameraY, n);
        } else if (k == "video.frame_cap" && caps.display_rows) {
            if ((n = IndexOf(v, kCaps, 3)) >= 0 && n != r.frame_cap) out.emplace_back(Action::SetFrameCap, n);
        } else if (k == "video.render_rate" && caps.display_rows) {
            if ((n = IndexOf(v, kRates, 2)) >= 0 && n != r.render_rate) out.emplace_back(Action::SetRenderRate, n);
        } else if (k == "video.texture_filter" && caps.texture_filter) {
            if ((n = IndexOf(v, kFilters, 4)) >= 0 && n != r.texture_filter) out.emplace_back(Action::SetTextureFilter, n);
        } else if (k == "video.anisotropy" && caps.texture_filter) {
            if (Int(v, 1, 16, &n) && n != r.anisotropy) out.emplace_back(Action::SetAnisotropy, n);
        } else if (k == "ui.perf_overlay" && caps.perf_overlay) {
            if ((n = OnOff(v)) >= 0 && (n != 0) != r.perf_overlay) out.emplace_back(Action::SetPerfOverlay, n);
        } else if (k == "input.kbm_pad" && caps.keyboard_input) {
            if ((n = OnOff(v)) >= 0 && (n != 0) != r.keyboard_input) out.emplace_back(Action::SetKeyboardInput, n);
        } else if (k == "audio.volume" && caps.volume) {
            if (Int(v, 0, 100, &n) && n != r.volume_percent) out.emplace_back(Action::SetVolume, n);
        }
    }
    return out;
}

}  // namespace ps2r::app
