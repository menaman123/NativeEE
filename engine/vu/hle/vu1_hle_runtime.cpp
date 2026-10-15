// Vu1HleRuntime -- the HLE tier's TryExecute (rules in the header).
#include "engine/vu/hle/vu1_hle_runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "engine/vu/vu1_case.h"
#include "engine/vu/vu_image_hash.h"
#include "engine/vu/vu_unit.h"

Vu1HleRuntime::Vu1HleRuntime(const Vu1ProgramLibrary& library, GifArbiter& arbiter)
    : registry_(library),
      analyzer_(library),
      family_exit_tpc_(library.family.family_exit_tpc),
      owned_lowering_(new KickLowering(arbiter)),
      lowering_(owned_lowering_.get()) {}

Vu1HleRuntime::Vu1HleRuntime(const Vu1ProgramLibrary& library, KickLowering& lowering)
    : registry_(library),
      analyzer_(library),
      family_exit_tpc_(library.family.family_exit_tpc),
      lowering_(&lowering) {}

void Vu1HleRuntime::SetVifTops(uint16_t tops, uint16_t itops) {
    tops_ = tops;
    itops_ = itops;
}

void Vu1HleRuntime::SetIdentityObserver(IdentityObserver fn, void* user) {
    identity_observer_ = fn;
    identity_user_ = user;
}

void Vu1HleRuntime::SetMissCapture(const std::string& dir, const char* tag, unsigned per_kind) {
    capture_dir_ = dir;
    capture_tag_ = tag ? tag : "vu1";
    capture_per_kind_ = per_kind;
}

void Vu1HleRuntime::CaptureMiss(const VuUnit& unit, uint32_t start_pc, uint64_t variant_hash, uint32_t entry_pc,
                                uint64_t count) {
    if (capture_dir_.empty() || count > capture_per_kind_) return;
    Vu1Case c;
    unit.SaveCase(c, start_pc);
    // The VIF1 TOPS / ITOPS shadows are the runtime's (the unit carries TOP / ITOP).
    c.vif1_tops = tops_;
    c.vif1_itops = itops_;
    char name[96];
    std::snprintf(name, sizeof(name), "/%s_%016llx_%04x_%llu.bin", capture_tag_, (unsigned long long)variant_hash,
                  (unsigned)entry_pc, (unsigned long long)count);
    const std::string path = capture_dir_ + name;
    if (vu1_case::WriteCase(path.c_str(), c)) miss_captures_++;
}

bool Vu1HleRuntime::Hook(void* user, VuUnit& unit, uint32_t start_pc, uint64_t micro_generation) {
    return static_cast<Vu1HleRuntime*>(user)->TryExecute(unit, start_pc, micro_generation);
}

const Vu1ImageInfo& Vu1HleRuntime::AnalyzeMemo(uint64_t image, const uint8_t* micro, const uint8_t* data) {
    for (const MemoEntry& e : memo_)
        if (e.image == image) return e.info;
    if (memo_.size() > kMemoLimit) memo_.clear();
    memo_.push_back({image, analyzer_.Analyze(micro, data)});
    return memo_.back().info;
}

bool Vu1HleRuntime::TryExecute(VuUnit& unit, uint32_t start_pc, uint64_t micro_generation) {
    Vu1State& st = unit.state();

    // Image hash memo keyed on (micro generation, external epoch).
    uint64_t img;
    if (gen_valid_ && micro_generation == gen_key_ && ext_gen_ == ext_key_) {
        img = gen_img_;
    } else {
        img = VuImageHash::Fast(st.micro, kVu1HleMicroSize);
        gen_key_ = micro_generation;
        ext_key_ = ext_gen_;
        gen_img_ = img;
        gen_valid_ = true;
    }
    const Vu1ImageInfo& info = AnalyzeMemo(img, st.micro, st.data);

    const uint32_t entry_pc = start_pc & Vu1State::kVu1MemMask;
    const Vu1LibraryEntry* e = registry_.Lookup(info, entry_pc);
    if (!e) {
        census_.misses++;
        uint64_t count = 1;
        bool known = false;
        for (Census::MissKind& kind : census_.miss_kinds)
            if (kind.variant_hash == info.variant_hash && kind.entry_pc == entry_pc) {
                count = ++kind.count;
                known = true;
                break;
            }
        if (!known && census_.miss_kinds.size() < Census::kMissKinds) {
            census_.miss_kinds.push_back({info.variant_hash, entry_pc, 1});
            known = true;
        }
        // Only the censused kinds are captured (a kind past kMissKinds has no count).
        if (known && capture_per_kind_) CaptureMiss(unit, start_pc, info.variant_hash, entry_pc, count);
        return false;
    }

    Vu1HleInput in{};
    in.micro = st.micro;
    in.data = st.data;
    in.start_pc = entry_pc;
    in.top = unit.vif_top();
    in.tops = tops_;
    in.itop = unit.vif_itop();
    in.itops = itops_;
    in.regs = &st.regs;
    in.image_hash = info.image_hash;
    in.info = &info;

    Vu1HleOutput out;
    if (!e->fn(in, out)) {
        census_.declines++;
        bool known = false;
        for (Decline& d : declines_) {
            if (d.image_hash == info.image_hash && d.variant_hash == info.variant_hash && d.entry_pc == entry_pc &&
                d.name == e->name) {
                d.count++;
                known = true;
                break;
            }
        }
        if (!known && declines_.size() < kDeclineRows)
            declines_.push_back({e->name, info.image_hash, info.variant_hash, entry_pc, 1});
        return false;
    }

    // Data writes BEFORE the kicks: the packets were snapshotted by the program.
    for (const Vu1HleOutput::DataWrite& w : out.data_writes) {
        const uint32_t addr = w.addr & Vu1State::kVu1MemMask;
        const size_t n = std::min(w.bytes.size(), (size_t)(kVu1HleDataSize - addr));
        std::memcpy(st.data + addr, w.bytes.data(), n);
    }
    // Each kick's identity is reported BEFORE its submit (the binder stages it
    // for the arbiter slot the submit opens); null after the last one.
    KickIdentity id{};
    id.handler_name = e->name;
    id.image_hash = info.image_hash;
    id.kick_pc = static_cast<uint16_t>(entry_pc & 0x3FFF);
    // The lowering's unsupported census keys on the registration table's hash.
    KickLowering::Origin origin{};
    origin.program_hash = info.variant_hash;
    origin.name = e->name;
    origin.entry_pc = id.kick_pc;
    for (std::vector<uint8_t>& k : out.kicks) {
        if (identity_observer_) identity_observer_(identity_user_, &id);
        lowering_->Submit(std::move(k), origin);
        id.kick_ord++;
    }
    if (identity_observer_ && !out.kicks.empty()) identity_observer_(identity_user_, nullptr);

    if (out.has_regs) {
        st.regs = out.regs;
    } else {
        st.regs.TPC = family_exit_tpc_;
    }
    census_.runs++;
    return true;
}
