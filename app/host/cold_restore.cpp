#include "app/host/cold_restore.h"

#include <cstdio>
#include <mutex>

#include "app/host/savestate_slots.h"
#include "engine/core/savestate/savestate_format.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/savestate/state_restorer.h"

namespace ps2r::app {

namespace {

bool FileExists(const std::string& path) {
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fclose(f);
        return true;
    }
    return false;
}

}  // namespace

ColdRestore::ColdRestore(StateRestorer& restorer, ThreadSection& threads, ThreadManager& manager)
    : restorer_(restorer), threads_(threads), manager_(manager) {}

bool ColdRestore::Preflight(const std::string& path, const std::string& live_composition, const std::string& live_build_id,
                            std::string* err) {
    report_ = Report{};
    report_.path = path;
    savestate::FileMeta meta;
    if (!SavestateSlots::Preflight(path, restorer_.Registry(), err, &meta)) return false;
    report_.vsync_frame = meta.vsync_frame;
    // The sidecar: another composition is refused by name; a differing build is a
    // warning; no sidecar at all (a copied image) is a warning -- the registry
    // preflight above already proved every required owner has its section.
    if (FileExists(SavestateSlots::MetaPath(path))) {
        std::string warning;
        if (!SavestateSlots::PreflightComposition(path, live_composition, live_build_id, err, &warning)) return false;
        report_.warning = warning;
    } else {
        report_.warning = "no .meta sidecar beside the image (composition not checked)";
    }
    // The THRD section names exactly one boundary owner (the frame-boundary caller).
    std::vector<savestate::Blob> sections;
    if (!savestate::ReadStateFile(path, &meta, &sections, err)) return false;
    report_.boundary_tid = 0;
    for (const savestate::Blob& blob : sections) {
        if (blob.tag != savestate::TAG_THRD) continue;
        std::vector<ThreadSection::Record> records;
        if (!ThreadSection::Parse(blob.payload, records, err)) return false;
        for (const ThreadSection::Record& record : records) {
            if (!record.is_boundary) continue;
            if (report_.boundary_tid) {
                if (err) *err = "THRD: the image names several boundary owners";
                return false;
            }
            report_.boundary_tid = record.tid;
        }
    }
    if (!report_.boundary_tid) {
        if (err) *err = "THRD: the image names no boundary owner";
        return false;
    }
    preflighted_ = true;
    return true;
}

bool ColdRestore::Apply(std::string* err) {
    if (!preflighted_) {
        if (err) *err = "cold restore: Preflight first";
        return false;
    }
    // Before the first guest instruction: no started guest thread, and not on one.
    if (manager_.CurrentIncarnation() != 0) {
        if (err) *err = "cold restore: called on a guest thread (the cold route runs on the supervisor before the first instruction)";
        return false;
    }
    for (const ThreadManager::TableEntry& entry : manager_.SnapshotTable()) {
        if (!entry.second) continue;
        std::lock_guard<std::mutex> lock(entry.second->m);
        if (!entry.second->started) continue;
        if (err) *err = "cold restore: guest thread " + std::to_string(entry.first) + " is already started";
        return false;
    }
    if (!restorer_.RestoreFile(report_.path, err)) return false;
    report_.sections.clear();
    for (const savestate::Blob& blob : restorer_.LastSections())
        report_.sections += (report_.sections.empty() ? "" : ",") + savestate::TagName(blob.tag);
    report_.mhash = restorer_.RestoredMemoryHash();
    applied_ = true;
    return true;
}

int ColdRestore::Relaunch(const ThreadRelauncher::EnterFn& enter, std::string* err) {
    if (!applied_) {
        if (err) *err = "cold restore: Apply first";
        return -1;
    }
    relauncher_ = std::make_unique<ThreadRelauncher>(manager_, enter);
    const int count = relauncher_->Relaunch(threads_.Restored(), err);
    report_.relaunched = count;
    return count;
}

}  // namespace ps2r::app
