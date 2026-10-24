// app/launcher/save_state_list.cpp -- the save-state scan.
#include "save_state_list.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include "game_folder.h"
#include "title_manifest.h"
namespace ps2r::launcher {

SaveStateList::SaveStateList(const TitleManifest* manifest, const GameFolder* folder) : manifest_(manifest), folder_(folder) {}

QString SaveStateList::folder() const { return folder_->chosen() ? folder_->saveStatesFolder() : QString(); }

bool SaveStateList::readMeta(const QString& metaPath, quint64* frame, qint64* wallMs) {
    QFile f(metaPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    bool any = false;
    for (const QString& line : QString::fromUtf8(f.readAll()).split('\n')) {
        const int eq = line.indexOf('=');
        if (eq < 0) continue;
        const QString key = line.left(eq).trimmed(), value = line.mid(eq + 1).trimmed();
        if (key == QLatin1String("frame")) { *frame = value.toULongLong(); any = true; }
        else if (key == QLatin1String("wall_ms")) { *wallMs = value.toLongLong(); any = true; }
    }
    return any;
}

bool SaveStateList::scan() {
    entries_.clear();
    const QString dir = folder();
    if (dir.isEmpty() || !QDir(dir).exists()) return false;
    const QString ext = manifest_->saveStateExtension();
    for (const QFileInfo& fi : QDir(dir).entryInfoList(QDir::Files, QDir::Name)) {
        if (!fi.fileName().endsWith(ext, Qt::CaseInsensitive)) continue;   // the game's .tmp transients are not states
        SaveStateEntry e;
        e.path = fi.absoluteFilePath();
        e.name = fi.fileName().left(fi.fileName().size() - ext.size());
        e.sizeBytes = fi.size();
        e.when = fi.lastModified();
        quint64 frame = 0;
        qint64 wall = 0;
        if (readMeta(e.path + manifest_->saveStateMetaExtension(), &frame, &wall)) {
            e.frame = frame;
            if (wall > 0) e.when = QDateTime::fromMSecsSinceEpoch(wall);
        }
        const QString thumb = e.path + manifest_->saveStateThumbnailExtension();
        if (QFileInfo::exists(thumb)) e.thumbnailPath = thumb;
        entries_ << e;
    }
    std::sort(entries_.begin(), entries_.end(), [](const SaveStateEntry& a, const SaveStateEntry& b) {
        if (a.when != b.when) return a.when > b.when;
        return a.name < b.name;
    });
    return true;
}

}  // namespace ps2r::launcher
