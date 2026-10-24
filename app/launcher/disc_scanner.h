// app/launcher/disc_scanner.h -- finds disc images in the usual places (the manifest's folders, the Games folder,
// removable drives) and identifies each by its fingerprint, on a worker thread; the Games page receives cards as they
// are found and never blocks.
#pragma once
#include <QFutureWatcher>
#include <QObject>
#include <QStringList>
#include <QVector>
namespace ps2r::launcher {

struct DiscFound {
    QString path;
    QString fingerprintFile;   // which of the manifest's fingerprint files matched
    qint64 sizeBytes = 0;
};

class DiscScanner : public QObject {
    Q_OBJECT
public:
    explicit DiscScanner(QObject* parent = nullptr);
    ~DiscScanner() override;

    // The candidate files under `folders` (to `maxDepth`) and, when `removable`, every mounted removable volume;
    // `extensions` filters by suffix before any file is read.
    void start(const QStringList& folders, bool removable, int maxDepth, const QStringList& extensions,
               const QStringList& fingerprintFiles);
    bool running() const { return running_; }
    static QStringList removableVolumes();
    // The synchronous half (the worker calls it): every image in the folders that carries one of the fingerprints.
    static QVector<DiscFound> scanNow(const QStringList& folders, int maxDepth, const QStringList& extensions,
                                      const QStringList& fingerprintFiles);

signals:
    void found(const DiscFound& disc);
    void finished(int foundCount, int filesLooked);

private:
    QFutureWatcher<QVector<DiscFound>>* watcher_ = nullptr;
    bool running_ = false;
};

}  // namespace ps2r::launcher
