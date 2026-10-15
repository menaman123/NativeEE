// gs/hle/present/presenter.cpp -- the present step.
#include "engine/gs/hle/present/presenter.h"

namespace ps2r {
namespace gs {

Presenter::Presenter(ScanoutComposer& scanout) : scanout_(scanout) {}

void Presenter::RestoreTimeline(uint32_t display_frame, uint32_t game_frame) {
    frame_count_ = display_frame;
    ee_frame_count_ = game_frame;
    last_ = {};
}

ScanoutComposer::DrawResult Presenter::Present(VramImage& vram, const VramLayout& layout,
                                               const CrtcClassifier::Snapshot& crtc,
                                               uint64_t gfx_epoch, int window_w, int window_h) {
    last_ = scanout_.DrawScanout(vram, layout, crtc, frame_count_, gfx_epoch,
                                 render::Rect{0, 0, window_w, window_h});
    ++frame_count_;
    return last_;
}

void Presenter::PresentBlank(int window_w, int window_h) {
    scanout_.PresentBlank(render::Rect{0, 0, window_w, window_h});
    last_ = ScanoutComposer::DrawResult{};
    ++frame_count_;
}

void Presenter::RecordOwnedPresent(uint32_t source_key) {
    last_ = {};
    last_.source = ScanoutComposer::Source::Target;
    last_.fbp = source_key;
    ++frame_count_;
}

}  // namespace gs
}  // namespace ps2r
