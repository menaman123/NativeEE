// app/launcher/disc_identifier.h -- decides what a disc image is (DiscIdentity) from its content against the manifest:
// the root directory's file set, the executable's name and its checksum. Never refuses silently: every outcome is a
// DiscIdentity with a plain reason.
#pragma once
#include "disc_identity.h"
namespace ps2r::launcher {
class TitleManifest;

class DiscIdentifier {
public:
    // Reads the image and classifies it. Never throws; an unreadable file is Unreadable with the reason.
    static DiscIdentity identify(const TitleManifest& manifest, const QString& path);
    // The card's second line: "NTSC-U (North America) · <disc id> · file.iso".
    static QString subtitle(const DiscIdentity& d);
};

}  // namespace ps2r::launcher
