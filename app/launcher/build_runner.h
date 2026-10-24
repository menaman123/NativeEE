// app/launcher/build_runner.h -- one target's build: the title's preset or configure line run into
// Builds/<title>/<target> under app-data, the progress bar driven by Ninja's `[n/m]` lines with an elapsed /
// remaining estimate, the outcome recorded in build.json (source commit, toolchain pins, binary, size) so a rebuild
// happens only when the pinned source or toolchain changed.
#pragma once
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QVector>
#include "title_manifest.h"
namespace ps2r::launcher {
class AppData;
class Installer;
class ProcessRunner;

struct BuildRecord {
    bool ok = false;
    QString sourceCommit;
    QString toolchainPins;
    QDateTime builtAt;
    QString binaryPath;     // the runnable binary (macos / windows)
    QString artifactPath;   // the artefact (vpk / zip) or the binary
    qint64 artifactBytes = 0;
    qint64 seconds = 0;
    QString failure;        // the first error line, in plain words
};

class BuildRunner : public QObject {
    Q_OBJECT
public:
    BuildRunner(AppData* appData, const TitleManifest* manifest, Installer* installer, QObject* parent = nullptr);

    QString buildDir(const QString& targetId) const;
    BuildRecord record(const QString& targetId) const;
    // Built, current with the installed source and toolchain pins, and the artefact exists.
    bool upToDate(const QString& targetId) const;
    bool running() const { return running_; }
    QString runningTarget() const { return target_; }
    QString currentLogPath() const;
    qint64 elapsedSeconds() const { return running_ ? timer_.elapsed() / 1000 : 0; }
    static bool hostCanBuild(const TargetSpec& target, QString* reason);
    static QString hostTargetId();   // macos / windows / linux

    void start(const QString& targetId);
    void cancel();

    static QString durationText(qint64 seconds);

signals:
    void progress(const QString& text, int done, int total, qint64 elapsedSeconds, qint64 remainingSeconds);
    void logLine(const QString& text);
    void finished(const QString& targetId, bool ok, const BuildRecord& record);

private:
    void configure();
    void build();
    void stage();
    void finish(bool ok, const QString& failure);
    void saveRecord(const QString& targetId, const BuildRecord& r) const;
    QString plainFailure() const;
    QStringList substitute(const QStringList& args) const;

    AppData* appData_;
    const TitleManifest* manifest_;
    Installer* installer_;
    ProcessRunner* proc_ = nullptr;
    QString target_;
    const TargetSpec* spec_ = nullptr;
    QElapsedTimer timer_;
    QVector<QPair<qint64, int>> recent_;   // (ms, done) of the last ninja steps for the estimate
    int lastDone_ = 0, lastTotal_ = 0;
    bool running_ = false;
};

}  // namespace ps2r::launcher
