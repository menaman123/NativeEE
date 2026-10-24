// app/launcher/iso_fingerprint.h -- identifies a disc image by what is ON it: the ISO 9660 root directory is read
// and the manifest's fingerprint file (the game's executable name on the disc) looked up. The file name of the
// image is never consulted. Reads at most a few sectors.
#pragma once
#include <QString>
#include <QStringList>
namespace ps2r::launcher {

class IsoFingerprint {
public:
    enum Result { Match, NotTheGame, NotAnIso, Unreadable };

    // Lists the root directory's file identifiers (upper case, the ;1 version stripped). False when the image is not
    // ISO 9660 or cannot be read; `error` says which.
    static bool rootEntries(const QString& path, QStringList* entries, QString* error);
    // Looks for one of `files` (case-insensitive) in the root directory. `matched` receives the file found.
    static Result identify(const QString& path, const QStringList& files, QString* matched, QString* error);
    // The volume identifier from the primary volume descriptor (trimmed), for the card's status line.
    static QString volumeId(const QString& path);
};

}  // namespace ps2r::launcher
