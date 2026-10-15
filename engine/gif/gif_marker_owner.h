#pragma once
// The three questions the arbiter asks the MTVU worker when routing a submit:
// is the worker up, is the caller the worker thread, and which order marker
// is the worker currently filling. The worker implements this; null means the
// inline (single-thread) path. The interface keeps engine/gif free of any
// VU / VIF dependency.
#include <memory>

#include "engine/gif/gif_mtvu_marker.h"

class GifMarkerOwner {
public:
    virtual ~GifMarkerOwner() = default;
    // The worker thread is running (submits go to the inbox or a marker).
    virtual bool Running() const = 0;
    // The calling thread is the worker (its submits fill the active marker).
    virtual bool OnWorkerThread() const = 0;
    // The marker of the span the worker is replaying; null between spans.
    virtual std::shared_ptr<GifMtvuMarker> ActiveMarker() const = 0;
};
