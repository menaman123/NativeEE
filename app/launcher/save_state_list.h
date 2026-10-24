// app/launcher/save_state_list.h -- the save states in the game folder's saves: each file with its frame and the
// moment it was written (from the sidecar the game writes beside it), its size and the thumbnail beside it when the
// game left one. Pure file reading; newest first.
#pragma once
#include <QDateTime>
#include <QString>
#include <QVector>
namespace ps2r::launcher {
class GameFolder;
class TitleManifest;

struct SaveStateEntry {
    QString path;
    QString name;            // the file name without its extension
    QString thumbnailPath;   // empty when the game left none
    QDateTime when;          // the sidecar's clock, else the file's
    quint64 frame = 0;       // the game's frame count at the save (0 = unknown)
    qint64 sizeBytes = 0;
};

class SaveStateList {
public:
    SaveStateList(const TitleManifest* manifest, const GameFolder* folder);
    QString folder() const;
    // Reads the folder; false when it does not exist yet (an empty list, not an error).
    bool scan();
    const QVector<SaveStateEntry>& entries() const { return entries_; }
    static bool readMeta(const QString& metaPath, quint64* frame, qint64* wallMs);

private:
    const TitleManifest* manifest_;
    const GameFolder* folder_;
    QVector<SaveStateEntry> entries_;
};

}  // namespace ps2r::launcher
