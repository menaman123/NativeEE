#include "app/host/disc_verify.h"

#include <cctype>
#include <cstdio>
#include <vector>

#include "engine/cdvd/iso_fs.h"
#include "engine/core/hash/sha256.h"

namespace ps2r::app {
namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string StripVersion(const std::string& id) {
    const size_t semi = id.find(';');
    return semi == std::string::npos ? id : id.substr(0, semi);
}

}  // namespace

bool DiscVerify::LooksLikeSerial(const std::string& identifier) {
    // ABCD_123.45 : four upper-case letters, '_', three digits, '.', two digits.
    const std::string id = StripVersion(identifier);
    if (id.size() != 11) return false;
    for (int i = 0; i < 4; i++)
        if (!std::isupper(static_cast<unsigned char>(id[i]))) return false;
    if (id[4] != '_' || id[8] != '.') return false;
    for (int i : {5, 6, 7, 9, 10})
        if (!std::isdigit(static_cast<unsigned char>(id[i]))) return false;
    return true;
}

std::string DiscVerify::RegionOf(const std::string& identifier) {
    if (!LooksLikeSerial(identifier)) return {};
    const std::string p = StripVersion(identifier).substr(0, 4);
    if (p == "SLUS" || p == "SCUS") return "NTSC-U";
    if (p == "SLES" || p == "SCES" || p == "SCED" || p == "SLED") return "PAL";
    if (p == "SLPM" || p == "SLPS" || p == "SCPS" || p == "SCAJ" || p == "SLAJ") return "NTSC-J";
    if (p == "SLKA" || p == "SCKA") return "NTSC-K";
    if (p == "SCPN" || p == "PAPX" || p == "PBPX" || p == "PCPX") return "demo/other";
    return "unknown region";
}

std::vector<std::string> DiscVerify::RootIdentifiers(const std::string& image) {
    std::vector<std::string> out;
    IsoFs fs;
    if (!fs.Open(image)) return out;
    std::vector<IsoFs::Entry> entries;
    if (!fs.List(fs.Root(), entries)) return out;
    for (const IsoFs::Entry& e : entries)
        if (!e.directory) out.push_back(StripVersion(e.name));
    return out;
}

DiscVerify::Report DiscVerify::Verify(const std::string& image, const std::string& serial,
                                      const std::string& expected_sha256) {
    Report r;
    IsoFs fs;
    std::string error;
    if (!fs.Open(image, &error)) {
        std::FILE* f = std::fopen(image.c_str(), "rb");
        if (!f) {
            r.state = State::NotFound;
            r.message = "Cannot open " + image + ".";
            return r;
        }
        std::fclose(f);
        r.state = State::NotIso9660;
        r.message = "Not a PlayStation 2 disc image: " + image +
                    " has no ISO9660 volume descriptor (expected a .iso with 2048-byte sectors or a raw .bin with "
                    "2352-byte sectors). A .cue, .mdf, .nrg, CHD or compressed archive must be converted to .iso/.bin first.";
        return r;
    }
    r.volume_id = fs.VolumeId();
    r.raw_sectors = fs.RawSectors();
    r.image_bytes = fs.ImageBytes();

    IsoFs::Entry elf;
    if (!fs.Find(serial, elf) || elf.directory) {
        // Which title / region is this?
        std::vector<IsoFs::Entry> entries;
        fs.List(fs.Root(), entries);
        for (const IsoFs::Entry& e : entries) {
            if (e.directory) continue;
            const std::string id = StripVersion(e.name);
            if (LooksLikeSerial(id)) { r.found_serial = id; r.region = RegionOf(id); break; }
        }
        if (r.found_serial.empty()) {
            r.state = State::NoTitle;
            r.message = "No PlayStation 2 title found on this disc image (volume '" + r.volume_id +
                        "'): the root carries no " + serial + " and no other title identifier. This build runs " +
                        serial + " (" + RegionOf(serial) + ").";
            return r;
        }
        r.state = State::WrongRegion;
        const std::string ours_region = RegionOf(serial);
        if (r.region == "PAL" || r.region == "NTSC-J" || r.region == "NTSC-K")
            r.message = r.region + " release not supported: the disc image carries " + r.found_serial + " (" + r.region +
                        ")" + (r.region == "PAL" ? " -- the PAL/other region builds of the title differ from the " +
                        serial + " ELF this binary recompiled; only the " + ours_region + " disc (" + serial + ") can be used." :
                        " -- this binary recompiles " + serial + " (" + ours_region + "); another region's ELF cannot run on it.");
        else
            r.message = "Wrong disc: the image carries " + r.found_serial + " (" + r.region + "), not " + serial +
                        " (this build's title, " + ours_region + ").";
        return r;
    }
    r.found_serial = StripVersion(elf.name);
    r.region = RegionOf(r.found_serial);
    r.elf_size = elf.size;
    r.elf_lba = elf.lba;
    // Hash the ELF whole through the volume's logical reads (raw-aware).
    hash::Sha256 h;
    std::vector<uint8_t> buf(64 * 1024);
    uint64_t pos = 0;
    while (pos < elf.size) {
        const size_t want = static_cast<size_t>(std::min<uint64_t>(buf.size(), elf.size - pos));
        const size_t got = fs.ReadBytes(static_cast<uint64_t>(elf.lba) * IsoFs::kSectorSize + pos, want, buf.data());
        if (got != want) {
            r.state = State::ReadError;
            r.message = "The disc image is truncated: " + serial + " could not be read whole (" + std::to_string(pos + got) +
                        " of " + std::to_string(elf.size) + " bytes).";
            return r;
        }
        h.Update(buf.data(), got);
        pos += got;
    }
    uint8_t digest[hash::Sha256::kDigestBytes];
    h.Finish(digest);
    r.elf_sha256 = hash::Sha256::Hex(digest);
    if (r.elf_sha256 != Lower(expected_sha256)) {
        r.state = State::HashMismatch;
        r.message = serial + " is present but it is not the ELF this build recompiled: SHA-256 " + r.elf_sha256 + " (" +
                    std::to_string(elf.size) + " bytes), expected " + Lower(expected_sha256) +
                    ". Another revision, a patched/modified ELF, or a damaged image.";
        return r;
    }
    r.state = State::Ok;
    r.message = "Disc " + serial + " (" + r.region + ") verified: ELF SHA-256 matches this build (" +
                std::to_string(elf.size) + " bytes); volume '" + r.volume_id + "', " +
                std::to_string(r.image_bytes >> 20) + " MB" + (r.raw_sectors ? ", raw 2352-byte sectors" : "") + ".";
    return r;
}

}  // namespace ps2r::app
