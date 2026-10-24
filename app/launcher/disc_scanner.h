// app/launcher/disc_scanner.h -- finds disc images in the usual places (the manifest's folders, removable drives)
// and identifies each by its content, on a worker thread; the pages receive one card per recognised image (supported
// or not) as they are found and never block.
#pragma once
#include <QFutureWatcher>
#include <QObject>
#include <QStringList>
#include <QVector>
#include "disc_identity.h"
namespace ps2r::launcher {
class TitleManifest;

class DiscScanner : public QObject {
    Q_OBJECT
public:
    explicit DiscScanner(QObject* parent = nullptr);
    ~DiscScanner() override;

    // The candidate files under `folders` (to `maxDepth`) and, when `removable`, every mounted removable volume;
    // the manifest's extensions filter by suffix before any file is read. The manifest must outlive the scan.
    void start(const TitleManifest* manifest, const QStringList& folders, bool removable, int maxDepth);
    bool running() const { return running_; }
    static QStringList removableVolumes();
    // The synchronous half (the worker calls it): every recognised image in the folders.
    static QVector<DiscIdentity> scanNow(const TitleManifest* manifest, const QStringList& folders, int maxDepth);

signals:
    void found(const DiscIdentity& disc);
    void finished(int foundCount, int filesLooked);

private:
    QFutureWatcher<QVector<DiscIdentity>>* watcher_ = nullptr;
    bool running_ = false;
};

}  // namespace ps2r::launcher
