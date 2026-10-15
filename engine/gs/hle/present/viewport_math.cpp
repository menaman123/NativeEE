// gs/hle/present/viewport_math.cpp -- fit / fill / integer / custom rect math.
#include "engine/gs/hle/present/viewport_math.h"

#include <algorithm>
#include <cmath>

namespace ps2r {
namespace gs {

int ViewportMath::ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

double ViewportMath::ComputeAutoAspect(int vck_w, int vck_h) {
    if (vck_w <= 0 || vck_h <= 0) return 0.0;
    return (4.0 / 3.0) * (static_cast<double>(vck_w) / 2560.0) / (static_cast<double>(vck_h) / 480.0);
}

ViewportMath::Rect ViewportMath::ComputeOutputRect(int W, int H, int Cw, int Ch, FitMode mode,
                                                   AspectSel asp, double auto_aspect,
                                                   int custom_scale_pct, int custom_off_x,
                                                   int custom_off_y) {
    if (W <= 0 || H <= 0) return {0, 0, 0, 0};
    if (mode == FitMode::Stretch) return {0, 0, W, H};
    if (mode == FitMode::Custom) {
        if (Cw <= 0 || Ch <= 0) return {0, 0, 0, 0};
        const int P = ClampInt(custom_scale_pct, 25, 400);
        const int ox = ClampInt(custom_off_x, -8192, 8192);
        const int oy = ClampInt(custom_off_y, -8192, 8192);
        const int w = static_cast<int>(std::llround(static_cast<double>(Cw) * P / 100.0));
        const int h = static_cast<int>(std::llround(static_cast<double>(Ch) * P / 100.0));
        const int x = static_cast<int>(std::floor((static_cast<double>(W) - w) / 2.0)) + ox;
        const int y = static_cast<int>(std::floor((static_cast<double>(H) - h) / 2.0)) - oy;
        return {x, y, w, h};
    }
    double A = 0.0;
    switch (asp) {
        case AspectSel::R4_3: A = 4.0 / 3.0; break;
        case AspectSel::R16_9: A = 16.0 / 9.0; break;
        case AspectSel::PAR1_1:
            A = (Cw > 0 && Ch > 0) ? static_cast<double>(Cw) / static_cast<double>(Ch) : 0.0;
            break;
        case AspectSel::Auto:
        default:
            A = auto_aspect;
            if (A <= 0.0 && Cw > 0 && Ch > 0) A = static_cast<double>(Cw) / static_cast<double>(Ch);
            break;
    }
    if (A <= 0.0) return {0, 0, W, H};
    const double Aw = static_cast<double>(W) / static_cast<double>(H);
    Rect r{0, 0, W, H};
    auto fit_dims = [&](int& w, int& h) {
        if (Aw >= A) { h = H; w = static_cast<int>(std::llround(static_cast<double>(H) * A)); }
        else         { w = W; h = static_cast<int>(std::llround(static_cast<double>(W) / A)); }
    };
    if (mode == FitMode::Fit) {
        fit_dims(r.w, r.h);
    } else if (mode == FitMode::Fill) {
        if (Aw >= A) { r.w = W; r.h = static_cast<int>(std::llround(static_cast<double>(W) / A)); }
        else         { r.h = H; r.w = static_cast<int>(std::llround(static_cast<double>(H) * A)); }
    } else {
        if (Cw <= 0 || Ch <= 0) return {0, 0, W, H};
        const long long Eh = Ch;
        const long long Ew = (asp == AspectSel::PAR1_1)
                                 ? static_cast<long long>(Cw)
                                 : static_cast<long long>(std::llround(static_cast<double>(Ch) * A));
        if (Ew <= 0) return {0, 0, W, H};
        const long long n = static_cast<long long>(std::floor(
            std::min(static_cast<double>(W) / static_cast<double>(Ew),
                     static_cast<double>(H) / static_cast<double>(Eh))));
        if (n >= 1) {
            r.w = static_cast<int>(n * Ew);
            r.h = static_cast<int>(n * Eh);
        } else {
            fit_dims(r.w, r.h);
        }
    }
    r.x = static_cast<int>(std::floor((static_cast<double>(W) - r.w) / 2.0));
    r.y = static_cast<int>(std::floor((static_cast<double>(H) - r.h) / 2.0));
    return r;
}

}  // namespace gs
}  // namespace ps2r
