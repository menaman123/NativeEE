#include "app/gui/fps_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "imgui.h"

namespace ps2r::app {
namespace {

struct FpsGlyphPoint { float x, y; };
struct FpsGlyphStroke { uint16_t first, count; };
struct FpsGlyph { char ch; float advance; uint16_t first_stroke, stroke_count; };
#include "app/gui/fps_glyphs.inc"

constexpr float kLean = 0.12f;           // the forward lean: x shifts by kLean per unit of height above the baseline
constexpr float kStroke = 0.16f;         // the fill stroke's width in cells
constexpr float kOutline = 0.07f;        // the outline's extra width on each side, in cells
constexpr float kSpaceAdvance = 5.0f;    // an unknown character's advance (half a digit cell)
constexpr float kLetterGap = 1.6f;       // between cells
constexpr int kMaxStrokePoints = 64;

const FpsGlyph* FindGlyph(char ch) {
    for (const FpsGlyph& g : kFpsGlyphs)
        if (g.ch == ch) return &g;
    return nullptr;
}

// One pass over a text: `col` and `width` select the outline or the fill pass. Returns the pen's x
// after the text (its right edge in pixels).
float DrawTextPass(ImDrawList* draw, float x, float baseline_y, float cell_h, const char* text, ImU32 col, float width) {
    const float unit = cell_h / kFpsGlyphCellHeight;
    ImVec2 pts[kMaxStrokePoints];
    for (const char* p = text; *p; ++p) {
        const FpsGlyph* g = FindGlyph(*p);
        if (!g) { x += kSpaceAdvance * unit; continue; }
        for (uint16_t s = 0; s < g->stroke_count; ++s) {
            const FpsGlyphStroke& st = kFpsGlyphStrokes[g->first_stroke + s];
            const int n = std::min<int>(st.count, kMaxStrokePoints);
            for (int i = 0; i < n; ++i) {
                const FpsGlyphPoint& q = kFpsGlyphPoints[st.first + i];
                const float lean = (kFpsGlyphCellHeight - q.y) * kLean;
                pts[i] = ImVec2(x + (q.x + lean) * unit, baseline_y - (kFpsGlyphCellHeight - q.y) * unit);
            }
            if (draw) {
                // Round caps (the polyline's ends are square); a two-point stroke of near-identical
                // points (the '.') is a dot of the cap alone.
                draw->AddCircleFilled(pts[0], width * 0.5f, col, 12);
                draw->AddCircleFilled(pts[n - 1], width * 0.5f, col, 12);
                if (n >= 2) draw->AddPolyline(pts, n, col, ImDrawFlags_None, width);
            }
        }
        x += (g->advance + kLetterGap) * unit;
    }
    return x;
}

void DrawText(ImDrawList* draw, float x, float baseline_y, float cell_h, const char* text, ImU32 fill, ImU32 outline) {
    const float unit = cell_h / kFpsGlyphCellHeight;
    const float fill_w = kStroke * kFpsGlyphCellHeight * unit;
    DrawTextPass(draw, x, baseline_y, cell_h, text, outline, fill_w + 2.0f * kOutline * kFpsGlyphCellHeight * unit);
    DrawTextPass(draw, x, baseline_y, cell_h, text, fill, fill_w);
}

}  // namespace

void FpsHistory::NoteShown(uint64_t now_ns) {
    times_[next_] = now_ns;
    next_ = (next_ + 1) % (kHistory + 1);
    if (count_ < kHistory + 1) ++count_;
    ++shown_;
}

FpsHistory::Stats FpsHistory::Compute(uint64_t now_ns) const {
    Stats s;
    if (count_ == 0) return s;
    // The ring's oldest entry first.
    const size_t oldest = count_ < kHistory + 1 ? 0 : next_;
    auto at = [&](size_t i) { return times_[(oldest + i) % (kHistory + 1)]; };
    // The gaps, oldest first, clamped to a visible maximum.
    for (size_t i = 1; i < count_; ++i) {
        const uint64_t a = at(i - 1), b = at(i);
        const float gap_ms = b > a ? static_cast<float>(b - a) * 1e-6f : 0.0f;
        s.gaps_ms[s.gap_count++] = gap_ms;
    }
    // The window: the pictures shown within the last kWindowNs. The figure is (n - 1) / span over
    // the pictures inside the window, so a steady 60 Hz reads 60.0 whatever the window's phase; a
    // window that holds one picture (or none) reads 0 -- and once the newest picture is older
    // than the window plus a period the counter is stale: 0.
    const uint64_t newest = at(count_ - 1);
    if (now_ns > newest && now_ns - newest > kWindowNs + 20000000) return s;
    size_t first = count_ - 1;
    while (first > 0 && newest - at(first - 1) <= kWindowNs) --first;
    const size_t n = count_ - first;
    if (n >= 2) {
        const uint64_t span = newest - at(first);
        if (span > 0) {
            s.fps = static_cast<float>(n - 1) * 1e9f / static_cast<float>(span);
            s.frame_ms = static_cast<float>(span) * 1e-6f / static_cast<float>(n - 1);
        }
    }
    return s;
}

uint32_t FpsColour(float fps) {
    if (fps >= 59.5f) return IM_COL32(74, 222, 92, 255);    // green
    if (fps >= 50.0f) return IM_COL32(255, 184, 40, 255);   // amber
    return IM_COL32(236, 64, 56, 255);                      // red
}

uint32_t GapColour(float gap_ms) {
    if (gap_ms <= 0.0f) return FpsColour(60.0f);
    return FpsColour(1000.0f / gap_ms);
}

float FpsTextWidth(const char* text, float cell_h) {
    return DrawTextPass(nullptr, 0.0f, 0.0f, cell_h, text, 0, 0.0f) - kLetterGap * cell_h / kFpsGlyphCellHeight;
}

float DrawFpsOverlay(ImDrawList* draw, float right_x, float top_y, float cell_h, const FpsHistory::Stats& stats,
                     const FpsOverlayTheme& theme) {
    const float small_h = cell_h * 0.5f;
    const float pad = cell_h * 0.35f;
    const float unit = cell_h / kFpsGlyphCellHeight;
    const float outline_px = kOutline * kFpsGlyphCellHeight * unit;
    char fps_text[16], ms_text[24];
    std::snprintf(fps_text, sizeof fps_text, "%d", static_cast<int>(std::lround(stats.fps)));
    std::snprintf(ms_text, sizeof ms_text, "%.1f", stats.frame_ms);

    // Layout: [ 60 fps ]  on the first line (the figure big, the unit small), [16.7 ms] under it
    // at the small size, then the sparkline (kHistory bars, 2 px each at a 28 px cell) with its
    // 60 Hz reference line.
    const float fps_w = FpsTextWidth(fps_text, cell_h);
    const float fps_unit_w = FpsTextWidth("fps", small_h);
    const float ms_w = FpsTextWidth(ms_text, small_h);
    const float ms_unit_w = FpsTextWidth("ms", small_h);
    const float spark_bar = std::max(1.0f, std::round(cell_h / 14.0f));
    const float spark_w = spark_bar * static_cast<float>(FpsHistory::kHistory);
    const float spark_h = cell_h * 0.9f;
    const float line1_w = fps_w + small_h * 0.45f + fps_unit_w;
    const float line2_w = ms_w + small_h * 0.45f + ms_unit_w;
    const float content_w = std::max({line1_w, line2_w, spark_w});
    const float panel_w = content_w + 2.0f * pad + 2.0f * outline_px;
    const float line1_h = cell_h + outline_px;
    const float line2_h = small_h + outline_px + cell_h * 0.2f;
    const float panel_h = pad + line1_h + line2_h + cell_h * 0.3f + spark_h + pad;
    const ImVec2 p0(right_x - panel_w, top_y), p1(right_x, top_y + panel_h);
    if (!draw) return panel_h;
    draw->AddRectFilled(p0, p1, theme.panel, cell_h * 0.3f);
    draw->AddRect(p0, p1, theme.panel_border, cell_h * 0.3f, ImDrawFlags_None, 1.0f);

    // Line 1: right-aligned.
    float baseline = top_y + pad + cell_h;
    float x = right_x - pad - outline_px - line1_w;
    DrawText(draw, x, baseline, cell_h, fps_text, FpsColour(stats.fps), theme.outline);
    DrawText(draw, x + fps_w + small_h * 0.45f, baseline, small_h, "fps", theme.label, theme.outline);
    // Line 2.
    baseline += line2_h;
    x = right_x - pad - outline_px - line2_w;
    DrawText(draw, x, baseline, small_h, ms_text, theme.label, theme.outline);
    DrawText(draw, x + ms_w + small_h * 0.45f, baseline, small_h, "ms", theme.label, theme.outline);
    // The sparkline: one bar per gap, the newest at the right; 50 ms = the full height.
    const float sx1 = right_x - pad - outline_px;
    const float sy1 = baseline + cell_h * 0.3f + spark_h;
    constexpr float kFullMs = 50.0f;
    for (size_t i = 0; i < stats.gap_count; ++i) {
        const float gap = std::min(stats.gaps_ms[i], kFullMs);
        const float h = std::max(1.0f, gap / kFullMs * spark_h);
        const float bx1 = sx1 - static_cast<float>(stats.gap_count - 1 - i) * spark_bar;
        draw->AddRectFilled(ImVec2(bx1 - spark_bar, sy1 - h), ImVec2(bx1, sy1), GapColour(stats.gaps_ms[i]));
    }
    const float ref_y = sy1 - (1000.0f / 60.0f) / kFullMs * spark_h;
    draw->AddLine(ImVec2(sx1 - spark_w, ref_y), ImVec2(sx1, ref_y), theme.spark_frame, 1.0f);
    draw->AddLine(ImVec2(sx1 - spark_w, sy1), ImVec2(sx1, sy1), theme.panel_border, 1.0f);
    return panel_h;
}

}  // namespace ps2r::app
