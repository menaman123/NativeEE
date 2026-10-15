// lib_vu0_sin_cos.inl -- LibVu0SinCos::SineCosine, the ONE host sin/cos (rules
// in lib_vu0_sin_cos.h). Included from lib_vu0_sin_cos.h only.
#include <cmath>

inline void LibVu0SinCos::SineCosine(float x, float& sine, float& cosine) {
    double xd = static_cast<double>(x);
    if (std::fabs(xd) >= kLargeArgument) xd = std::fmod(xd, kTwoPi);  // exact; |xd| < 2*pi
    // k = the integer nearest x * 2/pi (floor is rounding-mode independent).
    const double k = std::floor(std::fma(xd, kTwoOverPi, 0.5));
    double r = std::fma(-k, kPio2Part1, xd);
    r = std::fma(-k, kPio2Part2, r);
    r = std::fma(-k, kPio2Part3, r);
    const double r2 = r * r;
    // sin r = r + r^3 (S1 + r2 (S2 + ... S7)); cos r = (1 - r2/2) + r2^2 (C2 + r2 (C3 + ... C8)).
    double s = kSin[6];
    for (int i = 5; i >= 0; --i) s = std::fma(s, r2, kSin[i]);
    const double r3 = r * r2;
    s = std::fma(r3, s, r);
    if (s == 0.0) s = r;  // sin(+-0) keeps the argument's sign ((-0 * S) + -0 is +0 in IEEE)
    double c = kCos[6];
    for (int i = 5; i >= 0; --i) c = std::fma(c, r2, kCos[i]);
    const double r4 = r2 * r2;
    c = std::fma(r4, c, std::fma(-0.5, r2, 1.0));
    // The quadrant: fmod is exact, so k of any magnitude lands in -3..3.
    int q = static_cast<int>(std::fmod(k, 4.0));
    if (q < 0) q += 4;
    double sine_d, cosine_d;
    switch (q) {
        case 0: sine_d = s; cosine_d = c; break;
        case 1: sine_d = c; cosine_d = -s; break;
        case 2: sine_d = -s; cosine_d = -c; break;
        default: sine_d = -c; cosine_d = s; break;
    }
    sine = static_cast<float>(sine_d);
    cosine = static_cast<float>(cosine_d);
}
