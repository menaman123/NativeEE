// app/launcher/disc_scanner.cpp -- the folder walk and the fingerprint check off the GUI thread.
#include "disc_identifier.h"
#include "disc_scanner.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStorageInfo>
#include <QtConcurrent/QtConcurrentRun>
#include "iso_fingerprint.h"
#include "title_manifest.h"
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

QVector<DiscIdentity> DiscScanner::scanNow(const TitleManifest* manifest, const QStringList& folders, int maxDepth) {
    QStringList files;
    for (const QString& f : folders) walk(f, 0, maxDepth, manifest->discExtensions(), &files);
    files.removeDuplicates();
    QVector<DiscIdentity> out;
    for (const QString& path : files) {
        const DiscIdentity d = DiscIdentifier::identify(*manifest, path);
        if (d.recognised()) out << d;
    }
    return out;
}

void DiscScanner::start(const TitleManifest* manifest, const QStringList& folders, bool removable, int maxDepth) {
    if (running_) return;
    running_ = true;
    QStringList all = folders;
    if (removable) all += removableVolumes();
    all.removeDuplicates();
    if (!watcher_) {
        watcher_ = new QFutureWatcher<QVector<DiscIdentity>>(this);
        connect(watcher_, &QFutureWatcherBase::finished, this, [this] {
            running_ = false;
            const QVector<DiscIdentity> found = watcher_->result();
            for (const DiscIdentity& d : found) emit this->found(d);
            emit finished(found.size(), 0);
        });
    }
    watcher_->setFuture(QtConcurrent::run([=] { return scanNow(manifest, all, maxDepth); }));
}

}  // namespace ps2r::launcher
