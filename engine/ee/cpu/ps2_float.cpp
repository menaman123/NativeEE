#include "engine/ee/cpu/ps2_float.h"

#include <cfenv>

float Ps2Float::Clamp(float v) { return ps2_float_clamp(v); }
float Ps2Float::DivS(float a, float b) { return ps2_div_s(a, b); }
float Ps2Float::SqrtS(float v) { return ps2_sqrt_s(v); }
float Ps2Float::RsqrtS(float a, float b) { return ps2_rsqrt_s(a, b); }

void Ps2Float::SetGuestThreadRounding() { fesetround(FE_TOWARDZERO); }
