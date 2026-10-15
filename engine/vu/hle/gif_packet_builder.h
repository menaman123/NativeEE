#pragma once
// PACKED-mode GIF packet builder: a GIFtag whose NLOOP is patched at EndTag
// from the quadwords written, then one quadword per register item in the
// GS PACKED layouts (PRIM low 11 bits; RGBAQ one byte per word; ST three
// floats; UV 14-bit fixed; XYZ2 x/y 16-bit, z 32-bit, ADC bit 15 of word 3;
// XYZF2 z in bits 4..27 of word 2, fog in bits 4..11 and ADC bit 15 of word
// 3; A+D value in the low 64 bits, address byte at offset 8). Generic GIF
// semantics; the tests and tools emit packets with it.
#include <cstddef>
#include <cstdint>
#include <vector>

class GifPacketBuilder {
public:
    // prim < 0 => PRE=0. `regs` = PACKED descriptors (16 encodes as NREG 0).
    void BeginTag(int prim, const std::vector<uint8_t>& regs, bool eop);
    void EndTag();

    void QW(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
    void PRIM(uint32_t prim);
    void RGBAQ(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void ST(float s, float t, float q);
    void UV(uint32_t u12_4, uint32_t v12_4);
    void XYZ2(uint32_t x12_4, uint32_t y12_4, uint32_t z, bool adc = false);
    void XYZF2(uint32_t x12_4, uint32_t y12_4, uint32_t z24, uint8_t fog, bool adc = false);
    void AD(uint8_t addr, uint64_t value);

    std::vector<uint8_t> Take();
    const std::vector<uint8_t>& Bytes() const { return buf_; }

private:
    std::vector<uint8_t> buf_;
    size_t tag_off_ = 0;
    uint32_t nreg_ = 1, qws_ = 0;
};
