#pragma once
// Generated-code ABI forwarder (row 3/4): `QuadWord` and the `memory::`
// free functions the generated loads/stores call, routed to the bound FastMem.
//
// Two forms of every accessor:
//   memory::read<T>(addr) ...                 -- the classic form: RecompAbi::Memory()
//                                                (one global pointer load) then FastMem's
//                                                inline fast path (its cached bases). Used
//                                                by hand-written HLE bodies and tools.
//   memory::read<T>(const Window&, addr) ...  -- the generated bodies' form (--mem-window):
//                                                the two inline bases are read
//                                                ONCE per body into a local `Window` the
//                                                recompiler emits in the prologue
//                                                (`memory::window()`), so a guest store --
//                                                a char-typed store the compiler must
//                                                otherwise assume may alias the pointer
//                                                globals -- forces no reload of them.
//                                                Bit-identical to the classic form by
//                                                construction: the same arms in the same
//                                                order, the slow path is FastMem's own.
// PS2R_RECOMP_RESTRICT marks the generated bodies' context parameter: guest RAM and the
// scratchpad never overlap a CpuContext, so a guest store cannot change a guest register
// and the compiler may keep register values in host registers across it. Every other
// path that can change the running context (a syscall, an interrupt dispatch, a yield)
// is a call the body passes `ctx` to, which the compiler treats as a clobber.
#include <cstdint>
#include <cstring>

#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/quad_word.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/recomp_abi/recomp_abi.h"

#if defined(_MSC_VER) && !defined(__clang__)
#define PS2R_RECOMP_RESTRICT __restrict
#else
#define PS2R_RECOMP_RESTRICT __restrict__
#endif

#ifdef PS2R_TRACE_ACCESS
// The tracing build (CMake PS2R_TRACE_ACCESS=ON): every generated load and
// store is reported to AccessTracer after it is served, so the per-call corpus
// records the bytes as the game saw them. Off: these are the plain
// forwarders below and AccessTracer is not referenced.
#include "engine/ee/cpu/access_tracer.h"
#endif

namespace memory {

// The inline arms' host bases (FastMem::RamBase / SpadBase), by value: a local
// whose address never escapes, so no store through another pointer can change it.
struct Window {
    uint8_t* ram;
    uint8_t* spad;
};

inline Window window() {
    FastMem& m = RecompAbi::Memory();
    return Window{m.RamBase(), m.SpadBase()};
}

#ifdef PS2R_TRACE_ACCESS
template <typename T>
inline T read(uint32_t address) {
    T value = RecompAbi::Memory().Read<T>(address);
    AccessTracer::OnRead(address, &value, static_cast<uint32_t>(sizeof(T)));
    return value;
}

template <typename T>
inline void write(uint32_t address, T value) {
    RecompAbi::Memory().Write<T>(address, value);
    AccessTracer::OnWrite(address, &value, static_cast<uint32_t>(sizeof(T)));
}

inline void read_quad(uint32_t address, QuadWord& value) {
    RecompAbi::Memory().ReadQuad(address, value);
    AccessTracer::OnRead(address & ~0xFu, &value, 16);
}

inline void write_quad(uint32_t address, const QuadWord& value) {
    RecompAbi::Memory().WriteQuad(address, value);
    AccessTracer::OnWrite(address & ~0xFu, &value, 16);
}

// The tracing build keeps one path: the Window form is the classic form.
template <typename T>
inline T read(const Window&, uint32_t address) { return read<T>(address); }
template <typename T>
inline void write(const Window&, uint32_t address, T value) { write<T>(address, value); }
inline void read_quad(const Window&, uint32_t address, QuadWord& value) { read_quad(address, value); }
inline void write_quad(const Window&, uint32_t address, const QuadWord& value) { write_quad(address, value); }
#else
template <typename T>
inline T read(uint32_t address) {
    return RecompAbi::Memory().Read<T>(address);
}

template <typename T>
inline void write(uint32_t address, T value) {
    RecompAbi::Memory().Write<T>(address, value);
}

inline void read_quad(uint32_t address, QuadWord& value) {
    RecompAbi::Memory().ReadQuad(address, value);
}

inline void write_quad(uint32_t address, const QuadWord& value) {
    RecompAbi::Memory().WriteQuad(address, value);
}

// The Window form: FastMem::Read/Write/ReadQuad/WriteQuad's arms over the hoisted bases.
template <typename T>
inline T read(const Window& w, uint32_t address) {
    const uint32_t phys = MainMemory::Phys(address);
    if (static_cast<uint64_t>(phys) + sizeof(T) <= MainMemory::kSize) {
        T value;
        std::memcpy(&value, w.ram + phys, sizeof(T));
        return value;
    }
    if (Scratchpad::Contains(address)) {
        const uint32_t off = Scratchpad::Offset(address);
        if (static_cast<uint64_t>(off) + sizeof(T) <= Scratchpad::kSize) {
            T value;
            std::memcpy(&value, w.spad + off, sizeof(T));
            return value;
        }
    }
    return RecompAbi::Memory().ReadSlow<T>(address);
}

template <typename T>
inline void write(const Window& w, uint32_t address, T value) {
    const uint32_t phys = MainMemory::Phys(address);
    if (static_cast<uint64_t>(phys) + sizeof(T) <= MainMemory::kSize) {
#ifndef NDEBUG
        if (FastMem::HasWriteObserver()) { RecompAbi::Memory().Write<T>(address, value); return; }
#endif
        std::memcpy(w.ram + phys, &value, sizeof(T));
        return;
    }
    if (Scratchpad::Contains(address)) {
        const uint32_t off = Scratchpad::Offset(address);
        if (static_cast<uint64_t>(off) + sizeof(T) <= Scratchpad::kSize) {
            std::memcpy(w.spad + off, &value, sizeof(T));
            return;
        }
    }
    RecompAbi::Memory().WriteSlow<T>(address, value);
}

inline void read_quad(const Window& w, uint32_t address, QuadWord& value) {
    const uint32_t aligned = address & ~0xFu;
    const uint32_t phys = MainMemory::Phys(aligned);
    if (static_cast<uint64_t>(phys) + sizeof(QuadWord) <= MainMemory::kSize) {
        std::memcpy(&value, w.ram + phys, sizeof(QuadWord));
        return;
    }
    if (Scratchpad::Contains(aligned)) {
        std::memcpy(&value, w.spad + Scratchpad::Offset(aligned), sizeof(QuadWord));
        return;
    }
    RecompAbi::Memory().ReadQuadSlow(address, value);
}

inline void write_quad(const Window& w, uint32_t address, const QuadWord& value) {
    const uint32_t aligned = address & ~0xFu;
    const uint32_t phys = MainMemory::Phys(aligned);
    if (static_cast<uint64_t>(phys) + sizeof(QuadWord) <= MainMemory::kSize) {
#ifndef NDEBUG
        if (FastMem::HasWriteObserver()) { RecompAbi::Memory().WriteQuad(address, value); return; }
#endif
        std::memcpy(w.ram + phys, &value, sizeof(QuadWord));
        return;
    }
    if (Scratchpad::Contains(aligned)) {
        std::memcpy(w.spad + Scratchpad::Offset(aligned), &value, sizeof(QuadWord));
        return;
    }
    RecompAbi::Memory().WriteQuadSlow(address, value);
}
#endif

// Host pointer to `size` bytes of main RAM at a KUSEG/KSEG0/KSEG1/UCAB address
// (block copies: disc sectors landing in EE RAM). Null when the range leaves
// main RAM -- MMIO and the scratchpad have no linear host image here.
inline uint8_t* get_pointer(uint32_t address, size_t size) {
    return RecompAbi::Memory().Ram().Pointer(MainMemory::Phys(address), size);
}

}  // namespace memory
