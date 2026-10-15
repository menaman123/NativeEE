// core/savestate/section_registry.cpp
#include "engine/core/savestate/section_registry.h"

#include <set>
#include <utility>

namespace ps2r::savestate {

bool SectionRegistry::Register(const SectionSpec& spec) {
    if (Has(spec.tag)) return false;
    specs_.push_back(spec);
    ++revision_;
    return true;
}

bool SectionRegistry::RegisterValidation(std::string owner, ImageValidation validate) {
    if (owner.empty() || !validate) return false;
    for (const auto& entry : validations_) if (entry.first == owner) return false;
    validations_.emplace_back(std::move(owner), std::move(validate));
    ++revision_;
    return true;
}

bool SectionRegistry::Has(uint32_t tag) const {
    for (const SectionSpec& s : specs_)
        if (s.tag == tag) return true;
    return false;
}

std::vector<uint32_t> SectionRegistry::KnownTags() const {
    std::vector<uint32_t> tags;
    tags.reserve(specs_.size());
    for (const SectionSpec& s : specs_) tags.push_back(s.tag);
    return tags;
}

std::vector<Blob> SectionRegistry::Capture() const {
    std::vector<Blob> out;
    out.reserve(specs_.size());
    for (const SectionSpec& s : specs_) {
        Blob b;
        b.tag = s.tag;
        b.version = s.version;
        b.flags = (s.required || s.capture_required) ? kSectionRequired : 0;
        if (s.serialize) s.serialize(b.payload);
        out.push_back(std::move(b));
    }
    return out;
}

uint32_t SectionRegistry::FileVersionFor(const std::vector<Blob>& sections) {
    uint32_t v = kFileVersion;
    for (const Blob& b : sections) {
        if (b.tag == TAG_TIMR && v < kFileVersionTimr) v = kFileVersionTimr;
        if (b.tag == TAG_SNDC && v < kFileVersionSnd) v = kFileVersionSnd;
    }
    return v;
}

bool SectionRegistry::Validate(const std::vector<Blob>& sections, std::string* err) const {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    // Pass 1: every REQUIRED unknown tag and every missing required spec is
    // an error before any state is touched.
    std::set<uint32_t> tags;
    for (const Blob& blob : sections)
        if (!tags.insert(blob.tag).second) return fail("duplicate section '" + TagName(blob.tag) + "'");
    for (const Blob& b : sections)
        if ((b.flags & kSectionRequired) && !Has(b.tag))
            return fail("unknown REQUIRED section '" + TagName(b.tag) + "' (state file from a newer build)");
    for (const SectionSpec& s : specs_) {
        if (!s.required) continue;
        bool present = false;
        for (const Blob& b : sections)
            if (b.tag == s.tag) { present = true; break; }
        if (!present) return fail("missing required section '" + TagName(s.tag) + "' (" + s.owner + ")");
    }
    // Pass 2: validate all, then restore all (registration order).
    for (const SectionSpec& s : specs_) {
        for (const Blob& b : sections) {
            if (b.tag != s.tag || !s.validate) continue;
            std::string e;
            if (!s.validate(b, &e)) return fail("section '" + TagName(b.tag) + "' rejected by " + s.owner + ": " + e);
        }
    }
    for (const auto& entry : validations_) {
        std::string error;
        if (!entry.second(sections, &error))
            return fail("checkpoint rejected by " + entry.first + ": " + error);
    }
    return true;
}

bool SectionRegistry::Restore(const std::vector<Blob>& sections, std::string* err) const {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    if (!Validate(sections, err)) return false;
    for (const SectionSpec& s : specs_) {
        bool present = false;
        for (const Blob& b : sections) {
            if (b.tag != s.tag) continue;
            present = true;
            if (!s.restore) continue;
            std::string e;
            if (!s.restore(b, &e)) return fail("section '" + TagName(b.tag) + "' restore failed in " + s.owner + ": " + e);
        }
        if (!present && !s.required && s.absent) s.absent();
    }
    return true;
}

}  // namespace ps2r::savestate
