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

void Vu1AotTier::SetNativeExports(uint64_t image_hash, const Vu1AotImageExports* exports) {
    std::lock_guard<std::mutex> lock(mu_);
    if (exports == nullptr) native_.erase(image_hash);
    else native_[image_hash] = exports;
    epoch_.fetch_add(1, std::memory_order_release);
}

const Vu1AotImageExports* Vu1AotTier::native_exports(uint64_t image_hash) const {
    std::lock_guard<std::mutex> lock(mu_);
    const auto it = native_.find(image_hash);
    return it == native_.end() ? nullptr : it->second;
}

size_t Vu1AotTier::native_images() const {
    std::lock_guard<std::mutex> lock(mu_);
    return native_.size();
}

bool Vu1AotTier::InstallNative(VuBlockProgram& prog, const Vu1AotImageExports& ex, bool flags_lazy) {
    if (ex.abi_version != kVu1AotAbiVersion || ex.slot_size != sizeof(VuDecodedSlot) || ex.image_hash != prog.hash) return false;
    if ((ex.flags_lazy != 0) != flags_lazy) return false;  // the pushes name the other MAC-ring arm
    const size_t n_slots = prog.slots.size();
    for (uint32_t i = 0; i < ex.block_count; ++i) {
        const Vu1AotBlockExport& b = ex.blocks[i];
        if ((b.pc & 7u) != 0 || (b.pc >> 3) >= n_slots || b.fn == nullptr) return false;
        const VuBlock* blk = block_.BlockAt(prog, b.pc);
        if (blk->ops.size() != b.n_ops) return false;
    }
    for (uint32_t i = 0; i < ex.block_count; ++i) block_.BlockAt(prog, ex.blocks[i].pc)->native = ex.blocks[i].fn;
    prog.native = &ex;
    ++stats_.native_installs;
    stats_.native_blocks += ex.block_count;
    return true;
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
// the memo's; the maps (under their lock) otherwise. The memo carries the
// image's exports pointer too.
Vu1AotTier::Status Vu1AotTier::Lookup(uint64_t image_hash) {
    const uint32_t epoch = epoch_.load(std::memory_order_acquire);
    if (image_hash == memo_hash_ && epoch == memo_epoch_) return memo_status_;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto it = status_.find(image_hash);
        memo_status_ = it == status_.end() ? Status::Unverified : it->second;
        const auto nt = native_.find(image_hash);
        memo_native_ = nt == native_.end() ? nullptr : nt->second;
    }
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
    const Status looked = Lookup(prog->hash);  // (the memo also carries the exports: read even when assumed)
    const Status st = assume_verified_ ? Status::Verified : looked;
    if (st == Status::Verified) {
        if (prog->resolve != &Vu1AotTier::ResolveSpecialized) {
            block_.SetResolver(*prog, &Vu1AotTier::ResolveSpecialized);
            ++stats_.specialized;
        }
        // The generated code, once the builder has published it (and withdrawn:
        // the blocks go back to RunOps). A refused table is dropped from the map
        // so this is not retried every kick.
        if (prog->native != memo_native_) {
            if (memo_native_ == nullptr) {
                VuBlockTier::ClearNative(*prog);
            } else if (!InstallNative(*prog, *memo_native_, unit.flags().lazy())) {
                VuBlockTier::ClearNative(*prog);
                ++native_refused_;
                SetNativeExports(prog->hash, nullptr);
                memo_native_ = nullptr;
            }
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
    if (prog->native != nullptr) VuBlockTier::ClearNative(*prog);
    if (observer_ != nullptr) observer_(observer_user_, unit, start_pc, prog->hash, st);
    ++stats_.fallback_kicks;
    if (fallback_ == Fallback::Interp) generic_.Run(unit, start_pc);
    else block_.ExecuteAcquired(unit, pc, *prog);
}
