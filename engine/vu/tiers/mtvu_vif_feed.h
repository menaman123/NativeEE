#pragma once
// What the MTVU worker drives on the VIF1 command processor, behind one
// interface so engine/vu (the worker) never includes engine/vif (which links
// engine/vu): the VIF1 span feed, the TTE-quadword bracket, the word-latch
// reset at a chain-walk head, and the VU1 unit the VIF1 programs run on.
// `Vif` implements it; the composition root hands the worker a `Vif&`.
//
// Cross-thread contract: ProcessVif1 / SetVif1TteFeed / ResetVif1Latch are
// called by the worker thread only while it runs (the EE never feeds VIF1
// directly then); BindVu1 is called on the EE thread at Start / Stop.
#include <cstddef>
#include <cstdint>

class VuUnit;

class MtvuVifFeed {
public:
    virtual ~MtvuVifFeed() = default;
    // `size` bytes of VIF1 stream.
    virtual void ProcessVif1(const uint8_t* data, size_t size) = 0;
    // Bracket a DMAtag's TTE quadword (its pad words are not payload).
    virtual void SetVif1TteFeed(bool on) = 0;
    // Every chain walk starts with an empty word latch.
    virtual void ResetVif1Latch() = 0;
    // The unit VIF1 programs run on; null = the inline unit.
    virtual void BindVu1(VuUnit* unit) = 0;
};
