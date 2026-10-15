#pragma once
// The GIF's two per-item dispatchers, shared by the parser and by the HLE kick
// lowering so a packet delivered directly produces exactly the parser's sink
// sequence for the same bytes.
//
// DispatchGifRegister: the one 64-bit GS register dispatcher -- a REGLIST half
// or a PACKED A+D pair. PRIM is masked to 11 bits, the four vertex registers
// become kicks (the 64-bit forms carry no ADC bit, so XYZ2/XYZF2 always draw),
// registers the GS applies are forwarded, everything else (reserved 0x0B, A+D
// 0x0E, NOP 0x0F, TEXCLUT 0x1C, undefined numbers) is dropped.
//
// DispatchPackedQuadword: one PACKED quadword under descriptor `reg` -- the
// decoded set (PRIM 11 bits; RGBAQ one byte per lane; ST raw S/T/Q; UV 14-bit;
// FOG; XYZF2/XYZ2 kicks drawing unless ADC, XYZF3/XYZ3 never drawing; NOP
// consumed), A+D (address in the high half, value in the low half, through
// DispatchGifRegister), and any other descriptor as that register's 64-bit
// value in the low half.
#include <cstdint>

class GsSink;

void DispatchGifRegister(GsSink& sink, uint8_t reg, uint64_t value);
void DispatchPackedQuadword(GsSink& sink, uint8_t reg, uint64_t lo, uint64_t hi);
