// tools/prepare/ui_strings.h -- EVERY string the prepare pipeline shows a player, in one place.
//
// The launcher shows one progress bar and one line; this file is the whole vocabulary of that
// line. Rules (the launcher stage's lint, tools/prepare/lint_ui_strings.py, enforces them over
// this file and over the launcher's own sources): plain words, no codenames, no developer
// vocabulary -- none of: recompile, compile, link, build, shard, AOT, cmake, binary, ELF, ISO,
// hash, manifest, bundle, toolchain, SDK, download(ing) as a noun of art. "Preparing your game"
// is the only name for the prepare step. Error texts say what the player can do next.
#pragma once

namespace ps2r::prepare::ui {

// Phase lines (progress()). %1 / %2 are the done / total counters of the long phase.
inline constexpr const char* kGettingFiles = "Getting the files your game needs";
inline constexpr const char* kUnpacking = "Unpacking";
inline constexpr const char* kCheckingDisc = "Checking your disc";
inline constexpr const char* kPreparing = "Preparing your game";
inline constexpr const char* kPreparingCounted = "Preparing your game (%1 of %2)";
inline constexpr const char* kAlmostDone = "Almost done";
inline constexpr const char* kReady = "Your game is ready";

// Failure lines (failed()). Each names the next step the player can take.
inline constexpr const char* kFailNoInternet =
    "Couldn't reach the server to get the files your game needs. Check your internet connection and try again.";
inline constexpr const char* kFailBadDownload =
    "A file didn't arrive complete. Try again; it picks up where it left off.";
inline constexpr const char* kFailNoRelease =
    "This version of the game isn't available for your computer yet.";
inline constexpr const char* kFailDiscUnreadable =
    "Couldn't read your disc image. Make sure the file is a complete copy and try again.";
inline constexpr const char* kFailWrongDisc =
    "This disc image isn't the edition this game was made from (another region or a modified copy). "
    "Use a complete copy of the original disc.";
inline constexpr const char* kFailNoDeveloperTools =
    "Your computer is missing a system component this game needs once, to prepare itself. "
    "Get Apple's developer tools first (open Terminal and run: xcode-select --install), then try again.";
inline constexpr const char* kFailPrepare =
    "Something went wrong while preparing your game. Try again; if it happens twice, send the details below.";
inline constexpr const char* kFailDiskFull =
    "There isn't enough free space in the game folder. Free some space and try again.";
inline constexpr const char* kFailCancelled = "Stopped. Nothing was changed.";

}  // namespace ps2r::prepare::ui
