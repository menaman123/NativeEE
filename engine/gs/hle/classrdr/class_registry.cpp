// gs/hle/classrdr/class_registry.cpp -- dispatch, warm-up and the counter table.
#include "engine/gs/hle/classrdr/class_registry.h"

namespace ps2r {
namespace gs {

using Class = ClassClassifier;

ClassRegistry::ClassRegistry(const ClassClassifier::Rules& rules) : rules_(rules) {
    for (bool& e : enabled_) e = true;
}

ClassRenderer* ClassRegistry::Register(uint8_t cls, ClassRenderer* r) {
    if (cls >= Class::kClassCount) return nullptr;
    ClassRenderer* prev = slots_[cls];
    slots_[cls] = r;
    return prev;
}

ClassRenderer* ClassRegistry::Slot(uint8_t cls) const {
    return cls < Class::kClassCount ? slots_[cls] : nullptr;
}

void ClassRegistry::SetEnabled(uint8_t cls, bool on) {
    if (cls < Class::kClassCount) enabled_[cls] = on;
}

bool ClassRegistry::Enabled(uint8_t cls) const {
    return cls < Class::kClassCount && enabled_[cls];
}

int ClassRegistry::RegisteredCount() const {
    int n = 0;
    for (ClassRenderer* r : slots_) n += (r != nullptr);
    return n;
}

bool ClassRegistry::Dispatch(DrawPipeline& pipe, const DrawIr& ir, const char* name_override) {
    const Class::Class c = Class::Classify(rules_, ir, name_override);
    current_class_ = c;
    cnt_.seen[c]++;
    cnt_.verts[c] += ir.verts.size();
    ClassRenderer* r = slots_[c];
    if (!r) {
        cnt_.fb_norenderer[c]++;
        return false;
    }
    if (!enabled_[c]) {
        cnt_.fb_disabled[c]++;
        return false;
    }
    if (!r->Accept(ir)) {
        cnt_.fb_declined[c]++;
        return false;
    }
    r->Draw(pipe, ir);
    cnt_.native[c]++;
    return true;
}

int ClassRegistry::WarmUpAll(DrawPipeline& pipe, std::FILE* f) {
    int renderers = 0, programs = 0;
    for (int i = 0; i < Class::kClassCount; i++) {
        ClassRenderer* r = slots_[i];
        if (!r) continue;
        renderers++;
        if (enabled_[i]) programs += r->WarmUp(pipe);
    }
    if (f) std::fprintf(f, "[GS-CLASSRDR] warmup renderers=%d programs=%d\n", renderers, programs);
    return programs;
}

void ClassRegistry::EmitCounters(std::FILE* f) const {
    if (!f) return;
    uint64_t tseen = 0, tnative = 0, tfb = 0, tdecl = 0;
    for (int i = 0; i < Class::kClassCount; i++) {
        const uint64_t s = cnt_.seen[i];
        if (!s) continue;
        const uint64_t na = cnt_.native[i], fn = cnt_.fb_norenderer[i], fd = cnt_.fb_disabled[i],
                       dc = cnt_.fb_declined[i];
        tseen += s;
        tnative += na;
        tfb += fn + fd;
        tdecl += dc;
        std::fprintf(f,
                     "[GS-CLASSRDR] class=%s seen=%llu verts=%llu native=%llu"
                     " fb_norenderer=%llu fb_disabled=%llu declined=%llu\n",
                     Class::ClassName(static_cast<uint8_t>(i)), (unsigned long long)s,
                     (unsigned long long)cnt_.verts[i], (unsigned long long)na,
                     (unsigned long long)fn, (unsigned long long)fd, (unsigned long long)dc);
    }
    std::fprintf(f,
                 "[GS-CLASSRDR] SUMMARY on=1 renderers=%d seen=%llu native=%llu"
                 " fallback=%llu declined=%llu\n",
                 RegisteredCount(), (unsigned long long)tseen, (unsigned long long)tnative,
                 (unsigned long long)(tfb + tdecl), (unsigned long long)tdecl);
}

}  // namespace gs
}  // namespace ps2r
