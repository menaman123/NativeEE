#pragma once
// AccessTracer: the tracing build's per-call read/write-set recorder (CMake
// PS2R_TRACE_ACCESS=ON). The generated code's memory accessors (the
// recomp_abi/host_app/memory.h forwarders) report every load and store to
// OnRead / OnWrite; a probed call (FnHooks, every id armed with kCorpus while a
// recording runs) opens a record frame on entry and closes it on exit. An
// access belongs to the INNERMOST probed call (the function on top of the
// probe stack, which mirrors the shadow stack for generated code); when that
// call is outside the recording scope the access is dropped, never credited to
// an ancestor. A record keeps each byte READ with the value first seen and
// each byte WRITTEN with its final value, the arguments (FnHooks::kArgs), v0/v1,
// sp, ra, the caller and the sequence of callees (real calls only: interior
// re-entries are not calls). Records are deduplicated into a CorpusWriter and
// written as RACORP1 at Stop.
//
// Not a tracing build (the option off): Start reports false, OnRead / OnWrite
// are not compiled into the accessors, nothing else exists.
#include <cstdint>
#include <string>
#include <vector>

class AccessTracer {
public:
    struct Stats {
        uint64_t calls = 0;        // calls recorded (sum of multiplicities)
        uint64_t distinct = 0;     // distinct records
        uint64_t dropped = 0;      // calls refused by the budget
        uint64_t accesses = 0;     // reads + writes attributed
        uint64_t unattributed = 0; // accesses outside any recording frame
        uint64_t bytes = 0;        // serialized bytes held
        bool truncated = false;
        uint32_t first_frame = 0, last_frame = 0;
    };

    static bool Available();   // compiled as a tracing build
    static bool Active();

    // Starts recording into `path` (written at Stop). `functions` empty = all.
    // `meta_json` = extra header fields as `"key": value` pairs (may be empty).
    static bool Start(const std::string& path, const std::vector<uint32_t>& functions,
                      const std::vector<std::pair<std::string, std::string>>& meta_json,
                      uint64_t max_distinct, std::string* error);
    // Closes the recording and writes the file. False with *error.
    static bool Stop(std::string* error);
    static Stats CurrentStats();

    // The accessor seams (hot in the tracing build).
    static void OnRead(uint32_t addr, const void* bytes, uint32_t n);
    static void OnWrite(uint32_t addr, const void* bytes, uint32_t n);
};
