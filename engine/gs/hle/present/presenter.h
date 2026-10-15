// gs/hle/present/presenter.h -- Presenter: the per-VSync order the module gate
// exercises -- the EE game-frame mark, the display grab hook, the display quad
// (ScanoutComposer::DrawScanout: source pick, mirror maintenance, Backend::Present).
// Pacing, decimation and interpolation are integration policy and are not here.
#pragma once

#include <cstdint>
#include <vector>

#include "engine/gs/hle/present/crtc_classifier.h"
#include "engine/gs/hle/present/scanout_composer.h"
#include "engine/gs/hle/targets/vram_layout.h"
#include "engine/gs/vram/vram_image.h"
#include "engine/render/backend.h"

namespace ps2r {
namespace gs {

class Presenter {
public:
    explicit Presenter(ScanoutComposer& scanout);   // the composer owns the backend call

    // VSync with arg1 == 1: the EE finished a game frame.
    void MarkEeFrameEnd() { ++ee_frame_count_; }
    uint32_t EeFrameCount() const { return ee_frame_count_; }
    uint32_t FrameCount() const { return frame_count_; }
    // Target-age/clear timeline only. Does not present, grab, pace or credit
    // any application's frame/renderer execution census.
    void RestoreTimeline(uint32_t display_frame, uint32_t game_frame);

    // Grab the display bytes (see ScanoutComposer::GrabDisplay; `vram` enables the
    // local-memory arm under the composer's grab policy); false when none.
    bool Grab(std::vector<uint8_t>* out, int* w, int* h, const VramImage* vram = nullptr) {
        return scanout_.GrabDisplay(out, w, h, vram);
    }
    // The display quad into the window (after the grab) and the frame counter advance.
    ScanoutComposer::DrawResult Present(VramImage& vram, const VramLayout& layout,
                                        const CrtcClassifier::Snapshot& crtc, uint64_t gfx_epoch,
                                        int window_w, int window_h);
    // The swap alone (ScanoutComposer::PresentBlank) and the frame counter advance:
    // the corpus replay's present, which draws no display quad and moves no mirror
    // byte. LastPresent reads Source::None afterwards.
    void PresentBlank(int window_w, int window_h);
    // Accounting for the application's explicit resource presentation at the
    // same frame boundary. This performs no swap or scanout/model lookup.
    void RecordOwnedPresent(uint32_t source_key);
    const ScanoutComposer::DrawResult& LastPresent() const { return last_; }

private:
    ScanoutComposer& scanout_;
    uint32_t ee_frame_count_ = 0;
    uint32_t frame_count_ = 0;
    ScanoutComposer::DrawResult last_;
};

}  // namespace gs
}  // namespace ps2r
