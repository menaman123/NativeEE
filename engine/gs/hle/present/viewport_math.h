// gs/hle/present/viewport_math.h -- ViewportMath: the window rectangle the
// composed scanout is drawn into for a fit mode and aspect selection. Pure
// integer / double math over the window and content sizes; user-facing only.
#pragma once

namespace ps2r {
namespace gs {

class ViewportMath {
public:
    struct Rect {
        int x = 0, y = 0, w = 0, h = 0;
        bool operator==(const Rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
        bool operator!=(const Rect& o) const { return !(*this == o); }
    };
    enum class FitMode : int { Stretch = 0, Fit = 1, Fill = 2, Integer = 3, Custom = 4 };
    enum class AspectSel : int { Auto = 0, R4_3 = 1, R16_9 = 2, PAR1_1 = 3 };

    static int ClampInt(int v, int lo, int hi);
    // The display aspect the CRTC implies from the DISPLAY width in VCK units and
    // the height in lines (2560 VCK x 480 lines is 4:3).
    static double ComputeAutoAspect(int vck_w, int vck_h);
    // Output rect inside a W x H window for content Cw x Ch.
    static Rect ComputeOutputRect(int W, int H, int Cw, int Ch, FitMode mode, AspectSel asp,
                                  double auto_aspect, int custom_scale_pct, int custom_off_x,
                                  int custom_off_y);
};

}  // namespace gs
}  // namespace ps2r
