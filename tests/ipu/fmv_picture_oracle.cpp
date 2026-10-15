// tests/ipu/fmv_picture_oracle.cpp -- FmvPictureOracle: demux + decode .PSS movies
// through the same PssDemux / FfmpegVideoDecoder classes MpegApi uses and hash every
// decoded picture; in `--manifest` mode assert the OLD per-picture golden (the
// sha256 of each served picture's P6 PPM, movie 1 then movie 2 in one index space)
// is a contiguous WINDOW of movie 1's decoded pictures reaching its last picture,
// followed by a contiguous WINDOW of movie 2's reaching its last picture (the
// windows start where the game's post-Reset pump resumed: k1/k2 are reported).
//
//   fmv_picture_oracle --avcodec-version
//   fmv_picture_oracle --og-root <dir> --movie <rel> [--movie <rel> ...]
//                      [--manifest <file>] [--dump-dir <dir>]
//
// `<rel>` is relative to <og-root>/game_files/example_title.rep/. The P6 bytes
// are `P6\n<w> <h>\n255\n` + RGB triplets row-major (alpha dropped), exactly the
// OLD dump layout; the FNV-1a64 of the RGBA bytes is printed alongside.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "engine/ipu/hle/decoded_picture.h"
#include "engine/ipu/hle/ffmpeg_video_decoder.h"
#include "engine/ipu/hle/pss_demux.h"
#include "engine/ipu/hle/pss_demux_listener.h"
#include "file_fmv_sector_source.h"

using ps2r::ipu::DecodedPicture;
using ps2r::ipu::FfmpegVideoDecoder;

using ps2r::ipu::PssDemux;
using ps2r::ipu::PssDemuxListener;

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4), test-side only.
// ---------------------------------------------------------------------------
namespace {

struct Sha256State {
    uint32_t h[8];
    uint64_t length = 0;
    uint8_t block[64];
    size_t block_len = 0;
};

const uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t Rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

void Sha256Init(Sha256State& s) {
    const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::memcpy(s.h, init, sizeof(init));
    s.length = 0;
    s.block_len = 0;
}

void Sha256Block(Sha256State& s, const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t(p[4 * i]) << 24) | (uint32_t(p[4 * i + 1]) << 16) |
               (uint32_t(p[4 * i + 2]) << 8) | uint32_t(p[4 * i + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s.h[0], b = s.h[1], c = s.h[2], d = s.h[3];
    uint32_t e = s.h[4], f = s.h[5], g = s.h[6], h = s.h[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + S1 + ch + kSha256K[i] + w[i];
        const uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    s.h[0] += a; s.h[1] += b; s.h[2] += c; s.h[3] += d;
    s.h[4] += e; s.h[5] += f; s.h[6] += g; s.h[7] += h;
}

void Sha256Update(Sha256State& s, const uint8_t* data, size_t n) {
    s.length += n;
    while (n > 0) {
        const size_t take = std::min(n, 64 - s.block_len);
        std::memcpy(s.block + s.block_len, data, take);
        s.block_len += take;
        data += take;
        n -= take;
        if (s.block_len == 64) {
            Sha256Block(s, s.block);
            s.block_len = 0;
        }
    }
}

std::string Sha256Final(Sha256State& s) {
    const uint64_t bits = s.length * 8;
    const uint8_t one = 0x80;
    Sha256Update(s, &one, 1);
    s.length -= 1;
    const uint8_t zero = 0;
    while (s.block_len != 56) {
        Sha256Update(s, &zero, 1);
        s.length -= 1;
    }
    uint8_t len[8];
    for (int i = 0; i < 8; ++i) len[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
    Sha256Update(s, len, 8);
    char hex[65];
    for (int i = 0; i < 8; ++i) std::snprintf(hex + 8 * i, 9, "%08x", s.h[i]);
    return std::string(hex, 64);
}

uint64_t Fnv1a64(const uint8_t* data, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) {
        h ^= data[i];
        h *= 1099511628211ull;
    }
    return h;
}

// The OLD dump layout: header + RGB triplets (alpha dropped), row-major.
std::vector<uint8_t> PpmBytes(const DecodedPicture& f) {
    char hdr[64];
    const int n = std::snprintf(hdr, sizeof(hdr), "P6\n%d %d\n255\n", f.width, f.height);
    std::vector<uint8_t> out(hdr, hdr + n);
    const size_t pixels = static_cast<size_t>(f.width) * static_cast<size_t>(f.height);
    out.reserve(out.size() + pixels * 3);
    for (size_t i = 0; i < pixels; ++i) {
        out.push_back(f.rgba[i * 4 + 0]);
        out.push_back(f.rgba[i * 4 + 1]);
        out.push_back(f.rgba[i * 4 + 2]);
    }
    return out;
}

struct PictureRecord {
    int width = 0;
    int height = 0;
    std::string ppm_sha256;
    uint64_t rgba_fnv = 0;
};

struct ManifestRow {
    int index = 0;
    std::string sha256;
    int width = 0;
    int height = 0;
};

struct Manifest {
    std::string avcodec_version;
    int served_total = -1;
    std::vector<ManifestRow> rows;
};

bool ReadManifest(const std::string& path, Manifest& m) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') {
            std::istringstream hs(line.substr(1));
            std::string tok;
            while (hs >> tok) {
                const size_t eq = tok.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = tok.substr(0, eq), val = tok.substr(eq + 1);
                if (key == "avcodec_version") m.avcodec_version = val;
                if (key == "served_total") m.served_total = std::atoi(val.c_str());
            }
            continue;
        }
        std::istringstream rs(line);
        ManifestRow r;
        if (!(rs >> r.index >> r.sha256 >> r.width >> r.height)) return false;
        m.rows.push_back(r);
    }
    return true;
}

}  // namespace

// The ONE struct with member functions in this TU: the demux listener that owns
// the decoder and records every picture (the same feed rule MpegContext applies:
// every video ES payload goes to the decoder in demux order; the sequence end
// flushes the decoder).
struct FmvPictureOracle : public PssDemuxListener {
    FfmpegVideoDecoder decoder;
    std::deque<DecodedPicture> pending;
    std::vector<PictureRecord> records;
    std::string dump_dir;
    int movie_index = 0;

    void OnVideoEs(const uint8_t* data, size_t size, bool) override {
        decoder.Feed(data, size, pending);
        Drain();
    }
    void OnAudioPes(const uint8_t*, size_t) override {}
    void OnProgramEnd() override {}

    void Drain() {
        while (!pending.empty()) {
            const DecodedPicture& f = pending.front();
            PictureRecord r;
            r.width = f.width;
            r.height = f.height;
            const std::vector<uint8_t> ppm = PpmBytes(f);
            Sha256State s;
            Sha256Init(s);
            Sha256Update(s, ppm.data(), ppm.size());
            r.ppm_sha256 = Sha256Final(s);
            r.rgba_fnv = Fnv1a64(f.rgba.data(), f.rgba.size());
            if (!dump_dir.empty()) {
                char name[256];
                std::snprintf(name, sizeof(name), "%s/movie%d_pic_%04d.ppm", dump_dir.c_str(), movie_index,
                              static_cast<int>(records.size()));
                if (FILE* fp = std::fopen(name, "wb")) {
                    std::fwrite(ppm.data(), 1, ppm.size(), fp);
                    std::fclose(fp);
                }
            }
            records.push_back(r);
            pending.pop_front();
        }
    }

    // Feeds the whole file in 16-sector pumps (as MpegApi::PumpUntilFrame does);
    // stops at the video sequence end (PssDemux::Ended) or the file end, then flushes.
    bool Decode(const std::string& path) {
        FileFmvSectorSource src;
        if (!src.Open(path)) return false;
        PssDemux demux(*this);
        std::vector<uint8_t> buf(16 * FileFmvSectorSource::kSectorBytes);
        while (!demux.Ended()) {
            const uint32_t got = src.PumpSectors(buf.data(), 16);
            if (got == 0) {
                demux.Process(true);
                break;
            }
            demux.Append(buf.data(), static_cast<size_t>(got) * FileFmvSectorSource::kSectorBytes);
            demux.Process(false);
        }
        decoder.Flush(pending);
        Drain();
        decoder.Shutdown();
        return true;
    }
};

static void Usage() {
    std::fprintf(stderr,
                 "usage: fmv_picture_oracle --avcodec-version | --og-root <dir> --movie <rel> "
                 "[--movie <rel>...] [--manifest <file>] [--dump-dir <dir>]\n");
}

int main(int argc, char** argv) {
    std::string og_root, manifest_path, dump_dir;
    std::vector<std::string> movies;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--avcodec-version") {
            std::printf("%s\n", FfmpegVideoDecoder::LibraryVersion().c_str());
            return 0;
        }
        if (i + 1 >= argc) {
            Usage();
            return 2;
        }
        const std::string v = argv[++i];
        if (a == "--og-root") og_root = v;
        else if (a == "--movie") movies.push_back(v);
        else if (a == "--manifest") manifest_path = v;
        else if (a == "--dump-dir") dump_dir = v;
        else {
            Usage();
            return 2;
        }
    }
    if (og_root.empty() || movies.empty()) {
        Usage();
        return 2;
    }

    const std::string version = FfmpegVideoDecoder::LibraryVersion();
    std::printf("avcodec_version=%s\n", version.c_str());

    std::vector<std::vector<PictureRecord>> per_movie;
    for (size_t k = 0; k < movies.size(); ++k) {
        const std::string path = og_root + "/game_files/example_title.rep/" + movies[k];
        FmvPictureOracle oracle;
        oracle.dump_dir = dump_dir;
        oracle.movie_index = static_cast<int>(k + 1);
        if (!oracle.Decode(path)) {
            std::printf("SKIP: movie not readable: %s\n", path.c_str());
            return 0;
        }
        std::printf("movie%zu %s decoded=%zu", k + 1, movies[k].c_str(), oracle.records.size());
        if (!oracle.records.empty()) {
            std::printf(" first=%dx%d", oracle.records.front().width, oracle.records.front().height);
        }
        std::printf("\n");
        if (manifest_path.empty()) {
            for (size_t i = 0; i < oracle.records.size(); ++i) {
                const PictureRecord& r = oracle.records[i];
                std::printf("m%zu %zu %dx%d sha256=%s fnv=%016llx\n", k + 1, i, r.width, r.height,
                            r.ppm_sha256.c_str(), static_cast<unsigned long long>(r.rgba_fnv));
            }
        }
        per_movie.push_back(std::move(oracle.records));
    }
    if (manifest_path.empty()) return 0;

    Manifest m;
    if (!ReadManifest(manifest_path, m)) {
        std::printf("FAIL: cannot read manifest %s\n", manifest_path.c_str());
        return 1;
    }
    if (m.avcodec_version != version) {
        std::printf("META: avcodec %s != fixture %s\nfmv_picture_oracle: FAIL\n", version.c_str(),
                    m.avcodec_version.c_str());
        return 1;
    }
    const size_t total = m.rows.size();
    if (m.served_total != static_cast<int>(total)) {
        std::printf("FAIL: manifest served_total=%d but %zu rows\n", m.served_total, total);
        return 1;
    }
    for (size_t i = 0; i < total; ++i) {
        if (m.rows[i].index != static_cast<int>(i)) {
            std::printf("FAIL: manifest index %d at row %zu (non-contiguous)\n", m.rows[i].index, i);
            return 1;
        }
    }
    if (per_movie.size() != 2) {
        std::printf("FAIL: compare mode needs exactly two movies (got %zu)\n", per_movie.size());
        return 1;
    }
    const std::vector<PictureRecord>& h1 = per_movie[0];
    const std::vector<PictureRecord>& h2 = per_movie[1];

    // The served sequence is a contiguous WINDOW of each movie's decoded pictures
    // (the game's first demux window is discarded by its sceMpegReset before the
    // first GetPicture, so the window starts at k > 0 and runs to the movie's end).
    // k1 = the decoded index of manifest[0] in movie 1; s1 = the run length from
    // there; then k2/s2 likewise for the rest of the manifest against movie 2.
    int failures = 0;
    size_t k1 = 0;
    while (k1 < h1.size() && h1[k1].ppm_sha256 != m.rows[0].sha256) ++k1;
    if (k1 == h1.size()) {
        std::printf("FAIL: manifest[0] is not a movie-1 picture\n");
        ++failures;
        k1 = 0;
    }
    size_t s1 = 0;
    while (s1 < total && k1 + s1 < h1.size() && m.rows[s1].sha256 == h1[k1 + s1].ppm_sha256) ++s1;
    const size_t s2 = total - s1;
    size_t k2 = 0;
    if (s2 > 0) {
        while (k2 < h2.size() && h2[k2].ppm_sha256 != m.rows[s1].sha256) ++k2;
        if (k2 == h2.size()) {
            std::printf("FAIL: manifest[%zu] is neither movie 1's next picture nor a movie-2 picture\n", s1);
            ++failures;
            k2 = 0;
        }
    }
    if (k2 + s2 > h2.size()) {
        std::printf("FAIL: manifest needs movie-2 pictures %zu..%zu, only %zu decoded\n", k2, k2 + s2 - 1, h2.size());
        ++failures;
    }
    for (size_t i = 0; i < s2 && k2 + i < h2.size(); ++i) {
        const ManifestRow& row = m.rows[s1 + i];
        const PictureRecord& r = h2[k2 + i];
        if (row.sha256 != r.ppm_sha256 || row.width != r.width || row.height != r.height) {
            if (failures < 10) {
                std::printf("FAIL: manifest %zu (%dx%d %s) != movie2 picture %zu (%dx%d %s)\n", s1 + i,
                            row.width, row.height, row.sha256.c_str(), k2 + i, r.width, r.height,
                            r.ppm_sha256.c_str());
            }
            ++failures;
        }
    }
    for (size_t i = 0; i < s1; ++i) {
        if (m.rows[i].width != h1[k1 + i].width || m.rows[i].height != h1[k1 + i].height) {
            std::printf("FAIL: manifest %zu dims %dx%d != movie1 %dx%d\n", i, m.rows[i].width, m.rows[i].height,
                        h1[k1 + i].width, h1[k1 + i].height);
            ++failures;
        }
    }
    if (s1 == 0) {
        std::printf("FAIL: movie 1 contributes no pictures\n");
        ++failures;
    }
    // The served windows run to the end of each movie (the game plays both out).
    if (k1 + s1 != h1.size()) {
        std::printf("FAIL: movie-1 window %zu..%zu does not reach the last decoded picture %zu\n", k1, k1 + s1 - 1,
                    h1.size() - 1);
        ++failures;
    }
    if (s2 > 0 && k2 + s2 != h2.size()) {
        std::printf("FAIL: movie-2 window %zu..%zu does not reach the last decoded picture %zu\n", k2, k2 + s2 - 1,
                    h2.size() - 1);
        ++failures;
    }
    std::printf("movie1 decoded=%zu served=%zu (decoded %zu..%zu); movie2 decoded=%zu served=%zu (decoded %zu..%zu); %s\n",
                h1.size(), s1, k1, k1 + s1 - 1, h2.size(), s2, k2, k2 + s2 - 1, failures ? "WINDOW FAIL" : "WINDOW OK");
    if (failures) {
        std::printf("fmv_picture_oracle: FAIL (%d)\n", failures);
        return 1;
    }
    std::printf("WINDOW OK k1=%zu s1=%zu k2=%zu s2=%zu served_total=%zu decoded1=%zu decoded2=%zu%s\n", k1, s1, k2,
                s2, total, h1.size(), h2.size(), (k1 == 0 && k2 == 0) ? " (PREFIX)" : "");
    std::printf("fmv_picture_oracle: PASS\n");
    return 0;
}
