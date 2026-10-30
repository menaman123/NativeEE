// TimeStretch implementation; the method is described in the header.
#include "engine/sound/time_stretch.h"

#include <algorithm>
#include <cmath>

namespace ps2r::sound {

namespace {
constexpr int kCoarseStep = 4;
constexpr double kPi = 3.14159265358979323846;
// Prefers the nominal offset among equal matches (a periodic signal matches at every period).
constexpr double kOffsetBias = 1e-6;
}  // namespace

TimeStretch::TimeStretch()
    : ola_(static_cast<size_t>(kWindow) * kChannels, 0.0f), window_(static_cast<size_t>(kWindow)) {
    for (int i = 0; i < kWindow; ++i)
        window_[static_cast<size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * i / kWindow));
}

void TimeStretch::Reset() {
    in_.clear();
    base_ = 0;
    pos_ = 0.0;
    prev_ = -1;
    std::fill(ola_.begin(), ola_.end(), 0.0f);
}

int64_t TimeStretch::BufferedEnd() const { return base_ + static_cast<int64_t>(in_.size() / kChannels); }

double TimeStretch::Match(int64_t start, int step) const {
    const size_t x0 = static_cast<size_t>(start - base_) * kChannels, y0 = static_cast<size_t>(prev_ + kHop - base_) * kChannels;
    double xy = 0.0, xx = 0.0, yy = 0.0;
    for (size_t j = 0; j < static_cast<size_t>(kHop) * kChannels; j += static_cast<size_t>(step) * kChannels) {
        for (size_t c = 0; c < kChannels; ++c) {
            const double x = in_[x0 + j + c], y = in_[y0 + j + c];
            xy += x * y;
            xx += x * x;
            yy += y * y;
        }
    }
    if (xx <= 0.0 || yy <= 0.0) return 0.0;
    return xy / std::sqrt(xx * yy);
}

int64_t TimeStretch::PickStart(int64_t nominal) const {
    if (prev_ < 0) return nominal;
    const int64_t lo = std::max(base_, nominal - kSeek), hi = nominal + kSeek;
    int64_t best = nominal;
    double best_score = -2.0;
    for (int64_t s = lo; s <= hi; s += kCoarseStep) {
        const double score = Match(s, kCoarseStep) - kOffsetBias * static_cast<double>(std::llabs(s - nominal));
        if (score > best_score) {
            best_score = score;
            best = s;
        }
    }
    const int64_t center = best;
    best_score = -2.0;
    for (int64_t s = std::max(lo, center - kCoarseStep + 1); s <= std::min(hi, center + kCoarseStep - 1); ++s) {
        const double score = Match(s, 1) - kOffsetBias * static_cast<double>(std::llabs(s - nominal));
        if (score > best_score) {
            best_score = score;
            best = s;
        }
    }
    return best;
}

void TimeStretch::Process(const float* interleaved, int frames, double ratio, std::vector<float>& out) {
    if (frames > 0) {
        in_.insert(in_.end(), interleaved, interleaved + static_cast<size_t>(frames) * kChannels);
        frames_in_ += static_cast<uint64_t>(frames);
    }
    ratio = std::clamp(ratio, kMinRatio, kMaxRatio);
    for (;;) {
        const int64_t nominal = static_cast<int64_t>(std::llround(pos_));
        int64_t need = nominal + kWindow;
        if (prev_ >= 0) need = std::max(nominal + kSeek + kWindow, prev_ + 2 * kHop);
        if (BufferedEnd() < need) break;
        const int64_t start = PickStart(nominal);
        const size_t from = static_cast<size_t>(start - base_) * kChannels;
        for (int i = 0; i < kWindow; ++i) {
            const float w = window_[static_cast<size_t>(i)];
            for (int c = 0; c < kChannels; ++c)
                ola_[static_cast<size_t>(i) * kChannels + c] += w * in_[from + static_cast<size_t>(i) * kChannels + c];
        }
        out.insert(out.end(), ola_.begin(), ola_.begin() + static_cast<size_t>(kHop) * kChannels);
        std::copy(ola_.begin() + static_cast<size_t>(kHop) * kChannels, ola_.end(), ola_.begin());
        std::fill(ola_.begin() + static_cast<size_t>(kWindow - kHop) * kChannels, ola_.end(), 0.0f);
        frames_out_ += static_cast<uint64_t>(kHop);
        prev_ = start;
        pos_ += kHop / ratio;
        // Keep what the next search window and the next continuation template can still reach.
        const int64_t keep = std::min(prev_ + kHop, static_cast<int64_t>(std::llround(pos_)) - kSeek);
        if (keep > base_) {
            in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>((keep - base_) * kChannels));
            base_ = keep;
        }
    }
}

}  // namespace ps2r::sound
