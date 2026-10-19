// core/config/config.h -- THE configuration schema (ARCHITECTURE section 4.8,
// PHASE1_PLAN section 3). Every field exists from Phase 1 with its PEAK default
// (the FLAG_LEDGER SETTINGS table); `ConfigLoader` is the only writer and the
// process holds one immutable instance passed by const&.
//
// String access (Get/Set by "section.key") serves the loader, --set, the
// fingerprint and the tests; the fields are read directly by their consumers.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps2r {

enum class VideoBackend : uint8_t { Gl = 0, Soft, Metal };   // Metal: Apple GPUs (macOS builds)
enum class FitMode : uint8_t { Stretch = 0, Fit, Fill, Integer, Custom };
// video.aspect: the display shape the title renders FOR (the projection, culling
// frustum, HUD fit and movie picture take the ratio -- TR renderer.cpp's
// g_WidescreenTv term -- and the presenter fits the anamorphic frame at it).
// `auto` snaps the window's shape to the nearest of the four; 4:3 is the console's
// TV and the deterministic lanes' identity (no model body installed).
enum class AspectSel : uint8_t { Auto = 0, R4_3, R16_9, R16_10, R21_9 };
// video.fullscreen: the session window's mode -- a resizable window, the
// display's own mode (exclusive fullscreen) or a borderless window covering the
// desktop. Applied live from the title's Display page; persisted (Charter 2 A2).
enum class FullscreenMode : uint8_t { Windowed = 0, Fullscreen, Borderless };
// video.render_rate (Charter 2 A3): `60` presents one frame per game tick (the
// presenter paced at video.fps_cap); `display` presents at the display's refresh
// (or harness.display_hz when simulated): each presented picture is the previous
// tick's frame RE-VIEWED at the camera interpolated between the two latest ticks
// (FrameReview on the GS consumer: the camera-projected VU1 families re-projected,
// screen-space and model/raw-route draws at the tick; one tick of presentation
// latency). The game's logic tick stays 60 Hz in both.
// PARKED (Charter 3, 2026-10-13): `display` causes the pause->Continue ring-screen
// regression (s7 vs s8 bisect; 60 clean). Default 60; the Display-page row is hidden
// (MenuComposition) until Charter 3 item 6; the ini / --set / harness pins still take it.
enum class RenderRate : uint8_t { Tick60 = 0, Display };
// video.frame_cap: WHICH presenter fields show a NEW picture (the presented-frame
// cadence a player sees; the game's logic tick stays 60 Hz by the clock contract
// and no game tick is dropped -- a picture the cap does not show is superseded by
// the next). `60` = a new picture at every 60 Hz field edge it is ready for (the
// first title's shape); `30` = at every OTHER field edge, a steady 33.3 ms train
// (a game producing 30..60 pictures per second shows every one at a fixed
// cadence instead of a 1/2/3-field mixture); `off` = at the pump it arrives
// (the consumer's own cadence, no field quantisation). Applied live from the
// Display page (FramePacer); persisted.
enum class FrameCap : uint8_t { Off = 0, Hz30, Hz60 };
// video.texture_filter (Charter 2 A2): the player's filtering enhancement over
// the textures the engine owns at load. `title` = the title's own TEX1 choice per
// draw (point or bilinear, mip-nearest where it authored a chain) -- the
// deterministic lanes' identity; `point` / `bilinear` force nearest / linear;
// `trilinear` forces linear with linear level blending over the owned mip chain.
// The GS layer's render::TextureFilter shares this order.
enum class TextureFilterSel : uint8_t { Title = 0, Point, Bilinear, Trilinear };
enum class CdvdLatencyModel : uint8_t { Instant = 0, Modelled };
// When a completed host-served disc read becomes visible to the EE: at the next
// boundary (slot 4) or at the poll that asks once the modelled drive is done.
enum class CdvdDeliveryVisibility : uint8_t { Boundary = 0, Poll };
// The VU1 fallback tier: hle = the title's library over the cached tier (the
// Crash ladder), cached, interp, block (the pre-resolved straight-line block
// tier), aot (the generic run-time recompiler: a verified image on its
// (kind, dest)-specialised block table, every other image on vu1_aot_fallback).
enum class Vu1Mode : uint8_t { Hle = 0, Cached, Interp, Block, Aot };
// Where an image the AOT tier has not verified runs.
enum class Vu1AotFallback : uint8_t { Block = 0, Interp };
enum class Pacing : uint8_t { Realtime = 0, Freerun };
enum class IopDispatch : uint8_t { Realtime = 0, Frame };
// The first due of a RUNNING hard timer restored from a savestate: one period
// after the restored now, or at the restore itself (the earliest admissible phase).
enum class IopRestoredTimerDue : uint8_t { OnePeriod = 0, AtRestore };
enum class LogLevelSetting : uint8_t { Quiet = 0, Error, Warn, Info, Narrate };
// Where the GS DrawIR consumer (the DrawPipeline) runs: inline on the pushing EE
// thread at each kick, or on its own consumer thread fed by the DrawIR ring --
// `thread` in lockstep (each push returns consumed: frames identical by
// construction), `pipelined` with the ring running ahead to the frame boundary
// on its own GS local-memory image (the EE's image calls travel in the stream,
// DP-25 (b)): identical to inline on the deterministic lanes (the pin
// RootGsQueuePipelinedIdentical; the s4 replay) and the default.
enum class GsQueue : uint8_t { Inline = 0, Thread, Pipelined };
enum class GraphicsStrips : uint8_t { Model = 0, Plain, All };
// M11 service-only startup: `retained` boots the IOP kernel with the recompiled
// STREAM.IRX and routes the title's STREAM/libsd SIF surface to it (the oracle
// arm the deterministic profile keeps); `native` never boots the IOP -- the
// eight sceSifLoadModule requests, the IOP heap and the STREAM RPC batches are
// answered by the owned services (StreamTransport, SoundEffects, the libraries).
enum class StartupMode : uint8_t { Retained = 0, Native };
// F8 (track 3a, Bernoulli): `live` restores the newest slot IN the running
// process -- the game thread quiesced at an admitted boundary, Graphics settled
// (the capture settle), every owner's restore on the live composition, the
// presenter/audio re-armed without recreating the window or the GL device;
// `relaunch` keeps the former path (the session closes and the same image
// re-executes with --load-state <slot>). Live is the default; it falls back to
// the relaunch, naming why, when the composition retains IOP state
// (StartupOwnership) -- the fallback is never silent.
enum class RestoreMode : uint8_t { Live = 0, Relaunch };

struct VideoConfig {
    int          scale = 1;               // 1..4; 1 is the byte-identity contract and the play
                                          // default (track 1d: the level route's frame rate); 2..4 an option;
                                          // 0 = native window (the factor follows the window's drawable height
                                          // in 448-line steps, 1..4; play mode only -- a leg reads 1)
    bool         interp = true;           // present interpolation (1 frame)
    int          fps_cap = 60;            // 0 = uncapped
    bool         vsync = false;
    FullscreenMode fullscreen = FullscreenMode::Windowed;
    RenderRate   render_rate = RenderRate::Tick60;
    FrameCap     frame_cap = FrameCap::Hz60;
    VideoBackend backend = VideoBackend::Gl;
    FitMode      fit_mode = FitMode::Fit;
    AspectSel    aspect = AspectSel::Auto;
    int          custom_scale_pct = 100;  // 25..400
    int          custom_offset_x = 0;     // -8192..8192
    int          custom_offset_y = 0;     // -8192..8192, +Y down
    // A4 texture pack: `<paths.texture_packs>/<name>/<TextureContentHash>.png`
    // replaces the converted texture with that hash at load, any resolution.
    // "none" = no pack (the converted textures exactly; the identity arm).
    std::string  texture_pack = "none";
    // A2 texture filtering: the enhancement mode and the anisotropy (1 = none;
    // 2|4|8|16, clamped to the driver's limit at the draw). Both apply live to
    // the owned textures' samplers from the in-game Display page; `title` / 1
    // leave every sampler at the title's choice.
    TextureFilterSel texture_filter = TextureFilterSel::Title;
    int          anisotropy = 1;
};

struct AudioConfig {
    bool enabled = true;
    int  volume = 100;                    // 0..100
    int  sdr_block_ms = 8;                // libsdr virtual SPU2 block transfer duration (ms of mixer time)
};

struct InputConfig {
    std::string device = "auto";
    bool        kbm_pad = true;
    std::string bindings_file = "default_bindings.ini";
    // The title's actuator requests reach the game controller (play mode).
    // Off: the pad reports its motors but no rumble is queued (CHARTER 2 A5).
    bool        rumble = true;
};

struct CdvdConfig {
    CdvdLatencyModel latency_model = CdvdLatencyModel::Instant;
    double           latency_scale = 1.0;
    int              mc_latency_polls = 4;
    bool             load_turbo = false;
    CdvdDeliveryVisibility delivery_visibility = CdvdDeliveryVisibility::Poll;   // default poll since gate S4 (DP-17 attempt 4 KEPT, 2026-10-03); `boundary` = the pre-decision model
    double           poll_latency_scale = 1.0;   // the drive's time under delivery_visibility=poll (> 0; the deterministic profile leaves it)
};

struct EngineConfig {
    Vu1Mode vu1_mode = Vu1Mode::Cached;
    // The AOT tier (engine.vu1_mode=aot): the corpus directory of Vu1Case
    // files its verifier reads at start ("" = only the corpus collected under
    // <user_data>/vu1_aot/corpus), whether an image must be verified before it
    // runs native (off = every image native: the harness / bench knob, never
    // the play default), and the tier an unverified image runs on.
    std::string    vu1_aot_corpus;
    bool           vu1_aot_verify = true;
    Vu1AotFallback vu1_aot_fallback = Vu1AotFallback::Block;
    // The generated-code step above the specialised tier (Vu1AotBuilder): on
    // = a verified image's blocks are emitted as C++, compiled with the host
    // toolchain found at run time and loaded once re-verified; the compiler
    // ("" = /usr/bin/clang++, then c++ / g++ / clang++ on the PATH; "none" =
    // never compile: the shipped pack / the cache alone), the staged prelude
    // include root ("" = the build's), the -O level (2 or 1).
    bool           vu1_aot_codegen = true;
    std::string    vu1_aot_compiler;
    std::string    vu1_aot_prelude;
    int            vu1_aot_opt = 2;
    // The pre-built library pack (app/host/vu1_aot_pack.h) the loader tries
    // before the user cache and the compiler ("" = <executable dir>/vu1_aot_pack).
    std::string    vu1_aot_pack;
    bool    mtvu = true;
    int     mtvu_depth = 2;
    // The clock contract's KICK DROP (play mode): the game thread never parks
    // on the MTVU worker; a frame the worker is > 2 frames behind runs its VIF
    // stream without its VU1 programs (run.meta mtvu= kick_drop/drop/kicks_
    // dropped). off = the bytes/frames PARK (the Mecha-Bandicoot 560 ms stall
    // of 2026-10-13; kept as the red-before and the deterministic lanes' path).
    bool    mtvu_kick_drop = true;
    int     ee_runahead = 2;
    Pacing  pacing = Pacing::Realtime;
    int     time_regime_pin = -1;         // -1 = none (adaptive)
    int     turbo = 0;
    int     shutdown_wait_s = 10;         // 0..600
};

struct TimeConfig {
    bool tick_sync = false;
    bool tick_deterministic = false;
    bool t0_tickpaced = false;
    bool maint_eepaced = false;
};

struct IopConfig {
    IopDispatch dispatch = IopDispatch::Frame;   // Frame until BUG-026 closes (PHASE4_PLAN 3.3/5.2)
    IopRestoredTimerDue restored_timer_due = IopRestoredTimerDue::OnePeriod;
};

struct GameConfig {
    bool rtc_fixed = false;
};

struct LoaderConfig {
    // A title's windowed file reader may serve its records from a callback-stream
    // ring it fills once per window of the file; `on` lets the title's loader seam
    // model that ring rule (DECISIONS_LOG DP-5 (b)), `off` fills the ring per record
    // (the pre-decision seam). Default on since DP-5 (b) attempt 2 was KEPT under the
    // DT-7 score (2026-10-06).
    bool ring_cache = true;
};

struct HleConfig {
    // Replacement selection for same-input retirement comparisons. New
    // boundaries stay opt-in until the frame/audio/census gate is accepted.
    //
    // ALL-ON, NO EXCEPTIONS (CUTOVER.md, user 2026-10-09 ~11:30): EVERY hle.*
    // toggle is the compiled default -- including graphics_frame,
    // graphics_strips=all, graphics_sprites, graphics_shadows, graphics_text and
    // stream_files, which the one-flip stages and the first all-on build turned
    // off after coldboot aborts (frames 60-220) or anchor refusals (GCLK). Those
    // are now KNOWN CRASHES the user plays into: the crash path writes a triage
    // bundle (engine/core/triage TriageBundle) and `--set hle.<key>=off` turns
    // any one key back off per run (ConfigValidator::DegradeDependents turns
    // the dependents off with a logged line instead of refusing). See
    // docs/design/M_CUTOVER.md "All-on build, no exceptions". The retired arms
    // stay selectable with `off` (the control arm of every self-oracle).
    //
    // Cutover flip 5 (2026-10-08): the MathLibrary helpers (macro, collision,
    // trig, angle, bounds, clipping and joint source operations) are the
    // default; the captured VU0-macro/generated bodies are retired for the
    // owned entries.
    bool math_helpers = true;
    // Own the entire chunk-view culling producer/reader group and CULL section.
    bool culling = true; // all-on
    // Cutover flip 4 (2026-10-08): the owned MovieDecoder lifecycle (Initialise/
    // Start/Finished/Close, FeedAudio, SetVolume, stream, demux, PCM gain) is the
    // default; the IPU command interface / libmpeg model is retired for the lifecycle.
    bool movie_lifecycle = true;
    bool movie_step = true; // all-on; complete software player Step; requires graphics_movie
    bool movie_start = true; // all-on; source Start with owned priming; requires movie buffers/pictures
    bool movie_wait = true; // all-on; complete WaitFrame service; requires movie_start
    bool movie_stop = true; // all-on; paired direct presentation and complete Stop; requires movie_wait/step
    bool sound_bank_headers = true;
    bool stream_files = true; // all-on, no exceptions (flip 6: the frozen anchors refuse GCLK -- recorded)
    bool files = true; // all-on; synchronous Platform::Files disc intents
    // One composition: owned file delivery, banks, sample controls and mixer.
    bool sound_samples = true; // all-on
    // Cutover flip 9b (2026-10-09, DT-13 re-run of flip 8): the owned MusicLibrary
    // service (file windows, immutable assets, Audio voices) is the default; the
    // libsd/STREAM music path is retired for the owned entries.
    bool music = true;
    bool music_buffers = true; // all-on; requires music + movie_lifecycle
    bool input_service = true; // all-on
    bool save_storage = true; // all-on
    bool audio_mix = true; // all-on; requires music + movie_lifecycle; owns final output
    bool service_time = true; // all-on; owned deadlines and loader headroom
    // M11 (Herschel 3a): the IOP never boots in play; requires every service
    // owner selected (stream_files, files, sound_bank_headers, sound_samples,
    // music, music_buffers, audio_mix, movie_lifecycle, movie_start,
    // input_service, save_storage) and iop.dispatch=frame. The deterministic
    // profile pins `retained` (the IOP oracles stay for the acceptance lanes).
    StartupMode startup = StartupMode::Native;
    GraphicsStrips graphics_strips = GraphicsStrips::All; // all-on, no exceptions (flip 3 aborted f118 -- known crash class)
    // all-on, no exceptions (the first all-on coldboot aborted at frame 113 `Shadow
    // model load could not establish owned geometry`, ShadowLoads::Run -- known
    // crash class); owned shadow application over the shared scene; requires
    // video.backend=gl.
    bool graphics_shadows = true;
    bool graphics_distortion = true; // all-on; owned bucket-23 distortion application over the shared scene; requires video.backend=gl
    bool graphics_screen = true; // all-on; owned bucket-4 screen effects (blur/filter/wave) over the shared scene; requires video.backend=gl
    bool graphics_sprites = true; // all-on, no exceptions (flips 1/1b aborted -- known crash class)
    // all-on, no exceptions (the first all-on coldboot: the first owned text group
    // of the title, frame 117, failed delivery and the walk aborted at frame 119 --
    // known crash class).
    bool graphics_text = true;
    bool graphics_sky = true; // all-on
    // Textures at load (TEXTURE_OWNERSHIP.md): every texture record the title
    // reads converts to an owned RGBA8 asset at TextureRead, and the title's
    // per-frame slot upload (WriteTextureUpload 0x1c0880) binds that asset to
    // the GS slot in stream order instead of sending the storage image over
    // PATH2 -- the texture resolver serves the bind (no VRAM mirror read, no
    // decode). Off: the UI pairs alone convert (sprites/text), the model uploads.
    bool graphics_textures = true;
    // M8 slice B (evidence/MESH.md, INTERFACES.md section 15): the owned mesh
    // bucket for the type-01 rigid/placed family -- the certified materials'
    // (MaterialCertificate) rigid/placed instances are collected at the title's
    // SetRigidModelDma_/SetPlacedModelDMA, their geometry built from the model
    // asset converted at ModelRead, the material captured at FlushMaterial, and
    // the draws recorded at the bucket's slot of the ordered output; no VIF/VU1/
    // GIF transport for those materials (gs_sink_path1_by_bucket 2/3 is the
    // retirement counter). Requires graphics_textures (every world texture a
    // converted asset) and video.backend=gl. Off: the model writers run.
    bool graphics_meshes = true;
    bool graphics_movie = true; // all-on (flip 8b: DT-13 (2)/(3) held)
    bool graphics_frame = true; // all-on, no exceptions (flips 2/2b aborted f62-63 -- known crash class)
    // A title workaround may route a class of the title's own small allocations
    // out of the title's allocator into a host arena outside the console's RAM
    // (the META-ARENA hook, HOOK_RECLASSIFICATION section 3.1). `on` keeps the
    // routing (the frozen baselines' observable); `off` lets the title's
    // allocator run as recompiled -- what the hardware does -- while the arena's
    // storage stays readable for restored images that already hold nodes there
    // (DECISIONS_LOG DP-31). Default `on` until the DT-7 score keeps `off`.
    bool meta_arena = true;
    // libmc's sceMcGetInfo(port, slot, *type, *free, *format) takes five
    // arguments; the EE calling convention passes the fifth in t0. `on` reads
    // *format from t0 (the hardware). `off` keeps the inherited stack read
    // (sp+0x10): the caller's spilled register, in this title its MC manager's
    // header -- the frozen baselines' observable (BUG-089). All-on: `on`.
    bool libmc_format_in_t0 = true;
};

struct GsConfig {
    GsQueue queue = GsQueue::Pipelined;   // `inline` = on the EE thread; `thread` = lockstep consumer thread (plan row 6.3)
};

struct SaveConfig {
    int slot = 0;
    RestoreMode restore_mode = RestoreMode::Live;
};

struct UiConfig {
    bool gui_open = true;
    // The in-game performance overlay (game Hz / render fps, the clock
    // contract's rule-5 census) drawn over the presented frame. The first
    // option of the title's added 'Display' menu page (MenuExtension): the
    // page's toggle and this key are one value, applied live and persisted in
    // the settings file (never the memory card).
    bool perf_overlay = false;
    // The owned menu extension (title MenuExtension): the 'Display' and
    // 'Controls' pages added to the title's options menu. Installed in PLAY
    // mode only (a headless or tick-synced leg never builds the pages, so the
    // deterministic lanes are unchanged by construction); `off` runs the
    // title's menu as shipped.
    bool menu_pages = true;
};

struct DevConfig {
    LogLevelSetting log_level = LogLevelSetting::Quiet;
};

// The first-run launcher (CHARTER 2 C2): the ImGui screen shown BEFORE the
// game starts when the extracted game files are absent (drop the disc image ->
// verify -> extract -> Play), and the settings screen afterwards. A harness
// command line (--game-files / --profile / --set / --headless / --iso ...)
// bypasses it; `show=off` goes straight to the game once the files exist.
struct LauncherConfig {
    bool show = true;
    // The SHA-256 the disc's title ELF must carry: "" = the ELF this build
    // recompiled (TitleProfile::elf_sha256). A test injects its fake ELF's hash
    // here (RootFirstRunExtract) -- never a way to accept another ELF for play.
    std::string elf_sha256;
};

// Quality-of-life options (CHARTER 2 A5), play mode only; the deterministic
// lanes never see them (no live pad, no wall clock).
struct QolConfig {
    // START or CROSS during a movie (FMV) skips it: the title's own skip
    // intent where the movie is skippable (TR gamecontroller.cpp: CROSS on a
    // skippable movie -> RequestStop), else the owned MovieDecoder reports the
    // movie finished and the title's state machine stops it.
    bool skip_movies = true;
    // Hold the fast-forward button during a cutscene: the title's cutscene
    // clock advances N game ticks per wall tick (the game boundary stays at
    // 60 Hz wall -- DT-14; the cutscene player alone runs ahead). 1 = off.
    int fast_forward = 1;
};

// RETAIL_BUGS fixes (CHARTER 2 A5): each a patch of a retail defect the decomp
// documents (TR docs/RETAIL_BUGS.md), default OFF -- the title as shipped --
// listed on the in-game 'Fixes' page with its citation.
struct FixesConfig {
    // LookController::Construct never stores restSeconds (+0x60): a new
    // head-look rests for the heap's leftover float. On: 0 is stored.
    bool look_rest_seconds = false;
    // ConstructSkidMarks never sets lastPoint (+0x10) or the two strip colours
    // (+0x30/+0x34): the first strip is coloured from heap leftovers. On: zeroed.
    bool skid_mark_init = false;
};

struct LogConfig {
    std::string channels = "all";          // ring channel list: all|none|perf,ee,gfx,input,mem
    int         ring_events = 65536;       // pow2 in 1024..4194304
};

struct DiagConfig {
    bool perf = false;
    // The tick's caller census (TickProfile::Mark): the hot hooks sample the
    // title's shadow call stack and attribute the tick's wall time to the
    // chains, printed in run.meta pace_long_trace as callers=... Off = marks
    // are counted only (the long trace keeps its site breakdown).
    bool tick_census = false;
    // The VU1 tier A/B (VuLockstep): every kick the configured tier runs is
    // also run on the interpreter reference on a shadow unit and the end
    // states (registers, data memory, clock, latches, kicked bytes) diffed;
    // the first divergences are printed, all are counted (run.meta
    // vu1_lockstep=). The GS sees the tier's output only. Off = no shadow.
    bool vu1_lockstep = false;
};

struct TriageConfig {
    bool enabled = true;
    bool fallbacks = true;
    bool perf = true;
    bool spikes = true;
    int  fps_target = 60;
    int  disk_budget_mb = 2048;
    int  ring_period_s = 10;    // 0 = ring off (TRIAGE_CAPTURE section 3)
};

struct HarnessConfig {
    std::string out_dir;                   // empty = harness off
    bool        headless = false;
    int         frames = 0;                // 0 = run until quit
    std::string load_state;
    std::string inputs;
    bool        record_input = false;
    // Optional explicit destination of the FIRST recording segment
    // (`--record-input <file>`); empty = `<out_dir>/input.padlog`.
    std::string record_input_path;
    // Explicit OLD producer classification for opt-in Input/Save migration.
    // Ordinary reproduction restore never treats missing state as legacy.
    bool        legacy_input_save = false;
    bool        live_restore = false; // root acceptance opt-in; not an ordinary menu setting
    // Harness-only fault injection (the TriageBundle acceptance pin): at frame
    // boundary N (> 0) the leg throws a synthetic fault so the crash path --
    // the triage bundle, run.meta park=exception, the census at exit -- is
    // proven on a real process. Accepted by `--set` only, and only with the
    // harness armed (a window-only play session refuses it). 0 = off.
    int         inject_fault_frame = 0;
    // Harness-only savestate trigger (the F5 path without a key press): from
    // frame boundary N (> 0) on, the leg captures the play-mode checkpoint at
    // the FIRST admissible boundary into save.slot (the same composition,
    // writer, refusal text and .meta sidecar as F5); after 120 refused
    // boundaries it refuses loudly, naming the owner. `--set` only, harness
    // armed. 0 = off.
    int         save_at_frame = 0;
    // Harness-only F8 trigger (the key press without a key): at frame boundary
    // N (> 0) a play session submits RestoreReproduction exactly as F8 does
    // (the newest slot or the browser's selection; the same preflight, loud
    // lines and relaunch). `--set` only, harness armed (or paths.save_dir set);
    // a window-less leg has no session to press it in. 0 = off. The relaunch
    // drops it (and save_at_frame) from its own argv: one press per leg.
    int         restore_at_frame = 0;
    // Harness-only synthetic KEY PRESS (the real key route, no finger): at frame
    // boundary N a play session pushes a real SDL_KEYDOWN/KEYUP pair for <key>
    // into SDL's queue, stamped the way SDL stamps a physical press (the
    // keyboard-focus window's id, 0 without focus), so the press travels
    // SDL_PollEvent -> SessionWindow -> SessionGui -> SessionControls exactly as
    // the user's does. Format "<KEY>@<frame>[,<KEY>@<frame>...]", KEY one of
    // F1..F12 (the GUI hotkey route) or any other SDL key name (Space, W, Tab,
    // Up, Return ... -- the PAD route: KeyboardMouseBackend's latch), with an
    // optional HOLD "<KEY>@<from>-<to>": the key is down from boundary <from>
    // through <to> (the KEYUP is pushed at <to>; while held the kbm backend's
    // injection overlay stands in for SDL's keyboard-state array, which a pushed
    // event cannot update). "" = off. `--set` only, harness armed (a window-less
    // leg has no window to press it in).
    std::string inject_key;
    // Harness-only WINDOW CLOSE (the user's red X, no finger; RootExitClean): at
    // frame boundary N (> 0) a play session pushes a real SDL_QUIT into SDL's
    // queue, drained by the main pump exactly as the window manager's close is
    // (SessionGui -> SessionControls::Close -> the shutdown ladder). 0 = off.
    // `--set` only, harness armed, a window required.
    int         close_at_frame = 0;
    // With close_at_frame: the SDL_QUIT is pushed by MAIN this many ms AFTER
    // boundary N instead of at it -- a close while the game thread is inside a
    // loader (no boundaries for seconds: the title's Quit reload). 0 = at N.
    int         close_after_ms = 0;
    // Harness-only VIRTUAL GAMEPAD (pin RootPadHotplug): "attach@N[,press@K-L][,detach@M]"
    // -- at boundary N a play session attaches an SDL virtual game controller (the
    // real JOYDEVICEADDED route into the pad backend), holds its cross from K through
    // L (real controller events), detaches it at M (JOYDEVICEREMOVED: the keyboard
    // takes over). "" = off. `--set` only, harness armed, a window required.
    std::string virtual_pad;
    // Harness-only SIMULATED DISPLAY (Charter 2 A3, RootHighFrameRate): under
    // video.render_rate=display the presenter's clock paces at this rate instead
    // of the window's display mode (0 = the real display's refresh). `--set`
    // only, harness armed.
    int         display_hz = 0;
    // Harness-only SCRIPTED DISPLAY CHANGES (Charter 3 item 4, RootScreenModeSwap):
    // at frame boundary N a play session submits the Display page's request for
    // <mode> exactly as the menu widget does (SessionControls), so the swap runs
    // on the presenter's thread between two presented frames while the game
    // thread keeps ticking. Format "<frame>:<mode>[,<frame>:<mode>...]", mode one
    // of windowed | fullscreen | borderless | scale1..scale4 | vsync | novsync,
    // frame > 0; "" = off. `--set` only, harness armed (a window-less leg has no
    // display to change).
    std::string display_sequence;
    // Harness-only LIVE PAD PROBE (Charter 3 item 4, RootScreenModeSwap): a play
    // session replaying `--inputs` ALSO opens the live PlatformInput and reads it
    // at every boundary exactly as a live session does (the pad's round trip to
    // the main thread -- the product path a display swap used to block), while
    // the served buttons stay the replay's (the route is unchanged). The read's
    // census is run.meta pad_live=. `--set` only, harness armed, a window
    // required (the live pad is the window's).
    bool        live_pad_probe = false;
    int         state_every = 0;
    int         warp_to_frame = 0;
};

// The user data layout (CHARTER 2 C4): settings, saves, savestates, texture
// packs and the extracted game files live under ONE user data root -- never
// beside the binary. The root is `user_data` when set, else the platform's
// (macOS ~/Library/Application Support/NativeEE, Windows %APPDATA%/NativeEE,
// Linux $XDG_DATA_HOME/NativeEE; the app hands it to ConfigLoader). An empty
// mc_dir / game_files / log_dir / texture_packs resolves to <root>/memcards,
// <root>/game_files, <root>/logs, <root>/textures at Load; with no root known
// (the engine tests) the former cwd-relative names stand. An explicit value
// (ini, --set, the CLI flags) is used as given.
struct PathsConfig {
    std::string user_data;                    // the root; "" = the platform's
    std::string iso;
    std::string mc_dir;                       // "" -> <root>/memcards
    std::string game_files;                   // the title's ELF / disc-side files; "" -> <root>/game_files
    std::string log_dir;                      // "" -> <root>/logs
    std::string texture_packs;                // textures/<pack>/<hash>.png overrides (A4); "" -> <root>/textures
    // Where F5 / --set harness.save_at_frame write <save_dir>/savestates/slot<N>.ps2st
    // (+ .meta) and F8 / --load-state read them; empty = the harness out dir,
    // or <log_dir> when no harness is armed.
    std::string save_dir;
};

struct MetaConfig {
    std::string profile = "default";
    std::string source_fingerprint;        // build id, stamped by the app
};

class Config {
public:
    VideoConfig   video;
    AudioConfig   audio;
    InputConfig   input;
    CdvdConfig    cdvd;
    EngineConfig  engine;
    TimeConfig    time;
    IopConfig     iop;
    GameConfig    game;
    GsConfig      gs;
    LoaderConfig  loader;
    HleConfig     hle;
    SaveConfig    save;
    UiConfig      ui;
    LauncherConfig launcher;
    QolConfig qol;
    FixesConfig fixes;
    DevConfig     dev;
    LogConfig     log;
    DiagConfig    diag;
    TriageConfig  triage;
    HarnessConfig harness;
    PathsConfig   paths;
    MetaConfig    meta;

    // Every "section.key" the schema exposes, in canonical (sorted) order.
    static const std::vector<std::string>& Keys();
    static bool IsKey(const std::string& key);

    // String view of one field ("on"/"off" for bools, enum names, decimal
    // numbers, raw strings). Returns false for an unknown key.
    bool Get(const std::string& key, std::string* out) const;
    // Parses and assigns one field; enum/bool/number parse failures and
    // unknown keys return false with *err set. No range validation here
    // (ConfigValidator owns the constraints).
    bool Set(const std::string& key, const std::string& value, std::string* err);

    // Canonical fingerprint: `key=value;` for every field whose value differs
    // from the peak default, in sorted key order, then `profile=<meta.profile>`.
    // Harness plumbing (harness.*), paths.* and meta.source_fingerprint are
    // excluded -- they legitimately differ per leg.
    std::string Fingerprint() const;
    // FNV-1a64 over Fingerprint() (run.meta env_hash semantics: each
    // `key=value` entry folded followed by ";").
    uint64_t FingerprintHash() const;
};

}  // namespace ps2r
