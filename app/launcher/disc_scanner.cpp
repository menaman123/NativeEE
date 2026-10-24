// app/launcher/disc_scanner.cpp -- the folder walk and the fingerprint check off the GUI thread.
#include "disc_scanner.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStorageInfo>
#include <QtConcurrent/QtConcurrentRun>
#include "iso_fingerprint.h"
namespace ps2r::launcher {

namespace {
constexpr qint64 kMinImageBytes = 32ll * 1024 * 1024;   // smaller than this is not a PS2 disc

void walk(const QString& folder, int depth, int maxDepth, const QStringList& extensions, QStringList* out) {
    QDir dir(folder);
    if (!dir.exists()) return;
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name);
    for (const QFileInfo& fi : entries) {
        if (fi.isSymLink()) continue;
        if (fi.isDir()) {
            if (depth < maxDepth && !fi.isHidden()) walk(fi.absoluteFilePath(), depth + 1, maxDepth, extensions, out);
            continue;
        }
        const QString name = fi.fileName().toLower();
        for (const QString& ext : extensions) {
            if (name.endsWith(ext.toLower()) && fi.size() >= kMinImageBytes) { *out << fi.absoluteFilePath(); break; }
        }
    }
}
}  // namespace

DiscScanner::DiscScanner(QObject* parent) : QObject(parent) {}

DiscScanner::~DiscScanner() {
    if (watcher_) { watcher_->waitForFinished(); delete watcher_; }
}

QStringList DiscScanner::removableVolumes() {
    QStringList out;
    for (const QStorageInfo& v : QStorageInfo::mountedVolumes()) {
        if (!v.isValid() || !v.isReady() || v.isRoot()) continue;
        const QString root = v.rootPath();
#ifdef Q_OS_MACOS
        if (!root.startsWith("/Volumes/")) continue;
        if (v.device().startsWith("/dev/disk") && v.fileSystemType() == "apfs" && v.bytesTotal() > 500ll * 1024 * 1024 * 1024) continue;   // an internal volume
#elif defined(Q_OS_WIN)
        if (root.compare("C:/", Qt::CaseInsensitive) == 0) continue;
#else
        if (!(root.startsWith("/media/") || root.startsWith("/run/media/") || root.startsWith("/mnt/"))) continue;
#endif
        out << root;
    }
    return out;
}

QVector<DiscFound> DiscScanner::scanNow(const QStringList& folders, int maxDepth, const QStringList& extensions,
                                        const QStringList& fingerprintFiles) {
    QStringList files;
    for (const QString& f : folders) walk(f, 0, maxDepth, extensions, &files);
    files.removeDuplicates();
    QVector<DiscFound> out;
    for (const QString& path : files) {
        QString matched, err;
        if (IsoFingerprint::identify(path, fingerprintFiles, &matched, &err) == IsoFingerprint::Match) {
            DiscFound d;
            d.path = path;
            d.fingerprintFile = matched;
            d.sizeBytes = QFileInfo(path).size();
            out << d;
        }
    }
    // the count of files looked at travels in a trailing sentinel-free way: the caller re-counts; keep it simple
    return out;
}

void DiscScanner::start(const QStringList& folders, bool removable, int maxDepth, const QStringList& extensions,
                        const QStringList& fingerprintFiles) {
    if (running_) return;
    running_ = true;
    QStringList all = folders;
    if (removable) all += removableVolumes();
    all.removeDuplicates();
    if (!watcher_) {
        watcher_ = new QFutureWatcher<QVector<DiscFound>>(this);
        connect(watcher_, &QFutureWatcherBase::finished, this, [this] {
            running_ = false;
            const QVector<DiscFound> found = watcher_->result();
            for (const DiscFound& d : found) emit this->found(d);
            emit finished(found.size(), 0);
        });
    }
    watcher_->setFuture(QtConcurrent::run([=] { return scanNow(all, maxDepth, extensions, fingerprintFiles); }));
}

}  // namespace ps2r::launcher
