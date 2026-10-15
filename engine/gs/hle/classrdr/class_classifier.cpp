// gs/hle/classrdr/class_classifier.cpp -- the census decision tree.
#include "engine/gs/hle/classrdr/class_classifier.h"

#include <cstring>

namespace ps2r {
namespace gs {

const char* ClassClassifier::ClassName(uint8_t c) {
    switch (c) {
        case kWorldTristrip:     return "world-tristrip";
        case kSkinned:           return "skinned";
        case kParticlesUntex:    return "particles-untex";
        case kIndexedDisplace:   return "indexed-displace";
        case kSpritesHud:        return "sprites-HUD";
        case kHleOther:          return "hle-other";
        case kStencilComposite:  return "S9-shadow";
        case kRawBlitFullscreen: return "raw-blit-fullscreen";
        case kRawSpritesHud:     return "raw-sprites-HUD";
        case kRawOther:          return "raw-other";
        default:                 return "?";
    }
}

ClassClassifier::Class ClassClassifier::ClassifyFields(const Rules& rules, uint32_t hle_handler,
                                                       const char* hle_name, bool test_date,
                                                       uint8_t frame_psm, bool tex_enabled,
                                                       uint32_t tpsm, uint8_t prim_type, float w,
                                                       float h) {
    if (hle_handler) {
        if (hle_name)
            for (const Stamp& s : rules.stamps)
                if (std::strstr(hle_name, s.substring.c_str())) return s.cls;
        return kHleOther;
    }
    if (rules.composite_on_date && test_date) return kStencilComposite;
    if (rules.composite_frame_psm >= 0 && frame_psm == rules.composite_frame_psm)
        return kStencilComposite;
    if (rules.composite_tex_psm >= 0 && tex_enabled &&
        tpsm == static_cast<uint32_t>(rules.composite_tex_psm))
        return kStencilComposite;
    if ((prim_type & 7) == 6) {
        const bool big = (w >= rules.blit_min_w && h >= rules.blit_min_h);
        if (tex_enabled && big) return kRawBlitFullscreen;
        return kRawSpritesHud;
    }
    return kRawOther;
}

ClassClassifier::Class ClassClassifier::Classify(const Rules& rules, const DrawIr& ir,
                                                 const char* name_override) {
    float w = 0.0f, h = 0.0f;
    if (ir.identity.hle_handler == 0 && (ir.prim_type & 7) == 6 && !ir.verts.empty()) {
        float minx = ir.verts[0].x, maxx = minx, miny = ir.verts[0].y, maxy = miny;
        for (const VertexRec& v : ir.verts) {
            if (v.x < minx) minx = v.x;
            if (v.x > maxx) maxx = v.x;
            if (v.y < miny) miny = v.y;
            if (v.y > maxy) maxy = v.y;
        }
        w = maxx - minx;
        h = maxy - miny;
    }
    return ClassifyFields(rules, ir.identity.hle_handler, name_override, ir.raster.test.date,
                          ir.raster.frame.psm, ir.tex_enabled, ir.tex.psm, ir.prim_type, w, h);
}

}  // namespace gs
}  // namespace ps2r
