// gs/hle/classrdr/class_registry.h -- ClassRegistry: one renderer slot per draw
// class, the dispatch that routes a draw to its slot, and the routing counters.
//
// Dispatch classifies, counts the draw against its class, and returns false --
// without touching the GPU -- whenever the draw is NOT natively rendered: no
// renderer in the slot (fb_norenderer), the slot disabled (fb_disabled), or the
// renderer's Accept declined (declined). The CALLER then runs its own generic
// path, so a fallback draw executes exactly as it would with no registry at all.
// A natively rendered draw occupies exactly the queue position its fallback form
// would: dispatch happens at the consumption point, one draw at a time, in order.
// Per-class enable is "a renderer is registered and the slot is enabled"; slots
// are enabled by default and disabled only by tests.
#pragma once

#include <cstdint>
#include <cstdio>

#include "engine/gs/hle/classrdr/class_classifier.h"
#include "engine/gs/hle/classrdr/class_renderer.h"

namespace ps2r {
namespace gs {

class DrawPipeline;

class ClassRegistry {
public:
    struct Counters {
        uint64_t seen[ClassClassifier::kClassCount] = {};
        uint64_t verts[ClassClassifier::kClassCount] = {};
        uint64_t native[ClassClassifier::kClassCount] = {};
        uint64_t fb_norenderer[ClassClassifier::kClassCount] = {};
        uint64_t fb_disabled[ClassClassifier::kClassCount] = {};
        uint64_t fb_declined[ClassClassifier::kClassCount] = {};
    };

    explicit ClassRegistry(const ClassClassifier::Rules& rules);

    // Returns the previous occupant (nullptr clears the slot).
    ClassRenderer* Register(uint8_t cls, ClassRenderer* r);
    ClassRenderer* Slot(uint8_t cls) const;
    void SetEnabled(uint8_t cls, bool on);
    bool Enabled(uint8_t cls) const;
    int RegisteredCount() const;

    // Route one draw; true when a renderer drew it, false when the caller must
    // run the generic path. `name_override` is the HLE program name.
    bool Dispatch(DrawPipeline& pipe, const DrawIr& ir, const char* name_override);
    // The class of the draw most recently dispatched (-1 outside a dispatch).
    int CurrentClass() const { return current_class_; }
    void ClearCurrentClass() { current_class_ = -1; }

    // Compile every registered, enabled renderer's programs; returns the total.
    int WarmUpAll(DrawPipeline& pipe, std::FILE* f);
    // The routing table in the corpus tool's grammar (one line per class seen +
    // a SUMMARY line).
    void EmitCounters(std::FILE* f) const;

    const Counters& Count() const { return cnt_; }
    void ResetCounters() { cnt_ = Counters{}; }
    const ClassClassifier::Rules& Rules() const { return rules_; }

private:
    const ClassClassifier::Rules& rules_;
    ClassRenderer* slots_[ClassClassifier::kClassCount] = {};
    bool enabled_[ClassClassifier::kClassCount];
    Counters cnt_;
    int current_class_ = -1;
};

}  // namespace gs
}  // namespace ps2r
