// AgentServer: the live control plane of the running recompiled game (the recomp-agent program's
// protocol: protocol/control-plane.schema.json in the recomp-agent repository). Newline-delimited JSON
// over AgentChannel (a UNIX socket; TCP 127.0.0.1 only on request). Title-neutral: the composition
// supplies what the ops read and write through Bindings (guest memory, the display grab, the draw list,
// the savestate owners, the knobs) and drives the server from two points of its game thread:
//
//   OnBoundary(frame)   the completed frame boundary (GameBoundary's consumer: the frame's lanes are
//                       hashed and its picture presented). The server finishes the run op whose
//                       condition holds here, answers queued requests, and -- paused -- WAITS here for
//                       the next run op. The game only advances between two OnBoundary calls.
//   ApplyPad(stamp, pad) the pad sample served at a boundary (pad.set's state, pad.press's hold on top).
//
// `frame` is the 0-based ordinal of the boundary just completed = the pad stamp served at it = the
// padlog clock. Paused at frame k, pad.set is first served at stamp k+1. When the socket is set the
// game pauses at boundary 0 (the first frame) and advances only on command; nothing paces it.
//
// Savestates: state.save captures on the completed boundary (the composition's SessionCapture) and
// waits for the writer. state.load is a LIVE restore: the composition prepares the image on this
// thread, OnBoundary returns false so the composition unwinds the boundary owner, the supervisor
// restores and relaunches, and the relaunched boundary owner calls OnRestored(frame) before its first
// instruction: the load is answered there and the server waits paused at the restored frame.
//
// Ops this build answers: session.info, run.step/resume/pause/until (frame | watch | max_frames),
// pad.set/press, state.save/load, screen.grab, gs.frame, vram.read, mem.read/write/search,
// mem.watch.add/remove (boundary-granular: the bytes are compared at every boundary, pc/fn null),
// cpu.regs, knobs.set. cpu.stack, fn.*, corpus.record, alloc.log, run.until fn_enter/fn_exit answer
// "not available in this build" until the trace hooks land.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/host/agent_channel.h"
#include "app/host/agent_json.h"

struct EmotionEngineState;

namespace ps2r::app {

class AgentServer {
public:
    struct Memory {
        uint8_t* ram = nullptr;
        uint32_t ram_size = 0;
        uint8_t* spad = nullptr;        // mapped at 0x70000000
        uint32_t spad_size = 0;
        const uint8_t* vram = nullptr;  // GS local memory (may be null: vram.read refused)
        uint32_t vram_size = 0;
    };
    struct Pad {
        uint16_t buttons = 0;           // libpad active-high order (SELECT 0x0001 ... SQUARE 0x8000)
        uint8_t lx = 0x80, ly = 0x80, rx = 0x80, ry = 0x80;
    };
    struct Grab {
        std::vector<uint8_t> rgba;      // top row first
        int w = 0, h = 0;
    };
    struct Bindings {
        std::string title, elf_sha256, build;
        Memory memory;
        // The last presented frame (false + reason when none yet).
        std::function<bool(Grab*, std::string*)> grab;
        // The draw list of the last presented frame as the schema's array (empty array when unknown).
        std::function<AgentJson()> draws;
        // On the completed boundary: capture to `path` (empty = slot) and wait for the file; *out_path names it.
        std::function<bool(const std::string& path, int slot, std::string* out_path, std::string* err)> state_save;
        // On the completed boundary: validate and prepare the live restore of `path` (empty = slot). True =
        // admitted: the caller unwinds the boundary owner and the supervisor restarts. *resolved names the file.
        std::function<bool(const std::string& path, int slot, std::string* resolved, std::string* err)> state_load_prepare;
        // A NATIVEEE_* knob at run time (false + reason: refused).
        std::function<bool(const std::string& name, const AgentJson& value, std::string* err)> knob_set;
        // Optional rows added to session.info.
        std::function<void(AgentJson*)> extra_info;
    };

    explicit AgentServer(Bindings bindings);
    ~AgentServer();
    AgentServer(const AgentServer&) = delete;
    AgentServer& operator=(const AgentServer&) = delete;

    bool Open(const std::string& socket_path, int tcp_port, std::string* error);
    void Close();

    // --- game thread -------------------------------------------------------------------------------
    // Slot 7 (Savestate) of the boundary sequence: the boundary owner's register file for cpu.regs.
    void NoteContext(const EmotionEngineState* ctx) { ctx_ = ctx; }
    // The completed boundary (see above). False: a state.load was admitted; unwind the boundary owner.
    bool OnBoundary(uint64_t frame);
    // The relaunched boundary owner, before its first instruction (see above).
    void OnRestored(uint64_t frame, const EmotionEngineState* ctx);
    // The supervisor: the admitted restore failed (the leg ends); answers the pending load.
    void RestoreFailed(const std::string& error);
    // Slot 4 (MaintDuties): overlay the agent's pad on the sample served at `stamp`.
    void ApplyPad(uint64_t stamp, Pad* io);
    bool LoadPending() const { return load_pending_; }

    // Census (run.meta agent=).
    std::string Census() const;
    static const char* const* Features(size_t* count);
    // The GS PRIM type's name for gs.frame ("triangle_strip", "sprite", ...).
    static const char* PrimName(uint8_t prim);

private:
    struct Request {
        uint64_t connection = 0;
        int64_t id = -1;
        bool has_id = false;
        std::string op;
        AgentJson body;
    };
    enum class RunKind { None, Step, Until, Press, Resume };
    struct Run {
        RunKind kind = RunKind::None;
        uint64_t connection = 0;
        int64_t id = 0;
        bool has_target = false;
        uint64_t target = 0;
        const char* target_reason = "step";   // the reply's reason when the target is reached
        uint64_t watch = 0;                   // run.until {watch}: stop when this watch fires (0 = none)
    };
    struct Watch {
        uint32_t addr = 0;
        uint32_t len = 0;
        std::string kind;
        std::vector<uint8_t> last;
    };
    struct Press {
        bool active = false;
        uint16_t buttons = 0;
        uint64_t first_stamp = 0, last_stamp = 0;   // held for stamps in [first, last]
    };

    // The boundary's work, shared by OnBoundary and OnRestored.
    void CheckWatches(uint64_t frame);
    bool DrainQueue(bool block);           // false: a load was admitted
    void FinishRun(const char* reason, const AgentJson* detail);
    void Reply(const Request& r, AgentJson body);
    void Fail(const Request& r, const std::string& error);
    void Event(AgentJson body);
    bool Handle(const Request& r);         // false: a load was admitted (OnBoundary returns false)
    bool StartRun(const Request& r, RunKind kind, bool has_target, uint64_t target, const char* target_reason, uint64_t watch);

    // Ops.
    void OpSessionInfo(const Request& r);
    void OpPadSet(const Request& r);
    void OpStateSave(const Request& r);
    bool OpStateLoad(const Request& r);
    void OpScreenGrab(const Request& r);
    void OpGsFrame(const Request& r);
    void OpVramRead(const Request& r);
    void OpMemRead(const Request& r);
    void OpMemWrite(const Request& r);
    void OpMemSearch(const Request& r);
    void OpWatchAdd(const Request& r);
    void OpWatchRemove(const Request& r);
    void OpCpuRegs(const Request& r);
    void OpKnobsSet(const Request& r);

    // Guest address -> host pointer over `len` bytes (nullptr when outside RAM / scratchpad).
    uint8_t* Resolve(uint32_t addr, uint32_t len) const;
    static bool ParseButtons(const AgentJson* arr, uint16_t* out, std::string* err);
    static bool GetU32(const AgentJson& body, const char* key, uint32_t* out, bool required, std::string* err);
    static std::string Base64Encode(const uint8_t* bytes, size_t n);
    static bool Base64Decode(const std::string& text, std::vector<uint8_t>* out);
    static std::string Hex(uint64_t v);

    Bindings b_;
    AgentChannel channel_;
    std::atomic<bool> open_{false};
    const EmotionEngineState* ctx_ = nullptr;
    uint64_t frame_ = 0;
    bool paused_ = true;
    bool started_ = false;
    Run run_;
    bool pause_requested_ = false;
    std::vector<Request> pause_requests_;   // run.pause received while a run op was in flight
    Press press_;
    Pad pad_;
    std::map<uint64_t, Watch> watches_;
    uint64_t next_watch_ = 1;
    const char* stop_reason_ = nullptr;     // set by CheckWatches for the run op's reply
    AgentJson stop_detail_;
    bool load_pending_ = false;
    Request load_request_;
    std::string load_path_;
    // Census.
    uint64_t requests_ = 0, replies_ = 0, errors_ = 0, events_ = 0, boundaries_ = 0, waits_ = 0, saves_ = 0, loads_ = 0;
};

}  // namespace ps2r::app
