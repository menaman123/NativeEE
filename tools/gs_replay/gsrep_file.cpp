// tools/gs_replay/gsrep_file.cpp -- the `.gsrep` writer / reader and the downsample.
#include "tools/gs_replay/gsrep_file.h"

#include <cstring>

namespace ps2r {
namespace tools {

GsrepFile::~GsrepFile() {
    if (f_) std::fclose(f_);
}

bool GsrepFile::Open(const char* path, uint32_t ds_n) {
    f_ = std::fopen(path, "wb");
    if (!f_) return false;
    hdr_ = GsrepHeader{};
    hdr_.magic = kMagic;
    hdr_.version = kVersion;
    hdr_.ds_n = ds_n;
    std::fwrite(&hdr_, sizeof(hdr_), 1, f_);
    return true;
}

void GsrepFile::AppendFrame(const GsrepFrame& fr, const std::vector<uint8_t>& ds) {
    if (!f_) return;
    std::fwrite(&fr, sizeof(fr), 1, f_);
    std::fwrite(ds.data(), 1, ds.size(), f_);
}

void GsrepFile::Close(uint32_t frame_count, uint64_t replay_hash, uint64_t total_draws,
                      uint64_t total_verts) {
    if (!f_) return;
    hdr_.frame_count = frame_count;
    hdr_.replay_hash = replay_hash;
    hdr_.total_draws = total_draws;
    hdr_.total_verts = total_verts;
    std::fseek(f_, 0, SEEK_SET);
    std::fwrite(&hdr_, sizeof(hdr_), 1, f_);
    std::fclose(f_);
    f_ = nullptr;
}

bool GsrepFile::Load(const char* path, Loaded& r) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return false;
    }
    if (std::fread(&r.hdr, sizeof(r.hdr), 1, f) != 1 || r.hdr.magic != kMagic ||
        r.hdr.version != kVersion || r.hdr.ds_n == 0 || r.hdr.ds_n > 512) {
        std::fprintf(stderr, "%s: not a gs_replay .gsrep artifact\n", path);
        std::fclose(f);
        return false;
    }
    const size_t ds_bytes = static_cast<size_t>(r.hdr.ds_n) * r.hdr.ds_n * 4;
    GsrepFrame gf;
    while (std::fread(&gf, sizeof(gf), 1, f) == 1) {
        std::vector<uint8_t> px(ds_bytes);
        if (std::fread(px.data(), 1, ds_bytes, f) != ds_bytes) break;
        r.frames.push_back(gf);
        r.ds.push_back(std::move(px));
    }
    std::fclose(f);
    return true;
}

// Cell [cy][cx] averages rows [cy*h/n, (cy+1)*h/n) x columns [cx*w/n, (cx+1)*w/n)
// (at least one row / column each), channel sums divided by the cell area with
// truncating integer division; rows are consumed in the buffer's own order.
void GsrepFile::Downsample(const std::vector<uint8_t>& rgba, int w, int h, uint32_t ds_n,
                           std::vector<uint8_t>& out) {
    out.assign(static_cast<size_t>(ds_n) * ds_n * 4, 0);
    if (w <= 0 || h <= 0 || rgba.size() < static_cast<size_t>(w) * h * 4) return;
    for (uint32_t cy = 0; cy < ds_n; cy++) {
        const int y0 = static_cast<int>(static_cast<uint64_t>(cy) * h / ds_n);
        int y1 = static_cast<int>(static_cast<uint64_t>(cy + 1) * h / ds_n);
        if (y1 <= y0) y1 = y0 + 1;
        for (uint32_t cx = 0; cx < ds_n; cx++) {
            const int x0 = static_cast<int>(static_cast<uint64_t>(cx) * w / ds_n);
            int x1 = static_cast<int>(static_cast<uint64_t>(cx + 1) * w / ds_n);
            if (x1 <= x0) x1 = x0 + 1;
            uint64_t s[4] = {0, 0, 0, 0};
            for (int y = y0; y < y1 && y < h; y++) {
                const uint8_t* row = rgba.data() + (static_cast<size_t>(y) * w + x0) * 4;
                for (int x = x0; x < x1 && x < w; x++) {
                    s[0] += row[0]; s[1] += row[1]; s[2] += row[2]; s[3] += row[3];
                    row += 4;
                }
            }
            const uint64_t n = static_cast<uint64_t>(y1 - y0) * static_cast<uint64_t>(x1 - x0);
            uint8_t* o = out.data() + (static_cast<size_t>(cy) * ds_n + cx) * 4;
            for (int c = 0; c < 4; c++) o[c] = static_cast<uint8_t>(s[c] / (n ? n : 1));
        }
    }
}

}  // namespace tools
}  // namespace ps2r
