// core/config/config_validator.cpp -- see config_validator.h.
#include "engine/core/config/config_validator.h"

#include <vector>

namespace ps2r {

namespace {
bool InRange(int v, int lo, int hi) { return v >= lo && v <= hi; }
bool IsPow2(int v) { return v > 0 && (v & (v - 1)) == 0; }
bool Refuse(std::string* err, const char* msg) {
    if (err) *err = msg;
    return false;
}
}  // namespace

void ConfigValidator::ApplyDependentDefaults(Config& c, bool latency_model_explicit) {
    // Freerun pacing has no wall clock to model disc latency against.
    if (c.engine.pacing == Pacing::Freerun && !latency_model_explicit)
        c.cdvd.latency_model = CdvdLatencyModel::Instant;
}

std::vector<std::string> ConfigValidator::DegradeDependents(Config& c, bool retained_startup_available,
                                                            std::string* startup_refusal) {
    // One rule per Validate() hle.* dependency, in prerequisite order so one
    // pass settles every chain (a degraded key is itself a prerequisite of the
    // rules below it). Each line names the dependent and the first unmet
    // prerequisite; nothing else is touched.
    std::vector<std::string> notes;
    auto off = [&](bool& key, const char* name, const char* prerequisite) {
        if (!key) return;
        key = false;
        notes.push_back(std::string(name) + " disabled: requires " + prerequisite);
    };
    // GL and Metal host the live video scene the Graphics producers need; Soft hosts none.
    const bool gl = c.video.backend == VideoBackend::Gl || c.video.backend == VideoBackend::Metal;
    if (!gl) {
        off(c.hle.graphics_shadows, "hle.graphics_shadows", "video.backend=gl");
        off(c.hle.graphics_distortion, "hle.graphics_distortion", "video.backend=gl");
        off(c.hle.graphics_screen, "hle.graphics_screen", "video.backend=gl");
        off(c.hle.graphics_sprites, "hle.graphics_sprites", "video.backend=gl");
        off(c.hle.graphics_text, "hle.graphics_text", "video.backend=gl");
        off(c.hle.graphics_sky, "hle.graphics_sky", "video.backend=gl");
        off(c.hle.graphics_frame, "hle.graphics_frame", "video.backend=gl");
        off(c.hle.graphics_movie, "hle.graphics_movie", "video.backend=gl");
        if (c.hle.graphics_strips == GraphicsStrips::All) {
            c.hle.graphics_strips = GraphicsStrips::Plain;
            notes.push_back("hle.graphics_strips=all disabled (plain): requires video.backend=gl");
        }
        // Textures at load convert into the shared video scene's resources
        // (GraphicsRuntime requires the live scene): the Soft backend hosts none.
        off(c.hle.graphics_textures, "hle.graphics_textures", "video.backend=gl");
        off(c.hle.graphics_meshes, "hle.graphics_meshes", "video.backend=gl");
    }
    // The owned mesh bucket reads every world texture as a converted asset
    // (TextureAssets::Find under textures at load); without that owner a
    // certified material's texture is MissingAsset and every instance refuses.
    if (!c.hle.graphics_textures) off(c.hle.graphics_meshes, "hle.graphics_meshes", "hle.graphics_textures");
    if (!c.hle.movie_lifecycle) off(c.hle.graphics_movie, "hle.graphics_movie", "hle.movie_lifecycle");
    if (!c.hle.movie_lifecycle) off(c.hle.movie_step, "hle.movie_step", "hle.movie_lifecycle");
    if (!c.hle.graphics_movie) off(c.hle.movie_step, "hle.movie_step", "hle.graphics_movie");
    if (!c.hle.music) off(c.hle.music_buffers, "hle.music_buffers", "hle.music");
    if (!c.hle.movie_lifecycle) off(c.hle.music_buffers, "hle.music_buffers", "hle.movie_lifecycle");
    if (!c.hle.movie_lifecycle) off(c.hle.movie_start, "hle.movie_start", "hle.movie_lifecycle");
    if (!c.hle.graphics_movie) off(c.hle.movie_start, "hle.movie_start", "hle.graphics_movie");
    if (!c.hle.music_buffers) off(c.hle.movie_start, "hle.movie_start", "hle.music_buffers");
    if (!c.hle.movie_start) off(c.hle.movie_wait, "hle.movie_wait", "hle.movie_start");
    if (!c.hle.movie_wait) off(c.hle.movie_stop, "hle.movie_stop", "hle.movie_wait");
    if (!c.hle.movie_step) off(c.hle.movie_stop, "hle.movie_stop", "hle.movie_step");
    if (!c.hle.music) off(c.hle.audio_mix, "hle.audio_mix", "hle.music");
    if (!c.hle.movie_lifecycle) off(c.hle.audio_mix, "hle.audio_mix", "hle.movie_lifecycle");
    if (c.iop.dispatch != IopDispatch::Frame) off(c.hle.audio_mix, "hle.audio_mix", "iop.dispatch=frame");
    if (!c.hle.stream_files && !c.hle.sound_samples && !c.hle.music)
        off(c.hle.service_time, "hle.service_time", "hle.stream_files, hle.sound_samples or hle.music");
    if (c.iop.dispatch != IopDispatch::Frame) off(c.hle.service_time, "hle.service_time", "iop.dispatch=frame");
    // M11 service-only startup: every consumer of STREAM.IRX / CDVDSTM.IRX /
    // LIBSD.IRX / SDRDRV.IRX / the IOP heap must have an owner before the IOP
    // boot is skipped; one unmet owner keeps the retained (booted) startup.
    if (c.hle.startup == StartupMode::Native) {
        const char* unmet = !c.hle.stream_files ? "hle.stream_files" : !c.hle.files ? "hle.files" :
                            !c.hle.sound_bank_headers ? "hle.sound_bank_headers" : !c.hle.sound_samples ? "hle.sound_samples" :
                            !c.hle.music ? "hle.music" : !c.hle.music_buffers ? "hle.music_buffers" :
                            !c.hle.audio_mix ? "hle.audio_mix" : !c.hle.movie_lifecycle ? "hle.movie_lifecycle" :
                            !c.hle.movie_start ? "hle.movie_start" : !c.hle.input_service ? "hle.input_service" :
                            !c.hle.save_storage ? "hle.save_storage" :
                            c.iop.dispatch != IopDispatch::Frame ? "iop.dispatch=frame" : nullptr;
        if (unmet && retained_startup_available) {
            c.hle.startup = StartupMode::Retained;
            notes.push_back(std::string("hle.startup=native degraded (retained): requires ") + unmet);
        } else if (unmet) {
            // M12: the service-only binary has no retained arm to degrade to -- a
            // REFUSAL naming the key, never a silent IOP boot.
            if (startup_refusal)
                *startup_refusal = std::string("hle.startup=native requires ") + unmet +
                                   "=on: the retained (IOP-booting) fallback is not compiled into this service-only "
                                   "binary (nativeee); turn the key back on, or run the oracle binary nativeee_oracle";
        }
    }
    return notes;
}

bool ConfigValidator::Validate(const Config& c, bool harness_window_requested, std::string* err) {
    if (c.hle.graphics_movie && !c.hle.movie_lifecycle)
        return Refuse(err, "hle.graphics_movie requires hle.movie_lifecycle=on");
    if (c.hle.movie_step && (!c.hle.movie_lifecycle || !c.hle.graphics_movie))
        return Refuse(err, "hle.movie_step requires hle.movie_lifecycle=on and hle.graphics_movie=on");
    if (c.hle.movie_start && (!c.hle.movie_lifecycle || !c.hle.graphics_movie || !c.hle.music_buffers))
        return Refuse(err, "hle.movie_start requires movie_lifecycle, graphics_movie and music_buffers");
    if (c.hle.movie_wait && !c.hle.movie_start)
        return Refuse(err, "hle.movie_wait requires hle.movie_start=on");
    if (c.hle.movie_stop && (!c.hle.movie_wait || !c.hle.movie_step))
        return Refuse(err, "hle.movie_stop requires hle.movie_wait=on and hle.movie_step=on");
    if ((c.hle.graphics_shadows || c.hle.graphics_distortion || c.hle.graphics_screen || c.hle.graphics_sprites || c.hle.graphics_text || c.hle.graphics_sky ||
         c.hle.graphics_movie || c.hle.graphics_frame || c.hle.graphics_textures || c.hle.graphics_meshes ||
         c.hle.graphics_strips == GraphicsStrips::All) &&
        c.video.backend == VideoBackend::Soft)
        return Refuse(err, "selected Graphics producers require a live video backend (video.backend=gl|metal)");
    if (c.hle.graphics_meshes && !c.hle.graphics_textures)
        return Refuse(err, "hle.graphics_meshes requires hle.graphics_textures=on");
    if (c.hle.audio_mix && (!c.hle.music || !c.hle.movie_lifecycle || c.iop.dispatch != IopDispatch::Frame))
        return Refuse(err, "hle.audio_mix requires hle.music=on, hle.movie_lifecycle=on and iop.dispatch=frame");
    if (c.hle.music_buffers && (!c.hle.music || !c.hle.movie_lifecycle))
        return Refuse(err, "hle.music_buffers requires hle.music=on and hle.movie_lifecycle=on");
    if (c.hle.service_time && ((!c.hle.stream_files && !c.hle.sound_samples && !c.hle.music) ||
                              c.iop.dispatch != IopDispatch::Frame))
        return Refuse(err, "hle.service_time requires selected files and iop.dispatch=frame while remaining services exist");
    if (c.time.tick_sync && c.engine.mtvu)
        return Refuse(err, "time.tick_sync requires engine.mtvu=off (EE-synchronous ticks cannot "
                           "interleave a VU1 worker)");
    if (c.engine.mtvu_depth < 1)
        return Refuse(err, "engine.mtvu_depth must be >= 1");
    if (!InRange(c.video.scale, 0, 4))
        return Refuse(err, "video.scale must be in 0..4 (0 = native window)");
    if (c.video.anisotropy != 1 && c.video.anisotropy != 2 && c.video.anisotropy != 4 &&
        c.video.anisotropy != 8 && c.video.anisotropy != 16)
        return Refuse(err, "video.anisotropy must be one of 1, 2, 4, 8, 16");
    if (!InRange(c.video.custom_scale_pct, 25, 400))
        return Refuse(err, "video.custom_scale_pct must be in 25..400");
    if (!InRange(c.video.custom_offset_x, -8192, 8192) ||
        !InRange(c.video.custom_offset_y, -8192, 8192))
        return Refuse(err, "video.custom_offset_x/y must be in -8192..8192");
    if (c.video.fps_cap < 0)
        return Refuse(err, "video.fps_cap must be >= 0 (0 = uncapped)");
    if (!InRange(c.audio.volume, 0, 100))
        return Refuse(err, "audio.volume must be in 0..100");
    if (!InRange(c.audio.sdr_block_ms, 1, 1000))
        return Refuse(err, "audio.sdr_block_ms must be in 1..1000");
    if (!InRange(c.engine.shutdown_wait_s, 0, 600))
        return Refuse(err, "engine.shutdown_wait_s must be in 0..600");
    if (c.engine.ee_runahead < 0)
        return Refuse(err, "engine.ee_runahead must be >= 0");
    if (c.engine.turbo < 0)
        return Refuse(err, "engine.turbo must be >= 0");
    if (c.cdvd.latency_scale < 0.0)
        return Refuse(err, "cdvd.latency_scale must be >= 0");
    if (!(c.cdvd.poll_latency_scale > 0.0))
        return Refuse(err, "cdvd.poll_latency_scale must be > 0");
    if (c.cdvd.mc_latency_polls < 0)
        return Refuse(err, "cdvd.mc_latency_polls must be >= 0");
    if (!IsPow2(c.log.ring_events) || !InRange(c.log.ring_events, 1024, 1 << 22))
        return Refuse(err, "log.ring_events must be a power of two in 1024..4194304");
    if (c.save.slot < 0)
        return Refuse(err, "save.slot must be >= 0");
    if (c.harness.frames < 0 || c.harness.state_every < 0 || c.harness.warp_to_frame < 0)
        return Refuse(err, "harness.frames/state_every/warp_to_frame must be >= 0");
    if (c.harness.record_input && c.harness.out_dir.empty())
        return Refuse(err, "--record-input requires --harness-out <directory>");
    if (c.harness.inject_fault_frame < 0)
        return Refuse(err, "harness.inject_fault_frame must be >= 0 (0 = off)");
    if (c.harness.inject_fault_frame > 0 && c.harness.out_dir.empty())
        return Refuse(err, "harness.inject_fault_frame requires --harness-out <directory> (a harness leg, never a plain play session)");
    if (c.harness.save_at_frame < 0)
        return Refuse(err, "harness.save_at_frame must be >= 0 (0 = off)");
    if (c.harness.save_at_frame > 0 && c.harness.out_dir.empty() && c.paths.save_dir.empty())
        return Refuse(err, "harness.save_at_frame requires --harness-out <directory> or paths.save_dir (the savestate root)");
    if (c.harness.restore_at_frame < 0)
        return Refuse(err, "harness.restore_at_frame must be >= 0 (0 = off)");
    if (c.harness.restore_at_frame > 0 && c.harness.out_dir.empty() && c.paths.save_dir.empty())
        return Refuse(err, "harness.restore_at_frame requires --harness-out <directory> or paths.save_dir (the savestate root)");
    if (c.harness.display_hz < 0 || c.harness.display_hz > 1000)
        return Refuse(err, "harness.display_hz must be in 0..1000 (0 = the real display)");
    if (c.harness.display_hz > 0 && c.harness.headless)
        return Refuse(err, "harness.display_hz requires a window (--harness-window): a headless leg has no presenter");
    if (!c.harness.inject_key.empty()) {
        if (c.harness.headless)
            return Refuse(err, "harness.inject_key requires a window (--harness-window): a headless leg has nothing to press a key in");
        // "<KEY>@<frame>[-<frame>][,...]": KEY a non-empty name without '@' ',' '-'
        // (F1..F12 or an SDL key name -- resolved by the session, which refuses an
        // unknown name loudly), frame > 0, an optional hold end >= the press frame.
        std::string rest = c.harness.inject_key;
        while (!rest.empty()) {
            const size_t comma = rest.find(',');
            const std::string item = rest.substr(0, comma);
            rest = comma == std::string::npos ? std::string() : rest.substr(comma + 1);
            const size_t at = item.find('@');
            bool ok = at != std::string::npos && at >= 1 && at + 1 < item.size();
            if (ok) {
                const std::string frames = item.substr(at + 1);
                const size_t dash = frames.find('-');
                const std::string from = frames.substr(0, dash);
                const std::string to = dash == std::string::npos ? std::string() : frames.substr(dash + 1);
                auto number = [](const std::string& text, unsigned long long& value) {
                    if (text.empty() || text[0] == '0' || text.size() > 12) return false;
                    for (char ch : text) if (ch < '0' || ch > '9') return false;
                    value = std::strtoull(text.c_str(), nullptr, 10);
                    return true;
                };
                unsigned long long a = 0, b = 0;
                ok = number(from, a) && (dash == std::string::npos || (number(to, b) && b >= a));
                for (size_t i = 0; i < at && ok; ++i) ok = item[i] != ',' && item[i] != '-';
            }
            if (!ok) return Refuse(err, "harness.inject_key must be <KEY>@<frame>[-<frame>][,...] with KEY F1..F12 or an SDL key name and frame > 0 (e.g. F5@300, Space@600-660)");
        }
    }
    if (c.harness.close_at_frame < 0)
        return Refuse(err, "harness.close_at_frame must be >= 0 (0 = off)");
    if (c.harness.close_at_frame > 0 && c.harness.headless)
        return Refuse(err, "harness.close_at_frame requires a window (--harness-window): a headless leg has no window to close");
    if (c.harness.close_after_ms < 0 || (c.harness.close_after_ms > 0 && c.harness.close_at_frame == 0))
        return Refuse(err, "harness.close_after_ms must be >= 0 and needs harness.close_at_frame=N (the boundary it counts from)");
    if (!c.harness.virtual_pad.empty()) {
        if (c.harness.headless)
            return Refuse(err, "harness.virtual_pad requires a window (--harness-window): a headless leg has no pad to plug in");
        // "attach@N[,press@K-L][,detach@M]": each verb at most once, frames > 0, press a range.
        std::string rest = c.harness.virtual_pad;
        int attach = 0, press = 0, detach = 0;
        while (!rest.empty()) {
            const size_t comma = rest.find(',');
            const std::string item = rest.substr(0, comma);
            rest = comma == std::string::npos ? std::string() : rest.substr(comma + 1);
            const size_t at = item.find('@');
            bool ok = at != std::string::npos && at + 1 < item.size();
            const std::string verb = ok ? item.substr(0, at) : std::string();
            const std::string frames = ok ? item.substr(at + 1) : std::string();
            const size_t dash = frames.find('-');
            auto number = [](const std::string& text) {
                if (text.empty() || text[0] == '0' || text.size() > 12) return false;
                for (char ch : text) if (ch < '0' || ch > '9') return false;
                return true;
            };
            if (ok && verb == "press") {
                ok = dash != std::string::npos && number(frames.substr(0, dash)) && number(frames.substr(dash + 1)) &&
                     std::strtoull(frames.c_str() + dash + 1, nullptr, 10) >= std::strtoull(frames.c_str(), nullptr, 10);
                ++press;
            } else if (ok && (verb == "attach" || verb == "detach")) {
                ok = dash == std::string::npos && number(frames);
                (verb == "attach" ? attach : detach)++;
            } else ok = false;
            if (!ok || attach > 1 || press > 1 || detach > 1)
                return Refuse(err, "harness.virtual_pad must be attach@N[,press@K-L][,detach@M], each once, frames > 0 (e.g. attach@600,press@700-760,detach@900)");
        }
        if (attach == 0) return Refuse(err, "harness.virtual_pad needs attach@N");
    }
    if (!c.harness.display_sequence.empty()) {
        if (c.harness.headless)
            return Refuse(err, "harness.display_sequence requires a window (--harness-window): a headless leg has no display to change");
        // "<frame>:<mode>[,<frame>:<mode>...]", frame > 0, mode one of the Display
        // page's requests (the composition root's DisplaySequence names the same set).
        static const char* const kModes[] = {"windowed", "fullscreen", "borderless", "scale1", "scale2", "scale3", "scale4",
                                             "vsync", "novsync", "aspect_auto", "aspect_4_3", "aspect_16_9", "aspect_16_10",
                                             "aspect_21_9", "filter_title", "filter_point", "filter_bilinear", "filter_trilinear",
                                             "aniso1", "aniso2", "aniso4", "aniso8", "aniso16", "overlay", "nooverlay", "rate60",
                                             "ratedisplay"};
        std::string rest = c.harness.display_sequence;
        while (!rest.empty()) {
            const size_t comma = rest.find(',');
            const std::string item = rest.substr(0, comma);
            rest = comma == std::string::npos ? std::string() : rest.substr(comma + 1);
            const size_t colon = item.find(':');
            bool ok = colon != std::string::npos && colon >= 1 && colon + 1 < item.size() && item[0] != '0';
            for (size_t i = 0; i < colon && ok; ++i) ok = item[i] >= '0' && item[i] <= '9';
            if (ok) {
                const std::string mode = item.substr(colon + 1);
                ok = false;
                for (const char* name : kModes) ok = ok || mode == name;
            }
            if (!ok)
                return Refuse(err, "harness.display_sequence must be <frame>:<mode>[,...] with frame > 0 and mode one of "
                                   "windowed|fullscreen|borderless|scale1..scale4|vsync|novsync|aspect_<auto|4_3|16_9|16_10|21_9>|"
                                   "filter_<title|point|bilinear|trilinear>|aniso<1|2|4|8|16>|overlay|nooverlay|rate60|ratedisplay "
                                   "(e.g. 300:borderless,600:windowed)");
        }
    }
    if (c.harness.live_pad_probe) {
        if (c.harness.headless)
            return Refuse(err, "harness.live_pad_probe requires a window (--harness-window): the live pad is the window's");
        if (c.harness.inputs.empty())
            return Refuse(err, "harness.live_pad_probe requires --inputs: it reads the live pad beside a replay");
    }
    // TRIAGE_CAPTURE section 3: fps_target 0 = auto, ring_period_s 0 = ring off.
    if (c.triage.fps_target < 0 || c.triage.disk_budget_mb < 0 || c.triage.ring_period_s < 0)
        return Refuse(err, "triage.fps_target/ring_period_s/disk_budget_mb must be >= 0");
    const bool harness_armed = !c.harness.out_dir.empty() || c.harness.frames > 0 ||
                               !c.harness.load_state.empty() || !c.harness.inputs.empty() ||
                               c.harness.record_input || c.harness.state_every > 0 || !c.harness.inject_key.empty() ||
                               !c.harness.display_sequence.empty() || c.harness.close_at_frame > 0 ||
                               !c.harness.virtual_pad.empty();
    if (harness_armed && !c.harness.headless && !harness_window_requested)
        return Refuse(err, "harness.* requires --headless (or an explicit --harness-window)");
    return true;
}

}  // namespace ps2r
