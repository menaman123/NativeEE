// VuSavestateSections: the VU0C / VU0D / VU1C / VU1D memory sections
// (VuMemorySection) and the VU1R register section (Vu1RegisterSection) --
// round trip into fresh units byte-equal, the exact payload lengths, wrong
// lengths / sizes refused, required / optional semantics through the registry
// (memories REQUIRED, VU1R OPTIONAL), the micro-epoch bump + the VU1 callback
// on restore, and the frozen VU1R layout (running 0, size 1600, VUregs at 992).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"
#include "engine/vu/savestate/vu1_register_section.h"
#include "engine/vu/savestate/vu_memory_section.h"
#include "engine/vu/vu_unit.h"

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

void FillPattern(uint8_t* p, size_t n, uint32_t seed) {
    uint32_t x = seed;
    for (size_t i = 0; i < n; ++i) {
        x = x * 1664525u + 1013904223u;
        p[i] = static_cast<uint8_t>(x >> 24);
    }
}

ps2r::savestate::Blob MakeBlob(uint32_t tag, std::vector<uint8_t> payload) {
    ps2r::savestate::Blob b;
    b.tag = tag;
    b.payload = std::move(payload);
    return b;
}

}  // namespace

int main() {
    using namespace ps2r::savestate;
    std::printf("VuSavestateSections\n");

    // --- the memories: fill, capture through the registry, restore into fresh units ---
    {
        VuUnit vu0(0), vu1(1);
        FillPattern(vu0.state().micro, VuMemorySection::kVu0Bytes, 1);
        FillPattern(vu0.state().data, VuMemorySection::kVu0Bytes, 2);
        FillPattern(vu1.state().micro, VuMemorySection::kVu1Bytes, 3);
        FillPattern(vu1.state().data, VuMemorySection::kVu1Bytes, 4);
        VuMemorySection src(vu0, vu1);
        SectionRegistry reg;
        src.Register(reg);
        const std::vector<Blob> blobs = reg.Capture();
        Check(blobs.size() == 4 && blobs[0].tag == TAG_VU0C && blobs[1].tag == TAG_VU0D && blobs[2].tag == TAG_VU1C &&
                  blobs[3].tag == TAG_VU1D,
              "registration order VU0C VU0D VU1C VU1D");
        Check(blobs[0].payload.size() == 4096 && blobs[1].payload.size() == 4096 && blobs[2].payload.size() == 16384 &&
                  blobs[3].payload.size() == 16384,
              "payload lengths 4096 / 4096 / 16384 / 16384");
        Check((blobs[0].flags & kSectionRequired) && (blobs[3].flags & kSectionRequired), "memory sections REQUIRED");
        Check(std::memcmp(blobs[0].payload.data(), vu0.state().micro, 4096) == 0 &&
                  std::memcmp(blobs[3].payload.data(), vu1.state().data, 16384) == 0,
              "payloads are the raw memory bytes");

        VuUnit fresh0(0), fresh1(1);
        VuMemorySection dst(fresh0, fresh1);
        int vu1_callbacks = 0;
        dst.SetOnVu1Restored([&] { ++vu1_callbacks; });
        SectionRegistry reg2;
        dst.Register(reg2);
        const uint64_t gen0 = fresh0.micro_generation(), gen1 = fresh1.micro_generation();
        std::string err;
        Check(reg2.Restore(blobs, &err), ("registry restore ok: " + err).c_str());
        Check(std::memcmp(fresh0.state().micro, vu0.state().micro, 4096) == 0 &&
                  std::memcmp(fresh0.state().data, vu0.state().data, 4096) == 0 &&
                  std::memcmp(fresh1.state().micro, vu1.state().micro, 16384) == 0 &&
                  std::memcmp(fresh1.state().data, vu1.state().data, 16384) == 0,
              "restored memories byte-equal");
        Check(fresh0.micro_generation() == gen0 + 1 && fresh1.micro_generation() == gen1 + 1,
              "micro epoch bumped once per restored micro memory");
        Check(vu1_callbacks == 2 && dst.RestoredCount() == 4, "VU1 callback ran for VU1C and VU1D; 4 restored");
        const std::vector<Blob> again = reg2.Capture();
        bool same = again.size() == 4;
        for (size_t i = 0; same && i < 4; ++i) same = again[i].payload == blobs[i].payload;
        Check(same, "re-serialize byte-identical");

        // Wrong lengths refused by validate; missing REQUIRED section refused by the registry.
        Check(!VuMemorySection::Validate(TAG_VU0C, std::vector<uint8_t>(4095), &err), "VU0C 4095 bytes refused");
        Check(!VuMemorySection::Validate(TAG_VU1D, std::vector<uint8_t>(16385), &err), "VU1D 16385 bytes refused");
        Check(!VuMemorySection::Validate(TAG_VU1R, std::vector<uint8_t>(16384), &err), "foreign tag refused");
        std::vector<Blob> missing(blobs.begin(), blobs.begin() + 3);
        Check(!reg2.Validate(missing, &err) && err.find("VU1D") != std::string::npos,
              ("missing VU1D is a load error: " + err).c_str());
        std::vector<Blob> bad = blobs;
        bad[2].payload.resize(100);
        Check(!reg2.Validate(bad, &err) && err.find("VU1C") != std::string::npos,
              ("mis-sized VU1C is a load error: " + err).c_str());
    }

    // --- VU1R: layout, round trip, optional semantics ---
    {
        VuUnit vu1(1);
        VUregs& r = vu1.regs();
        FillPattern(reinterpret_cast<uint8_t*>(&r), sizeof(VUregs), 77);
        Vu1RegisterSection src(vu1);
        std::vector<uint8_t> payload;
        src.Serialize(payload);
        Check(payload.size() == Vu1RegisterSection::kPayloadBytes && payload.size() == 1605, "VU1R payload is 1605 bytes");
        Check(payload[0] == 0, "running = 0 under the deterministic profile");
        uint32_t size = 0;
        std::memcpy(&size, payload.data() + 1, 4);
        Check(size == 1600, "size word = 1600");
        Check(std::memcmp(payload.data() + 5 + 992, &r, 608) == 0, "VUregs block at image offset 992");
        bool zero_pad = true;
        for (size_t i = 5; i < 5 + 992; ++i) zero_pad = zero_pad && payload[i] == 0;
        Check(zero_pad, "EE-context padding before the block is zero");

        VuUnit fresh(1);
        Vu1RegisterSection dst(fresh);
        std::string err;
        Check(dst.Restore(payload, &err), "VU1R restore ok");
        Check(std::memcmp(&fresh.regs(), &r, sizeof(VUregs)) == 0, "restored VUregs byte-equal");
        std::vector<uint8_t> again;
        dst.Serialize(again);
        Check(again == payload, "VU1R re-serialize byte-identical");
        Check(dst.RestoredCount() == 1 && dst.RestoredRunningCount() == 0, "counters: 1 restored, 0 running");

        // running = 1 (the reference's MTVU worker) restores the same block and is counted.
        std::vector<uint8_t> running = payload;
        running[0] = 1;
        VuUnit fresh2(1);
        Vu1RegisterSection dst2(fresh2);
        Check(dst2.Restore(running, &err) && std::memcmp(&fresh2.regs(), &r, sizeof(VUregs)) == 0 &&
                  dst2.RestoredRunningCount() == 1,
              "running = 1 restores the block; counted");

        // Refusals: length, size word.
        Check(!Vu1RegisterSection::Validate(std::vector<uint8_t>(1604), &err), "1604 bytes refused");
        std::vector<uint8_t> bad_size = payload;
        bad_size[1] = 0x3F;  // size 1599
        Check(!Vu1RegisterSection::Validate(bad_size, &err) && err.find("1599") != std::string::npos,
              ("size != 1600 refused: " + err).c_str());

        // Optional: a file without VU1R restores (and leaves the live registers).
        SectionRegistry reg;
        VuUnit fresh3(1);
        FillPattern(reinterpret_cast<uint8_t*>(&fresh3.regs()), sizeof(VUregs), 5);
        VUregs before = fresh3.regs();
        Vu1RegisterSection opt(fresh3);
        opt.Register(reg);
        const std::vector<Blob> cap = reg.Capture();
        Check(cap.size() == 1 && cap[0].tag == TAG_VU1R && (cap[0].flags & kSectionRequired) == 0, "VU1R is OPTIONAL");
        std::vector<Blob> none;
        Check(reg.Restore(none, &err) && std::memcmp(&fresh3.regs(), &before, sizeof(VUregs)) == 0,
              "absent VU1R: restore ok, registers untouched");
        std::vector<Blob> one{MakeBlob(TAG_VU1R, payload)};
        Check(reg.Restore(one, &err) && std::memcmp(&fresh3.regs(), &r, sizeof(VUregs)) == 0,
              "present VU1R through the registry restores the block");
    }

    std::printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
