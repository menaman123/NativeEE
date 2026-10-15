// gs/hle/classrdr/class_classifier.h -- ClassClassifier: the draw taxonomy and the
// identity-stamp + state-shape decision tree that assigns every DrawIr a class.
//
// The ten class names are the census vocabulary of the corpus tools (the replay
// census, CENSUS_MASTER rows and the class-diff verdicts key on them), so the enum
// and its display names are the engine's taxonomy. Which HLE-program NAME
// substring maps to which class, and the raw-shape thresholds (the stencil
// composite's DATE / accumulator PSM / T8H self-read signature, the fullscreen
// blit extent), are title data supplied through Rules by the renderer profile.
//
// Decision order (binding): a stamped draw is classified by its program name
// (first matching stamp in table order; a stamped draw with no matching or no
// name is hle-other); else the three stencil-composite signatures; else a sprite
// is a fullscreen blit when textured and at least the blit extent, otherwise a
// HUD sprite; everything else is raw-other. The extent is the vertex bounding
// box, scanned only when the classifier can reach the sprite branch.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/gs/hle/ir/draw_ir.h"

namespace ps2r {
namespace gs {

class ClassClassifier {
public:
    enum Class : uint8_t {
        kWorldTristrip = 0,
        kSkinned,
        kParticlesUntex,
        kIndexedDisplace,
        kSpritesHud,
        kHleOther,
        kStencilComposite,
        kRawBlitFullscreen,
        kRawSpritesHud,
        kRawOther,
        kClassCount
    };

    struct Stamp {
        std::string substring;   // matched with strstr against the HLE program name
        Class cls = kHleOther;
    };

    struct Rules {
        std::vector<Stamp> stamps;      // in precedence order
        bool composite_on_date = false; // TEST.DATE draws are the stencil composite
        int composite_frame_psm = -1;   // FRAME.PSM of the composite accumulator (-1 = none)
        int composite_tex_psm = -1;     // TEX0.PSM of the composite self-read (-1 = none)
        float blit_min_w = 0.0f;        // fullscreen-blit extent threshold (inclusive)
        float blit_min_h = 0.0f;
    };

    static const char* ClassName(uint8_t c);

    static Class ClassifyFields(const Rules& rules, uint32_t hle_handler, const char* hle_name,
                                bool test_date, uint8_t frame_psm, bool tex_enabled, uint32_t tpsm,
                                uint8_t prim_type, float w, float h);
    // `name_override` supplies the program name when the record carries only the
    // handler id (the recording's name table).
    static Class Classify(const Rules& rules, const DrawIr& ir, const char* name_override);
};

}  // namespace gs
}  // namespace ps2r
