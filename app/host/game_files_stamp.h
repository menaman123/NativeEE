#pragma once
// GameFilesStamp -- the BUILD-ID stamp on the extracted game files (CHARTER 2
// C4). The first-run extractor (C2) writes `<game_files>/.nativeee_build_id`
// holding the build id of the binary that extracted the files; every start
// reads it back and WARNS when the files came from a different build (the
// extraction rules may have changed with the build), so an updated binary
// over an old data folder is attributable from the first line of the log.
// Absent stamp = files supplied by hand (the repository layout, the harness) or
// an extraction that never reached its last stage: reported in run.meta, never
// a warning at game start; the launcher's Ready page DOES warn on it (C2 review
// loop 1) and offers the re-extract.
#include <string>

namespace ps2r::app {

class GameFilesStamp {
public:
    static constexpr const char* kFileName = ".nativeee_build_id";

    enum class State { Absent, Match, Mismatch, Unreadable };
    struct Check {
        State state = State::Absent;
        std::string stamped;   // the build id the stamp carries ("" when absent)
        std::string path;
    };

    static std::string Path(const std::string& game_files_dir);
    // Reads the stamp and compares it with `build_id`.
    static Check Verify(const std::string& game_files_dir, const std::string& build_id);
    // Writes the stamp (the extractor's last step). False when the file could
    // not be written.
    static bool Write(const std::string& game_files_dir, const std::string& build_id);
    // The run.meta value: absent | ok | mismatch:<stamped> | unreadable.
    static std::string MetaValue(const Check& check);
    // The warning line for a mismatch ("" otherwise).
    static std::string Warning(const Check& check, const std::string& build_id);
};

}  // namespace ps2r::app
