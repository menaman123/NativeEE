#include "engine/dmac/dmac_mtvu_feed.h"

#include "engine/vu/tiers/mtvu_worker.h"

DmacMtvuFeed::DmacMtvuFeed(ChainWalker& walker, MtvuWorker& worker) : walker_(walker), worker_(worker) {
    Install();
}

DmacMtvuFeed::~DmacMtvuFeed() { Uninstall(); }

void DmacMtvuFeed::Install() {
    if (installed_) return;
    walker_.SetVif1StagedFeed(&MtvuWorker::StagedFeedThunk, &MtvuWorker::StagedFlushThunk,
                              &MtvuWorker::StagedResetThunk, &worker_);
    installed_ = true;
}

void DmacMtvuFeed::Uninstall() {
    if (!installed_) return;
    walker_.SetVif1StagedFeed(nullptr, nullptr, nullptr, nullptr);
    installed_ = false;
}
