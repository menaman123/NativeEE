// app/launcher/game_process.cpp -- the running game.
#include "game_process.h"
#include <QFileInfo>
#include "process_runner.h"
#include "title_manifest.h"
namespace ps2r::launcher {

GameProcess::GameProcess(const TitleManifest* manifest, QObject* parent) : QObject(parent), manifest_(manifest) {
    proc_ = new ProcessRunner(this);
    connect(proc_, &ProcessRunner::line, this, [this](const QString& l) {
        emit logLine(l);
        if (!reachedTitle_ && !manifest_->titleStateLine().isEmpty() && l.contains(manifest_->titleStateLine()) &&
            l.contains(manifest_->titleStateName())) {
            reachedTitle_ = true;
            emit titleReached();
        }
    });
    connect(proc_, &ProcessRunner::finished, this, [this](bool ok, int) { emit finished(ok, timer_.elapsed() / 1000); });
}

bool GameProcess::running() const { return proc_->running(); }
qint64 GameProcess::elapsedSeconds() const { return running() ? timer_.elapsed() / 1000 : 0; }
QString GameProcess::logPath() const { return proc_->logPath(); }
qint64 GameProcess::processId() const { return proc_->processId(); }

bool GameProcess::start(const QString& binary, const QString& isoPath, const QString& configPath, const QString& memoryCardsDir,
                        const QStringList& extraArgs, const QString& logPath, QString* error) {
    if (running()) { if (error) *error = QStringLiteral("the game is already running"); return false; }
    if (!QFileInfo(binary).isExecutable()) { if (error) *error = QStringLiteral("%1 is not there -- build first").arg(binary); return false; }
    if (!QFileInfo::exists(isoPath)) { if (error) *error = QStringLiteral("the disc image %1 is gone").arg(isoPath); return false; }
    QStringList args;
    args << manifest_->runIsoFlag() << isoPath;
    if (!configPath.isEmpty() && !manifest_->runConfigFlag().isEmpty()) args << manifest_->runConfigFlag() << configPath;
    if (!memoryCardsDir.isEmpty() && !manifest_->runMemoryCardsFlag().isEmpty()) args << manifest_->runMemoryCardsFlag() << memoryCardsDir;
    args << manifest_->runArgs() << extraArgs;
    iso_ = isoPath;
    reachedTitle_ = false;
    proc_->setLogPath(logPath);
    proc_->setWorkingDirectory(QFileInfo(binary).absolutePath());
    timer_.start();
    if (!proc_->start(binary, args, error)) return false;
    emit started();
    return true;
}

void GameProcess::stop() { proc_->cancel(); }

}  // namespace ps2r::launcher
