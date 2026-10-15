#pragma once
// DiscVerify -- the disc image check of the first-run launcher (CHARTER 2 C2).
//
// The user drops a disc image; before anything is extracted the image must
// (1) be an ISO9660 volume (plain .iso or raw 2352 .bin -- IsoFs tells), (2)
// carry the title's ELF (SLUS_123.45) in its root, and (3) that ELF's SHA-256
// must equal the ELF this binary recompiled (TitleProfile::elf_sha256, or
// `launcher.elf_sha256` when a test injects a fake). Anything else is refused
// with an EXACT report: not ISO9660; no PlayStation 2 title found; another
// region's or another title's ELF (SLES_525.68 -> "PAL release -- not
// supported: this build recompiles SLUS_123.45 (NTSC-U)"); the right serial
// with another hash (a modified/other-revision ELF). The report is a plain
// string the launcher shows verbatim and the CLI prints.
#include <cstdint>
#include <string>
#include <vector>

namespace ps2r::app {

class DiscVerify {
public:
    enum class State {
        Ok,
        NotFound,          // the path does not open
        NotIso9660,        // no primary volume descriptor
        NoTitle,           // no SLxx_/SCxx_ identifier in the root
        WrongRegion,       // another region's / another title's serial present, ours absent
        HashMismatch,      // SLUS_123.45 present, its SHA-256 differs
        ReadError,         // the ELF could not be read whole
    };
    struct Report {
        State state = State::NotFound;
        std::string message;        // the user-facing report (one or two sentences)
        std::string volume_id;
        bool raw_sectors = false;   // .bin layout
        uint64_t image_bytes = 0;   // logical (2048/sector)
        std::string found_serial;   // the SLxx_xxx.xx identifier found (any region)
        std::string region;         // NTSC-U | PAL | NTSC-J | ... for found_serial
        std::string elf_sha256;     // of the ELF read (when read)
        uint32_t elf_size = 0;
        uint32_t elf_lba = 0;
    };

    // The region name a PS2 serial prefix implies ("" when not a serial).
    static std::string RegionOf(const std::string& identifier);
    // True for a PlayStation 2 disc-id-shaped identifier (ABCD_123.45[;1]).
    static bool LooksLikeSerial(const std::string& identifier);

    // image: the disc image path; serial: the title ELF's identifier
    // ("SLUS_123.45"); expected_sha256: 64 hex digits (lower/upper case).
    static Report Verify(const std::string& image, const std::string& serial, const std::string& expected_sha256);

private:
    static std::vector<std::string> RootIdentifiers(const std::string& image);
};

}  // namespace ps2r::app
