// Vu1AotTier -- the verified-native / fallback switch per image, the status
// map and the specialised resolver. Rules in the header.
#include "engine/vu/vu1_aot/vu1_aot_tier.h"

#include <chrono>

#include "engine/vu/tiers/vu_interpreter_tier.h"
#include "engine/vu/vu_unit.h"

Vu1AotTier::Vu1AotTier(VuBlockTier& block, VuInterpreterTier& generic) : block_(block), generic_(generic) {}

void Vu1AotTier::ResolveSpecialized(VuBlockOp& op, const VuDecodedSlot& s) {
    VuBlockTier::ResolveOp(op, s);  // every fact of the block tier's op ...
    // ... and the two bodies with the dest mask folded in. The NOP upper stays
    // null (the loop's ACC re-clamp arm) and a lower that does not issue stays null.
    if (op.upper != nullptr) op.upper = VuCachedTier::UpperFastFnD(s.upper_kind, s.u_dest);
    if (op.lower != nullptr) op.lower = VuCachedTier::LowerFastFnD(s.lower_kind, s.l_dest);
}

void Vu1AotTier::SetStatus(uint64_t image_hash, Status status) {
    std::lock_guard<std::mutex> lock(mu_);
    status_[image_hash] = status;
    epoch_.fetch_add(1, std::memory_order_release);
}

Vu1AotTier::Status Vu1AotTier::status(uint64_t image_hash) const {
    std::lock_guard<std::mutex> lock(mu_);
    const auto it = status_.find(image_hash);
    return it == status_.end() ? Status::Unverified : it->second;
}

size_t Vu1AotTier::images() const {
    std::lock_guard<std::mutex> lock(mu_);
    return status_.size();
}

size_t Vu1AotTier::verified() const {
    std::lock_guard<std::mutex> lock(mu_);
    size_t n = 0;
    for (const auto& kv : status_) n += kv.second == Status::Verified;
    return n;
}

size_t Vu1AotTier::refused() const {
    std::lock_guard<std::mutex> lock(mu_);
    size_t n = 0;
    for (const auto& kv : status_) n += kv.second == Status::Refused;
    return n;
}

void Vu1AotTier::SetKickObserver(KickObserver fn, void* user) {
    observer_ = fn;
    observer_user_ = user;
}

double Vu1AotTier::native_ns_per_bundle() const {
    return stats_.native_bundles ? static_cast<double>(stats_.native_ns) / static_cast<double>(stats_.native_bundles) : 0.0;
}

// The per-kick status read: one compare when the hash and the map epoch are
// the memo's; the map (under its lock) otherwise.
Vu1AotTier::Status Vu1AotTier::Lookup(uint64_t image_hash) {
    const uint32_t epoch = epoch_.load(std::memory_order_acquire);
    if (image_hash == memo_hash_ && epoch == memo_epoch_) return memo_status_;
    memo_status_ = status(image_hash);
    memo_hash_ = image_hash;
    memo_epoch_ = epoch;
    return memo_status_;
}

void Vu1AotTier::Execute(VuUnit& unit, uint32_t start_pc) {
    const uint32_t pc = start_pc & VuBlockTier::kPcMask;
    if (unit.id() != 1 || (pc & 7u) != 0) {  // not this tier's: the block tier hands it to the reference
        block_.Execute(unit, start_pc);
        return;
    }
    VuBlockProgram* prog = block_.Acquire(unit);
    const Status st = assume_verified_ ? Status::Verified : Lookup(prog->hash);
    if (st == Status::Verified) {
        if (prog->resolve != &Vu1AotTier::ResolveSpecialized) {
            block_.SetResolver(*prog, &Vu1AotTier::ResolveSpecialized);
            ++stats_.specialized;
        }
        const uint64_t before = block_.bundles();
        const auto t0 = std::chrono::steady_clock::now();
        block_.ExecuteAcquired(unit, pc, *prog);
        const auto t1 = std::chrono::steady_clock::now();
        ++stats_.native_kicks;
        stats_.native_bundles += block_.bundles() - before;
        stats_.native_ns += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        return;
    }
    // Not verified (yet): the collector sees the kick, the fallback runs it.
    // A table specialised under an earlier verdict goes back to the dynamic bodies.
    if (prog->resolve != nullptr) block_.SetResolver(*prog, nullptr);
    if (observer_ != nullptr) observer_(observer_user_, unit, start_pc, prog->hash, st);
    ++stats_.fallback_kicks;
    if (fallback_ == Fallback::Interp) generic_.Run(unit, start_pc);
    else block_.ExecuteAcquired(unit, pc, *prog);
}
