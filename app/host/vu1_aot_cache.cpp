// Vu1AotCache -- manifest, corpus, collector, verifier thread (see the header).
#include "app/host/vu1_aot_cache.h"

#include "app/host/vu1_aot_builder.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "engine/core/log/log.h"
#include "engine/vu/vu1_aot/vu1_aot_verifier.h"
#include "engine/vu/vu1_case.h"
#include "engine/vu/vu_unit.h"

namespace ps2r::app {

namespace {

const char* StatusName(Vu1AotTier::Status s) {
    switch (s) {
        case Vu1AotTier::Status::Verified: return "verified";
        case Vu1AotTier::Status::Refused: return "refused";
        default: return "unverified";
    }
}

std::string Hex16(uint64_t v) {
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

uint64_t NowUnixSeconds() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

}  // namespace

Vu1AotCache::Vu1AotCache(Vu1AotTier& tier, Settings settings) : tier_(tier), settings_(std::move(settings)) {
    if (settings_.cases_per_image < 1) settings_.cases_per_image = 1;
}

Vu1AotCache::~Vu1AotCache() { Close(/*drain=*/false); }

// The build id as a token (the directory name, the row's engine field): every
// byte outside [A-Za-z0-9._-] becomes '_'.
std::string Vu1AotCache::BuildToken(const std::string& build_id) {
    std::string tok = build_id.empty() ? "unknown-build" : build_id;
    for (char& ch : tok) {
        const bool ok = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '.' || ch == '_' || ch == '-';
        if (!ok) ch = '_';
    }
    return tok;
}

std::string Vu1AotCache::ManifestPath(const std::string& root, const std::string& build_id) {
    return root + "/" + BuildToken(build_id) + "/manifest.txt";
}

std::string Vu1AotCache::CorpusDir(const std::string& root) { return root + "/corpus"; }

// ---- the manifest ---------------------------------------------------------

std::string Vu1AotCache::FormatRow(const ManifestRow& r) {
    std::string s = "image=" + Hex16(r.image) + " status=" + StatusName(r.status) + " cases=" + std::to_string(r.cases) +
                    " corpus=" + Hex16(r.corpus) + " engine=" + BuildToken(r.engine) + " verified_at=" + std::to_string(r.verified_at) +
                    " bundles=" + std::to_string(r.bundles);
    if (!r.divergence.empty()) s += " divergence=" + r.divergence;
    return s;
}

bool Vu1AotCache::ParseRow(const std::string& line, ManifestRow& r) {
    if (line.empty() || line[0] == '#') return false;
    r = ManifestRow();
    bool have_image = false, have_status = false;
    size_t pos = 0;
    while (pos < line.size()) {
        const size_t eq = line.find('=', pos);
        if (eq == std::string::npos) break;
        const std::string key = line.substr(pos, eq - pos);
        // The divergence field runs to the end of the line (it carries spaces).
        size_t end = key == "divergence" ? line.size() : line.find(' ', eq + 1);
        if (end == std::string::npos) end = line.size();
        const std::string val = line.substr(eq + 1, end - eq - 1);
        if (key == "image") {
            r.image = std::strtoull(val.c_str(), nullptr, 16);
            have_image = true;
        } else if (key == "status") {
            if (val == "verified") r.status = Vu1AotTier::Status::Verified;
            else if (val == "refused") r.status = Vu1AotTier::Status::Refused;
            else return false;
            have_status = true;
        } else if (key == "cases") {
            r.cases = std::strtoull(val.c_str(), nullptr, 10);
        } else if (key == "corpus") {
            r.corpus = std::strtoull(val.c_str(), nullptr, 16);
        } else if (key == "engine") {
            r.engine = val;
        } else if (key == "verified_at") {
            r.verified_at = std::strtoull(val.c_str(), nullptr, 10);
        } else if (key == "bundles") {
            r.bundles = std::strtoull(val.c_str(), nullptr, 10);
        } else if (key == "divergence") {
            r.divergence = val;
        }
        pos = end + 1;
    }
    return have_image && have_status;
}

void Vu1AotCache::AppendManifest(const ManifestRow& r) {
    const std::string path = ManifestPath(settings_.root, settings_.build_id);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::lock_guard<std::mutex> lock(mu_);
    if (std::FILE* f = std::fopen(path.c_str(), "a")) {
        const std::string row = FormatRow(r) + "\n";
        std::fwrite(row.data(), 1, row.size(), f);
        std::fclose(f);
    }
}

// ---- the corpus -----------------------------------------------------------

void Vu1AotCache::LoadCorpusDir(const std::string& dir, std::map<uint64_t, Image>& images) {
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(dir, ec)) return;
    std::vector<std::string> files;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string name = e.path().filename().string();
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".bin") == 0) files.push_back(e.path().string());
    }
    std::sort(files.begin(), files.end());
    for (const std::string& f : files) {
        std::unique_ptr<Vu1Case> c(new Vu1Case());
        if (!vu1_case::ReadCase(f.c_str(), *c)) continue;  // not a case file (the probe's kicks_*.bin, say)
        const uint64_t h = Vu1AotVerifier::ImageHashOf(*c);
        Image& img = images[h];
        ++img.per_entry[c->start_pc & 0x3FFFu];
        img.cases.push_back(std::move(c));
        ++census_.corpus_cases;
    }
}

bool Vu1AotCache::Open(std::string* note) {
    if (open_) return true;
    std::error_code ec;
    std::filesystem::create_directories(CorpusDir(settings_.root), ec);
    if (ec && !std::filesystem::is_directory(CorpusDir(settings_.root))) {
        if (note) *note = "vu1_aot: cannot create " + CorpusDir(settings_.root) + ": " + ec.message();
        return false;
    }
    // 1. The manifest: verdicts of THIS engine build seed the tier.
    std::map<uint64_t, ManifestRow> judged;
    if (std::FILE* f = std::fopen(ManifestPath(settings_.root, settings_.build_id).c_str(), "r")) {
        char line[1024];
        while (std::fgets(line, sizeof(line), f)) {
            std::string s(line);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            ManifestRow r;
            if (!ParseRow(s, r) || r.engine != BuildToken(settings_.build_id)) continue;
            judged[r.image] = r;
        }
        std::fclose(f);
    }
    for (const auto& kv : judged) {
        tier_.SetStatus(kv.first, kv.second.status);
        ++census_.manifest_rows;
    }
    // 2. The corpus on disk: the collected dir, then the extra dir. Images
    //    already judged are not re-verified (the manifest IS the cache);
    //    the others are queued for the verifier.
    LoadCorpusDir(CorpusDir(settings_.root), images_);
    LoadCorpusDir(settings_.extra_corpus, images_);
    size_t queued = 0;
    for (auto& kv : images_) {
        Image& img = kv.second;
        const auto jt = judged.find(kv.first);
        if (jt != judged.end()) {
            img.queued = true;  // nothing more to collect or judge for it
            // A verified image goes to the code generator with its corpus (the
            // library on disk is reused, else emitted and compiled, then re-verified).
            if (builder_ != nullptr && jt->second.status == Vu1AotTier::Status::Verified && !img.cases.empty()) {
                builder_->Submit(kv.first, std::move(img.cases), {});
            }
            img.cases.clear();
            continue;
        }
        if (!img.cases.empty()) {
            img.queued = true;
            Enqueue(kv.first, std::move(img.cases));
            ++queued;
        }
    }
    // 3. The verifier thread and the collector.
    stop_.store(false);
    verifier_ = std::thread([this] { VerifierMain(); });
    tier_.SetKickObserver(&Vu1AotCache::OnKick, this);
    open_ = true;
    if (note) {
        *note = "vu1_aot: root=" + settings_.root + " build=" + settings_.build_id + " manifest_rows=" +
                std::to_string(census_.manifest_rows) + " corpus_cases=" + std::to_string(census_.corpus_cases) +
                " images=" + std::to_string(images_.size()) + " queued=" + std::to_string(queued);
    }
    return true;
}

void Vu1AotCache::Close(bool drain) {
    if (!open_) return;
    tier_.SetKickObserver(nullptr, nullptr);
    {
        std::lock_guard<std::mutex> lock(mu_);
        drain_on_stop_ = drain;
        stop_.store(true);
    }
    cv_.notify_all();
    if (verifier_.joinable()) verifier_.join();
    open_ = false;
}

// ---- the collector (the observer's thread: the live VU1's) ---------------

void Vu1AotCache::OnKick(void* user, VuUnit& unit, uint32_t start_pc, uint64_t image_hash, Vu1AotTier::Status status) {
    if (status != Vu1AotTier::Status::Unverified) return;
    static_cast<Vu1AotCache*>(user)->Collect(unit, start_pc, image_hash);
}

void Vu1AotCache::Collect(VuUnit& unit, uint32_t start_pc, uint64_t image_hash) {
    Image& img = images_[image_hash];
    ++img.kicks;
    if (img.queued) return;
    const int quota = settings_.cases_per_image;
    const int total = static_cast<int>(img.cases.size());
    if (settings_.collect && total < quota) {
        // Spread the quota over the image's entry addresses: the first kick of
        // an entry is always taken, later ones at most quota/4 per entry and
        // only every 16th kick (the OLD collector spaced its captures by frames).
        const uint32_t pc = start_pc & 0x3FFFu;
        int& n_entry = img.per_entry[pc];
        const int per_entry_cap = quota >= 4 ? quota / 4 : 1;
        if (n_entry == 0 || (n_entry < per_entry_cap && (img.kicks & 15u) == 0)) {
            std::unique_ptr<Vu1Case> c(new Vu1Case());
            unit.SaveCase(*c, start_pc);
            char name[64];
            std::snprintf(name, sizeof(name), "/%016llx_%04x_%03d.bin", static_cast<unsigned long long>(image_hash), pc, total);
            vu1_case::WriteCase((CorpusDir(settings_.root) + name).c_str(), *c);
            img.cases.push_back(std::move(c));
            ++n_entry;
            std::lock_guard<std::mutex> lock(mu_);
            ++census_.collected;
        }
    }
    // Judge at the quota, or once the image has been kicked a lot with a
    // partial corpus (an image whose rarer entries never show up again).
    const int now = static_cast<int>(img.cases.size());
    if (now > 0 && (now >= quota || (img.kicks % 1024u) == 0 || !settings_.collect)) {
        img.queued = true;
        Enqueue(image_hash, std::move(img.cases));
    }
}

void Vu1AotCache::Enqueue(uint64_t image_hash, std::vector<std::unique_ptr<Vu1Case>> cases) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        Job j;
        j.image = image_hash;
        j.cases = std::move(cases);
        queue_.push_back(std::move(j));
        ++census_.pending;
    }
    cv_.notify_one();
}

// ---- the verifier thread ----------------------------------------------------

void Vu1AotCache::VerifierMain() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] { return !queue_.empty() || stop_.load(); });
            if (queue_.empty()) return;              // stop with nothing left
            if (stop_.load() && !drain_on_stop_) return;
            job = std::move(queue_.front());
            queue_.erase(queue_.begin());
        }
        Judge(job.image, std::move(job.cases));
    }
}

void Vu1AotCache::Judge(uint64_t image_hash, std::vector<std::unique_ptr<Vu1Case>> cases) {
    std::vector<const Vu1Case*> ptrs;
    ptrs.reserve(cases.size());
    for (const auto& c : cases) ptrs.push_back(c.get());
    const auto t0 = std::chrono::steady_clock::now();
    const Vu1AotVerifier::Result r = Vu1AotVerifier::Verify(ptrs);
    const auto t1 = std::chrono::steady_clock::now();
    const Vu1AotTier::Status status = r.identical ? Vu1AotTier::Status::Verified : Vu1AotTier::Status::Refused;
    {
        std::lock_guard<std::mutex> lock(mu_);
        ++census_.verify_runs;
        census_.verify_ns += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        if (census_.pending > 0) --census_.pending;
    }
    tier_.SetStatus(image_hash, status);  // published after the census: a reader that sees Verified sees pending down
    ManifestRow row;
    row.image = image_hash;
    row.status = status;
    row.cases = r.cases;
    row.corpus = r.corpus_hash;
    row.engine = settings_.build_id;
    row.verified_at = NowUnixSeconds();
    row.bundles = r.bundles;
    row.divergence = r.divergence;
    AppendManifest(row);
    PS2R_LOG(VU, Info, "[VU1-AOT] image %016llx %s cases=%zu bundles=%llu corpus=%016llx in %.1f ms%s%s",
             static_cast<unsigned long long>(image_hash), StatusName(status), r.cases, static_cast<unsigned long long>(r.bundles),
             static_cast<unsigned long long>(r.corpus_hash),
             static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()) / 1000.0,
             r.divergence.empty() ? "" : " divergence=", r.divergence.c_str());
    // Verified: the code generator takes the corpus (and the block pcs the
    // native legs built, the JR / JALR targets among them).
    if (r.identical && builder_ != nullptr) builder_->Submit(image_hash, std::move(cases), r.block_pcs);
}

// ---- the census ----------------------------------------------------------------

Vu1AotCache::Census Vu1AotCache::census() const {
    std::lock_guard<std::mutex> lock(mu_);
    return census_;
}

std::string Vu1AotCache::CensusRow() const {
    const Census c = census();
    const Vu1AotTier::Stats& s = tier_.stats();
    char ns[32];
    std::snprintf(ns, sizeof(ns), "%.2f", tier_.native_ns_per_bundle());
    return "programs:" + std::to_string(tier_.images()) + ":verified:" + std::to_string(tier_.verified()) + ":refused:" +
           std::to_string(tier_.refused()) + ":pending:" + std::to_string(c.pending) + ":native_kicks:" +
           std::to_string(s.native_kicks) + ":fallback_kicks:" + std::to_string(s.fallback_kicks) + ":native_bundles:" +
           std::to_string(s.native_bundles) + ":ns_per_bundle:" + ns + ":corpus:" + std::to_string(c.corpus_cases) +
           ":collected:" + std::to_string(c.collected) + ":verify_runs:" + std::to_string(c.verify_runs) + ":verify_ms:" +
           std::to_string(c.verify_ns / 1000000u);
}

}  // namespace ps2r::app
