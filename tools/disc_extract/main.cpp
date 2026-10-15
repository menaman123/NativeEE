// ps2r_disc_extract -- copy one file out of a PlayStation 2 disc image (ISO9660, plain
// .iso or raw 2352-byte .bin) and, optionally, refuse it unless its SHA-256 matches.
//
// The build uses it to pull the title's inputs (the ELF in the volume root, the IOP module
// under DATA/SYS/) out of the user's own disc image into the BUILD directory, where the
// recompilers run over them (cmake/TitleInputs.cmake). It reads the image through the same
// IsoFs the engine and the first-run launcher use, so a .bin is accepted as a .iso is.
//
//   ps2r_disc_extract <image> <path-on-disc> <out-file> [--sha256 <64 hex digits>]
//
// Exit codes: 0 extracted (and verified); 2 usage; 3 the image does not open or is not an
// ISO9660 volume; 4 the path is not on the volume; 5 the file could not be read whole or the
// output could not be written; 6 the SHA-256 does not match (the output file is removed).
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/cdvd/iso_fs.h"
#include "engine/core/hash/sha256.h"

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

int Usage() {
    std::fprintf(stderr,
                 "usage: ps2r_disc_extract <image.iso|image.bin> <path-on-disc> <out-file> [--sha256 <hex>]\n"
                 "  copies <path-on-disc> (e.g. SLUS_123.45 or DATA/SYS/STREAM.IRX) out of the disc image;\n"
                 "  with --sha256 the copy is kept only if its SHA-256 equals <hex>.\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) return Usage();
    const std::string image = argv[1];
    const std::string on_disc = argv[2];
    const std::string out_path = argv[3];
    std::string expected;
    for (int i = 4; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--sha256" && i + 1 < argc) {
            expected = Lower(argv[++i]);
        } else {
            return Usage();
        }
    }
    if (!expected.empty() && expected.size() != 2 * ps2r::hash::Sha256::kDigestBytes) {
        std::fprintf(stderr, "ps2r_disc_extract: --sha256 wants 64 hex digits, got %zu\n", expected.size());
        return 2;
    }

    IsoFs fs;
    std::string error;
    if (!fs.Open(image, &error)) {
        std::fprintf(stderr,
                     "ps2r_disc_extract: %s is not a PlayStation 2 disc image this tool can read (%s).\n"
                     "  Expected a .iso with 2048-byte sectors or a raw .bin with 2352-byte sectors; a .cue,\n"
                     "  .mdf, .nrg, CHD or compressed archive must be converted first.\n",
                     image.c_str(), error.empty() ? "cannot open" : error.c_str());
        return 3;
    }
    IsoFs::Entry entry;
    if (!fs.Find(on_disc, entry) || entry.directory) {
        std::fprintf(stderr, "ps2r_disc_extract: %s has no file %s (volume '%s').\n", image.c_str(),
                     on_disc.c_str(), fs.VolumeId().c_str());
        return 4;
    }

    std::vector<uint8_t> bytes(entry.size);
    const size_t got = entry.size == 0 ? 0 : fs.ReadBytes(static_cast<uint64_t>(entry.lba) * IsoFs::kSectorSize,
                                                          entry.size, bytes.data());
    if (got != entry.size) {
        std::fprintf(stderr, "ps2r_disc_extract: %s is truncated: %s read %zu of %u bytes.\n", image.c_str(),
                     on_disc.c_str(), got, entry.size);
        return 5;
    }
    const std::string digest = ps2r::hash::Sha256::HexOf(bytes.data(), bytes.size());
    if (!expected.empty() && digest != expected) {
        std::fprintf(stderr,
                     "ps2r_disc_extract: %s on %s is not the file this build was made from:\n"
                     "  SHA-256 %s (%u bytes), expected %s.\n"
                     "  Another revision / region, a patched file, or a damaged image.\n",
                     on_disc.c_str(), image.c_str(), digest.c_str(), entry.size, expected.c_str());
        return 6;
    }

    std::FILE* out = std::fopen(out_path.c_str(), "wb");
    if (!out) {
        std::fprintf(stderr, "ps2r_disc_extract: cannot write %s\n", out_path.c_str());
        return 5;
    }
    const bool written = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), out) == bytes.size();
    const bool closed = std::fclose(out) == 0;
    if (!written || !closed) {
        std::remove(out_path.c_str());
        std::fprintf(stderr, "ps2r_disc_extract: short write to %s\n", out_path.c_str());
        return 5;
    }
    std::printf("ps2r_disc_extract: %s -> %s (%u bytes, sha256 %s%s)\n", on_disc.c_str(), out_path.c_str(),
                entry.size, digest.c_str(), expected.empty() ? "" : ", verified");
    return 0;
}
