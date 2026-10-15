// core/savestate/savestate_writer.h -- asynchronous state-file writer.
//
// Capturing a state is done by the emulation thread at a frame boundary; the
// 40+ MB file I/O is NOT: the caller moves the captured meta + sections into
// a one-slot job and returns, and a dedicated writer thread runs
// WriteStateFile (atomic tmp + rename). Contract:
//   * ONE in-flight state job (bounds the transient RSS); TryEnqueue returns
//     false without queuing while the slot is busy -- the caller drops that
//     capture and says so.
//   * sections are OWNED copies moved into the job before the caller resumes;
//     nothing points into live emulator state.
//   * the writer calls WriteStateFile with the caller-captured inputs, so an
//     async file is byte-identical to a synchronous one for the same inputs.
//   * JoinForExit() drains the slot and joins the thread; it must run before
//     process exit (the harness FrameCap exit path calls it).
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "engine/core/savestate/savestate_format.h"
#include "engine/core/threads/park.h"

namespace ps2r::savestate {

struct WriteJob {
    std::string       path;      // final .ps2st path
    FileMeta          meta;
    std::vector<Blob> sections;
};

struct WriteResult {
    std::string path;
    bool        ok = false;
    std::string error;
    uint64_t    write_ms = 0;
    uint64_t    bytes = 0;
};

class SavestateWriter {
public:
    SavestateWriter() = default;
    ~SavestateWriter();
    SavestateWriter(const SavestateWriter&) = delete;
    SavestateWriter& operator=(const SavestateWriter&) = delete;

    // False WITHOUT queuing when a write is in flight. Starts the writer
    // thread lazily on first use.
    bool TryEnqueue(WriteJob&& job);
    // True while the slot is occupied.
    bool Busy() const;
    // Block until the slot is drained (no-op when never started).
    void Flush();
    // Flush + stop + join. Idempotent; safe when never started.
    void JoinForExit();

    // Outcome of the most recent completed write (for logs / tests).
    WriteResult LastResult() const;
    uint64_t CompletedCount() const;

private:
    void WriterMain();

    mutable std::mutex mu_;
    Park               wake_;     // producer -> writer
    Park               idle_;     // writer -> waiters
    std::thread        thread_;
    bool               started_ = false;
    bool               stop_ = false;
    bool               pending_ = false;
    WriteJob           job_;
    WriteResult        last_;
    uint64_t           completed_ = 0;
};

}  // namespace ps2r::savestate
