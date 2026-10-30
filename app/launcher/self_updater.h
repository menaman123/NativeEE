// app/launcher/self_updater.h -- the launcher keeps itself and the game beside it current. At start (a window, a
// release build: version() is a release tag) it asks the title's release repository (release.repo, GitHub Releases)
// for its releases; when one newer than this launcher's carries this computer's download (release.asset_pattern), it
// downloads that zip, checks it against the release's .sha256 file when there is one, unpacks it, and hands the copy
// to a small script: the script waits for the launcher to close, copies the new folders (launcher/, game/) over the
// ones beside this launcher and opens the launcher again. The player's game folder and the launcher's own state are
// not touched here; the new launcher refreshes the game folder itself (LauncherApp::refreshGame).
//
// Off the network (or anything else going wrong before the copy) the launcher carries on as it is. A development
// build (its version is not one of the releases) and a test run (an offscreen window, PS2R_LAUNCHER_NO_UPDATE, the
// launcher's --no-update) never update.
#pragma once
#include <functional>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QString>
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
namespace ps2r::launcher {

class TitleManifest;

class SelfUpdater : public QObject {
    Q_OBJECT
public:
    // The release to update to, from the listing.
    struct Pick {
        QString tag, assetUrl, checksumUrl, pageUrl;
        qint64 size = 0;
        bool valid() const { return !tag.isEmpty(); }
    };
    // From a GitHub Releases listing (newest first): the newest release ahead of `currentTag` carrying the download
    // `assetName(tag)`. Invalid when the current release is the newest, or when it is not in the listing (a
    // development build, or a release that was withdrawn).
    static Pick pickUpdate(const QByteArray& listing, const QString& currentTag, const std::function<QString(const QString&)>& assetName);

    // `workDir`: where the download and the unpacked copy go (the launcher's app-data folder); `logPath`: the
    // script's log.
    SelfUpdater(const TitleManifest* manifest, const QString& currentTag, const QString& hostTarget, const QString& workDir,
                const QString& logPath, QObject* parent = nullptr);
    ~SelfUpdater() override;

    // Whether this launcher updates itself; else *why says why not (for the log).
    bool enabled(QString* why = nullptr) const;
    // The folder holding this launcher and the game beside it (the release's top folder); empty outside that layout.
    QString bundleRoot() const;
    // Asks for the releases: updateAvailable() or noUpdate().
    void check();
    // Downloads, checks and unpacks the release check() found, then starts the copy script: restartRequired() (the
    // launcher must quit now) or failed().
    void install();
    void cancel();
    Pick pick() const { return pick_; }

signals:
    void noUpdate();
    void updateAvailable(const QString& tag);
    // `total` < 0 while not known.
    void progress(qint64 done, qint64 total, const QString& phase);
    void restartRequired();
    void failed(const QString& text);

private:
    void downloaded();
    void verify(const QByteArray& expected);
    void unpack();
    void unpacked(bool ok);
    void startCopy();
    void fail(const QString& text, const QString& detail);
    QString zipPath() const;
    QString stagingDir() const;

    const TitleManifest* manifest_;
    QString currentTag_, hostTarget_, workDir_, logPath_;
    QNetworkAccessManager* net_;
    QPointer<QNetworkReply> reply_;
    QPointer<QProcess> unpacker_;
    QFile file_;
    Pick pick_;
    bool cancelled_ = false;
    bool triedFallback_ = false;
};

}  // namespace ps2r::launcher
