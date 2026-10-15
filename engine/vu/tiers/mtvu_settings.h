#pragma once
// MTVU settings, module-local with the PEAK defaults. Mirrors the existing
// Config keys `engine.mtvu` (enabled) and `engine.mtvu_depth` (depth_frames);
// the composition root copies Config into this struct -- nothing here reads
// Config. The deterministic profile is {enabled=false, depth_frames=2}: with
// MTVU off neither the worker nor the GIF drain thread is constructed.
//
// depth_frames: the runahead cap in display frames (frames declared at the
// boundary minus frames presented); 0 = no frame park; 2 lifts to 3 while the
// present pacer reports its lowest cadence band.
struct MtvuSettings {
    bool enabled = true;
    int depth_frames = 2;
};
