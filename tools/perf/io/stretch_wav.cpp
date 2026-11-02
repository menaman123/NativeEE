// stretch_wav IN.wav OUT.wav RATIO [--seek N] [--quantum N] [--ratios CSV]
// Runs TimeStretch over a 48 kHz stereo int16 wav in mixer quanta (800 frames) at a fixed ratio (or, with
// --ratios, the per-quantum ratios of a lane's audio_ring.csv `ratio` column: a controller's actual
// sequence) and the given seek range, writes the output wav, and prints the splice census: how many
// segments were not the exact continuation (the splices), those of 2 ms or more, their mean match score
// (1 = seamless), mean and max offset (frames). The offline arm of the FMV audio lane: the same guest
// audio through the stretch under two controllers' ratio sequences, or two seek ranges.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "engine/sound/time_stretch.h"

namespace {

bool ReadWav(const char* path, std::vector<float>& out) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    uint8_t hdr[44];
    if (std::fread(hdr, 1, 44, f) != 44) { std::fclose(f); return false; }
    std::vector<int16_t> pcm;
    int16_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, sizeof(int16_t), 4096, f)) > 0) pcm.insert(pcm.end(), buf, buf + n);
    std::fclose(f);
    out.resize(pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i) out[i] = static_cast<float>(pcm[i]) / 32768.0f;
    return true;
}

void Put32(uint8_t* p, uint32_t v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff; }
void Put16(uint8_t* p, uint16_t v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; }

bool WriteWav(const char* path, const std::vector<float>& v) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const uint32_t bytes = static_cast<uint32_t>(v.size() * 2);
    uint8_t h[44];
    std::memcpy(h, "RIFF", 4); Put32(h + 4, 36 + bytes); std::memcpy(h + 8, "WAVE", 4);
    std::memcpy(h + 12, "fmt ", 4); Put32(h + 16, 16); Put16(h + 20, 1); Put16(h + 22, 2); Put32(h + 24, 48000);
    Put32(h + 28, 48000 * 4); Put16(h + 32, 4); Put16(h + 34, 16); std::memcpy(h + 36, "data", 4); Put32(h + 40, bytes);
    std::fwrite(h, 1, 44, f);
    std::vector<int16_t> pcm(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        float x = v[i] * 32767.0f;
        if (x > 32767.0f) x = 32767.0f;
        if (x < -32768.0f) x = -32768.0f;
        pcm[i] = static_cast<int16_t>(x);
    }
    std::fwrite(pcm.data(), sizeof(int16_t), pcm.size(), f);
    std::fclose(f);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: stretch_wav IN.wav OUT.wav RATIO [--seek N] [--quantum N]\n"); return 2; }
    int seek = ps2r::sound::TimeStretch::kSeek, quantum = 800;
    std::vector<double> ratios;
    for (int i = 4; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--seek")) seek = std::atoi(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--quantum")) quantum = std::atoi(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--ratios")) {
            std::ifstream in(argv[i + 1]);
            std::string line;
            int col = -1;
            while (std::getline(in, line)) {
                std::stringstream ss(line);
                std::string cell;
                int c = 0;
                if (col < 0) {   // the header: find `ratio`
                    while (std::getline(ss, cell, ',')) { if (cell == "ratio") col = c; ++c; }
                    if (col < 0) { std::fprintf(stderr, "no ratio column in %s\n", argv[i + 1]); return 1; }
                    continue;
                }
                while (std::getline(ss, cell, ',')) { if (c == col) ratios.push_back(std::atof(cell.c_str())); ++c; }
            }
        }
    }
    const double ratio = std::atof(argv[3]);
    std::vector<float> in;
    if (!ReadWav(argv[1], in)) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    ps2r::sound::TimeStretch ts(seek);
    std::vector<float> out;
    const size_t frames = in.size() / 2;
    size_t q = 0;
    for (size_t f = 0; f + static_cast<size_t>(quantum) <= frames; f += static_cast<size_t>(quantum), ++q)
        ts.Process(in.data() + f * 2, quantum, q < ratios.size() ? ratios[q] : ratio, out);
    if (!WriteWav(argv[2], out)) { std::fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
    const uint64_t hops = ts.FramesOut() / ps2r::sound::TimeStretch::kHop;
    std::printf("stretch_wav: ratio %.4f seek %d: in %llu frames -> out %llu (%.4f); hops %llu splices %llu (%.1f%%; one per %.2f s) mean score %.3f mean offset %.0f frames"
                " max %llu; >= 2 ms: %llu\n",
                ratio, ts.Seek(), static_cast<unsigned long long>(ts.FramesIn()), static_cast<unsigned long long>(ts.FramesOut()),
                ts.FramesIn() ? static_cast<double>(ts.FramesOut()) / static_cast<double>(ts.FramesIn()) : 0.0, static_cast<unsigned long long>(hops),
                static_cast<unsigned long long>(ts.Splices()), hops ? 100.0 * static_cast<double>(ts.Splices()) / static_cast<double>(hops) : 0.0,
                ts.Splices() ? static_cast<double>(ts.FramesOut()) / 48000.0 / static_cast<double>(ts.Splices()) : 0.0,
                ts.Splices() ? ts.SpliceScoreSum() / static_cast<double>(ts.Splices()) : 0.0,
                ts.Splices() ? static_cast<double>(ts.SpliceOffsetSum()) / static_cast<double>(ts.Splices()) : 0.0,
                static_cast<unsigned long long>(ts.SpliceOffsetMax()), static_cast<unsigned long long>(ts.LargeSplices()));
    return 0;
}
