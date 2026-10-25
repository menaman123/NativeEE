// app/launcher/shipped_game_runner.cpp -- the shipped game copied into the game folder on a worker thread.
#include "shipped_game_runner.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
namespace ps2r::launcher {

namespace {
const char* kPreparing = "Preparing your game";
const char* kReady = "Your game is ready";
const char* kFailCopy = "The game could not be placed in its folder. Make sure there is free space there and try again.";
const char* kFailCancelled = "Stopped. Nothing was changed.";

void appendLog(const QString& path, const QString& line) {
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Append)) f.write((line + '\n').toUtf8());
}
}  // namespace

ShippedGameRunner::ShippedGameRunner(const QString& shippedDir, const QString& hostTarget, const QString& configFileName, QObject* parent)
    : PrepareRunner(parent), shippedDir_(shippedDir), hostTarget_(hostTarget), configFileName_(configFileName) {}

ShippedGameRunner::~ShippedGameRunner() {
    cancel_->store(true);
    if (worker_.joinable()) worker_.join();
}

QString ShippedGameRunner::findShipped(const QString& binary) {
    if (binary.isEmpty()) return QString();
    // <top>/launcher/<the launcher> or <top>/<Name>.app/Contents/MacOS/, beside <top>/game/
    const QString app = QCoreApplication::applicationDirPath();
    for (const QString& rel : {QStringLiteral("/../game"), QStringLiteral("/game"), QStringLiteral("/../../../game")}) {
        const QString dir = QDir::cleanPath(app + rel);
        if (QFileInfo::exists(dir + "/" + binary)) return dir;
    }
    return QString();
}

bool ShippedGameRunner::available(const QString& targetId, QString* why) const {
    if (!targetId.isEmpty() && targetId != hostTarget_) {
        if (why) *why = QStringLiteral("This download carries the game for this kind of computer only. Download the one for the other computer and open it there.");
        return false;
    }
    return true;
}

QString ShippedGameRunner::estimateText(const QString&) const { return QStringLiteral("a few seconds"); }

bool ShippedGameRunner::start(const PrepareRequest& request, QString* error) {
    if (running_->load()) { if (error) *error = QStringLiteral("The game is already being prepared."); return false; }
    QString why;
    if (!available(request.targetId, &why)) { if (error) *error = why; return false; }
    if (worker_.joinable()) worker_.join();
    request_ = request;
    QDir().mkpath(QFileInfo(request.logPath).absolutePath());
    QFile::remove(request.logPath);
    appendLog(request.logPath, QStringLiteral("shipped game: %1\nfolder: %2\ndisc: %3").arg(shippedDir_, request.gameFolder, request.isoPath));
    running_->store(true);
    cancel_->store(false);

    ShippedGameRunner* owner = this;   // the queued calls' context; the destructor joins the worker first
    QPointer<ShippedGameRunner> self = this;
    const QString from = shippedDir_, to = request.gameFolder, log = request.logPath, config = configFileName_;
    const QString binary = request.gameFolder + "/" + request.binaryName;
    auto running = running_, cancel = cancel_;
    worker_ = std::thread([owner, self, from, to, log, config, binary, running, cancel] {
        auto progress = [owner, self](double fraction) {
            QMetaObject::invokeMethod(owner, [self, fraction] { if (self) emit self->progress(fraction, -1, QString::fromLatin1(kPreparing)); }, Qt::QueuedConnection);
        };
        auto fail = [owner, self, running, log](const QString& text, const QString& detail) {
            appendLog(log, QStringLiteral("FAILED: ") + detail);
            running->store(false);   // before the signal: a receiver asking running() hears the truth
            QMetaObject::invokeMethod(owner, [self, text, log] { if (self) emit self->failed(text, log); }, Qt::QueuedConnection);
        };
        // every file under the shipped folder, the player's options file excepted (the launcher already wrote it)
        QStringList files;
        qint64 total = 0;
        QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            const QString rel = QDir(from).relativeFilePath(f);
            if (rel.compare(config, Qt::CaseInsensitive) == 0) continue;
            files << rel;
            total += QFileInfo(f).size();
        }
        progress(0);
        qint64 done = 0;
        for (const QString& rel : files) {
            if (cancel->load()) { fail(QString::fromLatin1(kFailCancelled), QStringLiteral("cancelled")); return; }
            const QString src = from + "/" + rel, dst = to + "/" + rel;
            QDir().mkpath(QFileInfo(dst).absolutePath());
            QFile::remove(dst);
            if (!QFile::copy(src, dst)) { fail(QString::fromLatin1(kFailCopy), QStringLiteral("cannot copy %1 -> %2").arg(src, dst)); return; }
            done += QFileInfo(src).size();
            progress(total > 0 ? double(done) / double(total) : 1.0);
        }
        if (!QFileInfo::exists(binary)) { fail(QString::fromLatin1(kFailCopy), QStringLiteral("no %1 after the copy").arg(binary)); return; }
        appendLog(log, QStringLiteral("copied %1 files (%2 bytes); ready: %3").arg(files.size()).arg(total).arg(binary));
        running->store(false);
        QMetaObject::invokeMethod(owner, [self, binary] {
            if (!self) return;
            emit self->progress(1.0, 0, QString::fromLatin1(kReady));
            emit self->finished(binary);
        }, Qt::QueuedConnection);
    });
    return true;
}

void ShippedGameRunner::cancel() { cancel_->store(true); }

}  // namespace ps2r::launcher
