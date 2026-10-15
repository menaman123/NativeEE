#pragma once
// Routes the DMAC's VIF1 walks to the MTVU worker: every span and TTE
// quadword of a walk is staged into one Batch segment (SubmitVif1Staged),
// flushed at the walk exit (FlushVif1Staged), and the walk-head latch reset
// becomes a ResetLatch segment -- so the worker replays the walk exactly as
// the inline feed would have, one arbiter marker per walk. Constructing the
// object installs the seam on the walker; destroying it (or Uninstall) puts
// the inline Vif feed back. With MTVU off nothing is constructed and the
// walker feeds Vif directly (the harness path).
//
// Cross-thread contract: install / uninstall on the EE thread with no walk
// in progress; the thunks run on the DMAC (EE) thread, the worker's producer.
#include "engine/dmac/chain_walker.h"

class MtvuWorker;

class DmacMtvuFeed {
public:
    DmacMtvuFeed(ChainWalker& walker, MtvuWorker& worker);
    ~DmacMtvuFeed();
    DmacMtvuFeed(const DmacMtvuFeed&) = delete;
    DmacMtvuFeed& operator=(const DmacMtvuFeed&) = delete;

    void Install();
    void Uninstall();
    bool Installed() const { return installed_; }

private:
    ChainWalker& walker_;
    MtvuWorker& worker_;
    bool installed_ = false;
};
