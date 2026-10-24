// VuLockstep -- the in-game tier A/B. Rules in the header.
#include "engine/vu/tiers/vu_lockstep.h"

#include <cstring>

#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu1_case.h"
#include "engine/vu/vu_image_hash.h"
#include "engine/vu/vu_unit.h"

namespace {

uint32_t Le32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

std::string Hex8(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "%08x", v);
    return b;
}

std::string Qword(const uint8_t* p) {
    return Hex8(Le32(p)) + " " + Hex8(Le32(p + 4)) + " " + Hex8(Le32(p + 8)) + " " + Hex8(Le32(p + 12));
}

std::string Lanes(const VR_reg& v) {
    return Hex8(v.I[0]) + "," + Hex8(v.I[1]) + "," + Hex8(v.I[2]) + "," + Hex8(v.I[3]);
}

std::string U64(uint64_t v) { return std::to_string(static_cast<unsigned long long>(v)); }

// The first register field the two blocks disagree on, named (TPC only when asked).
bool FirstRegDiff(const VUregs& a, const VUregs& b, bool with_tpc, std::string& detail) {
    static const char kLane[4] = {'x', 'y', 'z', 'w'};
    for (int i = 0; i < 32; ++i) {
        for (int l = 0; l < 4; ++l) {
            if (a.VF[i].I[l] != b.VF[i].I[l]) {
                detail = "VF" + std::to_string(i) + "." + kLane[l] + " tier=" + Lanes(a.VF[i]) + " ref=" + Lanes(b.VF[i]);
                return true;
            }
        }
    }
    for (int i = 0; i < 16; ++i) {
        if (a.VI[i] != b.VI[i]) {
            detail = "VI" + std::to_string(i) + " tier=" + Hex8(a.VI[i]) + " ref=" + Hex8(b.VI[i]);
            return true;
        }
    }
    for (int l = 0; l < 4; ++l) {
        if (a.ACC.I[l] != b.ACC.I[l]) {
            detail = std::string("ACC.") + kLane[l] + " tier=" + Lanes(a.ACC) + " ref=" + Lanes(b.ACC);
            return true;
        }
    }
    uint32_t qa, qb, pa, pb;
    std::memcpy(&qa, &a.Q, 4);
    std::memcpy(&qb, &b.Q, 4);
    std::memcpy(&pa, &a.P, 4);
    std::memcpy(&pb, &b.P, 4);
    if (qa != qb) { detail = "Q tier=" + Hex8(qa) + " ref=" + Hex8(qb); return true; }
    if (pa != pb) { detail = "P tier=" + Hex8(pa) + " ref=" + Hex8(pb); return true; }
    if (a.I != b.I) { detail = "I tier=" + Hex8(a.I) + " ref=" + Hex8(b.I); return true; }
    if (a.R != b.R) { detail = "R tier=" + Hex8(a.R) + " ref=" + Hex8(b.R); return true; }
    if (a.status_flag != b.status_flag) { detail = "STATUS tier=" + Hex8(a.status_flag) + " ref=" + Hex8(b.status_flag); return true; }
    if (a.mac_flag != b.mac_flag) { detail = "MAC tier=" + Hex8(a.mac_flag) + " ref=" + Hex8(b.mac_flag); return true; }
    if (a.clip_flag != b.clip_flag) { detail = "CLIP tier=" + Hex8(a.clip_flag) + " ref=" + Hex8(b.clip_flag); return true; }
    if (with_tpc && a.TPC != b.TPC) { detail = "TPC tier=" + Hex8(a.TPC) + " ref=" + Hex8(b.TPC); return true; }
    return false;
}

}  // namespace

VuLockstep::VuLockstep() : parser_(sink_, gif_regs_), arbiter_(parser_) {}

VuLockstep::~VuLockstep() = default;

void VuLockstep::EnsureShadow(const VuUnit& unit) {
    if (shadow_) return;
    const bool paced = unit.xgkick() == nullptr || unit.xgkick()->paced_drain();
    const bool lazy = unit.flags().lazy();
    shadow_.reset(new VuUnit(unit.id(), &arbiter_, paced, lazy));
    entry_.reset(new VuUnit(unit.id(), &arbiter_, paced, lazy));
    keep_.reset(new VuUnit(unit.id(), &arbiter_, paced, lazy));
    for (VuUnit* u : {shadow_.get(), entry_.get(), keep_.get()}) {
        u->dispatch().settings().hle_enabled = false;
        u->dispatch().settings().vu1_mode = VuTierMode::Interp;
        if (XgKick* x = u->xgkick()) x->SetMuted(true);  // the reference's packets are compared, never delivered
    }
    if (XgKick* xs = shadow_->xgkick()) xs->SetSubmitTap(&VuLockstep::TapShadow, this);
}

void VuLockstep::Append(std::vector<uint8_t>& to, const uint8_t* data, size_t size) {
    const uint32_t n = static_cast<uint32_t>(size);
    const uint8_t* np = reinterpret_cast<const uint8_t*>(&n);
    to.insert(to.end(), np, np + 4);
    to.insert(to.end(), data, data + size);
}

void VuLockstep::TapReal(void* user, const uint8_t* data, size_t size) {
    VuLockstep* self = static_cast<VuLockstep*>(user);
    Append(self->kicks_real_, data, size);
    ++self->kick_packets_real_;
}

void VuLockstep::TapShadow(void* user, const uint8_t* data, size_t size) {
    VuLockstep* self = static_cast<VuLockstep*>(user);
    Append(self->kicks_shadow_, data, size);
    ++self->kick_packets_shadow_;
}

void VuLockstep::RunReference(uint32_t start_pc, uint64_t cycle_limit) {
    kicks_shadow_.clear();
    kick_packets_shadow_ = 0;
    VuInterpreterTier::RunOptions opt;
    opt.cycle_limit = cycle_limit;
    shadow_->dispatch().interp().Run(*shadow_, start_pc, opt);
}

uint32_t VuLockstep::Compare(const VuUnit& real, bool with_tpc, uint64_t bundles_real, uint64_t bundles_ref, std::string& detail) const {
    uint32_t cat = 0;
    std::string d;
    const VuUnit& ref = *shadow_;
    if (FirstRegDiff(real.regs(), ref.regs(), with_tpc, d)) {
        cat |= kRegs;
        detail += " regs:" + d;
    }
    const Vu1State& sa = real.state();
    const Vu1State& sb = ref.state();
    if (std::memcmp(sa.data, sb.data, sizeof(sa.data)) != 0) {
        cat |= kData;
        size_t i = 0;
        while (i < sizeof(sa.data) && sa.data[i] == sb.data[i]) ++i;
        const size_t q = i & ~size_t(15);
        size_t n = 0;
        for (size_t j = 0; j < sizeof(sa.data); j += 16) n += std::memcmp(sa.data + j, sb.data + j, 16) != 0;
        detail += " data[0x" + Hex8(static_cast<uint32_t>(q)).substr(4) + "] tier=" + Qword(sa.data + q) + " ref=" + Qword(sb.data + q) +
                  " (" + std::to_string(n) + " qwords differ)";
    }
    if (real.cycle() != ref.cycle()) {
        cat |= kCycle;
        detail += " cycle tier=" + U64(real.cycle()) + " ref=" + U64(ref.cycle());
    }
    const FlagPipeline& fa = real.flags();
    const FlagPipeline& fb = ref.flags();
    const float qva = fa.q_pending_value(), qvb = fb.q_pending_value(), pva = fa.p_pending_value(), pvb = fb.p_pending_value();
    if (fa.q_pending() != fb.q_pending() || (fa.q_pending() && (fa.q_pending_retire() != fb.q_pending_retire() ||
                                                                 fa.q_pending_status() != fb.q_pending_status() ||
                                                                 std::memcmp(&qva, &qvb, 4) != 0)) ||
        fa.p_pending() != fb.p_pending() || (fa.p_pending() && (fa.p_pending_retire() != fb.p_pending_retire() ||
                                                                 std::memcmp(&pva, &pvb, 4) != 0))) {
        cat |= kLatches;
        detail += " latches Q(" + std::to_string(fa.q_pending()) + "/" + std::to_string(fb.q_pending()) + ") P(" +
                  std::to_string(fa.p_pending()) + "/" + std::to_string(fb.p_pending()) + ")";
    }
    if (bundles_real != bundles_ref) {
        cat |= kBundles;
        detail += " bundles tier=" + U64(bundles_real) + " ref=" + U64(bundles_ref);
    }
    if (kick_packets_real_ != kick_packets_shadow_ || kicks_real_ != kicks_shadow_) {
        cat |= kPackets;
        size_t i = 0;
        const size_t n = kicks_real_.size() < kicks_shadow_.size() ? kicks_real_.size() : kicks_shadow_.size();
        while (i < n && kicks_real_[i] == kicks_shadow_[i]) ++i;
        detail += " packets tier=" + std::to_string(kick_packets_real_) + "/" + std::to_string(kicks_real_.size()) + "B ref=" +
                  std::to_string(kick_packets_shadow_) + "/" + std::to_string(kicks_shadow_.size()) + "B first_diff_at=" + std::to_string(i);
    }
    return cat;
}

void VuLockstep::Bisect(VuTierDispatch& dispatch, VuUnit& unit, uint32_t start_pc, uint64_t bundles) {
    keep_->CopyStateFrom(unit);
    const uint64_t cap0 = dispatch.cycle_cap();
    XgKick* xr = unit.xgkick();
    if (xr != nullptr) xr->SetMuted(true);
    std::string detail;
    // Both tiers from the entry state, k bundles each; the states compared on
    // `want` (registers without TPC, data memory, the latches -- the clock
    // only when nothing else parts: a tier's CUT path may charge a cycle
    // differently from its whole-block path at some caps, so a cycle-only
    // bisect is a hint to confirm on an uncut replay, not a verdict).
    uint32_t want = kRegs | kData | kLatches;
    auto differs = [&](uint64_t k) {
        unit.CopyStateFrom(*entry_);
        dispatch.set_cycle_cap(k);
        kicks_real_.clear();
        kick_packets_real_ = 0;
        dispatch.RunTier(unit, start_pc);
        shadow_->CopyStateFrom(*entry_);
        RunReference(start_pc, k);
        detail.clear();
        return (Compare(unit, false, 0, 0, detail) & want) != 0;
    };
    bool cycle_only = false;
    if (bundles != 0 && !differs(bundles)) {
        want = kCycle;
        cycle_only = true;
    }
    if (bundles == 0 || !differs(bundles)) {
        std::fprintf(out_, "vu1_lockstep:   bisect: no register / data / latch / clock divergence under a cap of %llu bundles (the diff is in the end-of-run drain or the packets only)\n",
                     static_cast<unsigned long long>(bundles));
    } else {
        uint64_t lo = 0, hi = bundles;  // differs(lo) false (nothing ran), differs(hi) true
        while (hi - lo > 1) {
            const uint64_t mid = lo + (hi - lo) / 2;
            if (differs(mid)) hi = mid;
            else lo = mid;
        }
        differs(hi);  // leave the states at the first differing cap for the report
        const uint32_t pc = unit.state().cur_bundle_pc;
        const uint8_t* micro = unit.state().micro;
        std::fprintf(out_, "vu1_lockstep:   first %sdivergence after bundle %llu (tier pc 0x%04x ref pc 0x%04x; slot lower=%08x upper=%08x):%s%s\n",
                     cycle_only ? "CLOCK-ONLY " : "", static_cast<unsigned long long>(hi), pc, shadow_->state().cur_bundle_pc,
                     Le32(micro + (pc & 0x3FF8u)), Le32(micro + (pc & 0x3FF8u) + 4), detail.c_str(),
                     cycle_only ? "  [a cap-cut path may differ by a cycle where the whole block does not: confirm on an uncut replay]" : "");
    }
    dispatch.set_cycle_cap(cap0);
    unit.CopyStateFrom(*keep_);
    if (xr != nullptr) xr->SetMuted(false);
}

void VuLockstep::Run(VuTierDispatch& dispatch, VuUnit& unit, uint32_t start_pc) {
    EnsureShadow(unit);
    ++stats_.kicks;
    entry_->CopyStateFrom(unit);
    shadow_->CopyStateFrom(unit);
    kicks_real_.clear();
    kick_packets_real_ = 0;
    XgKick* xr = unit.xgkick();
    if (xr != nullptr) xr->SetSubmitTap(&VuLockstep::TapReal, this);
    const uint64_t b0 = dispatch.bundles();
    dispatch.RunTier(unit, start_pc);
    const uint64_t bundles_real = dispatch.bundles() - b0;
    if (xr != nullptr) xr->SetSubmitTap(nullptr, nullptr);
    const uint64_t r0 = shadow_->dispatch().bundles();
    RunReference(start_pc, dispatch.cycle_cap());
    const uint64_t bundles_ref = shadow_->dispatch().bundles() - r0;
    std::string detail;
    const uint32_t cat = Compare(unit, true, bundles_real, bundles_ref, detail);
    if (cat == 0) return;
    ++stats_.divergent;
    if (stats_.first_divergent_kick == 0) stats_.first_divergent_kick = stats_.kicks;
    if (cat & kRegs) ++stats_.diverged_regs;
    if (cat & kData) ++stats_.diverged_data;
    if (cat & kCycle) ++stats_.diverged_cycle;
    if (cat & kLatches) ++stats_.diverged_latches;
    if (cat & kBundles) ++stats_.diverged_bundles;
    if (cat & kPackets) ++stats_.diverged_packets;
    if (reported_ >= report_limit_) return;
    ++reported_;
    const Vu1State& st = unit.state();
    std::fprintf(out_, "vu1_lockstep: DIVERGENCE at kick %llu start_pc=0x%04x image=%016llx bundles=%llu/%llu kicks=%d:%s\n",
                 static_cast<unsigned long long>(stats_.kicks), start_pc & 0x3FFFu,
                 static_cast<unsigned long long>(VuImageHash::Fast(st.micro, static_cast<size_t>(st.mem_mask + 1))),
                 static_cast<unsigned long long>(bundles_real), static_cast<unsigned long long>(bundles_ref), st.xgkick_count, detail.c_str());
    // The state the kick started from that a Vu1Case does NOT carry: the
    // in-flight writebacks, the flag rings, the kick path (the hidden state
    // a divergence here and not on the corpus points at).
    const StallModel& es = entry_->stalls();
    const FlagPipeline& ef = entry_->flags();
    const XgKick* ex = entry_->xgkick();
    std::fprintf(out_, "vu1_lockstep:   entry: cycle=%llu fmac_newest_retire=%llu ialu_newest_retire=%llu mac_depth=%d clip_depth=%d q_pending=%d p_pending=%d xgkick_armed=%d\n",
                 static_cast<unsigned long long>(ef.cycle()), static_cast<unsigned long long>(es.fmac_newest_retire()),
                 static_cast<unsigned long long>(es.ialu_newest_retire()), ef.mac_depth(), ef.clip_depth(), ef.q_pending() ? 1 : 0,
                 ef.p_pending() ? 1 : 0, (ex != nullptr && ex->armed()) ? 1 : 0);
    if (!dump_dir_.empty()) {
        Vu1Case c;
        entry_->SaveCase(c, start_pc);
        char name[64];
        std::snprintf(name, sizeof(name), "/kick_%llu_%04x.bin", static_cast<unsigned long long>(stats_.kicks), start_pc & 0x3FFFu);
        const std::string path = dump_dir_ + name;
        std::fprintf(out_, "vu1_lockstep:   entry case -> %s%s\n", path.c_str(), vu1_case::WriteCase(path.c_str(), c) ? "" : " (WRITE FAILED)");
    }
    const uint64_t n = bundles_real < bundles_ref ? bundles_real : bundles_ref;
    Bisect(dispatch, unit, start_pc, n);
    std::fflush(out_);
}

std::string VuLockstep::Summary() const {
    return "kicks=" + U64(stats_.kicks) + ":divergent=" + U64(stats_.divergent) + ":first=" + U64(stats_.first_divergent_kick) +
           ":regs=" + U64(stats_.diverged_regs) + ":data=" + U64(stats_.diverged_data) + ":cycle=" + U64(stats_.diverged_cycle) +
           ":latches=" + U64(stats_.diverged_latches) + ":bundles=" + U64(stats_.diverged_bundles) + ":packets=" + U64(stats_.diverged_packets);
}
