// app/launcher/game_process.cpp -- the running game.
#include "game_process.h"
#include <QFileInfo>
#include "process_runner.h"
#include "title_manifest.h"
namespace ps2r::launcher {

GameProcess::GameProcess(const TitleManifest* manifest, QObject* parent) : QObject(parent), manifest_(manifest) {
    proc_ = new ProcessRunner(this);
    connect(proc_, &ProcessRunner::line, this, [this](const QString& l) { emit logLine(l); });
    connect(proc_, &ProcessRunner::finished, this, [this](bool ok, int) { emit finished(ok, timer_.elapsed() / 1000); });
}

bool GameProcess::running() const { return proc_->running(); }
qint64 GameProcess::elapsedSeconds() const { return running() ? timer_.elapsed() / 1000 : 0; }
QString GameProcess::logPath() const { return proc_->logPath(); }
qint64 GameProcess::processId() const { return proc_->processId(); }

bool GameProcess::start(const QString& executable, const QString& isoPath, const QStringList& args, const QString& logPath, QString* error) {
    if (running()) { if (error) *error = QStringLiteral("The game is already running."); return false; }
    if (!QFileInfo(executable).isExecutable()) { if (error) *error = QStringLiteral("The game is not in its folder any more. Prepare it again."); return false; }
    if (!QFileInfo::exists(isoPath)) { if (error) *error = QStringLiteral("The disc image %1 is gone.").arg(QFileInfo(isoPath).fileName()); return false; }
    iso_ = isoPath;
    proc_->setLogPath(logPath);
    proc_->setWorkingDirectory(QFileInfo(executable).absolutePath());
    timer_.start();
    if (!proc_->start(executable, args, error)) return false;
    emit started();
    return true;
}

void GameProcess::stop() { proc_->cancel(); }

}  // namespace ps2r::launcher
