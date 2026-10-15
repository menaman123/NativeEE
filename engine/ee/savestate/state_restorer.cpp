#include "engine/ee/savestate/state_restorer.h"

#include <algorithm>
#include <cstdio>
#include <set>

#include "engine/ee/savestate/memory_sections.h"

using ps2r::savestate::Blob;
using ps2r::savestate::FileMeta;

StateRestorer::StateRestorer(MemorySections& memory) : memory_(memory) { memory_.Register(registry_); }

void StateRestorer::SetPreRestore(Step step) {
    pre_restore_ = std::move(step);
    ++policy_revision_;
}

void StateRestorer::AddImagePreflight(ImagePreflight check, std::function<void()> discard) {
    image_preflights_.emplace_back(std::move(check), std::move(discard));
    ++policy_revision_;
}

void StateRestorer::SetImageValidation(ImageValidation validate) {
    image_validation_ = std::move(validate);
    ++policy_revision_;
}

bool StateRestorer::PrepareImage(const uint8_t* data, size_t len, std::string* err) {
    if (HasPrepared()) {
        if (err) *err = "a prepared restore already owns the staged resources";
        return false;
    }
    if (!data || !len) {
        if (err) *err = "empty restore image";
        return false;
    }
    // Allocate the immutable retained bytes before any owner stages resources.
    std::vector<uint8_t> retained(data, data + len);
    FileMeta meta;
    std::vector<Blob> sections;
    // Parse the complete envelope before applying ownership policy. Partial
    // phase tools still inspect old images whose other owners are absent.
    // The full application enables RequireSectionOwners so filtering cannot
    // silently discard a selected service's only persistent state.
    if (!ps2r::savestate::ParseStateFile(data, len, &meta, &sections, err)) return false;
    std::set<uint32_t> tags;
    for (const auto& blob : sections) if (!tags.insert(blob.tag).second) {
        if (err) *err = "duplicate section: " + ps2r::savestate::TagName(blob.tag);
        return false;
    }
    if (image_validation_ && !image_validation_(meta, sections, err)) return false;
    if (require_owners_) {
        for (const Blob& b : sections) {
            if (!(b.flags & ps2r::savestate::kSectionRequired) || registry_.Has(b.tag)) continue;
            if (std::find(legacy_unowned_.begin(), legacy_unowned_.end(), b.tag) != legacy_unowned_.end())
                continue;
            if (err) *err = "required section has no active owner: " + ps2r::savestate::TagName(b.tag);
            return false;
        }
    }
    std::vector<Blob> owned;
    for (const Blob& b : sections)
        if (registry_.Has(b.tag)) owned.push_back(b);
    for (const auto& spec : registry_.Specs()) {
        bool present = false;
        for (const Blob& b : owned) present = present || b.tag == spec.tag;
        if (spec.required && !present) {
            if (err) *err = std::string("required section missing: ") + spec.owner;
            return false;
        }
    }
    // Every owned section is validated BEFORE the pre-restore step so a bad
    // file never reaches EmuDomain::ReAnchor / the thread teardown.
    if (!registry_.Validate(owned, err)) return false;
    try {
        for (const auto& stage : image_preflights_)
            if (stage.first && !stage.first(sections, err)) {
                DiscardPrepared();
                return false;
            }
    } catch (...) {
        DiscardPrepared();
        throw;
    }
    prepared_meta_ = meta;
    prepared_sections_ = std::move(sections);
    prepared_owned_ = std::move(owned);
    prepared_bytes_ = std::move(retained);
    prepared_policy_ = policy_revision_;
    prepared_registry_ = registry_.Revision();
    return true;
}

void StateRestorer::DiscardPrepared() {
    prepared_bytes_.clear();
    prepared_sections_.clear();
    prepared_owned_.clear();
    for (auto stage = image_preflights_.rbegin(); stage != image_preflights_.rend(); ++stage)
        if (stage->second) stage->second();
}

bool StateRestorer::RestorePrepared(const uint8_t* data, size_t len, std::string* err) {
    if (!HasPrepared() || !data || len != prepared_bytes_.size() ||
        !std::equal(prepared_bytes_.begin(), prepared_bytes_.end(), data) ||
        prepared_registry_ != registry_.Revision() || prepared_policy_ != policy_revision_) {
        if (err) *err = "prepared restore image or owner policy changed";
        return false;
    }
    // Consume before calling providers: a failed/throwing apply may never be
    // retried against a partially published graph. Staging does not run twice.
    auto owned = std::move(prepared_owned_);
    auto sections = std::move(prepared_sections_);
    const auto meta = prepared_meta_;
    prepared_bytes_.clear();
    try {
        if ((pre_restore_ && !pre_restore_(err)) || !registry_.Restore(owned, err)) {
            DiscardPrepared();
            return false;
        }
    } catch (...) {
        DiscardPrepared();
        throw;
    }
    DiscardPrepared();
    last_sections_ = std::move(sections);
    last_meta_ = meta;
    return true;
}

bool StateRestorer::RestoreImage(const uint8_t* data, size_t len, std::string* err) {
    return PrepareImage(data, len, err) && RestorePrepared(data, len, err);
}

bool StateRestorer::RestoreFile(const std::string& path, std::string* err) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { if (err) *err = "cannot open " + path; return false; }
    std::vector<uint8_t> image;
    uint8_t buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) image.insert(image.end(), buf, buf + n);
    std::fclose(f);
    return RestoreImage(image.data(), image.size(), err);
}

std::vector<uint8_t> StateRestorer::SaveImage(FileMeta meta) const {
    const std::vector<Blob> sections = registry_.Capture();
    meta.file_version = ps2r::savestate::SectionRegistry::FileVersionFor(sections);
    return ps2r::savestate::SerializeStateFile(meta, sections);
}

uint64_t StateRestorer::RestoredMemoryHash() const { return memory_.Hash(); }
