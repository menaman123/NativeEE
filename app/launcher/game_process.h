// app/launcher/game_process.h -- Play: the built binary launched with the disc image, the options file and the
// memory-card folder, its output in Logs/play-*.log; one game at a time; the playtime and the moment the game reached
// its title screen (the manifest's state line) reported for the card and the judge.
#pragma once
#include <QElapsedTimer>
#include <QObject>
namespace ps2r::launcher {
class ProcessRunner;
class TitleManifest;

class GameProcess : public QObject {
    Q_OBJECT
public:
    explicit GameProcess(const TitleManifest* manifest, QObject* parent = nullptr);

    bool running() const;
    QString isoPath() const { return iso_; }
    qint64 elapsedSeconds() const;
    bool reachedTitle() const { return reachedTitle_; }
    QString logPath() const;
    qint64 processId() const;

    // Starts the game; false (with *error) when the binary cannot start or one is already running.
    bool start(const QString& binary, const QString& isoPath, const QString& configPath, const QString& memoryCardsDir,
               const QStringList& extraArgs, const QString& logPath, QString* error);
    void stop();

signals:
    void started();
    void titleReached();
    void logLine(const QString& text);
    void finished(bool ok, qint64 seconds);

private:
    const TitleManifest* manifest_;
    ProcessRunner* proc_;
    QString iso_;
    QElapsedTimer timer_;
    bool reachedTitle_ = false;
};

}  // namespace ps2r::launcher
