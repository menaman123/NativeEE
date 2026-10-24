// app/launcher/fake_prepare_runner.cpp -- the sleeping runner.
#include "fake_prepare_runner.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
namespace ps2r::launcher {

FakePrepareRunner::FakePrepareRunner(const QString& spec, QObject* parent) : PrepareRunner(parent) {
    // "ok:3" | "fail:0.4" | "fail:0.4:6" | "hang" | "ok"
    const QStringList parts = spec.split(':');
    const QString mode = parts.value(0);
    if (mode == QLatin1String("fail")) {
        failAt_ = qBound(0.0, parts.value(1).toDouble(), 1.0);
        if (parts.size() > 2) seconds_ = qMax(0.1, parts.value(2).toDouble());
    } else if (mode == QLatin1String("hang")) {
        hang_ = true;
    } else if (parts.size() > 1) {
        seconds_ = qMax(0.1, parts.value(1).toDouble());
    }
    timer_ = new QTimer(this);
    timer_->setInterval(50);
    connect(timer_, &QTimer::timeout, this, &FakePrepareRunner::tick);
}

QStringList FakePrepareRunner::phases() {
    return {QStringLiteral("Reading your disc"), QStringLiteral("Checking the game's code"), QStringLiteral("Making the game for this computer"),
            QStringLiteral("Making the graphics programs"), QStringLiteral("Putting the game in its folder")};
}

bool FakePrepareRunner::available(const QString&, QString*) const { return true; }

QString FakePrepareRunner::estimateText(const QString&) const {
    if (seconds_ < 5) return QStringLiteral("a few seconds");
    if (seconds_ < 90) return QStringLiteral("about %1 seconds").arg(qRound(seconds_));
    const int m = qRound(seconds_ / 60);
    return m == 1 ? QStringLiteral("about a minute") : QStringLiteral("about %1 minutes").arg(m);
}

void FakePrepareRunner::writeLog(const QString& line) {
    QFile f(request_.logPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Append)) f.write((line + '\n').toUtf8());
}

bool FakePrepareRunner::start(const PrepareRequest& request, QString* error) {
    if (running_) { if (error) *error = QStringLiteral("already preparing"); return false; }
    request_ = request;
    QDir().mkpath(QFileInfo(request.logPath).absolutePath());
    QFile::remove(request.logPath);
    writeLog(QStringLiteral("fake prepare: disc=%1 folder=%2 target=%3").arg(request.isoPath, request.gameFolder, request.targetId));
    running_ = true;
    lastPhase_ = -1;
    clock_.start();
    timer_->start();
    return true;
}

void FakePrepareRunner::cancel() {
    if (!running_) return;
    running_ = false;
    timer_->stop();
    writeLog(QStringLiteral("cancelled"));
    emit failed(QStringLiteral("Preparing was stopped."), QStringLiteral("cancelled by the user"));
}

void FakePrepareRunner::tick() {
    if (!running_) return;
    const double elapsed = clock_.elapsed() / 1000.0;
    double fraction = hang_ ? qMin(0.35, elapsed / 10.0) : qMin(1.0, elapsed / seconds_);
    const QStringList names = phases();
    const int phase = qMin(names.size() - 1, int(fraction * names.size()));
    if (phase != lastPhase_) { lastPhase_ = phase; writeLog(names[phase]); }
    const qint64 eta = hang_ ? -1 : qMax<qint64>(0, qRound64(seconds_ - elapsed));
    if (failAt_ >= 0 && fraction >= failAt_) {
        running_ = false;
        timer_->stop();
        writeLog(QStringLiteral("error: the game's code could not be read at 0x00123400 (simulated)"));
        emit failed(QStringLiteral("The game could not be prepared: a part of your disc image could not be read. Try again; if it happens again, make a fresh image of your disc."),
                    QStringLiteral("error: the game's code could not be read at 0x00123400 (simulated)"));
        return;
    }
    emit progress(fraction, eta, names[phase]);
    if (!hang_ && fraction >= 1.0) {
        running_ = false;
        timer_->stop();
        const QString binary = request_.gameFolder + "/" + request_.binaryName;
        QDir().mkpath(request_.gameFolder);
        QFile f(binary);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write("#!/bin/sh\nexit 0\n");
            f.setPermissions(f.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
        }
        writeLog(QStringLiteral("done: %1").arg(binary));
        emit finished(binary);
    }
}

}  // namespace ps2r::launcher
