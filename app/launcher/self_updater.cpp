// app/launcher/self_updater.cpp -- the release check, the download, its checksum, the unpacking and the copy script.
#include "self_updater.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QUrl>
#include "shipped_game_runner.h"
#include "title_manifest.h"
namespace ps2r::launcher {

namespace {
const char* kFailInstall = "The update could not be installed. The launcher carries on with this version.";
const char* kFailDownload = "The update could not be downloaded. The launcher carries on with this version; it tries again next time.";
const char* kFailChecksum = "The downloaded update was not complete. The launcher carries on with this version; it tries again next time.";
const char* kFailFolder = "The update could not be unpacked. Make sure there is free space on this computer and try again.";

void appendLog(const QString& path, const QString& line) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Append))
        f.write((QDateTime::currentDateTime().toString(Qt::ISODate) + ' ' + line + '\n').toUtf8());
}

QNetworkRequest request(const QUrl& url, int timeoutMs) {
    QNetworkRequest r(url);
    r.setRawHeader("Accept", "application/vnd.github+json");   // lint: allow
    r.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("NativeEE-Launcher"));
    r.setTransferTimeout(timeoutMs);
    return r;
}

// A path inside a single-quoted PowerShell / POSIX shell string.
QString psQuote(const QString& s) { return "'" + QString(s).replace("'", "''") + "'"; }
QString shQuote(const QString& s) { return "'" + QString(s).replace("'", "'\\''") + "'"; }
}  // namespace

SelfUpdater::Pick SelfUpdater::pickUpdate(const QByteArray& listing, const QString& currentTag,
                                          const std::function<QString(const QString&)>& assetName) {
    const QJsonArray releases = QJsonDocument::fromJson(listing).array();
    int current = -1;
    for (int i = 0; i < releases.size(); ++i)
        if (releases[i].toObject().value("tag_name").toString() == currentTag) { current = i; break; }
    if (current < 0) return Pick{};
    for (int i = 0; i < current; ++i) {
        const QJsonObject r = releases[i].toObject();
        if (r.value("draft").toBool()) continue;
        const QString tag = r.value("tag_name").toString();
        const QString name = assetName(tag);
        Pick p;
        for (const QJsonValue& a : r.value("assets").toArray()) {
            const QJsonObject o = a.toObject();
            const QString n = o.value("name").toString();
            if (n == name) {
                p.assetUrl = o.value("browser_download_url").toString();
                p.size = o.value("size").toInteger();
            } else if (n == name + ".sha256") {   // lint: allow
                p.checksumUrl = o.value("browser_download_url").toString();
            }
        }
        if (p.assetUrl.isEmpty()) continue;   // a release for other computers only
        p.tag = tag;
        p.pageUrl = r.value("html_url").toString();
        return p;
    }
    return Pick{};
}

SelfUpdater::SelfUpdater(const TitleManifest* manifest, const QString& currentTag, const QString& hostTarget, const QString& workDir,
                         const QString& logPath, QObject* parent)
    : QObject(parent), manifest_(manifest), currentTag_(currentTag), hostTarget_(hostTarget), workDir_(workDir), logPath_(logPath),
      net_(new QNetworkAccessManager(this)) {}

SelfUpdater::~SelfUpdater() {
    cancelled_ = true;
    if (reply_) reply_->abort();
    if (unpacker_) unpacker_->kill();
}

QString SelfUpdater::bundleRoot() const {
    const TargetSpec* t = manifest_->target(hostTarget_);
    const QString game = ShippedGameRunner::findShipped(t ? t->binary : manifest_->binaryName());
    if (game.isEmpty()) return QString();
    const QString root = QFileInfo(game).absolutePath();
    if (!QCoreApplication::applicationFilePath().startsWith(root + "/")) return QString();
    return root;
}

bool SelfUpdater::enabled(QString* why) const {
    auto no = [why](const QString& text) { if (why) *why = text; return false; };
    if (qEnvironmentVariableIsSet("PS2R_LAUNCHER_NO_UPDATE")) return no(QStringLiteral("turned off (PS2R_LAUNCHER_NO_UPDATE)"));
    auto* gui = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (!gui || gui->platformName() == "offscreen" || gui->platformName() == "minimal") return no(QStringLiteral("no window"));
    if (currentTag_.isEmpty() || manifest_->releaseRepo().isEmpty()) return no(QStringLiteral("no release to compare with"));
    const QString root = bundleRoot();
    if (root.isEmpty()) return no(QStringLiteral("not opened from a release folder"));
    QFile probe(root + "/.update-probe");
    if (!probe.open(QIODevice::WriteOnly)) return no(QStringLiteral("the release folder is read-only: ") + root);
    probe.close();
    probe.remove();
    return true;
}

void SelfUpdater::check() {
    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/releases?per_page=30").arg(manifest_->releaseRepo()));
    appendLog(logPath_, QStringLiteral("launcher %1 (%2): checking %3").arg(currentTag_, hostTarget_, url.toString()));
    reply_ = net_->get(request(url, 8000));
    connect(reply_, &QNetworkReply::finished, this, [this, reply = reply_.data()] {
        reply->deleteLater();
        if (cancelled_) return;
        if (reply->error() != QNetworkReply::NoError) {
            appendLog(logPath_, QStringLiteral("no answer (%1); carrying on").arg(reply->errorString()));
            emit noUpdate();
            return;
        }
        pick_ = pickUpdate(reply->readAll(), currentTag_,
                           [this](const QString& tag) { return manifest_->releaseAsset(tag, hostTarget_); });
        if (!pick_.valid()) {
            appendLog(logPath_, QStringLiteral("up to date"));
            emit noUpdate();
            return;
        }
        appendLog(logPath_, QStringLiteral("update %1: %2 (%3 bytes)").arg(pick_.tag, pick_.assetUrl).arg(pick_.size));
        emit updateAvailable(pick_.tag);
    });
}

QString SelfUpdater::zipPath() const { return workDir_ + "/" + manifest_->releaseAsset(pick_.tag, hostTarget_); }
QString SelfUpdater::stagingDir() const { return workDir_ + "/unpacked"; }

void SelfUpdater::install() {
    if (!pick_.valid()) { fail(QString::fromLatin1(kFailInstall), QStringLiteral("nothing to install")); return; }
    QDir(workDir_).removeRecursively();
    QDir().mkpath(workDir_);
    file_.setFileName(zipPath());
    if (!file_.open(QIODevice::WriteOnly)) { fail(QString::fromLatin1(kFailFolder), QStringLiteral("cannot write ") + file_.fileName()); return; }
    emit progress(0, pick_.size > 0 ? pick_.size : -1, QStringLiteral("Downloading the new version"));
    reply_ = net_->get(request(QUrl(pick_.assetUrl), 30000));
    connect(reply_, &QNetworkReply::readyRead, this, [this, reply = reply_.data()] { file_.write(reply->readAll()); });
    connect(reply_, &QNetworkReply::downloadProgress, this, [this](qint64 done, qint64 total) {
        emit progress(done, total > 0 ? total : (pick_.size > 0 ? pick_.size : -1), QStringLiteral("Downloading the new version"));
    });
    connect(reply_, &QNetworkReply::finished, this, &SelfUpdater::downloaded);
}

void SelfUpdater::downloaded() {
    QNetworkReply* reply = reply_.data();
    if (!reply) return;
    reply->deleteLater();
    if (cancelled_) { file_.close(); return; }
    file_.write(reply->readAll());
    file_.close();
    if (reply->error() != QNetworkReply::NoError) { fail(QString::fromLatin1(kFailDownload), reply->errorString()); return; }
    if (pick_.size > 0 && file_.size() != pick_.size) {
        fail(QString::fromLatin1(kFailChecksum), QStringLiteral("%1 bytes, the release lists %2").arg(file_.size()).arg(pick_.size));
        return;
    }
    if (pick_.checksumUrl.isEmpty()) { verify(QByteArray()); return; }
    reply_ = net_->get(request(QUrl(pick_.checksumUrl), 15000));
    connect(reply_, &QNetworkReply::finished, this, [this, sums = reply_.data()] {
        sums->deleteLater();
        if (cancelled_) return;
        if (sums->error() != QNetworkReply::NoError) { fail(QString::fromLatin1(kFailDownload), sums->errorString()); return; }
        verify(sums->readAll().simplified().split(' ').value(0).toLower());
    });
}

void SelfUpdater::verify(const QByteArray& expected) {
    emit progress(-1, -1, QStringLiteral("Checking the download"));
    if (!expected.isEmpty()) {
        QFile f(zipPath());
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!f.open(QIODevice::ReadOnly) || !hash.addData(&f)) { fail(QString::fromLatin1(kFailFolder), QStringLiteral("cannot read ") + f.fileName()); return; }
        const QByteArray got = hash.result().toHex();
        if (got != expected) { fail(QString::fromLatin1(kFailChecksum), QStringLiteral("checksum %1, the release lists %2").arg(QString::fromLatin1(got), QString::fromLatin1(expected))); return; }
        appendLog(logPath_, QStringLiteral("checksum ok"));
    }
    unpack();
}

void SelfUpdater::unpack() {
    emit progress(-1, -1, QStringLiteral("Unpacking the new version"));
    QDir(stagingDir()).removeRecursively();
    QDir().mkpath(stagingDir());
    const QString zip = QDir::toNativeSeparators(zipPath()), dest = QDir::toNativeSeparators(stagingDir());
    QString program;
    QStringList args;
#ifdef Q_OS_WIN
    // Windows' own tar reads zips; the PowerShell cmdlet is the slower second choice
    const QString tar = qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows")) + "\\System32\\tar.exe";
    if (!triedFallback_ && QFileInfo::exists(tar)) {
        program = tar;
        args = {QStringLiteral("-xf"), zip, QStringLiteral("-C"), dest};
    } else {
        triedFallback_ = true;
        program = QStringLiteral("powershell.exe");
        args = {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                QStringLiteral("-Command"), QStringLiteral("Expand-Archive -LiteralPath %1 -DestinationPath %2 -Force").arg(psQuote(zip), psQuote(dest))};
    }
#elif defined(Q_OS_MACOS)
    triedFallback_ = true;
    program = QStringLiteral("/usr/bin/ditto");
    args = {QStringLiteral("-x"), QStringLiteral("-k"), zip, dest};
#else
    triedFallback_ = true;
    program = QStringLiteral("unzip");
    args = {QStringLiteral("-q"), QStringLiteral("-o"), zip, QStringLiteral("-d"), dest};
#endif
    appendLog(logPath_, QStringLiteral("unpacking with %1").arg(program));
    unpacker_ = new QProcess(this);
    connect(unpacker_, &QProcess::finished, this, [this, p = unpacker_.data()](int code, QProcess::ExitStatus status) {
        p->deleteLater();
        if (cancelled_) return;
        const bool ok = status == QProcess::NormalExit && code == 0;
        if (!ok) appendLog(logPath_, QStringLiteral("unpacking failed (exit %1): %2").arg(code).arg(QString::fromLocal8Bit(p->readAllStandardError()).trimmed()));
        unpacked(ok);
    });
    connect(unpacker_, &QProcess::errorOccurred, this, [this, p = unpacker_.data()](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || cancelled_) return;
        p->deleteLater();
        appendLog(logPath_, QStringLiteral("unpacker did not start: %1").arg(p->errorString()));
        unpacked(false);
    });
    unpacker_->start(program, args);
}

void SelfUpdater::unpacked(bool ok) {
    const QString staged = stagingDir() + "/" + manifest_->bundleFolder(hostTarget_);
    const QString root = bundleRoot();
    const QString self = QDir(root).relativeFilePath(QCoreApplication::applicationFilePath());
    const bool complete = ok && QFileInfo::exists(staged + "/" + self) && QFileInfo(staged + "/game").isDir();
    if (!complete) {
        if (!triedFallback_) { triedFallback_ = true; unpack(); return; }
        fail(QString::fromLatin1(kFailFolder), QStringLiteral("no %1 under %2").arg(self, staged));
        return;
    }
    QFile::remove(zipPath());   // the space back before the copy
    startCopy();
}

void SelfUpdater::startCopy() {
    const QString staged = QDir::toNativeSeparators(stagingDir() + "/" + manifest_->bundleFolder(hostTarget_));
    const QString root = QDir::toNativeSeparators(bundleRoot());
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString work = QDir::toNativeSeparators(workDir_);
    const QString log = QDir::toNativeSeparators(logPath_);
    const qint64 pid = QCoreApplication::applicationPid();
    QString script, program;
    QStringList args;
#ifdef Q_OS_WIN
    // waits for this launcher to close, copies the new folders over the old ones (retrying while a file is still
    // held), removes the download and opens the launcher again -- whether or not the copy went through
    script = QFileInfo(workDir_).absolutePath() + "/update.ps1";
    const QString text = QStringLiteral(
        "$log = %1\n"
        "function Say($t) { Add-Content -LiteralPath $log -Value ((Get-Date -Format s) + ' ' + $t) }\n"
        "Say 'waiting for the launcher (%2) to close'\n"
        "try { Wait-Process -Id %2 -Timeout 120 -ErrorAction Stop } catch { }\n"
        "$ok = $false\n"
        "for ($i = 0; $i -lt 30 -and -not $ok; $i++) {\n"
        "  & robocopy %3 %4 /E /R:2 /W:1 /NP /NFL /NDL /NJH /NJS | Out-Null\n"
        "  if ($LASTEXITCODE -lt 8) { $ok = $true } else { Say \"copy attempt $i failed (robocopy $LASTEXITCODE)\"; Start-Sleep -Seconds 1 }\n"
        "}\n"
        "if ($ok) { Say 'installed %5' } else { Say 'the copy did not go through; the launcher keeps its version' }\n"
        "Remove-Item -LiteralPath %6 -Recurse -Force -ErrorAction SilentlyContinue\n"
        "Start-Process -FilePath %7\n")
        .arg(psQuote(log)).arg(pid).arg(psQuote(staged), psQuote(root), pick_.tag, psQuote(work), psQuote(exe));
    QFile f(script);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { fail(QString::fromLatin1(kFailInstall), QStringLiteral("cannot write ") + script); return; }
    f.write("\xEF\xBB\xBF");   // UTF-8 with a byte-order mark: PowerShell then reads any path
    f.write(text.toUtf8());
    f.close();
    program = QStringLiteral("powershell.exe");
    args = {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
            QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"), QStringLiteral("-File"), QDir::toNativeSeparators(script)};
#else
    script = QFileInfo(workDir_).absolutePath() + "/update.sh";
    const QString text = QStringLiteral(
        "#!/bin/sh\n"
        "log=%1\n"
        "echo \"$(date) waiting for the launcher (%2) to close\" >> \"$log\"\n"
        "n=0; while kill -0 %2 2>/dev/null && [ $n -lt 120 ]; do sleep 1; n=$((n+1)); done\n"
        "if cp -R %3/. %4/ >> \"$log\" 2>&1; then echo \"$(date) installed %5\" >> \"$log\"; "
        "else echo \"$(date) the copy did not go through\" >> \"$log\"; fi\n"
        "rm -rf %6\n"
        "%7 >/dev/null 2>&1 &\n")
        .arg(shQuote(log)).arg(pid).arg(shQuote(staged), shQuote(root), pick_.tag, shQuote(work), shQuote(exe));
    QFile f(script);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { fail(QString::fromLatin1(kFailInstall), QStringLiteral("cannot write ") + script); return; }
    f.write(text.toUtf8());
    f.close();
    program = QStringLiteral("/bin/sh");
    args = {script};
#endif
    appendLog(logPath_, QStringLiteral("handing over to %1").arg(script));
    QProcess p;
    p.setProgram(program);
    p.setArguments(args);
    p.setWorkingDirectory(QFileInfo(workDir_).absolutePath());
#ifdef Q_OS_WIN
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) { a->flags |= 0x08000000; });   // CREATE_NO_WINDOW
#endif
    if (!p.startDetached()) { fail(QString::fromLatin1(kFailInstall), QStringLiteral("cannot start ") + program); return; }
    emit progress(-1, -1, QStringLiteral("Restarting"));
    emit restartRequired();
}

void SelfUpdater::cancel() {
    cancelled_ = true;
    if (reply_) reply_->abort();
    if (unpacker_) unpacker_->kill();
    file_.close();
    appendLog(logPath_, QStringLiteral("cancelled by the player"));
    QDir(workDir_).removeRecursively();
}

void SelfUpdater::fail(const QString& text, const QString& detail) {
    appendLog(logPath_, QStringLiteral("FAILED: ") + detail);
    QDir(workDir_).removeRecursively();
    emit failed(text);
}

}  // namespace ps2r::launcher
