// app/launcher/game_process.h -- Play: the prepared executable launched with the arguments the manifest's `run.args`
// describe (the disc image, the options file, the folders), its output in Logs/play-*.log; one game at a time; the
// playtime reported for the card.
#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
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
    QString logPath() const;
    qint64 processId() const;

    // Starts the game; false (with *error) when the executable cannot start or one is already running.
    bool start(const QString& executable, const QString& isoPath, const QStringList& args, const QString& logPath, QString* error);
    void stop();

signals:
    void started();
    void logLine(const QString& text);
    void finished(bool ok, qint64 seconds);

private:
    const TitleManifest* manifest_;
    ProcessRunner* proc_;
    QString iso_;
    QElapsedTimer timer_;
};

}  // namespace ps2r::launcher
