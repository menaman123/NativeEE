// app/launcher/prepare_runner.h -- the one step between "here is my disc" and "Play": the launcher calls start() and
// shows ONE progress bar named "Preparing your game" from progress(); failed() is a plain sentence (the log goes
// behind Details); finished() names the executable that now plays. The launcher knows nothing of what happens inside:
// the real runner (the title's prepare pipeline, bundled with the release) plugs in through createDefault(); without
// one the launcher carries an honest "cannot prepare on this version" state rather than a fake success.
#pragma once
#include <QObject>
#include <QString>
namespace ps2r::launcher {
class TitleManifest;

struct PrepareRequest {
    QString isoPath;          // the supported disc image
    QString discSha256;       // the executable's checksum as read from it
    QString gameFolder;       // where the game lives: the executable, the options file and the sub-folders go here
    QString cacheFolder;      // the folder for anything the preparation may keep between runs
    QString targetId;         // the platform prepared for (manifest targets key); empty = this machine
    QString binaryName;       // the executable name expected in gameFolder when done (the target's `binary`)
    QString logPath;          // where the runner writes its log
    QString appDataFolder;    // the launcher's app-data root (bundles beside the launcher are found from the application path)
};

class PrepareRunner : public QObject {
    Q_OBJECT
public:
    explicit PrepareRunner(QObject* parent = nullptr) : QObject(parent) {}
    // True when a start() could succeed on this installation (the bundle for the target is present).
    virtual bool available(const QString& targetId, QString* why) const = 0;
    // A plain-text estimate for the progress caption before the first progress() ("about 4 minutes"); empty = none.
    virtual QString estimateText(const QString& targetId) const = 0;
    virtual bool start(const PrepareRequest& request, QString* error) = 0;
    virtual void cancel() = 0;
    virtual bool running() const = 0;
    virtual QString logPath() const = 0;
    // What a title ships as the runner: PS2R_LAUNCHER_FAKE_PREPARE (tests / screens) selects the fake; else the
    // registered real runner; else the "not available" runner. The prepare track registers its runner here.
    static PrepareRunner* createDefault(const TitleManifest* manifest, QObject* parent);

signals:
    void progress(double fraction, qint64 etaSeconds, const QString& phaseText);   // fraction 0..1; eta < 0 = unknown
    void failed(const QString& plainText, const QString& log);
    void finished(const QString& binaryPath);
};

}  // namespace ps2r::launcher
