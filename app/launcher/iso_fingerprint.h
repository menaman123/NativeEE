// app/launcher/iso_fingerprint.h -- identifies a disc image by what is ON it: the ISO 9660 root directory is read
// and the manifest's fingerprint file (the game's executable name on the disc) looked up; the executable's bytes are
// then checksummed (SHA-256) so a modified image is told from the original. The file name of the image is never
// consulted. Reads a few sectors plus the executable.
#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
namespace ps2r::launcher {

struct IsoRootEntry {
    QString name;        // upper case, the ;1 version stripped
    quint32 lba = 0;     // the file's first sector
    quint32 length = 0;  // bytes
    bool directory = false;
};

class IsoFingerprint {
public:
    enum Result { Match, NotTheGame, NotAnIso, Unreadable };

    // The root directory's records. False when the image is not ISO 9660 or cannot be read; `error` says which.
    static bool rootRecords(const QString& path, QVector<IsoRootEntry>* entries, QString* error);
    // Lists the root directory's file identifiers (upper case, the ;1 version stripped).
    static bool rootEntries(const QString& path, QStringList* entries, QString* error);
    // Looks for one of `files` (case-insensitive) in the root directory. `matched` receives the file found.
    static Result identify(const QString& path, const QStringList& files, QString* matched, QString* error);
    // The SHA-256 (lower-case hex) of the root file `name`'s bytes; empty when the file is not there or unreadable.
    static QString fileSha256(const QString& path, const QString& name, QString* error = nullptr);
    // The volume identifier from the primary volume descriptor (trimmed), for the card's status line.
    static QString volumeId(const QString& path);
};

}  // namespace ps2r::launcher
